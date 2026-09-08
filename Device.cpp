#include "Device.h"

void Device::start()
{
    if(status == "idle")
    {
        status = "running";
    }
}

void Device::stop()
{
    if(status == "running")
    {
        status = "stopped";
    }
}

void Device::reset()
{
    if(status == "stopped"||status == "error")
    {
        status = "idle";
    }
}

void Device::fault()
{
    status = "error";
}

const std::string& Device::getStatus() const
{
    return status;
}

bool Device::setSpeedSetpoint(int value)
{
    if (value < 0 || value > 3000)
    {
        return false;
    }

    speedSetpoint = value;
    return true;
}

int Device::getSpeedSetpoint() const
{
    return speedSetpoint;
}

bool Device::setTempLimit(int value)
{
    if (value < 1 || value > 80)
    {
        return false;
    }

    tempLimit = value;
    return true;
}

int Device::getTempLimit() const
{
    return tempLimit;
}

bool Device::setParameters(int speed, int limit)
{
    if (speed < 0 || speed > 3000 || limit < 1 || limit > 80)
    {
        return false;
    }

    speedSetpoint = speed;
    tempLimit = limit;

    return true;
}