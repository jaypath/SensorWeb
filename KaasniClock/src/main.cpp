#define MYNAME "kaasniclock"

#include <ArduinoOTA.h>

#include "Globals.hpp"
#include "Graphics.hpp"
#include "SDCard.hpp"
#include "timesetup.hpp"
#include "server.hpp"


extern LGFX tft;
extern  Screen myScreen;


byte fcnDrawClock();
void drawBmp(const char *filename, int16_t x, int16_t y,int32_t transparent=-1);
void fcnDrawPic() ;
String weatherID2string(uint16_t weatherID);


void setup() {

//Init globals
myScreen.BG_COLOR = TFT_BLACK;
myScreen.FG_COLOR = TFT_LIGHTGRAY;
myScreen.DSTOFFSET=0;

//arbitrary impossible values for timer
myScreen.timer_s=100;
myScreen.timer_m=100;
myScreen.timer_h=100;
myScreen.timer_d=100;
myScreen.lastX=0;
myScreen.lastY=0;

myScreen.int_Weather_MIN=30;
myScreen.time_lastClock=0;
myScreen.time_lastPic=0;
myScreen.time_lastWeather=0;


tft.init();

tft.setColorDepth(16);
tft.fillScreen(myScreen.BG_COLOR);
tft.setTextColor(myScreen.FG_COLOR);
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
    #endif

  }

  myScreen.num_images = countFilesInDirectory("/PICS");
  myScreen.lastPic=-1;

tft.setTextColor(myScreen.FG_COLOR);

  
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
    tft.setTextColor(myScreen.FG_COLOR);


    tft.print("Set up TimeClient... ");
    timeClient.begin(); //time is in UTC
    updateTime(100,250); //check if DST and set time to EST or EDT
    tft.setTextColor(TFT_GREEN);
    tft.printf(" OK.\n");
    tft.setTextColor(myScreen.FG_COLOR);

     tft.print("Connecting ArduinoOTA... ");

    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname(MYNAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    tft.fillScreen(myScreen.BG_COLOR);            // Clear screen
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
    tft.setTextColor(myScreen.FG_COLOR);

    myScreen.t = now();
    tft.printf("Started at... %s\n",dateify(myScreen.t,"mm/dd hh:nn"));
    myScreen.ALIVESINCE = myScreen.t;
    


    
  tft.printf("Total heap: %d\n", ESP.getHeapSize());
  tft.printf("Free heap: %d\n", ESP.getFreeHeap());
  tft.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  tft.printf("Free PSRAM: %d\n", ESP.getFreePsram());




    tft.setTextFont(4);
    tft.printf("SETUP DONE. %d pics\n",myScreen.num_images);



 
}

void loop() {

  //stuff to do always, every loop
    

  ArduinoOTA.handle();
  updateTime(1,0); //just try once


  myScreen.t= now();

  getWeather();

  fcnDrawClock();
    

if (myScreen.timer_s!=second()) {
  //stuff do each second
  myScreen.timer_s=second();

}

if (myScreen.timer_m!=minute()) {
  //stuff do each minute, start of each minue
myScreen.timer_m=minute();
//myScreen.time_lastClock=0;
  
}

if (myScreen.timer_h!=hour()) {
  //stuff do each hour
  myScreen.timer_h=hour();
}

if (myScreen.timer_h!=day()) {
  //stuff do each day
  myScreen.timer_h=day();
//  myScreen.time_lastClock=0;
}

}


void fcnDrawPic() {

//    if ((myScreen.lastPic>0 && myScreen.time_lastPic>0) && (myScreen.time_lastPic == myScreen.t || myScreen.t - myScreen.time_lastPic < myScreen.int_Pic_MIN*60)) return;
    
    
    //choose a new random picture
    myScreen.lastPic = random(0,myScreen.num_images-1);
    myScreen.time_lastPic=myScreen.t;

    String fn;
    fn= "/PICS/" + getNthFileName("/PICS",myScreen.lastPic);

    drawBmp(fn.c_str(),0,0,-1);

  
}

byte fcnDrawClock() {

    if ((uint32_t) myScreen.time_lastClock!=0 &&  ((uint32_t) myScreen.time_lastClock== myScreen.t || ((uint32_t) myScreen.timer_s!=0)))  return 0;
   fcnDrawPic();


    uint16_t Xpos,Ypos;
String st;

    Xpos = 475;
    Ypos=475;
    tft.setTextFont(8);
    tft.setCursor(Xpos,Ypos);
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(myScreen.FG_COLOR);
    tft.drawString(dateify(myScreen.t,"hh:nn"),Xpos,Ypos);

    
    byte fntsz = 4;
    Xpos=5;
    Ypos=5;
    tft.setTextDatum(TL_DATUM);    
    tft.setCursor(Xpos,Ypos);
    tft.setTextFont(fntsz);
    st = (String) dateify(myScreen.t,"dow") + ",  " + (String) fcnMMM(myScreen.t,false) + " " + (String) day();
    tft.drawString(st,Xpos,Ypos);
    Ypos += (tft.fontHeight(4)+2);
    tft.setTextFont(1);
    st="ViewPort: " + getNthFileName("/PICS",myScreen.lastPic);
    st.replace("_["," from ");
    st.replace("]","");
    st.replace(".bmp","");
    tft.drawString(st,Xpos,Ypos);


    Xpos=475;
    Ypos=5;
    tft.setTextDatum(TR_DATUM);    
    tft.setTextFont(fntsz);
    st = "Planetary Status";
    tft.drawString(st,Xpos,Ypos);

    Ypos += (tft.fontHeight(fntsz)+2);
    fntsz = 2;
    tft.setTextFont(fntsz);
    st = "PREDICTED HAZARDS";
    tft.drawString(st,Xpos,Ypos);

    Ypos += (tft.fontHeight(2)+2);
    tft.setTextFont(2);
    st = (String) myScreen.wthr_DailyPoP + "% precipitation risk";
    tft.drawString(st,Xpos,Ypos);

    Ypos += (tft.fontHeight(fntsz)+2);
    fntsz = 2;
    tft.setTextFont(fntsz);
    st = "Environment: " + weatherID2string(myScreen.wthr_DailyWeatherID);
    tft.drawString(st,Xpos,Ypos);

    Ypos += (tft.fontHeight(fntsz)+2);
    tft.setTextFont(fntsz);
    st = "Temperature: " + (String) myScreen.wthr_DailyLow + " - " + (String) myScreen.wthr_DailyHigh + "F";
    tft.drawString(st,Xpos,Ypos);


    Ypos += (tft.fontHeight(fntsz)+2);
    fntsz = 2;
    tft.setTextFont(fntsz);
    st = "CURRENT HAZARDS";
    tft.drawString(st,Xpos,Ypos);

Ypos += (tft.fontHeight(fntsz)+2);
    fntsz=2;
    tft.setTextFont(fntsz);
    st = "Condition: " +  weatherID2string(myScreen.wthr_currentWeatherID);
    tft.drawString(st,Xpos,Ypos);

    Ypos += (tft.fontHeight(fntsz)+2);
    tft.setTextFont(fntsz);
    fntsz=4;
    tft.setTextFont(fntsz);
    
    st = "" + (String) myScreen.wthr_currentTemp + "F";
    tft.drawString(st,Xpos,Ypos);
    

    tft.setTextDatum(TL_DATUM);
    myScreen.time_lastClock=myScreen.t;

    return 1;
}


String weatherID2string(uint16_t weatherID) {
  switch(weatherID) {
    case 504: {
      return (String) "STORM";
    }
    case 603: {
      return (String) "Blizzard";
    }
    case 611: {
      return (String) "Ice+Snow";
    }
    case 600: {
      return (String) "Light Snow";
    }
    case 601: {
      return (String) "Mod Snow";
    }
    case 602: {
      return (String) "Heavy Snow";
    }
    case 311: {
      return (String) "Sleet";
    }
    case 201: {
      return (String) "Thunderstorm";
    }
    case 200: {
      return (String) "Severe Thunderstorm";
    }
    case 301: {
      return (String) "Drizzle";
    }
    case 500: {
      return (String) "Light Rain";
    }
    case 501: {
      return (String) "Mod Rain";
    }
    case 502: {
      return (String) "Heavy Rain";
    }
    case 761: {
      return (String) "Dust";
    }
    case 800: {
      return (String) "Sunny";
    }
    case 801: {
      return (String) "Mostly Sunny";
    }
    case 802: {
      return (String) "Light Clouds";
    }
    case 803: {
      return (String) "Cloudy";
    }
    case 804: {
      return (String) "Overcast";
    }
   case 741: {
      return (String) "Fog";
    }

  }

  return (String) weatherID;
}

void drawBmp(const char *filename, int16_t x, int16_t y,  int32_t alpha) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  File bmpFS;

    // Open requested file on SD card
  bmpFS = SD.open(filename, "r");

  if (!bmpFS)
  {
    tft.print(filename);
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row;
  uint8_t  r, g, b;

  if (read16(bmpFS) == 0x4D42)
  {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
        
          *tptr = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
          if (alpha>=0)  {
            if (*tptr==alpha) *tptr = myScreen.BG_COLOR; //convert background pixels to background
          }
          *tptr++;
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
      //Serial.print("Loaded in "); Serial.print(millis() - startTime);
      //Serial.println(" ms");
    }
    else tft.println("BMP format not recognized.");
  }
  bmpFS.close();
}

