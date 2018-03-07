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
#include "common/Exception.h"
#include "common/ExceptionInternal.h"
#include "common/Memory.h"
#include "common/Logger.h"
#include "core/Manifest.h"

#include <sys/fcntl.h>

namespace Gopherwood {
namespace Internal {

Manifest::Manifest(std::string path) :
        mFilePath(path), mFD(-1) {
    mfOpen();
}

void Manifest::logAcquireNewBlock(std::vector<Block> &blocks) {
    /* build Acquire New Block Opaque */
    RecOpaque opaque;
    opaque.acquireNewBlock.padding = 0;

    /* build log record */
    std::string logRecord = serializeManifestLog(blocks, RecordType::acquireNewBlock, opaque);

    /* flush to log */
    mfAppend(logRecord);
}

void Manifest::logExtendBlock(std::vector<Block> &blocks) {
    /* build Acquire New Block Opaque */
    RecOpaque opaque;
    opaque.extendBlock.padding = 0;

    /* build log record */
    std::string logRecord = serializeManifestLog(blocks, RecordType::assignBlock, opaque);

    /* flush to log */
    mfAppend(logRecord);
}

void Manifest::logFullStatus(std::vector<Block> &blocks) {
    /* truncate existing Manifest file */
    mfTruncate();

    /* build Acquire New Block Opaque */
    RecOpaque opaque;
    opaque.common.padding = 0;

    /* build log record */
    std::string logRecord = serializeManifestLog(blocks, RecordType::fullStatus, opaque);

    /* flush to log */
    mfAppend(logRecord);
}

void Manifest::flush() {

}

std::string Manifest::serializeManifestLog(std::vector<Block> &blocks, RecordType type, RecOpaque opaque) {
    /* build log record header */
    std::string logRecord;

    RecordHeader header;
    header.recordLength = sizeof(RecordHeader) + blocks.size() * sizeof(BlockRecord);
    header.eyecatcher = MANIFEST_RECORD_EYECATCHER;
    header.type = type;
    header.flags = 0;
    header.opaque = opaque;
    header.numBlocks = blocks.size();

    /* build header string */
    std::string headerStr;
    char buf[sizeof(RecordHeader)];
    memcpy(buf, &header, sizeof(RecordHeader));
    headerStr.append(buf, sizeof(buf));

    /* build the log record */
    std::stringstream ss;
    ss << headerStr;
    for (unsigned int i = 0; i < blocks.size(); i++) {
        ss << blocks[i].toLogFormat();
    }

    logRecord = ss.str();
    if (logRecord.size() != header.recordLength) {
        THROW(GopherwoodException,
              "[Manifest::serializeManifestLog] Broken log record, expect_size=%lu, actual_size=%lu",
              header.recordLength, logRecord.size());
    }
    LOG(INFO, "[Manifest::serializeManifestLog] new log record, type=%d, length=%lu", type, header.recordLength);

    return logRecord;
}

void Manifest::catchUpLog() {
    /* TODO: unimplemented */
}


/************************************************************
 *      Support Functions For Manifest File Operations      *
 ************************************************************/
void Manifest::mfOpen() {
    int flags = O_CREAT | O_RDWR;
    mFD = open(mFilePath.c_str(), flags, 0644);
}

void Manifest::mfSeekEnd() {
    lseek(mFD, 0, SEEK_END);
}

void Manifest::mfAppend(std::string &record) {
    mfSeekEnd();
    write(mFD, record.c_str(), record.size());
}

void Manifest::mfTruncate() {
    ftruncate(mFD, 0);
    lseek(mFD, 0, SEEK_SET);
}

void Manifest::lock() {
    lockf(mFD, F_LOCK, 0);
}

void Manifest::unlock() {
    lockf(mFD, F_ULOCK, 0);
}

void Manifest::mfClose() {
    close(mFD);
    mFD = -1;
}

Manifest::~Manifest() {
    mfClose();
}

}
}