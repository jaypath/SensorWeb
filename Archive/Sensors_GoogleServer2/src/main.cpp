//#define _DEBUG 99
#define ARDNAME "GoogleServer"
#define _USETFT
#define _USETOUCH
//#define NOISY true
#define _USEOTA


#include <Arduino.h>
//#include "esp32/spiram.h"
#include <String.h>

#include <WiFI.h>
#include <timesetup.hpp>
//#include <WebServer.h>
//#include <HTTPClient.h>

#include <NTPClient.h>
#include <WiFiUdp.h>
#include "globals.hpp"
#include "SDCard.hpp"
#include "utility.hpp"
#include "graphics.hpp"
#include "server.hpp"

#ifdef _USEOTA
  #include <ArduinoOTA.h>
#endif

#include <Wire.h>

#include <esp_task_wdt.h> //watchdog timer libary
#define WDT_TIMEOUT_MS 120000
 esp_task_wdt_config_t WDT_CONFIG = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 2) - 1,    // Bitmask of all cores
        .trigger_panic = true,
    }; 


extern byte OldTime[5];

extern String GsheetID; //file ID for this month's spreadsheet
extern String GsheetName; //file name for this month's spreadsheet

extern weathertype WeatherData;

extern LGFX tft;            // declare display variable




extern SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
extern struct ScreenFlags ScreenInfo;

extern byte OldTime[5];



void setup()
{
  //watchdog reset
  esp_task_wdt_deinit(); //wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&WDT_CONFIG); //setup watchdog 
  esp_task_wdt_add(NULL);                            //add the current thread

  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI

 tft.init();

  // Setting display to landscape
  tft.setRotation(2);

  clearTFT();
  tft.setTextFont(2);

  tft.printf("Running setup\n");

  tft.printf("Mounting SD: ");

  if(!SD.begin(41)){  //CS=41
      tft.setTextColor(TFT_RED);
      tft.printf("SD Mount Failed\n");      
      while(false);
  } 
  else {
    tft.setTextColor(TFT_GREEN);
    tft.printf("SD Mount OK\n");
  }
  tft.setTextColor(ScreenInfo.FG_COLOR);

  tft.printf("Specs:\n");
  // Get flash size and display
  tft.printf("-HEAP size: %d KB\n",  ESP.getHeapSize()/1000);
  tft.printf("-free HEAP size: %d KB\n",ESP.getMinFreeHeap()/1000);
  // Initialize PSRAM (if available) and show the total size
  if (psramFound()) {
     tft.setTextColor(TFT_GREEN);
    tft.printf("-PSRAM size: %d MB\n",heap_caps_get_total_size(MALLOC_CAP_SPIRAM)/1000000);
  } else {
    tft.setTextColor(TFT_RED);
    tft.printf("-No PSRAM found.\n");
  }

  tft.setTextColor(ScreenInfo.FG_COLOR);


  tft.printf("Connecting WiFi");

#ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Serial started.");
#endif

    // Set auto reconnect WiFi or network connection
#if defined(ESP32) || defined(ESP8266)
    WiFi.setAutoReconnect(true);
#endif

// Connect to WiFi or network
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    multi.addAP(WIFI_SSID, WIFI_PASSWORD);
    multi.run();
#else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

#ifdef _DEBUG
    Serial.print("Connecting to Wi-Fi");
#endif
    unsigned long ms = millis();
    tft.setTextColor(TFT_RED);
    while (WiFi.status() != WL_CONNECTED)
    {
      #ifdef _DEBUG
              Serial.print(".");
      #endif
      tft.printf(".");
      delay(500);
      #if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        if (millis() - ms > 10000)
        break;
      #endif
    }
    tft.setTextColor(ScreenInfo.FG_COLOR);    

     tft.setTextColor(TFT_GREEN);
    tft.printf("\n%s\n",WiFi.localIP().toString().c_str());
    tft.setTextColor(ScreenInfo.FG_COLOR);

    #ifdef _DEBUG
        Serial.println();
        Serial.print("Connected with IP: ");
        Serial.println();
    #endif


    // The WiFi credentials are required for Pico W
    // due to it does not have reconnect feature.
    #if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        GSheet.clearAP();
        GSheet.addAP(WIFI_SSID, WIFI_PASSWORD);
        // You can add many WiFi credentials here
    #endif


    tft.printf("Init GSheets... ");

    // Set the callback for Google API access token generation status (for debug only)
    GSheet.setTokenCallback(tokenStatusCallback);
    // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
    GSheet.setPrerefreshSeconds(300);

    //Begin the access token generation for Google API authentication
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    // In case SD/SD_MMC storage file access, mount the SD/SD_MMC card.
    // SD_Card_Mounting(); // See src/GS_SDHelper.h

    // Or begin with the Service Account JSON file that uploaded to the Filesystem image or stored in SD memory card.
    // GSheet.begin("path/to/serviceaccount/json/file", esp_google_sheet_file_storage_type_flash /* or esp_google_sheet_file_storage_type_sd */);
    tft.setTextColor(TFT_GREEN);
    tft.printf("OK\n");
    tft.setTextColor(ScreenInfo.FG_COLOR);
    
    tft.printf("Connect to GSheets... ");
    if (!GSheet.ready()) {
      tft.setTextColor(TFT_RED);
      tft.printf("Failed!\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);
    } else {
      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    
    }

  #ifdef _USEOTA
    tft.printf("Setting OTA... ");

    ArduinoOTA.setHostname(ARDNAME);
    ArduinoOTA.onStart([]() {
      #ifdef _DEBUG
      Serial.println("OTA started");
      #endif
      #ifdef _USETFT
        clearTFT();
        tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
        tft.setCursor(0,0);
        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(4);
        tft.println("Receiving OTA:");
        tft.setTextDatum(TL_DATUM);
      
      tft.drawRect(5,tft.height()/2-5,tft.width()-10,10,ScreenInfo.FG_COLOR);
      #endif  
    });
    ArduinoOTA.onEnd([]() {
      #ifdef _DEBUG
      Serial.println("OTA End");
      #endif
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      #ifdef _DEBUG
          Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
      #endif
      #ifdef _USETFT
        //String strbuff = "Progress: " + (100*progress / total);
        if (progress % 5 == 0) tft.fillRect(5,tft.height()/2-5,(int) (double (tft.width()-10)*progress / total),10,ScreenInfo.FG_COLOR);
        

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
      #ifdef _USETFT
        clearTFT();
        tft.setCursor(0,0);
        String strbuff;
        strbuff = (String) "Error[%u]: " + (String) error + " ";
        tft.print(strbuff);
        if (error == OTA_AUTH_ERROR) tft.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) tft.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) tft.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) tft.println("Receive Failed");
        else if (error == OTA_END_ERROR) tft.println("End Failed");
      #endif
    });
    ArduinoOTA.begin();
      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    
      
  #endif




    tft.printf("Start Server... ");

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/POST", handlePost);   
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.onNotFound(handleNotFound);
    server.begin();

      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    
    

    tft.printf("Set up TimeClient... ");

    timeClient.begin(); //time is in UTC
    updateTime(10,250); //check if DST and set time to EST or EDT
    
    OldTime[0] = 61; //force sec update.
    OldTime[2] = 61;
    OldTime[2] = 61;
    OldTime[3] = weekday(); //do not force daily update

    tft.setTextColor(TFT_GREEN);
    tft.printf("OK\n");
    tft.setTextColor(ScreenInfo.FG_COLOR);    


    tft.printf("Recover or init globals:\n");
    if (!readSensorsSD("/Data/SensorBackup.dat")) {
        
        tft.setTextColor(TFT_RED);
        tft.printf("-Failed to read Sensor Data\n");
            
    } else {
      tft.setTextColor(TFT_GREEN);
        tft.printf("-Read Sensor Data\n");
    }


    if (!readScreenInfoSD()) {      
      tft.setTextColor(TFT_RED);
      tft.printf("-Failed to read settings\n");

      ScreenInfo.intervalWeatherDraw = 5; //MINUTES between weather redraws    
      ScreenInfo.intervalClockDraw = 1; //MINUTES between  redraws    
      ScreenInfo.intervalListDraw = 1; //MINUTES between  redraws    
      ScreenInfo.intervalFlagTallySEC = 60; //SECONDS
      ScreenInfo.lastFlagTally=0;

    } else {
      tft.setTextColor(TFT_GREEN);
      tft.printf("-Read settings\n");
    }
#ifdef _DEBUG
  Serial.printf("1\n");
  #endif

    tft.setTextColor(ScreenInfo.FG_COLOR, ScreenInfo.BG_COLOR);

    //init globals
    ScreenInfo.BG_luminance = color2luminance(ScreenInfo.BG_COLOR,true);
    
    //redraw everything   
    ScreenInfo.lastDrawHeader = 0;
    ScreenInfo.lastDrawClock=0; //seconds left before redraw
    ScreenInfo.lastDrawWeather=0; //seconds left before redraw
    ScreenInfo.lastDrawList=0; //SECONDS left for this screen (then  goes to main)
    ScreenInfo.lastFlagTally=0;

    #ifdef _DEBUG
  Serial.printf("2\n");
  #endif

    tallyFlags();
    
    ScreenInfo.ALIVESINCE = ScreenInfo.t;
    if (ScreenInfo.ALIVESINCE>ScreenInfo.lastResetTime+300) ScreenInfo.rebootsSinceLast++;
    else ScreenInfo.rebootsSinceLast=0;


    tft.printf("Get weather");

    //init weather
    WeatherData.WeatherFetchInterval=60; //get weather every 60 minutes;
    WeatherData.lastWeatherFetch=0;

    byte wthrcnt = 0;
    while (getWeather()==false && wthrcnt++<20) {
      tft.setTextColor(TFT_RED);
      tft.printf(".");
      delay(500);
    };

    if (wthrcnt<20) {
      tft.setTextColor(TFT_GREEN);
      tft.printf(" OK\n");
    } else {
      tft.printf(" FAILED\n");
    }
    tft.setTextColor(ScreenInfo.FG_COLOR);    

    delay(5000);


}



void loop()
{


  esp_task_wdt_reset(); //reset the watchdog!


    //Call ready() repeatedly in loop for authentication checking and processing
  ScreenInfo.t = now();
  updateTime(1,0); //just try once
  timeClient.update();
  
  #ifdef _USEOTA
    ArduinoOTA.handle();
  #endif
  
  server.handleClient();

  drawScreen();
  getWeather();
  tallyFlags();

  bool ready = GSheet.ready(); //maintains authentication
  uploadData(ready);
  
  

  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second

      
    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

  }      
      
  if (OldTime[1] != minute()) {
    OldTime[1] = minute();
  //do stuff every minute
    ScreenInfo.lastDrawClock=0; //synchronize to time
    tallyFlags();
    
  }
 

  if (OldTime[2] != hour()) {
 
    OldTime[2] = hour(); 

    
    //expire any measurements that are too old, and delete noncritical ones
    checkExpiration(-1,ScreenInfo.t,false);
    writeSensorsSD("/Data/SensorBackup.dat");
    storeScreenInfoSD();

    SendData(); //update server about our status

   

  }

  if (OldTime[3] != weekday()) {
    
    ScreenInfo.uploadGsheetFailCount = 0;

    OldTime[3] = weekday();
    //daily

    if (day()==1) {
      GsheetName = ""; //reset the sheet name on the first of the month!
      GsheetID = "";
    }

    for (byte j=0;j<SENSORNUM;j++) {
      Sensors[j].snsValue_MAX = -10000;
      Sensors[j].snsValue_MIN = 10000;
      
      if (ScreenInfo.t-Sensors[j].timeLogged>86400 && bitRead(Sensors[j].localFlags,1)==0) initSensor(j); //expire one day old sensors that are not critical

    }
  }
}

