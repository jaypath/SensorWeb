//#define DEBUG_ 0
//#define _WEBDEBUG 0

//Version 11.31 - 
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
  */

/*

/*local server commands:
    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/LIST", handleLIST);   
    server.on("/POST", handlePost);   
    server.on("/REQUESTUPDATE",handleREQUESTUPDATE);
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.on("/REQUESTWEATHER",handleREQUESTWEATHER);


 *Kiyaan server commands: 
 *192.168.68.104
 *  /WEATHERREQUEST?weatherType=xxx
 *   where xxx = temp, feels_like, pressure, humidity, dew_point, uvi, clouds, visibility, wind_speed, wind_deg, wind_gust, pop, last_rain, Next_Precip, weather [requires additional info specified by weatherObjType]
 *     if last_ or Next_Precip then you will receive unixtime of next event (or zero)
 *     if /WEATHERREQUEST?weatherType=weather&weatherObjType=yyy
 *        where yyy can be id, main, description, icon
 *        
 *  /GET?varName=zzz
 *    where zzz is a varName [sensor/arduino value] that I assign. varName can be, at most, 32 char. Example 23.12.2 [which could be arduino 23, sensor type 12, number 2]
 *  
 *  /PUT?varName=zzz&varValue=NUMERIC&logID=FLOAT&timeLogged=UNX&isFlagged=bool
 *    where varName is a 32 char value representing arduino and sensor
 *    varValue is a numeric value (stored on server as float)
 *    logID = int representing the arduino sensor ID
 *    timeLogged is the unixtime (sec), but optional
 *    isFlagged is bool true or false (note lowercase), and is optional
 */


/*sens types
1 - dht temp
2 - dht rh
3 - soil


*/

#ifdef _WEBDEBUG
  String WEBDEBUG="";
#endif


#include <String.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "SPI.h"
#include "TFT_eSPI.h"
#include "ArduinoJson.h"
#include "LittleFS.h"
#include "ArduinoOTA.h"
//#include "free_fonts.h"
#define FileSys LittleFS
//#define BG_COLOR 0xD69A

//this server
ESP8266WebServer server(80);

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();


#define XLIM 240
#define YLIM 320

/*
//free fonts
use tft.setFreeFont(FSS9);
#define FSS9 &FreeSans9pt7b
#define FSS12 &FreeSans12pt7b
#define FSS18 &FreeSans18pt7b
#define FSS24 &FreeSans24pt7b

#define FSSB9 &FreeSansBold9pt7b
#define FSSB12 &FreeSansBold12pt7b
#define FSSB18 &FreeSansBold18pt7b
#define FSSB24 &FreeSansBold24pt7b

#define FSSO9 &FreeSansOblique9pt7b
#define FSSO12 &FreeSansOblique12pt7b
#define FSSO18 &FreeSansOblique18pt7b
#define FSSO24 &FreeSansOblique24pt7b

#define FSSBO9 &FreeSansBoldOblique9pt7b
#define FSSBO12 &FreeSansBoldOblique12pt7b
#define FSSBO18 &FreeSansBoldOblique18pt7b
#define FSSBO24 &FreeSansBoldOblique24pt7b
*/


//wifi
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov");
// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
// NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
//#define ESP_SSID "kandy-hispeed" // Your network name here
//#define ESP_PASS "manath77" // Your network password here

#define SERVER "192.168.68.104"

#define TIMEOUTHTTP 2000

#define GLOBAL_TIMEZONE_OFFSET  -14400

bool GoogleServer = true;

//wellesley, MA
#define LAT 42.307614
#define LON -71.299288


#define SENSORNUM 30

#define NUMSCREEN 2
#define SECSCREEN 15

#define NUMWTHRDAYS 7

//gen global types

struct Screen {
    byte wifi;
    byte redraw;
    byte ScreenNum;
};


struct SensorVal {
  IPAddress IP;
  uint8_t ardID;
  uint8_t  snsType ;
  uint8_t snsID;
  char snsName[32];
  double snsValue;
  uint32_t timeRead;
  uint32_t timeLogged;  
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value  
};


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

union convertSensorVal {
  SensorVal a;
  uint8_t b[14];
};

//globals
int DSTOFFSET = 0;

Screen I; //here, I is of type Screen (struct)

SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!

uint32_t LAST_BAR_READ=0;
double LAST_BAR=0;


uint32_t daily_time = 0;
uint32_t hourly_time = 0;
uint32_t Next_Precip = 0;

int16_t hourly_temp[24]; //hourly temperature
int16_t hourly_weatherID[24];
int16_t hourly_pop[24];

int16_t       daily_tempMax[NUMWTHRDAYS];
int16_t       daily_tempMin[NUMWTHRDAYS];
int16_t      daily_weatherID[NUMWTHRDAYS];
int16_t       daily_pop[NUMWTHRDAYS];
double        daily_snow[NUMWTHRDAYS];

void timeUpdate(void);
uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d

String WeatherDescToday = "?"; //current weather desc

bool isFlagged = false;

//uint16_t BG_COLOR = 0xD69A; //light gray
uint16_t FG_COLOR = TFT_BLACK; //Foreground color
uint16_t BG_COLOR = TFT_LIGHTGREY; //light gray
//uint16_t BG_COLOR = TFT_DARKGREY; //Foreground color

char DATESTRING[24]=""; //holds up to hh:nn:ss day mm/dd/yyy


//fuction declarations
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);
void initSensor(byte k);
char* strPad(char* str, char* pad, byte L);
bool inIndex(byte lookfor,byte used[],byte arraysize);
int ID2Icon(int); //provide a weather ID, obtain an icon ID
void drawBmp(const char*, int16_t, int16_t);
bool Server_Message(String* , String* , int* );
void fcnDrawScreen(bool);
void fcnDrawWeather();
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
char* Byte2Bin(uint8_t value, char* , bool invert = false);
void handleLIST();
void handleNotFound();
void handlePost();
void handleRoot();
void handleREQUESTUPDATE();
void handleCLEARSENSOR();
void handleTIMEUPDATE();
void handleREQUESTWEATHER();
bool GetWeather();
double valSensorType(byte snsType, bool asAvg = false, int isflagged=-1, int isoutdoor=-1, uint32_t MoreRecentThan = 0);
uint16_t temp2color(int temp);
String fcnDOW(time_t t, bool caps=false);
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ,int x=-1,int y=-1);
void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1);
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1);

//uint16_t read16(fs::File);
//uint32_t read32(fs::File);


void setup()
{
  #ifdef DEBUG_
    Serial.begin(115200);
  #endif

  tft.init();
  tft.invertDisplay(1); //colors are inverted using official tft lib.
  tft.setRotation(0);
  tft.fillScreen(BG_COLOR);            // Clear screen

  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(4);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  tft.println("Setup...");
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.println("TFT OK. Configuring WiFi");
    

  WiFi.begin(ESP_SSID, ESP_PASS);

  #ifdef DEBUG_
    Serial.println();
    Serial.print("Connecting");
  #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    tft.print(".");
    
    #ifdef DEBUG_
    Serial.print(".");
    #endif
  }

  tft.println("");
  tft.print("Wifi OK. IP is ");
  tft.println(WiFi.localIP());


tft.println("Connecting ArduinoOTA...");

    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname("JayWeatherSrvr");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    tft.fillScreen(BG_COLOR);            // Clear screen
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(0,0);
    tft.println("OTA started, receiving data.");
  });
  ArduinoOTA.onEnd([]() {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.println("OTA End. About to reboot!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
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
    tft.setCursor(0,0);
    tft.print("OTA error: ");
    tft.println(error);
  });
  ArduinoOTA.begin();
  
  
  
    #ifdef DEBUG_
      Serial.println("Connected!");
      Serial.println(WiFi.localIP());
    #endif

  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  
  if(!LittleFS.begin()){
    tft.println("An Error has occurred while mounting LittleFS");
    tft.println("Please restart!");
    while (true) {
//      ESP.restart();
        ArduinoOTA.handle();

      }
  } else {
    tft.print("FileSys OK. ");
  }


    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/LIST", handleLIST);   
    server.on("/POST", handlePost);   
    server.on("/REQUESTUPDATE",handleREQUESTUPDATE);
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.on("/REQUESTWEATHER",handleREQUESTWEATHER);

    server.onNotFound(handleNotFound);
    server.begin();
    //init globals
    for (byte i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }
    tft.println("Set up TimeClient...");

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET);


    timeUpdate();

    tft.print("TimeClient OK.  ");


    tft.println("Starting...");

    delay(250);

}


int16_t cumsum(int16_t * arr, int16_t ind1, int16_t ind2) {
  int16_t output = 0;
  for (int x=ind1;x<=ind2;x++) {
    output+=arr[x];
  }
  return output;
}


//sensor fcns
int16_t findOldestDev() {
  //return 0 entry or oldest
  int oldestInd = 0;
  int  i=0;
  for (i=0;i<SENSORNUM;i++) {
    if (Sensors[i].ardID == 0 && Sensors[i].snsType == 0 && Sensors[i].snsID ==0) return i;
    if (Sensors[oldestInd].timeLogged == 0) {
      oldestInd = i;
    }    else {
      if (Sensors[i].timeLogged< Sensors[oldestInd].timeLogged && Sensors[i].timeLogged >0) oldestInd = i;
    }
  }
  
//  if (Sensors[oldestInd].timeLogged == 0) oldestInd = -1;

  return oldestInd;
}

void initSensor(byte k) {
  snprintf(Sensors[k].snsName,30,"");
  Sensors[k].IP = (0,0,0,0);
  Sensors[k].ardID=0;
  Sensors[k].snsType=0;
  Sensors[k].snsID=0;
  Sensors[k].snsValue=0;
  Sensors[k].timeRead=0;
  Sensors[k].timeLogged=0;  
  Sensors[k].Flags = 0;
  
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID > 0) c++; 
  }
  return c;
}

int16_t findDev(struct SensorVal *S, bool oldest = false) {
  //provide the desired devID and snsname, and will return the index to sensors array that represents that node
  //special cases:
  //  if snsID = 0 then just find the first blank entry
  //if no finddev identified and oldest = true, will return first empty or oldest
  //if no finddev and oldest = false, will return -1
  
  if (S->snsID==0) {
        #ifdef DEBUG_
          Serial.println("FINDDEV: you passed a zero index.");
        #endif

    return -1;  //can't find a 0 id sensor!
  }
  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].ardID == S->ardID && Sensors[j].snsType == S->snsType && Sensors[j].snsID == S->snsID) {
        #ifdef DEBUG_
          Serial.print("FINDDEV: I foud this dev, and the index is: ");
          Serial.println(j);
        #endif
        
        return j;
      }
    }
    
//if I got here, then nothing found.
  if (oldest) {
  #ifdef DEBUG_
    Serial.print("FINDDEV: I didn't find the registered dev. the index to the oldest element is: ");
    Serial.println(findOldestDev());
  #endif

    return findOldestDev();
  } 

  return -1; //dev not found
}


double valSensorType(byte snsType, bool asAvg, int isflagged, int isoutdoor, uint32_t MoreRecentThan) {
  double val = 0;
  byte count = 0;
  bool flagstat = true;
  bool outstat = true;

  for (byte j = 0; j<SENSORNUM; j++) {
    if (Sensors[j].snsType == snsType) {
      if (isflagged >= 0) {
        if (isflagged == 0) {
          if (bitRead(Sensors[j].Flags,0)==0) flagstat = true;
          else flagstat = false;
        } else {
          if (bitRead(Sensors[j].Flags,0)==0) flagstat = false;
          else flagstat = true;
        }
      }
      if (isoutdoor >= 0) {
        if (isoutdoor == 0) {
          if (bitRead(Sensors[j].Flags,2)==0) outstat = true;
          else outstat = false;
        } else {
          if (bitRead(Sensors[j].Flags,2)==0) outstat = false;
          else outstat = true;
        }
      }
      if (outstat && flagstat) {
        if (Sensors[j].timeLogged > MoreRecentThan) {
          if (asAvg) val+= Sensors[j].snsValue;
          else val= Sensors[j].snsValue;
          count++;
        }
      }
    }
  }

  if (count == 0) return -1000;
  return val/count;  
}



//Time fcn
void timeUpdate() {
  timeClient.update();
  if (month() < 3 || (month() == 3 &&  day() < 10) || month() ==12 || (month() == 11 && day() >=3)) DSTOFFSET = -1*60*60; //2024 DST
  else DSTOFFSET = 0;

  setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  return;
}



String fcnDOW(time_t t, bool caps) {
  if (caps) {
    if (weekday(t) == 1) return "SUN";
    if (weekday(t) == 2) return "MON";
    if (weekday(t) == 3) return "TUE";
    if (weekday(t) == 4) return "WED";
    if (weekday(t) == 5) return "THU";
    if (weekday(t) == 6) return "FRI";
    if (weekday(t) == 7) return "SAT";

  } else {
    if (weekday(t) == 1) return "Sun";
    if (weekday(t) == 2) return "Mon";
    if (weekday(t) == 3) return "Tue";
    if (weekday(t) == 4) return "Wed";
    if (weekday(t) == 5) return "Thu";
    if (weekday(t) == 6) return "Fri";
    if (weekday(t) == 7) return "Sat";
  }
    return "???";
}

bool stringToLong(String s, uint32_t* val) {
 
  char* e;
  uint32_t myint = strtol(s.c_str(), &e, 0);

  if (e != s.c_str()) { // check if strtol actually found some digits
    *val = myint;
    return true;
  } else {
    return false;
  }
  
}

//server fcns
String IP2String(byte* IP) {
//reconstruct a string from 4 bytes. If the first or second element is 0 then use 192 or 168
  String IPs = "";

  for (byte j=0;j<3;j++){
    if (IP[j] ==0) {
      if (j==0) IPs = IPs + String(192,DEC) + ".";
      else IPs = IPs + String(168,DEC) + ".";
    } else     IPs = IPs + String(IP[j],DEC) + ".";
  }

  IPs = IPs + String(IP[3],DEC);
  return IPs;
}

bool IPString2ByteArray(String IPstr,byte* IP) {
        
    String temp;
    
    int strOffset; 
    IPstr = IPstr + "."; //need the string to end with .
    for(byte j=0;j<4;j++) {
      strOffset = IPstr.indexOf(".", 0);
      if (strOffset == -1) { //did not find the . , IPstr not correct. abort.
        return false;
      } else {
        temp = IPstr.substring(0, strOffset);
        IP[j] = temp.toInt();
        IPstr.remove(0, strOffset + 1);
      }
    }
    
    return true;
}


bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum) {
    
    String temp;
    
    int strOffset = logID.indexOf(".", 0);
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

void parseLine_to_Sensor(String token) {
  //the token contains:
  String logID; //#.#.# [as string], ARDID, SNStype, SNSID
  SensorVal S;
  uint8_t tempIP[4] = {0,0,0,0};

  String temp;
  int16_t strOffset;

  #ifdef DEBUG_
    Serial.println("I am beginning parsing!");
    Serial.println(token);
  #endif

  strOffset = token.indexOf(",",0);
  if (strOffset == -1) { //did not find the comma, we may have cut the last one. abort.
    return;
  } else {
    //element 1 is IP
    logID= token.substring(0,strOffset); //end index is NOT inclusive
    token.remove(0,strOffset+1);
    IPString2ByteArray(logID,tempIP);
    S.IP = tempIP;

    //element2 is logID
    logID= token.substring(0,strOffset); //end index is NOT inclusive
    token.remove(0,strOffset+1);
    if (breakLOGID(logID,&S.ardID,&S.snsType,&S.snsID) == false) return;

    #ifdef DEBUG_
      Serial.print("token: ");
      Serial.println(token);
      Serial.print("logID: ");
      Serial.println(logID);
      Serial.print("ardid: ");
      Serial.println(S.ardID);
      Serial.print("snsType: ");
      Serial.println(S.snsType);
      Serial.print("snsID: ");
      Serial.println(S.snsID);
    #endif
  }

//timelogged
  strOffset = token.indexOf(",",0);
  temp= token.substring(0,strOffset); //
  token.remove(0,strOffset+1);
  stringToLong(temp,&S.timeRead);

//  uint32_t timeReceived
  strOffset = token.indexOf(",",0);
  temp= token.substring(0,strOffset); //
  token.remove(0,strOffset+1);
  stringToLong(temp,&S.timeLogged);

//  String varName; // [as string]
  strOffset = token.indexOf(",",0);
  temp = token.substring(0,strOffset); 
  token.remove(0,strOffset+1);
  snprintf(S.snsName,29,"%s",temp.c_str());
  #ifdef DEBUG_
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("snsName: ");
    Serial.println(S.snsName);

  #endif

//  double value;
  strOffset = token.indexOf(",",0);
  temp= token.substring(0,strOffset); 
  token.remove(0,strOffset+1);
  S.snsValue = temp.toDouble();
  #ifdef DEBUG_
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("value: ");
    Serial.println(S.snsValue);
  #endif

//bool isFlagged; last one
  strOffset = token.indexOf(",",0);
  if (strOffset==-1) temp= token;
  else temp= token.substring(0,strOffset); 
  //token.remove(0,strOffset+1);
  if (temp.toInt()!=0) {
    bitWrite(S.Flags,0,1);
  }
  #ifdef DEBUG_
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("isFlagged: ");
    Serial.println(S.Flags);
  #endif


  int sn = findDev(&S,true);

  if (sn<0) return;
  Sensors[sn] = S;

  #ifdef DEBUG_
  Serial.print("sn=");
  Serial.print(sn);
  Serial.print(",   ");
  Serial.print(Sensors[sn].snsName);
  Serial.print(", ");
  Serial.print("isFlagged: ");
  Serial.println(Sensors[sn].Flags);
  #endif

}



bool Server_Message(String* URL, String* payload, int* httpCode) {
  WiFiClient wfclient;
  HTTPClient http;
  
  if(WiFi.status()== WL_CONNECTED){
    I.wifi=10;
     http.useHTTP10(true);
     http.begin(wfclient,URL->c_str());
     *httpCode = http.GET();
     *payload = http.getString();
     http.end();
     return true;
  } 

  return false;
}


//weather FCNs

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
  #ifdef DEBUG_
    Serial.println("If the following starts with a comma, then payload removal requires +1");
    Serial.println(payload);
    Serial.println("");
    

  #endif


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


bool GetWeather() {

bool isgood = false;
String     payload;
payload.reserve(2500);
payload = "http://api.openweathermap.org/data/2.5/onecall?lat=42.307614&lon=-71.299288&exclude=minutely&units=imperial&appid=0acc2e002c61b662274debd545d4f558";

  WiFiClient wfclient;
  HTTPClient http;
  
  
  if(WiFi.status()== WL_CONNECTED){
    I.wifi=10;
    http.useHTTP10(true);


    http.begin(wfclient,payload.c_str());

    int httpCode = http.GET();
  
    if (httpCode > 0) {
      JsonDocument doc;
      //    deserializeJson(doc, payload.c_str());
      deserializeJson(doc, http.getStream());
      //GLOBAL_TIMEZONE_OFFSET = doc["timezone_offset"]; // -14400

      
   //   JsonObject current = doc["current"];
      JsonArray hourly = doc["hourly"];
      JsonArray daily = doc["daily"];


    

 //   JsonObject current_weather_0 = current["weather"][0];
 //   int current_weather_0_id = current_weather_0["id"]; // 802

  Next_Precip = 0; //initialize
      uint8_t i=0;
  //    double pop;

  //clear values
      for (i=0;i<24;i++) {
          hourly_temp[i] = 0;
          hourly_weatherID[i] = 0;
          hourly_pop[i] = 0;
      }
      for (i=0;i<7;i++) {
          daily_tempMax[i] = 0;
          daily_tempMin[i] = 0;
          daily_weatherID[i] = 0;
          daily_pop[i] = 0;
          daily_snow[i] = 0;
      }
  
      
      hourly_time = (uint32_t) hourly[0]["dt"];
    #ifdef DEBUG_
    Serial.print("hourly_time = ");
    Serial.print(hourly_time);
    #endif

    String temp = "";

      for (i=0;i<24;i++) {
        hourly_temp[i] = (int) (hourly[i]["temp"]); //hourly temperature
  //      hourly_temp[i] = (int) (hourly[i]["feels_like"]); //hourly feels like temperature
        hourly_weatherID[i] = (int) (hourly[i]["weather"][0]["id"]);
        temp = (String) hourly[i]["pop"];
        hourly_pop[i] = (int) (temp.toDouble() * 100);
        if (Next_Precip == 0 && hourly_pop[i] > 10) Next_Precip = now() + i*60*60;
      }


      daily_time = (uint32_t) daily[0]["dt"];
      for (i=0;i<7;i++) {
  
          daily_tempMax[i] = (int) (daily[i]["temp"]["max"]);
          daily_tempMin[i] = (int) (daily[i]["temp"]["min"]);
          daily_weatherID[i] = (int) (daily[i]["weather"][0]["id"]);
          temp = (String) daily[i]["pop"];
          daily_pop[i] = (int) (temp.toDouble() * 100);
          if (Next_Precip == 0 && daily_pop[i] > 10) Next_Precip = now() + i*60*60*24;

          if (daily_weatherID[i]>=600 && daily_weatherID[i]<700) daily_snow[i] = (double) daily[i]["snow"] / 25.4; //in inches
          else daily_snow[i] = 0;
      }


    WeatherDescToday = (String) daily[0]["weather"][0]["description"];
    isgood = true;

    } else {
      #ifdef DEBUG_
        Serial.println("No connection... ");
        Serial.println(httpCode);
      #endif
      isgood = false;
      
    }
  
    http.end();
   
  } else {
    isgood = false;
    #ifdef DEBUG_
      Serial.println("No wifi");
    #endif
    I.wifi=0;

  } 

//  fcnDrawScreen(isFlagged);
  return isgood;
}

int ID2Icon(int weatherID) {
  //provide a weather ID, obtain an icon ID

  if (weatherID>=200 && weatherID<300) { //t-storm group
    if (weatherID==200 || weatherID==201 || weatherID==210 || weatherID==211 ||  weatherID>229) {
      return 200;
    } else {
      return 201;
    }
  }
    
  if (weatherID>=300 && weatherID<400) { //drizzle group
    return 301;
  }

  if (weatherID>=500 && weatherID<600) { //rain group
    if (weatherID==500 || weatherID==520 ) return 500;
    if (weatherID==501 || weatherID==521) return 501;
    if (weatherID==502 || weatherID==522) return 502;
    if (weatherID==503 || weatherID == 504 || weatherID==531) return 503;
    if (weatherID==511) return 511;
  }
    
  if (weatherID>=600 && weatherID<700) { //snow group
    if (weatherID==600 || weatherID==615 || weatherID==620 ) return 600;
    if (weatherID==601 || weatherID==616|| weatherID==621) return 601;
    if (weatherID==602 || weatherID==622) return 602;
    return 611; //sleet category
    
  }

  if (weatherID>=700 && weatherID<800) { //atm group
    return 700; //sleet category
  }

  if (weatherID>=800) { //clear group
  
    if (weatherID>=801 && weatherID<803) return 801;
    return weatherID; //otherwise weatherID matches iconID!
    
  }

  return 999;
}


//tft drawing fcn
void drawBmp(const char *filename, int16_t x, int16_t y) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  fs::File bmpFS;

  // Open requested file on SD card
  bmpFS = LittleFS.open(filename, "r");

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
          if (*tptr==0xFFFF) *tptr = BG_COLOR; //convert white pixels to background
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

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void fcnPrintTxtCenter(String msg,byte FNTSZ, int x, int y) {
int X,Y;
if (x>=0 && y>=0) {
  X = x-tft.textWidth(msg,FNTSZ)/2;
  if (X<0) X=0;
  Y = y-tft.fontHeight(FNTSZ)/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}

tft.setCursor(X,Y);
tft.print(msg.c_str());

return;


}
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x,int y) {
  //print two temperatures with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position

String temp = (String) value1 + "/" + (String) value2;
int X,Y;
if (x>=0 && y>=0) {
  X = x-tft.textWidth(temp,FNTSZ)/2;
  if (X<0) X=0;
  Y = y-tft.fontHeight(FNTSZ)/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}

tft.setCursor(X,Y);
tft.setTextColor(temp2color(value1));
tft.print(value1);
tft.setTextColor(FG_COLOR,BG_COLOR);
tft.print("/");
tft.setTextColor(temp2color(value2));
tft.print(value2);
tft.setTextColor(FG_COLOR,BG_COLOR);

return;
}


void fcnPrintTxtColor(int value,byte FNTSZ,int x,int y) {
  //print  temperature with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position

String temp = (String) value;
int X,Y;
if (x>=0 && y>=0) {
  X = x-tft.textWidth(temp,FNTSZ)/2;
  if (X<0) X=0;
  Y = y-tft.fontHeight(FNTSZ)/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}

tft.setCursor(X,Y);
tft.setTextColor(temp2color(value));
tft.print(value);
tft.setTextColor(FG_COLOR,BG_COLOR);

return;
}

uint16_t temp2color(int temp) {
  uint8_t temp_colors[104] = {
  20, 255, 0, 255, //20
  24, 200, 0, 200, //21 - 24
  27, 200, 0, 100, //25 - 27
  29, 200, 100, 100, //28 - 29
  32, 200, 200, 200, //30 - 32
  34, 150, 150, 200, //33 - 34
  37, 0, 0, 150, //35 - 37
  39, 0, 50, 200, //38 - 39
  42, 0, 100, 200, //40 - 42
  44, 0, 150, 150, //43 - 44
  47, 0, 200, 150, //45 - 47
  51, 0, 200, 100, //48 - 51
  54, 0, 150, 100, //52 - 54
  59, 0, 100, 100, //55 - 59
  64, 0, 100, 50, //60 - 64
  69, 0, 100, 0, //65 - 69
  72, 0, 150, 0, //70 - 72
  75, 0, 200, 0, //73 - 75
  79, 50, 200, 0, //76 - 79
  82, 100, 200, 0, //80 - 82
  84, 150, 200, 0, //83 - 84
  87, 200, 100, 50, //85 - 87
  89, 200, 100, 100, //88 - 89
  92, 200, 50, 50, //90 - 92
  94, 250, 50, 50, //93 - 94
  100, 250, 0, 0 //95 - 100
};
  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }
  return tft.color565(temp_colors[j+1],temp_colors[j+2],temp_colors[j+3]);
}


void fcnDrawScreen(bool alternate = false) {
alternate = true; //always show second screen

time_t t = now();
  if (I.redraw == 0) {
    I.ScreenNum++;
    if (I.ScreenNum>NUMSCREEN) I.ScreenNum = 1;
    if (I.ScreenNum == 1) I.redraw = SECSCREEN;
    else I.redraw = 10;

    tft.setRotation(0);
    tft.fillScreen(BG_COLOR);            // Clear screen
  
    //draw header    
    tft.setCursor(0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
    tft.setTextFont(4);
    tft.setTextDatum(TL_DATUM);
    tft.print(dateify(t,"dow mm/dd"));
  
    //draw a black bar below header
    tft.fillRect(0,25,240,5, FG_COLOR);

    if (alternate) {
      if (I.ScreenNum==1) { 
        fcnDrawWeather();
      }
      if (I.ScreenNum==2) {
        //fcnDrawSensors();
        fcnDrawWeather();
      }
    } else {
      fcnDrawWeather();
    }
    
  }
  return;
  
}


void fcnDrawSensors() {
 
  int iconID;
  char tempbuf[32];
  int Y = 32,Z=0; 
  
  int i=0,j=0;
  byte section_spacer = 5;

  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setTextFont(8);
  tft.setTextDatum(MC_DATUM);
  snprintf(tempbuf,31,"%s",dateify(now(),"hh:nn"));
  tft.drawString(tempbuf, tft.width() / 2, Y + tft.fontHeight(8)/2);

  Y+=tft.fontHeight(8)+section_spacer;

  j=8; //space 5 days across width, so start offset
  for (i=1;i<6;i++) {
    Z=0;
    iconID = ID2Icon(daily_weatherID[i]);
    snprintf(tempbuf,31,"/BMP30x30/%d.bmp",iconID);
    drawBmp(tempbuf,j,Y);
    Z+=30+2;
    tft.setTextFont(2); 
    tft.setTextDatum(MC_DATUM);
    snprintf(tempbuf,31,"%s",dateify(now()+i*60*60*24,"DOW"));
    tft.drawString(tempbuf, j+30 / 2, Y + Z + tft.fontHeight(4)/2);
    Z+=tft.fontHeight(2)+2;

    tft.setTextFont(2); 
    if (daily_tempMax[i]>85 || daily_tempMax[i]<40 || daily_tempMin[i]>80 || daily_tempMin[i]<32)   tft.setTextColor(TFT_BLUE,TFT_YELLOW);
    else tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background    
    snprintf(tempbuf,31,"%d/%d",daily_tempMax[i],daily_tempMin[i]);
    
    tft.drawString(tempbuf,j+30 / 2,Y+Z+tft.fontHeight(2)/2);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
    
    Z+=tft.fontHeight(2);
    
    j+=45;
  }

  Y+=Z+section_spacer;

  tft.setCursor(0,Y);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(2); //
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Flagged Sensors:", tft.width()/2, Y + tft.fontHeight(2)/2);
  Y+=tft.fontHeight(2)+2;
  tft.setCursor(0,Y);

  tft.setTextColor(TFT_BLUE,TFT_YELLOW);
  
  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM-1;j++)  {
    used[j] = 255;
  }

  byte usedINDEX = 0;  
  for(byte i=0;i<SENSORNUM;i++) {
    //march through sensors and list by ArdID
    if (Sensors[i].snsID>0 && Sensors[i].snsType>0 && inIndex(i,used,SENSORNUM) == false && Sensors[i].timeLogged>0 && bitRead(Sensors[i].Flags,0)) {
      used[usedINDEX++] = i;
      tft.printf("%d %s: %d %s",Sensors[i].ardID, Sensors[i].snsName, (int) Sensors[i].snsValue,dateify(Sensors[i].timeLogged,"hh:nn mm/dd"));

    //if a sns is flagged, check all it's other sensors (so organize by sensor)
      for(byte j=i+1;j<SENSORNUM;j++) {
        if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false && Sensors[j].ardID==Sensors[i].ardID && bitRead(Sensors[j].Flags,0)) {
          used[usedINDEX++] = j;
          tft.printf("%d %s: %d %s",Sensors[j].ardID, Sensors[j].snsName, (int) Sensors[j].snsValue,dateify(Sensors[j].timeLogged,"hh:nn mm/dd"));
        }
      }
    }
  }
  
}

void fcnDrawWeather() {

time_t t = now();
int X=0,Y = 30,Z=0; //header ends at 30
int i=0,j=0;
char tempbuf[32];
double tempval;
double Pred = valSensorType(12, true,1,-1,now()-4*60*60); //weather prediction, isflagged=true
uint8_t FNTSZ = 0;

byte section_spacer = 1;

//add to header:  pressure, if available
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  FNTSZ = 2;
  tft.setTextFont(FNTSZ);
  tempval = valSensorType(13, true,-1,-1, now()-2*60*60); //BME pressure, average multiple values, ignore flagged, ignore outside, within 2 hrs
  if (tempval <= 0)   tempval = valSensorType(9, true,-1,-1,now()-2*60*60); //BMP pressure

  if (tempval >0) {
    tft.setCursor(130,0);
    if (tempval>LAST_BAR+.5) snprintf(tempbuf,31,"%dhPa^",(int) tempval);
    else if (tempval<LAST_BAR-.5) snprintf(tempbuf,31,"%dhPa_",(int) tempval);
    else snprintf(tempbuf,31,"%dhPa~",(int) tempval);

    if (Pred>0)   tft.setTextColor(TFT_RED,TFT_WHITE);
    if (Pred<0)   tft.setTextColor(TFT_BLUE,TFT_YELLOW);

    tft.print(tempbuf);
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);

//draw icon for today
  int iconID = ID2Icon(daily_weatherID[0]);
  snprintf(tempbuf,29,"/BMP120x120/%d.bmp",iconID);
  drawBmp(tempbuf,0,Y);


//add info to side of icon  
Z = 0;
X=120+(tft.width()-120)/2; //midpoint of side

  FNTSZ = 2;
  tft.setTextFont(FNTSZ);
  tft.setCursor(125,Y+Z);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  
  if (Next_Precip > 0 && Next_Precip < t + 60*60*24 ) {
    tft.setTextColor(TFT_YELLOW,TFT_BLUE);
    if (daily_snow[0]>0) {
      snprintf(tempbuf,30,"Snow %.1f\"",daily_snow[0]);
      tft.print(tempbuf);
    } else {
      tft.print("Rain@");
      tft.print(dateify(Next_Precip,"hh:nn"));      
    }
    Z=Z+tft.fontHeight(FNTSZ)+section_spacer;
    tft.setTextColor(FG_COLOR,BG_COLOR);
  } 
  tft.setCursor(125,Y+Z);

  if ((int) Pred!=-1000) {
    FNTSZ = 2;
    tft.setTextFont(FNTSZ);
    tft.setTextColor(TFT_YELLOW,TFT_BLUE);
    
    switch ((int) tempval) {
      case 3:
        tft.print("GALE");
        break;
      case 2:
        tft.print("WIND");
        break;
      case 1:
        tft.print("POOR");
        break;
      case -1:
        tft.print("RAIN");
        break;
      case -2:
        tft.print("RAIN");
        break;
      case -3:
        tft.print("STRM");
        break;
      case -4:
        tft.print("STRM!");
        break;
      case -5:
        tft.print("!STRM!");
        break;
      case -6:
        tft.print("GALE");
        break;
      case -7:
        tft.print("TSTRM");
        break;
      case -8:
        tft.print("!BOMB!");
        break;
    }
    Z=Z+tft.fontHeight( FNTSZ)+section_spacer;
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }

  tft.setCursor(125,Y+Z);

  bool USELOCALTEMP = false; //set to true to use BME or dht temps
  FNTSZ = 8;
  if (tft.fontHeight(FNTSZ) > 120-Z-section_spacer-tft.fontHeight(4)) FNTSZ = 4;
  tft.setTextFont(FNTSZ);

  if (USELOCALTEMP) {
    tempval = valSensorType(1, true,-1,1,now()-1*60*60); //outdoor temp, average values that are outside (ignore flagged)
      
    if (tempval==-1000) {
      fcnPrintTxtColor(hourly_temp[0],FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);
    } else {
      fcnPrintTxtColor(tempval,FNTSZ,120+(tft.width()-120)/2,Y+Z+tft.fontHeight(FNTSZ)/2);
    }
  } else {
      fcnPrintTxtColor(hourly_temp[0],FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);
  }  
  Z=Z+tft.fontHeight( FNTSZ)+section_spacer;

  FNTSZ = 4;  
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setTextFont(FNTSZ);
  fcnPrintTxtColor2(daily_tempMax[0],daily_tempMin[0],FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);

  tft.setTextColor(FG_COLOR,BG_COLOR);

  Z=Z+tft.fontHeight(FNTSZ)+section_spacer;
  
//end current wthr


  //now draw weather icons 
  Y = 30+120+section_spacer;
  tft.setCursor(0,Y);

  FNTSZ = 4;
  tft.setTextFont(FNTSZ);
  
  tft.setTextColor(FG_COLOR,BG_COLOR);

  for (i=1;i<7;i++) {
    Z=0;
    X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
    #ifdef DEBUG_
      Serial.print("i=");
      Serial.print(i);
    #endif
    iconID = ID2Icon(hourly_weatherID[i]);
    snprintf(tempbuf,29,"/BMP30x30/%d.bmp",iconID);
    drawBmp(tempbuf,X,Y);
    Z+=30;

     tft.setTextColor(FG_COLOR,BG_COLOR);
    X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

    FNTSZ=1;
    snprintf(tempbuf,3,"%s:00",dateify(now()+i*60*60,"hh"));
    tft.setTextFont(FNTSZ); //small font
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+tft.fontHeight(FNTSZ)/2);

    Z+=tft.fontHeight(FNTSZ)+section_spacer;

    FNTSZ=4;
    tft.setTextFont(FNTSZ); //med font
    fcnPrintTxtColor(hourly_temp[i],FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    Z+=tft.fontHeight(FNTSZ);
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);
  Y+=Z;

  tft.setCursor(0,Y);
 
  for (i=1;i<6;i++) {
    Z=0;
    X = (i-1)*(tft.width()/5) + ((tft.width()/5)-30)/2; 
    iconID = ID2Icon(daily_weatherID[i]);
    snprintf(tempbuf,31,"/BMP30x30/%d.bmp",iconID);
    drawBmp(tempbuf,X,Y);
    Z+=30;
    
    X = (i-1)*(tft.width()/5) + ((tft.width()/5))/2; 
    FNTSZ=1;
    tft.setTextFont(FNTSZ); 
    snprintf(tempbuf,31,"%s",dateify(now()+i*60*60*24,"DOW"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+tft.fontHeight(FNTSZ)/2);
    
    Z+=tft.fontHeight(FNTSZ)+section_spacer;

    tft.setTextFont(FNTSZ);
    fcnPrintTxtColor2(daily_tempMax[i],daily_tempMin[i],FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2); 
    
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
    
    Z+=tft.fontHeight(FNTSZ)+section_spacer;
    
    j+=45;
  }

Y+=Z;

 tft.setCursor(0,Y);
 
//print time at bottom
  tft.setTextColor(FG_COLOR,BG_COLOR);
  FNTSZ=6;
  Y = tft.height() - tft.fontHeight(FNTSZ) - 2;
  X = tft.width()/2;
  snprintf(tempbuf,31,"%s",dateify(now(),"hh:nn"));
  tft.setTextFont(FNTSZ);

  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+tft.fontHeight(FNTSZ)/2);
  
}


void loop() {

  ArduinoOTA.handle();
  server.handleClient();
 
 
  time_t t = now(); // store the current time in time variable t
  
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second    

    isFlagged = false;
    for (byte i=0;i<SENSORNUM;i++)  {
      if (Sensors[i].snsID>0 && Sensors[i].timeLogged>0 && bitRead(Sensors[i].Flags,0) && Sensors[i].snsType != 2 && (t-Sensors[i].timeLogged)<4*60*60) isFlagged = true; //isflagged set within past 4 hours and not an RH value
    }

    if (GoogleServer) {
      tft.fillCircle(240-15,12,6,TFT_GREEN);
      if (isFlagged) {
        if (OldTime[0]%2==0) tft.fillCircle(240-15,12,6,TFT_RED);
        else tft.fillCircle(240-15,12,6,TFT_GREEN);
      } 
    } else {
      tft.fillCircle(240-15,12,6,TFT_BLACK);
      if (isFlagged) {
        if (OldTime[0]%2==0) tft.fillCircle(240-15,12,3,TFT_RED);
        else tft.fillCircle(240-15,12,3,TFT_GREEN);
      } 

    }


    if (I.wifi>0) I.wifi--;
    if (I.redraw>0) I.redraw--;        
  }
  
  if (OldTime[1] != minute()) {
    OldTime[1] = minute();
    //do stuff every minute

   fcnDrawScreen(isFlagged);

    if(WiFi.status()== WL_CONNECTED){
      I.wifi=10;
    } else {
      I.wifi=0;
    }
    I.redraw=0;

    //check if weather available
    if (hourly_temp[0] == 0 &&  daily_tempMax[0] ==0 &&  daily_tempMin[0] == 0 &&  daily_weatherID[0] == 0) {
      //no weather
      if (GetWeather()==false) {
        Next_Precip = 0;
        for (byte x=0;x<24;x++) {
          if (hourly_pop[x]>0) {
            Next_Precip = hourly_time + 60*60*x;
            x = 100; 
          }
        }
      }
    }


    
  }
  
  if (OldTime[2] != hour()) {
    OldTime[2] = hour(); 

    timeUpdate();
    //expire any measurements that are older than 4 hours.
    for (byte i=0;i<SENSORNUM;i++)  {
      if (Sensors[i].snsID>0 && Sensors[i].timeLogged>0 && (t-Sensors[i].timeLogged)>4*60*60)  {
        //remove 4 hour old values 
        initSensor(i);
      }
      
    }

    

    //get weather
    if (GetWeather()==false) {
      #ifdef DEBUG_
        Serial.println("GetWeather failed!");
      #endif
      Next_Precip = 0;
      for (byte x=0;x<24;x++) {
        if (hourly_pop[x]>0) {
          Next_Precip = hourly_time + 60*60*x;
          x = 100; 
        }
      }
      
    } 


      #ifdef DEBUG_
        for (int i=0;i<24;i++) {
          t2 = now() + i*3600;
          Serial.print(hour(t2));
          Serial.print("@");
          Serial.print(hourly_temp[i]);
          Serial.print(" ");
        }
        Serial.println(" ");
      #endif

  }
  
  if (OldTime[3] != weekday()) {
    OldTime[3] = weekday();
  }


 
}

void handleCLEARSENSOR() {
  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  initSensor(j);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}

void handleREQUESTUPDATE() {

  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  String payload;
  int httpCode;
  String URL = Sensors[j].IP.toString() + "/UPDATEALLSENSORREADS";
  Server_Message(&URL, &payload, &httpCode);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}

void handleREQUESTWEATHER() {
//if no parameters passed, return current temp, max, min, today weather ID, pop, and snow amount
//otherwise, return the index value for the requested value

  String currentLine = "";

  if (server.args()==0) {
        currentLine += (String) hourly_temp[0] + ";"; //current temp
        currentLine += (String) daily_tempMax[0] + ";"; //dailymax
        currentLine += (String) daily_tempMin[0] + ";"; //dailymin
        currentLine += (String) daily_weatherID[0] + ";"; //dailyID
        currentLine += (String) daily_pop[0] + ";"; //POP
        currentLine += (String) daily_snow[0] + ";"; //snow
  } else {
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i)=="hourly_temp") currentLine += (String) hourly_temp[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_tempMax") currentLine += (String) daily_tempMax[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_tempMin") currentLine += (String) daily_tempMin[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_weatherID") currentLine += (String) daily_weatherID[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="hourly_weatherID") currentLine += (String) hourly_weatherID[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_pop") currentLine += (String) daily_pop[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="hourly_pop") currentLine += (String) hourly_pop[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_snow") currentLine += (String) daily_snow[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="hour") {
        uint32_t temptime = server.arg(i).toDouble();
        if (temptime==0) currentLine += (String) hour() + ";";
        else currentLine += (String) hour(temptime) + ";";
      }
    }
  }
  
  server.send(200, "text/plain", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

  return;
}

void handleTIMEUPDATE() {


  timeUpdate();

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}

void handleLIST() {

  
  char tempchar[9] = "";

  String currentLine = "<!DOCTYPE html><html><head><title>Pleasant Weather Server LIST</title></head><body><p>";

  for (byte j=0;j<SENSORNUM;j++)  {
      currentLine += (String) Sensors[j].ardID + "<br>";
      currentLine += Sensors[j].IP.toString() + "<br>";
      currentLine += (String) Sensors[j].snsID + "<br>";
      currentLine += (String) Sensors[j].snsType + "<br>";
      currentLine += (String) Sensors[j].snsName + "<br>";
      currentLine += (String) Sensors[j].snsValue + "<br>";
      currentLine += (String) Sensors[j].timeRead + "<br>";
      currentLine += (String) Sensors[j].timeLogged + "<br>";
      currentLine += (String) Byte2Bin(Sensors[j].Flags,tempchar,true) + "<br>";
      currentLine += "<br>";      
  }
currentLine += "</p></body></html>";
  
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client


}

void handleRoot() {

  String currentLine = "<!DOCTYPE html><html><head><title>Pleasant Weather Server</title></head><body>";
  
  currentLine = currentLine + "<h2>" + dateify(0,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br><p>";
  currentLine += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  currentLine += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  currentLine += "<button type=\"submit\">Update Time</button><br>";
  currentLine += "</FORM><br>";

  #ifdef _WEBDEBUG
      currentLine = "WEBDEBUG: " + WEBDEBUG + "<br>";
    #endif

  
  currentLine += "<a href=\"/LIST\">List all sensors</a><br>";
  currentLine = currentLine + "isFlagged: " + (String) isFlagged + "<br>";
  currentLine = currentLine + "Sensor Data: <br>";

  currentLine = currentLine + "----------------------------------<br>";      

  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM-1;j++)  {
    used[j] = 255;
  }

  byte usedINDEX = 0;  
  char padder[2] = ".";
  char tempchar[9] = "";
  char temp[26];
        
  for (byte j=0;j<SENSORNUM;j++)  {
//    currentLine += "inIndex: " + (String) j + " " + (String) inIndex(j,used,SENSORNUM) + "<br>";
    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      currentLine += "<FORM action=\"/REQUESTUPDATE\" method=\"get\">";
      currentLine += "<p style=\"font-family:monospace\">";

      currentLine += "<input type=\"hidden\" name=\"SensorNum\" value=\"" + (String) j + "\"><br>";  

      currentLine = currentLine + "Arduino: " + Sensors[j].ardID+"<br>";
      snprintf(temp,25,"%s","IP Address");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + "</label>";
      currentLine += " <a href=\"http://" + (String) Sensors[j].IP.toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors[j].IP.toString() + "</a><br>";
      
      currentLine = currentLine + ".............................." + "<br>";

      snprintf(temp,25,"%s","Sensor Name");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsName + "</label><br>";

      snprintf(temp,25,"%s","Value");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsValue + "</label><br>";

      snprintf(temp,25,"%s","Flagged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[j].Flags,0) + "</label><br>";
      
      snprintf(temp,25,"%s","Sns Type");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";

      snprintf(temp,25,"%s","Last Recvd");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeRead + " " + (String) dateify(Sensors[j].timeRead,"mm/dd hh:nn:ss") +  "</label><br>";
      
      snprintf(temp,25,"%s","Last Logged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeLogged + " " + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";

      Byte2Bin(Sensors[j].Flags,tempchar,true);
      snprintf(temp,25,"%s","Flags");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";
      currentLine += "<br>";
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
 
    
          snprintf(temp,25,"%s","Sensor Name");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsName + "</label><br>";
    
          snprintf(temp,25,"%s","Value");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsValue + "</label><br>";
    
          snprintf(temp,25,"%s","Flagged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[jj].Flags,0) + "</label><br>";
          
          snprintf(temp,25,"%s","Sns Type");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";
    
          snprintf(temp,25,"%s","Last Recvd");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeRead + " " + (String) dateify(Sensors[jj].timeRead,"mm/dd hh:nn:ss") + "</label><br>";
          
          snprintf(temp,25,"%s","Last Logged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeLogged + " " + (String) dateify(Sensors[jj].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";
    
          Byte2Bin(Sensors[jj].Flags,tempchar,true);
          snprintf(temp,25,"%s","Flags");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";      
          currentLine += "<br>";
              
        }

      }

      currentLine += "<button type=\"submit\" formaction=\"/CLEARSENSOR\">Clear ArdID Sensors</button><br>";
    
      currentLine += "<button type=\"submit\">Request Update</button><br>";

      currentLine += "</p></form><br>----------------------------------<br>";   
    }

  }
  
      currentLine += "</p></body></html>";   

   #ifdef DEBUG_
      Serial.println(currentLine);
    #endif
    
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void handlePost() {
SensorVal S;
uint8_t tempIP[4] = {0,0,0,0};

  for (byte k=0;k<server.args();k++) {
   #ifdef _WEBDEBUG
       WEBDEBUG = WEBDEBUG + server.argName(k) + ": " + String(server.arg(k)) + " @" + String(now(),DEC) + "\n";
    #endif
      if ((String)server.argName(k) == (String)"logID")  breakLOGID(String(server.arg(k)),&S.ardID,&S.snsType,&S.snsID);
      if ((String)server.argName(k) == (String)"IP") {
        IPString2ByteArray(String(server.arg(k)),tempIP);
        S.IP = tempIP;
      }
      if ((String)server.argName(k) == (String)"varName") snprintf(S.snsName,29,"%s", server.arg(k).c_str());
      if ((String)server.argName(k) == (String)"varValue") S.snsValue = server.arg(k).toDouble();
      if ((String)server.argName(k) == (String)"timeLogged") S.timeRead = server.arg(k).toDouble();      //time logged at sensor is time read by me
      if ((String)server.argName(k) == (String)"Flags") S.Flags = server.arg(k).toInt();
  }
  S.timeLogged = now(); //time logged by me is when I received this.
  if (S.timeRead == 0)     S.timeRead = now();
  
  
  int sn = findDev(&S,true);

  if((S.snsType==9 || S.snsType == 13) && LAST_BAR_READ < now() - 60*60) { //pressure
    LAST_BAR_READ = S.timeRead;
    LAST_BAR = S.snsValue;
  }

  if (sn<0) return;
  Sensors[sn] = S;

  server.send(200, "text/plain", "Received Data"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request

}

char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  snprintf(holder,4,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  snprintf(holder,4,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  snprintf(holder,4,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  snprintf(holder,4,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  snprintf(holder,4,"%02d",hour(t));
  dateformat.replace("hh",holder);

  snprintf(holder,4,"%02d",minute(t));
  dateformat.replace("nn",holder);

  snprintf(holder,4,"%02d",second(t));
  dateformat.replace("ss",holder);

  snprintf(holder,4,"%s",fcnDOW(t,true).c_str());
  dateformat.replace("DOW",holder);

  snprintf(holder,4,"%s",fcnDOW(t,false).c_str());
  dateformat.replace("dow",holder);

  snprintf(DATESTRING,23,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

char* Byte2Bin(uint8_t value, char* output, bool invert) {
  snprintf(output,8,"00000000");
  for (byte i = 0; i < 8; i++) {
    if (invert) {
      if (value & (1 << i)) output[i] = '1';
      else output[i] = '0';
    } else {
      if (value & (1 << i)) output[8-i] = '1';
      else output[8-i] = '0';
    }
  }

  return output;
}

bool inIndex(byte lookfor,byte used[],byte arraysize) {
  for (byte i=0;i<arraysize;i++) {
    if (used[i] == lookfor) return true;
  }

  return false;
}


char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}

