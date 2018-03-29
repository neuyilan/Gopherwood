

#include "FSConfig.h"
#include "SharedMemoryManager.h"

namespace Gopherwood {

    namespace Internal {
        int64_t SIZE_OF_BLOCK = 64 * 1024 * 1024; //the size of the bucket size

        char *BUCKET_PATH_FILE_NAME = "/ssdfile/prototype/gopherwood";

        char *SHARED_MEMORY_PATH_FILE_NAME = "/ssdfile/prototype/sharedMemory/smFile";

        char *FILE_LOG_PERSISTENCE_PATH = "/ssdfile/prototype/logPersistence/";
        int FILENAME_MAX_LENGTH = 255;
        int NUMBER_OF_BLOCKS = 5000;//char+(char+long+int(size of file name)+char[255])
        int MIN_QUOTA_SIZE = 10;  // (MIN_QUOTA_SIZE+1)*2<NUMBER_OF_BLOCKS

        int QINGSTOR_BUFFER_SIZE = 4 * 1024 * 1024;
        int32_t READ_BUFFER_SIZE = SIZE_OF_BLOCK / 4;
        int WRITE_BUFFER_SIZE = 8 * 1024 * 1024;

        int MAX_PROCESS = 6;//maximum number of processes running at the same time.
        int MAX_QUOTA_SIZE = NUMBER_OF_BLOCKS / MAX_PROCESS;

        char *LOG_FILE_PATH = "/ssdfile/prototype/logs/gopherwood.log";
    }


}
