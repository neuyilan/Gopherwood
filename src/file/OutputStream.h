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
#ifndef _GOPHERWOOD_FILE_OUTPUTSTREAM_H_
#define _GOPHERWOOD_FILE_OUTPUTSTREAM_H_

#include "platform.h"

#include "block/BlockOutputStream.h"
#include "common/Memory.h"
#include "core/ActiveStatus.h"

namespace Gopherwood {
namespace Internal {
class OutputStream {
public:
    OutputStream(shared_ptr<ActiveStatus> status);

    void write(const char *buffer, int64_t length);

    ~OutputStream();
private:
    void updateBlockStream(int64_t curPos);

    shared_ptr<BlockOutputStream> blockOutputStream;
    shared_ptr<ActiveStatus> status;
    int64_t pos;
};

}
}

#endif //_GOPHERWOOD_FILE_OUTPUTSTREAM_H_
