#ifndef DEVICE_H
#define DEVICE_H

#include <string>

class Device
{
private:
    std::string status = "idle";

public:
    void start();
    void stop();
    void reset();
    const std::string& getStatus() const;
};

#endif // DEVICE_H
