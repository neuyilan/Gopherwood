sudo rm -rf /ssdfile/gopherwood.log
sudo rm -rf /ssdfile/goworkspace/*
scp /gopherwood/exchangefile/TestReadWriteSeek-ReadEvictBlock /ssdfile/goworkspace/
g++ -std=c++11 ../TestGWAPI.cpp ../gopherwood.h -o TestGWAPI -lgopherwood
rm -rf log*

