#include "globals.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"
#include "firmwareUpdate.hpp"
#include "server.hpp"
#include <esp_app_format.h>
#include "esp_ota_ops.h"

#ifdef _USETFT
extern LGFX tft;
extern const uint16_t FG_COLOR ; //Foreground color
extern const uint16_t BG_COLOR;
#endif

#if _HAS_LOCAL_SENSORS
extern STRUCT_SNSHISTORY SensorHistory;
#endif

//flags for sensors:
////  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)

ERROR_STRUCT LASTERROR;

// SD is mounted in initSystem() after TFT init; guard writes until mount completes.
static bool sdCardReady() {
  #ifdef _USESDCARD
  return SD.cardType() != CARD_NONE;
  #else
  return false;
  #endif
}

//setup functions
void updateWifiChannel() {
  uint8_t ch = WiFi.channel();
  if (ch < 1 || ch > 14) return;

  if (ch != I.WifiChannel) {
    if (I.WifiChannel != 0) {
      SerialPrint("Wifi channel changed: " + String(I.WifiChannel) + " -> " + String(ch), true);
    }
    I.WifiChannel = ch;
    I.isUpToDate = false;
  }
}

#ifndef _USELOWPOWER
static struct {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t lastProgressMs = 0;
} s_arduinoOtaFocus;

static constexpr uint32_t ARDUINO_OTA_MAX_MS = 600000;
static constexpr uint32_t ARDUINO_OTA_STALL_MS = 90000;

void beginArduinoOtaFocus() {
  s_arduinoOtaFocus.active = true;
  s_arduinoOtaFocus.startMs = millis();
  s_arduinoOtaFocus.lastProgressMs = s_arduinoOtaFocus.startMs;
  SerialPrint("Arduino OTA focus mode started", true);
  logSystemEvent("Arduino OTA focus mode", EVENT_FIRMWARE_UPDATED);
}

static void endArduinoOtaFocus(const char* reason) {
  if (!s_arduinoOtaFocus.active) return;
  s_arduinoOtaFocus.active = false;
  if (reason && reason[0]) {
    SerialPrint(String("Arduino OTA focus ended: ") + reason, true);
    logSystemEvent(String("Arduino OTA ended: ") + reason, EVENT_FIRMWARE_UPDATED);
  }
}

static bool arduinoOtaFocusTimedOut() {
  if (!s_arduinoOtaFocus.active) return false;
  const uint32_t now = millis();
  if (now - s_arduinoOtaFocus.startMs > ARDUINO_OTA_MAX_MS) return true;
  if (now - s_arduinoOtaFocus.lastProgressMs > ARDUINO_OTA_STALL_MS) return true;
  return false;
}

static void abortArduinoOtaFocus(const char* reason) {
  if (Update.isRunning()) Update.abort();
  endArduinoOtaFocus(reason);
  storeError(String("Arduino OTA aborted: ") + reason, ERROR_UNDEFINED, true);
}

void notifyArduinoOtaProgress(unsigned int progress, unsigned int total) {
  (void)progress;
  (void)total;
  s_arduinoOtaFocus.lastProgressMs = millis();
  esp_task_wdt_reset();
  if (arduinoOtaFocusTimedOut()) {
    abortArduinoOtaFocus("timeout");
  }
}

void notifyArduinoOtaError() {
  endArduinoOtaFocus("error");
}

bool serviceArduinoOtaFocusMode() {
  if (!wifiReadyForNetwork()) return false;

  ArduinoOTA.handle();
  // Auth success sets OTA_RUNUPDATE; run transfer immediately without other loop work.
  if (wifiReadyForNetwork() && !Update.isRunning()) {
    ArduinoOTA.handle();
  }

  if (!s_arduinoOtaFocus.active && !Update.isRunning()) return false;

  while (s_arduinoOtaFocus.active || Update.isRunning()) {
    esp_task_wdt_reset();
    if (arduinoOtaFocusTimedOut()) {
      abortArduinoOtaFocus("timeout");
      break;
    }
    if (wifiReadyForNetwork()) {
      ArduinoOTA.handle();
    }
    if (!s_arduinoOtaFocus.active && !Update.isRunning()) break;
  }
  return true;
}
#endif

void systemHousekeeping(bool fullHousekeeping) {
  //this should run on every loop cycle
  updateTime();

  if (softApRunning()) {
    serviceAPStationMode();
  }

  if (fullHousekeeping) {
    I.MY_DEVICE_INDEX = Sensors.findMyDeviceIndex(); //update my device index

    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap();
      
    // If memory is critically low, restart
    if (freeHeap < 1000) { // Less than 1KB free
      SerialPrint("CRITICAL: Low memory detected, restarting system", true);
      storeError("CRITICAL: Low memory detected, restarting system", ERROR_HARDWARE_MEMORY,true);
      delay(1000);
      controlledReboot("Low memory detected, restarting system", RESET_MEMORY_LOW, true);
    }
    
    // minFreeHeap is a lifetime watermark (lowest since boot), not current fragmentation.
    // Only warn when both watermark and current free heap are critically low.
    if (minFreeHeap < 5000 && freeHeap < 15000) {
      SerialPrint("WARNING: Heap critically low (min=" + String(minFreeHeap) + " free=" + String(freeHeap) + ")", true);
    }


    if (isTimeValid(I.ALIVESINCE)==false) I.ALIVESINCE = I.currentTime; //if ALIVESINCE is not valid, set it to the current time

    if (isTimeValid(I.lastResetTime)==false) I.lastResetTime = I.currentTime; //if lastResetTime is not valid, set it to the current time

    if (wifiReadyForNetwork() && I.UTCTime >= TIMEZERO) {
      if (I.lastTimezoneRefresh == 0
          || (I.currentTime >= I.lastTimezoneRefresh
              && I.currentTime - I.lastTimezoneRefresh >= TIMEZONE_REFRESH_INTERVAL_SEC)) {
        refreshTimezoneFromNetwork(TIMEZONE_REFRESH_HTTP_TIMEOUT_MS);
      }
    }

    CheckWifiStatus(WIFI_CHECK_NORMAL);

    updateWifiChannel();

    ensureESPNOW(); // router not required; retry if init failed earlier

    #ifdef _USEUDP
    maybeRefreshIGMPMembership();
    #endif

    #if _HAS_LOCAL_SENSORS
    peripheralFirmwareHourlyCheck();
    processChunkFirmwareTick();
    #endif

  }

  if (wifiReadyForNetwork() || softApRunning()) {
    server.handleClient();
  }

  esp_task_wdt_reset();

  #ifdef _USEUDP
  receiveUDPMessage(); //receive ESPNow UDP messages, which are sent in parallel to ESPNow
  #endif

  processDeferredDataRequest();

}
void initI2C() {
  #ifdef _USEI2C
  Wire.begin();
  Wire.setClock(400000L);
  SerialPrint("Wire initialized",true);
  #endif
}

void FirmwareVersion::clear() {
  v[0] = v[1] = v[2] = 0;
}

bool FirmwareVersion::isUnset() const {
  return v[0] == 0 && v[1] == 0 && v[2] == 0;
}

bool FirmwareVersion::fromText(const char* text) {
  if (!text || text[0] == '\0') return false;
  unsigned int major = 0, minor = 0, patch = 0;
  if (sscanf(text, "%u.%u.%u", &major, &minor, &patch) != 3) return false;
  if (major > 255 || minor > 255 || patch > 255) return false;
  char normalized[20];
  snprintf(normalized, sizeof(normalized), "%u.%u.%u", major, minor, patch);
  if (strcmp(normalized, text) != 0) return false;
  v[0] = (uint8_t)major;
  v[1] = (uint8_t)minor;
  v[2] = (uint8_t)patch;
  return true;
}

void FirmwareVersion::toChar(char* out, size_t outLen) const {
  if (!out || outLen == 0) return;
  snprintf(out, outLen, "%u.%u.%u", v[0], v[1], v[2]);
}

void FirmwareVersion::toBinPathSegment(char* out, size_t outLen) const {
  toChar(out, outLen);
}

int FirmwareVersion::compareBytes(const uint8_t a[3], const uint8_t b[3]) {
  if (!a || !b) return 0;
  for (uint8_t i = 0; i < 3; i++) {
    if (a[i] != b[i]) return (a[i] > b[i]) ? 1 : -1;
  }
  return 0;
}

int FirmwareVersion::compare(const uint8_t other[3]) const {
  return compareBytes(v, other);
}

int FirmwareVersion::compare(const FirmwareVersion& other) const {
  return compare(other.v);
}

bool getEmbeddedFirmwareVersion(FirmwareVersion& out) {
  const esp_app_desc_t* desc = esp_app_get_description();
  if (desc && out.fromText(desc->version)) {
    return true;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return false;

  esp_app_desc_t partitionDesc = {};
  if (esp_ota_get_partition_description(running, &partitionDesc) != ESP_OK) {
    return false;
  }
  return out.fromText(partitionDesc.version);
}

void getLocalFirmware(FirmwareVersion& out) {
  if (getEmbeddedFirmwareVersion(out)) return;
  out = Prefs.FIRMWARE;
}

String firmwareJsonArray(const FirmwareVersion& fw) {
  return "[" + String(fw.v[0]) + "," + String(fw.v[1]) + "," + String(fw.v[2]) + "]";
}

bool initSystem() {


  #ifdef _USESERIAL
    
  Serial.begin(_USESERIAL);
  Serial.println("Serial started");
  SerialPrint("SerialPrint started",true);
  I.SerialPrintLevel = 0; //temporarily print all messages
  #endif


  #ifdef _USESPI
  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
  #endif

  initI2C(); //initialize the I2C bus

  #ifdef _USESSD1306
  initOled();
  #endif

  #ifdef _USETFT
  #ifdef _ISCLOCK480X480
    initClock480X480Graphics();
  #else
    tft.init();
    tft.setRotation(2);
    tft.setCursor(0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    tft.printf("Running setup\n");
  #endif
  #endif

  #if defined(_USESDCARD) && !defined(_ISCLOCK480X480)
  if (initSDCard() == 0) return false;
  #endif

  SerialPrint("Check core data ...", true);
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
      SerialPrint("Prefs failed to load with error code: " + String(boot_status), true,5);
      SerialPrint("Will redefine Prefs struct later...", true,5);
  } else SerialPrint("Prefs loaded successfully, my name is: " + String(Prefs.DEVICENAME),true,5);

  #ifdef _USESDCARD
  // Load devices/sensors before any registration or IP sync can overwrite DevicesSensors.dat.
  loadSensorData();
  #endif

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

  {
    FirmwareVersion buildFw;
    if (getEmbeddedFirmwareVersion(buildFw)) {
      if (Prefs.FIRMWARE.compare(buildFw) != 0) {
        char oldBuf[16], newBuf[16];
        Prefs.FIRMWARE.toChar(oldBuf, sizeof(oldBuf));
        buildFw.toChar(newBuf, sizeof(newBuf));
        Prefs.FIRMWARE = buildFw;
        int8_t saveStatus = bootSecure.setPrefs(true);
        if (saveStatus > 0) {
          SerialPrint("Prefs.FIRMWARE updated from " + String(oldBuf) + " to embedded version " + String(newBuf), true, 5);
        } else {
          Prefs.isUpToDate = false;
          SerialPrint("Prefs.FIRMWARE set to " + String(newBuf) + " but NVS save failed: " + String(saveStatus), true, 5);
        }
      }
    } else {
      SerialPrint("Embedded firmware version parse failed", true, 5);
    }
  }

  SerialPrint("Check firmware version...", true,5);
  {
    FirmwareVersion fw;
    getLocalFirmware(fw);
    char verBuf[16];
    fw.toChar(verBuf, sizeof(verBuf));
    tftPrint("Cuurrent firmware version: " + String(verBuf), true);
  }
  bool newfirmware = checkOtaSlotAtBoot();
  if (newfirmware && Prefs.AUTOSWITCHNEWERFIRMWARE) {
    check_and_switch_to_newer_firmware(false, true);
  }

  tftPrint("Init Wifi... \n", true);
  SerialPrint("Init Wifi... ",true,5);
  setupServerRoutes();
  WiFi.onEvent(WiFiEvent); //register the WiFi event handler

  CheckWifiStatus(WIFI_CHECK_BOOT);
  syncInitialSetupState();

  #ifdef _USEUDP
  if (wifiReadyForNetwork() && !connectUDP()) {
    SerialPrint("Failed to connect to UDP",true,5);
    storeError("Failed to connect to UDP");
  }
  #endif

  #ifdef _USETFT
  displaySetupProgress(true);
  #endif

  if (wifiReadyForNetwork()) {
    SerialPrint("Wifi OK. Current IP Address: " + WiFi.localIP().toString(), true, 5);
  } else {
    SerialPrint("Wifi not connected; continuing with AP/ESP-NOW. Status: " + String(I.WiFiStatus), true, 5);
  }

  IPAddress regIP = wifiReadyForNetwork() ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  int16_t devIndex = Sensors.findMyDeviceIndex();
  if (devIndex < 0) {
    devIndex = Sensors.addDevice(ESP.getEfuseMac(), regIP, Prefs.DEVICENAME, 0, 0, _MYTYPE);
  }
  if (devIndex < 0) {
    failedToRegister();
    return false;
  }
  I.MY_DEVICE_INDEX = devIndex;
  Sensors.updateMyDeviceVersion();
  syncDeviceIPFromWifi();

  tftPrint("Init server... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  if (!softApRunning()) {
    server.begin();
  }
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

  // Early NTP when WiFi is already up; setupTime() in main handles full path + ESP-NOW fallback.
  if (wifiReadyForNetwork()) {
    if (!syncNtpAndApplyDST()) {
      SerialPrint("Boot NTP/DST apply failed; will retry when time is valid", true, 5);
    }
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

  // ScreenFlags.dat can be saved while AP provisioning was active; do not restore that runtime state.
  reconcileWifiStateAfterCoreLoad();

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
  Sensors.updateMyDeviceVersion();
  I.MY_DEVICE_INDEX = Sensors.findMyDeviceIndex();
  displaySetupProgress( sdread);
  SerialPrint("Sensor data loaded? ",false);
  SerialPrint((sdread==true)?"yes":"no",true);
  if (sdread) {
    SerialPrint("Loaded devices/sensors: " + String(Sensors.getNumDevices()) + " / " + String(Sensors.getNumSensors()), true);
  }
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
  if (soil < -100 || soil > 400) return false;
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

#if _HAS_LOCAL_SENSORS
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
    int16_t index = index_start;
    while (cycleIndex(index, _SENSORHISTORYSIZE, SensorHistory.HistoryIndex[n])) {
      if (isTimeValid(SensorHistory.TimeStamps[n][index])) {
        t[i] = SensorHistory.TimeStamps[n][index];
        v[i] = SensorHistory.Values[n][index];
        f[i] = SensorHistory.Flags[n][index];
        i++;
      }
    }
  } else {
    //pull data in order of newest to oldest
    int16_t index = SensorHistory.HistoryIndex[n];
    while (cycleIndex(index, _SENSORHISTORYSIZE, SensorHistory.HistoryIndex[n],true) && i<*N) { //true means cycle backwards
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

  int8_t sensorHistoryIndex = SensorHistory.getSensorHistoryIndex(sensorIndex);

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
        if (isBit(SensorHistory.Flags[sensorHistoryIndex][j],0)) avgFlag=1;
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
      I.rebootsToday=0;
      I.wifiFailCount=0;
      I.currentTime=0;

      #if defined(_USETFT) && _IS_SERVER_HUB
      
    
      I.isExpired = false; //are any critical sensors expired?
      I.wasFlagged=false;
      I.isHeat=false; //first bit is heat on, bits 1-6 are zones
      I.isAC=false; //first bit is compressor on, bits 1-6 are zones
      I.isFan=false; //first bit is fan on, bits 1-6 are zones
      I.wasHeat=false; //first bit is heat on, bits 1-6 are zones
      I.wasAC=false; //first bit is compressor on, bits 1-6 are zones
      I.wasFan=false; //first bit is fan on, bits 1-6 are zones
      #ifdef _MONITOROUTDOORBATTERYSENSORS
      I.localBatteryIndex=255;
      #endif

      I.isHot=0;
      I.isCold=0;
      I.isSoilDry=0;
      I.isLeak=0;

      I.currentOutsideTemp=-127;
      I.currentOutsideHumidity=-127;
      I.currentOutsidePressure=-127;
      I.Tmax=-127;
      I.Tmin=127;
      I.lastCurrentOutsideTemp=-127;

      #endif
      
      I.lastErrorTime=0;
  }

  
  #ifdef _USETFT
  initGraphics();
  #endif

  #if defined(_USETFT) && _IS_SERVER_HUB
  
  #ifdef _USEWEATHER
  #endif
  #endif

  #ifdef _USESDCARD
  I.lastStoreCoreDataTime = 0;
  #endif

  I.WiFiLastEvent = ARDUINO_EVENT_WIFI_READY;
  I.WifiChannel = 0;
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

  // RSSI min/max are since boot; do not carry over from ScreenFlags.dat
  I.RSSIcurrent = -999;
  I.RSSIlow = -999;
  I.RSSIhigh = -999;
  I.lastRSSItime = 0;

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


bool SerialPrint(String S,bool newline, int8_t level) {
  return SerialPrint(S.c_str(),newline, level);
}

bool SerialPrint(const char* S,bool newline, int8_t level) {
  
  #ifdef _USESERIAL

    if (I.SerialPrintLevel == 0 || level < 0 && level == I.SerialPrintLevel || level > 0 && level >= I.SerialPrintLevel) {
      Serial.printf("%s",S);
      if (newline) Serial.println();
      return true;
    }
    return false;
  #endif

  return false;

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
  uint16_t value16 = value;
  char output16[17];
  uint16ToBin(value16, output16, invert);

  //copy the first 8 chars of output16 to output if invert is false, or the last 8 chars if invert is true
  if (invert) {
    memcpy(output, output16, 8);
  } else {
    memcpy(output, output16+8, 8);
  }
  output[8] = '\0';

  return;
}

void uint16ToBin(uint16_t value, char* output, bool invert) {
  //invert puts the LSB on the LEFT: so the value two would be 0000000000000010 with invert false, but with invert true it would be 0100000000000000
  snprintf(output,17,"0000000000000000");
  for (byte i = 0; i < 16; i++) {
    uint16_t testbit = value & (1 << i);
    if (testbit != 0) {
      if (invert)         output[i] = '1';
      else                output[15-i] = '1';      
    } 
  }
  return;
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

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void initSensor(int k) {
  Sensors.initSensor(k);
}

uint8_t countFlagged(int16_t snsType, uint16_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan, bool countCriticalExpired, bool countAnyExpired, uint16_t optionalsnsflags) {
  return Sensors.countFlagged(snsType, flagsthatmatter, flagsettings, MoreRecentThan,  countCriticalExpired, countAnyExpired, optionalsnsflags);    
}

#if _IS_SERVER_HUB
void checkHVAC() {
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
    ArborysSnsType* sensor = Sensors.snsIndexToPointer(snsindex);
    if (!sensor || !sensor->IsSet) continue;
    if (sensor->snsValue > 0) {
      switch (sensor->snsType) {
        case 50: //total time          
        break;
        case 51: //heat - gas valve
        case 52: //heat        
          if (isBit(sensor->Flags,0)) I.isHeat = 1;
        break;
        case 55: // Fan
          if (isBit(sensor->Flags,0)) I.isFan = 1;
        break;
        case 56: //ac
        case 57: //ac
          if (isBit(sensor->Flags,0)) I.isAC = 1;
        break;
      }
    }
    snsindex = Sensors.findSnsOfType("HVAC", false, snsindex+1);
  }
  return;
}
#endif


//bit functions
//uint8_t
bool isBit(uint8_t value, uint8_t bit) {
  return (value & (1 << bit)) != 0;
}
void clearBit(uint8_t &value, uint8_t bit) {
  value &= ~(1 << bit);
}
void setBit(uint8_t &value, uint8_t bit) {
  value |= (1 << bit);
}
void flipBit(uint8_t &value, uint8_t bit) {
  value ^= (1 << bit);
}

//uint16_t
bool isBit(uint16_t value, uint8_t bit) {
  return (value & (1 << bit)) != 0;
}
void clearBit(uint16_t &value, uint8_t bit) {
  value &= ~(1 << bit);
}
void setBit(uint16_t &value, uint8_t bit) {
  value |= (1 << bit);
}
void flipBit(uint16_t &value, uint8_t bit) {
  value ^= (1 << bit);
}


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


void storeError(String E, ERRORCODES CODE, bool writeToSD) {
  storeError(E.c_str(), CODE, writeToSD);
}

void storeError(const char* E, ERRORCODES CODE, bool writeToSD) {
  ERROR_STRUCT LASTERROR;
  strncpy(LASTERROR.errorMessage, E, 99);
  LASTERROR.errorCode = CODE;
  LASTERROR.errorTime = I.currentTime;

  #ifdef _USESDCARD
  if (writeToSD && sdCardReady()) writeErrorToSD(LASTERROR);
  #endif

  strncpy(I.lastError, LASTERROR.errorMessage, 75);
  I.lastErrorCode = LASTERROR.errorCode;
  I.lastErrorTime = LASTERROR.errorTime;

}

static void sanitizeSystemLogField(char* dest, const char* src, size_t maxLen) {
  size_t j = 0;
  if (!src) src = "";
  for (size_t i = 0; src[i] != '\0' && j < maxLen; ++i) {
    char c = src[i];
    if (c == '|' || c == '\n' || c == '\r') c = ' ';
    dest[j++] = c;
  }
  dest[j] = '\0';
}

void logSystemEvent(const char* description, SYSTEMEVENTS code) {
  #ifdef _USESDCARD
  if (!sdCardReady()) return;

  char descBuf[61];
  sanitizeSystemLogField(descBuf, description, 60);

  char timeBuf[32];
  if (isTimeValid(I.currentTime)) {
    strncpy(timeBuf, dateify(I.currentTime, "yyyy-mm-dd hh:nn:ss"), sizeof(timeBuf) - 1);
  } else {
    strncpy(timeBuf, "???", sizeof(timeBuf) - 1);
  }
  timeBuf[sizeof(timeBuf) - 1] = '\0';

  char line[128];
  snprintf(line, sizeof(line), "%s|%s|%d\n", timeBuf, descBuf, (int)code);

  if (!SD.exists("/Data")) SD.mkdir("/Data");
  File file = SD.open("/Data/systemlog.txt", FILE_APPEND);
  if (!file) {
    storeError("logSystemEvent: Could not open systemlog.txt", ERROR_SD_FILEWRITE, false);
    return;
  }
  file.print(line);
  file.close();
  #else
  (void)description;
  (void)code;
  #endif
}

void logSystemEvent(String description, SYSTEMEVENTS code) {
  logSystemEvent(description.c_str(), code);
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
  logSystemEvent(E, EVENT_REBOOT_TRIGGERED);
  I.resetInfo = R;
  I.lastResetTime = I.currentTime;

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

bool cycleByteIndex(byte& start, byte arraysize, byte origin, bool backwards) {
  //cycles through a byte vector of arraysize, from start and looping around back to 0 until it wraps around back to origin
  //usage: cycle through a vector of size 10, starting from 6... repeatedly call y = cycleIndex(x,10,6) where x is the last y returned
  //returns true if cycling, false if complete, and start is incremented appropriately.

  if (arraysize==0) return false;
  if (origin>=arraysize) return false;
  if (start>=arraysize) return false; 
  
  //check for first run!
  if (start==-1 && backwards==false) {
    //first run
    start=0;
    return true;
  } 
  if (start==arraysize && backwards==true) {
    start=arraysize-1;
    return true;
  }

  if (backwards) {   //cycle backwards
    if (start == 0) start=arraysize-1;
    else start = start - 1;
  } else {          //cycle forwards
    if (start == arraysize-1) start=0;
    else start = start + 1;
  }

  if (start==origin)  {
    //cycle index has already been updated, no changes
    return false;
  } else {
    return true;
  }
}

bool cycleIndex(int16_t& start, uint16_t arraysize, uint16_t origin, bool backwards) {
  //cycles through a uint16_t vector of arraysize, from start and looping around back to 0 until it wraps around back to origin
  //usage: cycle through a vector of size 10, starting from 6... repeatedly call y = cycleIndex(x,10,6) where x is the last y returned
  //returns true if cycling, false if complete
  if (arraysize==0) return false;
  if (origin>=arraysize) return false;
  if (start>=arraysize) return false; 
  //check for first run!
  if (start==-1 && backwards==false) {
    //first run
    start=0;
    return true;
  } 
  if (start==arraysize && backwards==true) {
    start=arraysize-1;
    return true;
  }

  if (backwards) {   //cycle backwards
    if (start == 0) start=arraysize-1;
    else start = start - 1;
  } else {          //cycle forwards
    if (start == arraysize-1) start=0;
    else start = start + 1;
  }

  if (start==origin)  {
    //cycle index has already been updated, no changes
    return false;
  } else {
    return true;
  }
  
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


int8_t shoutThis(String S, bool newline, uint16_t color, byte fontType, byte fontsize, bool cleartft, int x, int y) {
  SerialPrint(S, newline);
  tftPrint(S, newline, color, fontType, fontsize, cleartft, x, y);
  return 0;
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


static String otaSlotSwitchFailureCondition(esp_err_t err) {
    switch (err) {
        case ESP_ERR_OTA_VALIDATE_FAILED:
            return "target partition does not contain a valid firmware image (empty, corrupt, or incomplete OTA write)";
        case ESP_ERR_NOT_FOUND:
            return "OTA data partition (otadata) not found or invalid";
        case ESP_ERR_INVALID_ARG:
            return "target is not a valid OTA app partition";
        default:
            return "boot partition could not be updated";
    }
}

int16_t force_switch_ota_slot(int slot_number, String* failureDetail) {
    const esp_partition_t* target_partition = NULL;
    
    if (slot_number == -1) {
        // Correctly assign the "other" partition
        target_partition = esp_ota_get_next_update_partition(NULL);
        SerialPrint("Attempting to toggle to the other OTA slot...",true);
    } 
    else if (slot_number == 0) {
        target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    } 
    else if (slot_number == 1) {
        target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    }

    if (target_partition != NULL) {
        // Safety: Check if we are already running on the target partition
        const esp_partition_t* running_partition = esp_ota_get_running_partition();
        if (target_partition == running_partition) {
            SerialPrint("Already running from the target partition. No switch needed.",true);
            return 0; 
        }

        esp_err_t err = esp_ota_set_boot_partition(target_partition);
        if (err == ESP_OK) {
            SerialPrint("Boot partition set to " + String(target_partition->label) + ". Restarting...", true);
            delay(1000); // Give serial time to flush
            //esp_restart();
            return 1; //allow caller to restart
        } else {
            SerialPrint("Failed to set boot partition: " + String(esp_err_to_name(err)), true);
            if (failureDetail) {
                String runningLabel = running_partition ? String(running_partition->label) : String("unknown");
                *failureDetail = "Code -2: esp_ota_set_boot_partition failed with "
                    + String(esp_err_to_name(err)) + " (0x" + String((uint32_t)err, HEX) + "). "
                    + otaSlotSwitchFailureCondition(err)
                    + ". Running slot: " + runningLabel
                    + ", target slot: " + String(target_partition->label) + ".";
            }
            return -2; //error, could not set boot partition
        }
    } else {
        SerialPrint("Target partition not found! Check your partition table.", true);
        if (failureDetail) {
            if (slot_number == -1) {
                *failureDetail = "Code -1: esp_ota_get_next_update_partition returned NULL. "
                    "No alternate OTA app slot is available; check that the flash partition table defines both ota_0 and ota_1.";
            } else if (slot_number == 0 || slot_number == 1) {
                *failureDetail = "Code -1: OTA slot " + String(slot_number)
                    + " (ota_" + String(slot_number) + ") was not found in the partition table.";
            } else {
                *failureDetail = "Code -1: Invalid OTA slot request (" + String(slot_number)
                    + "); use -1 (toggle), 0 (ota_0), or 1 (ota_1).";
            }
        }
        return -1; //error, target partition not found
    }
}

int8_t otaPartitionSlotNumber(const esp_partition_t* partition) {
  if (!partition) return -1;
  if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return 0;
  if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return 1;
  return -1;
}

bool getOtaPartitionFirmwareVersion(const esp_partition_t* partition, FirmwareVersion& out) {
  if (!partition) return false;
  esp_app_desc_t desc = {};
  if (esp_ota_get_partition_description(partition, &desc) != ESP_OK) return false;
  return out.fromText(desc.version);
}

#ifdef _USETFT
static void bootOtaTftLine(const char* text, uint16_t fg, uint16_t bg) {
  if (!text) return;
  tft.setTextColor(fg, bg);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.println(text);
}
#endif

bool checkOtaSlotAtBoot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  if (!next) return false;

  esp_app_desc_t running_desc = {};
  esp_app_desc_t next_desc = {};
  if (esp_ota_get_partition_description(running, &running_desc) != ESP_OK
      || esp_ota_get_partition_description(next, &next_desc) != ESP_OK) {
    return false;
  }

  SerialPrint(String("OTA boot: running ") + running_desc.version + " | next " + next_desc.version, true, 5);

  FirmwareVersion runningFw;
  if (!runningFw.fromText(running_desc.version) && !getEmbeddedFirmwareVersion(runningFw)) {
    SerialPrint("OTA: running partition version invalid: " + String(running_desc.version), true, 5);
    return false;
  }

  FirmwareVersion nextFw;
  if (!nextFw.fromText(next_desc.version)) {
    SerialPrint("OTA: next partition version invalid (not x.x.x): " + String(next_desc.version), true, 5);
    #ifdef _USETFT
    bootOtaTftLine("Next OTA slot has invalid firmware", TFT_YELLOW, BG_COLOR);
    bootOtaTftLine("Current firmware is up to date.", TFT_GREEN, BG_COLOR);
    tft.setTextColor(FG_COLOR, BG_COLOR);
    #endif
    return false;
  }

  if (nextFw.compare(runningFw) <= 0) {
    SerialPrint("OTA: current firmware is up to date", true, 5);
    #ifdef _USETFT
    bootOtaTftLine("Current firmware is up to date.", TFT_GREEN, BG_COLOR);
    tft.setTextColor(FG_COLOR, BG_COLOR);
    #endif
    return false;
  }

  SerialPrint("OTA: next slot is newer than running firmware", true);
  #ifdef _USETFT
  bootOtaTftLine("Next OTA slot is newer", TFT_BLACK, TFT_RED);
  bootOtaTftLine("Consider a manual switch:", TFT_RED, BG_COLOR);
  bootOtaTftLine("System Config -> switch OTA", TFT_RED, BG_COLOR);
  tft.setTextColor(FG_COLOR, BG_COLOR);
  #endif
  logSystemEvent("Firmware is older than next OTA.", EVENT_BOOT_WARNING);
  delay(3000);
  return true;
}

bool check_and_switch_to_newer_firmware(bool verbose,bool doswitch) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);

  if (next == NULL) return false;

  esp_app_desc_t running_desc = {};
  esp_app_desc_t next_desc = {};
  if (esp_ota_get_partition_description(running, &running_desc) != ESP_OK
      || esp_ota_get_partition_description(next, &next_desc) != ESP_OK) {
    return false;
  }

  FirmwareVersion runningFw, nextFw;
  if (!runningFw.fromText(running_desc.version) && !getEmbeddedFirmwareVersion(runningFw)) {
    return false;
  }
  if (!nextFw.fromText(next_desc.version)) {
    return false;
  }
  if (nextFw.compare(runningFw) <= 0) {
    return false;
  }

  if (!doswitch) {
    return true;
  }

  SerialPrint("OTA: switching to newer firmware in other partition", true);
  if (verbose) {
    shoutThis("Switching to newer firmware...", true);
  }
  if (esp_ota_set_boot_partition(next) == ESP_OK) {
    esp_restart();
  }
  return true;
}




uint8_t returnLiBatteryPercentage(double voltage) {
  static float Li_BAT_VOLT[22] = {4.2,4.15,4.11,4.08,4.02,3.98,3.95,3.91,3.87,3.85,3.84,3.82,3.8,3.79,3.77,3.75,3.73,3.71,3.69,3.61,3.27,2};
  static byte Li_BAT_PCNT[22] = {100,95,90,85,80,75,70,65,60,55,50,45,40,35,30,25,20,15,10,5,1,0};

  for (byte jj=0;jj<22;jj++) {
    if (voltage> Li_BAT_VOLT[jj]) {
      return Li_BAT_PCNT[jj];
    } 
  }

  return 255;
}

uint8_t returnPbBatteryPercentage(double voltage) {
  static float Pb_BAT_VOLT[11] = {12.89,12.78,12.65,12.51,12.41,12.23,12.11,11.96,11.81,11.7,11.63};
  static byte Pb_BAT_PCNT[11] = {100,90,80,70,60,50,40,30,20,10,0};

  for (byte jj=0;jj<11;jj++) {
    if (voltage>= Pb_BAT_VOLT[jj]) {
      return Pb_BAT_PCNT[jj];
    } 
  }
  
  return 255;
}


void displaySetupProgress(bool success) {
  #ifdef _USETFT
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.println("OK.\n");
    tft.setTextColor(FG_COLOR,BG_COLOR);
  } else {
    tft.setTextColor(TFT_RED);
    tft.println("FAIL");
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }
  #endif
}

#ifdef _USETFT

void screenWiFiDown() {
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_RED);
  tft.printf("WiFi may be down.\nAP mode enabled.\nConnect to: %s\nIP: 192.168.4.1\nif wifi credentials need to be updated.\nRebooting in 5 min...", WiFi.softAPSSID().c_str());
}

void displayOTAProgress(unsigned int progress, unsigned int total) {
  if (progress==0) {
    tft.fillScreen(BG_COLOR);            // Clear screen
    tft.setTextFont(2);
    tft.setCursor(0,0);
    tft.println("OTA started, receiving data.");
    tft.drawRect(0,100,tft.width(),25,FG_COLOR);
  } else {
    tft.fillRect(0,100,tft.width()*progress/total,25,FG_COLOR);    
  }
}

void displayOTAError(int error) {
  tft.setCursor(0,0);
  tft.print("OTA error: ");
  tft.println(error);
} 

void displayWiFiStatus(byte retries, bool success) {
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.printf("Wifi ok, %u attempts.\nWifi IP is %s\nMAC is %s\n",retries,WiFi.localIP().toString().c_str(),MACToString(Prefs.PROCID).c_str());
    tft.setTextColor(FG_COLOR);
  } else {
    tft.printf("Wifi FAILED %d attempts - reboot in 120s",retries);
  }
}



#endif