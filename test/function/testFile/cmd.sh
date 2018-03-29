rm -rf /ssdfile/prototype/gopherwood
rm -rf /ssdfile/prototype/logPersistence/*
rm -rf /ssdfile/prototype/logs/*
rm -rf /ssdfile/prototype/sharedMemory/*
g++ -std=c++11 ../TestGWAPI.cpp ../gopherwood.h -o TestGWAPI -lgopherwood
rm -rf log*
