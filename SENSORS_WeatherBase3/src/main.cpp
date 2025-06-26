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

void fcnAssignWeatherArray(uint32_t* timeval, int16_t* wthrArr, byte nval, String payload, double multiplier=1.00);
void checkHeat();

void setup()
{
  //watchdog reset
  esp_task_wdt_deinit(); //wdt is enabled by default, so we need to deinit it first
  //esp_task_wdt_init(&WDT_CONFIG); //setup watchdog 
  esp_task_wdt_init(WDT_TIMEOUT_MS,true);
  esp_task_wdt_add(NULL);                            //add the current thread
  
  WEBHTML.reserve(7000); 
  #ifdef _DEBUG
    Serial.begin(115200);
  #endif

  #ifdef _DEBUG
    Serial.print("SPI begin and screen begin... ");
  #endif

  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
  tft.init();

  #ifdef _DEBUG
    Serial.println("ok ");
  #endif

  // Setting display to landscape
  tft.setRotation(2);

  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(2);
//  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM); 
  
  tft.printf("Running setup\n");

  //init globals
  initSensor(-256);

  tft.print("SD Card mounting...");

  if(!SD.begin(41)){  //CS=41
    displaySetupProgress("SD Card mounting", false);
    #ifdef _DEBUG
      Serial.println("SD mount failed... ");
    #endif
    delay(10000);
    I.resetInfo = RESET_SD;
    I.lastResetTime = I.currentTime;
    //can't dump to SD... since it didn't mount :(
    controlledReboot("SD Card failed",RESET_SD);
  } 
  else {
    #ifdef _DEBUG
      Serial.println("SD mounted... ");
    #endif
    displaySetupProgress("SD Card mounting", true);

     tft.print("Loading sensor data from SD... ");
     bool sdread = readSensorsSD();
     if (sdread) {
      displaySetupProgress("Loading sensor data from SD", true);
     } else {
      displaySetupProgress("Loading sensor data from SD", false);
      delay(5000);
     }

    #ifdef _DEBUG
      Serial.print("Sensor data loaded? ");
      Serial.println((sdread==true)?"yes":"no");
    #endif

     tft.print("Loading screen flags from SD... ");
     sdread = readScreenInfoSD();
     if (sdread) {
      displaySetupProgress("Loading screen flags from SD", true);
     } else {
      displaySetupProgress("Loading screen flags from SD", false);
      delay(5000);
     }
  }
    
  if (I.SERVERNAME[0] == 0) {
    snprintf(I.SERVERNAME, sizeof(I.SERVERNAME), "Pleasant Weather Server");
  }
  I.ScreenNum = 0;
  I.isFlagged = false;
  I.wasFlagged = false;

  WeatherData.lat = LAT;
  WeatherData.lon = LON;

  


  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/ALLSENSORS", handleALL);               // Call the 'handleall' function when a client requests URI "/"
  server.on("/POST", handlePost);   
  server.on("/REQUESTUPDATE",handleREQUESTUPDATE);
  server.on("/CLEARSENSOR",handleCLEARSENSOR);
  server.on("/TIMEUPDATE",handleTIMEUPDATE);
  server.on("/REQUESTWEATHER",handleREQUESTWEATHER);
  server.on("/GETSTATUS",handleGETSTATUS);
  server.on("/REBOOT",handleReboot);
  server.on("/UPDATEDEFAULTS",handleUPDATEDEFAULTS);
  server.on("/RETRIEVEDATA",handleRETRIEVEDATA);
  server.on("/FLUSHSD",handleFLUSHSD);
  server.on("/SETWIFI", HTTP_POST, handleSETWIFI);
  server.onNotFound(handleNotFound);
  


  tft.println("Connecting  Wifi...\n");

    
  #ifdef IGNORETHIS
  WIFI_INFO.DHCP = IPAddress(192,168,68,1);
  WIFI_INFO.DNS = IPAddress(192,168,68,1);
  WIFI_INFO.GATEWAY = IPAddress(192,168,68,1);
  WIFI_INFO.SUBNET = IPAddress(255,255,252,0);
  WIFI_INFO.MYIP = IPAddress (192,168,68,0);
  #endif
  
  

  WIFI_INFO.HAVECREDENTIALS = false;
  

  #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
  #endif



  byte retries = connectWiFi();

  if (retries==255) {
    //I am in AP mode!
    tft.clear();
    tft.setCursor(0,0);
    initCreds();

    uint32_t y = setFont(4);
    fcnPrintTxtCenter("HELLO!",4,TFT_WIDTH/2,y/2); //note that setFont() returns the font height

    tft.setCursor(0,y+5);
    setFont(1);
    tft.printf("Or connect to wifi ESPSERVER pwd=CENTRAL.server1\nand go to web site http://192.168.10.1\n");
    tft.printf("Touch anywhere above keypad to restart entry.\n");
    
    y=tft.getCursorY()+2;
    
    server.begin(); //in case user wants to use browser

    //for on screen keypad
    uint8_t keyPage = 0;
    uint8_t WiFiSet=0; //1 = SSID set, 2 = SSID and PWD set
    uint8_t entryI = 0;
    drawKeyPad4WiFi(y,keyPage,WiFiSet);

    int16_t keyval = 0;

    while (WiFiSet<2) {
      if (tft.getTouch(&I.touchX, &I.touchY)) {
        if (isTouchKey(&keyval, &keyPage)) {
          if (keyval == 256) {
            //clear
            if (WiFiSet==0) {
              memset(WIFI_INFO.SSID,0,sizeof(WIFI_INFO.SSID));
            } else {
              memset(WIFI_INFO.PWD,0,sizeof(WIFI_INFO.PWD));
            }
            entryI = 0;
          } else if (keyval == 257) {
            //backspace
            if (WiFiSet==0) {
              if (entryI>0) {
                WIFI_INFO.SSID[--entryI] = 0;
              }
            } else {
              if (entryI>0) {
                WIFI_INFO.PWD[--entryI] = 0;
              }
            }
          } else if (keyval == 300) {
            //page down
            if (keyPage>0) keyPage--;
          } else if (keyval == 301) {
            //page up
            if (keyPage<3) keyPage++;
          } else if (keyval == 32) {
            //space
            if (WiFiSet==0) {
              if (entryI<sizeof(WIFI_INFO.SSID)-1) {
                WIFI_INFO.SSID[entryI++] = ' ';
                WIFI_INFO.SSID[entryI] = 0;
              }
            } else {
              if (entryI<sizeof(WIFI_INFO.PWD)-1) {
                WIFI_INFO.PWD[entryI++] = ' ';
                WIFI_INFO.PWD[entryI] = 0;
              }
            }
          } else if (keyval < 0) {
            //submit
            if (WiFiSet==0) {
              WiFiSet = 1;
              entryI = 0;
            } else {
              WiFiSet = 2;
            }
            drawKeyPad4WiFi(y,keyPage,WiFiSet);
          } else if (keyval >= 33 && keyval < 128) {
            //normal character
            if (WiFiSet==0) {
              if (entryI<sizeof(WIFI_INFO.SSID)-1) {
                WIFI_INFO.SSID[entryI++] = keyval;
                WIFI_INFO.SSID[entryI] = 0;
              }
            } else {
              if (entryI<sizeof(WIFI_INFO.PWD)-1) {
                WIFI_INFO.PWD[entryI++] = keyval;
                WIFI_INFO.PWD[entryI] = 0;
              }
            }
          }
          drawKeyPad4WiFi(y,keyPage,WiFiSet);
        }
      }
      delay(100);
    }

    tft.clear();
    tft.setCursor(0,0);
    tft.setTextFont(2);
    tft.println("WIFI credentials entered... rebooting in 10 seconds");
    delay(10000);
    controlledReboot("WiFi credentials entered",RESET_WIFI);
  }

  if (WiFi.status() != WL_CONNECTED) {
    displayWiFiStatus(retries, false);
    delay(120000);
    I.resetInfo = RESET_WIFI;
    I.lastResetTime = I.currentTime;
    controlledReboot("WiFi failed",RESET_WIFI);
  } else {
    displayWiFiStatus(retries, true);

    tft.printf("Internal network ");
    if (WIFI_INFO.MYIP[0] == 192 && WIFI_INFO.MYIP[1] == 168) {
      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
    } else {
      tft.setTextColor(TFT_RED);
      tft.printf("FAIL\n");
    }
    tft.setTextColor(FG_COLOR);
  }

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
    
    // Broadcast initial server presence
    broadcastServerPresence();
  } else {
    tft.setTextColor(TFT_RED);
    tft.printf("FAILED.\n");
    tft.setTextColor(FG_COLOR);
    storeError("ESPNow initialization failed");
  }

  tft.print("Connecting ArduinoOTA... ");
  ArduinoOTA.setHostname("WeatherStation");
  ArduinoOTA.setPassword("12345678");
  ArduinoOTA.onStart([]() {
    displayOTAProgress(0, 100);
  });
  ArduinoOTA.onEnd([]() {
    tft.setTextSize(1);
    tft.println("OTA End. About to reboot!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    displayOTAProgress(progress, total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    displayOTAError(error);
  });
  ArduinoOTA.begin();
  tft.setTextColor(TFT_GREEN);
  tft.printf("OK.\n");
  tft.setTextColor(FG_COLOR);

  tft.print("Set up time... ");
  if (setupTime()) {
    displaySetupProgress("Set up time", true);
  } else {
    displaySetupProgress("Set up time", false);
  }

  I.ALIVESINCE = I.currentTime;

  tft.setTextColor(TFT_GREEN);
  tft.printf("Setup OK...");
  tft.setTextColor(FG_COLOR);
  tft.printf("Waiting for weather to load...");
}

// Non-graphical functions
void fcnAssignWeatherArray(uint32_t* timeval, int16_t* wthrArr, byte nval, String payload, double multiplier=1.00) {
  byte strOffset = 0;
  String tempPayload;
  char buf[12] = "";

  //process the first value, which is the time
  strOffset = payload.indexOf(",",0);
  tempPayload = payload.substring(0,strOffset);
  tempPayload.toCharArray(buf,12);
  *timeval = strtoul(buf,NULL,0); //do it this way because it is a 32 bit number, so toInt will fail
  payload.remove(0,strOffset+1); //include the comma

  //now iterate through the rest of the values!
  byte i=0;
  while (payload.length()>0 && i<nval) {
    strOffset = payload.indexOf(",",0);
    if (strOffset == -1) { //did not find the comma, we may have cut the last one
      wthrArr[i] = int16_t(payload.toDouble()*multiplier);
      payload = "";
    } else {
      tempPayload = payload.substring(0,strOffset);
      wthrArr[i] = int16_t(payload.toDouble()*multiplier);
      payload.remove(0,strOffset+1);
    }
    i++;   
  }
}

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

void loop() {
  esp_task_wdt_reset(); //reset the watchdog!

  I.currentTime = now(); // store the current time in time variable t

  if (WiFi.status() != WL_CONNECTED) {
    byte retries = connectWiFi();

    //if still not connected then increment fail counter
    if (WiFi.status() != WL_CONNECTED)     I.wifiFailCount++; //still not connected
  } 
  
  if (WiFi.status() == WL_CONNECTED) {
    I.wifiFailCount=0;
    ArduinoOTA.handle();
    server.handleClient();
    updateTime(1,0); //just try once  
  }
   
  if (OldTime[1] != minute()) {

    if (I.wifiFailCount>5) {
      controlledReboot("Wifi failed so resetting",RESET_WIFI,true);
    }

    WeatherData.updateWeather(3600);

    if (WeatherData.lastUpdateAttempt>WeatherData.lastUpdateT+300 && I.currentTime-I.ALIVESINCE > 10800) { //weather has not updated for 3 hrs
      tft.clear();
      tft.setCursor(0,0);
      tft.setTextColor(TFT_RED);
      setFont(2);
      tft.printf("Weather failed \nfor 60 minutes.\nRebooting in 3 seconds...");
      delay(3000);
      I.resetInfo = RESET_WEATHER;
      I.lastResetTime = I.currentTime;
      
      storeError("Weather failed too many times");

      controlledReboot("Weather failed too many times",RESET_WEATHER);
    }

    #ifdef _DEBUG
       if (I.flagViewTime==0) {
       Serial.printf("Loop update weather: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(I.currentTime,"mm/dd/yyyy hh:mm:ss"),I.currentTime-I.ALIVESINCE);
        tft.clear();
        tft.setCursor(0,0);
        tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(I.currentTime,"mm/dd/yyyy hh:mm:ss"),I.currentTime-I.ALIVESINCE);
       while(true);
       }
     #endif
    OldTime[1] = minute();
    //do stuff every minute

    I.isFlagged = 0;
    I.isSoilDry = 0;
    I.isHot = 0;
    I.isCold = 0;
    I.isLeak = 0;
    I.isExpired = 0;    

    I.isFlagged =countFlagged(-1,0b00000111,0b00000011,0); //-1 means flag for all common sensors. Note that it is possible for count to be zero for expired sensors in some cases (when morerecentthan is >0)
    
    checkHeat(); //this updates I.isheat and I.isac

     I.isSoilDry =countFlagged(3,0b00000111,0b00000011,(I.currentTime>3600)?I.currentTime-3600:0);

    I.isHot =countFlagged(-2,0b00100111,0b00100011,(I.currentTime>3600)?I.currentTime-3600:0);

    I.isCold =countFlagged(-2,0b00100111,0b00000011,(I.currentTime>3600)?I.currentTime-3600:0);

    I.isLeak = countFlagged(70,0b00000001,0b00000001,(I.currentTime>3600)?I.currentTime-3600:0);

    I.isExpired = checkExpiration(-1,I.currentTime,true); //this counts critical  expired sensors
    I.isFlagged+=I.isExpired; //add expired count, because expired sensors don't otherwiseget included

    if (minute()%5==0) {
      //overwrite  sensors to the sd card
      writeSensorsSD();
      storeScreenInfoSD();
      
      // Broadcast server presence via ESPNow every 5 minutes
      if (WiFi.status() == WL_CONNECTED) {
        broadcastServerPresence();
      }
    }
  }

  if (OldTime[2] != hour()) {
    OldTime[2] = hour(); 
    
    //expire any measurements that are too old, and delete noncritical ones
    checkExpiration(-1,I.currentTime,false);
  }

  if (OldTime[3] != weekday()) {
    //if ((uint32_t) I.currentTime-I.ALIVESINCE > 604800) ESP.restart(); //reset every week
    
    OldTime[3] = weekday();
  }

  //note that second events are last intentionally
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second    

    fcnDrawScreen();
    #ifdef _DEBUG
      if (I.flagViewTime==0) {
      Serial.printf("Loop drawscreen: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(I.currentTime,"mm/dd/yyyy hh:mm:ss"),I.currentTime-I.ALIVESINCE);
      tft.clear();
      tft.setCursor(0,0);
      tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(I.currentTime,"mm/dd/yyyy hh:mm:ss"),I.currentTime-I.ALIVESINCE);
      while(true);
      }
    #endif

    #ifdef _DEBUG
      if (WeatherData.getTemperature(I.currentTime+3600)<0 && TESTRUN<3600) {
        Serial.printf("Loop %s: Weather data just failed\n",dateify(t));
        WTHRFAIL = I.currentTime;
        TESTRUN = 3600;
      }
    #endif
  }
}

