#include "Devices.hpp"
#include "utility.hpp"
#include <TimeLib.h>

// Global instance
Devices_Sensors Sensors;

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

// Device management functions
int16_t Devices_Sensors::addDevice(uint64_t MAC, uint32_t IP, const char* devName, uint32_t sendingInt, uint8_t flags) {
    // Check if device already exists
    int16_t existingIndex = findDevice(MAC);
    if (existingIndex >= 0) {
        // Update existing device
        DevType* device = &devices[existingIndex];
        device->IP = IP;
        device->timeLogged = now();
        device->timeRead = I.currentTime;
        if (devName) {
            strncpy(device->devName, devName, sizeof(device->devName) - 1);
            device->devName[sizeof(device->devName) - 1] = '\0';
        }
        device->Flags = flags;
        device->SendingInt = sendingInt;
        device->expired = false;
        device->IsSet = 1;
        return existingIndex;
    }
    
    // Find empty slot
    for (int16_t i = 0; i < NUMDEVICES && i < numDevices + 10; i++) {
        if (!devices[i].IsSet) {
            devices[i].MAC = MAC;
            devices[i].IP = IP;
            devices[i].timeLogged = now();
            devices[i].timeRead = I.currentTime;
            devices[i].IsSet = 1;
            if (devName) {
                strncpy(devices[i].devName, devName, sizeof(devices[i].devName) - 1);
                devices[i].devName[sizeof(devices[i].devName) - 1] = '\0';
            } else {
                devices[i].devName[0] = '\0';
            }
            devices[i].Flags = flags;
            devices[i].SendingInt = sendingInt;
            devices[i].expired = false;
            
            if (i >= numDevices) {
                numDevices = i + 1;
            }
            return i;
        }
    }
    
    return -1; // No space available
}

int16_t Devices_Sensors::findDevice(uint64_t MAC) {
    for (int16_t i = 0; i < NUMDEVICES && i < numDevices; i++) {
        if (devices[i].IsSet && devices[i].MAC == MAC) {
            return i;
        }
    }
    return -1;
}

int16_t Devices_Sensors::findDevice(uint32_t IP) {
    for (int16_t i = 0; i < NUMDEVICES && i < numDevices; i++) {
        if (devices[i].IsSet && devices[i].IP == IP) {
            return i;
        }
    }
    return -1;
}

DevType* Devices_Sensors::getDeviceBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS && snsindex < numSensors && sensors[snsindex].IsSet) {
        return getDeviceByDevIndex(sensors[snsindex].deviceIndex);
    }
    return nullptr;
}

DevType* Devices_Sensors::getDeviceByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES && devindex < numDevices && devices[devindex].IsSet) {
        return &devices[devindex];
    }
    return nullptr;
}

uint64_t Devices_Sensors::getDeviceMACBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS && snsindex < numSensors && sensors[snsindex].IsSet) {
        return getDeviceMACByDevIndex(sensors[snsindex].deviceIndex);
    }
    return 0;
}

uint64_t Devices_Sensors::getDeviceMACByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES && devindex < numDevices && devices[devindex].IsSet) {
        return devices[devindex].MAC;
    }
    return 0;
}

uint32_t Devices_Sensors::getDeviceIPBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS && snsindex < numSensors && sensors[snsindex].IsSet) {
        return getDeviceIPByDevIndex(sensors[snsindex].deviceIndex);
    }
    return 0;
}

uint32_t Devices_Sensors::getDeviceIPByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES && devindex < numDevices && devices[devindex].IsSet) {
        return devices[devindex].IP;
    }
    return 0;
}

uint8_t Devices_Sensors::getNumDevices() {
    return numDevices;
}

uint8_t Devices_Sensors::countDev() {
    return numSensors; // Return number of sensors for legacy compatibility
}

bool Devices_Sensors::isDeviceInit(int16_t index) {
    return (index >= 0 && index < NUMDEVICES && index < numDevices && devices[index].IsSet);
}

void Devices_Sensors::initDevice(int16_t index) {
    if (index >= 0 && index < NUMDEVICES && index < numDevices) {
        devices[index].IsSet = 0;
        devices[index].expired = false;
    }
}

// Sensor management functions
int16_t Devices_Sensors::addSensor(uint64_t deviceMAC, uint32_t deviceIP, uint8_t snsType, uint8_t snsID, 
                                  const char* snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, 
                                  uint32_t sendingInt, uint8_t flags, const char* devName) {
    // Find or create device
    int16_t deviceIndex = addDevice(deviceMAC, deviceIP, devName);
    if (deviceIndex < 0) {
        storeError("Addsensor: Could not create device", ERROR_DEVICE_ADD);
        return -2; // Could not create device
    }
    
    // Check if sensor already exists
    int16_t existingIndex = findSensor(deviceMAC, snsType, snsID);
    if (existingIndex >= 0) {
        // Update existing sensor
        SnsType* sensor = &sensors[existingIndex];
        sensor->snsValue = snsValue;
        sensor->timeRead = timeRead;
        sensor->timeLogged = timeLogged;
        sensor->Flags = flags;
        sensor->SendingInt = sendingInt;
        sensor->expired = false;
        sensor->IsSet=1;
        return existingIndex;
    }
    
    // Find empty slot
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors + 20; i++) {
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
            sensors[i].Flags = flags;
            sensors[i].SendingInt = sendingInt;
            sensors[i].IsSet = 1;
            sensors[i].expired = false;
            
            if (i >= numSensors) {
                numSensors = i + 1;
            }
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
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (sensors[i].IsSet && sensors[i].deviceIndex == deviceIndex && 
            sensors[i].snsType == snsType && sensors[i].snsID == snsID) {
            return i;
        }
    }

    return -1;
}

int16_t Devices_Sensors::findSensor(uint32_t deviceIP, uint8_t snsType, uint8_t snsID) {
    int16_t deviceIndex = findDevice(deviceIP);
    if (deviceIndex < 0) {
        return -1;
    }
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (sensors[i].IsSet && sensors[i].deviceIndex == deviceIndex && 
            sensors[i].snsType == snsType && sensors[i].snsID == snsID) {
            return i;
        }
    }
    
    return -1;
}

SnsType* Devices_Sensors::getSensorBySnsIndex(int16_t index) {
    if (index >= 0 && index < NUMSENSORS && index < numSensors && sensors[index].IsSet) {
        return &sensors[index];
    }
    return nullptr;
}

uint8_t Devices_Sensors::getNumSensors() {
    return numSensors;
}

bool Devices_Sensors::isSensorInit(int16_t index) {
    return (index >= 0 && index < NUMSENSORS && index < numSensors && sensors[index].IsSet);
}

void Devices_Sensors::initSensor(int16_t index) {
    if (index >= 0 && index < NUMSENSORS && index < numSensors) {
        sensors[index].IsSet = 0;
        sensors[index].expired = false;
        sensors[index].deviceIndex = -1;
    }
}

// Utility functions
int16_t Devices_Sensors::findOldestDevice() {
    int16_t oldestIndex = -1;
    uint32_t oldestTime = 0xFFFFFFFF;
    
    for (int16_t i = 0; i < NUMDEVICES && i < numDevices; i++) {
        if (devices[i].IsSet && devices[i].timeLogged < oldestTime) {
            oldestTime = devices[i].timeLogged;
            oldestIndex = i;
        }
    }
    
    return oldestIndex;
}

int16_t Devices_Sensors::findOldestSensor() {
    int16_t oldestIndex = -1;
    uint32_t oldestTime = 0xFFFFFFFF;
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (sensors[i].IsSet && sensors[i].timeLogged < oldestTime) {
            oldestTime = sensors[i].timeLogged;
            oldestIndex = i;
        }
    }
    
    return oldestIndex;
}

byte Devices_Sensors::checkExpiration(int16_t index, time_t currentTime, bool onlyCritical) {
    if (index >= 0) {
        // Check specific device or sensor
        if (index < NUMDEVICES && index < numDevices) {
            if (devices[index].IsSet) {
                return checkExpirationDevice(index, currentTime, onlyCritical);
            }
        } else if (index < NUMSENSORS && index < numSensors) {
            if (sensors[index].IsSet) {
                return checkExpirationSensor(index, currentTime, onlyCritical);
            }
        }
        return 0;
    }
    
    // Check all devices and sensors
    byte expiredCount = 0;
    
    for (int16_t i = 0; i < NUMDEVICES && i < numDevices; i++) {
        if (devices[i].IsSet) {
            expiredCount += checkExpirationDevice(i, currentTime, onlyCritical);
        }
    }
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (sensors[i].IsSet) {
            expiredCount += checkExpirationSensor(i, currentTime, onlyCritical);
        }
    }
    
    return expiredCount;
}

uint8_t Devices_Sensors::countFlagged(int16_t snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan) {
    uint8_t count = 0;
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (!sensors[i].IsSet) continue;
        
        // Check sensor type filter
        if (snsType >= 0 && sensors[i].snsType != snsType) continue;
        if (snsType == -2 && (sensors[i].snsType < 1 || sensors[i].snsType > 9)) continue; // Temperature sensors only
        
        // Check time filter
        if (MoreRecentThan > 0 && sensors[i].timeLogged < MoreRecentThan) continue;
        
        // Check flags
        uint8_t sensorFlags = sensors[i].Flags & flagsthatmatter;
        if (sensorFlags == flagsettings) {
            count++;
        }
    }
    
    return count;
}

// Search functions
void Devices_Sensors::find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow) {
    *snsIndexHigh = 0;
    *snsIndexLow = 255;
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (!sensors[i].IsSet) continue;
        if (sensors[i].snsType != snsType) continue;
        if (snsname != sensors[i].snsName) continue;
        
        if (sensors[i].snsID > *snsIndexHigh) *snsIndexHigh = sensors[i].snsID;
        if (sensors[i].snsID < *snsIndexLow) *snsIndexLow = sensors[i].snsID;
    }
}

uint8_t Devices_Sensors::find_sensor_count(String snsname, uint8_t snsType) {
    uint8_t count = 0;
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
        if (!sensors[i].IsSet) continue;
        if (sensors[i].snsType != snsType) continue;
        if (snsname == sensors[i].snsName) {
            count++;
        }
    }
    
    return count;
}

uint8_t Devices_Sensors::findSensorByName(String snsname, uint8_t snsType, uint8_t snsID) {
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
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

int16_t Devices_Sensors::findSnsOfType(uint8_t snstype, bool newest) {
    int16_t targetIndex = -1;
    uint32_t targetTime = newest ? 0 : 0xFFFFFFFF;
    
    for (int16_t i = 0; i < NUMSENSORS && i < numSensors; i++) {
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

// Data storage functions
bool Devices_Sensors::storeAllSensors() {
    // This would implement storing all sensors to SD card
    // For now, return true as placeholder
    return true;
}

bool Devices_Sensors::readAllSensors() {
    // This would implement reading all sensors from SD card
    // For now, return true as placeholder
    return true;
}

// Helper functions for expiration checking
byte Devices_Sensors::checkExpirationDevice(int16_t index, time_t currentTime, bool onlyCritical) {
    if (index < 0 || index >= NUMDEVICES || index >= numDevices || !devices[index].IsSet) {
        return 0;
    }
    
    DevType* device = &devices[index];
    uint32_t expirationTime = device->timeLogged + device->SendingInt * 2; // 2x sending interval
    
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

byte Devices_Sensors::checkExpirationSensor(int16_t index, time_t currentTime, bool onlyCritical) {
    if (index < 0 || index >= NUMSENSORS || index >= numSensors || !sensors[index].IsSet) {
        return 0;
    }
    
    SnsType* sensor = &sensors[index];
    uint32_t expirationTime = sensor->timeLogged + sensor->SendingInt * 2; // 2x sending interval
    
    if (currentTime > expirationTime) {
        if (onlyCritical && !bitRead(sensor->Flags, 7)) {
            return 0; // Only check critical sensors
        }
        sensor->expired = true;
        return 1;
    }
    
    sensor->expired = false;
    return 0;
}

