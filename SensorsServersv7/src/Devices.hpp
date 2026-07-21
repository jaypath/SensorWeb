#ifndef DEVICES_HPP
#define DEVICES_HPP

#include <Arduino.h>
#include <WiFi.h>

struct STRUCT_CORE;

// Constants

//sensor flags  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)
//device types
/*

98 - clock
100 - weather server with persistent storage (ie SD card)
101 - sensor storage server with persistent storage (ie SD card)
102 - support analysis server with persistent storage
*/

// Expiration grace: 1.25 × SendingInt (integer math: SendingInt + SendingInt/4).
// Remotes use timeLogged (last received/logged update); local sensors use timeRead.
inline uint32_t sensorExpiryGraceSec(uint32_t sendingInt) {
    return sendingInt + sendingInt / 4;
}
inline uint32_t sensorExpirationTime(uint32_t freshnessTime, uint32_t sendingInt) {
    return freshnessTime + sensorExpiryGraceSec(sendingInt);
}

// Peripherals: servers typically broadcast ~every 10 min. Cap "live server" grace so a
// default SendingInt of 86400 does not delay APSTA debug access for ~30 hours.
#ifndef PERIPH_SERVER_STALE_SEC
#define PERIPH_SERVER_STALE_SEC 1800u // 30 minutes without contact → treat server as expired
#endif


// Device structure
struct ArborysDevType {
    uint64_t MAC;           // Device MAC address
    IPAddress IP;            // Device IP address
    uint8_t devType;        // Device type (server, sensor, etc.)
    uint32_t dataReceived;    // Last time data sent to device
    uint32_t dataSent;      // Last time device received from device
    uint8_t IsSet;          // Whether this device is initialized
    char devName[30];       // Device name
    FirmwareVersion firmware; //firmware version
    uint8_t Flags;          // Device flags
    uint32_t SendingInt;    // Sending interval
    bool expired;           // Whether device has expired
    // Daily ping-response metrics (reset at midnight; not used for broadcasts)
    uint8_t ping_att_ESPNow;     // ESPNow ping attempts to this device today
    uint8_t ping_success_ESPNow; // ESPNow ping successes with this device today
    uint8_t ping_att_UDP;        // UDP ping attempts to this device today
    uint8_t ping_success_UDP;    // UDP ping successes with this device today
    uint8_t ping_att_HTTP;       // HTTP ping attempts to this device today
    uint8_t ping_success_HTTP;   // HTTP ping successes with this device today
};

// Success rate percent for a modality; 0 when no attempts yet.
inline uint8_t pingSuccessRatePercent(uint8_t success, uint8_t attempts) {
    if (attempts == 0) return 0;
    return (uint8_t)((100u * (uint16_t)success) / (uint16_t)attempts);
}
// UDP routing rate: with no attempts yet, assume 100% so HTTP is not used until UDP is proven weak.
inline uint8_t udpPingSuccessRatePercent(const ArborysDevType* d) {
    if (!d) return 0;
    if (d->ping_att_UDP == 0) return 100;
    return pingSuccessRatePercent(d->ping_success_UDP, d->ping_att_UDP);
}
inline bool deviceUdpPingRateAbove50(const ArborysDevType* d) {
    return d && udpPingSuccessRatePercent(d) > 50;
}

// Sensor structure
struct ArborysSnsType {
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
    #ifdef _USESDCARD
    uint32_t lastSDUploadTime; // Time sensor data was last uploaded to SD card    
    #endif
    int16_t snsPin = -9999;    // local sensor pin; -9999 = none / remote sensor
    int16_t powerPin = -9999;
    uint8_t OverrideFlags = 0; // hub override for remote sensor flags
};

// Devices_Sensors class
class Devices_Sensors {
private:
    ArborysDevType devices[NUMDEVICES];
    ArborysSnsType sensors[NUMSENSORS];

public:
    Devices_Sensors();

    uint32_t lastSDSaveTime;
    uint32_t lastSensorSaveTime;
    uint32_t lastUpdatedTime;
    uint8_t numDevices;
    uint8_t numSensors;
    
    // Device management
    bool updateDeviceName(int16_t index, String newDeviceName);
    int16_t addDevice(uint64_t MAC, IPAddress IP, const char* devName = "", uint32_t sendingInt = 86400, uint8_t flags = 0, uint8_t devType = 0, const FirmwareVersion* firmware = nullptr);
    int16_t findDevice(uint64_t MAC);
    int16_t findDevice(IPAddress IP);
    ArborysDevType* getDeviceByDevIndex(int16_t devindex);
    ArborysDevType* devIndexToPointer(int16_t devindex);
    ArborysDevType* getDeviceBySnsIndex(int16_t snsindex);
    ArborysDevType* getDeviceByMAC(uint64_t MAC);
    uint64_t getDeviceMACByDevIndex(int16_t devindex);
    uint64_t getDeviceMACBySnsIndex(int16_t snsindex);
    IPAddress getDeviceIPByDevIndex(int16_t devindex);
    IPAddress getDeviceIPBySnsIndex(int16_t snsindex);
    IPAddress getMyDeviceIP(); //wrapper for getDeviceIPByDevIndex(-1)

    
    uint8_t getNumDevices();
    uint8_t countDev(uint8_t devType); // count the devices of the given type
    uint8_t countServers(); // count the servers
    int16_t nextServerIndex(int16_t startIndex=0, bool weatherServersOnly=false); // get the index of the next server
    // True if at least one non-expired server (devType>=100) has been heard from recently.
    // Also refreshes server.expired flags. Used by peripherals to keep APSTA for debug access.
    bool hasLiveServer(time_t currentTime = 0);
    bool isOutsideSensor(int16_t index);
    bool hasOutsideSensors(String parameter="all");
    int16_t findOutsideSensorByType(String parameter="all");
    double getAverageOutsideParameterValue(String parameter, uint32_t MoreRecentThan=0);

    uint8_t countSensors(int16_t snsType=-1,int16_t devIndex=-1); // count the sensors of the given type, and if device index is provided, count the sensors of the given type for the given device. If snsType is -1, then count all sensors.
    int16_t firstDeviceIndex();
    int16_t lastDeviceIndex();
    bool isDeviceInit(int16_t devindex);
    int16_t initDevice(int16_t devindex);
    bool cycleSensors(int16_t& currentPosition, uint8_t origin =0);
    // Sensor management
    int16_t addSensor(uint64_t deviceMAC, IPAddress deviceIP, uint8_t snsType, uint8_t snsID, 
                     const char* snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, 
                     uint32_t sendingInt, uint8_t flags, const char* devName = "", uint8_t devType = 0, int16_t snsPin = -9999, int16_t powerPin = -9999);
    int16_t findSensor(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID);
    int16_t findSensor(IPAddress deviceIP, uint8_t snsType, uint8_t snsID);
    int16_t findSensor(int16_t deviceIndex, uint8_t snsType, uint8_t snsID);
    int16_t findSensorByPointer(ArborysSnsType* P);
    int16_t findMySensorBySnsTypeAndID(int16_t snsType, int16_t snsID);
    ArborysSnsType* snsIndexToPointer(int16_t snsindex);
    ArborysSnsType* getSensorBySnsIndex(int16_t snsindex);
    uint8_t getNumSensors();
    bool isSensorInit(int16_t index);
    void initSensor(int16_t index);

    
    // Utility functions
    int16_t findOldestDevice();
    int16_t findOldestSensor();
    int8_t isSensorFlagged(int16_t snsIndex, uint16_t optionalsnsflags, uint16_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan, bool countCriticalExpired, bool countAnyExpired, uint8_t snsType=0, bool useOverrideFlags=true);
    bool matchesMainScreenAlert(int16_t snsIndex, bool respectRemoteOverride = true);
    uint16_t countMainScreenAlerts(bool respectRemoteOverride = true);
    uint16_t countMainScreenFlaggedAlerts(bool respectRemoteOverride = true);
    uint16_t countMainScreenCriticalExpiredAlerts(bool respectRemoteOverride = true);
    int8_t countFlagged(int16_t snsType, uint16_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan, bool countCriticalExpired, bool countAnyExpired, uint16_t optionalsnsflags); 
    String getSensorTypeFlaggedString(byte snstypeindex);

    uint8_t getSensorFlag(int16_t index);
    byte checkExpirationAllSensors(time_t currentTime, bool onlyCritical, uint8_t multiplier, bool expireDevice);
    ArborysDevType* getNextExpiredDevice(int16_t& startIndex);
    int16_t checkExpirationDevice(int16_t index, time_t currentTime, bool onlyCritical, uint8_t multiplier);
    int16_t checkExpirationSensor(int16_t index, time_t currentTime, bool onlyCritical, uint8_t multiplier, bool expireDevice);
    void checkDeviceFlags();
    void resetDailyPingCounters();
    uint8_t returnBatteryPercentage(ArborysSnsType* P);

    // Search functions
    void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
    uint8_t find_sensor_count(String snsname, uint8_t snsType);
    uint8_t findSensorByName(String snsname, uint8_t snsType, uint8_t snsID = 0);
    int16_t findSnsOfType(uint8_t snstype, bool newest = false, int16_t startIndex = -1);
    int16_t findSnsOfType(const char* snstype, bool newest = false, int16_t startIndex = -1);
    int16_t findMyDeviceIndex();
    void updateMyDeviceVersion();


    //peripheral specific functions
    uint32_t makeSensorID(uint8_t snsType, uint8_t snsID=-1, int16_t devID=-1); //make the sensor ID for this sensor. If snsID is -1, then it will be the next available sensor ID for the given sensor type
    uint32_t makeSensorID(ArborysSnsType* sensor); //make the sensor ID for this sensor
    uint32_t makeSensorID(int16_t index); //make the sensor ID for this sensor
    int16_t findSensorBySensorID(uint32_t sensorID);
    int16_t findSensorBySensorID(ArborysSnsType* sensor);
    //informational functions
    bool isMySensor(int16_t index);
    int16_t isDeviceIndexValid(int16_t index);
    int16_t isSensorIndexValid(int16_t index);
    uint16_t isSensorIndexInvalid(int16_t index, bool checkExpired=false);
    bool isSensorOfType(int16_t index, String type);
    bool isSensorOfType(ArborysSnsType* sensor, String type);
    bool isSensorOfType(const ArborysSnsType* sensor, String type);
    bool isSensorOfType(uint8_t snsType, String type);
    String sensorIsOfType(int16_t index);
    String sensorIsOfType(ArborysSnsType* sensor);
    String sensorIsOfType(uint8_t snsType);


    #ifdef _USESDCARD
    // Data storage
    bool setWriteTimestamp(int16_t sensorIndex, uint32_t timeWritten=0);
    uint8_t storeDevicesSensorsArrayToSD(uint8_t intervalMinutes);
    bool readDevicesSensorsArrayFromSD();
    int16_t storeAllSensorsSD(uint8_t intervalMinutes);
    #endif
    

    
    // Legacy compatibility functions (now removed)
    // All SensorVal-based functions have been eliminated
};

// Global instance
extern Devices_Sensors Sensors;
extern STRUCT_CORE I;



// All device IP addresses are now stored as uint32_t.

#endif
