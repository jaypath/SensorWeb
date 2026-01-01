
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
#include <esp_task_wdt.h>

#ifdef _USELOWPOWER
#include "LowPower.hpp"
#endif

// --- WiFi Down Timer ---
static uint32_t wifiDownSince = 0;
static uint32_t lastPwdRequestMinute = 0;

int16_t MY_DEVICE_INDEX = 0; //local stored index of my device


#ifdef _USETFT
extern LGFX tft;
#endif

#if defined(_ISPERIPHERAL) && !defined(_USELOWPOWER)
extern STRUCT_SNSHISTORY SensorHistory;
#endif

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
    #ifdef _USETFT
    ArduinoOTA.onStart([]() { 
        displayOTAProgress(0, 100); 
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
        tft.setTextSize(1); 
        displayOTAProgress(100, 100);
        tftPrint("OTA End.\nRebooting.", true, TFT_GREEN, 4, 1, false, 0, 200);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
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
        #ifdef _DONNOTUSE
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
  
    
    
    ArduinoOTA.onError([](ota_error_t error) { displayOTAError(error); });
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
    // Watchdog
    esp_task_wdt_deinit();
    esp_task_wdt_init(WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);




    WEBHTML.reserve(20000);

    initSystem(); //among other things, loads the Prefs struct

    initSensor(-256); //clear all sensors

    #ifdef _USESERIAL
    tftPrint("Using Serial.", true);
    SerialPrint("Using Serial.",true);
    #else
    tftPrint("Serial disabled.", true);
    #endif

    int8_t sdResult = initSDCard();
    if (sdResult==0) return;
    if (sdResult==-1) {
        tftPrint("SD Card not supported", true, TFT_RED, 2, 1, true, 0, 0);
    } else {
        loadScreenFlags(); //load the screen flags from the SD card
        loadSensorData(); //load the sensor data from the SD card
    }



    tftPrint("Set up time... ", false, TFT_WHITE, 2, 1, false, -1, -1);
    #ifdef _USETFT
    if (setupTime()) {
        displaySetupProgress( true);
    } else {
        displaySetupProgress( false);
    }
    #endif
    delay(100);
    tftPrint("Current time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")), true, TFT_WHITE, 2, 1, false, -1, -1);
    SerialPrint("Current time = " + String(dateify(now(),"yyyy-mm-dd hh:nn:ss")),true);
    
    
    

    initOTA();

    //check time and ensure validity
    I.currentTime = now();
    if (isTimeValid(I.currentTime)==false) {
        tftPrint("Current time is not valid", true, TFT_RED);
        SerialPrint("Current time is not valid",true);        
    }

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
            #ifdef _USESDCARD
            storeScreenInfoSD(); // Save the updated reset info
            #endif
        }
    } else if (I.lastResetTime == 0) {
        // This is likely the first boot, set initial values
        I.resetInfo = RESET_DEFAULT;
        I.lastResetTime = I.currentTime;
        #ifndef _ISPERIPHERAL
        SerialPrint("First boot detected, setting initial reset info", true);
        #endif
    }
    

    #ifdef _ISPERIPHERAL
    initHardwareSensors(); //initialize the hardware sensors
    #endif

    #ifdef _USELED
    initLEDs();
    #endif
    

    #ifdef _USETFLUNA
    setupTFLuna();
    #endif

    #ifdef _USEWEATHER
    tftPrint("Loading weather data...", false, TFT_WHITE, 2, 1, false, -1, -1);
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

    tftPrint("Setup OK.", true, TFT_GREEN);

    #ifdef _USETFT
    tft.clear();
    tft.setCursor(0,0);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    #endif
    #endif
    tftPrint("Please wait for SD card cleanup and Google Sheet updates.", true, TFT_GREEN);


#endif //_USELOWPOWER
    
}




// --- Main Loop ---
void loop() {

    esp_task_wdt_reset();

    #ifdef _USETFLUNA    
//    note that a tfluna device will operate even without wifi, but it will not be able to send/update data other than distance
    
    if (TFLunaUpdateMAX()) {
        server.handleClient();
        ArduinoOTA.handle();
        return; //if tfluna is reading, then skip everything else        
    }
    #endif

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiDownSince == 0) wifiDownSince = I.currentTime;
        int16_t retries = connectWiFi();
        if (WiFi.status() != WL_CONNECTED) I.wifiFailCount++;
        // If WiFi has been down for 10 minutes, enter AP mode
        if (wifiDownSince && (I.currentTime - wifiDownSince >= 600)) {
            #ifdef _USETFT
            tftPrint("Wifi failed for 10 minutes, entering AP mode...", true, TFT_RED, 2, 1, true, 0, 0);
            #endif
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
    

    #ifdef _USEUDP
    receiveUDPMessage(); //receive ESPNow UDP messages, which are sent in parallel to ESPNow
    #endif
    updateTime(); //sets I.currenttime

    #ifdef _USETFT
    checkTouchScreen();
    #endif

    #ifdef _USEGSHEET
    GSheet.ready(); //maintains authentication
    #endif
    
    #ifdef _USELED
    LEDs.LED_update();
    #endif

    // --- Periodic Tasks ---
    if (OldTime[1] != minute()) {

        OldTime[1] = minute();

        #ifdef _USESSD1306
        redrawOled();
        #endif
        
        I.MyRandomSecond = random(0, 59); //this is the random second at which I will send data. This prevents all devices from sending data at the same time, which could overload the network.
        if (minute() % 10 == 0 && _MYTYPE >= 100) I.makeBroadcast = true; //have servers broadcast every 10 minutes
        
        if (Sensors.getNumDevices() ==1) I.makeBroadcast = true; //if there is only one device (including  me), broadcast my presence
        
        if (I.ALIVESINCE == 0) I.ALIVESINCE = I.currentTime; //if ALIVESINCE is not set, set it to the current time

        if (I.wifiFailCount > 20) controlledReboot("Wifi failed so resetting", RESET_WIFI, true);

        MY_DEVICE_INDEX = Sensors.findMyDeviceIndex(); //update my device index

        #ifdef _ISPERIPHERAL
        //check if there are any new servers (that I have not sent data to yet), and if so reset the timelogged for all my sensors
        for (int16_t i=0; i<NUMDEVICES ; i++) {
          ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
          if (!d || !d->IsSet || d->devType < 100) continue;
          if (d->dataSent == 0 || d->dataSent + d->SendingInt < I.currentTime) {
            int16_t deviceIndex = Sensors.findDevice(d->MAC);
            sendAllSensors(true,deviceIndex,false); //http message to server
          }
        }
        
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

        if (WeatherData.lastUpdateAttempt > WeatherData.lastUpdateT + 3600 && I.currentTime - I.ALIVESINCE > 10800) {
            tftPrint("Weather failed for 60 minutes.", true, TFT_RED, 2, 1, true, 0, 0);
            tftPrint("Rebooting in 3 seconds...", false, TFT_WHITE, 2, 1, false, -1, -1);
            SerialPrint("Weather failed for 60 minutes. Rebooting in 3 seconds...",true);
            delay(3000);
            I.resetInfo = RESET_WEATHER;
            I.lastResetTime = I.currentTime;
            storeError("Weather failed too many times");
            controlledReboot("Weather failed too many times", RESET_WEATHER);
        }


        //see if we have local weather
        if (Sensors.hasOutsideSensors("temperature")) {
            I.currentOutsideTemp = Sensors.getAverageOutsideParameterValue("temperature", I.currentTime - 900);
            if (isTempValid(I.currentOutsideTemp)==true) I.haveOutsideTemperatureSensor = true;
            else I.haveOutsideTemperatureSensor = false;
        }
        if (Sensors.hasOutsideSensors("humidity")) {
            I.currentOutsideHumidity = Sensors.getAverageOutsideParameterValue("humidity", I.currentTime - 300);
        }
        if (Sensors.hasOutsideSensors("pressure")) {
            I.currentOutsidePressure = Sensors.getAverageOutsideParameterValue("pressure", I.currentTime - 300);
        }
        
        int8_t currentInternetTemp = WeatherData.getTemperature(I.currentTime);
        if (isTempValid(I.currentOutsideTemp)==false || I.haveOutsideTemperatureSensor==false) {
            I.currentOutsideTemp = currentInternetTemp;
            I.haveOutsideTemperatureSensor = false;
        } else {
            if (isTempValid(currentInternetTemp)==true) {
                if ((double) abs(I.currentOutsideTemp-currentInternetTemp)>150) { //hard to believe a greater than 15 degree difference is possible, ignore local temp
                    I.currentOutsideTemp = currentInternetTemp;
                    I.haveOutsideTemperatureSensor = false;
                }        
            }
        }

        //see if we have local battery        
        int16_t batteryIndex = Sensors.findSnsOfType((const char*) "battery",false,-1);
        bool haveLocalBattery = false;
        ArborysSnsType* sensor = NULL;
        while (batteryIndex != -1 && haveLocalBattery == false) {
          sensor = Sensors.getSensorBySnsIndex(batteryIndex);
          if (!sensor || sensor->IsSet == false) continue;
          if (sensor && sensor->IsSet == true) {        
            if (sensor->timeLogged + 7200>I.currentTime) {
              haveLocalBattery = true;
            }
          }
    
          if (haveLocalBattery == false) batteryIndex = Sensors.findSnsOfType((const char*) "battery",false,batteryIndex+1);
        }
        if (haveLocalBattery == true) {
          I.localBatteryIndex = batteryIndex;
        } else {
          I.localBatteryIndex = 255;
        }
    
        #endif

        
        #ifndef _ISPERIPHERAL
        checkHVAC();
        #endif
        I.isFlagged = 0;
        I.isSoilDry = 0;
        I.isHot = 0;
        I.isCold = 0;
        I.isLeak = 0;
        I.isExpired = 0;
        I.isFlagged = countFlagged(0, 0b00000111, 0b00000011, 0); //only count flagged sensors if they are monitored (bit 1 is set)
        I.isSoilDry = countFlagged(-3, 0b10000111, 0b10000011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isHot = countFlagged(-1, 0b10100111, 0b10100011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isCold = countFlagged(-1, 0b10100111, 0b10000011, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        I.isLeak = countFlagged(70, 0b10000001, 0b10000001, (I.currentTime > 3600) ? I.currentTime - 3600 : 0);
        
        I.isExpired = Sensors.checkExpirationAllSensors(I.currentTime, true,3,true); //this is where sensors are checked for expiration. Returns number of expired sensors. true means only check critical sensors

        if (I.currentTime % 300 == 0) Sensors.checkDeviceFlags(); //check the device flags every 5 minutes

        handleStoreCoreData();
        
        #ifdef _USEGSHEET
        //we got a sensor reading, so upload the data to the spreadsheet if time is appropriate
        if (GSheetInfo.useGsheet) Gsheet_uploadData();
        #endif
     
    }

    if (OldTime[2] != hour()) {
        OldTime[2] = hour();
        
        //check if my IP address has changed
        ArborysDevType* myDevice = Sensors.getDeviceByDevIndex(MY_DEVICE_INDEX);
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
        
        #ifdef _USE32
        size_t freeHeap = ESP.getFreeHeap();
        size_t minFreeHeap = ESP.getMinFreeHeap();
          
        // If memory is critically low, restart
        if (freeHeap < 10000) { // Less than 10KB free
          SerialPrint("CRITICAL: Low memory detected, restarting system", true);
          storeError("CRITICAL: Low memory detected, restarting system", ERROR_HARDWARE_MEMORY,true);
          delay(1000);
          ESP.restart();
        }
        
        // If minimum free heap is very low, log warning
        if (minFreeHeap < 5000) { // Less than 5KB minimum
          SerialPrint("WARNING: Memory fragmentation detected", true);
          storeError("WARNING: Memory fragmentation detected", ERROR_HARDWARE_MEMORY,true);
          ESP.restart();
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

        #ifdef _REBOOTDAILY
        SerialPrint("Rebooting daily...",true);
        ESP.restart();
        #endif

        #ifdef _ISPERIPHERAL
            #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 

                #ifdef _USESERIAL
                SerialPrint( "Reset HVACs...",true);
                #endif
                initHVAC();

            #endif
        #endif
    }
    if (OldTime[0] != second()) {
        OldTime[0] = second();
        SerialPrint("Second changed",true);
        //if time is invalid, completely reset the time
        if (isTimeValid(I.currentTime)==false) {
            SerialPrint("Time is invalid, completely resetting time",true);
            storeError("Time is invalid, completely resetting time", ERROR_TIME,true);
            I.currentTime = 0;
            setupTime();
            SerialPrint((String) "New time is: " + dateify(I.currentTime),true);
        }

        #ifdef _USETFT
        fcnDrawScreen();
        if (I.screenChangeTimer > 0) I.screenChangeTimer--;
        else I.ScreenNum=0;
        #endif



        #ifdef _ISPERIPHERAL
            //run through all my sensors and try and update them


            readAllSensors(false);
            if (I.MyRandomSecond == second())             sendAllSensors(false, -1, true);

        #else
            if (I.MyRandomSecond == second())   {

                //at a random point every minute, check for expired devices/sensors, and determine how to communicate with them
                //check for expired devices by determining if ANY sensors are expired
                Sensors.checkExpirationAllSensors(I.currentTime, true,1,true); //parameter 2 is true for  only critical sensors, parameter 3 is the multiplier, parameter 4 is to expire the device if any sensors are expired

                //now march through devices and send a ping to each if they are labeled as expired
                int16_t startIndex = -1;
                while (startIndex < NUMDEVICES) {
                    ArborysDevType* device = Sensors.getNextExpiredDevice(startIndex);
                    if (!device) break;
                    if (device->devType >= 100) continue; //don't send a ping to servers
                    if (device->dataSent > I.currentTime - 120) continue; //don't send a ping to devices that we have sent a ping to too recently
                    if (Sensors.countSensors(-1, Sensors.findDevice(device->MAC)) == 0) continue; //don't send a ping to devices that have no sensors
                    SerialPrint("Sensor expired: Sending sensor data request to " + String(device->devName),true);
                    
                    //now decide how to communicate with the device. The tiers are: no data within 5 minutes, no data within 10 minutes, and no data beyond 10 minutes
                    if (device->dataReceived < I.currentTime - 600) sendMSG_DataRequest(device, -1, true);
                    else if (device->dataReceived < I.currentTime - 120) {
                        sendMSG_DataRequest(device, -1, false);
                    } else {
                        sendESPNowSensorDataRequest(device, 1);
                    }
                    device->dataSent = I.currentTime;
                    delayWithNetwork(10,50);                    
                }
                //at a random point every 10 minutes, broadcast my presence (but it will only happen once every 10 minutes)
                if (I.makeBroadcast) {        //broadcast every 10 minutes, at some random second within the 10th minute        
                    broadcastServerPresence(true, 2);
                }
                
            }          
            
        #endif
    }

}



