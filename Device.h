#ifndef DEVICE_H
#define DEVICE_H

#include <string>

class Device
{
private:
    std::string status = "idle";
    int speedSetpoint = 0;
    int tempLimit = 1;

public:
    void start();
    void stop();
    void reset();
    void fault();
    const std::string& getStatus() const;
    bool setSpeedSetpoint(int value);
    int getSpeedSetpoint() const;
    bool setTempLimit(int value);
    int getTempLimit() const;
    bool setParameters(int speed, int limit);
};

#endif // DEVICE_H
