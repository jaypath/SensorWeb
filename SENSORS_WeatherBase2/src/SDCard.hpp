#ifndef SDCard_HPP
#define SDCard_HPP

#include <SD.h>
#include <utility.hpp>
#include <globals.hpp>

//declared as global constants
extern SensorVal Sensors[SENSORNUM];


bool writeSensorSD( String filename);
bool readSensorSD(String filename);
#endif