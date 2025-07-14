#include <SDCard.hpp>
#include <globals.hpp>
#include <SD.h>
#include <utility.hpp>
#include "Devices.hpp"

union ScreenInfoBytes {
    struct Screen screendata;
    uint8_t bytes[sizeof(Screen)];

    ScreenInfoBytes() {};
};

// New functions for Devices_Sensors class
bool storeSensorsSD() {
    String filename = "/Data/DevicesSensors.dat";
    File f = SD.open(filename, FILE_WRITE);
    if (f==false) {
        storeError("storeSensorsSD: Could not write Devices_Sensors data");
        f.close();
        return false;
    }

    // Write the entire Devices_Sensors object
    f.write((uint8_t*)&Sensors, sizeof(Devices_Sensors));
    f.close();
    return true;
}

bool readSensorsSD() {
    String filename = "/Data/DevicesSensors.dat";
    File f = SD.open(filename, FILE_READ);
    if (f==false) {
        storeError("readSensorsSD: Could not read Devices_Sensors data");
        f.close();
        return false;
    }

    if (f.size() != sizeof(Devices_Sensors)) {
        storeError("readSensorsSD: File size mismatch");
        f.close();
        return false;
    }

    // Read the entire Devices_Sensors object
    f.read((uint8_t*)&Sensors, sizeof(Devices_Sensors));
    f.close();
    return true;
}

//storing variables
bool storeScreenInfoSD() {
    String filename = "/Data/ScreenFlags.dat";
    File f = SD.open(filename, FILE_WRITE);
    if (f==false) {
        storeError("Failed to open file to write screen to SD");
        f.close();
        return false;
    }
    union ScreenInfoBytes D; 
    D.screendata=I;

    f.write(D.bytes,sizeof(Screen));

    f.close();
    return true;
    
}

bool readScreenInfoSD() //read last screenInfo
{
    union ScreenInfoBytes D; 
    String filename = "/Data/ScreenFlags.dat";

    File f = SD.open(filename, FILE_READ);

    if (f==false)  {
        #ifdef _DEBUG
        Serial.printf("Failed to open screenInfo file from SD\n");
        #endif
        storeError("Failed to open screenInfo file from SD");
        
        f.close();
        return false;
    }
    if (f.size() != sizeof(Screen)) {
              #ifdef _DEBUG
        Serial.printf("file on SD was not the size of screenInfo!\n");
        #endif

        storeError("Screen flag file was the wrong size");

        f.close();

        deleteFiles("ScreenFlags.dat","/Data");

        return false;
    }

    while (f.available()) {
        f.read(D.bytes,sizeof(Screen));
        I = D.screendata;
    }

    f.close();

    return true;

}

// New functions for individual sensor data storage using Devices_Sensors class
bool storeSensorDataSD(int16_t sensorIndex) {
    if (sensorIndex < 0 || sensorIndex >= NUMSENSORS) {
        return false;
    }
    
    SnsType* sensor = Sensors.getSensorByIndex(sensorIndex);
    if (!sensor) {
        return false;
    }
    
    // Create filename based on sensor type and ID
    char filename[32];
    snprintf(filename, sizeof(filename), "/sensor_%02d_%02d.dat", sensor->snsType, sensor->snsID);
    
    // Open file for writing
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("Failed to open file: %s\n", filename);
        return false;
    }
    
    // Write sensor data in optimized format
    // Format: deviceIndex(2) + snsType(1) + snsID(1) + snsValue(8) + timeRead(4) + timeLogged(4) + Flags(1) + SendingInt(4) + name(30) = 51 bytes
    file.write((uint8_t*)&sensor->deviceIndex, 2);
    file.write((uint8_t*)&sensor->snsType, 1);
    file.write((uint8_t*)&sensor->snsID, 1);
    file.write((uint8_t*)&sensor->snsValue, 8);
    file.write((uint8_t*)&sensor->timeRead, 4);
    file.write((uint8_t*)&sensor->timeLogged, 4);
    file.write((uint8_t*)&sensor->Flags, 1);
    file.write((uint8_t*)&sensor->SendingInt, 4);
    file.write((uint8_t*)sensor->snsName, 30);
    
    file.close();
    Serial.printf("Stored sensor data to %s\n", filename);
    return true;
}

bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID) {
    // Find the device and sensor indices
    int16_t deviceIndex = Sensors.findDev(deviceMAC, false);
    if (deviceIndex < 0) {
        storeError("storeSensorDataSD: Device not found");
        return false;
    }
    
    int16_t sensorIndex = Sensors.findSns(deviceIndex, sensorType, sensorID);
    if (sensorIndex < 0) {
        storeError("storeSensorDataSD: Sensor not found");
        return false;
    }
    
    return storeSensorDataSD(sensorIndex);
}

bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, 
                     uint32_t t[], double v[], byte *N, uint32_t *samples, 
                     uint32_t starttime, uint32_t endtime, byte avgN) {
    
    // Create filename
    String filename = "/Data/Sensor_" + String(deviceMAC, HEX) + "_" + 
                     String(sensorType) + "_" + String(sensorID) + "_v4.dat";
    
    File f = SD.open(filename, FILE_READ);
    if (!f) {
        storeError("readSensorDataSD: Could not open file for reading");
        return false;
    }
    
    struct SensorDataRecord {
        uint64_t deviceMAC;
        uint8_t sensorType;
        uint8_t sensorID;
        double sensorValue;
        uint32_t timeRead;
        uint32_t timeLogged;
        uint8_t flags;
        uint32_t sendingInt;
    } record;
    
    byte cnt = 0, deltacnt = 0;
    double avgV = 0;
    *samples = f.size() / sizeof(record);
    
    while (f.available() >= sizeof(record)) {
        f.read((uint8_t*)&record, sizeof(record));
        
        // Check if this is the correct sensor
        if (record.deviceMAC != deviceMAC || record.sensorType != sensorType || record.sensorID != sensorID) {
            continue;
        }
        
        // Check time range
        if (record.timeLogged < starttime) continue;
        if (endtime != 0xFFFFFFFF && record.timeLogged > endtime) break;
        
        if (cnt < *N) {
            avgV += record.sensorValue;
            deltacnt++;
            
            if (deltacnt >= avgN) {
                t[cnt] = record.timeLogged;
                v[cnt] = avgV / deltacnt;
                deltacnt = 0;
                avgV = 0;
                cnt++;
            }
        }
    }
    
    *N = cnt;
    f.close();
    return true;
}

bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, 
                     uint32_t t[], double v[], byte *N, uint32_t *samples, 
                     uint32_t starttime, byte avgN) {
    return readSensorDataSD(deviceMAC, sensorType, sensorID, t, v, N, samples, starttime, 0xFFFFFFFF, avgN);
}

bool storeAllSensorsSD() {
    bool success = true;
    
    for (int16_t i = 0; i < NUMSENSORS && i < Sensors.getNumSensors(); i++) {
        if (Sensors.isSensorInit(i)) {
            if (!storeSensorDataSD(i)) {
                success = false;
            }
        }
    }
    
    return success;
}

bool readAllSensorsSD() {
    bool success = true;
    
    for (int16_t i = 0; i < NUMSENSORS && i < Sensors.getNumSensors(); i++) {
        if (Sensors.isSensorInit(i)) {
            if (!readSensorDataSD(i)) {
                success = false;
            }
        }
    }
    
    return success;
}

void deleteFiles(const char* pattern,const char* directory) {
  File dir = SD.open(directory); // Open the root directory

    String filename ;;

  if (!dir) {
    storeError("deleteFiles: Could not open dir");

    #ifdef _DEBUG 
    Serial.println("Error opening root directory.");
    #endif
    return;
  }


  while (true) {
    File entry = dir.openNextFile();
    filename  = (String) directory + "/" + entry.name();
    #ifdef _DEBUG
        Serial.printf("Current file: %s\n",filename);
    #endif
    if (!entry) {
      // no more files
      break;
    }

    if (matchPattern(entry.name(), pattern)) {
        #ifdef _DEBUG
        Serial.print("Deleting: ");
        Serial.println(filename);
        #endif
      if (!SD.remove(filename)) {
        #ifdef _DEBUG
        Serial.print("Error deleting: ");
        Serial.println(filename);
        #endif
        
      }
    }
    entry.close();
  }
  dir.close();
}


// Simple wildcard matching function (supports * only)
bool matchPattern(const char* filename, const char* pattern) {
  const char* fnPtr = filename;
  const char* patPtr = pattern;

  while (*fnPtr != '\0' || *patPtr != '\0') {
    if (*patPtr == '*') {
      patPtr++; // Skip the '*'
      if (*patPtr == '\0') {
        return true; // '*' at the end matches everything remaining
      }
      while (*fnPtr != '\0') {
        if (matchPattern(fnPtr, patPtr)) {
          return true;
        }
        fnPtr++;
      }
      return false; // No match found after '*'
    } else if (*fnPtr == *patPtr) {
      fnPtr++;
      patPtr++;
    } else {
      return false; // Mismatch
    }
  }

  return (*fnPtr == '\0' && *patPtr == '\0'); // Both strings must be at the end
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  
  File root = fs.open(dirname);
  if(!root){
    return;
  }
  if(!root.isDirectory()){
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
  //    Serial.print("  DIR : ");
    //  Serial.println(file.name());
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      // Serial.print("  FILE: ");
      // Serial.print(file.name());
      // Serial.print("  SIZE: ");
      // Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}
