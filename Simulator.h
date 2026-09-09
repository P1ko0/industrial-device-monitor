#ifndef SIMULATOR_H
#define SIMULATOR_H

class Simulator
{
private:
    int temperature = 25;
    int speed = 2500;

public:
    void update();

    int getTemperature() const;
    int getSpeed() const;
};

#endif // SIMULATOR_H