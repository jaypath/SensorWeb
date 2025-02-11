//#define DEBUG_ 1
//use the serial debug
#define ARDNAME "LEDClk"

#include "ArduinoOTA.h"
#include <String.h>
#include <Wire.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
//#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>

#include "timesetup.hpp"

//MAX7219 display parameters
//#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW 
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CS_PIN      5
#define CLK_PIN     13
#define DATA_PIN    12
#define SCROLLSPEED 35
//#define CLOCKTIME 20 //seconds to maintain the clock before showing weather data

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES); // Software spi
char displayBuffer[350] = " ";

//timeconstants
String DOW[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

struct CLOCKVARTYPE{
  unsigned IsScrolling:1; 
  unsigned CycleNum:4; 
  unsigned ClockTimeLeft:5;
  unsigned ClockTime:5;
  unsigned ShowClockNext:1;

} __attribute__((packed));



#define NUMWTHRDAYS 1
#define NUMWTHRHRS 3

#define JSON_BUFF_DIMENSION 2500
CLOCKVARTYPE ClockDefs;
int32_t hourly_time0; //this is the index time of the other vectors
int8_t hourly_temp[NUMWTHRHRS]; //hourly temperature
uint16_t hourly_weatherID[NUMWTHRHRS];
uint8_t hourly_pop[NUMWTHRHRS];
int8_t       daily_tempMax[NUMWTHRDAYS];
int8_t       daily_tempMin[NUMWTHRDAYS];
uint16_t      daily_weatherID[NUMWTHRDAYS];
uint8_t       daily_pop[NUMWTHRDAYS];
double        daily_snow[NUMWTHRDAYS];
uint32_t sunrise,sunset;


#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here

//four timers
uint8_t TIMERS[4] = {0,0,0,0};

uint32_t ALIVESINCE = 0;

//functions
bool getWeather();
void weatherIDLookup(uint16_t wid);
bool getWeather() {
  //get weather from local weather server

  WiFiClient wfclient;
  HTTPClient http;
 
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String tempstring;
    
    tempstring = "http://192.168.68.93/REQUESTWEATHER?hourly_temp=0&hourly_temp=1&hourly_temp=2&daily_tempMax=0&daily_tempMin=0&daily_weatherID=0&daily_pop=0&daily_snow=0&sunrise=0&sunset=0";

    http.begin(wfclient,tempstring.c_str());
    http.GET();
    payload = http.getString();
    http.end();

    http.useHTTP10(true);

        hourly_temp[0] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        hourly_temp[1] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        hourly_temp[2] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        daily_tempMax[0] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        daily_tempMin[0] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
  
        daily_weatherID[0] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        daily_pop[0] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        daily_snow[0] = payload.substring(0, payload.indexOf(";",0)).toDouble(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        sunrise = payload.substring(0, payload.indexOf(";",0)).toDouble(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        sunset = payload.substring(0, payload.indexOf(";",0)).toDouble(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter


    return true;
  }

return false;
}

void weatherIDLookup(uint16_t wid) {
    #ifdef DEBUG_1
      Serial.println(wid);
    #endif

char temp[50] = "";

 switch (wid) {
  case 200 :
    snprintf(temp,49,"thunderstorms with light rain");
    break;
  case 201 :
    snprintf(temp,49,"thunderstorms with rain");
    break;
  case 202 :
    snprintf(temp,49,"thunderstorms with heavy rain");
    break;
  case 210 :
    snprintf(temp,49,"light thunderstorms");
    break;
  case 211 :
    snprintf(temp,49,"thunderstorms");
    break;
  case 212 :
    snprintf(temp,49,"heavy thunderstorms");
    break;
  case 221 :
    snprintf(temp,49,"ragged thunderstorms");
    break;
  case 230 :
    snprintf(temp,49,"thunderstorm with light drizzle");
    break;
  case 231 :
    snprintf(temp,49,"thunderstorm with drizzle");
    break;
  case 232 :
    snprintf(temp,49,"thunderstorm with heavy drizzle");
    break;
  case 300 :
    snprintf(temp,49,"light drizzle");
    break;
  case 301 :
    snprintf(temp,49,"drizzle");
    break;
  case 302 :
    snprintf(temp,49,"heavy drizzle");
    break;
  case 310 :
    snprintf(temp,49,"light drizzle");
    break;
  case 311 :
    snprintf(temp,49,"drizzle");
    break;
  case 312 :
    snprintf(temp,49,"heavy drizzle");
    break;
  case 313 :
    snprintf(temp,49,"rain showers and drizzle");
    break;
  case 314 :
    snprintf(temp,49,"heavy showers and drizzle");
    break;
  case 321 :
    snprintf(temp,49,"drizzle showers");
    break;
  case 500 :
    snprintf(temp,49,"light rain");
    break;
  case 501 :
    snprintf(temp,49,"moderate rain");
    break;
  case 502 :
    snprintf(temp,49,"heavy rain");
    break;
  case 503 :
    snprintf(temp,49,"very heavy rain");
    break;
  case 504 :
    snprintf(temp,49,"extreme rain");
    break;
  case 511 :
    snprintf(temp,49,"freezing rain");
    break;
  case 520 :
    snprintf(temp,49,"light rain showers");
    break;
  case 521 :
    snprintf(temp,49,"rain showers");
    break;
  case 522 : 
    snprintf(temp,49,"heavy rain showers");
    break;
  case 531 :
    snprintf(temp,49,"ragged rain showers");
    break;
  case 600 :
    snprintf(temp,49,"light snow");
    break;
  case 601 :
    snprintf(temp,49,"snow");
    break;
  case 602 :
    snprintf(temp,49,"heavy snow");
    break;
  case 611 :
    snprintf(temp,49,"snow and sleet");
    break;
  case 612 :
    snprintf(temp,49,"light shower sleet");
    break;
  case 613 :
    snprintf(temp,49,"shower sleet");
    break;
  case 615 :
    snprintf(temp,49,"light rain and snow  ");
    break;
  case 616 :
    snprintf(temp,49,"rain and snow");
    break;
  case 620 :
    snprintf(temp,49,"light snow");
    break;
  case 621 : 
    snprintf(temp,49,"snow");
    break;
  case 622 :
    snprintf(temp,49,"heavy snow");
    break;
  case 700:
  {
    snprintf(temp,49,"COLD");
    break;
  }
  case 701 :
    snprintf(temp,49,"mist");
    break;
  case 711 :
    snprintf(temp,49,"smoke");
    break;
  case 721 :
    snprintf(temp,49,"haze");
    break;
  case 731 :
    snprintf(temp,49,"sand and dust whirls");
    break;
  case 741 :
    snprintf(temp,49,"fog");
    break;
  case 751 :
    snprintf(temp,49,"sand storms");
    break;
  case 761 :
    snprintf(temp,49,"dust storms");
    break;
  case 762 :
    snprintf(temp,49,"volcanic ash storms");
    break;
  case 771 :
    snprintf(temp,49,"squalls");
    break;
  case 781 :
    snprintf(temp,49,"tornadoes");
    break;
  case 800 :
    snprintf(temp,49,"clear");
    break;
  case 801 :
    snprintf(temp,49,"light clouds");
    break;
  case 802 :
    snprintf(temp,49,"partly cloudy");
    break;
  case 803 :
    snprintf(temp,49,"cloudy");
    break;
  case 804 : 
    snprintf(temp,49,"cloudy");
    break;
  default :
    snprintf(temp,49,"???");
    
 }

 sprintf(displayBuffer,"%s",temp);
 return;
}

void displayTime() {
      //display the time

    
    #ifdef DEBUG_
      Serial.println(displayBuffer);
    #endif





  if (ClockDefs.IsScrolling==true) { 

    if (myDisplay.displayAnimate()) { //myDisplay.displayAnimate() returns true if the animation is finished/not in progress 
      myDisplay.displayReset(); //reset the display  
      ClockDefs.IsScrolling = false;
      
      ClockDefs.ClockTimeLeft = 0; //reset the next scroll
      #ifdef DEBUG_
        Serial.println("Finished scroll");
      #endif

    }
  } else {
    if (ClockDefs.ClockTimeLeft > 0) return;


    ClockDefs.ClockTimeLeft = ClockDefs.ClockTime;

    if (ClockDefs.ShowClockNext == true) {
      ClockDefs.IsScrolling = false; //we're not scrolling this stage
      myDisplay.displayClear();
      myDisplay.setTextAlignment(PA_CENTER);        
      sprintf(displayBuffer,"%d:%02d",hour(),minute());

      myDisplay.print(displayBuffer);
  

      ClockDefs.ShowClockNext = false; //next time do not show the clock!
      
    } else {      
      ClockDefs.ShowClockNext = true; //show the clock next
      

      if (ClockDefs.CycleNum==0) {
        snprintf(displayBuffer,349,"%s: Now: %dF (%d-%dF).",DOW[(weekday()-1)%7].c_str(),hourly_temp[0], daily_tempMin[0], daily_tempMax[0]);
        ClockDefs.CycleNum=1;

      } else {
        char temp[350] = {0};

        weatherIDLookup(daily_weatherID[0]);//fill display buffer

        if (daily_snow[0]>0) {
          snprintf(temp,349,"%s: %s, %.2f in.",DOW[(weekday()-1)%7].c_str(), displayBuffer,daily_snow[0]);
        } else {
          if (daily_pop[0]>0) {
            snprintf(temp,349,"%s: %s, %d%% precip.",DOW[(weekday()-1)%7].c_str(), displayBuffer,daily_pop[0]);
          } else {
            snprintf(temp,349,"%s: %s.",DOW[(weekday()-1)%7].c_str(), displayBuffer);
          }
        }

        snprintf(displayBuffer,349,"%s",temp);

        ClockDefs.CycleNum=0;
      }  

      myDisplay.displayReset(); //reset the display
      myDisplay.setTextAlignment(PA_LEFT);
      myDisplay.setInvert(false);
      myDisplay.displayScroll(displayBuffer, PA_CENTER, PA_SCROLL_LEFT, SCROLLSPEED);      
      ClockDefs.IsScrolling = true; //we're in scrolling mode now

    }
  }
}

void setup() {
  // put your setup code here, to run once:
    #ifdef DEBUG_
      Serial.begin(9600);
    #endif
    Wire.begin();
    Wire.setClock(400000L);


  ClockDefs.IsScrolling=false; 
  ClockDefs.CycleNum=0; 
  ClockDefs.ClockTime=5; //seconds to show clock
  ClockDefs.ClockTimeLeft=5; //seconds left to show clock
  ClockDefs.ShowClockNext = true;
/*
 *cycle numbers 
 *0 - today's hi/current/weathertype/low
 *1 - today's hourly forecast, 8 hrs by 2hrs
 *2 - tomorrow's hi/low/weathertype
 *3 - 2 days hi/low/wthr
 *4 - 3 days hi/low/wthr
 * 
 */

    myDisplay.begin();
    // Set the intensity (brightness) of the display (0-15)
    myDisplay.setIntensity(0);

    myDisplay.displayClear();
    myDisplay.setTextAlignment(PA_LEFT);        
    myDisplay.print("WIFI");

    
    WiFi.begin(ESP_SSID, ESP_PASS);
    #ifdef DEBUG_
      Serial.println();
      Serial.print("Connecting");
    #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    #ifdef DEBUG_
    Serial.print(".");
    #endif
  }
    #ifdef DEBUG_
      Serial.println("Connected!");
      Serial.println(WiFi.localIP());
    #endif


    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname("MasterBdrmClock");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    myDisplay.displayClear();
    myDisplay.print("OTA");
  });
  ArduinoOTA.onEnd([]() {
    myDisplay.displayClear();
    myDisplay.print("OTA OK");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  
    sprintf(displayBuffer,"OTA %u",(progress / (total / 100)));
  
    myDisplay.displayClear();
    myDisplay.print(displayBuffer);
  
  
    #ifdef DEBUG_
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    #endif
  });

  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG_
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
    myDisplay.displayClear();
    myDisplay.print("OTA E");
  });
  ArduinoOTA.begin();
  
    myDisplay.displayClear();
    myDisplay.print("Time");

    //set time
    timeClient.begin(); //time is in UTC
    updateTime(10,250); //check if DST and set time to EST

    ALIVESINCE = now();
    TIMERS[0] = 100;//some arbitrary seconds that will trigger a second update
    TIMERS[1] = 61; //some arbitrary seconds that will trigger a second update
    TIMERS[2] = hour();
    TIMERS[3] = day();


    #ifdef DEBUG_
      Serial.println(GLOBAL_TIMEZONE_OFFSET);
    #endif

  
    myDisplay.displayClear();
    myDisplay.print("Wthr");

    getWeather();
    ClockDefs.IsScrolling= false;
    
    displayTime();

    myDisplay.displayClear();
    myDisplay.print("OK");
    delay(1000);
}



void loop() {
  ArduinoOTA.handle();
  updateTime(1,0); //just try once
  
  time_t t=now();

  //on second
  byte x = second();
  if (TIMERS[0]!=x) {
    TIMERS[0] = x;
    if (ClockDefs.ClockTimeLeft>0) ClockDefs.ClockTimeLeft--;
  }

  x=minute(t);
  if (TIMERS[1]!=x) {
    TIMERS[1]=x;
    ClockDefs.ClockTimeLeft =0; //force a clock draw
  }

  x=hour(t);
  if (TIMERS[2]!=x) {
    
    TIMERS[2]=x;
    getWeather();

  }

  x=day(t);
  if (TIMERS[3]!=x) {
    
    TIMERS[3]=x;

  }

  displayTime();  
}
