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
#include "common/Logger.h"
#include "core/ActiveStatus.h"

namespace Gopherwood {
namespace Internal {

#define MANIFEST_LOG_BEGIN  mManifest->lock(); \
                            mManifest->catchUpLog();
#define MANIFEST_LOG_END    mManifest->unlock();

#define SHARED_MEM_BEGIN    mSharedMemoryContext->lock();
#define SHARED_MEM_END      mSharedMemoryContext->unlock();

ActiveStatus::ActiveStatus(FileId fileId, shared_ptr<SharedMemoryContext> sharedMemoryContext) :
        mFileId(fileId), mSharedMemoryContext(sharedMemoryContext){
    mNumBlocks = 0;
    mPos = 0;
    mEof = 0;
    mBlockSize = Configuration::LOCAL_BLOCK_SIZE;

    mManifest = shared_ptr<Manifest>(new Manifest(getManifestFileName(mFileId)));
    mLRUCache = shared_ptr<LRUCache<int, Block>>(new LRUCache<int, Block>(Configuration::CUR_QUOTA_SIZE));
}

int64_t ActiveStatus::getPosition(){
    return mPos;
}

void ActiveStatus::setPosition(int64_t pos){
    mPos = pos;
    if (mPos > mEof){
        mEof = mPos;
    }
}

int64_t ActiveStatus::getEof() {
    return mEof;
}

/* This is the main entry point of adjusting active status. When calling this function, it means
 * out/in stream wants to access this block. Thus we need to see if active status should be
 * adjusted. */
BlockInfo ActiveStatus::getCurBlockInfo() {
    int curBlockIndex = mPos/mBlockSize;

    if (needNewBlock(curBlockIndex)){
        adjustActiveBlock(curBlockIndex);
    }

    /* build the block info */
    BlockInfo info;
    Block block = getCurBlock();
    info.id = block.bucketId;
    info.isLocal = block.isLocal;
    info.offset = getCurBlockOffset();
    return info;
}

Block ActiveStatus::getCurBlock() {
    return mBlockArray[mPos/mBlockSize];
}

int64_t ActiveStatus::getCurBlockOffset() {
    return mPos % mBlockSize;
}

bool ActiveStatus::needNewBlock(int curBlockInd) {
    return (mNumBlocks <= 0 ||                              // empty file
            curBlockInd >= mNumBlocks ||                    // append more data
            !mBlockArray[curBlockInd].isLocal ||            // block been evicted
            (mBlockArray[curBlockInd].isLocal &&            // block in used(2) state
             mBlockArray[curBlockInd].state==BUCKET_USED));
}

void ActiveStatus::adjustActiveBlock(int curBlockInd) {
    if (curBlockInd + 1 > mNumBlocks){
        extendOneBlock();
    }
}

/* All block activation should follow these steps:
 * 1. Check shared memory for the current quota
 * 2(a). If still have quota available, and have 0 or 2 available
 *       Then -> acquire more buckets for preAllocatedBlocks
 * 2(b). If still have quota available, and no 0 or 2 available
 *       Then -> play with current owned buckets
 * 3(a). If quota equal to current active block num, and have 0 or 2 available
 *       Then -> inactivate blocks from LRU first, then acquire new blocks
 * 3(b). If quota equal to current active block num, and have 0 or 2 available
 *       Then -> play with current owned buckets
 * 4. If quota smaller than current active block num,
 *       Then -> release blocks and use own quota
 * Notes: When got chance to acquire new blocks, active status will try to
 *        pre acquire a number of buckets to reduce the Shared Memory contention. */
void ActiveStatus::acquireNewBlocks() {
    std::vector<Block> blocksForLog;
    int32_t numToAcquire = 0;

    SHARED_MEM_BEGIN
    uint32_t quota = mSharedMemoryContext->calcDynamicQuotaNum();
    int numAvailable = mSharedMemoryContext->getFreeBucketNum() +
                       mSharedMemoryContext->getUsedBucketNum();
    LOG(INFO, "[ActiveStatus::acquireNewBlocks] current quota is %u, num availables is %d.",
        quota, numAvailable);

    /* switch to different cases */
    if (mLRUCache->size() < quota){
        /* 2(a) */
        if (numAvailable > 0) {
            numToAcquire = numAvailable > Configuration::PRE_ALLOCATE_BUCKET_NUM ?
                                Configuration::PRE_ALLOCATE_BUCKET_NUM : numAvailable;
        }
        /* 2(b) */
        else{
            /* TODO: Not implemented */
        }
    } else if (mLRUCache->size() == quota){
        /* 3(a) */
        if (numAvailable > 0) {
            /* TODO: Not implemented */
        }
        /* 3(b) */
        else{
            /* TODO: Not implemented */
        }
    }
    /* 4 */
    else {
        /* TODO: Not implemented */
    }

    std::vector<int32_t> newBlocks = mSharedMemoryContext->acquireBlock(mFileId, numToAcquire);
    SHARED_MEM_END

    for (std::vector<int32_t>::size_type i=0; i<newBlocks.size(); i++)
    {
        LOG(INFO, "[ActiveStatus::acquireNewBlocks] add block %d.", newBlocks[i]);
        Block newBlock(newBlocks[i], InvalidBlockId, LocalBlock, BUCKET_ACTIVE);
        blocksForLog.push_back(newBlock);
        mPreAllocatedBlocks.push_back(newBlock);
    }

    /* Manifest Log */
    MANIFEST_LOG_BEGIN
    mManifest->logAcquireNewBlock(blocksForLog);
    MANIFEST_LOG_END
}

/* get block from pre-allocated blocks */
void ActiveStatus::extendOneBlock(){
    std::vector<Block> blocksForLog;

    if (mPreAllocatedBlocks.size() == 0) {
        acquireNewBlocks();
    }

    /* build the block */
    Block b = mPreAllocatedBlocks.back();
    mPreAllocatedBlocks.pop_back();
    b.blockId = mNumBlocks;

    /* add to block array */
    mBlockArray.push_back(b);
    mNumBlocks++;

    /* add to LRU cache */
    mLRUCache->put(b.blockId, b);

    /* prepare for log */
    blocksForLog.push_back(b);

    /* Manifest Log */
    MANIFEST_LOG_BEGIN
    mManifest->logExtendBlock(blocksForLog);
    MANIFEST_LOG_END
}

/* flush cached Manifest logs to disk */
void ActiveStatus::flush() {

}

/* truncate existing Manifest file and flush latest block status to it */
void ActiveStatus::archive() {
    MANIFEST_LOG_BEGIN

    /* get blocks to inactivate */
    std::vector<int> activeBlockIds = mLRUCache->getAllKeyObject();
    std::vector<Block> activeBlocks;
    for (uint32_t i=0; i<activeBlockIds.size(); i++) {
        activeBlocks.push_back(mBlockArray[i]);
    }

    /* release all preAllocatedBlocks & active buckets */
    SHARED_MEM_BEGIN
    mSharedMemoryContext->releaseBlocks(mPreAllocatedBlocks);
    mSharedMemoryContext->inactivateBlocks(activeBlocks, mFileId);
    SHARED_MEM_END

    /* truncate existing Manifest file and flush latest block status to it */
    mManifest->logFullStatus(mBlockArray);

    MANIFEST_LOG_END
}

std::string ActiveStatus::getManifestFileName(FileId fileId) {
    std::stringstream ss;
    ss << mSharedMemoryContext->getWorkDir() << Configuration::MANIFEST_FOLDER << '/' << mFileId.hashcode << '-' << mFileId.collisionId;
    return ss.str();
}

ActiveStatus::~ActiveStatus() {

}

}
}