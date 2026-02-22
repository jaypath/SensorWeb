#include "globals.hpp"

#ifdef _USESDCARD

#include <SD.h>
#include <cstdlib>
#include "SDCard.hpp"
#include "utility.hpp"
#include "Devices.hpp"
#include "GsheetUpload.hpp"

// ESP32 VFS timestamp functions
#ifdef ESP32
#include <sys/stat.h>
#include <sys/time.h>
#endif



// Function to maintain a timestamp index file
void updateTimestampIndex(const char* filename) {
    String indexFilename = "/Data/FileTimestamps.dat";
    String newEntry = String(filename) + "," + String(I.currentTime) + "\n";
    
    // First, check if the file already exists in the index
    File indexFile = SD.open(indexFilename, FILE_READ);
    if (indexFile) {
        String content = "";
        while (indexFile.available()) {
            content += indexFile.readString();
        }
        indexFile.close();
        
        // Check if filename already exists in the index
        bool fileExists = false;
        String newContent = "";
        int lineStart = 0;
        int lineEnd = content.indexOf('\n');
        
        while (lineEnd >= 0) {
            String line = content.substring(lineStart, lineEnd);
            line.trim();
            
            if (line.length() > 0) {
                int commaPos = line.indexOf(',');
                if (commaPos > 0) {
                    String fileInIndex = line.substring(0, commaPos);
                    if (fileInIndex == String(filename)) {
                        // File exists, update the timestamp
                        newContent += newEntry;
                        fileExists = true;
                    } else {
                        // Keep the existing line unchanged
                        newContent += line + "\n";
                    }
                }
            }
            
            lineStart = lineEnd + 1;
            lineEnd = content.indexOf('\n', lineStart);
        }
        
        // If file wasn't found, add it as a new entry
        if (!fileExists) {
            newContent += newEntry;
        }
        
        // Write the updated content back to the index file
        File newIndexFile = SD.open(indexFilename, FILE_WRITE);
        if (newIndexFile) {
            newIndexFile.print(newContent);
            newIndexFile.close();
            SerialPrint("Updated timestamp index for: " + String(filename) + " at " + dateify(I.currentTime), true);
        } else {
            SerialPrint("Failed to update timestamp index for: " + String(filename), true);
        }
    } else {
        // Index file doesn't exist, create it with the new entry
        File newIndexFile = SD.open(indexFilename, FILE_WRITE);
        if (newIndexFile) {
            newIndexFile.print(newEntry);
            newIndexFile.close();
            SerialPrint("Created timestamp index with: " + String(filename) + " at " + dateify(I.currentTime), true);
        } else {
            SerialPrint("Failed to create timestamp index for: " + String(filename), true);
        }
    }
}

// Function to remove a file from the timestamp index
void removeFromTimestampIndex(const char* filename) {
    String indexFilename = "/Data/FileTimestamps.dat";
    
    // Read the current index file
    File indexFile = SD.open(indexFilename, FILE_READ);
    if (!indexFile) {
        // Index file doesn't exist, nothing to remove
        return;
    }
    
    String content = "";
    while (indexFile.available()) {
        content += indexFile.readString();
    }
    indexFile.close();
    
    // Parse and filter out the deleted file
    String newContent = "";
    int lineStart = 0;
    int lineEnd = content.indexOf('\n');
    
    while (lineEnd >= 0) {
        String line = content.substring(lineStart, lineEnd);
        line.trim();
        
        if (line.length() > 0) {
            int commaPos = line.indexOf(',');
            if (commaPos > 0) {
                String fileInIndex = line.substring(0, commaPos);
                // Only keep lines that don't match the deleted filename
                if (fileInIndex != String(filename)) {
                    newContent += line + "\n";
                }
            }
        }
        
        lineStart = lineEnd + 1;
        lineEnd = content.indexOf('\n', lineStart);
    }
    
    // Write the filtered content back to the index file
    File newIndexFile = SD.open(indexFilename, FILE_WRITE);
    if (newIndexFile) {
        newIndexFile.print(newContent);
        newIndexFile.close();
        SerialPrint("Removed from timestamp index: " + String(filename), true);
    }
}

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
  
  // Update the timestamp index file
  updateTimestampIndex(filename.c_str());
  
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
    SerialPrint("readWeatherDataSD: File size mismatch, deleting file: " + filename,true);
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
int16_t loadSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,uint32_t* t, double* v, uint8_t f[], uint32_t timeStart, uint32_t timeEnd, uint16_t Npts) {
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
    if (fileSize % sizeof(SensorDataPoint) != 0) {
      SerialPrint("loadSensorDataFromFile: File size is not a multiple of SensorDataPoint size: " + filename + " (size: " + String(fileSize) + ", expected multiple of " + String(sizeof(SensorDataPoint)) + ")",true);
      storeError("loadSensorDataFromFile: File size is not a multiple of SensorDataPoint size",ERROR_SD_RETRIEVEDATAPARMS);
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
    
    uint32_t endOffset =  sizeof(SensorDataPoint);
    if (endOffset > fileSize) {
      SerialPrint("loadSensorDataFromFile: endOffset > fileSize, skipping",true);
      storeError("loadSensorDataFromFile: endOffset > fileSize, skipping",ERROR_SD_RETRIEVEDATAPARMS);
      file.close();
      return -1;
    }


    file.seek(fileSize); //go to the end of the file
    uint16_t arrindex = 0;
    while (file.position() >  endOffset && arrindex < pointsToRead) {
      
      file.seek(file.position()-endOffset); //rewind 1 record
      if (file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint)) != sizeof(SensorDataPoint)) {
        SerialPrint("loadSensorDataFromFile: Could not read data point",true);
        storeError("loadSensorDataFromFile: Could not read data point",ERROR_SD_FILEREAD);
        file.close();
        return -1;
      }

      file.seek(file.position()-endOffset); //rewind back to where we were


      if (isTimeValid(dataPoint.timeLogged)==false) continue;
      if (dataPoint.deviceMAC != deviceMAC || 
            dataPoint.snsType != sensorType || 
            dataPoint.snsID != sensorID) {
            continue;
        }
        
        // Check if within time range
        if (dataPoint.timeLogged > timeEnd) continue;
        if (dataPoint.timeLogged >= timeStart) {
            t[arrindex] = dataPoint.timeLogged;
            v[arrindex] = dataPoint.snsValue;
            f[arrindex] = dataPoint.Flags;
            arrindex++;
            if (arrindex >= pointsToRead) break; //we have read enough data
        } else break; //no more valid data       
      
    }

    file.close();

    return arrindex;
}

int16_t loadAverageSensorDataFromFile(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* averagedTimes, double* averagedValues, uint8_t averagedFlags[], uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize, uint16_t numPointsX) {

  if (windowSize == 0) {
    SerialPrint("loadAverageSensorDataFromFile: windowSize is 0",true);
    storeError("loadAverageSensorDataFromFile: windowSize is 0",ERROR_SD_RETRIEVEDATAPARMS);
    return -1;
  }

  if (timeEnd==0) timeEnd=-1;
  if (timeStart==0) timeStart=0;

  if (timeEnd==UINT32_MAX) timeEnd=I.currentTime; //UINT32_MAX is some huge number


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
    uint8_t avgFlag=0;
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

      if (isTimeValid(dataPoint.timeLogged)==false) continue;
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
        if (bitRead(dataPoint.Flags,0)) avgFlag=1;
        n++;
      } else {
        isgood = true; //we have reached the end of the window
        break; //no more valid data         in this window... without rewinding, so we check this datapoint again
      }

      file.seek(file.position()-endOffset); //rewind back another record, so next time we read the prior

    }

    if (n>0) {
      averagedValues[index] = avgVal/n;
      averagedFlags[index] = avgFlag;
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
    
    
    // Update the timestamp index file
    updateTimestampIndex(filename.c_str());
    
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
        SerialPrint("readDevicesSensorsSD: File size mismatch, deleting file: " + filename,true);
        f.close();
        deleteFiles("DevicesSensors.dat","/Data");
        return false;
    }

    // Read the entire Devices_Sensors object
    f.read((uint8_t*)&Sensors, sizeof(Devices_Sensors));
    f.close();
    return true;
}

bool writeErrorToSD(ERROR_STRUCT LASTERROR) {
  return writeAnythingToSD("/Data/DeviceErrors.txt", &LASTERROR, sizeof(ERROR_STRUCT), true);    
}

int8_t readErrorFromSD(ERROR_STRUCT& LASTERROR, uint8_t NthFromLast) {


  //open the file
  File file = SD.open("/Data/DeviceErrors.txt", FILE_READ);
  if (file==false) {
    storeError("readErrorFromSD: Could not open file",ERROR_SD_ERRORREAD);
    return false;
  }
  if (file.size() < sizeof(ERROR_STRUCT)) {
    file.close();
    storeError("readErrorFromSD: File is less than the size of an ERROR_STRUCT",ERROR_SD_ERRORFILESIZE);
    return false;
  }

  uint16_t nEntries = file.size()/sizeof(ERROR_STRUCT);

  if (file.size() != nEntries*sizeof(ERROR_STRUCT)) {
    //file is not a multiple of the size of an ERROR_STRUCT
    file.close();
    storeError("readErrorFromSD: File does not contain ERROR_STRUCTs",ERROR_SD_ERRORFILESIZE);
    deleteFiles("DeviceErrors.txt","/Data");
    return false;
  }

  //go to the end of the file
  file.seek(file.size());

  //rewind nthfromlast times
  for (int i = 0; i < NthFromLast; i++) {
    file.seek(file.position()-sizeof(ERROR_STRUCT));
  }

  //read the entry
  file.read((uint8_t*)&LASTERROR, sizeof(ERROR_STRUCT));
  file.close();

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
    I.isUpToDate = true;
    I.lastStoreCoreDataTime = I.currentTime;

    f.write((uint8_t*)&I,sizeof(STRUCT_CORE));

    f.close();
    
    
    // Update the timestamp index file
    updateTimestampIndex(filename.c_str());
    
    return true;
    
}

bool readScreenInfoSD() //read last screenInfo
{
    

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

        deleteCoreStruct();

        return false;
    }

    while (f.available()) {
        f.read((uint8_t*)&I,sizeof(STRUCT_CORE));
    }

    f.close();

    return true;

}

// New functions for individual sensor data storage using Devices_Sensors class
//wrapper, if device infor was provided, use it to find the sensor index
bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID) {
  
  int16_t sensorIndex = Sensors.findSensor(deviceMAC, sensorType, sensorID);
  if (sensorIndex < 0) {
      storeError("storeSensorDataSD: Sensor not found", ERROR_SD_DEVICESENSORSNOSNS);
      return false;
  }
  
  return storeSensorDataSD(sensorIndex);
}


bool storeSensorDataSD(int16_t sensorIndex) {
  uint8_t invalidIndex = Sensors.isSensorIndexInvalid(sensorIndex);
  if (invalidIndex != 0) {
    if (invalidIndex == 1) {
      SerialPrint((String) "storeSensorDataSD: sensor Index out of bounds: " + (String) sensorIndex + "\n");
      storeError("storeSensorDataSD: sensorIndex out of bounds", ERROR_SD_RETRIEVEDATAPARMS);
    } else if (invalidIndex == 2) {
      SerialPrint((String) "storeSensorDataSD: sensor Index not set: " + (String) sensorIndex + "\n");
      storeError("storeSensorDataSD: sensorIndex not set", ERROR_SD_RETRIEVEDATAPARMS);     
    } else if (invalidIndex == 3) {
      SerialPrint((String) "storeSensorDataSD: sensor Index expired: " + (String) sensorIndex + "\n");
      storeError("storeSensorDataSD: sensorIndex expired", ERROR_SD_RETRIEVEDATAPARMS);
    }
    return false;
  }

  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);
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
    if (Sensors.setWriteTimestamp(sensorIndex)==false) {
      SerialPrint((String) "storeSensorDataSD: setWriteTimestamp failed for sensor index " + (String) sensorIndex + "\n");
      storeError("storeSensorDataSD: setWriteTimestamp failed", ERROR_SD_DEVICESENSORSNOSNS);
      file.close();
      return false;
    }

    dataPoint.deviceMAC = Sensors.getDeviceMACBySnsIndex(sensorIndex);
    dataPoint.deviceIndex = sensor->deviceIndex;
    dataPoint.snsType = sensor->snsType;
    dataPoint.snsID = sensor->snsID;
    dataPoint.snsValue = sensor->snsValue;
    dataPoint.timeRead = sensor->timeRead;
    dataPoint.timeLogged = sensor->timeLogged;
    dataPoint.Flags = sensor->Flags;
    dataPoint.SendingInt = sensor->SendingInt;
    dataPoint.fileWriteTimestamp = sensor->timeWritten;  // Set when data is written to file
    strncpy(dataPoint.snsName, sensor->snsName, sizeof(dataPoint.snsName));
    dataPoint.snsName[sizeof(dataPoint.snsName) - 1] = '\0'; // Ensure null termination
    
    // Write the complete struct
    file.write((uint8_t*)&dataPoint, sizeof(SensorDataPoint));
    
    file.close();
    
    
    // Update the timestamp index file
    updateTimestampIndex(filename.c_str());
    
    //SerialPrint((String) "StoreSensorDataSD: Stored sensor data from " + (String) sensor->snsName + " with sensor index " + (String) sensorIndex + " and associated device MAC " + (String) Sensors.getDeviceMACBySnsIndex(sensorIndex) + " to " +  filename + " with time " + (String) sensor->timeLogged,true);
    return true;
}



bool retrieveSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, byte *N, uint32_t t[], double v[], uint8_t f[], uint32_t timeStart, uint32_t timeEnd, bool forwardOrder) {
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

  int16_t n = loadSensorDataFromFile(deviceMAC, sensorType, sensorID, t, v, f, timeStart, timeEnd, *N);

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
    uint8_t f0[n] = {0};

    int16_t i=0;
    for (int16_t j=n-1; j>=0;j--) {
      t0[i] = t[j];
      v0[i] = v[j];
      f0[i] = f[j];
      i++;
    }

    memcpy(t, t0, n * sizeof(uint32_t));
    memcpy(v, v0, n * sizeof(double));
    memcpy(f, f0, n * sizeof(uint8_t));
    // No free needed for stack arrays
  } 
  
  *N = n;
  return true;
} 

bool retrieveMovingAverageSensorDataFromSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID,
                                         uint32_t timeStart, uint32_t timeEnd,
                                         uint32_t windowSize, uint16_t* numPointsX, double* averagedValues, uint32_t* averagedTimes, uint8_t* averagedFlags, bool forwardOrder) {
    
      // Validate inputs
    if (windowSize == 0 || averagedValues == nullptr || averagedTimes == nullptr || timeEnd <= timeStart) {
      SerialPrint("retrieveMovingAverageSensorDataFromSD: Invalid parameters", true);
      storeError("retrieveMovingAverageSensorDataFromSD: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
      return false;
    }

    int16_t n = loadAverageSensorDataFromFile(deviceMAC, sensorType, sensorID, averagedTimes, averagedValues, averagedFlags, timeStart, timeEnd, windowSize, *numPointsX);

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
      uint8_t f0[n] = {0};
  
      int16_t i=0;
      for (int16_t j=n-1; j>=0;j--) {
        t0[i] = averagedTimes[j];
        v0[i] = averagedValues[j];
        f0[i] = averagedFlags[j];
        i++;
      }
  
      memcpy(averagedTimes, t0, n * sizeof(uint32_t));
      memcpy(averagedValues, v0, n * sizeof(double));
      memcpy(averagedFlags, f0, n * sizeof(uint8_t));
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

uint16_t deleteFiles(const char* pattern,const char* directory) {
  //deletes files in the specified directory that match the pattern
  //returns number of files deleted
  File dir = SD.open(directory); // Open the target directory

    String filename ;
    String tempdir = (String) directory;

  //remove trailing slash from directory if it exists
  if (tempdir.endsWith("/")) {
    tempdir.remove(tempdir.length() - 1);
  }

  if (!dir) {
    storeError("deleteFiles: Could not open dir", ERROR_SD_OPENDIR);

    SerialPrint("Error opening root directory.");
    return 0;
  }

  
  uint16_t nDeleted = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }

    filename  = entry.name();
    
    if (matchPattern(filename.c_str(),  pattern)) {
        SerialPrint("Deleting: " + filename + "\n");
        filename = tempdir + "/" + filename;
      if (!SD.remove(filename)) {
        SerialPrint("Error deleting: " + filename + "\n");
        storeError(((String) "Error deleting: " + filename).c_str(), ERROR_SD_FILEDEL);
      } else {
        // Remove the deleted file from the timestamp index
        removeFromTimestampIndex(filename.c_str());
        nDeleted++;
      }
    } 
    entry.close();
  }
  dir.close();
  if (nDeleted == 0) {
    SerialPrint("deleteFiles: No files deleted\n");
  }
  return nDeleted;
}


uint16_t deleteSensorDataSD() {
    // returns sensor data files
    uint16_t deletedCount = 0;
      
    File root = SD.open("/Data");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        String filename = file.name();
        if (filename.endsWith(".dat") && filename != "ScreenFlags.dat" && filename != "SensorBackupv2.dat" && filename != "GsheetInfo.dat" && filename != "FileTimestamps.dat" && filename != "WeatherData.dat" && filename != "DevicesSensors.dat") {
          if (SD.remove("/Data/" + filename)) {
            removeFromTimestampIndex(filename.c_str());
            deletedCount++;
          }
        }
        file = root.openNextFile();
      }
      root.close();
    }
    return deletedCount;
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


bool writeAnythingToSD(const char* filename, const void* data, uint16_t size, bool append) {
  File file = SD.open(filename, append ? FILE_APPEND : FILE_WRITE);
  if (!file) {
    storeError("writeAnythingToSD: Could not open file",ERROR_SD_FILEWRITE);
    return false;
  }
  file.write((uint8_t*)data, size);
  file.close();
  return true;
}

bool readAnythingFromSD(const char* filename, const void* data, uint16_t size) {
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    storeError("readAnythingFromSD: Could not open file",ERROR_SD_FILEREAD);
    return false;
  }
  file.read((uint8_t*)data, size);
  file.close();
  return true;
}

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
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
    
    
    // Update the timestamp index file
    updateTimestampIndex(filename.c_str());
    
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
        SerialPrint("readGsheetInfoSD: File size mismatch, deleting file: " + filename,true);
        deleteFiles("GsheetInfo.dat", "/Data");
        f.close();
        return false;
    }
    f.read((uint8_t*)&GSheetInfo, sizeof(STRUCT_GOOGLESHEET));
    f.close();
    return true;
}
#endif

#endif //_USESDCARD