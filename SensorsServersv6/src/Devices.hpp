#ifndef DEVICES_HPP
#define DEVICES_HPP

#include <Arduino.h>
#include <WiFi.h>

struct STRUCT_CORE;

// Constants

//sensor flags  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)


// Device structure
struct DevType {
    uint64_t MAC;           // Device MAC address
    IPAddress IP;            // Device IP address
    uint8_t devType;        // Device type (server, sensor, etc.)
    uint32_t dataReceived;    // Last time data sent to device
    uint32_t dataSent;      // Last time device received from device
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
    uint32_t timeWritten;     // Time sensor data was written to SD card
    uint8_t Flags;          // Sensor flags... 
    uint32_t SendingInt;    // Sending interval
    uint8_t IsSet;          // Whether this sensor is initialized
    bool expired;           // Whether sensor has expired
    uint32_t lastCloudUploadTime; // Time sensor data was last uploaded to cloud
    uint32_t lastSDUploadTime; // Time sensor data was last uploaded to SD card    
    #ifdef _ISPERIPHERAL
    int16_t snsPin;            // pin number for the sensor, if applicable. 0-99 is anolog in pin, 100-199 is MUX address, 200-299 is digital in pin, 300-399 is SPI pin, 400-599 is an I2C address. Negative values mean the same, but that there is an associated power pin. -9999 means no pin. 
    int16_t powerPin;          // pin number for the power pin, if applicable. -9999 means no power pin If -9999 then there is no power pin, if -200 then it is a special case
    #endif
};

// Devices_Sensors class
class Devices_Sensors {
private:
    DevType devices[NUMDEVICES];
    SnsType sensors[NUMSENSORS];

public:
    Devices_Sensors();

    uint32_t lastSDSaveTime;
    uint32_t lastSensorSaveTime;
    uint32_t lastUpdatedTime;
    uint8_t numDevices;
    uint8_t numSensors;
    
    // Device management
    bool updateDeviceName(int16_t index, String newDeviceName);
    int16_t addDevice(uint64_t MAC, IPAddress IP, const char* devName = "", uint32_t sendingInt = 3600, uint8_t flags = 0, uint8_t devType = 0);
    int16_t findDevice(uint64_t MAC);
    int16_t findDevice(IPAddress IP);
    DevType* getDeviceByDevIndex(int16_t devindex);
    DevType* getDeviceBySnsIndex(int16_t snsindex);
    uint64_t getDeviceMACByDevIndex(int16_t devindex);
    uint64_t getDeviceMACBySnsIndex(int16_t snsindex);
    IPAddress getDeviceIPByDevIndex(int16_t devindex);
    IPAddress getDeviceIPBySnsIndex(int16_t snsindex);
    uint8_t getNumDevices();
    uint8_t countDev(uint8_t devType); // count the devices of the given type
    uint8_t countSensors(uint8_t snsType,int16_t devIndex=-1); // count the sensors of the given type, and if device index is provided, count the sensors of the given type for the given device
    int16_t firstDeviceIndex();
    int16_t lastDeviceIndex();
    bool isDeviceInit(int16_t devindex);
    int16_t initDevice(int16_t devindex);
    bool cycleSensors(uint8_t* currentPosition, uint8_t origin =0);
    // Sensor management
    uint16_t isSensorIndexInvalid(int16_t index);
    int16_t addSensor(uint64_t deviceMAC, IPAddress deviceIP, uint8_t snsType, uint8_t snsID, 
                     const char* snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, 
                     uint32_t sendingInt, uint8_t flags, const char* devName = "", uint8_t devType = 0, int16_t snsPin = -9999, int16_t powerPin = -9999);
    int16_t findSensor(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID);
    int16_t findSensor(IPAddress deviceIP, uint8_t snsType, uint8_t snsID);
    int16_t findSensor(int16_t deviceIndex, uint8_t snsType, uint8_t snsID);
    SnsType* getSensorBySnsIndex(int16_t snsindex);
    uint8_t getNumSensors();
    bool isSensorInit(int16_t index);
    void initSensor(int16_t index);

    
    // Utility functions
    int16_t findOldestDevice();
    int16_t findOldestSensor();
    byte checkExpiration(int16_t index, time_t currentTime, bool onlyCritical);
    uint8_t countFlagged(int16_t snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan=0, bool countCriticalExpired=false, bool countAnyExpired=false);
    bool isSensorOfType(int16_t index, String type);
    uint8_t getSensorFlag(int16_t index);

    // Search functions
    void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
    uint8_t find_sensor_count(String snsname, uint8_t snsType);
    uint8_t findSensorByName(String snsname, uint8_t snsType, uint8_t snsID = 0);
    int16_t findSnsOfType(uint8_t snstype, bool newest = false, int16_t startIndex = -1);
    int16_t findSnsOfType(const char* snstype, bool newest = false, int16_t startIndex = -1);

    //peripheral specific functions
    #ifdef _ISPERIPHERAL
    int16_t getPrefsIndex(uint8_t snsType, uint8_t snsID, int16_t devID=-1); //get the preferences index for this sensor
    uint32_t makeSensorID(uint8_t snsType, uint8_t snsID=-1, int16_t devID=-1); //make the sensor ID for this sensor. If snsID is -1, then it will be the next available sensor ID for the given sensor type
    #endif
    int16_t findMyDeviceIndex();
    bool isMySensor(int16_t index);
    int16_t isDeviceIndexValid(int16_t index);
    int16_t isSensorIndexValid(int16_t index);
    #ifdef _USESDCARD
    // Data storage
    bool setWriteTimestamp(int16_t sensorIndex, uint32_t timeWritten=0);
    uint8_t storeDevicesSensorsArrayToSD(uint8_t intervalMinutes);
    bool readDevicesSensorsArrayFromSD();
    int16_t storeAllSensorsSD(uint8_t intervalMinutes);
    #endif
    

    // Helper functions for expiration checking
    byte checkExpirationDevice(int16_t index, time_t currentTime, bool onlyCritical);
    byte checkExpirationSensor(int16_t index, time_t currentTime, bool onlyCritical);
    
    // Legacy compatibility functions (now removed)
    // All SensorVal-based functions have been eliminated
};

// Global instance
extern Devices_Sensors Sensors;
extern STRUCT_CORE I;



// All device IP addresses are now stored as uint32_t.

#endif
