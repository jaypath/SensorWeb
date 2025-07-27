
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
#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include "graphics.hpp"
#include "AddESPNOW.hpp"
#include "BootSecure.hpp"
#include "devices.hpp"

// --- WiFi Down Timer ---
static uint32_t wifiDownSince = 0;
static uint32_t lastPwdRequestMinute = 0;

#define WDT_TIMEOUT_MS 120000


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
// SECSCREEN and HourlyInterval are now members of Screen struct (I.SECSCREEN, I.HourlyInterval)
extern STRUCT_CORE I;
extern String WEBHTML;
//extern uint8_t OldTime[4];
extern WeatherInfoOptimized WeatherData;

extern uint32_t LAST_WEB_REQUEST;
extern double LAST_BAR;

uint32_t FONTHEIGHT = 0;

//time
uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d

//fuction declarations


// --- Helper Function Declarations ---
void initDisplay();
void initScreenFlags(bool completeInit = false);
bool initSDCard();
bool loadSensorData();
bool loadScreenFlags();
void initServerRoutes();
void initOTA();
void handleESPNOWPeriodicBroadcast(uint8_t interval);

// --- Setup Helper Implementations ---

/**
 * @brief Initialize the TFT display and SPI.
 */
void initScreenFlags(bool completeInit) {
    if (completeInit) {

        deleteFiles("ScreenFlags.dat","/Data");

        I.rebootsSinceLast=0;
        I.wifiFailCount=0;
        I.currentTime=0;
        I.CLOCK_Y = 105;
        I.HEADER_Y = 30;
        
        I.cycleHeaderMinutes = 30; //how many seconds to show header?
        I.cycleCurrentConditionMinutes = 10; //how many minutes to show current condition?
        I.cycleWeatherMinutes = 10; //how many minutes to show weather values?
        I.cycleFutureConditionsMinutes = 10; //how many minutes to show future conditions?
        I.cycleFlagSeconds = 3; //how many seconds to show flag values?
        I.IntervalHourlyWeather = 2; //hours between daily weather display
        I.screenChangeTimer = 30; //how many seconds before screen changes back to main screen

        I.isExpired = false; //are any critical sensors expired?
        I.wasFlagged=false;
        I.isHeat=false; //first bit is heat on, bits 1-6 are zones
        I.isAC=false; //first bit is compressor on, bits 1-6 are zones
        I.isFan=false; //first bit is fan on, bits 1-6 are zones
        I.wasHeat=false; //first bit is heat on, bits 1-6 are zones
        I.wasAC=false; //first bit is compressor on, bits 1-6 are zones
        I.wasFan=false; //first bit is fan on, bits 1-6 are zones

        I.isHot=0;
        I.isCold=0;
        I.isSoilDry=0;
        I.isLeak=0;
        I.localWeatherIndex=255; //index of outside sensor
        I.localBatteryIndex=255;

        I.showTheseFlags=(1<<3) + (1<<2) + (1<<1) + 1; //bit 0 = 1 for flagged only, bit 1 = 1 include expired, bit 2 = 1 include soil alarms, bit 3 =1 include leak, bit 4 =1 include temperature, bit 5 =1 include  RH, bit 6=1 include pressure, 7 = 1 include battery, 8 = 1 include HVAC

        I.currentTemp-127;
        I.Tmax=-127;
        I.Tmin=127;
        I.lastErrorTime=0;
    }
    I.ScreenNum = 0;
    I.oldScreenNum = 0;

    I.lastHeaderTime=0; //last time header was drawn
    I.lastWeatherTime=0; //last time weather was drawn
    I.lastCurrentConditionTime=0; //last time current condition was drawn
    I.lastClockDrawTime=0; //last time clock was updated, whether flag or not
    I.lastFutureConditionTime=0; //last time future condition was drawn
    I.lastFlagViewTime=0; //last time clock was updated, whether flag or not
    
    I.DSTOFFSET = 0;
    I.GLOBAL_TIMEZONE_OFFSET = -18000;
    
    I.localBatteryLevel=0;

    I.lastESPNOW=0;
    I.lastResetTime=I.currentTime;
    I.ALIVESINCE=I.currentTime;
    I.wifiFailCount=0;
    I.ScreenNum = 0;
    I.isFlagged = false;


    if (completeInit) {
        storeScreenInfoSD();
    }
}


/**
 * @brief Initialize the TFT display and SPI.
 */
void initDisplay() {
    SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
    tft.init();
    tft.setRotation(2);
    tft.setCursor(0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setTextFont(1);
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
        displaySetupProgress(false);
        
        SerialPrint("SD mount failed... ",true);
        
        delay(5000);
        I.resetInfo = RESET_SD;
        I.lastResetTime = I.currentTime;
        controlledReboot("SD Card failed", RESET_SD, true);
        return false;
    }
    SerialPrint("SD mounted... ",true);
    displaySetupProgress(true);
    return true;
}

/**
 * @brief Load sensor data from SD card.
 * @return true if successful, false otherwise.
 */
bool loadSensorData() {
    tft.print("Loading sensor data from SD... ");
    bool sdread = Sensors.readAllSensors();
    displaySetupProgress( sdread);
    if (!sdread) delay(5000);
    #ifdef _USESERIAL
    SerialPrint("Sensor data loaded? ",false);
    SerialPrint((sdread==true)?"yes":"no",true);
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
    displaySetupProgress( sdread);
    if (!sdread) {
        delay(5000);
        initScreenFlags(true);
    }
    initScreenFlags(false);
    return sdread;
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
    server.on("/REBOOT", handleReboot);
    server.on("/STATUS", handleSTATUS);
    server.on("/RETRIEVEDATA", handleRETRIEVEDATA);
    server.on("/RETRIEVEDATA_MOVINGAVERAGE", handleRETRIEVEDATA_MOVINGAVERAGE);
    server.on("/FLUSHSD", handleFLUSHSD);
    server.on("/SETWIFI", HTTP_POST, handleSETWIFI);
    server.on("/CONFIG", HTTP_GET, handleCONFIG);
    server.on("/CONFIG", HTTP_POST, handleCONFIG_POST);
    server.on("/CONFIG_DELETE", HTTP_POST, handleCONFIG_DELETE);
    server.on("/WiFiConfig", HTTP_GET, handleWiFiConfig);
    server.on("/WiFiConfig", HTTP_POST, handleWiFiConfig_POST);
    server.on("/WiFiConfig_RESET", HTTP_POST, handleWiFiConfig_RESET);
    server.on("/WEATHER", HTTP_GET, handleWeather);
    server.on("/WEATHER", HTTP_POST, handleWeather_POST);
    server.on("/WeatherRefresh", HTTP_POST, handleWeatherRefresh);
    server.on("/WeatherZip", HTTP_POST, handleWeatherZip);
    server.on("/WeatherAddress", HTTP_POST, handleWeatherAddress);
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



BootSecure bootSecure;

// --- Main Setup ---
void setup() {
    // --- Boot Security Check ---
    // Watchdog
    esp_task_wdt_deinit();
    esp_task_wdt_init(WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);


    #ifdef _USESERIAL
    Serial.begin(115200);
    #endif

    WEBHTML.reserve(7000);

    initDisplay();
    int8_t boot_status = bootSecure.setup();
    if (boot_status <= 0) {
        #ifdef SETSECURE
            // Security check failed for any reason, halt device
            while (1) { delay(1000); }
        #endif
        tft.println("Prefs failed to load with error code: " + String(boot_status));
        delay(5000);
    }

    initSensor(-256);

    if (!initSDCard()) return;
    loadScreenFlags();
    loadSensorData();


    tft.printf("Init Wifi... \n");
    tft.printf("Wifi... ");    
    initServerRoutes();
    while (connectWiFi()<0) {
        displaySetupProgress( false);
        tft.clear();
        tft.setCursor(0, 0);
        tft.printf("Wifi... ");    
        delay(10000); //do not flood wifi        
    } 
    displaySetupProgress( true);


    tft.print("Set up time... ");
    if (setupTime()) {
        displaySetupProgress( true);
    } else {
        displaySetupProgress( false);
    }
    I.ALIVESINCE = I.currentTime;
    
    
    if (Prefs.DEVICENAME[0] == 0) {
        snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), MYNAME);
        Prefs.isUpToDate = false;
        // Save Prefs with the new device name
        bootSecure.setPrefs();
    }
    WeatherData.lat = Prefs.LAT;
    WeatherData.lon = Prefs.LON;


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

    tft.printf("Loading weather data...\n");
    //load weather data from SD card
    if (readWeatherDataSD()) {
        if (WeatherData.updateWeatherOptimized(3600)>0) {
            SerialPrint("Weather data loaded from SD card",true);
        } else {
            SerialPrint("Weather data loaded from SD card, but data is stale. Trying to update from NOAA.",true);
        }
    } else {
        SerialPrint("Weather data not found on SD card, updating from NOAA.",true);
        WeatherData.updateWeatherOptimized(3600);
    }
    tft.setTextColor(TFT_GREEN);
    tft.printf("Setup OK...");
    tft.setTextColor(FG_COLOR);
    
}

// Non-graphical functions

/**
 * @brief Handle periodic ESPNow server presence broadcast (every 5 minutes).
 */
void handleESPNOWPeriodicBroadcast(uint8_t interval) {    
    if (minute() % interval == 0 && I.lastESPNOW!=I.currentTime) {        
        // ESPNow does not require WiFi connection; always broadcast
        broadcastServerPresence();
    }
}

// --- Main Loop ---
void loop() {
    esp_task_wdt_reset();

    updateTime(); //sets I.currenttime
    
    checkTouchScreen();
    

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiDownSince == 0) wifiDownSince = I.currentTime;
        int16_t retries = connectWiFi();
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
    }

    // --- Periodic Tasks ---
    if (OldTime[1] != minute()) {
        if (I.wifiFailCount > 5) controlledReboot("Wifi failed so resetting", RESET_WIFI, true);

        // WEATHER OPTIMIZATION - Use optimized weather update method
        byte weatherResult = WeatherData.updateWeatherOptimized(3600);  // Optimized weather update with a sync interval of 3600 sec = 1 hr
        if (weatherResult == 0) {
            SerialPrint("Weather data is still fresh",true);
        } else if (weatherResult == 1) {
            SerialPrint("Weather data updated successfully",true);
        } else if (weatherResult == 2) {
            SerialPrint("Weather data update failed, too soon to retry",true);
        } else {
            SerialPrint("Weather data update failed",true);
        }

        //see if we have local weather
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 4);  //if no local weather sensor, use outside
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 1);  //if no local weather sensor, use outside dht
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 10);  //if no local weather sensor, use outside bmp
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 14);  //if no local weather sensor, use outside bme
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 17);  //if no local weather sensor, use outside bme680

        if (I.localWeatherIndex!=255) {
            SnsType* sensor = Sensors.getSensorBySnsIndex(I.localWeatherIndex);
            
            if (sensor->timeLogged + 30*60<I.currentTime)     I.localWeatherIndex = 255;
            else {
                I.currentTemp = sensor->snsValue;
                //SerialPrint((String) "Local weather device found, snsindex" + I.localWeatherIndex, true + ", snsValue=" + I.currentTemp);
            }
        }
        if (I.localWeatherIndex==255) I.currentTemp = WeatherData.getTemperature(I.currentTime);

        //see if we have local battery power
        if (I.localBatteryIndex == 255) I.localBatteryIndex = findSensorByName("Outside",61);
    
        if (I.localBatteryIndex<255 && I.localBatteryIndex >= 0 && I.localBatteryIndex < NUMDEVICES * SENSORNUM) {
            // Find the battery sensor
            DevType* batDevice = Sensors.getDeviceBySnsIndex(I.localBatteryIndex);
            SnsType* batSensor = Sensors.getSensorBySnsIndex(I.localBatteryIndex);
            if (batDevice && batSensor && batDevice->IsSet && batSensor->IsSet && batSensor->timeLogged + 3600 > I.currentTime) {
                I.localBatteryLevel = batSensor->snsValue;
                //SerialPrint((String) "Local battery device found, snsindex" + I.localBatteryIndex, true + ", snsValue=" + I.localBatteryLevel);
            }             else {
                I.localBatteryIndex = 255;
                I.localBatteryLevel = -1;
            }
        }

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
        if (minute() % 5 == 0 && !Sensors.isUpToDate) { 
            Sensors.storeAllSensors();        
        }
    }
    if (OldTime[2] != hour()) {
        OldTime[2] = hour();
        checkExpiration(-1, I.currentTime, false);
        if (OldTime[2] == 4) {
            //check if DST has changed every day at 4 am
            checkDST();
        }
    }
    if (OldTime[3] != weekday()) {
        OldTime[3] = weekday();
    }
    if (OldTime[0] != second()) {
        OldTime[0] = second();

        
        // Check if Prefs needs to be saved
        if (!Prefs.isUpToDate) {
            Prefs.isUpToDate = true;
            bootSecure.setPrefs();
        }

        if (!I.isUpToDate) {
            I.isUpToDate = true;
            storeScreenInfoSD();
        }

        //sensors is updated every 5 minutes if needed

        fcnDrawScreen();
        if (I.screenChangeTimer > 0) I.screenChangeTimer--;

    }
    handleESPNOWPeriodicBroadcast(5);

}



