#include "Simulator.h"

void Simulator::update()
{
    temperature++;
    speed += 100;
}

int Simulator::getTemperature() const
{
    return temperature;
}

int Simulator::getSpeed() const
{
    return speed;
}