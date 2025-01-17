#define MYNAME "kaasniclock"

#include <ArduinoOTA.h>

#include "Globals.hpp"
#include "Graphics.hpp"
#include "SDCard.hpp"
#include "timesetup.hpp"
#include "server.hpp"
#include "utilities.hpp"


extern LGFX tft;
extern  Screen I;





void setup() {


//Init globals
I.BG_COLOR = TFT_BLACK;
I.FG_COLOR = TFT_LIGHTGRAY;
I.DSTOFFSET=0;

//arbitrary impossible values for timer
I.timer_s=100;
I.timer_m=100;
I.timer_h=100;
I.timer_d=100;
I.lastX=0;
I.lastY=0;

I.int_Clock_SEC=60;
I.int_Pic_MIN=10;
I.int_Weather_MIN=30;
I.time_lastClock=0;
I.time_lastPic=0;
I.time_lastWeather=0;


tft.init();

tft.setColorDepth(16);
tft.fillScreen(I.BG_COLOR);
tft.setTextColor(I.FG_COLOR);
tft.setCursor(0,0);

tft.setTextFont(4);
  tft.printf("SETUP...\n");
tft.setTextFont(2);

  SPI.begin(48,41,47); //SPI pins per https://homeding.github.io/boards/esp32s3/panel-4848S040.htm

  tft.printf("SD Card mounting...");

  if(!SD.begin(42)){  //CS=42 per https://homeding.github.io/boards/esp32s3/panel-4848S040.htm
    tft.setTextColor(TFT_RED);
    tft.println("SD Mount Failed. Halted!");
    while(true);
    #ifdef _DEBUG
      Serial.println("SD mount failed... ");
    #endif

  } 
  else {
    #ifdef _DEBUG
      Serial.println("SD mounted... ");
    #endif

    tft.setTextColor(TFT_GREEN);
    tft.println("OK.\n");

    #ifdef _DEBUG
      Serial.print("Sensor data loaded? ");
      Serial.println((sdread==true)?"yes":"no");
    #endif

  }

  I.num_images = countFilesInDirectory("/PICS");
  I.lastPic=-1;

tft.setTextColor(I.FG_COLOR);

  
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
    WiFi.begin(ESP_SSID, ESP_PASS);
#endif

    tft.setTextColor(TFT_RED);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    tft.print(".");        
  } 
    tft.setTextColor(TFT_GREEN);
    tft.printf("OK: " );
    tft.setTextColor(TFT_GREEN);    
    tft.printf("%s\n", WiFi.localIP().toString().c_str());  
    tft.setTextColor(I.FG_COLOR);


    tft.print("Set up TimeClient... ");
    timeClient.begin(); //time is in UTC
    updateTime(100,250); //check if DST and set time to EST or EDT
    tft.setTextColor(TFT_GREEN);
    tft.printf(" OK.\n");
    tft.setTextColor(I.FG_COLOR);

     tft.print("Connecting ArduinoOTA... ");

    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname(MYNAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    tft.fillScreen(I.BG_COLOR);            // Clear screen
    tft.setTextFont(2);

    tft.setCursor(0,0);
    tft.println("OTA started, receiving data.");
  });
  ArduinoOTA.onEnd([]() {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.println("OTA End. About to reboot!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef _DEBUG
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
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
    tft.setCursor(0,0);
    tft.print("OTA error: ");
    tft.println(error);
  });
    #ifdef _DEBUG
      Serial.print("Beginning OTA... ");
    #endif

  ArduinoOTA.begin();

    tft.setTextColor(TFT_GREEN);
    tft.printf(" OTA OK.\n");
    tft.setTextColor(I.FG_COLOR);

    I.t = now();
    tft.printf("Started at... %s",dateify(I.t,"mm/dd hh:nn"));
    I.ALIVESINCE = I.t;
}

void loop() {

  //stuff to do always, every loop

  I.t= now();
  fcnDrawClock();
  ArduinoOTA.handle();
  updateTime(1,0); //just try once

if (I.timer_s!=second()) {
  //stuff do each second
I.timer_s=second();

}

if (I.timer_m!=minute()) {
  //stuff do each minute, start of each minue
I.timer_m=minute();
I.time_lastClock=0;
  
}

if (I.timer_h!=hour()) {
  //stuff do each hour
  I.timer_h=hour();
}

if (I.timer_h!=day()) {
  //stuff do each day
  I.timer_h=day();
}

}
