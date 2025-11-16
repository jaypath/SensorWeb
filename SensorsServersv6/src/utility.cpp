#include "globals.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"

#ifdef _USETFT
extern LGFX tft;
extern const uint16_t FG_COLOR ; //Foreground color
extern const uint16_t BG_COLOR;
#endif

#ifdef _ISPERIPHERAL
extern STRUCT_SNSHISTORY SensorHistory;
#endif

extern int16_t MY_DEVICE_INDEX;
//flags for sensors:
////  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)


//setup functions
void initI2C() {
  #ifdef _USEI2C
  Wire.begin();
  Wire.setClock(400000L);
  SerialPrint("Wire initialized",true);
  #endif
}

bool initSystem() {


  #ifdef _USESERIAL
    
  Serial.begin(_USESERIAL);
  Serial.println("Serial started");
  SerialPrint("SerialPrint started",true);
  #endif


  #ifdef _USESPI
  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
  #endif

  #ifdef _USESSD1306
  oled.begin();
  oled.setFont(ArialMT_Plain_10);
  #endif

    #ifdef _USETFT
    tft.init();
    tft.setRotation(2);
    tft.setCursor(0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    tft.printf("Running setup\n");
    #endif


    BootSecure bootSecure;
    int8_t boot_status = bootSecure.setup();
    if (boot_status <= 0) {
        #ifdef SETSECURE
            // Security check failed for any reason, halt device
            while (1) { delay(1000); }
        #endif
        #ifdef _USETFT
        tftPrint("Prefs failed to load with error code: " + String(boot_status), true, TFT_RED, 2, 1, false, -1, -1);
        #endif
        SerialPrint("Prefs failed to load with error code: " + String(boot_status), true);
        SerialPrint("Will redefine Prefs struct later...", true);
    } else SerialPrint("Prefs loaded successfully, my name is: " + String(Prefs.DEVICENAME),true);



    //register this device in devices and sensors. While I may already be registered due to loading from SD card, I may not be if no SD card and I may need to update my IP address!
    if (Prefs.DEVICENAME[0] == 0) {
      //name the device sensor-MAC where MAC is in hex without spacers
      #ifdef MYNAME
      strncpy(Prefs.DEVICENAME, MYNAME, sizeof(Prefs.DEVICENAME) - 1);
      Prefs.DEVICENAME[sizeof(Prefs.DEVICENAME) - 1] = '\0';
      #else
      //name the device server-MAC where MAC is in hex without spacers
      strncpy(Prefs.DEVICENAME, ("Dev" + String(ESP.getEfuseMac(), HEX)).c_str(), sizeof(Prefs.DEVICENAME) - 1);
      Prefs.DEVICENAME[sizeof(Prefs.DEVICENAME) - 1] = '\0';
      #endif
      Prefs.isUpToDate = false;
    }


    tftPrint("Init Wifi... \n", true);
    SerialPrint("Init Wifi... ",false);
    setupServerRoutes();

    if (Prefs.HAVECREDENTIALS) {

      if (connectWiFi()<0) {
          //if connectWiFi returned -10000, then we are in AP mode and handled elsewhere
          SerialPrint("Failed to connect to Wifi",true);
          if (connectWiFi()>-10000 && connectWiFi()<0) {

              tftPrint("Wifi failed too many times,\npossibly due to incorrect credentials.\nEnering into local mode... ", true, TFT_RED, 2, 1, true, 0, 0);  
              SerialPrint("Wifi failed too many times,\npossibly due to incorrect credentials.\nEntering local mode... login to my local wifi and go to http://192.168.4.1 to complete setup.", true);
              delay(10000);  
              Prefs.HAVECREDENTIALS = false;

              APStation_Mode();
          }        
      } 
  } else {
    SerialPrint("No credentials, starting AP Station Mode",true);
    APStation_Mode();
  }


  #ifdef _USETFT
  displaySetupProgress( true);
  #endif
  SerialPrint("Wifi OK. Current IP Address: " + WiFi.localIP().toString(),true);


  byte devIndex = Sensors.addDevice(ESP.getEfuseMac(), WiFi.localIP(), Prefs.DEVICENAME, 0, 0, _MYTYPE);
  if (devIndex == -1) {
    failedToRegister();
    return false;
  }


  while(WifiStatus()==false) {
    SerialPrint("Waiting for WiFi to be ready...", true);
    delay(500);
  }

  tftPrint("Init server... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  server.begin();
  tftPrint(" OK.", true, TFT_GREEN);



  tftPrint("Initializing ESPNow... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  int8_t errorCode = initESPNOW();
  if (errorCode == 1) {
      tftPrint("OK.", true, TFT_GREEN);
      if (_MYTYPE >= 100) {
        broadcastServerPresence(false);
      } 
      

  } else {
      tftPrint("FAILED with code " + String(errorCode) + ".", true, TFT_RED);
      storeError("ESPNow init error: " + String(errorCode));
  }

  return true;

}

bool isI2CDeviceReady(byte address) {
  #ifdef _USEI2C
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
  #endif
  return false;
}

int8_t initSDCard() {
  //returns 1 if successful, 0 if not successful, -1 if not supported
  #ifdef _USESDCARD
  #ifdef _USETFT
  tftPrint("SD Card mounting...", false, TFT_WHITE, 2, 1, false, -1, -1);
  #endif
  if (!SD.begin(41)) {
      displaySetupProgress(false);
      
      SerialPrint("SD mount failed... ",true);
      
      delay(5000);
      I.resetInfo = RESET_SD;
      I.lastResetTime = I.currentTime;
      controlledReboot("SD Card failed", RESET_SD, true);
      return 0;
  }

  SerialPrint("SD mounted... ",true);
  displaySetupProgress(true);

  return 1;
  #endif
  return -1;
}


bool loadScreenFlags() {
  #ifdef _USESDCARD
  #ifdef _USETFT
  tftPrint("Loading core data from SD... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  #endif
  bool sdread = readScreenInfoSD();
  displaySetupProgress( sdread);
  initScreenFlags();
  return sdread;
  #endif
  return false;
}

bool loadSensorData() {
  #ifdef _USESDCARD
  #ifdef _USETFT
  tftPrint("Loading sensor data from SD... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  #endif
  bool sdread = Sensors.readDevicesSensorsArrayFromSD();
  displaySetupProgress( sdread);
  SerialPrint("Sensor data loaded? ",false);
  SerialPrint((sdread==true)?"yes":"no",true);
  return sdread;
  #endif
  return false;
}


bool isTimeValid(uint32_t time) {
  if (time < TIMEZERO) return false;
  if (time > I.currentTime) return false;
  return true;
}

bool isTempValid(double temp, bool extremeTemp) {
  if (extremeTemp) {
    if (temp < -10 || temp > 650) return false;
  } else {
    if (temp < -10 || temp > 125) return false;
  }
  return true;
}

bool isRHValid(double rh) {
  if (rh < 0 || rh > 100) return false;
  return true;
}

bool isSoilCapacitanceValid(double soil) {
  if (soil < 0 || soil > 200) return false;
  return true;
}

bool isSoilResistanceValid(double soil) {
  if (soil < 0 || soil > 50000) return false;
  return true;
}

bool isPressureValid(double pressure) {
  if (pressure < 890 || pressure > 1070) return false;
  return true;
}

#ifdef _ISPERIPHERAL
bool retrieveSensorDataFromMemory(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID, byte* N, uint32_t* t, double* v, uint8_t* f, uint32_t timeStart, uint32_t timeEnd, bool forwardOrder) {
  //retrieve up to N most recent data points ending at timeEnd. If fewer than N data points are found, return the number of data points found.

  // Validate inputs
  if (N == nullptr || t == nullptr || v == nullptr) {
    SerialPrint("retrieveSensorDataFromMemory: N, t, or v were nullptr.\n");
    storeError("retrieveSensorDataFromMemory: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
    return false;
  }
  if (timeEnd <= timeStart || *N == 0) {
    storeError("retrieveSensorDataFromMemory: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
    return false;
  }

  //find the index of the sensor in the SensorHistory array
  //use the in memory array of a peripheral device to retrieve the data. The in memory array will be a struct array of historical data: SensorHistory[m].timestamp[n] and SensorHistory[m].value[n] where m is the index of the sensor and n is the index of the data point.

  int16_t m = Sensors.findSensor(deviceMAC, snsType, snsID);
  if (m < 0) {
    SerialPrint("retrieveSensorDataFromMemory: Sensor not found.\n");
    storeError("retrieveSensorDataFromMemory: Sensor not found.", ERROR_SD_RETRIEVEDATAPARMS);
    return false;
  }

  bool found = false;
  byte n=0; //index of the sensor in the SensorHistory array
  for (n=0; n<_SENSORNUM; n++) {
    if (SensorHistory.sensorIndex[n] == m) {
      found = true;
      break;
    }
  }

  if (!found) {
    SerialPrint("retrieveSensorDataFromMemory: Sensor not found in SensorHistory array.\n");
    storeError("retrieveSensorDataFromMemory: Sensor not found in SensorHistory array.", ERROR_SD_RETRIEVEDATAPARMS);
    return false;
  }

  //how many valid data points are there? //cycle through the history timestamps and count the number of valid data points
  uint16_t validDataPoints = 0;
  for (uint16_t i=0; i<_SENSORHISTORYSIZE; i++) {
    if (isTimeValid(SensorHistory.TimeStamps[n][i]))       validDataPoints++;
    
  }

  if (validDataPoints < *N) {
    *N = validDataPoints;
  } 
  
  if (*N==0) {
    SerialPrint("retrieveSensorDataFromMemory: No errors, but no data was returned.\n");
    storeError("retrieveSensorDataFromMemory: No errors, but no data was returned.", ERROR_SD_RETRIEVEDATAMISSING);
    return true; // No data found, but not an error
  }
  
  int16_t i=0;

  if (forwardOrder) {    //pull data in order of oldest to newest

    //using SensorHistory.HistoryIndex[n], which is the index of the last data point saved, rewind N points and pull them in order
    int16_t index_start = SensorHistory.HistoryIndex[n]-*N; //this might be a negative number, so we need to wrap around
    if (index_start < 0) index_start = _SENSORHISTORYSIZE + index_start;
    uint16_t index = index_start;
    while (cycleIndex(&index, _SENSORHISTORYSIZE, SensorHistory.HistoryIndex[n])) {
      if (isTimeValid(SensorHistory.TimeStamps[n][index])) {
        t[i] = SensorHistory.TimeStamps[n][index];
        v[i] = SensorHistory.Values[n][index];
        f[i] = SensorHistory.Flags[n][index];
        i++;
      }
    }
  } else {
    //pull data in order of newest to oldest
    uint16_t index = SensorHistory.HistoryIndex[n];
    while (cycleIndex(&index, _SENSORHISTORYSIZE, SensorHistory.HistoryIndex[n],true) && i<*N) { //true means cycle backwards
      if (isTimeValid(SensorHistory.TimeStamps[n][index])) {
        t[i] = SensorHistory.TimeStamps[n][index];
        v[i] = SensorHistory.Values[n][index];
        f[i] = SensorHistory.Flags[n][index];
        i++;
      }
    }
  }

  return true;

}

int16_t loadAverageSensorDataFromMemory(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* averagedTimes, double* averagedValues, uint8_t averagedFlags[], uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize, uint16_t numPointsX) {

  if (windowSize == 0) {
    SerialPrint("loadAverageSensorDataFromMemory: windowSize is 0",true);
    storeError("loadAverageSensorDataFromMemory: windowSize is 0",ERROR_SD_RETRIEVEDATAPARMS);
    return -1;
  }

  if (timeEnd==0) timeEnd=-1;
  if (timeStart==0) timeStart=0;

  if (timeEnd==UINT32_MAX) timeEnd=I.currentTime; //UINT32_MAX is some huge number


  if (timeStart>=timeEnd) {
    SerialPrint("loadAverageSensorDataFromMemory: timeStart >= timeEnd",true);
    storeError("loadAverageSensorDataFromMemory: timeStart >= timeEnd",ERROR_SD_RETRIEVEDATAPARMS);
    return -1;
  }

  int16_t sensorIndex = Sensors.findSensor(deviceMAC, sensorType, sensorID);
  if (sensorIndex < 0) {
    SerialPrint("loadAverageSensorDataFromMemory: Sensor not found.",true);
    storeError("loadAverageSensorDataFromMemory: Sensor not found.", ERROR_SD_RETRIEVEDATAPARMS);
    return -1;
  }

  int8_t sensorHistoryIndex = Sensors.getPrefsIndex(sensorType, sensorID, -1);

    //how many valid data points are there? //cycle through the history timestamps and count the number of valid data points
    uint16_t validDataPoints = 0;    
    for (uint16_t i=0; i<_SENSORHISTORYSIZE; i++) {
      if (isTimeValid(SensorHistory.TimeStamps[sensorHistoryIndex][i]))       validDataPoints++;
      
    }
  
  //check if the file is empty
  if (validDataPoints == 0) {
    SerialPrint("loadAverageSensorDataFromMemory: No data points found.",true);
    return 0;
  }

  uint32_t i;
  uint32_t j;

  uint32_t numWindows = (timeEnd-timeStart)/windowSize + 1;
  if (numWindows < numPointsX) numWindows = numPointsX; //typically numpointsX is not provided, ie numPointsX = 0, but if it is, we need to limit the number of windows

  if (numWindows>100) numWindows=100;
  if (numWindows==0) numWindows=1;

  //calculate the window start and end times
  uint32_t windowStart;
  
  if (timeStart + windowSize < timeEnd) windowStart = timeEnd - windowSize;
  else windowStart = timeStart;

  uint32_t windowEnd = timeEnd;

  
  i=0;
  int16_t index=0;
  while (i<numWindows) {
    //SerialPrint("loadAverageSensorDataFromFile: window " + (String) i + " of " + (String) numWindows + " from " + filename + " with windowStart = " + (String) windowStart + " and windowEnd = " + (String) windowEnd,true);
    uint16_t numPointsInWindow=0;
    double avgVal=0;
    uint8_t avgFlag=0;
    bool isgood = true;
    
    //load data from memory if it is within the window size time range
    for (j = 0; j < _SENSORHISTORYSIZE; j++) {
      if (isTimeValid(SensorHistory.TimeStamps[sensorHistoryIndex][j])) {
        if (SensorHistory.TimeStamps[sensorHistoryIndex][j] >= windowEnd) continue;
        if (SensorHistory.TimeStamps[sensorHistoryIndex][j] < windowStart) continue;
        avgVal+= SensorHistory.Values[sensorHistoryIndex][j];
        if (bitRead(SensorHistory.Flags[sensorHistoryIndex][j],0)==1) avgFlag=1;
        numPointsInWindow++;
      }
    }

    if (numPointsInWindow>0) {
      averagedValues[index] = avgVal/numPointsInWindow;
      averagedFlags[index] = avgFlag;
      averagedTimes[index++] = windowStart + (windowEnd-windowStart)/2;
      //SerialPrint("loadAverageSensorDataFromMemory: window " + (String) i + " of " + (String) numWindows + " from " + filename + " with windowStart = " + (String) windowStart + " and windowEnd = " + (String) windowEnd + " with avgVal = " + (String) avgVal + " and avgTime = " + (String) averagedTimes[i],true);
    }

    if (windowEnd > windowSize && windowStart > windowSize) {
      windowEnd -= windowSize;
      windowStart -= windowSize;
    }     else break; //no more valid data

    i++;
  }
  
  //returns the number of datapoints found
   return index; //the last window is not complete, so we return the previous one
}

bool retrieveMovingAverageSensorDataFromMemory(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize, uint16_t* numPointsX, double* averagedValues, uint32_t* averagedTimes, uint8_t* averagedFlags, bool forwardOrder) {
        // Validate inputs
        if (windowSize == 0 || averagedValues == nullptr || averagedTimes == nullptr || timeEnd <= timeStart) {
          SerialPrint("retrieveMovingAverageSensorDataFromMemory: Invalid parameters", true);
          storeError("retrieveMovingAverageSensorDataFromMemory: Invalid parameters", ERROR_SD_RETRIEVEDATAPARMS);
          return false;
        }
    
        int16_t numPoints = loadAverageSensorDataFromMemory(deviceMAC, sensorType, sensorID, averagedTimes, averagedValues, averagedFlags, timeStart, timeEnd, windowSize, *numPointsX);
    
        if (numPoints<0) {
          SerialPrint("retrieveMovingAverageSensorDataFromMemory: could not read", true);
          storeError("retrieveMovingAverageSensorDataFromMemory: could not read",  ERROR_SD_RETRIEVEDATAMISSING);
          return false;
        }
    
        if (numPoints==0) {
          SerialPrint("retrieveMovingAverageSensorDataFromMemory: No data points in time range", true);
          storeError("retrieveMovingAverageSensorDataFromMemory: No data points in time range",  ERROR_SD_RETRIEVEDATAMISSING);
          return false;
        }
    
        if (forwardOrder) {
          // reverse the orders of t and v, which were stored in reverse order
          //note that only the FIRST n points were actually entered
      
          uint32_t t0[numPoints] = {0};
          double v0[numPoints] = {0};
          uint8_t f0[numPoints] = {0};
      
          int16_t i=0;
          for (int16_t j=numPoints-1; j>=0;j--) {
            t0[i] = averagedTimes[j];
            v0[i] = averagedValues[j];
            f0[i] = averagedFlags[j];
            i++;
          }
      
          memcpy(averagedTimes, t0, numPoints * sizeof(uint32_t));
          memcpy(averagedValues, v0, numPoints * sizeof(double));
          memcpy(averagedFlags, f0, numPoints * sizeof(uint8_t));
        } 
        
        *numPointsX = numPoints;
      
    
        return true;
    
}
#endif

/**
 * @brief Initialize the Gsheet uploader 
 */
void initGsheetHandler() {
  #ifdef _USEGSHEET
  if (!GSheetInfo.useGsheet) return;
    SerialPrint("gsheet setup1: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);
    SerialPrint("gsheet setup1: file ID is " + (strlen(GSheetInfo.GsheetID) > 0 ? String(GSheetInfo.GsheetID) : "N/A"),true);
    
    if (!readGsheetInfoSD() || GSheetInfo.clientEmail == NULL || GSheetInfo.projectID == NULL || GSheetInfo.privateKey == NULL) {
        initGsheetInfo();
        storeGsheetInfoSD();        
    }
    initGsheet(); 
    if (I.currentTime > 1000 && GSheet.ready()) { //wait for time to be set and gsheet to be ready
        String newGsheetName = "ArduinoLog" + (String) dateify(I.currentTime,"yyyy-mm");
        strncpy(GSheetInfo.GsheetName, newGsheetName.c_str(), 23);
        GSheetInfo.GsheetName[23] = '\0'; // Ensure null termination
        snprintf(GSheetInfo.GsheetID,64,"%s",file_findSpreadsheetIDByName(GSheetInfo.GsheetName).c_str());
    } else {
        SerialPrint("gsheet setup: Gsheet not ready, waiting for time to be set and gsheet to be ready",true);
    }

    SerialPrint("gsheet setup2: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);
    SerialPrint("gsheet setup2: file ID is " + (strlen(GSheetInfo.GsheetID) > 0 ? String(GSheetInfo.GsheetID) : "N/A"),true);
  #endif
}


void handleESPNOWPeriodicBroadcast(uint8_t interval) {    
  #ifndef _ISPERIPHERAL
  if (I.makeBroadcast || (minute() % interval == 0)) {        
      // ESPNow does not require WiFi connection; always broadcast      
      broadcastServerPresence();
  }
  #endif
}

void handleStoreCoreData() {
  //use the handle version of storeCoreData for regular timed storage. use storeCoreData directly for forced manual storage.
  storeCoreData(false);


  #ifdef _USESDCARD
  //store device data
  Sensors.storeDevicesSensorsArrayToSD(60);
  Sensors.storeAllSensorsSD(60); //store all sensors to SD card once an hour
  #endif
}

void initScreenFlags(bool completeInit) {
  if (completeInit) {
      I.rebootsSinceLast=0;
      I.wifiFailCount=0;
      I.currentTime=0;

      #ifdef _USETFT
      I.CLOCK_Y = 105;
      I.HEADER_Y = 30;
      
      I.isExpired = false; //are any critical sensors expired?
      I.wasFlagged=false;
      I.isHeat=false; //first bit is heat on, bits 1-6 are zones
      I.isAC=false; //first bit is compressor on, bits 1-6 are zones
      I.isFan=false; //first bit is fan on, bits 1-6 are zones
      I.wasHeat=false; //first bit is heat on, bits 1-6 are zones
      I.wasAC=false; //first bit is compressor on, bits 1-6 are zones
      I.wasFan=false; //first bit is fan on, bits 1-6 are zones
      I.localBatteryIndex=255;
      I.showTheseFlags=(1<<3) + (1<<2) + (1<<1) + 1; //bit 0 = 1 for flagged only, bit 1 = 1 include expired, bit 2 = 1 include soil alarms, bit 3 =1 include leak, bit 4 =1 include temperature, bit 5 =1 include  RH, bit 6=1 include pressure, 7 = 1 include battery, 8 = 1 include HVAC

      I.isHot=0;
      I.isCold=0;
      I.isSoilDry=0;
      I.isLeak=0;
   
      I.cycleHeaderMinutes = 30; //how many seconds to show header?
      I.cycleCurrentConditionMinutes = 10; //how many minutes to show current condition?
      I.cycleWeatherMinutes = 10; //how many minutes to show weather values?
      I.cycleFutureConditionsMinutes = 10; //how many minutes to show future conditions?
      I.cycleFlagSeconds = 3; //how many seconds to show flag values?
      I.IntervalHourlyWeather = 2; //hours between daily weather display
      I.screenChangeTimer = 30; //how many seconds before screen changes back to main screen

      I.localWeatherIndex=255; //index of outside sensor

      I.currentTemp-127;
      I.Tmax=-127;
      I.Tmin=127;

      #endif

      I.lastErrorTime=0;
  }

  #ifdef _USETFT
  I.lastHeaderTime=0; //last time header was drawn
  I.lastClockDrawTime=0; //last time clock was updated, whether flag or not
  I.lastFlagViewTime=0; //last time clock was updated, whether flag or not
  I.ScreenNum = 0;
  I.oldScreenNum = 0;
  #ifdef _USEWEATHER
  I.lastWeatherTime=0; //last time weather was drawn
  I.lastCurrentConditionTime=0; //last time current condition was drawn
  I.lastFutureConditionTime=0; //last time future condition was drawn
  #endif
  #endif

  #ifdef _USESDCARD
  I.lastStoreCoreDataTime = 0;
  #endif

  #ifdef _USEBATTERY
  I.localBatteryLevel=0;
  #endif

  I.makeBroadcast = true;
  I.ESPNOW_SENDS = 0;
  I.ESPNOW_RECEIVES = 0;

  I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC=0;
  I.ESPNOW_LAST_INCOMINGMSG_TYPE=0;
  memset(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD,0,80);
  I.ESPNOW_LAST_INCOMINGMSG_TIME=0;
  I.ESPNOW_INCOMING_ERRORS = 0;


  I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC=0;
  I.ESPNOW_LAST_OUTGOINGMSG_TYPE=0;
  memset(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD,0,80);
  I.ESPNOW_LAST_OUTGOINGMSG_TIME=0;
  I.ESPNOW_OUTGOING_ERRORS = 0;


  I.UDP_LAST_INCOMINGMSG_TIME = 0;
  I.UDP_LAST_OUTGOINGMSG_TIME = 0;
  memset(I.UDP_LAST_INCOMINGMSG_TYPE,0,10); // message of last UDP status check - [Sensor, System, etc]
  memset(I.UDP_LAST_OUTGOINGMSG_TYPE,0,10); // message of last UDP status check - [Sensor, System, etc]
  I.UDP_LAST_INCOMINGMSG_FROM_IP = IPAddress(0,0,0,0);
  I.UDP_LAST_OUTGOINGMSG_TO_IP = IPAddress(0,0,0,0);
  I.UDP_INCOMING_ERRORS = 0;
  I.UDP_OUTGOING_ERRORS = 0;

  I.HTTP_LAST_INCOMINGMSG_TIME = 0;
  I.HTTP_LAST_OUTGOINGMSG_TIME = 0;
  memset(I.HTTP_LAST_INCOMINGMSG_TYPE,0,10); // message of last HTTP status check - [Sensor, System, etc]
  memset(I.HTTP_LAST_OUTGOINGMSG_TYPE,0,10); // message of last HTTP status check - [Sensor, System, etc]
  I.HTTP_LAST_INCOMINGMSG_FROM_IP = IPAddress(0,0,0,0);
  I.HTTP_LAST_OUTGOINGMSG_TO_IP = IPAddress(0,0,0,0);
  I.HTTP_INCOMING_ERRORS = 0;
  I.HTTP_OUTGOING_ERRORS = 0;

  I.lastResetTime=I.currentTime;
  I.ALIVESINCE=I.currentTime;
  I.wifiFailCount=0;
  I.isFlagged = false;

  I.isUpToDate = false;

  #ifdef _USESDCARD
  if (completeInit) {
      storeScreenInfoSD();
  }
  #endif
}


bool SerialPrint(String S,bool newline) {
  return SerialPrint(S.c_str(),newline);
}

bool SerialPrint(const char* S,bool newline) {
  bool printed =false;
  
  #ifdef _USESERIAL
    Serial.printf("%s",S);
    if (newline) Serial.println();
    printed=true;
  #endif

  return printed;


}

int inArray(int arr[], int N, int value,bool returncount) {
  //returns index to the integer array of length N holding value, or -1 if not found
  //if returncount == true then returns number of occurrences

byte cnt=0;

for (int i = 0; i < N-1 ; i++)   {
  if (arr[i]==value) {
    if (returncount) cnt++;
    else return i;
  }
}

return -1;

}

int inArrayBytes(byte arr[], int N, byte value,bool returncount) {
  //returns index to the integer array of length N holding value, or -1 if not found
  //if returncount == true then returns number of occurrences

byte cnt=0;

for (int i = 0; i < N-1 ; i++)   {
  if (arr[i]==value) {
    if (returncount) cnt++;
    else return i;
  }
}

return -1;

}


void pushDoubleArray(double arr[], byte N, double value) { //array variable, size of array, value to push
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;

}

bool inIndex(byte lookfor,byte used[],byte arraysize) {
  for (byte i=0;i<arraysize;i++) {
    if (used[i] == lookfor) return true;
  }

  return false;
}

void Byte2Bin(uint8_t value, char* output, bool invert) {
  snprintf(output,9,"00000000");
  for (byte i = 0; i < 8; i++) {
    if (invert) {
      if (value & (1 << i)) output[i] = '1';
      else output[i] = '0';
    } else {
      if (value & (1 << i)) output[7-i] = '1';
      else output[7-i] = '0';
    }
  }

  return;
}


char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}

bool uint64ToString(uint64_t val, char* str, bool strHex) {
  if (strHex) {
    snprintf(str, 16, "%012llX", val);
  } else {
    snprintf(str, 16, "%012llu", val);
  }
  return true;
}

bool stringToUInt64(String s, uint64_t* val, bool isHex) {
  char* e;
  uint64_t myint = 0;
  if (isHex) {
    myint = strtoull(s.c_str(), &e, 16);    
  } else {
    myint = strtoull(s.c_str(), &e, 0);
  }

  if (e != s.c_str()) { // check if strtoull actually found some digits
    *val = myint;
    return true;
  } else {
    return false;
  }


}

bool stringToLong(String s, uint32_t* val) {
 
  char* e;
  uint32_t myint = strtol(s.c_str(), &e, 0);

  if (e != s.c_str()) { // check if strtol actually found some digits
    *val = myint;
    return true;
  } else {
    return false;
  }
  
}

int16_t cumsum(int16_t * arr, int16_t ind1, int16_t ind2) {
  int16_t output = 0;
  for (int x=ind1;x<=ind2;x++) {
    output+=arr[x];
  }
  return output;
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}



// Legacy compatibility functions that delegate to the Devices_Sensors class

void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow){
  Sensors.find_limit_sensortypes(snsname, snsType, snsIndexHigh, snsIndexLow);
}

uint8_t find_sensor_count(String snsname,uint8_t snsType) {
  return Sensors.find_sensor_count(snsname, snsType);
}

uint8_t findSensorByName(String snsname,uint8_t snsType,uint8_t snsID) {
  return Sensors.findSensorByName(snsname, snsType, snsID);
}

uint8_t findSensorByName(String snsname,uint8_t snsType) {
  return Sensors.findSensorByName(snsname, snsType, 0);
}

//sensor fcns
int16_t findOldestDev() {
  //return 0 entry or oldest expired noncritical (but never critical sensors)
  int oldestInd = 0;
  int  i=0;

  for (i=0;i<Sensors.getNumSensors();i++) {
    //find a zero slot
    if (!Sensors.isSensorInit(i)) return i;

    //find an expired noncritical slot
    SnsType* sensor = Sensors.getSensorBySnsIndex(i);
    if (sensor && sensor->expired == 1 && !bitRead(sensor->Flags,7)) {
      return i;
    }
  }

  return oldestInd;
}

void initSensor(int k) {
  Sensors.initSensor(k);
}


bool isSensorInit(int i) {
  return Sensors.isSensorInit(i);
}


uint8_t countDev() {
  return Sensors.getNumDevices();
}

int16_t findDev(byte* macID, byte ardID, byte snsType, byte snsID,  bool oldest) {
  // Legacy function - convert to new format
  uint64_t mac=0;
  memcpy(&mac, macID, 6);
  return Sensors.findDevice(mac);
}

int16_t findSnsOfType(byte snstype, bool newest) {
  return Sensors.findSnsOfType(snstype, newest);
}

uint8_t countFlagged(int snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan) {
  return Sensors.countFlagged(snsType, flagsthatmatter, flagsettings, MoreRecentThan);
}

#ifndef _ISPERIPHERAL
void checkHeat() {
  // Check if any HVAC sensors are active
  I.isHeat = 0;
  I.isAC = 0;
  I.isFan = 0;
  
  // Iterate through all devices and sensors with bounds checking

  if (Sensors.getNumDevices() == 0) return;
  if (Sensors.getNumSensors() == 0) return;
  int16_t snsindex = Sensors.findSnsOfType("HVAC", false, -1);
  if (snsindex == -1) return;
  while (snsindex != -1) {
    SnsType* sensor = Sensors.getSensorBySnsIndex(snsindex);
    if (!sensor || !sensor->IsSet) continue;
    if (sensor->snsValue > 0) {
      switch (sensor->snsType) {
        case 50: // Heat
          I.isHeat = 1;
          break;
        case 51: // AC
          I.isAC = 1;
          break;
        case 52: // Fan
          I.isFan = 1;
          break;
      }
    }
    snsindex = Sensors.findSnsOfType("HVAC", false, snsindex+1);
  }
  return;
}
#endif

String breakString(String *inputstr,String token,bool reduceOriginal) 
//take a pointer to input string and break it to the token.
//if reduceOriginal=true, then will decimate string - otherwise will leave original string as is

{
  String output = "";
  int pos = inputstr->indexOf(token);
  if (pos>-1) {
    output = inputstr->substring(0,pos);
    if (reduceOriginal) {
      *inputstr = inputstr->substring(pos+token.length());
    }
  } else {
    output = *inputstr;
    if (reduceOriginal) {
      *inputstr = "";
    }
  }
  return output;
}


uint16_t countSubstr(String orig, String token) {
  uint16_t count = 0;
  int pos = 0;
  while ((pos = orig.indexOf(token, pos)) != -1) {
    count++;
    pos += token.length();
  }
  return count;
}

String enumErrorToName(ERRORCODES E) {
  switch (E) {
    case ERROR_HTTPFAIL_BOX: return "HTTP failed for NOAA grid location";
    case ERROR_HTTPFAIL_DAILY: return "HTTP failed for NOAA daily weather";
    case ERROR_HTTPFAIL_GRID: return "HTTP failed for NOAA weather grid";
    case ERROR_HTTPFAIL_HOURLY: return "HTTP failed for NOAA hourly weather";
    case ERROR_HTTPFAIL_SUNRISE: return "HTTP failed for sunrise.io";
    case ERROR_JSON_BOX: return "Json parsing failed for NOAA grid location";
    case ERROR_JSON_DAILY: return "Json parsing failed for NOAA daily weather";
    case ERROR_JSON_GRID: return "Json parsing failed for NOAA weather grid";
    case ERROR_JSON_HOURLY: return "Json parsing failed for NOAA hourly weather";
    case ERROR_JSON_SUNRISE: return "Json parsing failed for sunrise.io";
    case ERROR_SD_LOGOPEN: return "Could not open log on SD";
    

    default: return "Error code is indeterminate";
  }

}

void storeError(String E, ERRORCODES CODE, bool writeToSD) {
  storeError(E.c_str(), CODE, writeToSD);
}

void storeError(const char* E, ERRORCODES CODE, bool writeToSD) {
    
    if (E && strlen(E) < 75) {
        strncpy(I.lastError, E, 74);
        I.lastError[74] = '\0';
    } else if (E) {
        strncpy(I.lastError, E, 74);
        I.lastError[74] = '\0';
    } else {
        I.lastError[0] = '\0';
    }

    I.lastErrorCode = CODE;
    I.lastErrorTime = I.currentTime;

    #ifdef _USESDCARD
    if (writeToSD) writeErrorToSD();
    #endif
}


void storeCoreData(bool forceStore) {
  //force core data to be stored to SD

  BootSecure bootSecure;
  int8_t ret = bootSecure.setPrefs(forceStore); 
  if (ret<0) {
    if (ret == -1) {  
      SerialPrint("Failed to encrypt core data",true);
      storeError("Failed to encrypt core data", ERROR_FAILED_PREFS, true);
    }
    if (ret == -10) {
      SerialPrint("Failed Prefs security",true);
      storeError("Failed Prefs security", ERROR_FAILED_PREFS, true);  
    }      
    if (ret == -100) {
      SerialPrint("Failed to create prefs",true);
      storeError("Failed to create prefs", ERROR_FAILED_PREFS, true);  
    }
  }
  
  //setup TFLuna is needed in case the distances have changed
  #ifdef _USETFLUNA
  if (ret>0) setupTFLuna();
  #endif

  if (forceStore || (!I.isUpToDate && I.lastStoreCoreDataTime + 300 < I.currentTime)) { //store if out of date and more than 5 minutes since last store
    I.isUpToDate = true;
    #ifdef _USESDCARD
    storeScreenInfoSD();
    storeDevicesSensorsSD();
    #endif
  }
}


void controlledReboot(const char* E, RESETCAUSE R,bool doreboot) {
  storeError(E);
  I.resetInfo = R;
  I.lastResetTime = I.currentTime;
  I.rebootsSinceLast++;


  storeCoreData();
  
  if (doreboot) {
    SerialPrint("Controlled reboot in 1 second", true);
    delay(1000);
    ESP.restart();
  }
}

String lastReset2String(bool addtime) {
  String output = "";
  switch (I.resetInfo) {
    case RESET_DEFAULT: output = "Default"; break;
    case RESET_SD: output = "SD Card"; break;
    case RESET_WEATHER: output = "Weather"; break;
    case RESET_USER: output = "User"; break;
    case RESET_OTA: output = "OTA"; break;
    case RESET_WIFI: output = "WiFi"; break;
    case RESET_TIME: output = "Time"; break;
    case RESET_UNKNOWN: output = "Unexpected Error"; break;
    default: output = "Unknown"; break;
  }
  
  if (addtime) {
    output += " at " + String(I.lastResetTime ? dateify(I.lastResetTime) : "???");
  }
  
  return output;
}

// Helper function to get detailed reboot information for debugging
String getRebootDebugInfo() {
  String info = "Reboot Debug Info:\n";
  info += "Reset Cause: " + lastReset2String(false) + "\n";
  info += "Last Reset Time: " + String(I.lastResetTime ? dateify(I.lastResetTime) : "Never") + "\n";
  info += "ALIVESINCE: " + String(I.ALIVESINCE ? dateify(I.ALIVESINCE) : "Never") + "\n";
  info += "Current Time: " + String(I.currentTime ? dateify(I.currentTime) : "Not Set") + "\n";
  if (I.lastResetTime != 0 && I.ALIVESINCE != 0 && I.currentTime != 0) {
    time_t timeDiff = I.currentTime - I.ALIVESINCE;
    info += "Time Difference: " + String(timeDiff) + " seconds\n";
  }
  return info;
}

// --- IP address conversion utilities ---

uint64_t IPToMACID(IPAddress ip) {
  //convert IP address to MAC ID
  //replace with 0x00000000FF000000 prefix if desired
  uint32_t ip32 = IPToUint32(ip);
  return (uint64_t)ip32;

}

uint64_t IPToMACID(byte* ip) {
  //wrapper for IPToMACID when ip is a byte array

  return IPToMACID(IPAddress(ip));
}

// Convert uint64_t MAC to 6-byte array
void uint64ToMAC(uint64_t mac64, byte* macArray) {
    memcpy(macArray, &mac64, 6); //note that arduino is little endian, so this is correct
}

// Convert 6-byte array to uint64_t MAC
uint64_t MACToUint64(const uint8_t* macArray) {
    uint64_t mac64 = 0;
    memcpy(&mac64, macArray, 6);
    return mac64;
}

String MACToString(const uint64_t mac64, char separator, bool asHex) {
  byte macArray[6];
  uint64ToMAC(mac64, macArray);
  return ArrayToString(macArray, 6,separator,asHex);
}

String MACToString(const uint8_t* mac, char separator, bool asHex) {
  
  return ArrayToString(mac, 6,separator,asHex);
}


bool compareMAC(byte *MAC1,byte *MAC2) {
  for (byte i=0;i<6;i++) {
    if (MAC1[i]!=MAC2[i]) return false;
  }
  return true;
}

bool isMACSet(byte *m, bool doReset) {
  for (byte i=0;i<6;i++) {
    if (m[i]!=0) return true;
  }
  if (doReset) {
    memset(m,0,6);
  }
  return false;
}


String ArrayToString(const uint8_t* Arr, byte len, char separator, bool asHex) {

  String output = "";
  char holder[20] = "";
  for (int i=len-1; i>=0; i--) {
    if (separator!='\0') {
      if (i!=len-1) output += (String) separator;
    }
  
    if (asHex)    snprintf(holder,20,"%02x",Arr[i]);
    else    snprintf(holder,20,"%d",Arr[i]);
    output += (String) holder;
  }
  return output;

}


// Get a specific byte (zero indexed) from PROCID (MAC address).
uint8_t getPROCIDByte(uint64_t procid, uint8_t byteIndex) {
    if (byteIndex >= 6) return 0;
    return (procid >> (8 * (byteIndex))) & 0xFF;
}

bool cycleByteIndex(byte* start, byte arraysize, byte origin, bool backwards) {
  //cycles through a byte vector of arraysize, from start and looping around back to 0 until it wraps around back to origin
  //usage: cycle through a vector of size 10, starting from 6... repeatedly call y = cycleIndex(x,10,6) where x is the last y returned
  //returns true if cycling, false if complete, and start is incremented appropriately.

  if (arraysize==0) return false;
  if (origin>=arraysize) return false;
  if (*start>=arraysize) return false; 



  if (backwards) {   //cycle backwards
    if (*start == 0) *start=arraysize-1;
    else *start = *start - 1;
  } else {          //cycle forwards
    if (*start == arraysize-1) *start=0;
    else *start = *start + 1;
  }

  if (*start==origin)  {
    //cycle index has already been updated, no changes
    return false;
  } else {
    return true;
  }
}

bool cycleIndex(uint16_t* start, uint16_t arraysize, uint16_t origin, bool backwards) {
  //cycles through a uint16_t vector of arraysize, from start and looping around back to 0 until it wraps around back to origin
  //usage: cycle through a vector of size 10, starting from 6... repeatedly call y = cycleIndex(x,10,6) where x is the last y returned
  //returns true if cycling, false if complete
  if (arraysize==0) return false;
  if (origin>=arraysize) return false;
  if (*start>=arraysize) return false; 



  if (backwards) {   //cycle backwards
    if (*start == 0) *start=arraysize-1;
    else *start = *start - 1;
  } else {          //cycle forwards
    if (*start == arraysize-1) *start=0;
    else *start = *start + 1;
  }

  if (*start==origin)  {
    //cycle index has already been updated, no changes
    return false;
  } else {
    return true;
  }

  
}


uint32_t IPToUint32(IPAddress ip) {
  return (ip[0]<<24) + (ip[1]<<16) + (ip[2]<<8) + ip[3];
} 

int16_t updateMyDevice() {
    //update mySensorsIndices array and update my IP if needed
    MY_DEVICE_INDEX = Sensors.addDevice(ESP.getEfuseMac(), WiFi.localIP(), Prefs.DEVICENAME, 0, 0, _MYTYPE);

  return MY_DEVICE_INDEX;
}

void failedToRegister() {
  SerialPrint("I am not registered as a device, and could not register, so I cannot run...",true);
  
  #ifdef _USETFT
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_RED);
  tft.printf("Unable to register as a device, so I cannot run...",true);
  tft.printf("Reverting to initial settings...",true);
  #endif
  deleteCoreStruct();

  //flush wifi info, sd card, prefs, screen flags, screen data, error, etc.
  controlledReboot("Unable to register myself as a device, so I cannot run...", RESET_UNKNOWN);
}

int8_t delete_all_core_data(bool flushPrefs, bool flushDevicesSensors) {
  //returns the number of files deleted, with the flags, weather, devices, and gsheet files being shifted left by 8, 16, and 24 bits respectively
  //returns MAX_UINT32 if no SD card

  memset(&I, 0, sizeof(I));
  initScreenFlags(true);

  uint32_t ndeleted = deleteDataFiles(true, true, flushDevicesSensors, true);
  int8_t ret = 0;
  if (flushPrefs) {
    memset(&Prefs, 0, sizeof(Prefs));
    BootSecure bootSecure;
    ret = bootSecure.flushPrefs();
  }
  
  return ret;
}

uint32_t deleteCoreStruct() {
//delete the core struct
memset(&I, 0, sizeof(I));
uint32_t ndeleted = deleteDataFiles(true, false, false, false);
initScreenFlags(true);
return ndeleted;
}

uint32_t deleteDataFiles(bool deleteFlags, bool deleteWeather, bool deleteGsheet, bool deleteDevices) {
  //returns the number of files deleted, with the flags, weather, devices, and gsheet files being shifted left by 8, 16, and 24 bits respectively
  //returns MAX_UINT32 if no SD card
  #ifdef _USESDCARD
  uint32_t nDeleted = 0;
  if (deleteGsheet) nDeleted += deleteFiles("GsheetInfo.dat", "/Data");
  if (deleteWeather) nDeleted += (deleteFiles("WeatherData.dat", "/Data")<<8);
  if (deleteDevices) nDeleted += (deleteFiles("DevicesSensors.dat", "/Data")<<16);
  if (deleteFlags) nDeleted += (deleteFiles("ScreenFlags.dat", "/Data")<<24);
  return nDeleted;
  #else
  return UINT32_MAX;
  #endif
}


bool tftPrint(String S, bool newline, uint16_t color, byte fontType, byte fontsize, bool cleartft, int x, int y) {
  //wrapper function to print to TFT, if available
  #ifdef _USETFT

  if (cleartft) tft.clear();
  if (x>=0 && y>=0) tft.setCursor(x,y);
  tft.setTextColor(color);
  tft.setTextFont(fontType);
  tft.setTextSize(fontsize);
  tft.print(S.c_str());
  if (newline) tft.println();
  tft.setTextColor(FG_COLOR);
  tft.setTextFont(2);
  tft.setTextSize(1);
  return true;
  #endif
  return false;
}

#ifdef _USETFT
void displaySetupProgress(bool success) {
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.println("OK.\n");
    tft.setTextColor(FG_COLOR,BG_COLOR);
  } else {
    tft.setTextColor(TFT_RED);
    tft.println("FAIL");
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }
}
#endif