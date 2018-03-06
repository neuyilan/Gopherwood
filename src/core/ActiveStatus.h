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
#ifndef _GOPHERWOOD_CORE_ACTIVESTATUS_H_
#define _GOPHERWOOD_CORE_ACTIVESTATUS_H_

#include "platform.h"

#include "file/FileId.h"
#include "core/SharedMemoryContext.h"
#include "core/BlockStatus.h"
#include "core/Manifest.h"

namespace Gopherwood {
namespace Internal {

class ActiveStatus {
public:
    ActiveStatus(FileId fileId, shared_ptr<SharedMemoryContext> sharedMemoryContext);

    int64_t getPosition();

    void setPosition(int64_t pos);

    int64_t getEof();

    BlockInfo getCurBlockInfo();

    void flush();

    void archive();

    ~ActiveStatus();

private:
    Block getCurBlock();

    int64_t getCurBlockOffset();

    bool needNewBlock();

    void acquireNewBlocks();

    void extendOneBlock();

    std::string getManifestFileName(FileId fileId);

    /* Fields */
    FileId mFileId;
    shared_ptr<SharedMemoryContext> mSharedMemoryContext;
    shared_ptr<Manifest> mManifest;

    int64_t mPos;
    int64_t mEof;
    int32_t mNumBlocks;
    int64_t mBlockSize;

    std::vector<Block> mBlockArray;
    std::vector<Block> mPreAllocatedBlocks;

};


}
}

#endif //_GOPHERWOOD_CORE_ACTIVESTATUS_H_