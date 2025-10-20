
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

// --- WiFi Down Timer ---
static uint32_t wifiDownSince = 0;
static uint32_t lastPwdRequestMinute = 0;



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

uint16_t mydeviceindex = 0; //local stored index of my device
extern LGFX tft;
// SECSCREEN and HourlyInterval are now members of Screen struct (I.SECSCREEN, I.HourlyInterval)
extern STRUCT_CORE I;
extern String WEBHTML;
#ifdef _USEGSHEET
extern STRUCT_GOOGLESHEET GSheetInfo;
#endif

#ifdef _USEWEATHER
//extern uint8_t OldTime[4];
extern WeatherInfoOptimized WeatherData;
#endif

extern double LAST_BAR;

uint32_t FONTHEIGHT = 0;
BootSecure bootSecure;

//time
uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d

//function declarations
void initOTA();

/**
 * @brief Initialize Arduino OTA update functionality.
 */
void initOTA() {
    tftPrint("Connecting ArduinoOTA... ", false);
    ArduinoOTA.setHostname("WeatherStation");
    ArduinoOTA.setPassword("12345678");
    #ifdef _USETFT
    ArduinoOTA.onStart([]() { displayOTAProgress(0, 100); });
    ArduinoOTA.onEnd([]() {     tft.setTextSize(1); tft.println("OTA End. About to reboot!"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { displayOTAProgress(progress, total); });
    ArduinoOTA.onError([](ota_error_t error) { displayOTAError(error); });
    #endif
    ArduinoOTA.begin();

    tftPrint("OK.", true, TFT_GREEN);
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


    int8_t sdResult = initSDCard();
    if (sdResult==0) return;
    if (sdResult==-1) {
        tftPrint("SD Card not supported", true, TFT_RED, 2, 1, true, 0, 0);
    } else {
        loadScreenFlags();
        loadSensorData();
    }


    tftPrint("Init Wifi... \n", true);
    SerialPrint("Init Wifi... ",false);
    SerialPrint("start server routes... ");
    setupServerRoutes();
    SerialPrint("Server routes OK",true);

    if (Prefs.HAVECREDENTIALS) {

        if (connectWiFi()<0) {
            //if connectWiFi returned -10000, then we are in AP mode and handled elsewhere
            SerialPrint("Failed to connect to Wifi",true);
            if (connectWiFi()>-10000 && connectWiFi()<0) {
                tftPrint("Wifi failed too many times,\npossibly due to incorrect credentials.\nRebooting into local mode... ", true, TFT_RED, 2, 1, true, 0, 0);  
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


    tftPrint("Set up time... ", false, TFT_WHITE, 2, 1, false, -1, -1);
    if (setupTime()) {
        displaySetupProgress( true);
    } else {
        displaySetupProgress( false);
    }
    delay(100);
    tftPrint("Current time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")), true, TFT_WHITE, 2, 1, false, -1, -1);
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
    
    byte deviceIndex = Sensors.findMyDeviceIndex();
    if (deviceIndex == -1) {
        failedToRegister();
        return;
    }
    
    if (Prefs.DEVICENAME[0] == 0) {
        if (Sensors.findMyDeviceIndex() == -1) failedToRegister();
    }

    //check if I am registered as a device

    tftPrint("Init server... ", false, TFT_WHITE, 2, 1, false, -1, -1);
    server.begin();
    tftPrint(" OK.", true, TFT_GREEN);

    tftPrint("Initializing ESPNow... ", false, TFT_WHITE, 2, 1, false, -1, -1);
    if (initESPNOW()) {
        tftPrint("OK.", true, TFT_GREEN);
        broadcastServerPresence();
    } else {
        tftPrint("FAILED.", true, TFT_RED);
        storeError("ESPNow initialization failed");
    }

    initOTA();

    #ifdef _USEGSHEET
    if (GSheetInfo.useGsheet) {
        tftPrint("Initializing Gsheet... ", false, TFT_WHITE, 2, 1, false, -1, -1);
        initGsheetHandler();
        if (GSheet.ready()) {
            tftPrint("OK.", true, TFT_GREEN);
        } else {
            tftPrint("FAILED.", true, TFT_RED);
            storeError("Gsheet initialization failed");
        }
    }
    else {
        tftPrint("Skipping Gsheet initialization.", true, TFT_GREEN);
    }
    #endif


    #ifdef _ISPERIPHERAL
    initHardwareSensors();
    #endif


    
    #ifdef _USEWEATHER
    tftPrint("Loading weather data...", false, TFT_WHITE, 2, 1, false, -1, -1);
//load weather data from SD card
    if (readWeatherDataSD()) {
        WeatherData.lat = Prefs.LATITUDE;
        WeatherData.lon = Prefs.LONGITUDE;
    
        if (WeatherData.updateWeatherOptimized(3600)>0) {
            SerialPrint("Weather data loaded from SD card",true);
        } else {
            SerialPrint("Weather data loaded from SD card, but data is stale. Trying to update from NOAA.",true);
        }
    } else {
        SerialPrint("Weather data not found on SD card, updating from NOAA.",true);
        WeatherData.updateWeatherOptimized(3600);
    }
    tftPrint("Setup OK.", true, TFT_GREEN);
    #endif

    
}




// --- Main Loop ---
void loop() {
    esp_task_wdt_reset();

    updateTime(); //sets I.currenttime

    #ifdef _USETFT
    checkTouchScreen();
    #endif

    #ifdef _USEGSHEET
    GSheet.ready(); //maintains authentication
    #endif
    

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiDownSince == 0) wifiDownSince = I.currentTime;
        int16_t retries = connectWiFi();
        if (WiFi.status() != WL_CONNECTED) I.wifiFailCount++;
        // If WiFi has been down for 10 minutes, enter AP mode
        if (wifiDownSince && (I.currentTime - wifiDownSince >= 600)) {
            tftPrint("Wifi failed for 10 minutes, entering AP mode...", true, TFT_RED, 2, 1, true, 0, 0);
            SerialPrint("Wifi failed for 10 minutes, entering AP mode...",true);
            delay(3000);
            APStation_Mode();
//            controlledReboot("Wifi failed for 15 min, rebooting", RESET_WIFI, true);
        }
    } else {
        wifiDownSince = 0;
        I.wifiFailCount = 0;
        ArduinoOTA.handle();
        server.handleClient();
    }

    // --- Periodic Tasks ---
    if (OldTime[1] != minute()) {

        OldTime[1] = minute();

        if (I.wifiFailCount > 20) controlledReboot("Wifi failed so resetting", RESET_WIFI, true);

        #ifdef _ISPERIPHERAL
        mydeviceindex = Sensors.findMyDeviceIndex(); //update this periodically to ensure I have no changed
        #endif

        #ifdef _USEWEATHER
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
    
        if (I.localBatteryIndex<255 && I.localBatteryIndex >= 0 && I.localBatteryIndex < NUMDEVICES * NUMSENSORS) {
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
            tftPrint("Weather failed for 60 minutes.", true, TFT_RED, 2, 1, true, 0, 0);
            tftPrint("Rebooting in 3 seconds...", false, TFT_WHITE, 2, 1, false, -1, -1);
            SerialPrint("Weather failed for 60 minutes. Rebooting in 3 seconds...",true);
            delay(3000);
            I.resetInfo = RESET_WEATHER;
            I.lastResetTime = I.currentTime;
            storeError("Weather failed too many times");
            controlledReboot("Weather failed too many times", RESET_WEATHER);
        }
        #endif

        #ifndef _ISPERIPHERAL

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

        #endif

        handleESPNOWPeriodicBroadcast(10);
        handleStoreCoreData();
        
        #ifdef _USEGSHEET

        uint8_t gsheetResult = Gsheet_uploadData();

        if (gsheetResult<0) {
            SerialPrint(GsheetUploadErrorString(),true);
        } 
        #endif
    }

    if (OldTime[2] != hour()) {
        OldTime[2] = hour();


        #ifndef _ISPERIPHERAL
        checkExpiration(-1, I.currentTime, false);
        #endif


        #ifdef _ISPERIPHERAL
        //update mySensorsIndices array
        memset(I.mySensorsIndices, 0, sizeof(I.mySensorsIndices));
        for (int16_t i = 0; i < NUMSENSORS; i++) {
            uint16_t snsvalid = Sensors.isSensorIndexValid(i);
            if (snsvalid == 1 || snsvalid == 3) I.mySensorsIndices[i] = i;
            
        }
        #endif

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


        #ifdef _ISPERIPHERAL
        //run through all my sensors and try and update them

        for (int16_t i = 0; i < SENSORNUM; i++) {
            SnsType* sensor = Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[i]);
            if (sensor && sensor->IsSet) {
                int8_t readResult = ReadData(sensor, false, mydeviceindex);
                
                bool sendResult = SendData(sensor, false);
                if (sendResult)                     SensorHistory.recordSentValue(sensor,i);
                
            }
        }
        #endif
    }
}



