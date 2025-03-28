#ifndef SDCard_HPP
#define SDCard_HPP

#include <SD.h>
#include "globals.hpp"

//declared as global constants
//extern SensorVal *Sensors;
extern SensorVal Sensors[SENSORNUM];
extern Screen I;

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
bool matchPattern(const char* filename, const char* pattern);
void deleteFiles(const char* pattern,const char* directory);
bool storeScreenInfoSD();
bool readScreenInfoSD(); //read last screenInfo


bool writeSensorsSD(String filename = "/Data/SensorBackupv2.dat");
bool readSensorsSD(String filename = "/Data/SensorBackupv2.dat");
bool storeSensorSD(struct SensorVal *S);
bool readSensorSD(struct SensorVal *S, uint32_t t[], double v[], byte *N,uint32_t *samples, uint32_t starttime, uint32_t endtime,byte avgN);
bool readSensorSD(struct SensorVal *S, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, byte avgN);

uint16_t read16(File &f);
uint32_t read32(File &f);

#endif