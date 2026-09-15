#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <string>
#include <pthread.h>
#include <wiringPi.h>
#include <wiringSerial.h>

class SerialComm {
public:
    SerialComm();
    ~SerialComm();
    bool open(const char* device, int baudrate);
    void send(const std::string& data);
    bool startReceiveThread();
    void stop();

private:
    int fd;
    pthread_t recvThread;
    volatile bool running;
    static void* receiveThreadFunc(void* arg);
    void receiveLoop();
};

#endif