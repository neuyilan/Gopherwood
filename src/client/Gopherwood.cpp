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
#include "platform.h"

#include "common/Exception.h"
#include "common/ExceptionInternal.h"
#include "common/Logger.h"
#include "common/Memory.h"

#include "gopherwood.h"
#include "file/FileSystem.h"
#include "file/InputStream.h"
#include "file/OutputStream.h"

#ifdef __cplusplus
extern "C" {
#endif

using Gopherwood::exception_ptr;
using Gopherwood::Internal::InputStream;
using Gopherwood::Internal::OutputStream;
using Gopherwood::Internal::shared_ptr;
using Gopherwood::Internal::File;
using Gopherwood::Internal::FileSystem;
using Gopherwood::Internal::SetErrorMessage;
using Gopherwood::Internal::SetLastException;

struct GWFileSystemInternalWrapper {
public:
    GWFileSystemInternalWrapper(FileSystem *fs) :
            __filesystem(fs) {
    }

    ~GWFileSystemInternalWrapper() {
        delete __filesystem;
    }

    FileSystem &getFilesystem() {
        return *__filesystem;
    }

private:
    FileSystem *__filesystem;
};

struct GWFileInternalWrapper {
public:
    GWFileInternalWrapper(File *file) :
            __file(file) {
    }

    ~GWFileInternalWrapper() {
        delete __file;
    }

    File &getFile() {
        return *__file;
    }

private:
    File *__file;
};

static void handleException(Gopherwood::exception_ptr error) {
    try {
        std::string buffer;
        LOG(
                Gopherwood::Internal::LOG_ERROR,
                "Handle Exception: %s",
                Gopherwood::Internal::GetExceptionDetail(error, buffer));

        Gopherwood::rethrow_exception(error);
    } catch (const Gopherwood::GopherwoodSyncException &) {
        std::string buffer;
        LOG(
                Gopherwood::Internal::LOG_ERROR,
                "Handle Gopherwood Sync Exception: %s",
                Gopherwood::Internal::GetExceptionDetail(error, buffer));
        errno = ESYNC;
    } catch (const Gopherwood::GopherwoodException &) {
        std::string buffer;
        LOG(
                Gopherwood::Internal::LOG_ERROR,
                "Handle Gopherwood exception: %s",
                Gopherwood::Internal::GetExceptionDetail(error, buffer));
        errno = EGOPHERWOOD;
    } catch (const std::exception &) {
        std::string buffer;
        LOG(
                Gopherwood::Internal::LOG_ERROR,
                "Unexpected exception: %s",
                Gopherwood::Internal::GetExceptionDetail(error, buffer));
        errno = EINTERNAL;
    }
}

gopherwoodFS gwCreateContext(char *fileName) {
    gopherwoodFS retVal = NULL;
    try {
        FileSystem *fs = new FileSystem(fileName);
        retVal = new GWFileSystemInternalWrapper(fs);
    } catch (...) {
        SetLastException(Gopherwood::current_exception());
        handleException(Gopherwood::current_exception());
    }
    return retVal;
}

gwFile gwOpenFile(gopherwoodFS fs, const char *fileName, int flags) {
    gwFile retVal = NULL;
    File* file;

    try {
        if (flags & GW_CREAT)
        {
            file = fs->getFilesystem().CreateFile(fileName, flags);
        }
        else
        {
            file = fs->getFilesystem().OpenFile(fileName, flags);
        }

        retVal = new GWFileInternalWrapper(file);
    } catch (...) {
        SetLastException(Gopherwood::current_exception());
        handleException(Gopherwood::current_exception());
    }
    return retVal;
}

tSize gwRead(gopherwoodFS fs, gwFile file, void *buffer, tSize length) {
    return 0;
}

int gwSeek(gopherwoodFS fs, gwFile file, tOffset desiredPos) {
    return -1;
}

int32_t gwWrite(gopherwoodFS fs, gwFile file, const void *buffer, tSize length) {
    return -1;
}

int gwCloseFile(gopherwoodFS fs, gwFile file) {
    return -1;
}

int deleteFile(gopherwoodFS fs, gwFile file) {
    return -1;
}

#ifdef __cplusplus
}
#endif
