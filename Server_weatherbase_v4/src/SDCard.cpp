#include <SDCard.hpp>
#include <globals.hpp>
#include <SD.h>
#include <cstdlib>
#include "utility.hpp"
#include "Devices.hpp"
#include "GsheetUpload.hpp"


#ifdef _USEWEATHER
#include "Weather_Optimized.hpp"

extern WeatherInfoOptimized WeatherData;

//functions to store weather data
bool storeWeatherDataSD() {
  String filename = "/Data/WeatherData.dat";
  File f = SD.open(filename, FILE_WRITE);
  if (f==false) {
    storeError("storeWeatherDataSD: Could not write weather data",ERROR_SD_WEATHERDATAWRITE);
    return false;
  }

  f.write((uint8_t*)&WeatherData, sizeof(WeatherData));
  f.close();
  return true;
}

//function to read weather data
bool readWeatherDataSD() {
  String filename = "/Data/WeatherData.dat";
  File f = SD.open(filename, FILE_READ);
  if (f==false) {
    storeError("readWeatherDataSD: Could not read weather data",ERROR_SD_WEATHERDATAREAD);
    return false;
  }
  if (f.size() != sizeof(WeatherData)) {
    storeError("readWeatherDataSD: File size mismatch",ERROR_SD_WEATHERDATASIZE);
    f.close();

    deleteFiles("WeatherData.dat","/Data");
    return false;
  }
  f.read((uint8_t*)&WeatherData, sizeof(WeatherData));
  f.close();
  return true;
}

#endif




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
    //SerialPrint("loadSensorDataFromFile: opening sensor file: " + filename,true);  

    
    // Open file for reading
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        SerialPrint("loadSensorDataFromFile: Could not open sensor file: " + filename,true);  
        storeError("loadSensorDataFromFile: Could not open file",ERROR_SD_FILEREAD);
        return -1;
    }

    if (file.size() == 0) {
      SerialPrint("loadSensorDataFromFile: File is empty: " + filename,true);
      return 0;
    }



    uint32_t fileSize = file.size();
    if (fileSize != sizeof(SensorDataPoint)) {
      SerialPrint("loadSensorDataFromFile: File size mismatch: " + filename,true);
      storeError("loadSensorDataFromFile: File size mismatch",ERROR_SD_RETRIEVEDATAPARMS);
      file.close();
      deleteFiles(filename.c_str(),"/Data");
      return -2; //wrong file size
    }

    

    uint32_t totalPoints = fileSize / sizeof(SensorDataPoint);

    if (totalPoints == 0) {
      SerialPrint("loadSensorDataFromFile: No data found in file: " + filename,true);
      return 0;
    }

    uint32_t pointsToRead = 0;

    if (Npts <= totalPoints)   pointsToRead = Npts;
    else       pointsToRead = totalPoints;

    //SerialPrint("loadSensorDataFromFile: totalPoints = " + (String) totalPoints + " and pointsToRead = " + (String) pointsToRead,true);

    // Read all data points and filter by time range
    SensorDataPoint dataPoint;
    
    uint32_t j;
    uint32_t endOffset =  sizeof(SensorDataPoint);
    if (endOffset > fileSize) {
      SerialPrint("loadSensorDataFromFile: endOffset > fileSize, skipping",true);
      storeError("loadSensorDataFromFile: endOffset > fileSize, skipping",ERROR_SD_RETRIEVEDATAPARMS);
      file.close();
      return -1;
    }


    file.seek(fileSize); //go to the end of the file
    for (j = 0; j < pointsToRead; j++) {
      
      file.seek(file.position()-endOffset); //rewind 1 record
      if (file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint)) != sizeof(SensorDataPoint)) {
        SerialPrint("loadSensorDataFromFile: Could not read data point",true);
        storeError("loadSensorDataFromFile: Could not read data point",ERROR_SD_FILEREAD);
        file.close();
        return -1;
      }

      file.seek(file.position()-endOffset); //rewind back to where we were


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
            //SerialPrint("loadSensorDataFromFile: read data point " + (String) j + " of " + (String) pointsToRead + " from " + filename + " at time " + (String) dataPoint.timeLogged + " with value " + (String) dataPoint.snsValue,true);
        } else break; //no more valid data       
      
    }

    file.close();

    return j;
}

int16_t loadAverageSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* averagedTimes, double* averagedValues, uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize, uint16_t numPointsX) {

  if (windowSize == 0) {
    SerialPrint("loadAverageSensorDataFromFile: windowSize is 0",true);
    storeError("loadAverageSensorDataFromFile: windowSize is 0",ERROR_SD_RETRIEVEDATAPARMS);
    return -1;
  }

  if (timeEnd==0) timeEnd=-1;
  if (timeStart==0) timeStart=0;

  if (timeEnd==-1) timeEnd=I.currentTime; //-1 is some huge number


  if (timeStart>=timeEnd) {
    SerialPrint("loadAverageSensorDataFromFile: timeStart >= timeEnd",true);
    storeError("loadAverageSensorDataFromFile: timeStart >= timeEnd",ERROR_SD_RETRIEVEDATAPARMS);
    return -1;
  }

  String filename = createSensorFilename(deviceMAC, sensorType, sensorID);

  //SerialPrint("loadAverageSensorDataFromFile: opening sensor file: " + filename,true);  
  
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    SerialPrint("loadAverageSensorDataFromFile: Could not open sensor file: " + filename,true);  
    storeError("loadAverageSensorDataFromFile: Could not open file",ERROR_SD_FILEREAD);
    return -1;
  }

  uint32_t fileSize = file.size();
  //check if the file is empty
  if (fileSize == 0) {
    SerialPrint("loadAverageSensorDataFromFile: File is empty: " + filename,true);
    return 0;
  }

  uint32_t totalPoints = fileSize / sizeof(SensorDataPoint);
  uint32_t i;
  uint32_t j;
  SensorDataPoint dataPoint;

  uint32_t numWindows = (timeEnd-timeStart)/windowSize + 1;
  if (numWindows < numPointsX) numWindows = numPointsX; //typically numpointsX is not provided, ie numPointsX = 0, but if it is, we need to limit the number of windows

  if (numWindows>100) numWindows=100;
  if (numWindows==0) numWindows=1;

  //calculate the window start and end times
  uint32_t windowStart;
  
  if (timeStart + windowSize < timeEnd) windowStart = timeEnd - windowSize;
  else windowStart = timeStart;

  uint32_t endOffset = sizeof(SensorDataPoint);

  if (endOffset > fileSize) {
    SerialPrint("loadAverageSensorDataFromFile: endOffset > fileSize, skipping",true);
    storeError("loadAverageSensorDataFromFile: endOffset > fileSize, skipping",ERROR_SD_RETRIEVEDATAPARMS);
    file.close();
    return -1;
  }

  uint32_t windowEnd = timeEnd;
  //SerialPrint("loadAverageSensorDataFromFile: timeStart = " + (String) timeStart + " and timeEnd = " + (String) timeEnd + " and windowSize = " + (String) windowSize + " and numPointsX = " + (String) numPointsX + " and numWindows = " + (String) numWindows,true);

  if (totalPoints == 0) {
    SerialPrint("loadAverageSensorDataFromFile: No data found in file: " + filename,true);
    return 0;
  }
  
  file.seek(fileSize); //go to the end of the file

  i=0;
  int16_t index=0;
  while (i<numWindows) {
    //SerialPrint("loadAverageSensorDataFromFile: window " + (String) i + " of " + (String) numWindows + " from " + filename + " with windowStart = " + (String) windowStart + " and windowEnd = " + (String) windowEnd,true);
    uint16_t n=0;
    double avgVal=0;
    bool isgood = true;
    
    //load data from the file if it is within the window size time range
    for (j = 0; j < totalPoints; j++) {

      if (file.position() < endOffset) {
        SerialPrint("loadAverageSensorDataFromFile: file.position() < endOffset, skipping",true);
        storeError("loadAverageSensorDataFromFile: file.position() < endOffset, skipping",ERROR_SD_RETRIEVEDATAPARMS);
        isgood = false;
        break;
      }
      file.seek(file.position()-endOffset); //rewind 1 record so when we read we are back at this point
      if (file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint)) != sizeof(SensorDataPoint)) {
        SerialPrint("loadAverageSensorDataFromFile: Could not read data point",true);
        storeError("loadAverageSensorDataFromFile: Could not read data point",ERROR_SD_FILEREAD);
        isgood = false;
        break;
      } else {
        //SerialPrint("loadAverageSensorDataFromFile: read data point " + (String) j + " of " + (String) totalPoints + " from " + filename + " at time " + (String) dataPoint.timeLogged + " with value " + (String) dataPoint.snsValue,true);
      }

      if (dataPoint.deviceMAC != deviceMAC || 
            dataPoint.snsType != sensorType || 
            dataPoint.snsID != sensorID) {
            SerialPrint("loadAverageSensorDataFromFile: Data point does not match deviceMAC, sensorType, or sensorID",true);
            storeError("loadAverageSensorDataFromFile: Data point does not match deviceMAC, sensorType, or sensorID",ERROR_SD_RETRIEVEDATAPARMS);
            continue;
      }
      
      // Check if within time range
      if (dataPoint.timeLogged > windowEnd) continue; //without rewinding, so we check this datapoint again
      if (dataPoint.timeLogged >= windowStart) {
        avgVal+= dataPoint.snsValue;
        n++;
      } else {
        isgood = true; //we have reached the end of the window
        break; //no more valid data         in this window... without rewinding, so we check this datapoint again
      }

      file.seek(file.position()-endOffset); //rewind back another record, so next time we read the prior

    }

    if (n>0) {
      averagedValues[index] = avgVal/n;
      averagedTimes[index++] = windowStart + (windowEnd-windowStart)/2;
      //SerialPrint("loadAverageSensorDataFromFile: window " + (String) i + " of " + (String) numWindows + " from " + filename + " with windowStart = " + (String) windowStart + " and windowEnd = " + (String) windowEnd + " with avgVal = " + (String) avgVal + " and avgTime = " + (String) averagedTimes[i],true);
    }

    if (isgood==false) break;

    if (windowEnd > windowSize && windowStart > windowSize) {
      windowEnd -= windowSize;
      windowStart -= windowSize;
    }     else break; //no more valid data

    i++;
  }
  file.close();
  
  //returns the number of datapoints found
   return index; //the last window is not complete, so we return the previous one
}

union ScreenInfoBytes {
    STRUCT_CORE screendata;
    uint8_t bytes[sizeof(STRUCT_CORE)];

    ScreenInfoBytes() {};
};

// New functions for Devices_Sensors class
bool storeDevicesSensorsSD() {
    String filename = "/Data/DevicesSensors.dat";
    File f = SD.open(filename, FILE_WRITE); //overwrite the file
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
    SerialPrint("readDevicesSensorsSD: opening file: " + filename,true);
    File f = SD.open(filename, FILE_READ);
    if (f==false) {
        storeError("readDevicesSensorsSD: Could not read Devices_Sensors data",ERROR_SD_DEVICESENSORSOPEN);
        f.close();
        return false;
    }

    if (f.size() != sizeof(Devices_Sensors)) {
        storeError("readDevicesSensorsSD: File size mismatch",ERROR_SD_DEVICESENSORSSIZE);
        f.close();
        deleteFiles("DevicesSensors.dat","/Data");
        return false;
    }

    // Read the entire Devices_Sensors object
    f.read((uint8_t*)&Sensors, sizeof(Devices_Sensors));
    f.close();
    return true;
}

bool writeErrorToSD() {
    String filename = "/Data/DeviceErrors.txt";
    File f = SD.open(filename, FILE_APPEND); // Open in append mode to the end of the file

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

bool readErrorFromSD(String* error, uint16_t errornumber) {
  String filename = "/Data/DeviceErrors.txt";
  File f = SD.open(filename, FILE_READ);
  if (f==false) {
    return false;
  }
  if (f.size() <3) { //need at least 3 characters to read
    f.close();
    return false;
  }
  
  //start from end of file and go back errornumber lines
  *error = "";
  //seek the ith /n from the end of the file
  uint32_t n=0;
  f.seek(f.size()); //goto end of file
      
  while (n<errornumber+1 && f.position() > 2) {
    //is the last character a /n?
    f.seek(f.position()-1);
    if (f.read() == '\n') n++;
    //now position at the start of this line
    while (f.position() > 2) {
      f.seek(f.position()-2);
      if (f.read() == '\n') break;
    }      
  }

  if (n<errornumber+1) {
    f.close();
    return false;
  }
  
  *error = f.readStringUntil('\n');
  if (error->length() == 0) return false;
  
  f.close();
  return true;
}


//storing variables
bool storeScreenInfoSD() {
    String filename = "/Data/ScreenFlags.dat";
    File f = SD.open(filename, FILE_WRITE); //overwrite the file
    if (f==false) {
        storeError("Failed to open file to write screen to SD", ERROR_SD_SCREENFLAGSWRITE, false);
        f.close();
        return false;
    }
    union ScreenInfoBytes D; 
    D.screendata = I;

    f.write(D.bytes,sizeof(STRUCT_CORE));

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
    if (f.size() != sizeof(STRUCT_CORE)) {
              
        SerialPrint("file on SD was not the size of screenInfo!\n");
        

        storeError("Screen flag file was the wrong size",ERROR_SD_SCREENFLAGSSIZE);

        f.close();

        deleteFiles("ScreenFlags.dat","/Data");

        return false;
    }

    while (f.available()) {
        f.read(D.bytes,sizeof(STRUCT_CORE));
        I = D.screendata;
    }

    f.close();

    return true;

}

// New functions for individual sensor data storage using Devices_Sensors class
//wrapper, if device infor was provided, use it to find the sensor index
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


bool storeSensorDataSD(int16_t sensorIndex) {
    if (sensorIndex < 0 || sensorIndex >= NUMSENSORS) {
        SerialPrint((String) "storeSensorDataSD: sensorIndex out of bounds: " + (String) sensorIndex + "\n");
        storeError("storeSensorDataSD: sensorIndex out of bounds", ERROR_SD_RETRIEVEDATAPARMS);
        return false;
    }
    
    SnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);
    if (!sensor) {
        SerialPrint((String) "storeSensorDataSD: sensor not found: " + (String) sensorIndex + "\n");
        storeError("storeSensorDataSD: sensor not found", ERROR_SD_RETRIEVEDATAPARMS);
        return false;
    }
    
    // Create filename using helper function
    String filename = createSensorFilename(Sensors.getDeviceMACBySnsIndex(sensorIndex), sensor->snsType, sensor->snsID);
    
    // Open file for appending
    File file = SD.open(filename, FILE_APPEND); //append to the file
    if (!file) {
        SerialPrint((String) "Failed to open file " + filename + "\n");
        storeError(((String) "Failed to open sensor data file " + filename).c_str(),ERROR_SD_FILEWRITE,false);
        return false;
    }
    
    // Create and populate SensorDataPoint struct
    SensorDataPoint dataPoint;
    dataPoint.deviceMAC = Sensors.getDeviceMACBySnsIndex(sensorIndex);
    dataPoint.deviceIndex = sensor->deviceIndex;
    dataPoint.snsType = sensor->snsType;
    dataPoint.snsID = sensor->snsID;
    dataPoint.snsValue = sensor->snsValue;
    dataPoint.timeRead = sensor->timeRead;
    dataPoint.timeLogged = sensor->timeLogged;
    dataPoint.Flags = sensor->Flags;
    dataPoint.SendingInt = sensor->SendingInt;
    strncpy(dataPoint.snsName, sensor->snsName, sizeof(dataPoint.snsName));
    dataPoint.snsName[sizeof(dataPoint.snsName) - 1] = '\0'; // Ensure null termination
    
    // Write the complete struct
    file.write((uint8_t*)&dataPoint, sizeof(SensorDataPoint));
    
    file.close();
    //SerialPrint((String) "StoreSensorDataSD: Stored sensor data from " + (String) sensor->snsName + " with sensor index " + (String) sensorIndex + " and associated device MAC " + (String) Sensors.getDeviceMACBySnsIndex(sensorIndex) + " to " +  filename + " with time " + (String) sensor->timeLogged,true);
    return true;
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
    // Format: deviceMAC(8) + deviceIndex(2) + snsType(1) + snsID(1) + snsValue(8) + timeRead(4) + timeLogged(4) + Flags(1) + SendingInt(4) + name(30) 
    if (file.available() >= sizeof(SensorDataPoint)) {
        SensorDataPoint dataPoint;
        if (file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint)) == sizeof(SensorDataPoint)) {
            sensor->deviceIndex = dataPoint.deviceIndex;
            sensor->snsType = dataPoint.snsType;
            sensor->snsID = dataPoint.snsID;
            sensor->snsValue = dataPoint.snsValue;
            sensor->timeRead = dataPoint.timeRead;
            sensor->timeLogged = dataPoint.timeLogged;
            sensor->Flags = dataPoint.Flags;
            sensor->SendingInt = dataPoint.SendingInt;
            strncpy(sensor->snsName, dataPoint.snsName, sizeof(sensor->snsName));
            sensor->snsName[sizeof(sensor->snsName) - 1] = '\0'; // Ensure null termination
            
            file.close();
            Serial.printf("Read sensor data from %s\n", filename.c_str());
            return true;
        } else {
          SerialPrint("readSensorDataSD: Could not read sensor data from file: " + filename,true);
          storeError(((String) "readSensorDataSD: Could not read sensor data from file: " + filename).c_str() ,ERROR_SD_FILEREAD);
          return false;
        }
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
  if (timeEnd <= timeStart || *N == 0) {
    storeError("retrieveSensorDataFromSD: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
    return false;
  }


  int16_t n = loadSensorDataFromFile(deviceMAC, sensorType, sensorID, t, v, timeStart, timeEnd, *N);

  if (n>*N) {
    SerialPrint("retrieveSensorDataFromSD: ?ERROR? Returned more data than requested.\n");
    storeError("retrieveSensorDataFromSD: ?ERROR? Returned more data than requested.", ERROR_SD_RETRIEVEDATAPARMS);
    return false;

  }
  if (n<0) {
    SerialPrint("retrieveSensorDataFromSD: file loading failed.\n");
    return false;
  }
  
  if (n==0) {
    SerialPrint("retrieveSensorDataFromSD: No errors, but no data was returned.\n");
    storeError("retrieveSensorDataFromSD: No errors, but no data was returned.", ERROR_SD_RETRIEVEDATAMISSING);
    return true; // No data found, but not an error
  }
  
  if (forwardOrder) {
    // reverse the orders of t and v
    //note that only the FIRST n points were actually entered

    uint32_t t0[n] = {0};
    double v0[n] = {0};

    int16_t i=0;
    for (int16_t j=n-1; j>=0;j--) {
      t0[i] = t[j];
      v0[i] = v[j];
      i++;
    }

    memcpy(t, t0, n * sizeof(uint32_t));
    memcpy(v, v0, n * sizeof(double));
    // No free needed for stack arrays
  } 
  
  *N = n;
  return true;
} 

bool retrieveMovingAverageSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,
                                         uint32_t timeStart, uint32_t timeEnd,
                                         uint32_t windowSize, uint16_t* numPointsX, double* averagedValues, uint32_t* averagedTimes, bool forwardOrder) {
    
      // Validate inputs
    if (windowSize == 0 || averagedValues == nullptr || averagedTimes == nullptr || timeEnd <= timeStart) {
      SerialPrint("retrieveMovingAverageSensorDataFromSD: Invalid parameters", true);
      storeError("retrieveMovingAverageSensorDataFromSD: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
      return false;
    }

    int16_t n = loadAverageSensorDataFromFile(deviceMAC, sensorType, sensorID, averagedTimes, averagedValues, timeStart, timeEnd, windowSize, *numPointsX);

    if (n<0) {
      SerialPrint("retrieveMovingAverageSensorDataFromSD: could not read", true);
      storeError("retrieveMovingAverageSensorDataFromSD: could not read",  ERROR_SD_RETRIEVEDATAMISSING);
      return false;
    }

    if (n==0) {
      SerialPrint("retrieveMovingAverageSensorDataFromSD: No data points in time range", true);
      storeError("retrieveMovingAverageSensorDataFromSD: No data points in time range",  ERROR_SD_RETRIEVEDATAMISSING);
      return false;
    }

    if (forwardOrder) {
      // reverse the orders of t and v
      //note that only the FIRST n points were actually entered
  
      uint32_t t0[n] = {0};
      double v0[n] = {0};
  
      int16_t i=0;
      for (int16_t j=n-1; j>=0;j--) {
        t0[i] = averagedTimes[j];
        v0[i] = averagedValues[j];
        i++;
      }
  
      memcpy(averagedTimes, t0, n * sizeof(uint32_t));
      memcpy(averagedValues, v0, n * sizeof(double));
      
    } 
    
    *numPointsX = n;
  

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
    return readAllSensorsSD();
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

#ifdef _USEGSHEET
extern STRUCT_GOOGLESHEET GSheetInfo;

bool storeGsheetInfoSD() {
    if (GSheetInfo.lastGsheetSDSaveTime+60>I.currentTime) return true; //only save if it's been more than 60 seconds since last save
    String filename = "/Data/GsheetInfo.dat";
    File f = SD.open(filename, FILE_WRITE); //overwrite the file
    if (f==false) {
        storeError("storeGsheetInfoSD: Could not write GsheetInfo data",ERROR_SD_GSHEETINFOWRITE,false);
        f.close();
        return false;
    }
    GSheetInfo.lastGsheetSDSaveTime = I.currentTime;
    f.write((uint8_t*)&GSheetInfo, sizeof(STRUCT_GOOGLESHEET));
    f.close();
    return true;
}

bool readGsheetInfoSD() {
    String filename = "/Data/GsheetInfo.dat";
    File f = SD.open(filename, FILE_READ);
    if (f==false) {
        storeError("readGsheetInfoSD: Could not read GsheetInfo data",ERROR_SD_GSHEETINFOREAD,false);
        f.close();
        return false;
    }
    if (f.size() != sizeof(STRUCT_GOOGLESHEET)) {
        storeError("readGsheetInfoSD: File size mismatch",ERROR_SD_GSHEETINFOREAD,false);
        deleteFiles("GsheetInfo.dat", "/Data");
        f.close();
        return false;
    }
    f.read((uint8_t*)&GSheetInfo, sizeof(STRUCT_GOOGLESHEET));
    f.close();
    return true;
}
#endif
