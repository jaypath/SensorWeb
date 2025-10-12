
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

#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
#endif
#ifdef _USEFIREBASE
#include "FirebaseUpload.hpp"
#endif

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
#ifdef _USEGSHEET
extern STRUCT_GOOGLESHEET GSheetInfo;
#endif

//extern uint8_t OldTime[4];
extern WeatherInfoOptimized WeatherData;

extern uint32_t LAST_WEB_REQUEST;
extern double LAST_BAR;

uint32_t FONTHEIGHT = 0;
BootSecure bootSecure;

//time
uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d

//fuction declarations


// --- Helper Function Declarations ---
void initSystem();

bool initSDCard();
bool loadSensorData();
bool loadScreenFlags();
void initServerRoutes();
void initOTA();
void handleESPNOWPeriodicBroadcast(uint8_t interval);

// --- Setup Helper Implementations ---


#ifdef _USEGSHEET
/**
 * @brief Initialize the Gsheet uploader 
 */
void initGsheetHandler() {
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
}
#endif

/**
 * @brief Initialize the TFT display and SPI.
 */


/**
 * @brief Initialize the TFT display and SPI.
 */
void initSystem() {
    #ifdef _USETFT
    SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
    tft.init();
    tft.setRotation(2);
    tft.setCursor(0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    tft.printf("Running setup\n");
    #endif
    int8_t boot_status = bootSecure.setup();
    if (boot_status <= 0) {
        #ifdef SETSECURE
            // Security check failed for any reason, halt device
            while (1) { delay(1000); }
        #endif
        tft.println("Prefs failed to load with error code: " + String(boot_status));
        delay(1000);
    }

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
    bool sdread = Sensors.readDevicesSensorsArrayFromSD();
    displaySetupProgress( sdread);
    SerialPrint("Sensor data loaded? ",false);
    SerialPrint((sdread==true)?"yes":"no",true);
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
    } else     initScreenFlags(false);
    SerialPrint("Screen flags loaded? ",false);
    SerialPrint((sdread==true)?"yes":"no",true);
    return sdread;
}


/**
 * @brief Initialize HTTP server routes.
 * This function is now a wrapper that calls setupServerRoutes() from server.cpp
 */
void initServerRoutes() {
    setupServerRoutes();
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




// --- Main Setup ---
void setup() {
    // --- Boot Security Check ---
    // Watchdog
    esp_task_wdt_deinit();
    esp_task_wdt_init(WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);


    #ifdef _USESERIAL
    Serial.begin(115200);
    Serial.println("Serial started");
    SerialPrint("SerialPrint started",true);
    #endif


    WEBHTML.reserve(20000);

    initSystem();

    initSensor(-256);

    if (!initSDCard()) return;
    loadScreenFlags();
    loadSensorData();


    tft.printf("Init Wifi... \n");
    SerialPrint("Init Wifi... ",false);
    SerialPrint("start server routes... ");
    initServerRoutes();
    SerialPrint("Server routes OK",true);

    if (Prefs.HAVECREDENTIALS) {

        if (connectWiFi()<0) {
            //if connectWiFi returned -10000, then we are in AP mode and handled elsewhere
            SerialPrint("Failed to connect to Wifi",true);
            if (connectWiFi()>-10000 && connectWiFi()<0) {
                tft.clear();
                tft.setCursor(0, 0);
                tft.printf("Wifi failed too many times,\npossibly due to incorrect credentials.\nRebooting into local mode... ");  
                delay(30000);  
                Prefs.HAVECREDENTIALS = false;
                APStation_Mode();
            }        
        } 
    } else {
        SerialPrint("No credentials, starting AP Station Mode",true);
        APStation_Mode();
    }
    displaySetupProgress( true);
    SerialPrint("Wifi OK",true);


    tft.print("Set up time... ");
    if (setupTime()) {
        displaySetupProgress( true);
    } else {
        displaySetupProgress( false);
    }
    delay(100);
    tft.printf("Current time = %s\n", dateify(now(),"yyyy-mm-dd hh:nn:ss"));
    SerialPrint("Current time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")),true);
    I.currentTime = now();
    I.ALIVESINCE = I.currentTime;
    
    // Check for unexpected reboot by comparing previous ALIVESINCE with current time
    // The logic works as follows:
    // 1. On normal operation, ALIVESINCE is set to current time during setup
    // 2. If an unexpected reboot occurs, the previous ALIVESINCE value will be loaded from SD
    // 3. When we set ALIVESINCE again, if it's significantly different from the loaded value,
    //    it indicates an unexpected reboot occurred
    if (I.lastResetTime != 0 && I.ALIVESINCE != 0) {
        // If the previous ALIVESINCE is significantly different from current time, 
        // this indicates an unexpected reboot occurred
        time_t timeDiff = I.currentTime - I.ALIVESINCE;
        if (timeDiff > 300) { // If more than 5 minutes difference, consider it unexpected
            I.resetInfo = RESET_UNKNOWN;
            I.lastResetTime = I.currentTime;
            SerialPrint("Unexpected reboot detected! Previous ALIVESINCE: " + String(I.ALIVESINCE) + 
                       ", Current time: " + String(I.currentTime) + 
                       ", Time difference: " + String(timeDiff) + " seconds", true);
            storeScreenInfoSD(); // Save the updated reset info
        }
    } else if (I.lastResetTime == 0) {
        // This is likely the first boot, set initial values
        I.resetInfo = RESET_DEFAULT;
        I.lastResetTime = I.currentTime;
        SerialPrint("First boot detected, setting initial reset info", true);
    }
    
    SerialPrint("Current IP Address: " + WiFi.localIP().toString(),true);
    SerialPrint("Prefs.MYIP: " + Prefs.MYIP.toString(),true);

    byte deviceIndex = Sensors.findMyDeviceIndex();
    if (deviceIndex == -1) {
        SerialPrint("I am not registered as a device, and could not register, so I cannot run...",true);
        tft.clear();
        tft.setCursor(0, 0);
        tft.setTextColor(TFT_RED);
        tft.printf("Unable to register as a device, so I cannot run...",true);
        delay(10000);
        controlledReboot("Unable to register myself as a device, so I cannot run...", RESET_UNKNOWN);
        return;
    }
    
    if (Prefs.DEVICENAME[0] == 0) {
        snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), MYNAME);
        Prefs.isUpToDate = false;        

        //update my device name
        Sensors.addDevice(ESP.getEfuseMac(), WiFi.localIP(), Prefs.DEVICENAME, 0, 0, MYTYPE);
    }
    WeatherData.lat = Prefs.LATITUDE;
    WeatherData.lon = Prefs.LONGITUDE;

    //check if I am registered as a device

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

    #ifdef _USEGSHEET
    if (GSheetInfo.useGsheet) {
        tft.print("Initializing Gsheet... ");
        initGsheetHandler();
        if (GSheet.ready()) {
            tft.setTextColor(TFT_GREEN);
            tft.printf("OK.\n");
            tft.setTextColor(FG_COLOR);
        } else {
            tft.setTextColor(TFT_RED);
            tft.printf("FAILED.\n");
            tft.setTextColor(FG_COLOR);
            storeError("Gsheet initialization failed");
        }
    }
    else {
        tft.setTextColor(TFT_GREEN);
        tft.printf("Skipping Gsheet initialization.\n");
        tft.setTextColor(FG_COLOR);
    }
    #endif



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
    if (minute() % interval == 0 && I.ESPNOW_LAST_OUTGOINGMSG_TIME!=I.currentTime) {        
        // ESPNow does not require WiFi connection; always broadcast
        broadcastServerPresence();
    }
}

void handleStoreCoreData() {
    if (!Prefs.isUpToDate) { 
        Prefs.isUpToDate = true;
        BootSecure bootSecure;
        if (bootSecure.setPrefs()<0) {
            SerialPrint("Failed to store core data",true);
        }
    }

    if (!I.isUpToDate && I.lastStoreCoreDataTime + 300 < I.currentTime) { //store if out of date and more than 5 minutes since last store
        I.isUpToDate = true;
        storeScreenInfoSD();
    }
}


// --- Main Loop ---
void loop() {
    esp_task_wdt_reset();

    updateTime(); //sets I.currenttime

    
    checkTouchScreen();

    #ifdef _USEGSHEET
    GSheet.ready(); //maintains authentication
    #endif
    

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
        if (weatherResult > 3) {
            SerialPrint("Weather data has an unknown status",true);
        } else if (weatherResult == 1) {
            SerialPrint("Weather updated successfully",true);
        } else if (weatherResult == 2) {
            SerialPrint("Weather update: too soon to retry",true);
        } else if (weatherResult == 3) {
            SerialPrint("Weather update: data is still fresh",true);    
        } else {
            SerialPrint("Weather update: error code " + (String) weatherResult,true);
        }

        //see if we have local weather
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 4);  //if no local weather sensor, use outside
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 1);  //if no local weather sensor, use outside dht
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 10);  //if no local weather sensor, use outside bmp
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 14);  //if no local weather sensor, use outside bme
        if (I.localWeatherIndex==255)     I.localWeatherIndex=findSensorByName("Outside", 17);  //if no local weather sensor, use outside bme680


        if (I.localWeatherIndex!=255) {
            SnsType* sensor = Sensors.getSensorBySnsIndex(I.localWeatherIndex);
            if (!sensor || !sensor->IsSet) {
                I.localWeatherIndex = 255;
            } else {
                if (sensor->timeLogged + 30*60<I.currentTime)     I.localWeatherIndex = 255;
                else {
                    if (I.currentTemp != sensor->snsValue) {
                        I.currentTemp = sensor->snsValue;
                        I.lastCurrentConditionTime = 0; //force redraw of current condition
                    }
                    //SerialPrint((String) "Local weather device found, snsindex" + I.localWeatherIndex, true + ", snsValue=" + I.currentTemp);
                }
            }
        }
        int8_t currenttemp = WeatherData.getTemperature(I.currentTime);
        if (I.localWeatherIndex==255 || abs(I.currentTemp-currenttemp)>15) {
            if (I.currentTemp != currenttemp) {   
                I.currentTemp = currenttemp;
                I.lastCurrentConditionTime = 0; //force redraw of current condition
            }
        }
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
            SerialPrint("Weather failed for 60 minutes. Rebooting in 3 seconds...",true);
            delay(3000);
            I.resetInfo = RESET_WEATHER;
            I.lastResetTime = I.currentTime;
            storeError("Weather failed too many times");
            controlledReboot("Weather failed too many times", RESET_WEATHER);
        }

        if (I.currentTime - Sensors.lastSDSaveTime > 600) {
            storeDevicesSensorsSD();
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

        handleESPNOWPeriodicBroadcast(5);
        handleStoreCoreData();
        Sensors.storeDevicesSensorsArrayToSD(5);
        #ifdef _USEGSHEET

        uint8_t gsheetResult = Gsheet_uploadData();

        if (gsheetResult<0) {
            SerialPrint(GsheetUploadErrorString(),true);
        } 
        #endif
    }

    if (OldTime[2] != hour()) {
        OldTime[2] = hour();


        checkExpiration(-1, I.currentTime, false);

        Sensors.storeAllSensorsSD(); //store all sensors to SD card once an hour


        if (OldTime[2] == 4) {
            //check if DST has changed every day at 4 am
            checkDST();
        }
    }
    if (OldTime[3] != weekday()) {
        OldTime[3] = weekday();
        checkTimezoneUpdate();
        I.ESPNOW_SENDS = 0;
        I.ESPNOW_RECEIVES = 0;
    }
    if (OldTime[0] != second()) {
        OldTime[0] = second();


        fcnDrawScreen();
        if (I.screenChangeTimer > 0) I.screenChangeTimer--;
        else I.ScreenNum=0;


    }
    

}



