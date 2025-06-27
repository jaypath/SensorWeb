//Version 12 - 
/*
 * v11.1
 * now obtains info from Kiyaan's server
 * TFT attached to esp8266
 * no nrf radio in the loop
 * 
 * v11.2
 * Receives data directly from sensors
 * 
 * v11.3 new sensor sending definition, including flags
 * 
 * v12 - many changes. sends daa to google drive connected arduino
  */

/*
//0 - not defined/not a sensor/do not use
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - 
//7 - distance, HC-SR04
//8 - human presence (mm wave)
//9 - BMP pressure
//10 - BMP temp
//11 - BMP altitude
//12 - Pressure derived prediction (uses an array called BAR_HX containing hourly air pressure for past 24 hours). REquires _USEBARPRED be defined
//13 - BMe pressure
//14 - BMe temp
//15 - BMe humidity
//16 - BMe altitude
//17 - BME680 temp
18 - BME680 rh
19 - BME680 air press
20  - BME680 gas sensor
21 - human present (mmwave)
40 - any binary, 1=yes/true/on
41 = any on/off switch
42 = any yes/no switch
43 = any 3 way switch
50 = total HVAC time
55 - heat on/off {requires N DIO Pins}
56 - a/c  on/off {requires 2 DIO pins... compressor and fan}
57 - a/c fan on/off
60 -  battery power
61 - battery %
70 - leak
99 = any numerical value
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <LovyanGFX.hpp>
#include <esp_task_wdt.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "globals.hpp"
#include "utility.hpp"
#include "Devices.hpp"
#include "server.hpp"
#include "SDCard.hpp"
#include "Weather.hpp"
#include "timesetup.hpp"
#include "graphics.hpp"
#include "AddESPNOW.hpp"
#include "BootSecure.hpp"

#ifdef _WEBDEBUG
  String WEBDEBUG = "";
#endif

//gen global types

/*
//gen unions
union convertULONG {
  char str[4];
  unsigned long val;
};
union convertINT {
  char str[2];
  int16_t val;
};

union convertBYTE {
  char str;
  uint8_t  val;
};
*/

//globals

extern LGFX tft;
extern uint8_t SECSCREEN;
extern WiFi_type WIFI_INFO;
extern Screen I;
extern String WEBHTML;
//extern uint8_t OldTime[4];
extern WeatherInfo WeatherData;

extern uint32_t LAST_WEB_REQUEST;
extern WeatherInfo WeatherData;
extern double LAST_BAR;

uint32_t FONTHEIGHT = 0;

//time
uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d

//fuction declarations

void checkHeat();

// --- Helper Function Declarations ---
void initDisplay();
bool initSDCard();
bool loadSensorData();
bool loadScreenFlags();
bool initWiFi();
void initServerRoutes();
void initOTA();
void printSetupStatus();
void handleESPNOWPeriodicBroadcast(uint8_t interval = 5);

// --- Setup Helper Implementations ---

/**
 * @brief Initialize the TFT display and SPI.
 */
void initDisplay() {
    SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
    tft.init();
    tft.setRotation(2);
    tft.setCursor(0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    tft.printf("Running setup\n");
}

/**
 * @brief Initialize SD card and mount filesystem.
 * @return true if SD card mounted successfully, false otherwise.
 */
bool initSDCard() {
    tft.print("SD Card mounting...");
    if (!SD.begin(41)) {
        displaySetupProgress("SD Card mounting", false);
        #ifdef _DEBUG
        Serial.println("SD mount failed... ");
        #endif
        delay(10000);
        I.resetInfo = RESET_SD;
        I.lastResetTime = I.currentTime;
        controlledReboot("SD Card failed", RESET_SD);
        return false;
    }
    #ifdef _DEBUG
    Serial.println("SD mounted... ");
    #endif
    displaySetupProgress("SD Card mounting", true);
    return true;
}

/**
 * @brief Load sensor data from SD card.
 * @return true if successful, false otherwise.
 */
bool loadSensorData() {
    tft.print("Loading sensor data from SD... ");
    bool sdread = readSensorsSD();
    displaySetupProgress("Loading sensor data from SD", sdread);
    if (!sdread) delay(5000);
    #ifdef _DEBUG
    Serial.print("Sensor data loaded? ");
    Serial.println((sdread==true)?"yes":"no");
    #endif
    return sdread;
}

/**
 * @brief Load screen flags from SD card.
 * @return true if successful, false otherwise.
 */
bool loadScreenFlags() {
    tft.print("Loading screen flags from SD... ");
    bool sdread = readScreenInfoSD();
    displaySetupProgress("Loading screen flags from SD", sdread);
    if (!sdread) delay(5000);
    return sdread;
}

/**
 * @brief Initialize WiFi connection.
 * @return true if connected, false otherwise.
 */
bool initWiFi() {
    tft.println("Connecting  Wifi...\n");
    WIFI_INFO.HAVECREDENTIALS = false;
    #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
    #endif
    byte retries = connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
        displayWiFiStatus(retries, false);
        delay(120000);
        I.resetInfo = RESET_WIFI;
        I.lastResetTime = I.currentTime;
        controlledReboot("WiFi failed", RESET_WIFI);
        return false;
    }
    displayWiFiStatus(retries, true);
    tft.printf("Internal network ");
    if (((WIFI_INFO.MYIP >> 24) & 0xFF) == 192 && ((WIFI_INFO.MYIP >> 16) & 0xFF) == 168) {
        tft.setTextColor(TFT_GREEN);
        tft.printf("OK\n");
    } else {
        tft.setTextColor(TFT_RED);
        tft.printf("FAIL\n");
    }
    tft.setTextColor(FG_COLOR);
    return true;
}

/**
 * @brief Initialize HTTP server routes.
 */
void initServerRoutes() {
    server.on("/", handleRoot);
    server.on("/ALLSENSORS", handleALL);
    server.on("/POST", handlePost);
    server.on("/REQUESTUPDATE", handleREQUESTUPDATE);
    server.on("/CLEARSENSOR", handleCLEARSENSOR);
    server.on("/TIMEUPDATE", handleTIMEUPDATE);
    server.on("/REQUESTWEATHER", handleREQUESTWEATHER);
    server.on("/GETSTATUS", handleGETSTATUS);
    server.on("/REBOOT", handleReboot);
    server.on("/UPDATEDEFAULTS", handleUPDATEDEFAULTS);
    server.on("/RETRIEVEDATA", handleRETRIEVEDATA);
    server.on("/FLUSHSD", handleFLUSHSD);
    server.on("/SETWIFI", HTTP_POST, handleSETWIFI);
    server.onNotFound(handleNotFound);
}

/**
 * @brief Initialize Arduino OTA update functionality.
 */
void initOTA() {
    tft.print("Connecting ArduinoOTA... ");
    ArduinoOTA.setHostname("WeatherStation");
    ArduinoOTA.setPassword("12345678");
    ArduinoOTA.onStart([]() { displayOTAProgress(0, 100); });
    ArduinoOTA.onEnd([]() { tft.setTextSize(1); tft.println("OTA End. About to reboot!"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { displayOTAProgress(progress, total); });
    ArduinoOTA.onError([](ota_error_t error) { displayOTAError(error); });
    ArduinoOTA.begin();
    tft.setTextColor(TFT_GREEN);
    tft.printf("OK.\n");
    tft.setTextColor(FG_COLOR);
}

/**
 * @brief Print setup status and wait for weather to load.
 */
void printSetupStatus() {
    tft.setTextColor(TFT_GREEN);
    tft.printf("Setup OK...");
    tft.setTextColor(FG_COLOR);
    tft.printf("Waiting for weather to load...");
}

BootSecure bootSecure;

// --- Main Setup ---
void setup() {
    // --- Boot Security Check ---
    if (!bootSecure.setup()) {
        // Security check failed, halt device
        while (1) { delay(1000); }
    }
    // Watchdog
    esp_task_wdt_deinit();
    esp_task_wdt_init(WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);

    WEBHTML.reserve(7000);
    #ifdef _DEBUG
    Serial.begin(115200);
    #endif

    initDisplay();
    initSensor(-256);
    if (!initSDCard()) return;
    loadSensorData();
    loadScreenFlags();

    if (Prefs.DEVICENAME[0] == 0) {
        snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), MYNAME);
        // Save Prefs with the new device name
        bootSecure.setPrefs();
    }
    I.ScreenNum = 0;
    I.isFlagged = false;
    I.wasFlagged = false;
    WeatherData.lat = LAT;
    WeatherData.lon = LON;

    initServerRoutes();
    if (!initWiFi()) return;

    tft.printf("Init server... ");
    server.begin();
    tft.setTextColor(TFT_GREEN);
    tft.printf(" OK.\n");
    tft.setTextColor(FG_COLOR);

    tft.print("Initializing ESPNow... ");
    if (initESPNOW()) {
        tft.setTextColor(TFT_GREEN);
        tft.printf("OK.\n");
        tft.setTextColor(FG_COLOR);
        broadcastServerPresence();
    } else {
        tft.setTextColor(TFT_RED);
        tft.printf("FAILED.\n");
        tft.setTextColor(FG_COLOR);
        storeError("ESPNow initialization failed");
    }

    initOTA();
    tft.print("Set up time... ");
    if (setupTime()) {
        displaySetupProgress("Set up time", true);
    } else {
        displaySetupProgress("Set up time", false);
    }
    I.ALIVESINCE = I.currentTime;
    printSetupStatus();
}

// Non-graphical functions
void checkHeat() {
  // Check if any HVAC sensors are active
  I.isHeat = 0;
  I.isAC = 0;
  I.isFan = 0;
  
  // Iterate through all devices and sensors with bounds checking
  for (int16_t deviceIndex = 0; deviceIndex < NUMDEVICES && deviceIndex < Sensors.getNumDevices(); deviceIndex++) {
    DevType* device = Sensors.getDeviceByIndex(deviceIndex);
    if (!device || !device->IsSet) continue;
    
    for (int16_t sensorIndex = 0; sensorIndex < NUMSENSORS && sensorIndex < Sensors.getNumSensors(); sensorIndex++) {
      SnsType* sensor = Sensors.getSensorByIndex(sensorIndex);
      if (!sensor || !sensor->IsSet) continue;
      
      // Check HVAC sensors (types 50-59)
      if (sensor->snsType >= 50 && sensor->snsType < 60) {
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
      }
    }
  }
}

/**
 * @brief Handle periodic ESPNow server presence broadcast (every 5 minutes).
 */
void handleESPNOWPeriodicBroadcast(uint8_t interval = 5) {
    static int lastBroadcastMinute = -1;
    int currentMinute = minute();
    if (currentMinute % interval == 0 && currentMinute != lastBroadcastMinute) {
        lastBroadcastMinute = currentMinute;
        // ESPNow does not require WiFi connection; always broadcast
        broadcastServerPresence();
    }
}

// --- Main Loop ---
void loop() {
    esp_task_wdt_reset();
    I.currentTime = now();
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiDownSince == 0) wifiDownSince = I.currentTime;
        byte retries = connectWiFi();
        if (WiFi.status() != WL_CONNECTED) I.wifiFailCount++;
        // If WiFi has been down for 15 minutes, reboot
        if (wifiDownSince && (I.currentTime - wifiDownSince >= 900)) {
            controlledReboot("Wifi failed for 15 min, rebooting", RESET_WIFI, true);
        }
    } else {
        wifiDownSince = 0;
        I.wifiFailCount = 0;
        ArduinoOTA.handle();
        server.handleClient();
        updateTime(1, 0);
    }
    // --- Periodic Tasks ---
    if (OldTime[1] != minute()) {
        if (I.wifiFailCount > 5) controlledReboot("Wifi failed so resetting", RESET_WIFI, true);
        WeatherData.updateWeather(3600);
        if (WeatherData.lastUpdateAttempt > WeatherData.lastUpdateT + 300 && I.currentTime - I.ALIVESINCE > 10800) {
            tft.clear();
            tft.setCursor(0, 0);
            tft.setTextColor(TFT_RED);
            setFont(2);
            tft.printf("Weather failed \nfor 60 minutes.\nRebooting in 3 seconds...");
            delay(3000);
            I.resetInfo = RESET_WEATHER;
            I.lastResetTime = I.currentTime;
            storeError("Weather failed too many times");
            controlledReboot("Weather failed too many times", RESET_WEATHER);
        }
        #ifdef _DEBUG
        if (I.flagViewTime == 0) {
            Serial.printf("Loop update weather: Time is: %s and a critical failure occurred. This was %u seconds since start.\n", dateify(I.currentTime, "mm/dd/yyyy hh:mm:ss"), I.currentTime - I.ALIVESINCE);
            tft.clear();
            tft.setCursor(0, 0);
            tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n", dateify(I.currentTime, "mm/dd/yyyy hh:mm:ss"), I.currentTime - I.ALIVESINCE);
            while (true);
        }
        #endif
        OldTime[1] = minute();
        I.isFlagged = 0;
        I.isSoilDry = 0;
        I.isHot = 0;
        I.isCold = 0;
        I.isLeak = 0;
        I.isExpired = 0;
        I.isFlagged = countFlagged(-1, 0b00000111, 0b00000011, 0);
        checkHeat();
        I.isSoilDry = countFlagged(3, 0b00000111, 0b00000011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isHot = countFlagged(-2, 0b00100111, 0b00100011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isCold = countFlagged(-2, 0b00100111, 0b00000011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isLeak = countFlagged(70, 0b00000001, 0b00000001, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isExpired = checkExpiration(-1, I.currentTime, true);
        I.isFlagged += I.isExpired;
        if (minute() % 5 == 0) {
            writeSensorsSD();
            storeScreenInfoSD();
        }
    }
    if (OldTime[2] != hour()) {
        OldTime[2] = hour();
        checkExpiration(-1, I.currentTime, false);
    }
    if (OldTime[3] != weekday()) {
        OldTime[3] = weekday();
    }
    if (OldTime[0] != second()) {
        OldTime[0] = second();
        fcnDrawScreen();
        #ifdef _DEBUG
        if (I.flagViewTime == 0) {
            Serial.printf("Loop drawscreen: Time is: %s and a critical failure occurred. This was %u seconds since start.\n", dateify(I.currentTime, "mm/dd/yyyy hh:mm:ss"), I.currentTime - I.ALIVESINCE);
            tft.clear();
            tft.setCursor(0, 0);
            tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n", dateify(I.currentTime, "mm/dd/yyyy hh:mm:ss"), I.currentTime - I.ALIVESINCE);
            while (true);
        }
        #endif
        #ifdef _DEBUG
        if (WeatherData.getTemperature(I.currentTime + 3600) < 0 && TESTRUN < 3600) {
            Serial.printf("Loop %s: Weather data just failed\n", dateify(t));
            WTHRFAIL = I.currentTime;
            TESTRUN = 3600;
        }
        #endif
    }
    handleESPNOWPeriodicBroadcast(5);
}

// --- WiFi Down Timer ---
static uint32_t wifiDownSince = 0;
static uint32_t lastPwdRequestMinute = 0;

