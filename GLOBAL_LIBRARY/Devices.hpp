#ifndef DEVICES_HPP
#define DEVICES_HPP

#include <Arduino.h>
#include <WiFi.h>
#include "globals.hpp"

// Constants
#define SENSORNUM NUMSENSORS

//sensor flags  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)


// Device structure
struct DevType {
    uint64_t MAC;           // Device MAC address
    uint32_t IP;            // Device IP address
    uint8_t devType;        // Device type (server, sensor, etc.)
    uint32_t timeLogged;    // Last time device was seen
    uint32_t timeRead;      // Last time device sent data
    uint8_t IsSet;          // Whether this device is initialized
    char devName[30];       // Device name
    uint8_t Flags;          // Device flags
    uint32_t SendingInt;    // Sending interval
    bool expired;           // Whether device has expired
};

// Sensor structure
struct SnsType {
    int16_t deviceIndex;    // Index to parent device
    uint8_t snsType;        // Sensor type (temperature, humidity, etc.)
    uint8_t snsID;          // Sensor ID
    char snsName[30];       // Sensor name
    double snsValue;        // Current sensor value
    uint32_t timeRead;      // Time sensor was read
    uint32_t timeLogged;    // Time sensor data was logged
    uint8_t Flags;          // Sensor flags... 
    uint32_t SendingInt;    // Sending interval
    uint8_t IsSet;          // Whether this sensor is initialized
    bool expired;           // Whether sensor has expired
};

// Devices_Sensors class
class Devices_Sensors {
private:
    DevType devices[NUMDEVICES];
    SnsType sensors[NUMSENSORS];
    uint8_t numDevices;
    uint8_t numSensors;

public:
    Devices_Sensors();
    
    // Device management
    int16_t addDevice(uint64_t MAC, uint32_t IP, const char* devName = "", uint32_t sendingInt = 3600, uint8_t flags = 0);
    int16_t findDevice(uint64_t MAC);
    int16_t findDevice(uint32_t IP);
    DevType* getDeviceByDevIndex(int16_t devindex);
    DevType* getDeviceBySnsIndex(int16_t snsindex);
    uint64_t getDeviceMACByDevIndex(int16_t devindex);
    uint64_t getDeviceMACBySnsIndex(int16_t snsindex);
    uint32_t getDeviceIPByDevIndex(int16_t devindex);
    uint32_t getDeviceIPBySnsIndex(int16_t snsindex);
    uint8_t getNumDevices();
    uint8_t countDev(); // Legacy compatibility function
    bool isDeviceInit(int16_t devindex);
    void initDevice(int16_t devindex);
    
    // Sensor management
    int16_t addSensor(uint64_t deviceMAC, uint32_t deviceIP, uint8_t snsType, uint8_t snsID, 
                     const char* snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, 
                     uint32_t sendingInt, uint8_t flags, const char* devName = "");
    int16_t findSensor(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID);
    int16_t findSensor(uint32_t deviceIP, uint8_t snsType, uint8_t snsID);
    SnsType* getSensorBySnsIndex(int16_t snsindex);
    uint8_t getNumSensors();
    bool isSensorInit(int16_t index);
    void initSensor(int16_t index);
    
    // Utility functions
    int16_t findOldestDevice();
    int16_t findOldestSensor();
    byte checkExpiration(int16_t index, time_t currentTime, bool onlyCritical);
    uint8_t countFlagged(int16_t snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan);
    
    // Search functions
    void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
    uint8_t find_sensor_count(String snsname, uint8_t snsType);
    uint8_t findSensorByName(String snsname, uint8_t snsType, uint8_t snsID = 0);
    int16_t findSnsOfType(uint8_t snstype, bool newest = false);
    
    // Data storage
    bool storeAllSensors();
    bool readAllSensors();
    
    // Helper functions for expiration checking
    byte checkExpirationDevice(int16_t index, time_t currentTime, bool onlyCritical);
    byte checkExpirationSensor(int16_t index, time_t currentTime, bool onlyCritical);
    
    // Legacy compatibility functions (now removed)
    // All SensorVal-based functions have been eliminated
};

// Global instance
extern Devices_Sensors Sensors;
extern struct Screen I;


// All device IP addresses are now stored as uint32_t.

#endif
