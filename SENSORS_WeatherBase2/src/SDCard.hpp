#ifndef SDCard_HPP
#define SDCard_HPP

#include <SD.h>
#include <utility.hpp>
#include <globals.hpp>

//declared as global constants
extern SensorVal Sensors[SENSORNUM];


bool writeSensorsSD(String filename);
bool readSensorsSD(String filename);
bool storeSensorSD(struct SensorVal *S);
bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N, uint32_t starttime, uint32_t endtime, byte delta=1 );

#endif