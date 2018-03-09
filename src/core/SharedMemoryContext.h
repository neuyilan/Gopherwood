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
#ifndef _GOPHERWOOD_CORE_SHAREDMEMORYCONTEXT_H_
#define _GOPHERWOOD_CORE_SHAREDMEMORYCONTEXT_H_

#include "platform.h"
#include "common/Memory.h"
#include "core/BlockStatus.h"
#include "file/FileId.h"

#include <boost/interprocess/mapped_region.hpp>

namespace Gopherwood {
namespace Internal {

using namespace boost::interprocess;

typedef struct ShareMemHeader {
    uint8_t flags;
    char padding[3];
    int32_t numBuckets;
    int32_t numFreeBuckets;
    int32_t numActiveBuckets;
    int32_t numUsedBuckets;
    int32_t numMaxConn;

    inline void enter() { flags |= 0x01; };

    inline void exit() { flags &= 0xFE; };

    void reset(int32_t totalBucketNum, int32_t maxConn) {
        flags = 0;
        numBuckets = totalBucketNum;
        numFreeBuckets = totalBucketNum;
        numActiveBuckets = 0;
        numUsedBuckets = 0;
        numMaxConn = maxConn;
    };
} ShareMemHeader;

#define BucketTypeMask 0xFFFFFFFC
#define BUCKET_FREE      0
#define BUCKET_ACTIVE    1
#define BUCKET_USED      2

typedef struct ShareMemBucket {
    uint32_t flags;
    FileId fileId;
    int32_t fileBlockIndex;

    bool isFreeBucket() { return (flags & 0x00000003) == 0 ? true : false; };

    bool isActiveBucket() { return (flags & 0x00000003) == 1 ? true : false; };

    bool isUsedBucket() { return (flags & 0x00000003) == 2 ? true : false; };

    void setBucketFree() { flags = flags & BucketTypeMask; };

    void setBucketActive() { flags = (flags & BucketTypeMask) | 0x00000001; };

    void setBucketUsed() { flags = (flags & BucketTypeMask) | 0x00000002; };
} ShareMemBucket;

typedef struct ShareMemConn {
    int pid;
} ShareMemConn;

class SharedMemoryContext {
public:
    SharedMemoryContext(std::string dir, shared_ptr<mapped_region> region, int lockFD);

    int regist(int pid);

    int unregist(int connId, int pid);

    int calcDynamicQuotaNum();

    std::vector<int32_t> acquireBlock(FileId fileId, int num);

    void releaseBlocks(std::vector<Block> &blocks);

    void inactivateBlocks(std::vector<Block> &blocks, FileId fileId);

    void reset();
    void lock();
    void unlock();

    int32_t getFreeBucketNum();
    int32_t getActiveBucketNum();
    int32_t getUsedBucketNum();

    std::string &getWorkDir();
    int32_t getNumMaxConn();

    ~SharedMemoryContext();

private:
    void printStatistics();

    std::string workDir;
    shared_ptr<mapped_region> mShareMem;
    int mLockFD;
    ShareMemHeader *header;
    ShareMemBucket *buckets;
    ShareMemConn *conns;
};

}
}

#endif //_GOPHERWOOD_CORE_SHAREDMEMORYCONTEXT_H_
