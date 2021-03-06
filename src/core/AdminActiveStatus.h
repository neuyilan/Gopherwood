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
#ifndef _GOPHERWOOD_CORE_ADMINACTIVESTATUS_H_
#define _GOPHERWOOD_CORE_ADMINACTIVESTATUS_H_

#include "client/gopherwood.h"
#include "core/BaseActiveStatus.h"

namespace Gopherwood {
namespace Internal {

class AdminActiveStatus : BaseActiveStatus{
public:
    AdminActiveStatus(shared_ptr<SharedMemoryContext> sharedMemoryContext,
                      int localSpaceFD);

    void getShareMemStatistic(GWSysInfo* sysInfo);

    int32_t evictNumOfBlocks(int num);

    ~AdminActiveStatus();

private:
    void registInSharedMem();
    void unregistInSharedMem();

    void logEvictBlock(BlockInfo info);
};

}
}

#endif //_GOPHERWOOD_CORE_ADMINACTIVESTATUS_H_