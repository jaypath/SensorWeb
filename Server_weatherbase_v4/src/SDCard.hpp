#ifndef SDCARD_HPP
#define SDCARD_HPP

#include <Arduino.h>
#include <SD.h>
#include "globals.hpp"

// New functions for Devices_Sensors class
bool storeSensorsSD();
bool readSensorsSD();

// New functions for individual sensor data storage using Devices_Sensors class
bool storeSensorDataSD(int16_t sensorIndex);
bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID);
bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, uint32_t endtime, byte avgN);
bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, byte avgN);

// Functions for storing/reading all sensors
bool storeAllSensorsSD();
bool readAllSensorsSD();

// Screen and utility functions
bool storeScreenInfoSD();
bool readScreenInfoSD();

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
bool matchPattern(const char* filename, const char* pattern);
void deleteFiles(const char* pattern,const char* directory);

uint16_t read16(File &f);
uint32_t read32(File &f);

#endif