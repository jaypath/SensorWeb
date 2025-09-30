#ifdef _USESDCARD

#if !defined(SDCARD_HPP) 
#define SDCARD_HPP


#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include "globals.hpp"
#include "timesetup.hpp"

#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
#endif

extern STRUCT_CORE I;


// Function to maintain a timestamp index file
void updateTimestampIndex(const char* filename);

// Function to remove a file from the timestamp index
void removeFromTimestampIndex(const char* filename);





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
    uint32_t fileWriteTimestamp;  // When this data was written to the file
};




// Helper functions for data retrieval
bool storeDevicesSensorsSD(); //store the in memory devicesensors class to sd, which is the current status of the devices and sensors
bool readDevicesSensorsSD();


bool storeSensorDataSD(int16_t sensorIndex); //add the in memory sensor data class to sd file, creating a history of sensor data
bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID);

int16_t loadSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* t, double* v, uint8_t f[], uint32_t timeStart = 0, uint32_t timeEnd = 0xFFFFFFFF, uint16_t Npts = 100); //load sensor data from a file. Generally the retrieve functions are used and wrap around this function
int16_t loadAverageSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* averagedTimes, double* averagedValues, uint8_t averagedFlags[], uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize, uint16_t numPointsX);

// Data retrieval functions
//these functions wrap around the load functions, but provide error checking and logging
bool retrieveSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, byte *N, uint32_t t[], double v[], uint8_t f[], uint32_t timeStart, uint32_t timeEnd, bool forwardOrder = true);
bool retrieveMovingAverageSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,
                                         uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize,
                                         uint16_t* numPointsX, double* averagedValues, uint32_t* averagedTimes, uint8_t* averagedFlags, bool forwardOrder = true);



bool storeWeatherDataSD(); //store the in memory weather data class to sd, which is the current status of the weather data
bool readWeatherDataSD();


String createSensorFilename(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID); //create a filename for a sensor data file


double findNearestValue(const std::vector<SensorDataPoint>& dataPoints, uint32_t targetTime);

// Functions for storing/reading all sensors
bool writeErrorToSD();
bool readErrorFromSD(String* error, uint16_t errornumber=0);

// Screen and utility functions
bool storeScreenInfoSD();
bool readScreenInfoSD();

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
bool matchPattern(const char* filename, const char* pattern);
uint16_t deleteFiles(const char* pattern,const char* directory);
uint16_t deleteCoreStruct();

uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);

#ifdef _USEGSHEET

bool storeGsheetInfoSD();
bool readGsheetInfoSD();

#endif //_USEGSHEET

#endif //SDCARD_HPP

#endif //_USESDCARD