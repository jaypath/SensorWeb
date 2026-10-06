
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

#include "globals.hpp"
#include "utility.hpp"
#include "firmwareUpdate.hpp"
#if _HAS_LOCAL_SENSORS
#include "interrupt_triggers.hpp"
#include "actuators.hpp"
#include "bryant_bus.hpp"
#endif
#include <esp_task_wdt.h>
#include <esp_system.h>
#if _SUPABASE_RUNTIME
#include "supabase_prefs.hpp"
#endif

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
    // Peripherals: keep WEBHTML small — large buffers OOM mid-page after the header.
    WEBHTML.reserve(512);
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
    {
      // Capture panic/WDT/brownout so ArbNet TLS crashes leave a breadcrumb (they skip storeError mid-fault).
      const esp_reset_reason_t rr = esp_reset_reason();
      if (rr == ESP_RST_PANIC || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT ||
          rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT || rr == ESP_RST_SDIO) {
        const char* name = "unknown";
        switch (rr) {
          case ESP_RST_PANIC:     name = "panic"; break;
          case ESP_RST_INT_WDT:   name = "interrupt watchdog"; break;
          case ESP_RST_TASK_WDT:  name = "task watchdog"; break;
          case ESP_RST_WDT:       name = "other watchdog"; break;
          case ESP_RST_BROWNOUT:  name = "brownout"; break;
          case ESP_RST_SDIO:      name = "SDIO reset"; break;
          default: break;
        }
        char msg[80];
        snprintf(msg, sizeof(msg), "Unexpected reset: %s", name);
        storeError(msg, ERROR_UNDEFINED, true);
        SerialPrint(String(msg) + " (reason=" + String((int)rr) + ")", true);
      }
    }

    initOTA();

    //check time and ensure validity
    I.ALIVESINCE = 0;
    if (isTimeValid((uint32_t)utcNow())==false) {
        tftPrint("Current time is not valid", true, TFT_RED);
        SerialPrint("Current time is not valid",true);        
    } else {
        tftPrint("Current UTC time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")), true, TFT_WHITE, 2, 1, false, -1, -1);
        tftPrint("Current local time = " + String(dateify(I.currentTime,"yyyy-mm-dd hh:nn:ss")), true, TFT_WHITE, 2, 1, false, -1, -1);
        SerialPrint("Current UTC time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")),true);
        SerialPrint("Current local time = " + String(dateify(I.currentTime,"yyyy-mm-dd hh:nn:ss")),true);
        I.ALIVESINCE = utcNow();
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
        time_t timeDiff = utcNow() - I.ALIVESINCE;
        if (timeDiff > 300) {
            SerialPrint("Unexpected reboot detected! Previous ALIVESINCE: " + String(I.ALIVESINCE) + 
                       ", Current time: " + String(utcNow()) + 
                       ", Time difference: " + String(timeDiff) + " seconds", true);
            #ifdef _USESDCARD
            storeScreenInfoSD(); // Save the updated reset info
            #endif
        }
    } else if (I.lastResetTime == 0) {
        #if _IS_SERVER_HUB
        SerialPrint("First boot detected", true);
        #endif
    }

    commitBootRebootIssue();

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
        // Wrong object size or store version: delete the package now, fetch NOAA,
        // and write a new one. Do not leave it for the hourly push.
        const uint8_t pkgRecover = WeatherData.recoverCorruptWeatherPackage(true);
        if (pkgRecover == 1) {
            SerialPrint("Weather package size mismatch; rebuilt from fresh NOAA data", true);
            tftPrint("Weather package size mismatch.", true, TFT_YELLOW);
            tftPrint("Fresh weather downloaded. Package rebuilt.", true, TFT_GREEN);
        } else if (pkgRecover == 2) {
            SerialPrint("Weather package size mismatch; rebuild will retry", true);
            tftPrint("Weather package size mismatch.", true, TFT_YELLOW);
            tftPrint("Fresh download will retry.", true, TFT_YELLOW);
        }
    //load weather data from SD card; forceStaleRefresh applies content freshness
    //(e.g. hourly needs 24h coverage) rather than trusting a recent fetch timestamp.
        if (pkgRecover == 1) {
            SerialPrint("Weather package recovery already refreshed NOAA data", true);
        } else if (readWeatherDataSD()) {
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
        SerialPrint("Weather lite: no local weather; will request from type-100–150 weather server", true);
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
    #ifdef _USEGSHEET
    tftPrint("Please wait for SD card cleanup and Google Sheet updates.", true, TFT_GREEN);
    #else
    tftPrint("Finishing boot...", true, TFT_GREEN);
    #endif
    #endif

    esp_task_wdt_reset();


#endif //_USELOWPOWER/else
{
  FirmwareVersion fw;
  getLocalFirmware(fw);
  char verBuf[16];
  fw.toChar(verBuf, sizeof(verBuf));
  String setupMsg = String(verBuf) + " setup complete";
  SerialPrint(setupMsg, true);
  logSystemEvent(setupMsg, EVENT_BOOT_COMPLETE);
  esp_task_wdt_reset();
  #ifdef _USETFT
  tftPrint("Entering main loop...", true, TFT_GREEN);
  #endif
}

#ifdef _INITDIO_LOW
//set up pins 25,26,27,33,34,35 as DIO outputs and set them to LOW
  digitalWrite(25, LOW);
  digitalWrite(26, LOW);
  digitalWrite(27, LOW);
  digitalWrite(33, LOW);
  digitalWrite(34, LOW);
  digitalWrite(35, LOW);
  //digitalWrite(32, LOW); //this is a power pin for I2C
#endif
  // RCWL enable must stay HIGH (motion always armed). Re-assert after _INITDIO_LOW.
#if defined(_PIN_ENABLE_RCWL) && (_PIN_ENABLE_RCWL != -9999) && (_PIN_ENABLE_RCWL != -1)
  {
    const int16_t rcwlEn = (_PIN_ENABLE_RCWL < 0) ? (int16_t)(-(_PIN_ENABLE_RCWL)) : (int16_t)_PIN_ENABLE_RCWL;
    pinMode((uint8_t)rcwlEn, OUTPUT);
    digitalWrite((uint8_t)rcwlEn, HIGH);
  }
#endif

#if _HAS_LOCAL_SENSORS && !defined(_USELOWPOWER)
  // After pin setup. The first loop can send on a minute boundary before its own poll.
  readLocalSensorsAtBoot();
#endif
    
}




// --- Main Loop ---
void loop() {

    #ifndef _USELOWPOWER
    if (serviceArduinoOtaFocusMode()) return;
    #endif

    systemHousekeeping();

    #if _HAS_LOCAL_SENSORS
    InterruptTriggers_serviceWebForces();
    #if _USEINTERRUPT
    serviceInterruptSensors();
    #endif
    serviceFastActuators();
    bryantBusPoll();
    #endif

    #if _SUPABASE_RUNTIME
    // Staged ArborysNet TLS (one step per gap): Auth → Ping → Query.
    // Cloud sync (readings/keepalive) waits until that pipeline completes.
    esp_task_wdt_reset();
    supabaseServiceStartupSiteSync();
    esp_task_wdt_reset();
    supabaseServiceCloudSync(false);
    esp_task_wdt_reset();
    #endif

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
        if (minute() % 10 == 0 && _I_AM_SERVER) {
          I.makeBroadcast = true; //have servers broadcast every 10 minutes
        }
        
        if (Sensors.getNumDevices() ==1) I.makeBroadcast = true; //if there is only one device (including  me), broadcast my presence
        
        #if _HAS_LOCAL_SENSORS
        // Force a send cycle if any server is overdue (UDP broadcast + HTTP/HTTPS for low UDP-rate servers)
        {
          bool anyServerOverdue = false;
          for (int16_t i = 0; i < NUMDEVICES; i++) {
            ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
            if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) continue;
            if (d->dataSent == 0 || d->dataSent + d->SendingInt < (uint32_t)utcNow()) {
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
        if ((WeatherData.lastUpdateT == 0 || utcNow() > WeatherData.lastUpdateT + 3600)
            && utcNow() - I.ALIVESINCE > 10800
            && utcNow() - lastWeatherTimeoutErrorT > 3600) {
            #ifdef _USEWEATHERLITE
            storeError("Weather package stale >60 minutes", ERROR_WEATHER_TIMEOUT, true);
            #else
            storeError("Weather failed >60 minutes", ERROR_WEATHER_TIMEOUT, true);
            #endif
            lastWeatherTimeoutErrorT = (uint32_t)utcNow();
        }

        // Display current conditions: outside sensors when available, else packaged/NOAA forecast
        updateCurrentOutsideConditions();
        #endif

        
        #if _IS_SERVER_HUB
            #ifdef _ISHVACSERVER
                checkHVAC();
            #endif
            // supabaseHubPollTick() retained in supabase_prefs but unwired (inventory deferred)
        #endif
        I.isFlagged = 0;
        I.isSoilDry = 0;
        I.isHot = 0;
        I.isCold = 0;
        I.isLeak = 0;
        I.isFlagged = countFlagged(0, 0b00000111, 0b00000011, 0); //only count flagged sensors if they are monitored (bit 1 is set)
        I.isSoilDry = countFlagged(-3, 0b10000111, 0b10000011, (utcNow() > 3600) ? (uint32_t)utcNow() - 3600 : 0);
        I.isHot = countFlagged(-1, 0b10100111, 0b10100011, (utcNow() > 3600) ? (uint32_t)utcNow() - 3600 : 0);
        I.isCold = countFlagged(-1, 0b10100111, 0b10000011, (utcNow() > 3600) ? (uint32_t)utcNow() - 3600 : 0);
        I.isLeak = countFlagged(70, 0b10000001, 0b10000001, (utcNow() > 3600) ? (uint32_t)utcNow() - 3600 : 0);

        if ((uint32_t)utcNow() % 300 == 0) Sensors.checkDeviceFlags(); //check the device flags every 5 minutes

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
        I.MESH_SENDS = 0;
        I.MESH_RECEIVES = 0;
        I.UDP_RECEIVES = 0;
        I.UDP_SENDS = 0;
        I.HTTP_RECEIVES = 0;
        I.HTTP_SENDS = 0;

        I.MESH_INCOMING_ERRORS = 0;
        I.MESH_OUTGOING_ERRORS = 0;
        I.UDP_INCOMING_ERRORS = 0;
        I.UDP_OUTGOING_ERRORS = 0;
        I.HTTP_INCOMING_ERRORS = 0;
        I.HTTP_OUTGOING_ERRORS = 0;
        I.rebootsToday = 0;
        Sensors.resetDailyPingCounters();
        resetExpiredRequestLadder();

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
        if (isTimeValid((uint32_t)utcNow())==false) {
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

        #if defined(_USESSD1306) && defined(_USEBRYANT)
        redrawOled();
        #endif

        if (I.MyRandomSecond == second()) {
            // Send first so local timeLogged/timeRead clocks refresh before expiry evaluation.
            #if _HAS_LOCAL_SENSORS
            sendAllSensors(false, -1, true);
            #endif
            // once per minute at a random second.
            // Hub remotes: critical (bit 7 after override) at 1.05×SendingInt, others at 2.05×.
            // Local sensors stay at 1.25×. Rechecks run every second below.
            I.isExpired = Sensors.checkExpirationAllSensors(utcNow(), false, 0, true);

            #if _IS_SERVER_HUB
            serviceExpiredDeviceDataRequests(true);
            if (I.makeBroadcast) { //broadcast every 10 minutes, at some random second within the 10th minute
                broadcastServerPresence(true, 2);
            }
            if (minute() % 10 == 0) {
              I.makeCloudUpload = true; // Supabase reading upload at random second in the 10-min slot
            }
            #endif

        }

        #if _IS_SERVER_HUB
        // Expired rechecks: one peripheral per second. Critical uses HTTP/HTTPS; others use UDP.
        serviceExpiredDeviceDataRequests(false);
        #endif

        // Connectivity pings (response-required): start every 10 minutes; service one device/sec
        serviceDeviceConnectivityPings(minute() % 10 == 0 && I.MyRandomSecond == second());
    }

}



