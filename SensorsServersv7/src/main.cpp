
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
#include "firmwareUpdate.hpp"
#include <esp_task_wdt.h>

#ifdef _USELOWPOWER
#include "LowPower.hpp"
#endif

#ifdef _USENETWORKMONITOR
#if _USENETWORKMONITOR > 0
#include "NetworkMonitor.hpp"
#endif
#endif

#ifdef _USETFT
extern LGFX tft;
#endif

#if _HAS_LOCAL_SENSORS && !defined(_USELOWPOWER)
extern STRUCT_SNSHISTORY SensorHistory;
#endif

// SECSCREEN and HourlyInterval are now members of Screen struct (I.SECSCREEN, I.HourlyInterval)
extern STRUCT_CORE I;
extern String WEBHTML;
#ifdef _USEGSHEET
extern STRUCT_GOOGLESHEET GSheetInfo;
#endif

#if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
extern WeatherInfoOptimized WeatherData;
#endif

#ifdef _USELED
  extern Animation_type LEDs;
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
    #ifdef _USELOWPOWER
    //low power devices do not support OTA
    return;
    #else

    tftPrint("Connecting ArduinoOTA... ", false);
    ArduinoOTA.setHostname("WeatherStation");
    ArduinoOTA.setPassword("12345678");
    ArduinoOTA.setTimeout(30000);
    ArduinoOTA.onStart([]() {
        beginArduinoOtaFocus();
        #ifdef _USETFT
        displayOTAProgress(0, 100); 
        #endif
        #ifdef _USELED
        // Set all LEDs to green to indicate OTA start
        for (byte j = 0; j < _USELED_SIZE; j++) {
          if (j%3==0) LEDARRAY[j] = (uint32_t) 50 << 16 | 0 << 8 | 0; 
          else if (j%3==1) LEDARRAY[j] = (uint32_t) 0 << 16 | 50 << 8 | 0;
          else  LEDARRAY[j] = (uint32_t) 0 << 16 | 0 << 8 | 50;          
        }
        FastLED.show();
        #endif
    });

    ArduinoOTA.onEnd([]() {     
        #ifdef _USETFT
        tft.setTextSize(1); 
        displayOTAProgress(100, 100);
        #endif
        tftPrint("OTA End.\nRebooting.", true, TFT_GREEN, 4, 1, false, 0, 200);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        notifyArduinoOtaProgress(progress, total);
        #ifdef _USESERIAL
          Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
        #endif
        #ifdef _USETFT
        if (progress%10==0) {
          displayOTAProgress(progress, total);
        }
        #endif
        #ifdef _USESSD1306
          if ((int)(progress) % 10 == 0) oled.print(".");   
        #endif
        #ifdef _USELED
          // Show OTA progress on LEDs as a filling bar
          if (progress%10==0) {
            for (byte j = 0; j < _USELED_SIZE; j++) {
                LEDARRAY[_USELED_SIZE - j - 1] = 0;
                if (j <= (double) _USELED_SIZE * progress / total) {
                LEDARRAY[_USELED_SIZE - j - 1] = (uint32_t) 128 << 16 | 128 << 8 | 128; // medium white
                }
            }
            FastLED.show();
          }
        #endif
      });
  
    
    
    #ifdef _USETFT
    ArduinoOTA.onError([](ota_error_t error) {
        notifyArduinoOtaError();
        displayOTAError(error);
    });
    #else
    ArduinoOTA.onError([](ota_error_t error) {
        notifyArduinoOtaError();
        SerialPrint("OTA error: " + String((int)error), true);
    });
    #endif
    ArduinoOTA.begin();

    tftPrint("OK.", true, TFT_GREEN);

    #endif

}



// --- Main Setup ---
void setup() {

    #ifdef _USELOWPOWER
    LOWPOWER_Initialize();
    if (initSystem()==false) {
        while (1) { 
            SerialPrint("Critical error. Rebooting...", true);
            delay(1000); 
        }
    }; //among other things, loads the Prefs struct. If this is false I could not load prefs or I could not register myself. Critical errors

    initSensor(-256); //clear all sensors

    initHardwareSensors(); //initialize the hardware sensors

    LOWPOWER_readAndSend();

    return;
    #else

    // --- Boot Security Check ---
    // Watchdog - ESP32 core 3.3.5 uses new API with config struct
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),  // Bitmask for cores 0 and 1 (ESP32 has 2 cores)
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);


    #if _HAS_LOCAL_SENSORS
    // Peripherals have no TFT and minimal HTTP; avoid reserving 20KB to reduce heap pressure and fragmentation
    WEBHTML.reserve(2048);
    #else
    WEBHTML.reserve(20000);
    #endif

    if (!initSystem()) return;

    initSensor(-256); //clear all sensors

    #ifdef _USESERIAL
    tftPrint("Using Serial.", true);
    SerialPrint("Using Serial.",true);
    #else
    tftPrint("Serial disabled.", true);
    #endif

    #ifdef _USESDCARD
    loadScreenFlags(); //load the screen flags from the SD card
    // Devices/sensors are loaded in initSystem() immediately after SD mount, before registration/IP sync can overwrite DevicesSensors.dat
    #endif //_USESDCARD


    tftPrint("Set up time... ", false, TFT_WHITE, 2, 1, false, -1, -1);
    if (setupTime()) {
        displaySetupProgress( true);
    } else {
        displaySetupProgress( false);
    }
    
    #ifdef _USESDCARD
    //do this after time has been set
    if (I.rebootsToday < 255) I.rebootsToday++;
    logSystemEvent("System Booted", EVENT_BOOT_COMPLETE);
    #endif

    initOTA();

    //check time and ensure validity
    I.ALIVESINCE = 0;
    if (isTimeValid(I.currentTime)==false) {
        tftPrint("Current time is not valid", true, TFT_RED);
        SerialPrint("Current time is not valid",true);        
    } else {
        tftPrint("Current UTC time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")), true, TFT_WHITE, 2, 1, false, -1, -1);
        tftPrint("Current local time = " + String(dateify(I.currentTime,"yyyy-mm-dd hh:nn:ss")), true, TFT_WHITE, 2, 1, false, -1, -1);
        SerialPrint("Current UTC time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")),true);
        SerialPrint("Current local time = " + String(dateify(I.currentTime,"yyyy-mm-dd hh:nn:ss")),true);
        I.ALIVESINCE = I.currentTime;
        // Align day/hour/minute trackers so the first loop() pass does not run a false midnight rollover reset
        OldTime[3] = weekday();
        OldTime[2] = hour();
        OldTime[1] = minute();
        OldTime[0] = I.currentSecond;
    }
    
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
            #ifdef _USESDCARD
            storeScreenInfoSD(); // Save the updated reset info
            #endif
        }
    } else if (I.lastResetTime == 0) {
        // This is likely the first boot, set initial values
        I.resetInfo = RESET_DEFAULT;
        I.lastResetTime = I.currentTime;
        #if _IS_SERVER_HUB
        SerialPrint("First boot detected, setting initial reset info", true);
        #endif
    }
    
    


    #if _HAS_LOCAL_SENSORS
    initHardwareSensors(); //initialize the hardware sensors
    #endif

    #if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0) && _IS_SERVER_HUB && !_HAS_LOCAL_SENSORS
    NetworkMonitor.init(); // hub-only; hybrid/local-sensor path calls init() from setupSensors()
    #endif
    
    #ifdef _USELED
    initLEDs();
    #endif
    

    #ifdef _USETFLUNA
    setupTFLuna();
    #endif

    #if defined(_USESDCARD) && !defined(_USELOWPOWER)
    checkAndApplySDFirmwareOnBoot();
    #endif

    #ifdef _USEWEATHER
    tft.clear();
    tft.setCursor(0,0);

        tftPrint("Loading weather data...", false, TFT_WHITE, 2, 1, false, -1, -1);
        esp_task_wdt_reset();
    //load weather data from SD card; forceStaleRefresh applies content freshness
    //(e.g. hourly needs 24h coverage) rather than trusting a recent fetch timestamp.
        if (readWeatherDataSD()) {
            byte weatherBoot = WeatherData.updateWeatherOptimized(3600, true, true);
            if (weatherBoot == 3) {
                SerialPrint("Weather data loaded from SD card (content fresh)", true);
                tftPrint("Weather data on SD card ok.", true, TFT_GREEN);
            } else if (weatherBoot == 2) {
                SerialPrint("Weather data on SD is stale; retry window not open yet.", true);
                tftPrint("Weather data stale.", true, TFT_YELLOW);
            } else if (weatherBoot > 0) {
                SerialPrint("Weather data loaded from SD card and refreshed from NOAA.", true);
                tftPrint("Weather data updated.", true, TFT_GREEN);
            } else {
                SerialPrint("Weather data loaded from SD card, but update failed/stale. Retrying NOAA.", true);
                tftPrint("Expired.", true, TFT_YELLOW);
                tftPrint("Weather data on SD card stale.", true, TFT_YELLOW);
                tftPrint("Weather data updating from NOAA.", true, TFT_YELLOW);
                tftPrint("This may take several minutes.", true, TFT_YELLOW);
                esp_task_wdt_reset();
                WeatherData.updateWeatherOptimized(3600, true, true);
            }

        } else {
            SerialPrint("Weather data not found on SD card, updating from NOAA.",true);
            tftPrint("No weather data on SD card.", true, TFT_YELLOW);                
            tftPrint("Weather data not on SD. Updating from NOAA.", true, TFT_YELLOW);
            tftPrint("This may take several minutes.", true, TFT_YELLOW);
            esp_task_wdt_reset();
            WeatherData.updateWeatherOptimized(3600, true, true);
        }
        esp_task_wdt_reset();

        #ifdef _USEGSHEET
        startGsheet();
        #endif


    
        tftPrint("Setup OK.", true, TFT_GREEN);

    #endif //_USEWEATHER

    #ifdef _USEWEATHERLITE
    {
      SerialPrint("Weather lite: loading local weather cache...", true);
      if (FileOrDirectoryExists(WEATHER_PKG_PATH)) {
        weatherLiteUnpackFile(WEATHER_PKG_PATH);
      } else if (readWeatherDataSD()) {
        weatherLiteApplyIFlagsFromPackage();
        updateCurrentOutsideConditions();
        SerialPrint("Weather lite: loaded WeatherData.dat (no package yet)", true);
      } else {
        SerialPrint("Weather lite: no local weather; will request from type-100 server", true);
      }
      weatherLiteRequestFromAnyWeatherServer();
    }
    #endif




    #ifdef _USETFT
    #ifdef _ISCLOCK480X480
    // Clock480X480: avoid tft.clear() and setTextFont/setTextSize (can alter panel/write state).
    // Use explicit fill and LGFX setFont so all subsequent screens stay correct.
    tft.fillScreen(0x0000);  // RGB565 black
    tft.setCursor(0,0);
    tft.setTextColor(TFT_WHITE);
    tft.setFont(&fonts::Font2);
    #else
    tft.clear();
    tft.setCursor(0,0);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    #endif
    #endif

    #if _IS_SERVER_HUB
    tftPrint("Please wait for SD card cleanup and Google Sheet updates.", true, TFT_GREEN);
    #endif


#endif //_USELOWPOWER/else
{
  FirmwareVersion fw;
  getLocalFirmware(fw);
  char verBuf[16];
  fw.toChar(verBuf, sizeof(verBuf));
  String setupMsg = String(verBuf) + " setup complete";
  SerialPrint(setupMsg, true);
  logSystemEvent(setupMsg, EVENT_BOOT_COMPLETE);
}
    
}




// --- Main Loop ---
void loop() {

    #ifndef _USELOWPOWER
    if (serviceArduinoOtaFocusMode()) return;
    #endif

    systemHousekeeping();


    #ifdef _USETFLUNA    
//    note that a tfluna device will operate even without wifi, but it will not be able to send/update data other than distance
    
    if (TFLunaUpdateMAX()) {
        server.handleClient();
        ArduinoOTA.handle();
        return; //if tfluna is reading, then skip everything else        
    }
    #endif

    #ifdef _ISCLOCK480X480
    clockLoop();
    #endif


    #if defined(_USETFT) && _IS_SERVER_HUB
    updateGraphics();
    #endif

    #ifdef _USEGSHEET
    GSheet.ready(); //maintains authentication
    #endif
    
    #ifdef _USELED
    LEDs.LED_update();
    #endif

    // --- Periodic Tasks ---
    if (OldTime[1] != minute()) {
        systemHousekeeping(true);
        SerialPrint("Minute: " + String(minute()),true);

        OldTime[1] = minute();

        #ifdef _USESSD1306
        redrawOled();
        #endif
        
        I.MyRandomSecond = random(0, 59); //this is the random second at which I will send data. This prevents all devices from sending data at the same time, which could overload the network.
        if (minute() % 10 == 0 && _MYTYPE >= 100) I.makeBroadcast = true; //have servers broadcast every 10 minutes
        
        if (Sensors.getNumDevices() ==1) I.makeBroadcast = true; //if there is only one device (including  me), broadcast my presence
        
        #if _HAS_LOCAL_SENSORS
        // Force a send cycle if any server is overdue (UDP broadcast + HTTP/HTTPS for low UDP-rate servers)
        {
          bool anyServerOverdue = false;
          for (int16_t i = 0; i < NUMDEVICES; i++) {
            ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
            if (!d || !d->IsSet || d->devType < 100) continue;
            if (d->dataSent == 0 || d->dataSent + d->SendingInt < I.currentTime) {
              anyServerOverdue = true;
              break;
            }
          }
          if (anyServerOverdue) {
            sendAllSensors(true, -1, true);
          }
        }
        #endif

        #ifdef _USEWEATHER
        // Full weather station: fetch/process NOAA locally
        byte weatherResult = WeatherData.updateWeatherOptimized(3600);  // sync interval 3600 sec = 1 hr
        if (weatherResult == 1) {
            SerialPrint("Weather updated successfully",true);
        } else if (weatherResult == 3) {
            SerialPrint("Weather update: data is still fresh",true);
        } else if (weatherResult == 2) {
            SerialPrint("Weather update: data is stale (waiting for retry window)", true);
        } else if (weatherResult == 0) {
            SerialPrint("Weather update: one or more components failed",true);
        } else {
            SerialPrint("Weather update: error code " + (String) weatherResult,true);
        }
        #endif

        #if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
        static uint32_t lastWeatherTimeoutErrorT = 0;
        if ((WeatherData.lastUpdateT == 0 || I.currentTime > WeatherData.lastUpdateT + 3600)
            && I.currentTime - I.ALIVESINCE > 10800
            && I.currentTime - lastWeatherTimeoutErrorT > 3600) {
            #ifdef _USEWEATHERLITE
            storeError("Weather package stale >60 minutes", ERROR_WEATHER_TIMEOUT, true);
            #else
            storeError("Weather failed >60 minutes", ERROR_WEATHER_TIMEOUT, true);
            #endif
            lastWeatherTimeoutErrorT = I.currentTime;
        }

        // Display current conditions: outside sensors when available, else packaged/NOAA forecast
        updateCurrentOutsideConditions();
        #endif

        
        #if _IS_SERVER_HUB
            #ifdef _ISHVACSERVER
                checkHVAC();
            #endif

        #endif
        I.isFlagged = 0;
        I.isSoilDry = 0;
        I.isHot = 0;
        I.isCold = 0;
        I.isLeak = 0;
        I.isFlagged = countFlagged(0, 0b00000111, 0b00000011, 0); //only count flagged sensors if they are monitored (bit 1 is set)
        I.isSoilDry = countFlagged(-3, 0b10000111, 0b10000011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isHot = countFlagged(-1, 0b10100111, 0b10100011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isCold = countFlagged(-1, 0b10100111, 0b10000011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isLeak = countFlagged(70, 0b10000001, 0b10000001, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);

        if (I.currentTime % 300 == 0) Sensors.checkDeviceFlags(); //check the device flags every 5 minutes

        handleStoreCoreData();
        
        #ifdef _USEGSHEET
        //we got a sensor reading, so upload the data to the spreadsheet if time is appropriate
        if (GSheetInfo.useGsheet) Gsheet_uploadData();
        #endif
     
    }

    if (OldTime[2] != hour()) {
        OldTime[2] = hour();
      
        //check if DST has changed every hour
        DSTsetup();
        
        //check if my IP address has changed
        ArborysDevType* myDevice = Sensors.getDeviceByDevIndex(I.MY_DEVICE_INDEX);
        if (myDevice) {
            bool storeToSD = false;

            if (WiFi.localIP() != myDevice->IP) {
                myDevice->IP = WiFi.localIP();
                storeToSD = true;
            }

            //check if the device name has changed
            if (strcmp(Prefs.DEVICENAME, myDevice->devName) != 0) {
                //copy the devicename in mydevice to prefs
                strncpy(Prefs.DEVICENAME, myDevice->devName, sizeof(Prefs.DEVICENAME) - 1);
                Prefs.DEVICENAME[sizeof(Prefs.DEVICENAME) - 1] = '\0';
                Prefs.isUpToDate = false;
                //store prefs
                handleStoreCoreData();
                storeToSD = true;
            }

            //store the device to SD card
            #ifdef _USESDCARD
            if (storeToSD) storeDevicesSensorsSD();
            #endif
        }
        
 

        #ifdef _REBOOTWEEKLY
        if (OldTime[2] == 3) {
            //check if the day is Tue
            if (weekday() == 3) controlledReboot("Weekly scheduled reboot", RESET_DEFAULT);
            
        }
        #endif
    }
    if (OldTime[3] != weekday()) {
        OldTime[3] = weekday();
        I.ESPNOW_SENDS = 0;
        I.ESPNOW_RECEIVES = 0;
        I.UDP_RECEIVES = 0;
        I.UDP_SENDS = 0;
        I.HTTP_RECEIVES = 0;
        I.HTTP_SENDS = 0;

        I.ESPNOW_INCOMING_ERRORS = 0;
        I.ESPNOW_OUTGOING_ERRORS = 0;
        I.UDP_INCOMING_ERRORS = 0;
        I.UDP_OUTGOING_ERRORS = 0;
        I.HTTP_INCOMING_ERRORS = 0;
        I.HTTP_OUTGOING_ERRORS = 0;
        I.rebootsToday = 0;
        Sensors.resetDailyPingCounters();

        #ifdef _REBOOTDAILY
        SerialPrint("Rebooting daily...",true);
        //ESP.restart();
        controlledReboot("Daily reboot", RESET_DEFAULT);
        #endif

        #if _HAS_LOCAL_SENSORS
            #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 

                #ifdef _USESERIAL
                SerialPrint( "Reset HVACs...",true);
                #endif
                initHVAC();

            #endif
        #endif
    }    
    if (OldTime[0] != second()) {
        OldTime[0] = I.currentSecond;

        //if time is invalid, completely reset the time
        if (isTimeValid(I.currentTime)==false) {
            SerialPrint("Time is invalid, completely resetting time",true);
            storeError("Time is invalid, completely resetting time", ERROR_TIME,true);
            I.currentTime = 0;
            setupTime();
            SerialPrint((String) "New time is: " + dateify(I.currentTime),true);
        }

        updateRSSI();

        #if _HAS_LOCAL_SENSORS
            readAllSensors(false);
        #endif

        if (I.MyRandomSecond == second()) {
            // Send first so local timeLogged/timeRead clocks refresh before expiry evaluation.
            #if _HAS_LOCAL_SENSORS
            sendAllSensors(false, -1, true);
            #endif
            // once per minute at a random second: check critical sensor expiry
            // (local: timeRead + 1.25×SendingInt; remote: timeLogged + 1.25×SendingInt)
            I.isExpired = Sensors.checkExpirationAllSensors(I.currentTime, true, 0, true);

            #if _IS_SERVER_HUB
            // Hub: start expired-peripheral data-request cycle (one device per second below)
            serviceExpiredDeviceDataRequests(true);
            if (I.makeBroadcast) { //broadcast every 10 minutes, at some random second within the 10th minute
                broadcastServerPresence(true, 2);
            }
            #endif

        }

        #if _IS_SERVER_HUB
        // Expired data requests: one device per second; HTTP path is async (queued worker)
        serviceExpiredDeviceDataRequests(false);
        #endif

        // Connectivity pings (response-required): start every 10 minutes; service one device/sec
        serviceDeviceConnectivityPings(minute() % 10 == 0 && I.MyRandomSecond == second());
    }

}



