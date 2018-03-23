/*
 * gwWrite.cpp
 *
 *  Created on: Jan 30, 2018
 *      Author: houliang
 */

#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sstream>
#include <cstring>

#include "gopherwood.h"

using namespace std;


char workDir[] = "/ssdfile/goworkspace";
GWContextConfig config;


void initConfig() {
    config.blockSize = 4 * 1 * 1024;;
    config.numBlocks = 100;
}

void testGWFormat() {
    /*1. format the workDir*/
    gwFormatContext(workDir);
}

void testGWWrite(std::string fileName) {

    /*2. create the context and open the file*/
    initConfig();
    gopherwoodFS gwFS = gwCreateContext(workDir, &config);
    gwFile gwfile = gwOpenFile(gwFS, fileName.c_str(), GW_CREAT | GW_RDWR);


    /*3. seek the end of the file*/
    gwSeek(gwFS, gwfile, 0, SEEK_END);

    /*3. construct the  input source file name*/
    std::stringstream ss;
    ss << "/ssdfile/goworkspace/" << fileName;
    std::string filePath = ss.str();

    /*4. read data from file*/
    std::ifstream infile;
    infile.open(filePath);


    int SIZE = 128;
    int totalWriteLength = 0;
    char *buf = new char[SIZE];
    infile.read(buf, SIZE);
    int readLengthIn = infile.gcount();
    while (readLengthIn > 0) {
        totalWriteLength += readLengthIn;
        std::cout << "totalWriteLength=" << totalWriteLength << ",readLength="
                  << readLengthIn << std::endl;
        std::cout << "buf=" << buf << std::endl;
        /*5. write data to the gopherwood*/
        gwWrite(gwFS, gwfile, buf, readLengthIn);

        buf = new char[SIZE];
        infile.read(buf, SIZE);
        readLengthIn = infile.gcount();
    }

    /*6. close the file*/
    gwCloseFile(gwFS, gwfile);

    std::cout << "*******END OF WRITE*****, totalWriteLength=" << totalWriteLength << std::endl;
}


void writeUtil(char *fileName, char *buf, int size) {
    std::ofstream ostrm(fileName, std::ios::out | std::ios::app);
    ostrm.write(buf, size);
}

void testGWRead(string fileName) {

    /*2. create the context and open the file*/
    initConfig();
    gopherwoodFS gwFS = gwCreateContext(workDir, &config);
    gwFile gwfile = gwOpenFile(gwFS, fileName.c_str(), GW_CREAT | GW_RDONLY);

    //3. construct the file name
    std::stringstream ss;
    ss << "/ssdfile/goworkspace/" << fileName << "-readCache";
    std::string fileNameForWrite = ss.str();


    int SIZE = 128;

    char *readBuf = new char[SIZE];
    int readLength = gwRead(gwFS, gwfile, readBuf, SIZE);

    int totalLength = 0;
    while (readLength > 0) {
        //4. write data to file to check
        totalLength += readLength;
        writeUtil((char *) fileNameForWrite.c_str(), readBuf, readLength);

        readBuf = new char[SIZE];
        readLength = gwRead(gwFS, gwfile, readBuf, SIZE);
        std::cout << "**************** readLength =  *******************" << readLength << std::endl;
    }


    gwCloseFile(gwFS, gwfile);

    std::cout << "*******END OF READ*****, totalLength=" << totalLength << std::endl;
}


//void testGWDelete(string fileName) {
//    gopherwoodFS gwFS = gwCreateContext((char *) fileName.c_str());
//    gwFile file = gwOpenFile(gwFS, (char *) fileName.c_str(), O_RDONLY);
//    std::cout << "***********START OF DELETE**************" << std::endl;
//    deleteFile(gwFS, file);
//    std::cout << "***********END OF  DELETE**************" << std::endl;
//}


int main(int agrInt, char **agrStr) {

    int count = 3;

    std::string fileNameArr[count];
    fileNameArr[0] = "TestReadWriteSeek-ReadEvictBlock";
    fileNameArr[1] = "TestReadWriteSeek-WriteBlockWithEvictOtherFileBlock";
    fileNameArr[2] = "TestReadWriteSeek-ThirdThread";


    cout << "********main*******agrInt= " << agrInt << ", agrStr[1]= " << agrStr[1] << endl;

    int timeCount = 10;

    if (strcmp(agrStr[1], "format") == 0) {
        testGWFormat();
    } else if (strcmp(agrStr[1], "write-1") == 0) {
        for (int i = 0; i < timeCount; i++) {
            testGWWrite(fileNameArr[0]);
        }
    } else if (strcmp(agrStr[1], "write-2") == 0) {
        for (int i = 0; i < timeCount; i++) {
            testGWWrite(fileNameArr[1]);
        }
    } else if (strcmp(agrStr[1], "write-3") == 0) {
        for (int i = 0; i < timeCount; i++) {
            testGWWrite(fileNameArr[2]);
        }
    } else if (strcmp(agrStr[1], "read-1") == 0) {
        testGWRead(fileNameArr[0]);
    } else if (strcmp(agrStr[1], "read-2") == 0) {
        testGWRead(fileNameArr[1]);
    } else if (strcmp(agrStr[1], "read-3") == 0) {
        testGWRead(fileNameArr[2]);
    }
//    else if (strcmp(agrStr[1], "delete-1") == 0) {
//        testGWDelete(fileNameArr[0]);
//    } else if (strcmp(agrStr[1], "delete-2") == 0) {
//        testGWDelete(fileNameArr[1]);
//    } else if (strcmp(agrStr[1], "delete-3") == 0) {
//        testGWDelete(fileNameArr[2]);
//    }


}