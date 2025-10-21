#ifndef DEVICES_HPP
#define DEVICES_HPP

#include <Arduino.h>
#include <WiFi.h>

// Constants
#define NUMDEVICES 50
#define NUMSENSORS 100
#define SENSORNUM NUMSENSORS

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
    uint8_t Flags;          // Sensor flags
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
    DevType* getDeviceByIndex(int16_t index);
    uint8_t getNumDevices();
    bool isDeviceInit(int16_t index);
    void initDevice(int16_t index);
    
    // Sensor management
    int16_t addSensor(uint64_t deviceMAC, uint32_t deviceIP, uint8_t snsType, uint8_t snsID, 
                     const char* snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, 
                     uint32_t sendingInt, uint8_t flags);
    int16_t findSensor(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID);
    int16_t findSensor(uint32_t deviceIP, uint8_t snsType, uint8_t snsID);
    SnsType* getSensorByIndex(int16_t index);
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
    uint8_t find_sensor_name(String snsname, uint8_t snsType, uint8_t snsID = 0);
    int16_t findSns(uint8_t snstype, bool newest = false);
    
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

// All device IP addresses are now stored as uint32_t.

#endif
