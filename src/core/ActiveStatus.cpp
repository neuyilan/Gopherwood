/********************************************************************
 * 2017 -
 * open source under Apache License Version 2.0
 ********************************************************************/
/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "common/Configuration.h"
#include "common/Exception.h"
#include "common/ExceptionInternal.h"
#include "common/Logger.h"
#include "core/ActiveStatus.h"
#include "file/FileSystem.h"

namespace Gopherwood {
namespace Internal {

ActiveStatus::ActiveStatus(FileId fileId,
                           shared_ptr<SharedMemoryContext> sharedMemoryContext,
                           bool isCreate,
                           ActiveStatusType type,
                           int localSpaceFD) :
        mFileId(fileId),
        mSharedMemoryContext(sharedMemoryContext) {
    mIsWrite = (type == ActiveStatusType::writeFile);
    mIsDelete = (type == ActiveStatusType::deleteFile);

    /* check file exist if not creating a new file */
    std::string manifestFileName = Manifest::getManifestFileName(mSharedMemoryContext->getWorkDir(), mFileId);
    if (!isCreate && access(manifestFileName.c_str(), F_OK) == -1) {
        THROW(GopherwoodInvalidParmException,
              "[ActiveStatus::ActiveStatus] File does not exist %s",
              manifestFileName.c_str());
    }
    mManifest = shared_ptr<Manifest>(new Manifest(manifestFileName));
    mLRUCache = shared_ptr<LRUCache<int, int>>(new LRUCache<int, int>(Configuration::getCurQuotaSize()));
    mOssWriter = shared_ptr<OssBlockWriter>(new OssBlockWriter(FileSystem::OSS_CONTEXT, localSpaceFD));

    mNumBlocks = 0;
    mPos = 0;
    mEof = 0;
    mBucketSize = Configuration::LOCAL_BUCKET_SIZE;

    SHARED_MEM_BEGIN
        registInSharedMem();
    SHARED_MEM_END
}

/* Shared Memroy activeStatus field will maintain all connected files */
void ActiveStatus::registInSharedMem() {
    mActiveId = mSharedMemoryContext->regist(getpid(), mFileId, mIsWrite, mIsDelete);
    if (mActiveId == -1) {
        THROW(GopherwoodSharedMemException,
              "[ActiveStatus::registInSharedMem] Exceed max connection limitation %d",
              mSharedMemoryContext->getNumMaxActiveStatus());
    }
    LOG(INFO, "[ActiveStatus]          |"
              "Registered successfully, ActiveID=%d, PID=%d", mActiveId, getpid());
}

void ActiveStatus::unregistInSharedMem() {
    if (mActiveId == -1)
        return;

    int rc = mSharedMemoryContext->unregist(mActiveId, getpid());
    if (rc != 0) {
        mSharedMemoryContext->unlock();
        THROW(GopherwoodSharedMemException,
              "[ActiveStatus::unregistInSharedMem] connection info mismatch with SharedMem ActiveId=%d, PID=%d",
              mActiveId, getpid());
    }
    LOG(INFO, "[ActiveStatus]          |"
              "Unregistered successfully, ActiveID=%d, PID=%d", mActiveId, getpid());
    mActiveId = -1;
}

int64_t ActiveStatus::getPosition() {
    return mPos;
}

void ActiveStatus::setPosition(int64_t pos) {
    mPos = pos;
    if (mPos > mEof) {
        mEof = mPos;
    }
    LOG(INFO, "[ActiveStatus]          |"
              "Update ActiveStatus position, pos=%ld, eof=%ld",
        mPos, mEof);
}

int64_t ActiveStatus::getEof() {
    return mEof;
}

Block ActiveStatus::getCurBlock() {
    return mBlockArray[mPos / mBucketSize];
}

int64_t ActiveStatus::getCurBlockOffset() {
    return mPos % mBucketSize;
}

std::string ActiveStatus::getManifestFileName(FileId fileId) {
    std::stringstream ss;
    ss << mSharedMemoryContext->getWorkDir() << Configuration::MANIFEST_FOLDER << '/' << mFileId.hashcode << '-'
       << mFileId.collisionId;
    return ss.str();
}


/* [IMPORTANT] This is the main entry point of adjusting active status. OutpuStream/InputStream
 * will call this function to write/read to multi blocks. When mPos reaches block not activated
 * by current file instance, ActiveStatus will adjust the block status.*/
BlockInfo ActiveStatus::getCurBlockInfo() {
    int curBlockIndex = mPos / mBucketSize;

    /* adjust the active block status */
    adjustActiveBlock(curBlockIndex);

    /* build the block info */
    BlockInfo info;
    Block block = getCurBlock();
    info.fileId = mFileId;
    info.blockId = block.blockId;
    info.bucketId = block.bucketId;
    info.isLocal = block.isLocal;
    info.offset = getCurBlockOffset();
    return info;
}

void ActiveStatus::adjustActiveBlock(int curBlockInd) {
    if (curBlockInd + 1 > mNumBlocks) {
        extendOneBlock();
    } else if (!mBlockArray[curBlockInd].isMyActive) {
        /* need to mark the block to my active block */
        if (!mBlockArray[curBlockInd].isLocal) {
            /* load the bucket back
             * TODO： Implement this, middle priority */
            THROW(GopherwoodNotImplException, "Not implemented yet!");
        } else if (mBlockArray[curBlockInd].state == BUCKET_USED) {
            activateBlock(curBlockInd);
        } else if (mBlockArray[curBlockInd].state == BUCKET_ACTIVE &&
                   !mBlockArray[curBlockInd].isMyActive) {
            activateBlock(curBlockInd);
        }
    }
}

/* All block activation should follow these steps:
 * 1. Check shared memory for the current quota
 * 2(a). If still have quota available, and have 0 or 2 available
 *       Then -> acquire more buckets for preAllocatedBlocks
 * 2(b). If still have quota available, and no 0 or 2 available
 *       Then -> play with current owned buckets
 * 3(a). If quota equal to current active block num, and have 0 available
 *       Then -> inactivate blocks from LRU first, then acquire new blocks
 * 3(b). If quota equal to current active block num, and no 0 available
 *       Then -> play with current owned buckets
 * 4. If quota smaller than current active block num,
 *       Then -> release blocks and use own quota
 * Notes: When got chance to acquire new blocks, active status will try to
 *        pre acquire a number of buckets to reduce the Shared Memory contention. */
void ActiveStatus::acquireNewBlocks() {
    std::vector<Block> blocksForLog;
    std::vector<int32_t> newBuckets;
    BlockInfo evictBlockInfo;
    evictBlockInfo.bucketId = -1;
    bool evicting = false;

    uint32_t numToAcquire = 0;
    uint32_t numToInactivate = 0;

    uint32_t numFreeBuckets;
    uint32_t numUsedBuckets;
    uint32_t numAvailable;

    SHARED_MEM_BEGIN
        uint32_t quota = mSharedMemoryContext->calcDynamicQuotaNum();
        numFreeBuckets = mSharedMemoryContext->getFreeBucketNum();
        numUsedBuckets = mSharedMemoryContext->getUsedBucketNum();
        numAvailable = numFreeBuckets + numUsedBuckets;

        /************************************************
         * Step1: Determine the acquire policy first
         ************************************************/
        if (mLRUCache->size() < quota) {
            if (numAvailable > 0) {
                /* 2(a) acquire more buckets for preAllocatedBlocks */
                uint32_t tmpAcquire = quota - mLRUCache->size();
                if (tmpAcquire > Configuration::PRE_ALLOCATE_BUCKET_NUM){
                    tmpAcquire = Configuration::PRE_ALLOCATE_BUCKET_NUM;
                }
                numToAcquire = numAvailable > tmpAcquire ? tmpAcquire : numAvailable;
                /* it might exceed quota after acquired new buckets */
                numToInactivate = (mLRUCache->size() + numToAcquire) > quota ?
                                   mLRUCache->size() + numToAcquire - quota : 0;
            } else {
                /* 2(b) play with current owned buckets */
                numToInactivate = 0;
                numToAcquire = 0;
            }
        } else if (mLRUCache->size() == quota) {
            if (numFreeBuckets > 0) {
                /* 3(a) inactivate blocks from LRU first, then acquire new blocks */
                uint32_t tmpAcquire = quota > Configuration::PRE_ALLOCATE_BUCKET_NUM ?
                                 Configuration::PRE_ALLOCATE_BUCKET_NUM : quota;
                numToAcquire = numFreeBuckets > tmpAcquire ? tmpAcquire : numFreeBuckets;
                numToInactivate = numToAcquire;
            } else {
                /* 3(b) play with current owned buckets */
                numToInactivate = 0;
                numToAcquire = 0;
            }
        } else {
            /* 4 release blocks and use own quota*/
            numToInactivate = mLRUCache->size() - quota;
            numToAcquire = 0;
        }
        LOG(INFO, "[ActiveStatus]          |"
                  "Calculate new quota size, newQuota=%u, curQuota=%ld, numAvailables=%d, "
                  "inactivate=%d, acquire %d", quota, mLRUCache->size(), numAvailable,
            numToInactivate, numToAcquire);

        /**************************************************************
         * Step2: inactive/get free buckets/start evict 1st used bucket
         * NOTE: Only get free buckets since 3(a) is determined
         * by numFreeBuckets. We should do this consistently
         * before release SharedMemory lock.
         **************************************************************/
        /* release first */
        if (numToInactivate > 0) {
            std::vector<Block> blocksToInactivate;
            std::vector<int> blockIds = mLRUCache->removeNumOfKeys(numToInactivate);

            for (int blockId : blockIds){
                if (mBlockArray[blockId].isMyActive) {
                    blocksToInactivate.push_back(mBlockArray[blockId]);
                } else{
                    THROW(GopherwoodException,
                          "[ActiveStatus] Incorrect block status, the blockId %d should be activated!", blockId);
                }
            }
            std::vector<Block> turedToUsedBlocks =
                    mSharedMemoryContext->inactivateBuckets(blocksToInactivate,
                                                            mFileId,
                                                            mActiveId,
                                                            mIsWrite);

            /* update block status*/
            for (Block b : blocksToInactivate) {
                mBlockArray[b.blockId].isMyActive = false;
            }
            for (Block b : turedToUsedBlocks) {
                mBlockArray[b.blockId].state = b.state;
            }
            /* log inactivate buckets */
            mManifest->logInactivateBucket(turedToUsedBlocks);
        }

        /* acquire new buckets */
        if (numToAcquire > 0) {
            uint32_t numAcqurieFree = numToAcquire > numFreeBuckets ? numFreeBuckets : numToAcquire;
            int numToEvict = numToAcquire - numAcqurieFree;

            newBuckets = mSharedMemoryContext->acquireFreeBucket(mActiveId, numAcqurieFree, mFileId, mIsWrite);
            /* add free buckets to preAllocatedBlocks */
            for (int32_t bucketId : newBuckets) {
                LOG(INFO, "[ActiveStatus]          |"
                          "Pre-allocate bucket %d to pre-allocated bucket array.", bucketId);
                Block newBlock(bucketId,
                               InvalidBlockId,
                               LocalBlock,
                               BUCKET_ACTIVE,
                               true);/*is my active block*/
                blocksForLog.push_back(newBlock);
                mPreAllocatedBuckets.push_back(newBlock);
            }
            numToAcquire -= numAcqurieFree;

            /* Manifest Log */
            mManifest->logAcquireNewBlock(blocksForLog);
            blocksForLog.clear();

            /* start evict 1st used bucket */
            if (numToEvict > 0) {
                evictBlockInfo = mSharedMemoryContext->markBucketEvicting(mActiveId);
                evicting = true;
            }
        }
    SHARED_MEM_END

    /************************************************
     * Step2: Loop to get used buckets
     * 1. check if there is any free buckets again
     * 2. evict used buckets
     ************************************************/
    while (numToAcquire > 0 || evicting) {
        /* evict the bucket */
        if (evicting) {
            mOssWriter->writeBlock(evictBlockInfo);
        }

        SHARED_MEM_BEGIN
            /* mark evict finish */
            if (evicting) {
                int rc = mSharedMemoryContext->evictBucketFinish(evictBlockInfo.bucketId,
                                                                 mActiveId,
                                                                 mFileId,
                                                                 mIsWrite);
                if (rc == 0 || rc == 1) {
                    Block newBlock(evictBlockInfo.bucketId, InvalidBlockId, LocalBlock, BUCKET_ACTIVE, true);
                    LOG(INFO, "[ActiveStatus]          |"
                              "Add block %d to pre-allocated bucket array.",
                        newBlock.bucketId);
                    blocksForLog.push_back(newBlock);
                    mPreAllocatedBuckets.push_back(newBlock);
                    evicting = false;
                    numToAcquire--;
                }

                /* add log to the evicted file */
                if (rc == 1) {
                    logEvictBlock(evictBlockInfo);
                }

                /* 1: the evicted block has been deleted during eviction
                 * 2: the evicted bucket has been activated by it's file owner, give up this one*/
                if (rc == 1 || rc == 2) {
                    /* the evicted bucket has been activated by it's file owner, give up this one */
                    /* TODO: remove the bucket since that file has been deleted */
                    THROW(GopherwoodNotImplException, "Not implemented yet!");
                }
            }

            /* acquire free buckets */
            numFreeBuckets = mSharedMemoryContext->getFreeBucketNum();
            uint32_t numAcqurieFree = numToAcquire > numFreeBuckets ? numFreeBuckets : numToAcquire;

            if (numAcqurieFree > 0) {
                newBuckets = mSharedMemoryContext->acquireFreeBucket(mActiveId, numAcqurieFree, mFileId, mIsWrite);
                /* add free buckets to preAllocatedBlocks */
                for (int32_t bucketId: newBuckets) {
                    LOG(INFO, "[ActiveStatus]          |"
                              "Add block %d to pre-allocated bucket array.", bucketId);
                    Block newBlock(bucketId,
                                   InvalidBlockId,
                                   LocalBlock,
                                   BUCKET_ACTIVE,
                                   true);/*is my active block*/
                    blocksForLog.push_back(newBlock);
                    mPreAllocatedBuckets.push_back(newBlock);
                }
                numToAcquire -= numAcqurieFree;
            }

            /* Manifest Log */
            mManifest->logAcquireNewBlock(blocksForLog);
            blocksForLog.clear();

            /* start evict next bucket */
            if (numToAcquire > 0) {
                evictBlockInfo = mSharedMemoryContext->markBucketEvicting(mActiveId);
                evicting = true;
            }
        SHARED_MEM_END
    }
}

/* Extend the file to create a new block, the block will get bucket from the
 * pre allocated bucket array */
void ActiveStatus::extendOneBlock() {
    std::vector<Block> blocksModified;

    if (mPreAllocatedBuckets.size() == 0) {
        acquireNewBlocks();
    }

    if (mPreAllocatedBuckets.size() == 0) {
        /* TODO: play with own buckets, evict first */
        THROW(GopherwoodNotImplException, "Not implemented yet!");
    }

    /* build the block */
    Block b = mPreAllocatedBuckets.front();
    mPreAllocatedBuckets.pop_front();
    b.blockId = mNumBlocks;
    b.isMyActive = true;

    /* add to block array */
    mBlockArray.push_back(b);
    mNumBlocks++;

    /* add to LRU cache */
    mLRUCache->put(b.blockId, b.bucketId);

    /* prepare for log */
    blocksModified.push_back(b);

    SHARED_MEM_BEGIN
        /* update file info to shared memory */
        mSharedMemoryContext->updateActiveFileInfo(blocksModified, mFileId);
        /* Manifest Log */
        RecOpaque opaque;
        opaque.extendBlock.eof = mEof;
        mManifest->logExtendBlock(blocksModified, opaque);
    SHARED_MEM_END
    LOG(INFO, "[ActiveStatus]          |"
              "ExtendOneBlock blockId=%d, bucketId=%d",
        b.blockId, b.bucketId);
}

/* activate a block if it's not been marked */
void ActiveStatus::activateBlock(int blockInd) {
    SHARED_MEM_BEGIN
        /* inactivate first if LRUCache(Quota) is used up */
        if (mLRUCache->size() == mLRUCache->maxSize()){
            std::vector<int> blockIds = mLRUCache->removeNumOfKeys(1);
            std::vector<Block> blocksToInactivate;
            for (int i : blockIds) {
                blocksToInactivate.push_back(mBlockArray[i]);
            }
            std::vector<Block> turedToUsedBlocks =
                    mSharedMemoryContext->inactivateBuckets(blocksToInactivate,
                                                            mFileId,
                                                            mActiveId,
                                                            mIsWrite);

            /* update block status*/
            for (Block b : blocksToInactivate) {
                mBlockArray[b.blockId].isMyActive = false;
            }
            for (Block b : turedToUsedBlocks) {
                mBlockArray[b.blockId].state = b.state;
            }
            /* log inactivate buckets */
            mManifest->logInactivateBucket(turedToUsedBlocks);
        }

        /* activate the block */
        bool activated = mSharedMemoryContext->activateBucket(mFileId,
                                                              mBlockArray[blockInd],
                                                              mActiveId,
                                                              mIsWrite);
        mLRUCache->put(mBlockArray[blockInd].blockId, mBlockArray[blockInd].bucketId);
        mBlockArray[blockInd].isMyActive = true;

        /* the block is activated by me */
        if (activated) {
            mManifest->logActivateBucket(mBlockArray[blockInd]);
        }
    SHARED_MEM_END
}

void ActiveStatus::logEvictBlock(BlockInfo info) {
    /* check file exist */
    std::string manifestFileName = Manifest::getManifestFileName(mSharedMemoryContext->getWorkDir(), info.fileId);
    if (access(manifestFileName.c_str(), F_OK) == -1) {
        THROW(GopherwoodInvalidParmException,
              "[ActiveStatus::ActiveStatus] File does not exist %s",
              manifestFileName.c_str());
    }
    Manifest *manifest = new Manifest(manifestFileName);
    manifest->mfSeek(0, SEEK_END);
    Block block(InvalidBucketId, info.blockId, false, BUCKET_FREE, false);

    manifest->logEvcitBlock(block);
}

/* flush cached Manifest logs to disk
 * TODO: Currently all log record are flushed immediately, we just add UpdateEof log */
void ActiveStatus::flush() {
    SHARED_MEM_BEGIN
        RecOpaque opaque;
        opaque.updateEof.eof = mEof;
        mManifest->logUpdateEof(opaque);
    SHARED_MEM_END
}

/* truncate existing Manifest file and flush latest block status to it */
void ActiveStatus::close() {
    SHARED_MEM_BEGIN

        /* get blocks to inactivate */
        std::vector<int> activeBlockIds = mLRUCache->getAllKeyObject();
        std::vector<Block> activeBlocks;
        for (int32_t activeBlockId : activeBlockIds) {
            activeBlocks.push_back(mBlockArray[activeBlockId]);
        }

        /* release all preAllocatedBlocks & active buckets */
        mSharedMemoryContext->releaseBuckets(mPreAllocatedBuckets);
        /* log release buckets */
        mManifest->logReleaseBucket(mPreAllocatedBuckets);

        /* inactivate buckets, in other words, remove the marker of current ActiveId */
        std::vector<Block> turedToUsedBlocks = mSharedMemoryContext->inactivateBuckets(activeBlocks,
                                                                                       mFileId,
                                                                                       mActiveId,
                                                                                       mIsWrite);
        /* update block status*/
        for (Block b : activeBlocks) {
            mBlockArray[b.blockId].isMyActive = false;
        }
        for (Block tunredToUsedBlock : turedToUsedBlocks) {
            Block b = tunredToUsedBlock;
            mBlockArray[b.blockId].state = b.state;
        }
        /* log inactivate buckets */
        mManifest->logInactivateBucket(turedToUsedBlocks);

        unregistInSharedMem();

        /* truncate existing Manifest file and flush latest block status to it.
         * NOTES: Only do the Manifest log shrinking if nobody is opening
         * this file. */
        if (!mSharedMemoryContext->isFileOpening(mFileId)) {
            RecOpaque opaque;
            opaque.fullStatus.eof = mEof;
            mManifest->logFullStatus(mBlockArray, opaque);
        }

        /* clear LRU & blockArray */
        mBlockArray.clear();
        /* TODO: clear mLRUCache */

    SHARED_MEM_END
}

/* The delete file ActiveStatus already locked the Manifest log by
 * regist machanism, thus we don't need to worry about Manifest log
 * update contention here. */
void ActiveStatus::destroy() {
    std::vector<Block> localBlocks;
    std::vector<Block> remoteBlocks;

    /* check all blocks are not in active status */
    for (Block block : mBlockArray) {
        if (block.isLocal && block.state == BUCKET_USED) {
            localBlocks.push_back(block);
        } else if (block.isLocal && block.state == BUCKET_ACTIVE) {
            THROW(GopherwoodException,
                  "[ActiveStatus] File %s still using active bucket %d",
                  mFileId.toString().c_str(), block.bucketId);
        } else if (!block.isLocal) {
            remoteBlocks.push_back(block);
        } else {
            THROW(GopherwoodException,
                  "[ActiveStatus] Dead Zone, Internal Error!");
        }
    }

    /* free all block cached in local space. If it's evicting by someone,
     * mark it deleted. */
    SHARED_MEM_BEGIN
        mSharedMemoryContext->deleteBlocks(localBlocks, mFileId);
        unregistInSharedMem();
    SHARED_MEM_END

    /* remove all remote file
     * TODO: Not implemented yet */
    if (remoteBlocks.size() > 0){
        THROW(GopherwoodNotImplException, "Not implemented yet!");
    }

    /* delete manifest log */
    mManifest->destroy();
}

void ActiveStatus::catchUpManifestLogs() {
    std::vector<Block> blocks;

    while (true) {
        RecordHeader header = mManifest->fetchOneLogRecord(blocks);
        if (header.type == RecordType::invalidLog) {
            break;
        }
        /* integrity checks */
        assert(header.numBlocks == blocks.size());

        /* replay the log */
        switch (header.type) {
            case RecordType::activeBlock:
                LOG(INFO, "[ActiveStatus]          |"
                        "Replay activeBlock log record with %lu blocks.", blocks.size());
                for (Block block : blocks) {
                    mBlockArray[block.blockId].state = BUCKET_ACTIVE;
                }
                break;
            case RecordType::inactiveBlock:
                LOG(INFO, "[ActiveStatus]          |"
                          "Replay inactiveBlock log record with %lu blocks.", blocks.size());
                for (Block block : blocks) {
                    mBlockArray[block.blockId].state = BUCKET_USED;
                }
                break;
            case RecordType::acquireNewBlock:
                /* No need to replay this log for Read ActiveStatus and Delete ActiveStatus,
                 * the preAllocatedBuckets should bundled with Write ActiveStatus
                 * Since we only support one Write ActiveStatus at a time:
                 * 1. Read/Delete ActiveStatus do not need the preAllocatedBuckets info
                 * 2. When Write ActiveStatus do the log replay work, all preAllocatedBuckets should have
                 *    been released
                 * TODO: The only need to replay this log is to check the log integrity */
                LOG(INFO, "[ActiveStatus]          |"
                          "Skip acquireNewBlock log record with %lu blocks.", blocks.size());
                break;
            case RecordType::releaseBlock:
                /* As described in acquireNewBlock log, the only need to replay this
                 * log is to check the log integrity */
                LOG(INFO, "[ActiveStatus]          |"
                          "Skip releaseBlock log record with %lu blocks.", blocks.size());
                break;
            case RecordType::extendBlock:
                LOG(INFO, "[ActiveStatus]          |"
                          "Replay assignBlock log record with %lu blocks.", blocks.size());
                assert(header.numBlocks == 1);
                mBlockArray.push_back(blocks[0]);
                mNumBlocks++;
                mEof = header.opaque.extendBlock.eof;
                break;
            case RecordType ::evictBlock:
                LOG(INFO, "[ActiveStatus]          |"
                        "Replay evictBlock log record with %lu blocks.", blocks.size());
                assert(header.numBlocks == 1);
                if (mBlockArray[blocks[0].blockId].isLocal && mBlockArray[blocks[0].blockId].state == BUCKET_USED) {
                    mBlockArray[blocks[0].blockId] = blocks[0];
                } else {
                    THROW(GopherwoodException,
                          "[ActiveStatus] The block %d status is not BUCKET_USED when replaying the evictBlock log ",
                          blocks[0].blockId);
                }
                break;
            case RecordType::fullStatus:
                LOG(INFO, "[ActiveStatus]          |"
                          "Replay fullStatus log record with %lu blocks. EOF=%lu", blocks.size(),
                    header.opaque.fullStatus.eof);
                for (Block block : blocks) {
                    mBlockArray.push_back(block);
                    mNumBlocks++;
                }
                mEof = header.opaque.fullStatus.eof;
                break;
            case RecordType::updateEof:
                assert(header.numBlocks == 0);
                LOG(INFO, "[ActiveStatus]          |"
                          "Replay updateEof log record eof=%ld.", header.opaque.updateEof.eof);
                mEof = header.opaque.updateEof.eof;
                break;
            default:
                THROW(GopherwoodNotImplException,
                      "[ActiveStatus] Log type %d not implemented when catching up logs.",
                      header.type);
        }
        blocks.clear();
    }
}

ActiveStatus::~ActiveStatus() {
}

}
}