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
    if(status == "stopped")
    {
        status = "idle";
    }
}

const std::string& Device::getStatus() const
{
    return status;
}