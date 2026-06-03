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

bool Devices_Sensors::cycleSensors(int16_t& currentPosition, uint8_t origin) {
    //when first calling this function, currentPosition should be -1
    bool cycling = cycleIndex(currentPosition,NUMSENSORS,origin);
    while (isSensorInit(currentPosition)==false && cycling==true) {
        cycling = cycleIndex(currentPosition,NUMSENSORS,origin);
    }
    return cycling;
}

// Device management functions
int16_t Devices_Sensors::addDevice(uint64_t MAC, IPAddress IP, const char* devName, uint32_t sendingInt, uint8_t flags, uint8_t devType) {
    // Check if device already exists

    if (MAC == 0) return -1;
    int16_t existingIndex = findDevice(MAC);
    if (existingIndex >= 0) {
        //check if existing device has all the same parameters as the new device
        ArborysDevType* device = &devices[existingIndex];
        device->dataReceived = I.currentTime;

        if (device->IP == IP && strcmp(device->devName, devName) == 0 && device->Flags == flags && device->SendingInt == sendingInt && device->devType == devType) {
            //device already exists with all the same parameters (other than the time received), so return the existing index
            //do nothing
        }        else {
            // Update existing device
            device->IP = IP;
            //do not change time logged

            if (devName[0] != '\0') {
                strncpy(device->devName, devName, sizeof(device->devName));
                device->devName[sizeof(device->devName) - 1] = '\0';
            }
            if (bitRead(flags,7))  bitWrite(device->Flags, 7, 1); //there is a sensor that is critical, so the device is critical
            if (bitRead(flags,2)) bitWrite(device->Flags, 2, 1); //there is a low power sensor, so the device is low power
            if (bitRead(flags,1)) bitWrite(device->Flags, 1, 1); //there is a sensor that is monitored, so the device is monitored
            //do not worry about alarm status here, because we will intermittently check for alarms

            device->expired = false;
            if (sendingInt != 0 && (sendingInt < device->SendingInt || device->SendingInt == 0)) device->SendingInt = sendingInt; //sending interval is the shortest of all attached sensors. Therefore, I should get a message at least this often!        
            device->IsSet = 1;
            if (devType != 0) device->devType = devType;

            #ifdef _USESDCARD
            //if this device is me and I updated, then I need to save to SD card 
            if (existingIndex == findMyDeviceIndex()) {
                storeDevicesSensorsSD();
            }
            #endif
        }
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
            if (sendingInt == 0)         devices[i].SendingInt = 86400; //86400 seconds = 24 hours
            else devices[i].SendingInt = sendingInt;
            devices[i].expired = false;
            devices[i].devType = devType;
            
            numDevices++;
            
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

ArborysDevType* Devices_Sensors::getDeviceBySnsIndex(int16_t snsindex) {
    if (snsindex >= 0 && snsindex < NUMSENSORS  && sensors[snsindex].IsSet) {
        return getDeviceByDevIndex(sensors[snsindex].deviceIndex);
    }
    return nullptr;
}

ArborysDevType* Devices_Sensors::getDeviceByDevIndex(int16_t devindex) {
    if (devindex >= 0 && devindex < NUMDEVICES  && devices[devindex].IsSet) {
        return &devices[devindex];
    }
    return nullptr;
}

ArborysDevType* Devices_Sensors::devIndexToPointer(int16_t devindex) {
    //wrapper for backwards compatibility
    return getDeviceByDevIndex(devindex);
}

ArborysDevType* Devices_Sensors::getDeviceByMAC(uint64_t MAC) {
    int16_t devindex = findDevice(MAC);
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

IPAddress Devices_Sensors::getMyDeviceIP() {
    return getDeviceIPByDevIndex(-99);
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

    //special case, if devindex is -99, then return the IP address of the device that is the current device
    if (devindex == -99) {
        int16_t mydevindex = findMyDeviceIndex();
        if (mydevindex < 0) return IPAddress(0,0,0,0); //this is an error!
        //check if the registered IP address is the same as my wifi ip address
        IPAddress myIP = getDeviceIPByDevIndex(mydevindex);
        if (myIP == WiFi.localIP()) {
            return myIP;
        } else {
            //update the registered IP address to the current wifi ip address
            devices[mydevindex].IP = WiFi.localIP();
            #ifdef _USESDCARD
            storeDevicesSensorsSD();
            #endif
            return WiFi.localIP();
        }
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

uint8_t Devices_Sensors::countSensors(int16_t snsType,int16_t devIndex) {
    //returns the number of sensors of the given type
    uint8_t count = 0;
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && (sensors[i].snsType == snsType || snsType == -1)) {
            if (devIndex == -1 || sensors[i].deviceIndex == devIndex) count++;
        }
    }
    return count;
}

uint8_t Devices_Sensors::countServers() {
    //returns the number of sensors of the given type
    uint8_t count = 0;
    for (int16_t i = 0; i < NUMDEVICES ; i++) {
        if (devices[i].IsSet && devices[i].devType >= 100) {
            count++;
        }
    }
    return count;
}

int16_t Devices_Sensors::nextServerIndex(int16_t startIndex, bool weatherServersOnly) {
    //returns the index of the next server
    if (startIndex < 0) startIndex = 0;
    for (int16_t i = startIndex; i < NUMDEVICES ; i++) {
        if (devices[i].IsSet && devices[i].devType >= 100) {
            if (weatherServersOnly == true && devices[i].devType != 100) continue;
            return i;
        }
    }
    return -1;
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
    

    uint8_t deviceFlags = 0;
    if (bitRead(flags,2) == 1) bitWrite(deviceFlags, 2, 1); //if sensor is low power, then device is low power
    if (bitRead(flags,0) == 1 && bitRead(flags,1) == 1) {
        bitWrite(deviceFlags, 0, 1);//if sensor is flagged and monitored, then device is flagged
        bitWrite(deviceFlags, 1, 1);
    }
    if (bitRead(flags,7) == 1) bitWrite(deviceFlags, 7, 1); //if sensor is critical and monitored, then device is critical

    // Find or create device
    int16_t deviceIndex = addDevice(deviceMAC, deviceIP, devName, sendingInt, deviceFlags, devType);
    if (deviceIndex < 0) {
        storeError("Addsensor: Could not create device", ERROR_DEVICE_ADD);
        return -2; // Could not create device
    }
    
    // Check if sensor already exists
    int16_t existingIndex = findSensor(deviceMAC, snsType, snsID);
    if (existingIndex >= 0) {
        // Update existing sensor. note that timeWritten is not updated here.
        ArborysSnsType* sensor = &sensors[existingIndex];
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
        sensor->IsSet=true;
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
            #else
            sensors[i].OverrideFlags = 0;
            #endif            
            numSensors++;
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
ArborysSnsType* Devices_Sensors::getSensorBySnsIndex(int16_t snsIndex) {
    //wrapper for backwards compatibility
    return snsIndexToPointer(snsIndex);
}

ArborysSnsType* Devices_Sensors::snsIndexToPointer(int16_t snsIndex) {
    if (snsIndex < 0 || snsIndex >= NUMSENSORS || sensors[snsIndex].IsSet == false) {
        return nullptr;
    }

    return &sensors[snsIndex];
}


int16_t Devices_Sensors::findMySensorBySnsTypeAndID(int16_t snsType, int16_t snsID) {
    //Special case: assume this is my sensor, I am the device.
    int16_t devID = findMyDeviceIndex();
    if (devID == -1) return -1;
    return findSensor(devID, snsType, snsID);

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



int16_t Devices_Sensors::findSensorByPointer(ArborysSnsType* P) {
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && &sensors[i] == P) {
            return i;
        }
    }
    return -1;
}

bool Devices_Sensors::isOutsideSensor(int16_t index) {
    if (index >= 0 && index < NUMSENSORS && sensors[index].IsSet) {
        return bitRead(sensors[index].Flags,4);
    }
    return false;
}


bool Devices_Sensors::hasOutsideSensors(String parameter) {
    //return true if there are any outside sensors of the given type, note that type can be "all"
    return findOutsideSensorByType(parameter) >= 0;
}


int16_t Devices_Sensors::findOutsideSensorByType(String parameter) {
    //return >=0 if there are any outside sensors of the given type, note that type can be "all" (value is the first index found)
    //return -1 if no outside sensors found
    
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (isOutsideSensor(i) == false) continue;
        else {
            if (parameter == "all") return i;
            else if (isSensorOfType(i,parameter) == true) return i;
        }
    }
    return -1;
}


uint8_t Devices_Sensors::returnBatteryPercentage(ArborysSnsType* P) {

    if (P->snsType == 60 || P->snsType == 62) {
      return returnLiBatteryPercentage(P->snsValue);
    } else {
        if (P->snsType == 61 || P->snsType == 63) {
            return returnPbBatteryPercentage(P->snsValue);
        }
    }
  
    return 255;
  }
  

double Devices_Sensors::getAverageOutsideParameterValue(String parameter, uint32_t MoreRecentThan) {
    //outside sensors are those with bit 4 set in the flags
    //return -127 if no monitored outside sensors of that type, or return the average value if there are any. This its in an int8_t range.
    //parameter can be "temperature", "humidity", or "pressure", etc
    //optionally specify the recency of the last update, in unixtime seconds.

//RMB0 = Flagged, RMB1 = Monitored, RMB2=LowPower, RMB3-derived/calculated  value, RMB4 =  Outside sensor, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is critical and monitored - alert if it expires after time limit specified)


    //average the parameter value for sensors that are outside, if the sensor is both outside and monitored.
    double totalvalue = 0;
    int16_t count = 0;
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (!sensors[i].IsSet) continue;

        if (bitRead(sensors[i].Flags,4) == 0) continue; //not an outside sensor
        if (bitRead(sensors[i].Flags,1) == 0) continue; //not monitored
        if (isSensorOfType(i,parameter) == false) continue; //not the type I want
        
        // Check time filter
        if (MoreRecentThan > 0 && sensors[i].timeLogged < MoreRecentThan) continue;        

        //add the value to the total
        totalvalue += sensors[i].snsValue;
        count++;
    }

    if (count == 0) return -127;
    return totalvalue / count;

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

int8_t Devices_Sensors::isSensorFlagged(int16_t snsIndex, uint16_t optionalsnsflags, uint16_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan, bool countCriticalExpired, bool countAnyExpired, uint8_t snsType, bool useOverrideFlags) { 
//checks if a sensor is flagged based on the criteria used by countFlagged
//order of operations:
//0. check if the sensor has OverrideFlags set, and if so, use the OverrideFlags instead of the sensor's Flags
//1. check if the sensor is set
//2. check if the sensor is of the specified type
//3. check if the sensor is expired (either critical or any, depending on the criteria)
//4. check if the sensor is within the time range
//5. check if the sensor is within the flag settings
//here snsIndex is the index of the sensor to check, and snsType is an explicit declaration of the sesnor type if optionalsnsflags is set to 14
//optionalsnsflags is a bitmask of the sensor types to count: 0 = all , 1 = temp, 2 = humidity, 3 = soil, 4 = pressure, 5 = HVAC, 6 = server, 7 = dist, 8 = binary, 9 = leak, 10 = battery, 11 = human, ..., 14 = specified sensor type, 15 = EXCLUDE THE INDICATED SENSOR TYPES (note that you cannot have both bit 0 and exclude... ALL or NONE!)

byte overrideFlags = 0;
#ifdef _ISPERIPHERAL
overrideFlags = 0; 
useOverrideFlags = false;
#else
overrideFlags = sensors[snsIndex].OverrideFlags;
#endif

    if (!sensors[snsIndex].IsSet) return -100; //no sensor

    bool isgood=true;

    if (isBit(optionalsnsflags, 14) == 1) { //using a specific sensory type, note that exclude still allowed
        if (isBit(optionalsnsflags, 15) == 0) {
            if (sensors[snsIndex].snsType != snsType) return 0; //not the specified sensor
        } else {
            if (sensors[snsIndex].snsType == snsType) return 0; //not the specified sensor
        }
        isgood = true;

    }
    else if (isBit(optionalsnsflags, 0) == 1 || isBit(optionalsnsflags, 13) == 1) { // all sensor types or "everything else"
        //do nothing, it's already good
        isgood = true;
    }
    else {
        if (isBit(optionalsnsflags, 15) == 0) { //do not exclude all sensor types
            isgood = false;

            for (byte optionalsnsflagindex = 0; optionalsnsflagindex<11; optionalsnsflagindex++) {
                if (isBit(optionalsnsflags, optionalsnsflagindex) ) {
                    isgood = isSensorOfType(snsIndex, getSensorTypeFlaggedString(optionalsnsflagindex));
                    if (isgood) break; //regardless of how many types, if we hit even one then we are good
                }
            }
        }
        else {
            isgood = true;
            for (byte optionalsnsflagindex = 0; optionalsnsflagindex<11; optionalsnsflagindex++) {
                if (isBit(optionalsnsflags, optionalsnsflagindex) ) {
                    isgood = !isSensorOfType(snsIndex, getSensorTypeFlaggedString(optionalsnsflagindex));
                    if (isgood==false) break; //regardless of how many types, if we fail anywhere we are done
                }
            }
        }
    }

    if (isgood == false) return 0; //sensor not matching the criteria

    //now check the flags and expiration rules
    //is this expired?
    if ((countCriticalExpired && bitRead(sensors[snsIndex].Flags,7) && sensors[snsIndex].expired)) {
        if (useOverrideFlags && bitRead(overrideFlags,7) == 1) return -2; //critical expired but override flags set to not critical
        else return 2; //critical expired and override flags set to critical
    }

    //for regular expired, the sensor must be monitored 
    if (countAnyExpired && sensors[snsIndex].expired && bitRead(sensors[snsIndex].Flags,1) == 1) {
        if (useOverrideFlags && bitRead(overrideFlags,1) == 1) return -3; //any expired but override flags set to not monitored
        else return 3; //any expired and override flags set to monitored
    }

    // Check time filter
    if (MoreRecentThan > 0 && sensors[snsIndex].timeRead < MoreRecentThan) return -4; //not recent enough

    if ((sensors[snsIndex].Flags & flagsthatmatter) == flagsettings) {
        if (useOverrideFlags && (overrideFlags & flagsthatmatter) == flagsettings) return -1; //flagged but override flags set to not flagged
        else return 1; //flagged and override flags set to flagged
    }


    return -5; //found the sensor, but it is not flagged according to the specified criteria

}

int8_t Devices_Sensors::countFlagged(int16_t snsType, uint16_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan, bool countCriticalExpired, bool countAnyExpired, uint16_t optionalsnsflags) { 
     //RMB0 = Flagged, RMB1 = Monitored, RMB2=LowPower, RMB3-derived/calculated  value, RMB4 =  Outside sensor, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is critical and monitored - alert if it expires after time limit specified)
     //if snstype is 1-200 then count all sensors of that type (meeting the flagsthatmatter criteria)
    //if snsType is 0, then count all sensors (meeting the flagsthatmatter criteria)
    //if snsType is -1, then count all temperature sensors (meeting the flag criteria)
    //if snsType is -2, then count all humidity sensors (meeting the flag criteria)
    //if snsType is -3, then count all soil sensors (meeting the flag criteria)
    //if snsType is -4, then count all pressure sensors (meeting the flag criteria)
    //if snsType is -5, then count all HVAC sensors (meeting the flag criteria)
    //if snsType is -6, then count all server sensors (meeting the flag criteria)
    //if snsType is -7, then count all dist sensors (meeting the flag criteria)
    //if snsType is -8, then count all binary sensors (meeting the flag criteria)
    //if snsType is -9, then count all leak sensors (meeting the flag criteria)
    //if snsType is -10, then count all battery sensors (meeting the flag criteria)
    //if snsType is -11, then count all human sensors (meeting the flag criteria)

    //if snsType is =-1000 then use then use optionalsnsflags to determine sensor types to count (along with flagsthatmatter and flagsettings)
    //if snsType is <-1000 then count all sensors EXCEPT the specified types in optionalsnsflags (meeting the flagsthatmatter criteria)
    //optionalsnsflags is a bitmask of the sensor types to count: 
    //0 = all , 1 = temp, 2 = humidity, 3 = soil, 4 = pressure, 5 = HVAC, 6 = server, 7 = dist, 8 = binary, 9 = leak, 10 = battery, 11 = human, 
    //12 = TBD, 13 = TBD, 
    //14 = use specified sensor type
    //15 = EXCLUDE THE INDICATED SENSOR TYPES (note that you cannot have both bit 0 and exclude... ALL or NONE!)
    
    uint16_t count = 0;

    //special case for all sensors, which means no filtering by type
    if (snsType == 0) {
        setBit(optionalsnsflags, 0); //all sensor types
        clearBit(optionalsnsflags, 15); //do not exclude all sensor types
    }
    else if (snsType > 0) {
        setBit(optionalsnsflags, 14); //using a specific sensory type, not that exclude still allowed        
    }
    else if (snsType < -1000) {
        setBit(optionalsnsflags, 15); //exclude all sensor types
    }
    else if (snsType == -1000) {
        clearBit(optionalsnsflags, 15); //do not exclude all sensor types
    }
    else if (snsType <0 && snsType >= -11) {
        //convert to explicit sensor group
        optionalsnsflags = 0;
        if (snsType == -1) setBit(optionalsnsflags, 1); //temperature
        else if (snsType == -2) setBit(optionalsnsflags, 2); //humidity
        else if (snsType == -3) setBit(optionalsnsflags, 3); //soil
        else if (snsType == -4) setBit(optionalsnsflags, 4); //pressure
        else if (snsType == -5) setBit(optionalsnsflags, 5); //HVAC
        else if (snsType == -6) setBit(optionalsnsflags, 6); //server
        else if (snsType == -7) setBit(optionalsnsflags, 7); //dist
        else if (snsType == -8) setBit(optionalsnsflags, 8); //binary
        else if (snsType == -9) setBit(optionalsnsflags, 9); //leak
        else if (snsType == -10) setBit(optionalsnsflags, 10); //battery
        else if (snsType == -11) setBit(optionalsnsflags, 11); //human
    }


    //count the number of sensors that match the flags
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (isSensorFlagged(i, optionalsnsflags,flagsthatmatter, flagsettings, MoreRecentThan, countCriticalExpired, countAnyExpired, snsType) <=0) continue;
        count++;
    }

    return count;
}

String Devices_Sensors::getSensorTypeFlaggedString(byte snstypeindex) {
    //returns the string name of the sensor types that are used by countFlagged and isFlagged
    
    if (snstypeindex == 0) return "all";
    else if (snstypeindex == 1) return "temperature";
    else if (snstypeindex == 2) return "humidity";
    else if (snstypeindex == 3) return "soil";
    else if (snstypeindex == 4) return "pressure";
    else if (snstypeindex == 5) return "HVAC";
    else if (snstypeindex == 6) return "server";
    else if (snstypeindex == 7) return "dist";
    else if (snstypeindex == 8) return "binary";
    else if (snstypeindex == 9) return "leak";
    else if (snstypeindex == 10) return "battery";
    else if (snstypeindex == 11) return "human";    


    return "";
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
    //find a sensor based on text type name
    //if snstype is "temperature", then find a temperature sensor
    //if snstype is "humidity", then find a humidity sensor
    //if snstype is "soil", then find a soil sensor
    //if snstype is "pressure", then find a pressure sensor
    //if snstype is "HVAC", then find a HVAC sensor
    //if snstype is "server", then find a server sensor
    //if snstype is "dist", then find a distance sensor
    //if snstype is "binary", then find a binary sensor
    //if snstype is "battery", then find a battery sensor
    //if snstype is "battery_li", then find a weather sensor

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
            //I am not registered as a device, so try to register me
            // But only if WiFi is connected and we have a valid IP
            // This prevents registering with IP 0.0.0.0 which can cause devices to be lost

            if (CheckWifiStatus(false)!=1) {
                // WiFi not connected or no IP - can't register safely
                // Return -2 to indicate fatal error (I am not registered and could not be registered)
                SerialPrint("isDeviceIndexValid: Cannot register device - WiFi not connected or no IP address", true);
                return -2;
            }

            IPAddress myIP = WiFi.localIP();
            ismine = addDevice(ESP.getEfuseMac(), myIP, Prefs.DEVICENAME, 0, 0, _MYTYPE);
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


uint16_t Devices_Sensors::isSensorIndexInvalid(int16_t index, bool checkExpired) {
    //returns 1 if invalid (out of bounds), 2 if not set, 3 if expired, 0 if valid
    if (index < 0 || index >= NUMSENSORS ) {
        return 1;
    }
    if (!sensors[index].IsSet) {
        return 2;
    }
    if (checkExpired && sensors[index].expired) {
        return 3;
    }
    return 0;
}

ArborysDevType* Devices_Sensors::getNextExpiredDevice(int16_t& startIndex) {
    //takes a start index and finds the NEXT expired device. usage: Start at -1.
    //if it reaches the end of the list, no more expired devices
    startIndex++;
    if (startIndex < 0) startIndex = 0;
    for (int16_t i = startIndex; i < NUMDEVICES; i++) {
        ArborysDevType* device = &devices[i];
        if (!device->IsSet) continue;
        if (device->expired) {
            return device;
        }
    }
    return NULL;
}



void Devices_Sensors::checkDeviceFlags() {
    //check all devices and sensors and update the flags accordingly
    for (int16_t i = 0; i < NUMDEVICES; i++) {
        if (!devices[i].IsSet) continue;
        devices[i].Flags = 0;
        
        for (int16_t j = 0; j < NUMSENSORS; j++) {
            if (!sensors[j].IsSet) continue;
            if (sensors[j].deviceIndex != i) continue;
            if (bitRead(sensors[j].Flags,7)) bitWrite(devices[i].Flags, 7, 1); //there is a sensor that is critical, so the device is critical
            if (bitRead(sensors[j].Flags,1)) bitWrite(devices[i].Flags, 1, 1); //there is a sensor that is monitored, so the device is monitored
            if (bitRead(sensors[j].Flags,2)) bitWrite(devices[i].Flags, 2, 1); //there is a low power sensor, so the device is low power
            if (bitRead(sensors[j].Flags,0) && (bitRead(sensors[j].Flags,1) || bitRead(sensors[j].Flags,7))) bitWrite(devices[i].Flags, 0, 1); //there is a sensor that is flagged and matters, so the device is flagged            
        }
    }
}

//there are two ways to check expiration of device:
//1. check if any sensors are expired, and if the last read was multiplier x the sending interval ago then also label the device expired. This ensures that a device is expired even if only one sensor is missing (otherwise, other sensors could mask the expiration of one missing sensor)
//1a. call checkExpirationAllSensors(currentTime, onlyCritical, multiplier, expireDevice)
//1b. then call any device expiration checker function, such as checkExpirationDevice(index, currentTime, onlyCritical, multiplier)
//2. check the devie directly, assuming that sendingInt has been registered as the shortest sending interval of all attached sensors. If the last read was multiplier x the sending interval ago then label the device expired. This is simpler and faster, but may miss a sensor if others are present.
//2a. call checkExpirationDevice(index, currentTime, onlyCritical, multiplier)
int16_t Devices_Sensors::checkExpirationDevice(int16_t index, time_t currentTime, bool onlyCritical, uint8_t multiplier) {

    ArborysDevType* device = &devices[index];
    if (!device->IsSet) return 0;

    if (currentTime == 0) currentTime = I.currentTime;


    if (device->dataReceived + device->SendingInt * multiplier < currentTime) {
        device->expired = true;
        return 1;
    }

    return 0;    
}

byte Devices_Sensors::checkExpirationAllSensors(time_t currentTime, bool onlyCritical, uint8_t multiplier, bool expireDevice) {
    byte count = 0;
    for (int16_t i = 0; i < NUMSENSORS; i++) {
        if (checkExpirationSensor(i, currentTime, onlyCritical, 3, expireDevice) == 1)    count++; //check if any sensors are expired
        
    }
    return count;
}

int16_t Devices_Sensors::checkExpirationSensor(int16_t index, time_t currentTime, bool onlyCritical, uint8_t multiplier, bool expireDevice) {
    //returns -1 if invalid (out of bounds), -2 if not set, -3 if expired,-5 if this is not a critical sensor and onlyCritical is true, -10 if indeterminate (no sending interval set), 0 if valid not expired, 1 if expired
    
    int16_t result = isSensorIndexInvalid(index);
    if (result != 0) return -1*result;
    if (onlyCritical && !bitRead(sensors[index].Flags, 7)) return -5;
    
    if (multiplier == 0) multiplier = 3;
    uint16_t sendint = sensors[index].SendingInt;
    if (sendint==0) return -10;
    uint32_t expirationTime = sensors[index].timeLogged + sendint * multiplier; // 2x sending interval buffer allows for missing one reading

    if (currentTime > expirationTime) {
        if (expireDevice) devices[sensors[index].deviceIndex].expired = true;
        sensors[index].expired = true;
        return 1;
    }

    return 0;
}

uint8_t Devices_Sensors::getSensorFlag(int16_t index) {
    return sensors[index].Flags;
}

String Devices_Sensors::sensorIsOfType(int16_t index) {
    if (index < 0 || index >= NUMSENSORS ) return "Invalid index";
    return sensorIsOfType(sensors[index].snsType);
}

String Devices_Sensors::sensorIsOfType(ArborysSnsType* sensor) {
    if (sensor == NULL) return "Invalid sensor";
    return sensorIsOfType(sensor->snsType);
}

String Devices_Sensors::sensorIsOfType(uint8_t snsType) {
    if (snsType == 1 || snsType == 4 || snsType == 10 || snsType == 14 || snsType == 17) return "temperature";
    if (snsType == 2 || snsType == 5 || snsType == 15 || snsType == 18) return "humidity";
    if (snsType == 9 || snsType == 13 || snsType == 19) return "pressure";
    if (snsType == 60 || snsType == 61 || snsType == 62 || snsType == 63) return "battery";
    if (snsType == 60 || snsType == 62) return "battery_li";
    if (snsType == 61 || snsType == 63) return "battery_pb";
    if (snsType >= 50 && snsType < 60) return "HVAC";
    if (snsType == 3 || snsType == 33) return "soil";
    if (snsType == 70) return "leak";
    if (snsType == 8) return "human";
    if (snsType == 7) return "distance";
    if (snsType == 12 ) return "weather";
    if (snsType == 11 || snsType == 16) return "altitude";
    if (snsType == 98) return "clock";
    if (snsType == 71) return "binary";
    if (snsType >= 100) return "server";
    return "unknown";
}

bool Devices_Sensors::isSensorOfType(int16_t index, String type) {
    return isSensorOfType(sensors[index].snsType, type);
}

bool Devices_Sensors::isSensorOfType(ArborysSnsType* sensor, String type) {
    if (sensor == NULL) return false;
    return isSensorOfType(sensor->snsType, type);
}

bool Devices_Sensors::isSensorOfType(uint8_t snsType, String type) {

    if (type == "temperature") {//temperature
        return (snsType == 1 || snsType == 4 || snsType == 6 || snsType == 10 || snsType == 14 || snsType == 17);
    }
    if (type == "humidity") {//humidity
        return (snsType == 2 || snsType == 5 || snsType == 15 || snsType == 18);
    }
    if (type == "pressure") {//pressure
        return (snsType == 9 || snsType == 13 || snsType == 19);
    }
    if (type == "battery") {//battery
        return (snsType >= 60 && snsType <= 65);
    }
    if (type == "battery_li") {//battery_li
        return (snsType == 60 || snsType == 62);
    }
    if (type == "battery_pb") {//battery_pb
        return (snsType == 61 || snsType == 63);
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
        return (snsType == 8);
    }
    if (type == "distance") {//binary
        return (snsType == 7 );
    }
    if (type == "weather") {//weather
        return (snsType == 12);
    }
    if (type == "altitude") {//altitude
        return (snsType == 11 || snsType == 16);
    }
    if (type == "clock") {//clock
        return (snsType == 98);
    }
    if (type == "binary") {//binary
        return (snsType == 71);
    }
    
    if (type == "all" || type == "any") {//all
        return true;
    }

    return false;
}

int16_t Devices_Sensors::findMyDeviceIndex() {
    //returns -1 if I am not found and could not be registered (that's a problem), or the index to devices for me
    int16_t index = findDevice((uint64_t) ESP.getEfuseMac());
    if (index == -1) {
        SerialPrint("I am not registered as a device, registering...",true);
        if (Prefs.DEVICENAME[0] == 0) {
            snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "Temporary Name");
        }
        
        // Only register if WiFi is connected and we have a valid IP
        // This prevents registering with IP 0.0.0.0 which can cause devices to be lost
        if (CheckWifiStatus(false)!=1) {
            SerialPrint("Cannot register device: WiFi not connected or no IP address", true);
            return -1; // Don't register with invalid IP
        }
        
        IPAddress myIP = WiFi.localIP();
        index = addDevice(ESP.getEfuseMac(), myIP, Prefs.DEVICENAME, 0, 0, _MYTYPE);
        if (index >= 0) {
            SerialPrint("Device registered successfully with IP: " + myIP.toString(), true);
        }
    }
     
    return index;
}

uint32_t Devices_Sensors::makeSensorID(uint8_t snsType, uint8_t snsID, int16_t devID) {
    //an ID that combines the device index, sensor type, and sensor ID
    if (devID == -1) devID = findMyDeviceIndex();
    if (devID == -1) return 0;

    if (snsID == -1) snsID = countSensors(snsType, devID)+1;

    
    return (devID<<16) + (snsType<<8) + snsID;
}

uint32_t Devices_Sensors::makeSensorID(ArborysSnsType* sensor) {
    if (sensor == NULL) return 0;
    if (sensor->IsSet == false) return 0;
    return makeSensorID(sensor->snsType, sensor->snsID, sensor->deviceIndex);
}

uint32_t Devices_Sensors::makeSensorID(int16_t index) {
    if (index < 0 || index >= NUMSENSORS ) return 0;
    if (sensors[index].IsSet == false) return 0;
    return makeSensorID(&sensors[index]);
}

int16_t Devices_Sensors::findSensorBySensorID(uint32_t sensorID) {
    for (int16_t i = 0; i < NUMSENSORS ; i++) {
        if (sensors[i].IsSet && makeSensorID(sensors[i].snsType, sensors[i].snsID,sensors[i].deviceIndex) == sensorID) {
            return i;
        }
    }
    return -1;
}

int16_t Devices_Sensors::findSensorBySensorID(ArborysSnsType* sensor) {
    if (sensor == NULL) return -1;
    if (sensor->IsSet == false) return -1;
    return findSensorBySensorID(makeSensorID(sensor->snsType, sensor->snsID, sensor->deviceIndex));
}

bool Devices_Sensors::isMySensor(int16_t index) {
    if (index < 0 || index >= NUMSENSORS ) return false;
    if (sensors[index].IsSet == false) return false;
    if (isSensorIndexInvalid(index)!=0) return false;
    return  sensors[index].deviceIndex ==  findMyDeviceIndex();
}

bool Devices_Sensors::updateDeviceName(int16_t index, String newDeviceName) {
    if (isDeviceIndexValid(index)<=0) return false;
    strncpy(devices[index].devName, newDeviceName.c_str(), sizeof(devices[index].devName));
    devices[index].devName[sizeof(devices[index].devName) - 1] = '\0';
    return true;
}
