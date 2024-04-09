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
  unsigned CycleNum:3; 
  unsigned ClockTime:3;
  unsigned ShowClock:1;

} __attribute__((packed));



//weather constants
/*
 * Wynnewood, PA
#define LAT 39.988095
#define LON -75.279192
*/


#define LAT 42.301991979258844
#define LON -71.29820890220166
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
uint8_t LASTMINUTEDRAWN=0;



//wifi
WiFiUDP ntpUDP;
//ESP8266WebServer server(80);
NTPClient timeClient(ntpUDP);
// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
// NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
#define TIMEOUTHTTP 2000
//time zone offset is the number of seconds from Greenwich Mean Time. EST is 5 hours behind, but 4 hours in summer
const int16_t GLOBAL_timezone_offset = -18000; //need a variable, not constant. it's 5 hrs for normal time, 4 hrs for dst
int16_t DSTOFFSET = 0; //set to zero during winter, +3600 in summer


//four timers
uint32_t TIMERS[4] = {0,0,0,0};

/*
//this variable determines what is displaying...
0 - time
1 - max/min temp
2 - POP, prob of precipitation
3 - the weather ID code... change this later to the string name

*/

//functions
bool getWeather();
void weatherIDLookup(uint16_t wid);
bool TimerFcn(byte timerNum);
void setDST();
    

bool getWeather() {
  //get weather from local weather server

  WiFiClient wfclient;
  HTTPClient http;
 
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String tempstring;
    int httpCode=404;

    tempstring = "http://192.168.68.93/REQUESTWEATHER?hourly_temp=0&hourly_temp=1&hourly_temp=2&daily_tempMax=0&daily_tempMin=0&daily_weatherID=0&daily_pop=0&daily_snow=0";

    http.begin(wfclient,tempstring.c_str());
    httpCode = http.GET();
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

    if (LASTMINUTEDRAWN == minute()) return ; //don't redraw if I'm already drawn
    myDisplay.displayClear();
    myDisplay.setTextAlignment(PA_CENTER);        
    sprintf(displayBuffer,"%d:%02d",hour(),minute());
    myDisplay.print(displayBuffer);
    LASTMINUTEDRAWN = minute();

    #ifdef DEBUG_
      Serial.println(displayBuffer);
    #endif
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
  ClockDefs.ClockTime=4; //seconds to show clock
  ClockDefs.ShowClock = true;
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

  timeClient.begin();
  timeClient.update();



  setTime(timeClient.getEpochTime()+GLOBAL_timezone_offset); //do this once to get month and day

  setDST();
  setTime(timeClient.getEpochTime()+GLOBAL_timezone_offset+DSTOFFSET); //now set DST offset

    #ifdef DEBUG_
      Serial.println(GLOBAL_timezone_offset);
    #endif

  //set timers
  TIMERS[0] = now()+1*(60*60); //1 hour from now
  TIMERS[1] = now()+ClockDefs.ClockTime; //N second from now
  
    myDisplay.displayClear();
    myDisplay.print("Wthr");

  getWeather();
  ClockDefs.IsScrolling= false;
  LASTMINUTEDRAWN = 61;
  displayTime();

    myDisplay.displayClear();
    myDisplay.print("OK");
    delay(1000);
}


bool TimerFcn(byte timerNum) {
//returns true if the desired timer has ended (there are four timers, 0-3)

  uint32_t t = TIMERS[timerNum];
  
  if (now() >  t && t > 0) {

      #ifdef DEBUG_
        Serial.print("Timer ");
        Serial.print(timerNum);
        Serial.println(" done");
      #endif
    TIMERS[timerNum] = 0; //reset this timer
    return true;
    
  } else { 
    return false;
  }
}

void setDST(){
byte m = month();

    if ((m > 3 && m <11) || (m == 3 &&  day() > 9) || m <11 || (m == 11 && day() < 3)) DSTOFFSET = 1*60*60;
  else DSTOFFSET = 0;
return;
}

void loop() {
    ArduinoOTA.handle();
  
    // put your main code here, to run repeatedly

    //update the time if 1 hour passed
  if (TimerFcn(0)) {
  
  //sync/set time from internet
    timeClient.update();

    setDST();
    setTime(timeClient.getEpochTime()+GLOBAL_timezone_offset+DSTOFFSET);
    TIMERS[0] = now()+1*60*60;
    TIMERS[1] = now()+ClockDefs.ClockTime; //N second from now
    getWeather();
    
  }

  if (ClockDefs.IsScrolling==true) {

    if (myDisplay.displayAnimate()) { //myDisplay.displayAnimate() returns true if the animation is finished
      myDisplay.displayReset(); //reset the display  
      ClockDefs.IsScrolling = false;
      LASTMINUTEDRAWN = 61; //force a clock redraw when I'm done
      TIMERS[1] = now()+ClockDefs.ClockTime; //reset the next scroll
      displayTime();
      #ifdef DEBUG_
        Serial.println("Finished scroll");
      #endif

    }
  } else {
    if (TimerFcn(1)) { //N second timer is done
      TIMERS[1] = now()+ClockDefs.ClockTime;
      if (ClockDefs.ShowClock == true) {
        ClockDefs.IsScrolling = false;
        displayTime();
        ClockDefs.ShowClock = false;
      } else {      
        ClockDefs.ShowClock = true; //show the clock next
        ClockDefs.IsScrolling = true;
        LASTMINUTEDRAWN = 61; //force a clock redraw when I'm done
        char temp[350] = "";        
      

        if (ClockDefs.CycleNum==0) {
          snprintf(temp,349,"%s: Now: %dF (%d-%dF).",DOW[(weekday()-1)%7].c_str(),hourly_temp[0], daily_tempMin[0], daily_tempMax[0]);
          ClockDefs.CycleNum=1;

        } else {
          
          weatherIDLookup(daily_weatherID[0]);

          if (daily_snow[0]>0) {
           snprintf(temp,349,"%s: %s, %.2f in.",DOW[(weekday()-1)%7].c_str(), displayBuffer,daily_snow[0]);
          } else {
            if (daily_pop[0]>0) {
              snprintf(temp,349,"%s: %s, %d%% precip.",DOW[(weekday()-1)%7].c_str(), displayBuffer,daily_pop[0]);
            } else {
              snprintf(temp,349,"%s: %s.",DOW[(weekday()-1)%7].c_str(), displayBuffer);
            }
          }

          ClockDefs.CycleNum=0;
        }  
        snprintf(displayBuffer,349,"%s",temp);        

                
        myDisplay.displayReset(); //reset the display
        myDisplay.setTextAlignment(PA_LEFT);
        myDisplay.setInvert(false);
        myDisplay.displayScroll(displayBuffer, PA_CENTER, PA_SCROLL_LEFT, SCROLLSPEED);      
      }
    } else {
      if (second() == 0) {
        displayTime();
      }
    }
  }

  if (TIMERS[1]==0 || TIMERS[1]>now()+ClockDefs.ClockTime) TIMERS[1] = now()+ClockDefs.ClockTime;
  
}
