#include "Logger.h"
#include <fstream>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace std;

string getTime()
{
    time_t now = time(nullptr);

    tm* localTime = localtime(&now);

    stringstream ss;

    ss << localTime->tm_year + 1900 << "-"
       << setw(2) << setfill('0') << localTime->tm_mon + 1 << "-"
       << setw(2) << setfill('0') << localTime->tm_mday << " "
       << setw(2) << setfill('0') << localTime->tm_hour << ":"
       << setw(2) << setfill('0') << localTime->tm_min << ":"
       << setw(2) << setfill('0') << localTime->tm_sec;

    return ss.str();
}

void Logger::write(string message)
{
    ofstream file("log.txt",ios::app);
    if(!file.is_open())
    {
        cout<<"fail to open"<<endl;
        return;
    }

    file 
    << "["
    << getTime()
    << "] "
    << message
    << endl;

    file.close();
}