#include "globals.hpp"
#include "Devices.hpp"
#include "utility.hpp"
#include <TimeLib.h>

// Global instance
Devices_Sensors Sensors;
extern STRUCT_PrefsH Prefs;
#ifdef _ISPERIPHERAL
  extern STRUCT_SNSHISTORY SensorHistory;
#endif

// Constructor
Devices_Sensors::Devices_Sensors() {
    numDevices = 0;
    numSensors = 0;
    
    // Initialize all devices and sensors as unused
    for (int i = 0; i < NUMDEVICES; i++) {
        devices[i].IsSet = 0;
        devices[i].expired = false;
    }
    
    for (int i = 0; i < NUMSENSORS; i++) {
        sensors[i].IsSet = 0;
        sensors[i].expired = false;
        sensors[i].deviceIndex = -1;
    }
}

bool Devices_Sensors::cycleSensors(uint8_t* currentPosition, uint8_t origin) {
    


    bool cycling = cycleByteIndex(currentPosition,NUMSENSORS,origin);
    while (isSensorInit(*currentPosition)==false && cycling==true) {
        cycling = cycleByteIndex(currentPosition,NUMSENSORS,origin);
    }
    return cycling;
}

// Device management functions
int16_t Devices_Sensors::addDevice(uint64_t MAC, IPAddress IP, const char* devName, uint32_t sendingInt, uint8_t flags, uint8_t devType) {
    // Check if device already exists
    int16_t existingIndex = findDevice(MAC);
    if (existingIndex >= 0) {
        // Update existing device
        DevType* device = &devices[existingIndex];
        device->IP = IP;
        device->dataReceived = I.currentTime;
        //do not change time logged
        if (devName && strcmp(device->devName, devName) != 0) {
            strncpy(device->devName, devName, sizeof(device->devName) - 1);
            device->devName[sizeof(device->devName) - 1] = '\0';
        }
        if (flags != 0) device->Flags = flags;
        if (sendingInt != 0)         device->SendingInt = sendingInt;
        device->expired = false;
        device->IsSet = 1;
        if (devType != 0) device->devType = devType;
        return existingIndex;
    }
    
    // Find empty slot
    for (int16_t i = 0; i < NUMDEVICES; i++) {
        if (!devices[i].IsSet) {
            devices[i].MAC = MAC;
            devices[i].IP = IP;
            devices[i].dataSent = 0; //time logged is set to 0, because we have not sent data to this device yet
            devices[i].dataReceived = I.currentTime;
            devices[i].IsSet = 1;
            if (devName) {
                strncpy(devices[i].devName, devName, sizeof(devices[i].devName) - 1);
                devices[i].devName[sizeof(devices[i].devName) - 1] = '\0';
            } else {
                devices[i].devName[0] = '\0';
            }
            devices[i].Flags = flags;
            if (sendingInt == 0)         devices[i].SendingInt = 3600;
            else            devices[i].SendingInt = sendingInt;
            devices[i].expired = false;
            devices[i].devType = devType;
            if (i >= numDevices) {
                numDevices = i + 1;
            }
            return i;
        }
    }
    
    return -1; // No space available
}

int16_t Devices_Sensors::findDevice(uint64_t MAC) {
    for (int16_t i = 0; i < NUMDEVICES ; i++) {
        if (devices[i].IsSet && devices[i].MAC == MAC) {
            return i;
        }
    }
    return -1;
}

int16_t Devices_Sensors::findDevice(IPAddress IP) {
    for (int16_t i = 0; i < NUMDEVICES ; i++) {
        if (devices[i].IsSet && devices[i].IP == IP) {
            return i;
        }
    }
    return -1;
}

DevType* Devices_Sensors::getDeviceBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS  && sensors[snsindex].IsSet) {
        return getDeviceByDevIndex(sensors[snsindex].deviceIndex);
    }
    return nullptr;
}

DevType* Devices_Sensors::getDeviceByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES  && devices[devindex].IsSet) {
        return &devices[devindex];
    }
    return nullptr;
}

uint64_t Devices_Sensors::getDeviceMACBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS  && sensors[snsindex].IsSet) {
        return getDeviceMACByDevIndex(sensors[snsindex].deviceIndex);
    }
    return 0;
}

uint64_t Devices_Sensors::getDeviceMACByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES && devices[devindex].IsSet) {
        return devices[devindex].MAC;
    }
    return 0;
}

IPAddress Devices_Sensors::getDeviceIPBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS  && sensors[snsindex].IsSet) {
        return getDeviceIPByDevIndex(sensors[snsindex].deviceIndex);
    }
    return IPAddress(0,0,0,0);
}

IPAddress Devices_Sensors::getDeviceIPByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES  && devices[devindex].IsSet) {
        return devices[devindex].IP;
    }
    return IPAddress(0,0,0,0);
}

uint8_t Devices_Sensors::getNumDevices() {
    numDevices = 0;
    for (int16_t i = 0; i < NUMDEVICES; i++) {
        if (devices[i].IsSet) {
            numDevices++;
        }
    }
    return numDevices;
}

uint8_t Devices_Sensors::getNumSensors() {
    numSensors = 0;
    for (int16_t i = 0; i < NUMSENSORS; i++) {
        if (sensors[i].IsSet) {
            numSensors++;
        }
    }
    return numSensors;
}

uint8_t Devices_Sensors::countSensors(uint8_t snsType,int16_t devIndex) {
    //returns the number of sensors of the given type
    uint8_t count = 0;
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && sensors[i].snsType == snsType) {
            if (devIndex == -1 || sensors[i].deviceIndex == devIndex) count++;
        }
    }
    return count;
}

uint8_t Devices_Sensors::countDev(uint8_t devType) {
    //returns the number of devices of the given type
    uint8_t count = 0;
    for (int16_t i = 0; i < NUMDEVICES ; i++) {
        if (devices[i].IsSet && devices[i].devType == devType) {
            count++;
        }
    }
    return count;
}


int16_t  Devices_Sensors::initDevice(int16_t index) {
    if (index < 0 || index >= NUMDEVICES) {
        return -3;
    }
    byte sensorsinitialized = 0;

    if (devices[index].IsSet == 0) return -1;
    
    if (findMyDeviceIndex() == index) {
        return -2;
    }

    //initialize all sensors attached to this device
    for (int16_t i = 0; i < NUMSENSORS; i++) {
        if (sensors[i].IsSet && sensors[i].deviceIndex == index) {
            sensors[i].IsSet = 0;
            sensors[i].expired = false;
            sensorsinitialized++;
        }
    }

    //initialize the device
    if (index >= 0 && index < NUMDEVICES ) {
        devices[index].IsSet = 0;
        devices[index].expired = false;
    }

    //count remaining sensors and devices
    getNumDevices();
    getNumSensors();

    return sensorsinitialized;
}

// Sensor management functions
int16_t Devices_Sensors::addSensor(uint64_t deviceMAC, IPAddress deviceIP, uint8_t snsType, uint8_t snsID, 
                                  const char* snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, 
                                  uint32_t sendingInt, uint8_t flags, const char* devName, uint8_t devType, int16_t snsPin, int16_t powerPin) {
    //returns -2 if sensor could not be created, -1 if no space available, otherwise the index to the sensor
    // Find or create device
    int16_t deviceIndex = addDevice(deviceMAC, deviceIP, devName, 0, 0, devType);
    if (deviceIndex < 0) {
        storeError("Addsensor: Could not create device", ERROR_DEVICE_ADD);
        return -2; // Could not create device
    }
    
    // Check if sensor already exists
    int16_t existingIndex = findSensor(deviceMAC, snsType, snsID);
    if (existingIndex >= 0) {
        // Update existing sensor. note that timeWritten is not updated here.
        SnsType* sensor = &sensors[existingIndex];
        //update sensor name, if needed
        if (snsName) {
            strncpy(sensor->snsName, snsName, sizeof(sensor->snsName) - 1);
            sensor->snsName[sizeof(sensor->snsName) - 1] = '\0';
        }
        sensor->snsValue = snsValue;
        sensor->timeRead = timeRead;
        sensor->timeLogged = timeLogged;
        sensor->Flags = flags;
        sensor->SendingInt = sendingInt;
        sensor->expired = false;
        sensor->IsSet=1;
        Sensors.lastUpdatedTime = I.currentTime;
        #ifdef _ISPERIPHERAL
        if (snsPin !=0 && snsPin != -9999) {
            sensor->snsPin = snsPin;
            sensor->powerPin = powerPin;
        }
        #endif
        return existingIndex;
    }
    
    // Find empty slot
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) {
            sensors[i].deviceIndex = deviceIndex;
            sensors[i].snsType = snsType;
            sensors[i].snsID = snsID;
            if (snsName) {
                strncpy(sensors[i].snsName, snsName, sizeof(sensors[i].snsName) - 1);
                sensors[i].snsName[sizeof(sensors[i].snsName) - 1] = '\0';
            } else {
                sensors[i].snsName[0] = '\0';
            }
            sensors[i].snsValue = snsValue;
            sensors[i].timeRead = timeRead;
            sensors[i].timeLogged = timeLogged;
            sensors[i].timeWritten = 0;
            sensors[i].Flags = flags;
            sensors[i].SendingInt = sendingInt;
            sensors[i].IsSet = 1;
            sensors[i].expired = false;
            #ifdef _ISPERIPHERAL
            sensors[i].snsPin = snsPin;
            sensors[i].powerPin = powerPin;
            #endif
    
            
            if (i >= numSensors) {
                numSensors = i + 1;
            }
            Sensors.lastUpdatedTime = I.currentTime;
            return i;
        }
    }
    
    storeError("No space for sensor",ERROR_SENSOR_ADD);
    return -1; // No space available
}

int16_t Devices_Sensors::findSensor(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID) {
    int16_t deviceIndex = findDevice(deviceMAC);
    if (deviceIndex < 0) {
        return -1;
    }
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && sensors[i].deviceIndex == deviceIndex && 
            sensors[i].snsType == snsType && sensors[i].snsID == snsID) {
            return i;
        }
    }

    return -1;
}

int16_t Devices_Sensors::findSensor(int16_t deviceIndex, uint8_t snsType, uint8_t snsID) {
    if (deviceIndex < 0 || deviceIndex >= NUMDEVICES) {
        return -1;
    }
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && sensors[i].deviceIndex == deviceIndex && 
            sensors[i].snsType == snsType && sensors[i].snsID == snsID) {
            return i;
        }
    }
    return -1;
}

int16_t Devices_Sensors::findSensor(IPAddress deviceIP, uint8_t snsType, uint8_t snsID) {
    int16_t deviceIndex = findDevice(deviceIP);
    if (deviceIndex < 0) {
        return -1;
    }
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && sensors[i].deviceIndex == deviceIndex && 
            sensors[i].snsType == snsType && sensors[i].snsID == snsID) {
            return i;
        }
    }
    
    return -1;
}

SnsType* Devices_Sensors::getSensorBySnsIndex(int16_t index) {
    if (index >= 0 && index < NUMSENSORS  && sensors[index].IsSet) {
        return &sensors[index];
    }
    return nullptr;
}


bool Devices_Sensors::isSensorInit(int16_t index) {
    return (index >= 0 && index < NUMSENSORS &&  sensors[index].IsSet);
}

void Devices_Sensors::initSensor(int16_t index) {
    if (index >= 0 && index < NUMSENSORS ) {
        sensors[index].IsSet = 0;
        sensors[index].expired = false;
        sensors[index].deviceIndex = -1;
    }
}

// Utility functions
int16_t Devices_Sensors::findOldestDevice() {
    int16_t oldestIndex = -1;
    uint32_t oldestTime = 0xFFFFFFFF;
    
    for (int16_t i = 0; i < NUMDEVICES ; i++) {
        if (devices[i].IsSet && devices[i].dataReceived < oldestTime) {
            oldestTime = devices[i].dataReceived;
            oldestIndex = i;
        }
    }
    
    return oldestIndex;
}

int16_t Devices_Sensors::findOldestSensor() {
    int16_t oldestIndex = -1;
    uint32_t oldestTime = 0xFFFFFFFF;
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && sensors[i].timeLogged < oldestTime) {
            oldestTime = sensors[i].timeLogged;
            oldestIndex = i;
        }
    }
    
    return oldestIndex;
}



uint8_t Devices_Sensors::countFlagged(int16_t snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan, bool countCriticalExpired, bool countAnyExpired) { //provide the sensortypes, where this can include -1 for all temperature, -2 for all humidity, -9 for all pressure. Flag settings is a bitmask of the flags that matter (0b00000011 = flagged and monitored). MoreRecentThan is the time in seconds since the last update.
    ////  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored and expired- alert if no updates received within time limit specified)
    //if snsType is 0, then count all sensors (meeting the flag criteria)
    //if snsType is -1, then count all temperature sensors (meeting the flag criteria)
    //if snsType is -2, then count all humidity sensors (meeting the flag criteria)
    //if snsType is -3, then count all soil sensors (meeting the flag criteria)
    //if snsType is -9, then count all pressure sensors (meeting the flag criteria)
    //if snstype is -54 to -57, then count all HVAC sensors (meeting the flag criteria)
    //if snsType is <=-100, then count all server sensors (meeting the flag criteria)
    //if snsType is <=-1000 then use flags to specify multiple types: 0 = all EXCEPT the specified types , 1 = temp, 2 = humidity, 3 = soil, 4 = pressure, 5 = HVAC, 6 = server, 7 = dist, 8 = binary...

    uint16_t count = 0;
    int16_t snsFlagType;
    if (snsType < -1000) {
        snsFlagType = (-1 *snsType)-1000;
        //flags are as follows: but 0 means all types, 1 means temp, 2 means humidity, 3 means soil, 4 means pressure, 5 means HVAC, 6 means server, 7 means dist, 8 means binary...
    } else snsFlagType = 0;

    //count the number of sensors that match the flags
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;
        
        // Check sensor type filter
        if (snsType != 0) { //special case for all sensors
            if (snsType > 0 && sensors[i].snsType != snsType) continue;
            if (snsType == -1 && (isSensorOfType(i,"temperature") == false)) continue; // Temperature sensors only
            if (snsType == -2 && (isSensorOfType(i,"humidity") == false)) continue; // Humidity sensors only
            if ((snsType == -3 ) && (isSensorOfType(i,"soil") == false)) continue; // Soil sensors only
            if (snsType == -9 && (isSensorOfType(i,"pressure") == false)) continue; // Pressure sensors only
            if (snsType <= -54 && snsType >= -57 && (isSensorOfType(i,"HVAC") == false)) continue; // HVAC sensors only
            if (snsType == -100 && (isSensorOfType(i,"server") == false)) continue; // Server sensors only
            if (snsType == -1000) {
                //use I.showTheseFlags to determine which types to count
                bool isgood = false;
                if (bitRead(I.showTheseFlags, 11) == 1) isgood = true;
                if (bitRead(I.showTheseFlags, 3) == 1 && isSensorOfType(i,"leak") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 2) == 1 && isSensorOfType(i,"soil") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 4) == 1 && isSensorOfType(i,"temperature") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 5) == 1 && isSensorOfType(i,"humidity") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 6) == 1 && isSensorOfType(i,"pressure") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 7) == 1 && isSensorOfType(i,"battery") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 8) == 1 && isSensorOfType(i,"HVAC") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 9) == 1 && isSensorOfType(i,"human") == true)   isgood = true;
                if (bitRead(I.showTheseFlags, 10) == 1 && isSensorOfType(i,"distance") == true)   isgood = true;
                if (isgood == false) continue; //sensor does not meet the criteria
            }
            if (snsType  < -1000) {
                //multiple selectable types, based on provided value
                bool isgood = false;
                bool isgoodupdated = false;

                if (bitRead(snsFlagType, 0)) {
                    isgood = true;
                    isgoodupdated = false;
                } else {
                    isgood = false;
                    isgoodupdated = true;
                }
                if (bitRead(snsFlagType, 1) && isSensorOfType(i,"temperature") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 2) && isSensorOfType(i,"humidity") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 3) && isSensorOfType(i,"soil") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 4) && isSensorOfType(i,"pressure") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 5) && isSensorOfType(i,"HVAC") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 6) && isSensorOfType(i,"server") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 7) && isSensorOfType(i,"dist") == true) isgood = isgoodupdated;
                if (bitRead(snsFlagType, 8) && isSensorOfType(i,"binary") == true) isgood = isgoodupdated;
                if (isgood == false) continue; //sensor does not meet the criteria
            }
        }
        // Check time filter
        if (MoreRecentThan > 0 && sensors[i].timeRead < MoreRecentThan) continue;
        
        // Check flags
        uint8_t sensorFlags = sensors[i].Flags & flagsthatmatter;
        if (sensorFlags == flagsettings || (countCriticalExpired && bitRead(sensors[i].Flags,7) && sensors[i].expired) || (countAnyExpired && sensors[i].expired)) {
            count++;
        }
    }

    return count;
}

// Search functions
void Devices_Sensors::find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow) {
    *snsIndexHigh = 0;
    *snsIndexLow = 255;
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;
        if (sensors[i].snsType != snsType) continue;
        if (snsname != sensors[i].snsName) continue;
        
        if (sensors[i].snsID > *snsIndexHigh) *snsIndexHigh = sensors[i].snsID;
        if (sensors[i].snsID < *snsIndexLow) *snsIndexLow = sensors[i].snsID;
    }
}

uint8_t Devices_Sensors::find_sensor_count(String snsname, uint8_t snsType) {
    uint8_t count = 0;
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;
        if (sensors[i].snsType != snsType) continue;
        if (snsname == sensors[i].snsName) {
            count++;
        }
    }
    
    return count;
}

uint8_t Devices_Sensors::findSensorByName(String snsname, uint8_t snsType, uint8_t snsID) {
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;
        // Only compare snsType if snsType > 0
        if (snsType > 0 && sensors[i].snsType != snsType) continue;
        // Only compare snsID if both snsID > 0 and snsType > 0
        if (snsType > 0 && snsID > 0 && sensors[i].snsID != snsID) continue;
        // Partial match: snsname is a substring of sensors[i].snsName
        if (snsname.length() > 0 && String(sensors[i].snsName).indexOf(snsname) == -1) continue;
        return i;
    }
    return 255;
}


int16_t Devices_Sensors::findSnsOfType(const char* snstype, bool newest, int16_t startIndex) {
    if (startIndex == -1) startIndex = 0;
    int16_t targetIndex = -1;
    uint32_t targetTime = newest ? 0 : 0xFFFFFFFF;
    
    for (int16_t i = startIndex; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;
        if (isSensorOfType(i, snstype) == false) continue;
        
        if (newest) {
            if (sensors[i].timeLogged > targetTime) {
                targetTime = sensors[i].timeLogged;
                targetIndex = i;
            }
        } else {
            if (sensors[i].timeLogged < targetTime) {
                targetTime = sensors[i].timeLogged;
                targetIndex = i;
            }
        }
    }
    
    return targetIndex;
}



int16_t Devices_Sensors::findSnsOfType(uint8_t snstype, bool newest, int16_t startIndex) {
    if (startIndex == -1) startIndex = 0;
    int16_t targetIndex = -1;
    uint32_t targetTime = newest ? 0 : 0xFFFFFFFF;
    
    for (int16_t i = startIndex; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;
        if (sensors[i].snsType != snstype) continue;
        
        if (newest) {
            if (sensors[i].timeLogged > targetTime) {
                targetTime = sensors[i].timeLogged;
                targetIndex = i;
            }
        } else {
            if (sensors[i].timeLogged < targetTime) {
                targetTime = sensors[i].timeLogged;
                targetIndex = i;
            }
        }
    }
    
    return targetIndex;
}

#ifdef _USESDCARD
// Data storage functions
int16_t Devices_Sensors::storeAllSensorsSD(uint8_t intervalMinutes) {
    //march through all sensors and write to SD card if timeWritten is older than 1 hour
    uint8_t count = 0;

    if (intervalMinutes==0) intervalMinutes=10;//never write to SD card more than once per minute
    if (Sensors.lastSensorSaveTime+intervalMinutes*60<I.currentTime) { 
        Sensors.lastSensorSaveTime = I.currentTime;    
        for (int16_t i = 0; i < NUMSENSORS; i++) {
            if (Sensors.isSensorIndexInvalid(i)==0 && sensors[i].timeWritten + 3600 < I.currentTime) {
              storeSensorDataSD(i);
              count++;
            }
          }
      
    return count;
    }
    return -1; //not time yet
    
  }


  
bool Devices_Sensors::setWriteTimestamp(int16_t sensorIndex, uint32_t timeWritten) {
    if (isSensorIndexInvalid(sensorIndex)!=0) {
        return false;
    }
    if (timeWritten == 0) sensors[sensorIndex].timeWritten = I.currentTime;
    else sensors[sensorIndex].timeWritten = timeWritten;

    return true;
}

uint8_t Devices_Sensors::storeDevicesSensorsArrayToSD(uint8_t intervalMinutes) {
    //stores devicesensors array to sd
    if (intervalMinutes==0) intervalMinutes=1;//never write to SD card more than once per minute
    if (lastSDSaveTime+intervalMinutes*60<I.currentTime) {
        lastSDSaveTime = I.currentTime;    
        if (storeDevicesSensorsSD()==false) return -1; //failed to save
        return 1;
    }
    return 0; //not time yet
}

bool Devices_Sensors::readDevicesSensorsArrayFromSD() {
    readDevicesSensorsSD();
    return true;
}

#endif

// Helper functions for expiration checking
bool Devices_Sensors::isDeviceInit(int16_t index) {
//returns true if the device is set and initialized, false if index is out of bounds or device is not set

    return (index >= 0 && index < NUMDEVICES  && devices[index].IsSet);
}

int16_t Devices_Sensors::firstDeviceIndex() {
    //returns the index to the first device that is set and initialized
    uint16_t index = 0;
    while (isDeviceInit(index)==false) {
        if (index >= NUMDEVICES - 1) return -1;
        index++;
    }
    return index;
}

int16_t Devices_Sensors::lastDeviceIndex() {
    //returns the index to the last device that is set and initialized
    uint16_t index = NUMDEVICES - 1;
    while (isDeviceInit(index)==false) {
        if (index == 0) return -1;
        index--;
    }
    return index;
}

int16_t Devices_Sensors::isDeviceIndexValid(int16_t index) {
    //returns more info than isDeviceInit : -2 = I am not registered and could not be registered (fatal error, regardless of whether the provided index is valid or not),-1 = not set, 0 = invalid, 1 = set and mine, 2 = set and not mine, 3 = expired and mine, 4 = expired and not mine
    if (index < 0 || index >= NUMDEVICES ) {
        return 0; //invalid index
    }
    if (devices[index].IsSet) {
        
        int16_t ismine = findMyDeviceIndex();
        if (ismine == -1) {
            //I am not registered as a device, so register me
            ismine = addDevice(ESP.getEfuseMac(), WiFi.localIP(), Prefs.DEVICENAME, 0, 0, _MYTYPE);
            if (ismine == -1) {
                //failed to register me, so return invalid
                return -2;
            }
        }

        if (index == ismine ) {
            //check if expired jspxx
            if (devices[index].expired) {
                return 3; //expired and is mine
            } else {
                return 1; //set and is mine
            }
        } else {
            if (devices[index].expired) {
                return 4; //expired and not mine
            } else {
                return 2; //set and not mine
            }
        }
    }
    return -1; //not set, but valid
}

int16_t Devices_Sensors::isSensorIndexValid(int16_t index) {
    //checks if the sensor index is VALID
    //0 = invalid (out of bounds), 1 = set and mine, 2 = set and not mine, 3 = expired and mine, 4 = expired and not mine, -1 = not set
    if (index < 0 || index >= NUMSENSORS ) {
        //index is invalid
        return 0;
    }
    if (sensors[index].IsSet) {
        if (isMySensor(index)) {
            if (sensors[index].expired) {
                return 3; //expired and is mine //jspxx
            } else {
                return 1; //set and is mine
            }
        } else {
            if (sensors[index].expired) {
                return 4; //expired and not mine
            } else {
                return 2; //set and not mine
            }
        }
    }
    return -1; //not set, but valid
}


uint16_t Devices_Sensors::isSensorIndexInvalid(int16_t index) {
    //returns 1 if invalid (out of bounds), 2 if not set, 3 if expired, 0 if valid
    if (index < 0 || index >= NUMSENSORS ) {
        return 1;
    }
    if (!sensors[index].IsSet) {
        return 2;
    }
    if (sensors[index].expired) {
        return 3;
    }
    return 0;
}

int16_t Devices_Sensors::checkExpirationDevice(int16_t index, time_t currentTime, bool onlyCritical) {
    if (index < 0 || index >= NUMDEVICES  || !devices[index].IsSet) {
        return 0;
    }
    
    if (currentTime == 0) currentTime = I.currentTime;

    DevType* device = &devices[index];

    uint32_t expirationTime = device->dataReceived;
    if (expirationTime < device->dataSent) expirationTime = device->dataSent;  //expiration time is the more recent of the data received or data sent


    expirationTime += device->SendingInt * 2; // 2x sending interval
    
    if (currentTime > expirationTime) {
        if (onlyCritical && !bitRead(device->Flags, 7)) {
            return 0; // Only check critical devices
        }
        device->expired = true;
        return 1;
    }
    
    device->expired = false;
    return 0;
}

byte Devices_Sensors::checkExpirationAllSensors(time_t currentTime, bool onlyCritical) {
    byte count = 0;
    for (int16_t i = 0; i < NUMSENSORS; i++) {
        if (checkExpirationSensor(i, currentTime, onlyCritical) == 1) {
            count++;
        }
    }
    return count;
}

int16_t Devices_Sensors::checkExpirationSensor(int16_t index, time_t currentTime, bool onlyCritical) {
    //returns -1 if invalid (out of bounds), -2 if not set, -3 if expired,-5 if this is not a critical sensor and onlyCritical is true, -10 if indeterminate (no sending interval set), 0 if valid not expired, 1 if expired
    
    int16_t result = isSensorIndexInvalid(index);
    if (result != 0) return -1*result;
    if (onlyCritical && !bitRead(sensors[index].Flags, 7)) return -5;

    uint16_t sendint = sensors[index].SendingInt;
    if (sendint==0) return -10;
    uint32_t expirationTime = sensors[index].timeLogged + sendint + 120; // 2 minute buffer

    if (currentTime > expirationTime) {
        sensors[index].expired = true;
        return 1;
    }

    return 0;
}

uint8_t Devices_Sensors::getSensorFlag(int16_t index) {
    return sensors[index].Flags;
}

    

bool Devices_Sensors::isSensorOfType(int16_t index, String type) {
    return isSensorOfType(sensors[index].snsType, type);
}

bool Devices_Sensors::isSensorOfType(SnsType* sensor, String type) {
    if (sensor == NULL) return false;
    return isSensorOfType(sensor->snsType, type);
}

bool Devices_Sensors::isSensorOfType(uint8_t snsType, String type) {

    if (type == "temperature") {//temperature
        return (snsType == 1 || snsType == 4 || snsType == 10 || snsType == 14 || snsType == 17);
    }
    if (type == "humidity") {//humidity
        return (snsType == 2 || snsType == 5 || snsType == 15 || snsType == 18);
    }
    if (type == "pressure") {//pressure
        return (snsType == 9 || snsType == 13 || snsType == 19);
    }
    if (type == "battery") {//battery
        return (snsType == 60 || snsType == 61);
    }
    if (type == "HVAC") {//HVAC
        return (snsType >= 50 && snsType < 60);
    }
    if (type == "soil") {//soil
        return (snsType == 3 || snsType == 33);
    }
    if (type == "leak") {//leak
        return (snsType == 70);
    }
    if (type == "human") {//human
        return (snsType == 21);
    }
    if (type == "distance") {//binary
        return (snsType == 7 );
    }
    if (type == "all" || type == "any") {//all
        return true;
    }

    return false;
}

int16_t Devices_Sensors::findMyDeviceIndex() {
    //returns -1 if I am not found and could not be registered (that's a problem), or the index to devices for me
    int16_t index = findDevice(ESP.getEfuseMac());
    if (index == -1) {
        SerialPrint("I am not registered as a device, registering...",true);
        if (Prefs.DEVICENAME[0] == 0) {
            snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "Temporary Name");
        }
        index = addDevice(ESP.getEfuseMac(), WiFi.localIP(), Prefs.DEVICENAME, 0, 0, _MYTYPE);
    }
     
    return index;
}

#ifdef _ISPERIPHERAL

uint32_t Devices_Sensors::makeSensorID(uint8_t snsType, uint8_t snsID, int16_t devID) {
    if (devID == -1) devID = findMyDeviceIndex();
    if (devID == -1) return 0;

    if (snsID == -1) snsID = countSensors(snsType, devID)+1;

    
    return (devID<<16) + (snsType<<8) + snsID;
}

int16_t Devices_Sensors::getPrefsIndex(uint8_t snsType, uint8_t snsID, int16_t devID) {
    //find the index to the Prefs values for the given sensor type, sensor ID, and device index
    //this always references me as the device, unless devID is provided and is not -1
    //returns -2 if I am not registered,  -1 if no Prefsindex found, otherwise the index to Prefs values 

    if (devID == -1) devID = findMyDeviceIndex();

    if (devID == -1) return -2;
    

    byte sensorHistoryIndex=0; //index of the sensor in the SensorHistory array
    uint32_t sensorID = makeSensorID(snsType, snsID, devID);
    for (sensorHistoryIndex=0; sensorHistoryIndex<_SENSORNUM; sensorHistoryIndex++) {
    
      if (SensorHistory.PrefsSensorIDs[sensorHistoryIndex] == sensorID) {
        return sensorHistoryIndex;
      }  
    }
  
    return -1;

}
#endif

bool Devices_Sensors::isMySensor(int16_t index) {
    if (isSensorIndexInvalid(index)!=0) return false;
    return sensors[index].deviceIndex == findMyDeviceIndex();
}

bool Devices_Sensors::updateDeviceName(int16_t index, String newDeviceName) {
    if (isDeviceIndexValid(index)<=0) return false;
    strncpy(devices[index].devName, newDeviceName.c_str(), sizeof(devices[index].devName));
    devices[index].devName[sizeof(devices[index].devName) - 1] = '\0';
    return true;
}
