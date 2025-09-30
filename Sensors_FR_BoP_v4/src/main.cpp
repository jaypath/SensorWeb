#include <Arduino.h>
#include "globals.hpp"




byte OldTime[5] = {0,0,0,0,0};

//function declarations
bool SendData(struct SensorVal*);
bool ReadData(struct SensorVal*);
void timeUpdate(void);
void handleRoot(void);              // function prototypes for HTTP handlers
void initSensor(byte);
void handleNotFound(void);
void handleSETTHRESH(void);
void handleLIST(void);
void handleUPDATESENSORPARAMS(void);
void handleUPDATEALLSENSORREADS(void);
void handleUPDATESENSORREAD(void);
void handleNEXT(void);
void handleLAST(void);




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
  #endif

  WEBHTML.reserve(20000);

  initSystem();

  initSensor(-256);

  if (!initSDCard()) return;
  loadScreenFlags();
  loadSensorData();


  tft.printf("Init Wifi... \n");
  SerialPrint("Init Wifi... ",false);
  tft.printf("Wifi... ");   
  SerialPrint("start server routes... ");
  initServerRoutes();
  SerialPrint("Server routes OK",true);

  while (connectWiFi()<0) {
      displaySetupProgress( false);
      tft.clear();
      tft.setCursor(0, 0);
      tft.printf("Wifi... ");    
      delay(10000); //do not flood wifi        
  } 
  displaySetupProgress( true);
  SerialPrint("Wifi OK",true);


  tft.print("Set up time... ");
  if (setupTime()) {
      displaySetupProgress( true);
  } else {
      displaySetupProgress( false);
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
          storeScreenInfoSD(); // Save the updated reset info
      }
  } else if (I.lastResetTime == 0) {
      // This is likely the first boot, set initial values
      I.resetInfo = RESET_DEFAULT;
      I.lastResetTime = I.currentTime;
      SerialPrint("First boot detected, setting initial reset info", true);
  }
  
  SerialPrint("Current IP Address: " + WiFi.localIP().toString(),true);
  SerialPrint("Prefs.MYIP: " + String(Prefs.MYIP),true);
  SerialPrint("Prefs.MYIP.toInt(): " + String((uint32_t) Prefs.MYIP),true);
  if (Prefs.DEVICENAME[0] == 0) {
      snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), MYNAME);
      Prefs.isUpToDate = false;        
  }
  WeatherData.lat = Prefs.LATITUDE;
  WeatherData.lon = Prefs.LONGITUDE;


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
  if (minute() % interval == 0 && I.lastESPNOW_TIME!=I.currentTime) {        
      // ESPNow does not require WiFi connection; always broadcast
      broadcastServerPresence();
  }
}

void handleStoreCoreData() {
  if (!Prefs.isUpToDate) { 
      Prefs.isUpToDate = true;
      BootSecure::setPrefs();        
  }

  if (!I.isUpToDate && I.lastStoreCoreDataTime + 300 < I.currentTime) { //store if more than 5 minutes since last store
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

































  //old code

  #ifdef _DEBUG
    Serial.begin(115200);
    SerialPrint("Begin Setup");
  #endif

#ifdef _USELED
  FastLED.addLeds<WS2813,_USELED,GRB>( LEDARRAY, LEDCOUNT).setCorrection(TypicalLEDStrip);

  LEDs.LED_animation_defaults(1);
  LEDs.LEDredrawRefreshMS=20;
  LEDs.LED_set_color(255,255,255,255,255,255); //default is white
#endif 
  
  #ifdef _USESOILRES
    pinMode(SOILDIO,OUTPUT);
    digitalWrite(SOILDIO, LOW);  
  #endif

//  Wire.begin(); 
//  Wire.setClock(400000L);
  



  WiFi.begin(ESP_SSID, ESP_PASS);
    SerialPrint("wifi begin");

  #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
  #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    #ifdef _DEBUG
    Serial.print(".");
    #endif
    #ifdef _USESSD1306
      oled.print(".");
    #endif
  }

//  MYIP.IP = WiFi.localIP();

    #ifdef _DEBUG
    Serial.println("");
    Serial.print("Wifi OK. IP is ");
    Serial.println(WiFi.localIP().toString());
    Serial.println("Connecting ArduinoOTA...");
    #endif

  #ifdef _DEBUG
    Serial.println("Connected!");
    Serial.println(WiFi.localIP());
  #endif

    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("WiFi OK.");
      oled.println("OTA start.");      
    #endif


    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname(ARDNAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    #ifdef _DEBUG
    Serial.println("OTA started");
    #endif
    #ifdef _USELED
      for (byte j=0;j<LEDCOUNT;j++) {
        LEDARRAY[j] = (uint32_t) 0<<16 | 255<<8|0; //green
         
      }
      FastLED.show();
    #endif
  });
  ArduinoOTA.onEnd([]() {
    #ifdef _DEBUG
    Serial.println("OTA End");
    #endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef _USELED
      for (byte j=0;j<LEDCOUNT;j++) {
        LEDARRAY[LEDCOUNT-j-1] = 0;
        if (j<=(double) LEDCOUNT*progress/total) LEDARRAY[LEDCOUNT-j-1] = (uint32_t) 64 <<16 | 64 << 8 | 64;
         
      }
      FastLED.show();
    #endif
    #ifdef _DEBUG
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
    #endif
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("Receiving OTA:");
      String strbuff = "Progress: " + (100*progress / total);
      oled.println("OTA start.");   
      oled.println(strbuff);
    #endif

  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef _DEBUG
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      String strbuff;
      strbuff = "Error[%u]: " + (String) error + " ";
      oled.print(strbuff);
      if (error == OTA_AUTH_ERROR) oled.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) oled.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) oled.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) oled.println("Receive Failed");
      else if (error == OTA_END_ERROR) oled.println("End Failed");
    #endif
  });
  ArduinoOTA.begin();
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("OTA OK.");      
    #endif

    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("start time.");
    #endif

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET);

    if (month() < 3 || (month() == 3 &&  day() < 12) || month() ==12 || (month() == 11 && day() > 5)) DSTOFFSET = -1*60*60;
    else DSTOFFSET = 0;

    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET); //set stoffregen timelib time once, to get month and day. then reset with DST

    //set the stoffregen time library with timezone and dst
    timeUpdate();



    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("start Server.");
    #endif

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/UPDATEALLSENSORREADS", handleUPDATEALLSENSORREADS);               
    server.on("/UPDATESENSORREAD",handleUPDATESENSORREAD);
    server.on("/SETTHRESH", handleSETTHRESH);               
    server.on("/LIST", handleLIST);
    server.on("/UPDATESENSORPARAMS", handleUPDATESENSORPARAMS);
    server.on("/NEXTSNS", handleNEXT);
    server.on("/LASTSNS", handleLAST);
    server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call
    server.begin();

    #ifdef _DEBUG
      Serial.println("HTML server started!");
    #endif


  /*
   * 
   * DEVICE SPECIFIC SETUP HERE!
   * 
   */
   //**************************************
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("Sns setup.");
    #endif


    //init globals
    for ( i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }
    #ifdef _USEBARPRED
      for (byte ii=0;ii<24;ii++) {
        BAR_HX[ii] = -1;
      }
      LAST_BAR_READ=0; 
    #endif
  
  
  #ifdef _USEAHT
    while (aht21.begin() != true)
    {
      Serial.println(F("AHT2x not connected or fail to load calibration coefficient")); //(F()) save string to flash & keeps dynamic memory free
    }

  #endif

  #ifdef DHTTYPE
    #ifdef _DEBUG
        Serial.println("dht begin");
    #endif
    dht.begin();
  #endif


  #ifdef _USEBMP
    #ifdef _DEBUG
        Serial.println("bmp begin");
    #endif
    uint32_t t = millis();
    uint32_t deltaT = 0;
    while (!bmp.begin(0x76) and deltaT<15000) { //default address is 0x77, but amazon review says this is 0x76
      deltaT = millis()-t;
      #ifdef _USESSD1306
        oled.println("BMP failed.");
        #ifdef _DEBUG
            Serial.println("bmp failed.");
        #endif
        delay(500);
        oled.clear();
        oled.setCursor(0,0);
        delay(500);
      #else
        digitalWrite(LED,HIGH);
        delay(500);
        digitalWrite(LED,LOW);
        delay(500);
      #endif
    }
 
    /* Default settings from datasheet. */
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */


  #endif
  #ifdef _USEBME
      #ifdef _DEBUG
        Serial.println("bme begin");
    #endif

    while (!bme.begin()) {
      #ifdef _USESSD1306
        oled.println("BME failed.");
        delay(500);
        oled.clear();
        oled.setCursor(0,0);
        delay(500);
      #else
        digitalWrite(LED,HIGH);
        delay(500);
        digitalWrite(LED,LOW);
        delay(500);
      #endif
    }
 
    /* Default settings from datasheet. */
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BME280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BME280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BME280::FILTER_X16,      /* Filtering. */
                  Adafruit_BME280::STANDBY_MS_500); /* Standby time. */


  #endif


  /*
  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  
  if(!LittleFS.begin()){
    #ifdef _DEBUG
      Serial.println("An Error has occurred while mounting LittleFS");
    #endif
    while (true) {
    }
  } else {
    #ifdef _DEBUG
      Serial.println("FileSys OK. Config Wifi");
    #endif

  }

*/




  for (i=0;i<SENSORNUM;i++) {
    Sensors[i].snsType=SENSORTYPES[i];
    Sensors[i].snsID=1; //increment this if there are others of the same type, should not occur
    Sensors[i].Flags = 0;
    if (bitRead(MONITORED_SNS,i)) bitWrite(Sensors[i].Flags,1,1);
    else bitWrite(Sensors[i].Flags,1,0);
    
    if (bitRead(OUTSIDE_SNS,i)) bitWrite(Sensors[i].Flags,2,1);
    else bitWrite(Sensors[i].Flags,2,0);

    switch (SENSORTYPES[i]) {
      case 1: //DHT temp
        #ifdef DHTTYPE
          Sensors[i].snsPin=DHTPIN;
          snprintf(Sensors[i].snsName,31,"%s_T", ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=2*60;          
        #endif
        break;
      case 2: //DHT RH
        #ifdef DHTTYPE
          Sensors[i].snsPin=DHTPIN;
          snprintf(Sensors[i].snsName,31,"%s_RH",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 25;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
          }
          Sensors[i].PollingInt=2*60;
          Sensors[i].SendingInt=5*60;
        #endif
        break;
      case 3: //soil
        #ifdef _USESOILCAP
          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soil",ARDNAME);
          Sensors[i].limitUpper = 150;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=600;
        #endif
        #ifdef _USESOILRES
          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soilR",ARDNAME);
          Sensors[i].limitUpper = 750;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=300;
        #endif

        break;
      case 4: //AHT temp
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_T",ARDNAME);
          Sensors[i].limitUpper = 115;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
      case 5:
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_RH",ARDNAME);
          Sensors[i].limitUpper = 85;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
  

      case 7: //dist
        Sensors[i].snsPin=0; //not used
        snprintf(Sensors[i].snsName,31,"%s_Dist",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = 10;
        Sensors[i].PollingInt=100;
        Sensors[i].SendingInt=100;
        break;
      case 9: //BMP pres
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 10: //BMP temp
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMP_t",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 11: //BMP alt
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60000;
        Sensors[i].SendingInt=60000;
        break;
      case 12: //Bar prediction
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_Pred",ARDNAME);
        Sensors[i].limitUpper = 0;
        Sensors[i].limitLower = 0; //anything over 0 is an alarm
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        bitWrite(Sensors[i].Flags,3,1); //calculated
        bitWrite(Sensors[i].Flags,4,1); //predictive
        break;
      case 13: //BME pres
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 14: //BMEtemp
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMEt",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case 15: //bme rh
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMErh",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 15;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case 16: //bme alt
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=15*60*60;
        Sensors[i].SendingInt=15*60*60;
        break;
    }

    Sensors[i].snsValue=0;
    Sensors[i].LastReadTime=0;
    Sensors[i].LastSendTime=0;  

  }


   //**************************************




  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println(WiFi.localIP().toString());
    oled.print(hour());
    oled.print(":");
    oled.println(minute());
  #endif
}



//sensor fcns
bool checkSensorValFlag(struct SensorVal *P) {
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, flag status changed and I have not sent data yet
  
  bool lastflag = false;
  bool thisflag = false;

  if (P->snsType==50 || P->snsType==55 || P->snsType==56 || P->snsType==57) { //HVAC is a special case
    lastflag = bitRead(P->Flags,0); //this is the last flag status
    if (P->LastsnsValue <  P->snsValue) { //currently flagged
      bitWrite(P->Flags,0,1); //currently flagged
      bitWrite(P->Flags,5,1); //value is high
      if (lastflag) {
        bitWrite(P->Flags,6,0); //no change in flag

      } else {
        bitWrite(P->Flags,6,1); //changed to high

      }
      return true; //flagged
    } else { //currently NOT flagged
      bitWrite(P->Flags,0,0); //currently not flagged
      bitWrite(P->Flags,5,0); //irrelevant
      if (lastflag) {
        bitWrite(P->Flags,6,1); // changed from flagged to NOT flagged

      } else {
        bitWrite(P->Flags,6,0); //no change (was not flagged, still is not flagged)

      }
        return false; //not flagged
    }
  }

  if (P->LastsnsValue>P->limitUpper || P->LastsnsValue<P->limitLower) lastflag = true;

  if (P->snsValue>P->limitUpper || P->snsValue<P->limitLower) {
    thisflag = true;
    bitWrite(P->Flags,0,1);

    //is it too high? write bit 5
    if (P->snsValue>P->limitUpper) bitWrite(P->Flags,5,1);
    else bitWrite(P->Flags,5,0);
  } 

  //now check for changes...  
  if (lastflag!=thisflag) {
    bitWrite(P->Flags,6,1); //change detected
    
  } else {
    bitWrite(P->Flags,6,0);
    
  }
  
  return bitRead(P->Flags,0);

}



uint8_t findSensor(byte snsType, byte snsID) {
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID == snsID && Sensors[j].snsType == snsType) return j; 
  }
  return 255;  
}

uint16_t findOldestDev() {
  int oldestInd = 0;
  int  i=0;
  for (i=0;i<SENSORNUM;i++) {
    if (Sensors[oldestInd].LastReadTime == 0) oldestInd = i;
    else if (Sensors[i].LastReadTime< Sensors[oldestInd].LastReadTime && Sensors[i].LastReadTime >0) oldestInd = i;
  }
  if (Sensors[oldestInd].LastReadTime == 0) oldestInd = 30000;

  return oldestInd;
}

void initSensor(byte k) {
  snprintf(Sensors[k].snsName,20,"");
  Sensors[k].snsID = 0;
  Sensors[k].snsType = 0;
  Sensors[k].snsValue = 0;
  Sensors[k].PollingInt = 0;
  Sensors[k].SendingInt = 0;
  Sensors[k].LastReadTime = 0;
  Sensors[k].LastSendTime = 0;  
  Sensors[k].Flags =0; 
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID > 0) c++; 
  }
  return c;
}


//Time fcn
String fcnDOW(time_t t) {
    if (weekday(t) == 1) return "Sun";
    if (weekday(t) == 2) return "Mon";
    if (weekday(t) == 3) return "Tue";
    if (weekday(t) == 4) return "Wed";
    if (weekday(t) == 5) return "Thu";
    if (weekday(t) == 6) return "Fri";
    if (weekday(t) == 7) return "Sat";

    return "???";
}

void timeUpdate() {
  timeClient.update();
  if (month() < 3 || (month() == 3 &&  day() < 12) || month() ==12 || (month() == 11 && day() > 5)) DSTOFFSET = -1*60*60;
  else DSTOFFSET = 0;

  setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  return;
}


void loop() {
  time_t t = now(); // store the current time in time variable t
  ArduinoOTA.handle();

  server.handleClient();
  
  
  #ifdef _USELED
    LEDs.LED_update();
  #endif 

  if (OldTime[0] != second()) {
  
    OldTime[0] = second();
    //do stuff every second
    bool flagstatus=false;
    
    #ifdef _DEBUG
      Serial.printf("Time is %s. Sensor #1 is %s.   ",dateify(t,"hh:nn:ss"),Sensors[0].snsName);

    #endif

    for (byte k=0;k<SENSORNUM;k++) {
      bool goodread = false;

      if (Sensors[k].LastReadTime==0 || Sensors[k].LastReadTime>t || Sensors[k].LastReadTime + Sensors[k].PollingInt < t || t- Sensors[k].LastReadTime >60*60*24 ) goodread = ReadData(&Sensors[k]); //read value if it's time or if the read time is more than 24 hours from now in either direction
      
      if (goodread == true) {
        if (Sensors[k].LastSendTime ==0 || Sensors[k].LastSendTime>t || Sensors[k].LastSendTime + Sensors[k].SendingInt < t || bitRead(Sensors[k].Flags,6) /* isflagged changed since last read and this value was not sent*/ || t - Sensors[k].LastSendTime >60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
      }
    }
  }
  
  if (OldTime[1] != minute()) {
    OldTime[1] = minute();

    #ifdef _USESSD1306
      redrawOled();
    #endif

  }
  
  if (OldTime[2] != hour()) {
    OldTime[2] = hour();
    timeUpdate();

  }
  
  if (OldTime[3] != weekday()) {
    OldTime[3] = weekday();
  
  //sync time
    #ifdef _DEBUG
      t=now();
      Serial.print("Current time is ");
      Serial.print(hour(t));
      Serial.print(":");
      Serial.print(minute(t));
      Serial.print(":");
      Serial.println(second(t));
    #endif

  }
  
 
}


bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum) {
    
    String temp;
    
    byte strLen = logID.length();
    byte strOffset = logID.indexOf(".", 0);
    if (strOffset == -1) { //did not find the . , logID not correct. abort.
      return false;
    } else {
      temp = logID.substring(0, strOffset);
      *ardID = temp.toInt();
      logID.remove(0, strOffset + 1);

      strOffset = logID.indexOf(".", 0);
      temp = logID.substring(0, strOffset);
      *snsID = temp.toInt();
      logID.remove(0, strOffset + 1);

      *snsNum = logID.toInt();
    }
    
    return true;
}


void handleUPDATESENSORPARAMS() {
  String stateSensornum = server.arg("SensorNum");

  byte j = stateSensornum.toInt();

  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorName") snprintf(Sensors[j].snsName,31,"%s", server.arg(i).c_str());

    if (server.argName(i)=="Monitored") {
      if (server.arg(i) == "0") bitWrite(Sensors[j].Flags,1,0);
      else bitWrite(Sensors[j].Flags,1,1);
    }

    if (server.argName(i)=="Critical") {
      if (server.arg(i) == "0") bitWrite(Sensors[j].Flags,7,0);
      else bitWrite(Sensors[j].Flags,7,1);
    }

    if (server.argName(i)=="Outside") {
      if (server.arg(i)=="0") bitWrite(Sensors[j].Flags,2,0);
      else bitWrite(Sensors[j].Flags,2,1);
    }

    if (server.argName(i)=="UpperLim") Sensors[j].limitUpper = server.arg(i).toDouble();

    if (server.argName(i)=="LowerLim") Sensors[j].limitLower = server.arg(i).toDouble();

    if (server.argName(i)=="SendInt") Sensors[j].SendingInt = server.arg(i).toDouble();
    if (server.argName(i)=="PollInt") Sensors[j].PollingInt = server.arg(i).toDouble();

  }

  ReadData(&Sensors[j]);
  SendData(&Sensors[j]);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page
}

void handleNEXT() {

  CURRENTSENSOR_WEB++;
  if (CURRENTSENSOR_WEB>SENSORNUM) CURRENTSENSOR_WEB = 1;


  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleLAST() {

  if (CURRENTSENSOR_WEB==1) CURRENTSENSOR_WEB = SENSORNUM;
  else CURRENTSENSOR_WEB--;
   

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleUPDATESENSORREAD() {

  ReadData(&Sensors[CURRENTSENSOR_WEB-1]);
  SendData(&Sensors[CURRENTSENSOR_WEB-1]);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page
}

void handleUPDATEALLSENSORREADS() {
String currentLine = "";

currentLine += "Current time ";
currentLine += (String) now();
currentLine += " = ";
currentLine += (String) dateify() + "\n";


for (byte k=0;k<SENSORNUM;k++) {
  ReadData(&Sensors[k]);      
  currentLine +=  "Sensor " + (String) Sensors[k].snsName + " data sent to at least 1 server: " + SendData(&Sensors[k]) + "\n";
}
  server.send(200, "text/plain", "Status...\n" + currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client

}


void handleSETTHRESH() {
byte ardid, snsNum, snsType;
String strTemp;
double limitUpper=-1, limitLower=-1;
uint16_t PollingInt=0;
  uint16_t SendingInt=0;
byte k;

  for (k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"LOGID") breakLOGID(server.arg(k),&ardid,&snsType,&snsNum);
    if ((String)server.argName(k) == (String)"UPPER") {
      strTemp = server.arg(k);      
      limitUpper=strTemp.toDouble();
    }
    if ((String)server.argName(k) == (String)"LOWER") {
      strTemp = server.arg(k);      
      limitLower=strTemp.toDouble();
    }
    if ((String)server.argName(k) == (String)"POLLINGINT") {
      strTemp = server.arg(k);      
      PollingInt=strTemp.toInt();
    }
    if ((String)server.argName(k) == (String)"SENDINGINT") {
      strTemp = server.arg(k);      
      SendingInt=strTemp.toInt();
    }

    
  }

  k = findSensor(snsType,snsNum);
  if (k<100) {
    if (limitLower != -1) Sensors[k].limitLower = limitLower;
    if (limitUpper != -1) Sensors[k].limitUpper = limitUpper;
    if (PollingInt>0) Sensors[k].PollingInt = PollingInt;
    if (SendingInt>0) Sensors[k].SendingInt = SendingInt;
    checkSensorValFlag(&Sensors[k]);
    String currentLine = "";
    byte j=k;
    currentLine += (String) dateify() + "\n";

    currentLine = currentLine + "ARDID:" + String(ARDID, DEC) + "; snsType:"+(String) Sensors[j].snsType+"; snsID:"+ (String) Sensors[j].snsID + "; SnsName:"+ (String) Sensors[j].snsName + "; LastRead:"+(String) Sensors[j].LastReadTime+"; LastSend:"+(String) Sensors[j].LastSendTime + "; snsVal:"+(String) Sensors[j].snsValue + "; UpperLim:"+(String) Sensors[j].limitUpper + "; LowerLim:"+(String) Sensors[j].limitLower + "; Flag:"+(String) bitRead(Sensors[j].Flags,0) + "; Monitored: " + (String) bitRead(Sensors[j].Flags,1) + "\n";
    server.send(400, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
  } else {
    server.send(400, "text/plain", "That sensor was not found");   // Send HTTP status 400 as error
  }
}

void handleLIST() {
String currentLine = "";
currentLine =  "IP:" + WiFi.localIP().toString() + "\nARDID:" + String( ARDID, DEC) + "\n";
currentLine += (String) dateify(now(),"mm/dd/yyyy hh:nn:ss") + "\n";

  for (byte j=0;j<SENSORNUM;j++)  {
    currentLine += "     ";
    currentLine +=  "snsType: ";
    currentLine += String(Sensors[j].snsType,DEC);
    currentLine += "\n";

    currentLine += "     ";
    currentLine += "snsID: ";
    currentLine +=  String(Sensors[j].snsID,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "SnsName: ";
    currentLine +=  (String) Sensors[j].snsName;
    currentLine += "\n";

    currentLine += "     ";
    currentLine +=  "snsVal: ";
    currentLine +=  String(Sensors[j].snsValue,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "LastRead: ";
    currentLine += String(Sensors[j].LastReadTime,DEC);
    currentLine += " = ";
    currentLine += (String) dateify(Sensors[j].LastReadTime);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "LastSend: ";
    currentLine +=  String(Sensors[j].LastSendTime,DEC);
    currentLine += " = ";
    currentLine += (String) dateify(Sensors[j].LastSendTime);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "UpperLim: ";
    currentLine += String(Sensors[j].limitUpper,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "LowerLim: ";
    currentLine +=  String(Sensors[j].limitLower,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "Flag: ";
    currentLine +=  (String) bitRead(Sensors[j].Flags,0);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "Monitored: ";
    currentLine +=  (String) bitRead(Sensors[j].Flags,1);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "Flags: ";
    char cbuff[9] = "";
    Byte2Bin(Sensors[j].Flags,cbuff,1);

    #ifdef _DEBUG
        Serial.print("SpecType after byte2bin: ");
        Serial.println(cbuff);
    #endif

    currentLine +=  cbuff;
    currentLine += "\n\n";
  }
   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif
  server.send(200, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleRoot() {
char temp[30] = "";

String currentLine = "<!DOCTYPE html><html><head><title>Arduino Server Page</title></head><body>";

//currentLine += "<h1></h1>";

currentLine =  "<h2>Arduino: " + (String) ARDNAME + "<br>IP:" + WiFi.localIP().toString() + "<br>ARDID:" + String( ARDID, DEC) + "<br></h2><p>";
currentLine += "Current time ";
currentLine += (String) now();
currentLine += " = ";
currentLine += (String) dateify(now(),"mm/dd/yyyy hh:nn:ss");
currentLine += "<br>";
currentLine += "<a href=\"/LIST\">List all sensors</a><br>";
currentLine += "<a href=\"/UPDATEALLSENSORREADS\">Update all sensors</a><br>";

currentLine += "<br>-----------------------<br>";

  byte j=CURRENTSENSOR_WEB-1;
  currentLine += "SENSOR NUMBER: " + String(j,DEC);
  if (bitRead(Sensors[j].Flags,0)) currentLine += " !!!!!ISFLAGGED!!!!!";
  currentLine += "<br>";
  currentLine += "-----------------------<br>";


/*
  temp = FORM_page;
  
  temp.replace("@@SNSNUM@@",String(j,DEC));
  temp.replace("@@FLAG1@@",String(bitRead(Sensors[j].Flags,1),DEC));
  temp.replace("@@FLAG2@@",String(bitRead(Sensors[j].Flags,2),DEC));
  temp.replace("@@UPPERLIM@@",String(Sensors[j].limitUpper,DEC));
  temp.replace("@@LOWERLIM@@",String(Sensors[j].limitLower,DEC));
  temp.replace("@@POLL@@",String(Sensors[j].PollingInt,DEC));
  temp.replace("@@SEND@@",String(Sensors[j].SendingInt,DEC));
  temp.replace("@@CURRENTSENSOR@@",String(j,DEC));

  currentLine += temp;
*/

  currentLine += "<FORM action=\"/UPDATESENSORPARAMS\" method=\"get\">";
  currentLine += "<p style=\"font-family:monospace\">";

  currentLine += "<input type=\"hidden\" name=\"SensorNum\" value=\"" + (String) j + "\"><br>";  


  char padder[2] = ".";
  snprintf(temp,29,"%s","Sensor Name");
  strPad(temp,padder,25);
  currentLine += "<label for=\"MyName\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"MyName\" name=\"SensorName\" value=\"" + String(Sensors[j].snsName) + "\" maxlength=\"30\"><br>";  

  snprintf(temp,29,"%s","Sensor Value");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Val\">" + (String) temp + " " + String(Sensors[j].snsValue,DEC) + "</label>";
  currentLine +=  "<br>";
  
  snprintf(temp,29,"%s","Is Monitored");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Mon\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Mon\" name=\"Monitored\" value=\"" + String(bitRead(Sensors[j].Flags,1),DEC) + "\"><br>";  

  snprintf(temp,29,"%s","Is Critical");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Crit\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Crit\" name=\"Critical\" value=\"" + String(bitRead(Sensors[j].Flags,7),DEC) + "\"><br>";  

snprintf(temp,29,"%s","Is Outside");
strPad(temp,padder,25);
  currentLine += "<label for=\"Out\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Out\" name=\"Outside\" value=\"" + String(bitRead(Sensors[j].Flags,2),DEC) + "\"><br>";  

snprintf(temp,29,"%s","Upper Limit");
strPad(temp,padder,25);
  currentLine += "<label for=\"UL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"UL\" name=\"UpperLim\" value=\"" + String(Sensors[j].limitUpper,DEC) + "\"><br>";

snprintf(temp,29,"%s","Lower Limit");
strPad(temp,padder,25);
  currentLine += "<label for=\"LL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"LL\" name=\"LowerLim\" value=\"" + String(Sensors[j].limitLower,DEC) + "\"><br>";

snprintf(temp,29,"%s","Poll Int (sec)");
strPad(temp,padder,25);
  currentLine += "<label for=\"POLL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"POLL\" name=\"PollInt\" value=\"" + String(Sensors[j].PollingInt,DEC) + "\"><br>";

snprintf(temp,29,"%s","Send Int (Sec)");
strPad(temp,padder,25);
  currentLine += "<label for=\"SEND\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"SEND\" name=\"SendInt\" value=\"" + String(Sensors[j].SendingInt,DEC) + "\"><br>";

/*
  snprintf(temp,29,"%s","Recheck Sensor");
  strPad(temp,padder,25);
  currentLine += "<label for=\"recheck\" class=\"switch\">" + (String) temp;
  currentLine += "<input id=\"recheck\" type=\"checkbox\" name=\"recheckSensor\"><span class=\"slider round\"></span></label><br>";
 */
 
  currentLine += "<button type=\"submit\">Submit</button><br><br>";
  currentLine += "<button type=\"submit\" formaction=\"/UPDATESENSORREAD\">Recheck this sensor</button><br><br>";
  currentLine += "<button type=\"submit\" formaction=\"/LASTSNS\">Prior Sensor</button>";
  currentLine += "<button type=\"submit\" formaction=\"/NEXTSNS\">Next Sensor</button>";
  currentLine += "</p>";
  currentLine += "</form>";
  
  currentLine += "<br>";

  currentLine += "     ";
  currentLine +=  "snsType: ";
  currentLine += String(Sensors[j].snsType,DEC);
  currentLine += "<br>";

  currentLine += "     ";
  currentLine += "snsID: ";
  currentLine +=  String(Sensors[j].snsID,DEC);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "LastRead: ";
  currentLine += String(Sensors[j].LastReadTime,DEC);
  currentLine += " = ";
  currentLine += (String) dateify(Sensors[j].LastReadTime);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "LastSend: ";
  currentLine +=  String(Sensors[j].LastSendTime,DEC);
  currentLine += " = ";
  currentLine += (String) dateify(Sensors[j].LastSendTime);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "Flags: ";
  temp[8] = 0;//make sure no characters after the first 8 shown
  Byte2Bin(Sensors[j].Flags,temp);
  currentLine +=  (String) temp + "<br>";



/*
  currentLine += "     ";
  currentLine +=  "Monitored: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,1);
  currentLine +=  "<br>";
*/

  currentLine += "     ";
  currentLine +=  "IsFlagged: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,0);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "Calculated Value: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,3);
  currentLine += "<br>";

  currentLine += "     ";
  currentLine +=  "Predictive Value: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,4);
  currentLine += "<br>";

  currentLine += "     ";
  currentLine +=  "UpperLim: ";
  currentLine += String(Sensors[j].limitUpper,DEC);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "LowerLim: ";
  currentLine +=  String(Sensors[j].limitLower,DEC);
  currentLine +=  "<br>";


  currentLine += "-----------------------<br>";

  #ifdef _USEBARPRED
    currentLine += "Hourly_air_pressures (most recent [top] entry was ";
    currentLine +=  (String) dateify(LAST_BAR_READ);
    currentLine += "):<br>";

    for (byte j=0;j<24;j++)  {
      currentLine += "     ";
      currentLine += String(BAR_HX[j],DEC);
      currentLine += "<br>"; 
    }

  #endif 

currentLine += "</p></body></html>";

   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif

    //IF USING PROGMEM: use send_p   !!
  server.send(200, "text/html", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleNotFound(){
  server.send(404, "text/plain", "Arduino says 404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

bool SendData(struct SensorVal *snsreading) {
  if (bitRead(snsreading->Flags,1) == 0) return false;
  
#ifdef _DEBUG
  Serial.printf("SENDDATA: Sending data from %s. \n", snsreading->snsName);
#endif

  WiFiClient wfclient;
  HTTPClient http;
    
    byte ipindex=0;
    bool isGood = false;

      #ifdef _DEBUG
        Serial.println(" ");
      Serial.println("*****************");
      Serial.println("Sending Data");
      Serial.print("Device: ");
          Serial.println(snsreading->snsName);
      Serial.print("SnsType: ");
          Serial.println(snsreading->snsType);
      Serial.print("Value: ");
          Serial.println(snsreading->snsValue);
      Serial.print("LastLogged: ");
          Serial.println(snsreading->LastReadTime);
      Serial.print("isFlagged: ");
          Serial.println(bitRead(snsreading->Flags,0));
      Serial.print("isMonitored: ");
          Serial.println(bitRead(snsreading->Flags,1));
      Serial.print("Flags: ");
          Serial.println(snsreading->Flags);          

      #endif


  
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String URL;
    String tempstring;
    int httpCode=404;
    tempstring = "/POST?IP=" + WiFi.localIP().toString() + "," + "&varName=" + String(snsreading->snsName);
    tempstring = tempstring + "&varValue=";
    tempstring = tempstring + String(snsreading->snsValue, DEC);
    tempstring = tempstring + "&Flags=";
    tempstring = tempstring + String(snsreading->Flags, DEC);
    tempstring = tempstring + "&logID=";
    tempstring = tempstring + String(ARDID, DEC);
    tempstring = tempstring + "." + String(snsreading->snsType, DEC) + "." + String(snsreading->snsID, DEC) + "&timeLogged=" + String(snsreading->LastReadTime, DEC) + "&isFlagged=" + String(bitRead(snsreading->Flags,0), DEC);

    do {
      URL = "http://" + SERVERIP[ipindex].IP.toString();
      URL = URL + tempstring;
    
      http.useHTTP10(true);
    //note that I'm coverting lastreadtime to GMT
  
      snsreading->LastSendTime = now();
        #ifdef _DEBUG
            Serial.print("sending this message: ");
            Serial.println(URL.c_str());
        #endif

      http.begin(wfclient,URL.c_str());
      httpCode = (int) http.GET();
      payload = http.getString();
      http.end();

        #ifdef _DEBUG
          Serial.print("Received: ");
          Serial.println(payload);
          Serial.print("Code: ");
          Serial.println(httpCode);
          Serial.println("*****************");
          Serial.println(" ");
        #endif

    #ifdef _DEBUG
      Serial.printf("------>SENDDATA: Sensor is now named %s. \n", snsreading->snsName);
    #endif

        if (httpCode == 200) {
          isGood = true;
          SERVERIP[ipindex].server_status = httpCode;
        } 
    #ifdef _DEBUG
      Serial.printf("------>SENDDATA: Sensor is now named %s. \n", snsreading->snsName);
    #endif

    #ifdef _DEBUG
      Serial.printf("SENDDATA: Sent to %d. Sensor is now named %s. \n", ipindex,snsreading->snsName);
    #endif

      ipindex++;

    } while(ipindex<NUMSERVERS);
  
    
  }
#ifdef _DEBUG
  Serial.printf("SENDDATA: End of sending data. Sensor is now named %s. \n", snsreading->snsName);
#endif

     return isGood;


}


bool ReadData(struct SensorVal *P) {
      double val;

  time_t t=now();
  bitWrite(P->Flags,0,0);
  
    P->LastsnsValue = P->snsValue;

  switch (P->snsType) {
    case 1: //DHT temp
    {
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  (dht.readTemperature()*9/5+32);
      #endif
      break;
    }
    case 2: //DHT RH
    {
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue = dht.readHumidity();
      #endif
      break;
    }
    case 3: //soil
    {
        #ifdef _USESOILCAP
        //soil moisture v1.2
        val = analogRead(P->snsPin);
        //based on experimentation... this eq provides a scaled soil value where 0 to 100 corresponds to 450 to 800 range for soil saturation (higher is dryer. Note that water and air may be above 100 or below 0, respec
        val =  (int) ((-0.28571*val+228.5714)*100); //round value
        P->snsValue =  val/100;
      #endif

      #ifdef _USESOILRES
        //soil moisture by stainless steel probe unit (Resistance is characterized by a voltage signal, that varies between 0 (wet) and ~500 (dry) on a 1024 resolution scale 0 to 3.3v)
        
        val=0;
        uint8_t nsamp=100;
          digitalWrite(SOILDIO, HIGH);
          delay(100);
        
        for (uint8_t ii=0;ii<nsamp;ii++) {        
          val += analogRead(P->snsPin);
          
          delay(1); //wait X ms for reading to settle
        }
        digitalWrite(SOILDIO, LOW);
        val=val/nsamp;

        //convert val to voltage
        val = 3.3 * (val / _ADCRATE);

        //the chip I am using is a voltage divider with a 10K r1. 
        //equation for R2 is R2 = R1 * (V2/(V-v2))

        P->snsValue = (double) 10000 * (val/(3.3-val));



        #ifdef _USELED
          LEDs.LED_set_color_soil(P);
        #endif 

      #endif

      break;
    }
    case 4: //AHT Temp
    {
      #ifdef _USEAHT
        //AHT21 temperature
          val = aht21.readTemperature();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (100*(val*9/5+32))/100; 
            #ifdef _DEBUG
              Serial.print(F("AHT Temperature...: "));
              Serial.print(P->snsValue);
              Serial.println(F("F"));
            #endif
          }
          else
          {
            #ifdef _DEBUG
              Serial.print(F("AHT Temperature Error"));
            #endif
          }
      #endif
      break;
    }
    case 5: //AHT RH
    {
      //AHT21 humidity
        #ifdef _USEAHT
          val = aht21.readHumidity();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (val*100)/100; 
            #ifdef _DEBUG
              Serial.print(F("AHT HUmidity...: "));
              Serial.print(P->snsValue);
              Serial.println(F("%"));
            #endif
          }
          else
          {
            #ifdef _DEBUG
              Serial.print(F("AHT Humidity Error"));
            #endif
          }
      #endif
      break;
    }
    case 7: //dist
    {
      #ifdef _USEHCSR04
        #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
        digitalWrite(TRIGPIN, LOW);
        delayMicroseconds(2);
        //Now we'll activate the ultrasonic ability
        digitalWrite(TRIGPIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIGPIN, LOW);

        //Now we'll get the time it took, IN MICROSECONDS, for the beam to bounce back
        long duration = pulseIn(ECHOPIN, HIGH);

        //Now use duration to get distance in units specd b USONIC_DIV.
        //We divide by 2 because it took half the time to get there, and the other half to bounce back.
        P->snsValue = (duration / USONIC_DIV); 
      #endif

      break;
    }
    case 9: //BMP pres
    {
      #ifdef _USEBMP
         P->snsValue = bmp.readPressure()/100; //in hPa
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            P->limitLower = 1000;
          } else {
            P->limitLower = 1009;
          }

          if (LAST_BAR_READ+60*60 < t) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = t;          
          }
        #endif

      #endif
      break;
    }
    case 10: //BMP temp
    {
        #ifdef _USEBMP
        P->snsValue = ( bmp.readTemperature()*9/5+32);
      #endif
      break;
    case 11: //BMP alt
      #ifdef _USEBMP
         P->snsValue = (bmp.readAltitude(1013.25)); //meters
      #endif
      break;
    case 12: //make a prediction about weather
      #ifdef _USEBARPRED
        /*rules: 
        3 rise of 10 in 3 hrs = gale
        2 rise of 6 in 3 hrs = strong winds
        1 rise of 1.1 and >1015 = poor weather
        -1 fall of 1.1 and <1009 = rain
        -2 fall of 4 and <1023 = rain
        -3 fall of 4 and <1009 = storm
        -4 fall of 6 and <1009 = strong storm
        -5 fall of 7 and <990 = very strong storm
        -6 fall of 10 and <1009 = gale
        -7 fall of 4 and fall of 8 past 12 hours and <1005 = severe tstorm
        -8 fall of 24 in past 24 hours = weather bomb
        //https://www.worldstormcentral.co/law%20of%20storms/secret%20law%20of%20storms.html
        */        
        //fall of >3 hPa in 3 hours and P<1009 = storm
        P->snsValue = 0;
        if (BAR_HX[2]>0) {
          if (BAR_HX[0]-BAR_HX[2] >= 1.1 && BAR_HX[0] >= 1015) {
            P->snsValue = 1;
            snprintf(WEATHER,22,"Poor Weather");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 6) {
            P->snsValue = 2;
            snprintf(WEATHER,22,"Strong Winds");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 10) {
            P->snsValue = 3;        
            snprintf(WEATHER,22,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 1.1 && BAR_HX[0] <= 1009) {
            P->snsValue = -1;
            snprintf(WEATHER,22,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1023) {
            P->snsValue = -2;
            snprintf(WEATHER,22,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1009) {
            P->snsValue = -3;
            snprintf(WEATHER,22,"Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 6 && BAR_HX[0] <= 1009) {
            P->snsValue = -4;
            snprintf(WEATHER,22,"Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 7 && BAR_HX[0] <= 990) {
            P->snsValue = -5;
            snprintf(WEATHER,22,"Very Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 10 && BAR_HX[0] <= 1009) {
            P->snsValue = -6;
            snprintf(WEATHER,22,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[11]-BAR_HX[0] >= 8 && BAR_HX[0] <= 1005) {
            P->snsValue = -7;
            snprintf(WEATHER,22,"TStorm");
          }
          if (BAR_HX[23]-BAR_HX[0] >= 24) {
            P->snsValue = -8;
            snprintf(WEATHER,22,"BOMB");
          }
        }
      #endif
      break;
    }
    case 13: //BME pres
    {
      #ifdef _USEBME
         P->snsValue = bme.readPressure(); //in Pa
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            P->limitLower = 1000;
          } else {
            P->limitLower = 1009;
          }

          if (LAST_BAR_READ+60*60 < t) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = t;          
          }
        #endif
      #endif
      break;
    }
    case 14: //BMEtemp
    {
      #ifdef _USEBME
        P->snsValue = (( bme.readTemperature()*9/5+32) );
      #endif
      break;
    case 15: //bme rh
      #ifdef _USEBME
      
        P->snsValue = ( bme.readHumidity() );
      #endif
      break;
    case 16: //BMEalt
      #ifdef _USEBME
         P->snsValue = (bme.readAltitude(1013.25)); //meters

      #endif
      break;
    }
  }

  checkSensorValFlag(P); //sets isFlagged
  P->LastReadTime = t; //localtime
  

#ifdef _DEBUG
      Serial.println(" ");
      Serial.println("*****************");
      Serial.println("Reading Sensor");
      Serial.print("Device: ");
          Serial.println(P->snsName);
      Serial.print("SnsType: ");
          Serial.println(P->snsType);
      Serial.print("Value: ");
          Serial.println(P->snsValue);
      Serial.print("LastLogged: ");
          Serial.println(P->LastReadTime);
      Serial.print("isFlagged: ");
          Serial.println(bitRead(P->Flags,0));          
      Serial.println("*****************");
      Serial.println(" ");

      #endif


return true;
}

