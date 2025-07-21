#include <SDCard.hpp>
#include <globals.hpp>
#include <SD.h>
#include <utility.hpp>
#include <cstdlib>
#include "Devices.hpp"

// Helper function to create sensor filename
String createSensorFilename(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID) {
    char filename[64];
    uint8_t MACID[6] = {0};
    uint64ToMAC(deviceMAC,MACID);

    snprintf(filename, sizeof(filename), "/Data/sns_%x%x%x%x%x%x_%02d_%02d.dat", 
             MACID[5],MACID[4],MACID[3],MACID[2],MACID[1],MACID[0], sensorType, sensorID);
    
    return String(filename);
}

// Helper function to load sensor data from file with optional time filtering
int16_t loadSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,uint32_t* t, double* v, uint32_t timeStart, uint32_t timeEnd, uint16_t Npts) {
  //returns the number of datapoints found
    String filename = createSensorFilename(deviceMAC, sensorType, sensorID);
    
    // Open file for reading
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        SerialPrint("loadSensorDataFromFile: Could not open sensor file: " + filename,true);  
        storeError("loadSensorDataFromFile: Could not open file",ERROR_SD_FILEREAD);
        return -1;
    }
    
    // Read all data points and filter by time range
    SensorDataPoint dataPoint;
    
    uint16_t j;
    for (j=0;j<Npts;j++) {
      file.seek(file.size()-(Npts+1)*sizeof(SensorDataPoint));//go to end of file - for the most part the data of interest will be near the end. N is datapoint of interest
      if (file.available() >= sizeof(SensorDataPoint)) {
        file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint));
        
        // Verify this is the correct sensor
        if (dataPoint.deviceMAC != deviceMAC || 
            dataPoint.snsType != sensorType || 
            dataPoint.snsID != sensorID) {
            continue;
        }
        
        // Check if within time range
        if (dataPoint.timeLogged > timeEnd) continue;
        if (dataPoint.timeLogged >= timeStart) {
            t[j] = dataPoint.timeLogged;
            v[j] = dataPoint.snsValue;
        } else break; //no more valid data       
      } else break;
    }
    file.close();

    return j;
}

union ScreenInfoBytes {
    struct Screen screendata;
    uint8_t bytes[sizeof(Screen)];

    ScreenInfoBytes() {};
};

// New functions for Devices_Sensors class
bool storeDevicesSensorsSD() {
    String filename = "/Data/DevicesSensors.dat";
    File f = SD.open(filename, FILE_WRITE);
    if (f==false) {
        storeError("storeDevicesSensorsSD: Could not write Devices_Sensors data",ERROR_SD_DEVICESENSORSWRITE,false);
        f.close();
        return false;
    }

    // Write the entire Devices_Sensors object
    f.write((uint8_t*)&Sensors, sizeof(Devices_Sensors));
    f.close();
    return true;
}

bool readDevicesSensorsSD() {
    String filename = "/Data/DevicesSensors.dat";
    File f = SD.open(filename, FILE_READ);
    if (f==false) {
        storeError("readDevicesSensorsSD: Could not read Devices_Sensors data",ERROR_SD_DEVICESENSORSOPEN);
        f.close();
        return false;
    }

    if (f.size() != sizeof(Devices_Sensors)) {
        storeError("readDevicesSensorsSD: File size mismatch",ERROR_SD_DEVICESENSORSSIZE);
        f.close();
        return false;
    }

    // Read the entire Devices_Sensors object
    f.read((uint8_t*)&Sensors, sizeof(Devices_Sensors));
    f.close();
    return true;
}

bool writeErrorToSD() {
    String filename = "/Data/DeviceErrors.txt";
    File f = SD.open(filename, FILE_APPEND); // Open in append mode

    if (f==false) {
        storeError("writeErrorSD: Could not open error log",ERROR_SD_LOGOPEN,false); //do not store this error, to avoid infinite loop
        f.close();
        return false;
    }

    String logEntry = dateify(I.lastErrorTime) + (String) "," + I.lastErrorCode + "," + I.lastError;
    if (f.println(logEntry) == false)   {
      storeError("writeErrorSD: Could not write to error log",ERROR_SD_LOGWRITE,false); //do not store this error, to avoid infinite loop
      f.close();
      return false;
    }
    f.close();
    
    return true;
}


//storing variables
bool storeScreenInfoSD() {
    String filename = "/Data/ScreenFlags.dat";
    File f = SD.open(filename, FILE_WRITE);
    if (f==false) {
        storeError("Failed to open file to write screen to SD", ERROR_SD_SCREENFLAGSWRITE, false);
        f.close();
        return false;
    }
    union ScreenInfoBytes D; 
    D.screendata = I;

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
        
        SerialPrint("Failed to open screenInfo file from SD\n");
        
        storeError("Failed to open screenInfo file from SD",ERROR_SD_SCREENFLAGSREAD);
        
        f.close();
        return false;
    }
    if (f.size() != sizeof(Screen)) {
              
        SerialPrint("file on SD was not the size of screenInfo!\n");
        

        storeError("Screen flag file was the wrong size",ERROR_SD_SCREENFLAGSSIZE);

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
    
    SnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);
    if (!sensor) {
        return false;
    }
    
    // Create filename using helper function
    String filename = createSensorFilename(Sensors.getDeviceMACBySnsIndex(sensorIndex), sensor->snsType, sensor->snsID);
    
    // Open file for writing
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        SerialPrint((String) "Failed to open file " + filename + "\n");
        storeError(((String) "Failed to open sensor data file " + filename).c_str(),ERROR_SD_FILEWRITE,false);
        return false;
    }
    
    // Write sensor data in optimized format
    // Format: deviceMAC(8) + deviceIndex(2) + snsType(1) + snsID(1) + snsValue(8) + timeRead(4) + timeLogged(4) + Flags(1) + SendingInt(4) + name(30) = 59 bytes
    uint64_t deviceMAC = Sensors.getDeviceMACBySnsIndex(sensorIndex);
    file.write((uint8_t*)&deviceMAC, 8);
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
//    SerialPrint((String) "Stored sensor data to " +  filename,true);
    return true;
}

bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID) {
    // Find the device and sensor indices
    int16_t deviceIndex = Sensors.findDevice(deviceMAC);
    if (deviceIndex < 0) {
        storeError("storeSensorDataSD: Device not found", ERROR_SD_DEVICESENSORSNODEV);
        return false;
    }
    
    uint64_t deviceMACValue = Sensors.getDeviceMACByDevIndex(deviceIndex);
    int16_t sensorIndex = Sensors.findSensor(deviceMACValue, sensorType, sensorID);
    if (sensorIndex < 0) {
        storeError("storeSensorDataSD: Sensor not found", ERROR_SD_DEVICESENSORSNOSNS);
        return false;
    }
    
    return storeSensorDataSD(sensorIndex);
}

bool readSensorDataSD(int16_t sensorIndex) {
    if (sensorIndex < 0 || sensorIndex >= NUMSENSORS) {
        return false;
    }
    
    SnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);
    if (!sensor) {
        return false;
    }
    
    uint64_t deviceMAC = Sensors.getDeviceMACBySnsIndex(sensorIndex);

    // Create filename using helper function
    String filename = createSensorFilename(deviceMAC, sensor->snsType, sensor->snsID);
    
    // Open file for reading
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        SerialPrint( (String) "Failed to open file for reading: " + filename + "\n");
        storeError(((String) "Failed to open " + filename).c_str(),ERROR_SD_FILEREAD);
        return false;
    }
    
    // Read sensor data in optimized format
    // Format: deviceMAC(8) + deviceIndex(2) + snsType(1) + snsID(1) + snsValue(8) + timeRead(4) + timeLogged(4) + Flags(1) + SendingInt(4) + name(30) = 59 bytes
    if (file.available() >= 59) {
        file.read((uint8_t*)&deviceMAC, 8);
        file.read((uint8_t*)&sensor->deviceIndex, 2);
        file.read((uint8_t*)&sensor->snsType, 1);
        file.read((uint8_t*)&sensor->snsID, 1);
        file.read((uint8_t*)&sensor->snsValue, 8);
        file.read((uint8_t*)&sensor->timeRead, 4);
        file.read((uint8_t*)&sensor->timeLogged, 4);
        file.read((uint8_t*)&sensor->Flags, 1);
        file.read((uint8_t*)&sensor->SendingInt, 4);
        file.read((uint8_t*)sensor->snsName, 30);
        
        file.close();
        Serial.printf("Read sensor data from %s\n", filename.c_str());
        return true;
    }
    
    file.close();
    return false;
}


bool retrieveSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, byte *N, uint32_t t[], double v[], uint32_t timeStart, uint32_t timeEnd, bool forwardOrder) {
  //retrieve up to N most recent data points ending at timeEnd. If fewer than N data points are found, return the number of data points found.

  // Validate inputs
  if (N == nullptr || t == nullptr || v == nullptr) {
    SerialPrint("retrieveSensorDataFromSD: N, t, or v were nullptr.\n");
    storeError("retrieveSensorDataFromSD: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
    return false;
  }


  int16_t n = loadSensorDataFromFile(deviceMAC, sensorType, sensorID, t, v, timeStart, timeEnd, *N);
  if (n<0) {
    SerialPrint("retrieveSensorDataFromSD: file loading failed.\n");

    return false;
  }
  
  if (n==0) {
    SerialPrint("retrieveSensorDataFromSD: No errors, but no data was returned.\n");
    return true; // No data found, but not an error
  }
  
  if (forwardOrder) {
    // reverse the orders of t and v
    //note that only the FIRST n points were actually entered

    uint32_t t0[*N] = {0};
    double v0[*N] = {0};

    int16_t i=0;

    for (int16_t j=n-1; j>=0;j--) {
      t0[i] = t[j];
      v0[i] = v[j];
      i++;
    }

    //reassign actual vectors
    for (int16_t j=0; j<*N;j++) {
      t[j] = t0[j];
      v[j] = v0[j];
    }
  } 
  
  *N = n;
  return true;
} 

bool retrieveMovingAverageSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,
                                         uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize,
                                         uint16_t numPointsX, double* averagedValues, uint32_t* averagedTimes) {
    // Validate inputs
    if (timeEnd <= timeStart || numPointsX == 0 || windowSize == 0 || 
        averagedValues == nullptr || averagedTimes == nullptr) {
        storeError("retrieveMovingAverageSensorDataFromSD: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
        return false;
    }
    
    // Load data points using helper function
    std::vector<SensorDataPoint> validDataPoints;
    if (!loadSensorDataFromFile(deviceMAC, sensorType, sensorID, validDataPoints, timeStart, timeEnd)) {
        return false;
    }
    
    if (validDataPoints.empty()) {
        storeError("retrieveMovingAverageSensorDataFromSD: No data points in time range",  ERROR_SD_RETRIEVEDATAMISSING);
        return false;
    }
    
    // Sort data points by time
    std::sort(validDataPoints.begin(), validDataPoints.end(), 
              [](const SensorDataPoint& a, const SensorDataPoint& b) {
                  return a.timeLogged < b.timeLogged;
              });
    
    // Calculate time interval between output points (evenly spaced between timeStart and timeEnd)
    uint32_t timeInterval = 0;
    if (numPointsX > 1) {
        timeInterval = (timeEnd - timeStart) / (numPointsX - 1);
    }
    
    // Generate evenly spaced output points between timeStart and timeEnd
    for (uint16_t i = 0; i < numPointsX; i++) {
        uint32_t targetTime = timeStart + (i * timeInterval);
        
        // Calculate moving average around target time
        double sum = 0.0;
        uint32_t count = 0;
        uint32_t windowStart = (targetTime > windowSize/2) ? targetTime - windowSize/2 : 0; // windowStart is windowSize/2 seconds before targetTime
        uint32_t windowEnd = targetTime + windowSize/2; // windowEnd is windowSize/2 seconds after targetTime
        
        // Sum all data points within the window
        for (const auto& point : validDataPoints) {
            if (point.timeLogged >= windowStart && point.timeLogged <= windowEnd) {
                sum += point.snsValue;
                count++;
            }
        }
        
        // Calculate average and store result
        if (count > 0) {
            averagedValues[i] = sum / count;
        } else {
            // No data in window, use nearest point or interpolate
            averagedValues[i] = findNearestValue(validDataPoints, targetTime);
        }
        
        averagedTimes[i] = targetTime;
    }
    
    return true;
}

bool retrieveMovingAverageSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,
                                         uint32_t timeEnd, uint32_t windowSize,
                                         uint16_t numPointsX, double* averagedValues, uint32_t* averagedTimes) {
    // Validate inputs
    if (numPointsX == 0 || windowSize == 0 || 
        averagedValues == nullptr || averagedTimes == nullptr) {
        storeError("retrieveMovingAverageSensorDataFromSD: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
        return false;
    }
    
    // Load data points using helper function (up to timeEnd)
    std::vector<SensorDataPoint> validDataPoints;
    if (!loadSensorDataFromFile(deviceMAC, sensorType, sensorID, validDataPoints, 0, timeEnd)) {
        return false;
    }
    
    if (validDataPoints.empty()) {
        storeError("retrieveMovingAverageSensorDataFromSD: No data points found", ERROR_SD_RETRIEVEDATAMISSING);
        return false;
    }
    
    // Sort data points by time (most recent first)
    std::sort(validDataPoints.begin(), validDataPoints.end(), 
              [](const SensorDataPoint& a, const SensorDataPoint& b) {
                  return a.timeLogged > b.timeLogged; // Descending order
              });
    
    // Limit to the most recent data points if we have more than needed
    if (validDataPoints.size() > numPointsX * 10) { // Keep 10x more than needed for averaging
        validDataPoints.resize(numPointsX * 10);
    }
    
    // Sort back to ascending order for processing
    std::sort(validDataPoints.begin(), validDataPoints.end(), 
              [](const SensorDataPoint& a, const SensorDataPoint& b) {
                  return a.timeLogged < b.timeLogged;
              });
    
    // Calculate time range from the data
    uint32_t timeStart = validDataPoints.front().timeLogged;
    uint32_t actualTimeEnd = validDataPoints.back().timeLogged;
    
    // Calculate time interval between output points (windowSize seconds apart)
    uint32_t timeInterval = windowSize;
    
    // Generate output points spaced windowSize seconds apart
    for (uint16_t i = 0; i < numPointsX; i++) {
        uint32_t targetTime = timeStart + (i * timeInterval);
        
        // Calculate moving average around target time
        double sum = 0.0;
        uint32_t count = 0;
        uint32_t windowStart = (targetTime > windowSize/2) ? targetTime - windowSize/2 : 0;
        uint32_t windowEnd = targetTime + windowSize/2;
        
        // Sum all data points within the window
        for (const auto& point : validDataPoints) {
            if (point.timeLogged >= windowStart && point.timeLogged <= windowEnd) {
                sum += point.snsValue;
                count++;
            }
        }
        
        // Calculate average and store result
        if (count > 0) {
            averagedValues[i] = sum / count;
        } else {
            // No data in window, use nearest point or interpolate
            averagedValues[i] = findNearestValue(validDataPoints, targetTime);
        }
        
        averagedTimes[i] = targetTime;
    }
    
    return true;
}

// Helper function to find nearest value when no data in window
double findNearestValue(const std::vector<SensorDataPoint>& dataPoints, uint32_t targetTime) {
    if (dataPoints.empty()) return 0.0;
    
    // Find the closest data point by time
    uint32_t minDiff = 0xFFFFFFFF;
    double nearestValue = dataPoints[0].snsValue;
    
    for (const auto& point : dataPoints) {
        uint32_t diff = abs((int32_t)(point.timeLogged - targetTime));
        if (diff < minDiff) {
            minDiff = diff;
            nearestValue = point.snsValue;
        }
    }
    
    return nearestValue;
}

bool storeAllSensorSD() {
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

bool readAllSensorSD() {
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
    storeError("deleteFiles: Could not open dir", ERROR_SD_OPENDIR);

    SerialPrint("Error opening root directory.");
    return;
  }


  while (true) {
    File entry = dir.openNextFile();
    filename  = (String) directory + "/" + entry.name();
    SerialPrint("Current file: " + filename + "\n");
    if (!entry) {
      // no more files
      break;
    }

    if (matchPattern(entry.name(), pattern)) {
        SerialPrint("Deleting: " + filename + "\n");
        
      if (!SD.remove(filename)) {
        SerialPrint("Error deleting: " + filename + "\n");
        storeError(((String) "Error deleting: " + filename).c_str(), ERROR_SD_FILEDEL);
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
