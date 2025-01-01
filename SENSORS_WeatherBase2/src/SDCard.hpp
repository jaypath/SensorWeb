#ifndef SDCard_HPP
#define SDCard_HPP

#include <SD.h>
#include <utility.hpp>
#include <globals.hpp>

//declared as global constants
extern SensorVal Sensors[SENSORNUM];


bool writeSensorsSD(String filename = "/Data/SensorBackupv2.dat");
bool readSensorsSD(String filename = "/Data/SensorBackupv2.dat");
bool storeSensorSD(struct SensorVal *S);
bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N,uint32_t *samples, uint32_t starttime, uint32_t endtime,byte avgN);
bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, byte avgN);
uint16_t read16(File &f);
uint32_t read32(File &f);

#endif