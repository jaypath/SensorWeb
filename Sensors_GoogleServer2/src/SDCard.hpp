#ifndef SDCard_HPP
#define SDCard_HPP

#include <SD.h>
#include "globals.hpp"
#include "timesetup.hpp"

//declared as global constants
extern SensorVal Sensors[SENSORNUM];
extern ScreenFlags ScreenInfo;

bool storeScreenInfoSD();
bool readScreenInfoSD();
bool writeSensorsSD(String filename);
bool readSensorsSD(String filename);
bool storeSensorSD(struct SensorVal *S);
bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N,uint32_t *samples, uint32_t starttime, uint32_t endtime,byte avgN);
bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, byte avgN);
uint16_t read16(File &f);
uint32_t read32(File &f);
void deleteFiles(const char* pattern,const char* directory);
bool matchPattern(const char* filename, const char* pattern);
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
#endif