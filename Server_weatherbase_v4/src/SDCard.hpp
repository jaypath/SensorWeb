#ifndef SDCARD_HPP
#define SDCARD_HPP

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include "globals.hpp"
#include "timesetup.hpp"


extern STRUCT_CORE I;

// New functions for Devices_Sensors class
bool storeDevicesSensorsSD();
bool readDevicesSensorsSD();

// New functions for WeatherData class
bool storeWeatherDataSD();
bool readWeatherDataSD();


// New functions for individual sensor data storage using Devices_Sensors class
bool storeSensorDataSD(int16_t sensorIndex);
bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID);
bool readSensorDataSD(int16_t sensorIndex);
bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, uint32_t endtime, byte avgN);
bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, byte avgN);

// Common data structure for sensor data points
struct SensorDataPoint {
    uint64_t deviceMAC;
    uint16_t deviceIndex;
    uint8_t snsType;
    uint8_t snsID;
    double snsValue;
    uint32_t timeRead;
    uint32_t timeLogged;
    uint8_t Flags;
    uint32_t SendingInt;
    char snsName[30];
};

// Helper functions for data retrieval
int16_t loadSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* t, double* v, uint32_t timeStart = 0, uint32_t timeEnd = 0xFFFFFFFF, uint16_t Npts = 100);
String createSensorFilename(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID);
double findNearestValue(const std::vector<SensorDataPoint>& dataPoints, uint32_t targetTime);

// Data retrieval functions
bool retrieveSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, byte *N, uint32_t t[], double v[], uint32_t timeStart, uint32_t timeEnd, bool forwardOrder = true);

// Advanced data processing functions
bool retrieveMovingAverageSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,
                                         uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize,
                                         uint16_t* numPointsX, double* averagedValues, uint32_t* averagedTimes, bool forwardOrder = true);
// Functions for storing/reading all sensors
bool storeAllSensorSD();
bool readAllSensorsSD();
bool writeErrorToSD();
bool readErrorFromSD(String* error, uint16_t errornumber=0);

// Screen and utility functions
bool storeScreenInfoSD();
bool readScreenInfoSD();

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
bool matchPattern(const char* filename, const char* pattern);
void deleteFiles(const char* pattern,const char* directory);

uint16_t read16(File &f);
uint32_t read32(File &f);

#endif