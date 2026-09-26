#ifndef LOGGER_H
#define LOGGER_H

#include <string>

using namespace std;

enum class LogLevel{
    Info,Warning,Error
};

class Logger
{
public:

    static void write(string message,LogLevel level = LogLevel::Info);

};

#endif