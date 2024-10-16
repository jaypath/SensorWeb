//#define _DEBUG 0
//#define _WEBDEBUG 0

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


//#define XLIM 240  //use tft.width()
//#define YLIM 320  //use tft.height()

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


#define GLOBAL_TIMEZONE_OFFSET  -18000
#define TIMEUPDATEINT 60000
//wifi
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov",0,TIMEUPDATEINT); //utc time to start [have to spec the third param!]
// By default 'pool.ntp.org' is used with X seconds update interval and
// Y offset
// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in units).
// NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
//#define ESP_SSID "kandy-hispeed" // Your network name here
//#define ESP_PASS "manath77" // Your network password here


#define TIMEOUTHTTP 2000


//wellesley, MA
#define LAT 42.307614
#define LON -71.299288

#ifdef _WEBDEBUG
  String WEBDEBUG = "";
#endif



#define SENSORNUM 60

#define NUMSCREEN 2
#define SECSCREEN 15

#define OLDESTSENSORHR 24 //hours before a sensor is removed

#define NUMWTHRDAYS 7

//gen global types

struct Screen {
    byte wifi;
    byte redraw;
    byte ScreenNum;
    bool isFlagged;
    bool wasFlagged;
    bool isHeat;
    bool isAC;
    bool isHot;
    bool isCold;
    bool isSoilDry;
    bool isLeak;
    uint8_t localWeather; //index of outside sensor
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
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = not used here, used only by the sensor (to indicate changed values that were not yet sent)

};


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
 
union convertSensorVal {
  SensorVal a;
  uint8_t b[14];
};
*/

//globals
int DSTOFFSET = 0;

Screen I; //here, I is of type Screen (struct)

SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!

uint32_t LAST_BAR_READ=0,LAST_BAT_READ=0;
double batteryArray[48] = {0};
double LAST_BAR=0;
uint32_t LAST_WEB_REQUEST = 0;

uint32_t daily_time = 0;
uint32_t hourly_time = 0;
uint32_t Next_Precip = 0;

int16_t hourly_temp[24]; //hourly temperature
int16_t hourly_weatherID[24];
int16_t hourly_pop[24];

uint32_t sunrise,sunset;

int16_t       daily_tempMax[NUMWTHRDAYS];
int16_t       daily_tempMin[NUMWTHRDAYS];
int16_t      daily_weatherID[NUMWTHRDAYS];
int16_t       daily_pop[NUMWTHRDAYS];
double        daily_snow[NUMWTHRDAYS];

uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d

String WeatherDescToday = "?"; //current weather desc

//uint16_t BG_COLOR = 0xD69A; //light gray
uint16_t FG_COLOR = TFT_BLACK; //Foreground color
uint16_t BG_COLOR = TFT_LIGHTGREY; //light gray = 211,211,211
//uint16_t BG_COLOR = TFT_DARKGREY; //Foreground color = 128,128,128

char DATESTRING[30]=""; //holds up to hh:nn:ss day mm/dd/yyyy

uint8_t Heat=0,Cool=0,Fan=0;

time_t ALIVESINCE = 0;

String WEBHTML;

//fuction declarations
int16_t findDev(struct SensorVal *S, bool oldest = false);
int16_t findSns(byte snstype, bool newest = false);
void checkHeat(byte* heat, byte* cool, byte* fan);
uint8_t find_sensor_name(String snsname, uint8_t snsType, uint8_t snsID = 255);
uint8_t find_sensor_count(String snsname,uint8_t snsType);
void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
uint8_t countFlagged(int snsType=0, uint8_t flagsthatmatter = B00000011, uint8_t flagsettings= B00000011, uint32_t MoreRecentThan=0);
uint8_t countDev();
uint32_t set_color(byte r, byte g, byte b);
void checkDST(void);
bool updateTime(byte retries=10,uint16_t waittime=250);
bool checkTime(void);
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);
void initSensor(int k); //k is the index to sensor to init. use -256 [anything <-255] to clear all, and any number over 255 to clear expired (in which case the value of k is the max age in minutes)
char* strPad(char* str, char* pad, byte L);
bool inIndex(byte lookfor,byte used[],byte arraysize);
int ID2Icon(int); //provide a weather ID, obtain an icon ID
void drawBmp(const char*, int16_t, int16_t);
bool Server_Message(String* , String* , int* );
void fcnDrawHeader(time_t t);
void fcnDrawScreen();
void fcnDrawWeather(time_t t);
void fcnDrawSensors(int Y);
void fcnPrintTxtHeatingCooling(int x,int y);
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ,int x=-1,int y=-1);
void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1);
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1);
void fcnPredictionTxt(char* tempPred, uint32_t* fg, uint32_t* bg);
void fcnPressureTxt(char* tempPres, uint32_t* fg, uint32_t* bg);
void drawBox(String roomname, int X, int Y, byte boxsize_x,byte boxsize_y);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
char* Byte2Bin(uint8_t value, char* , bool invert = false);
void handleReboot();
void handleNotFound();
void handleGETSTATUS();
void handlePost();
void handleRoot(void);
void handleALL(void);
void handlerForRoot(bool allsensors=false);
void handleREQUESTUPDATE();
void handleCLEARSENSOR();
void handleTIMEUPDATE();
void handleREQUESTWEATHER();
bool GetWeather();
double valSensorType(byte snsType, bool asAvg = false, int isflagged=-1, int isoutdoor=-1, uint32_t MoreRecentThan = 0);
uint16_t temp2color(int temp);
String fcnDOW(time_t t, bool caps=false);
void pushDoubleArray(double,byte,double);
int inArray(int arrind[], int N, int value);
//uint16_t read16(fs::File);
//uint32_t read32(fs::File);


int inArray(int arr[], int N, int value) {
  //returns index to the integer array of length N holding value, or -1 if not found

for (int i = 0; i < N-1 ; i++)   if (arr[i]==value) return i;
return -1;

}
void pushDoubleArray(double arr[], byte N, double value) { //array variable, size of array, value to push
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;

}

void find_limit_sensortypes(String snsname, byte snsType, byte* snsIndexHigh, byte* snsIndexLow){
  //returns index to the highest flagged sensorval and lowest flagged sensorval with name like snsname and type like snsType. index is 255 if no lowval is flagged

  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = not used by the server

  byte cnt = find_sensor_count(snsname,snsType);
  
  byte j = 255;

  double snsVal;
  //do highest
  snsVal=-99999;
  *snsIndexHigh = 255;
  for (byte i=1;i<=cnt;i++) {
    j = find_sensor_name(snsname,snsType,i);
    if (bitRead(Sensors[j].Flags,0) && bitRead(Sensors[j].Flags,5)) {
      if (Sensors[j].snsValue>snsVal) {
        *snsIndexHigh = j;
        snsVal = Sensors[j].snsValue;
      }
    }
  }

  snsVal=99999;
  *snsIndexLow = 255;
  for (byte i=1;i<=cnt;i++) {
    j = find_sensor_name(snsname,snsType,i);
    if (bitRead(Sensors[j].Flags,0) && bitRead(Sensors[j].Flags,5)==0) {
      if (Sensors[j].snsValue<snsVal) {
        *snsIndexLow = j;
        snsVal = Sensors[j].snsValue;
      }
    }
  }

}


byte find_sensor_count(String snsname,byte snsType) {
//find the number of sensors associated with sensor havign snsname 
String temp;
byte cnt =0;
  for (byte j=0; j<SENSORNUM;j++) {
    temp = Sensors[j].snsName; 

    if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType) cnt++;

  }

  return cnt;
}

byte find_sensor_name(String snsname,byte snsType,byte snsID) {
  //return the first sensor that has fieldname within its name
  //set snsID to 255 to ignore this field (this is an optional field, if not specified then find first snstype for fieldname)
  //returns 255 if no such sensor found
  String temp;
  for (byte j=0; j<SENSORNUM;j++) {
    temp = Sensors[j].snsName; 

    if (snsID==255) {
      if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType) return j;
    } else {
      if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType && Sensors[j].snsID == snsID) return j;
    }
  }
  return 255;
}


void setup()
{
  I.ScreenNum = 0;
  I.redraw = SECSCREEN;
  I.isFlagged = false;
  I.wasFlagged = false;
  


  #ifdef _DEBUG
    Serial.begin(115200);
  #endif

  tft.init();
  tft.invertDisplay(1); //colors are inverted using official tft lib.
  tft.setRotation(0);
  BG_COLOR = set_color(175,175,175); //slightly darker gray
  
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
    


  IPAddress   DHCP(192,168,68,1);
  IPAddress   DNS1(192,168,68,1);
  IPAddress   DNS2(192,168,68,1);
  IPAddress   GATEWAY(192,168,68,1);
  IPAddress   SUBNET(255,255,252,0);
  IPAddress   me(192,168,68,93);

  WiFi.config(me,  GATEWAY, SUBNET,DNS1,DNS2 ); 
  WiFi.begin(ESP_SSID, ESP_PASS);

  #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
  #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    tft.print(".");
    
    #ifdef _DEBUG
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
  ArduinoOTA.begin();
    #ifdef _DEBUG
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
    server.on("/ALLSENSORS", handleALL);               // Call the 'handleall' function when a client requests URI "/"
    server.on("/POST", handlePost);   
    server.on("/REQUESTUPDATE",handleREQUESTUPDATE);
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.on("/REQUESTWEATHER",handleREQUESTWEATHER);
    server.on("/GETSTATUS",handleGETSTATUS);
    server.on("/REBOOT",handleReboot);
    
    server.onNotFound(handleNotFound);
    server.begin();
    //init globals
    initSensor(-256);
    tft.println("Set up TimeClient...");

//    setSyncInterval(600); //set NTP interval for sync in sec
    timeClient.begin(); //time is in UTC
    updateTime(10,250); //check if DST and set time to EST or EDT
    
    tft.print("TimeClient OK.  ");
    tft.println("Starting...");

    delay(250);

    ALIVESINCE = now();

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

void initSensor(int k) {
  //special cases... k>255 then expire any sensor that is older than k mimnutes
  //k<0 then init ALL sensors
  if (k<-255 || k>255) {
    if (k<-255)     for (byte i=0;i<SENSORNUM;i++) initSensor(i);
    else {
      time_t t=now();

      for (byte i=0;i<SENSORNUM;i++)  {
        if (Sensors[i].snsID>0 && Sensors[i].timeLogged>0 && (uint32_t) (t-Sensors[i].timeLogged)>k*60)  initSensor(i); //convert k to seconds and  remove N hour old values 
      }
    }
    return;
  }

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

int16_t findDev(struct SensorVal *S, bool oldest) {
  //provide the desired devID and snsname, and will return the index to sensors array that represents that node
  //special cases:
  //  if snsID = 0 then just find the first blank entry
  //if no finddev identified and oldest = true, will return first empty or oldest
  //if no finddev and oldest = false, will return -1
  
  if (S->snsID==0) {
        #ifdef _DEBUG
          Serial.println("FINDDEV: you passed a zero index.");
        #endif

    return -1;  //can't find a 0 id sensor!
  }
  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].ardID == S->ardID && Sensors[j].snsType == S->snsType && Sensors[j].snsID == S->snsID) {
        #ifdef _DEBUG
          Serial.print("FINDDEV: I foud this dev, and the index is: ");
          Serial.println(j);
        #endif
        
        return j;
      }
    }
    
//if I got here, then nothing found.
  if (oldest) {
  #ifdef _DEBUG
    Serial.print("FINDDEV: I didn't find the registered dev. the index to the oldest element is: ");
    Serial.println(findOldestDev());
  #endif

    return findOldestDev();
  } 

  return -1; //dev not found
}

int16_t findSns(byte snstype, bool newest) {
  //find the first (or newest) instance of a sensor of tpe snstype
  //returns -1 if no sensor found
  
  if (snstype==0) {
        #ifdef _DEBUG
          Serial.println("FINDDEV: you passed a zero index.");
        #endif

    return -1;  //can't find a 0 id sensor!
  }

  uint16_t newestInd = -1;
  uint32_t newestTime = 0;

  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].snsType == snstype && Sensors[j].timeLogged > newestTime) {
        if (newest==false) return j;
        newestInd = j;
        newestTime = Sensors[j].timeLogged;


        #ifdef _DEBUG
          Serial.print("FINDSNS: I foud this dev, and the index is: ");
          Serial.println(j);
        #endif

      }
  }
        
  return newestInd;

}  


uint8_t countFlagged(int snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan) {
  //count sensors of type snstype [default is 0, meaning all sensortypes], flags that matter [default is 00000011 - which means that I only care about RMB1 and RMB2], what the flags should be [default is 00000011, which means I am looking for sensors that are flagged and monitored], and last logged more recently than this time [default is 0]
  //special use case... is snsType == -1 then this is a special case where we will look for types 1, 4, 10, 14, 17, 3, 61 [temperatures from various sensors, battery%]
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = Not used by server

byte count =0;
int snsArr[10] = {0}; //this is for special cases

if (snsType == -1) { //critical sensors, all types
snsArr[0] = 1; 
snsArr[1] = 4;
snsArr[2] = 10;
snsArr[3] = 14;
snsArr[4] = 17;
snsArr[5] = 3;
snsArr[6] = 61;
snsArr[7] = 70;
snsArr[8] = -1;
snsArr[9] = -1;
} 

if (snsType == -2) { //temperature sensors
snsArr[0] = 1; 
snsArr[1] = 4;
snsArr[2] = 10;
snsArr[3] = 14;
snsArr[4] = 17;
snsArr[5] = -1;
snsArr[6] = -1;
snsArr[7] = -1;
snsArr[8] = -1;
snsArr[9] = -1;
} 

if (snsType == -3) { //hvac sensors
snsArr[0] = 55; 
snsArr[1] = 56;
snsArr[2] = 57;
snsArr[3] = -1;
snsArr[4] = -1;
snsArr[5] = -1;
snsArr[6] = -1;
snsArr[7] = -1;
snsArr[8] = -1;
snsArr[9] = -1;
} 



  for (byte j = 0; j<SENSORNUM; j++) {
    if (snsType==0 || (snsType<0 && inArray(snsArr,10,Sensors[j].snsType)>=0) || (snsType>0 && (int) Sensors[j].snsType == snsType)) 
      if ((Sensors[j].Flags & flagsthatmatter ) == (flagsthatmatter & flagsettings)) {
        if (Sensors[j].timeLogged> MoreRecentThan) count++;
      }
  }

  return count;
}

double valSensorType(byte snsType, bool asAvg, int isflagged, int isoutdoor, uint32_t MoreRecentThan) {
  //return average of snstype
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
bool updateTime(byte retries,uint16_t waittime) {


  bool isgood = timeClient.update();
  byte i=1;


  while (i<retries && isgood==false) {
    i++; 
    isgood = timeClient.update();
    if (isgood==false) {
      delay(waittime);

      #ifdef _DEBUG
        Serial.printf("timeupdate: Attempt %u... Time is: %s and timeclient failed to update.\n",i,dateify(now(),"mm/dd/yyyy hh:mm:ss"));
      #endif
    }
  } 

  if (isgood) {
    checkDST();
  }

  return isgood;
}

void checkDST(void) {
  timeClient.setTimeOffset(GLOBAL_TIMEZONE_OFFSET);
  setTime(timeClient.getEpochTime());
#ifdef _DEBUG
  Serial.printf("checkDST: Starting time EST is: %s\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"));
#endif


//check if time offset is EST (-5h) or EDT (-4h)
int m = month();
int d = day();
int dow = weekday(); //1 is sunday

  if (m > 3 && m < 11) DSTOFFSET = 3600;
  else {
    if (m == 3) {
      //need to figure out if it is past the second sunday at 2 am
      if (d<8) DSTOFFSET = 0;
      else {
        if (d>13)  DSTOFFSET = 3600; //must be past second sunday... though technically could be the second sunday and before 2 am... not a big error though
        else {
          if (d-dow+1>7) DSTOFFSET = 3600; //d-dow+1 is the date of the most recently passed sunday. if it is >7 then it is past the second sunday
          else DSTOFFSET = 0;
        }
      }
    }

    if (m == 11) {
      //need to figure out if it is past the first sunday at 2 am
      if (d>7)  DSTOFFSET = 0; //must be past first sunday... though technically could be the second sunday and before 2 am... not a big error though
      else {
        if ((int) d-dow+1>1) DSTOFFSET = 0; //d-dow+1 is the date of the most recently passed sunday. if it is >1 then it is past the first sunday
        else DSTOFFSET = 3600;
      }
    }
  }

    timeClient.setTimeOffset(GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
    setTime(timeClient.getEpochTime());

    #ifdef _DEBUG
      Serial.printf("checkDST: Ending time is: %s\n\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"));
    #endif


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

  #ifdef _DEBUG
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

    #ifdef _DEBUG
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
  #ifdef _DEBUG
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
  #ifdef _DEBUG
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
  #ifdef _DEBUG
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("isFlagged: ");
    Serial.println(S.Flags);
  #endif


  int sn = findDev(&S,true);

  if (sn<0) return;
  Sensors[sn] = S;

  #ifdef _DEBUG
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
  #ifdef _DEBUG
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

    #ifdef _DEBUG
    Serial.println("Getweather: entering");
    #endif

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

      sunrise = (uint32_t) doc["current"]["sunrise"] + GLOBAL_TIMEZONE_OFFSET + DSTOFFSET;
      sunset = (uint32_t) doc["current"]["sunset"] + GLOBAL_TIMEZONE_OFFSET + DSTOFFSET;

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
    #ifdef _DEBUG
    Serial.printf("GetWeather: API hourly_time is = %i, clock time is %i\n",hourly_time,now());
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
      #ifdef _DEBUG
        Serial.println("No connection... ");
        Serial.println(httpCode);
      #endif
      isgood = false;
      
    }
  
    http.end();
   
  } else {
    isgood = false;
    #ifdef _DEBUG
      Serial.println("No wifi");
    #endif
    I.wifi=0;

  } 

   #ifdef _DEBUG
    Serial.printf("GetWeather: Exiting with status %i\n",isgood);
    #endif

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
tft.setTextFont(FNTSZ);
tft.setCursor(X,Y);
tft.print(msg.c_str());
tft.setTextDatum(TL_DATUM);
    
return;


}

void fcnPrintTxtHeatingCooling(int X,int Y) {
  //print:
  //1,2,3
  //4,5,6
  //where the color of each number is orange for heating, blue for cooling, black for none, or orange on blue for both

  byte FNTSZ = 1;
  String temp;


  uint32_t c_heat = TFT_ORANGE;
  uint32_t c_ac = TFT_BLUE * 0.75;
  uint32_t c_fan = TFT_SKYBLUE;
  uint32_t c_FG=TFT_DARKGREY, c_BG=BG_COLOR;


  tft.setTextFont(FNTSZ);

  checkHeat(&Heat,&Cool, &Fan);

  int x = X;
  tft.setTextDatum(TL_DATUM);
  for (byte j=1;j<7;j++) {
    if (j==4) {
      x=X;
      Y+= 10; //tft.fontheight does not work for font 0
    }

    if (bitRead(Heat,j)) {
      c_FG = c_heat;
      c_BG = BG_COLOR;
      if (bitRead(Fan,j)) {
        c_BG = c_fan; 
      }
      if (bitRead(Cool,j)) {
        c_BG = c_ac; //warning!!
      }
    } else {
      if (bitRead(Cool,j)) {
        c_FG = c_ac;
        c_BG = TFT_RED; //if the fan is not on, this is an alarm!
        if (bitRead(Fan,j)) c_BG = c_fan;
      } else {
        if (bitRead(Fan,j)) {
          c_FG = c_fan;
          c_BG = BG_COLOR;
        }
      }
    }
    temp = (String) j;
    tft.setTextColor(c_FG,c_BG);    
    x +=tft.drawString(temp,x,Y,1) + 5;
    c_FG=TFT_DARKGREY;
    c_BG=BG_COLOR;
    tft.setTextColor(c_FG,c_BG);    
  }

  temp = "4 5 6";

  tft.setCursor(X + tft.textWidth(temp,FNTSZ) ,Y-tft.fontHeight(0));

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

void checkHeat(byte* heat, byte* cool, byte* fan) {
  //heat byte, from RIGHT to LEFT bits are: heat is on, zone 1, zone 2, zone 3... zone 6, unused
  //cool byte from RIGHT to LEFT bits are: AC compressor is on, zone 1, zone 2, zone 3... zone 6, unused
  //cool byte from RIGHT to LEFT bits are: AC compressor is on, zone 1, zone 2, zone 3... zone 6, unused
  //fan byte from RIGHT TO LEFT bits are: fan is on,  zone 1, zone 2, zone 3... zone 6, unused
  //initialize heat and cool variaiables, now all bits are 0

  /*
  ZONES (in system):
  1=office
  2=mast br
  3=lr/dr
  4=bed
  5=kitch/fam
  6=den

  ZONES (per me, this is hardwired into receiver arduino):
  1=FamRm
  2=LR/DR
  3=office
  4=Bedrms
  5=MastBR
  6=Den

  */
*heat=0;
*cool=0;
*fan=0;

byte t = 0;

  for (byte j=0;j<SENSORNUM;j++) {
    if (Sensors[j].snsType == 55) {
      if ( bitRead(Sensors[j].Flags,0) && (double) t-Sensors[j].timeRead < 600 ) bitWrite(*heat,Sensors[j].snsID,1); //note that 0th bit is that any heater is on or off!
      else bitWrite(*heat,Sensors[j].snsID,0);
    } else {
      if (Sensors[j].snsType == 56) { //ac comp
        //which ac?
        t = bitRead(Sensors[j].Flags,0);
        if (Sensors[j].ardID == 103) {
          bitWrite(*cool,1,t);
          bitWrite(*cool,2,t);
          bitWrite(*cool,3,t);
          bitWrite(*cool,6,t);
        } else {
          bitWrite(*cool,4,t);
          bitWrite(*cool,5,t);
        }
      }
      if (Sensors[j].snsType == 57) { //ac fan
        //which ac?
        t = bitRead(Sensors[j].Flags,0);
        if (Sensors[j].ardID == 103) {
          bitWrite(*fan,1,t);
          bitWrite(*fan,2,t);
          bitWrite(*fan,3,t);
          bitWrite(*fan,6,t);
        } else {
          bitWrite(*fan,4,t);
          bitWrite(*fan,5,t);
        }
      }

    }
  }

  if (*heat>0) bitWrite(*heat,0,1); //any heater is on
  if (*cool>0) bitWrite(*cool,0,1); //any ac is on
  if (*fan>0) bitWrite(*fan,0,1); //any fan is on
  
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

void fcnDrawHeader(time_t t) {
  //draw header    
  String st = "";
  uint16_t x = 0,y=0;
  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(4);
  tft.setTextDatum(TL_DATUM);
  st = dateify(t,"dow mm/dd");
  tft.drawString(st,x,y);
  x += tft.textWidth(st,4)+10;
  
  fcnPrintTxtHeatingCooling(x,3);

  st = "1 2 3 ";
  x += tft.textWidth(st,1)+10;
  y=3;
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  
  if (I.localWeather<255) {
    tft.drawString("LOCAL",x,y);
    y+=tft.fontHeight(0)+2;    
    x+=tft.drawString("TEMP",x,y+tft.fontHeight(1)+1) + 5;

    //draw battery pcnt
    byte tind = find_sensor_name("Outside",61);
    if (tind<255) {
        tft.setTextDatum(TR_DATUM);
        tft.setTextFont(2);
        st = (String) ((int) Sensors[tind].snsValue) + "%";
        tft.drawString(st,tft.width(),y);
        tft.setTextDatum(TL_DATUM);
    } 
  }

  tft.setTextFont(2);

  //draw a black bar below header
  tft.fillRect(0,25,240,5, FG_COLOR);

}

void fcnDrawScreen() {

  if (I.redraw > 0 && I.isFlagged==I.wasFlagged) return; //not time to redraw and flag status has not changed

  I.wasFlagged = I.isFlagged; //change was status to is status because we are drawing now

  time_t t = now();
  
  I.localWeather = 255; //set to 255 as false, or to snsnum to use outdoor temps    
  I.localWeather=find_sensor_name("Outside", 4);
  if (I.localWeather<255) {
    if (t-Sensors[I.localWeather].timeRead>60*30) I.localWeather = 255; //localweather is only useful if <30 minutes old 
  }

  tft.fillScreen(BG_COLOR);            // Clear screen
  fcnDrawHeader(t);


  if (I.isFlagged) {
    I.ScreenNum=1;
    I.redraw = SECSCREEN;

/*
    I.ScreenNum = ((I.ScreenNum+1)%NUMSCREEN);

    if (I.ScreenNum==0) I.redraw = SECSCREEN;
    
    if (I.ScreenNum==1) I.redraw = SECSCREEN;
*/
  } else {
    I.redraw = 60;
    I.ScreenNum = 0;
  }
  fcnDrawWeather(t);


  return;
  
}

uint32_t set_color(byte r, byte g, byte b) {
  return tft.color565(r,g, b);
}

void drawBox(String roomname, int X, int Y, byte boxsize_x,byte boxsize_y) {
  /*uint8_t box_nl_border[3] = {0,125,0};
  uint8_t box_nl_fill[3] = {120,255,120};
  uint8_t box_high_border[3] = {150,20,20}; //r,g,b
  uint8_t box_high_fill[3] = {255,100,100};
  uint8_t box_low_border[3] = {20,20,150};
  uint8_t box_low_fill[3] = {150,150,255};
  uint8_t box_dry_border[3] = {65,45,20};
  uint8_t box_dry_fill[3] = {250,170,100};
  uint8_t box_wet_border[3] = {0,0,255};
  uint8_t box_wet_fill[3] = {0,190,255};
  */
  
  uint32_t box_border = set_color(0,125,0);
  uint32_t box_fill = set_color(120,255,120);
  uint32_t text_color = set_color(0,0,0);

  String box_text = "";
  char tempbuf[14];

  byte isHigh = 255,isLow=255;
  byte FNTSZ=2;
  tft.setTextFont(FNTSZ);

  //get sensor vals 1, 4, 3, 70, 61... if any are flagged then set box color
  //temperature
  isHigh = 255;
  isLow = 255;

  find_limit_sensortypes(roomname,1,&isHigh,&isLow);
  if (isLow != 255) {
    box_text += (String) ((int) Sensors[isLow].snsValue) + "F(LOW)|";
    box_border = set_color(20,20,150);
    box_fill = set_color(150,150,255);
    text_color = set_color(255-150,255-150,255-255);
  }
  if (isHigh != 255) {
    box_text += (String) ((int) Sensors[isHigh].snsValue) + "F(HIGH)|";
    box_border = set_color(150,20,20);
    box_fill = set_color(255,100,100);
    text_color = set_color(255-255,255-100,255-100);
  }

  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,4,&isHigh,&isLow);
  if (isLow != 255) {
    box_text += (String) ((int) Sensors[isLow].snsValue) + "F(LOW)|";
    box_border = set_color(20,20,150);
    box_fill = set_color(150,150,255);
    text_color = set_color(255-150,255-150,255-255);
  }
  if (isHigh != 255) {
    box_text += (String) ((int) Sensors[isHigh].snsValue) + "F(HIGH)|";
    box_border = set_color(150,20,20);
    box_fill = set_color(255,100,100);
    text_color = set_color(255-255,255-100,255-100);
  }

  //soil
  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,3,&isHigh,&isLow);    
  if (isHigh != 255) {
    box_text += "DRY|";
    box_border = set_color(65,45,20);
    box_fill = set_color(250,170,100);
    text_color = set_color(255-250,255-170,255-100);
  } 

  //leak
  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,70,&isHigh,&isLow);    
  if (isHigh != 255) { //leak is a 1, no leak is a 0
    box_text += "LEAK|";
    box_border = set_color(0,0,255);
    box_fill = set_color(0,190,255);
    text_color = set_color(255-0,255-190,255-255);
  }
 

  //bat
  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,61,&isHigh,&isLow);    
  if (isLow != 255) {
    box_text += (String) ((int) Sensors[isLow].snsValue) + "%(LOW)|";
    box_border = set_color(20,20,150);
    box_fill = set_color(150,150,255);
    text_color = set_color(255-150,255-150,255-255);
  }


  //draw  box
  tft.fillRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_fill);
  tft.drawRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_border);
  tft.setTextColor(0,box_fill);
  
  Y+=  2;

  snprintf(tempbuf,7,"%s",roomname.c_str());
  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X+boxsize_x/2,Y+tft.fontHeight(FNTSZ)/2);

  Y+=3+tft.fontHeight(FNTSZ);
  
  tft.setTextColor(text_color,box_fill);
      
  //print each line to the |, then print on the next line
  FNTSZ=1;
  int strOffset = -1;
  String tempstr;
  while (box_text.length()>1) {
    strOffset = box_text.indexOf("|", 0);
    if (strOffset == -1) { //did not find the break point
      tempstr = box_text;
      box_text = "";
    } else {
      tempstr = box_text.substring(0, strOffset);
      box_text.remove(0, strOffset + 1); //include the trailing "|"
    }

    if (tempstr.length()>1) {
      fcnPrintTxtCenter(tempstr,FNTSZ, X+boxsize_x/2,Y+tft.fontHeight(FNTSZ)/2);
      Y+=2+tft.fontHeight(FNTSZ);
    }
  } 

  
}

void fcnDrawSensors(int Y) {
  /*
  need to show:
  outside r1/c1
  attic r1/c2
  upstairs hall r1/c3
  Upstairs BR r2/c1
  FamRm r2/c2
  DR r2/c3
  LR r3/c1
  Office r3/c2
  Den r3/c3
*/

   
  int X = 0;
  char tempbuf[7];
//print time at top
  tft.setTextColor(FG_COLOR,BG_COLOR);
  byte FNTSZ=4;

  X = 3*tft.width()/4;
  snprintf(tempbuf,6,"%s",dateify(now(),"hh:nn"));
  tft.setTextFont(FNTSZ);

  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,tft.fontHeight(FNTSZ)/2);

  byte boxsize_x=60,boxsize_y=50;
  String roomname;

  X = 0;
 
  byte rows = 3;
  byte cols = 4;

  for (byte r=0;r<rows;r++) {
    for (byte c=0;c<cols;c++) {
      switch ((uint16_t) r<<8 | c) {
        case 0:
          roomname = "Outside";
          break;
        case 0<<8 | 1:
          roomname = "Attic";
          break;
        case 0<<8 | 2:
          roomname = "UpHall";
          break;
        case 0<<8 | 3:
          roomname = "MastBR";
          break;
        case 1<<8 | 0:
          roomname = "FamRm";
          break;
        case 1<<8 | 1:
          roomname = "FRBoP";
          break;
        case 1<<8 | 2:
          roomname = "Dining";
          break;
        case 1<<8 | 3:
          roomname = "LivRm";
          break;
        case 2<<8 | 0:
          roomname = "Office";
          break;
        case 2<<8 | 1:
          roomname = "Den";
          break;
        case 2<<8 | 2:
          roomname = "Garage";
          break;
        case 2<<8 | 3:
          roomname = "Bmnt";
          break;
      }


      drawBox(roomname,  X,  Y,boxsize_x,boxsize_y );
        

      X=X+boxsize_x;
    }
    Y+=boxsize_y;
    X=0;
  }
    tft.setTextColor(FG_COLOR,BG_COLOR);

}


void fcnPressureTxt(char* tempPres, uint32_t* fg, uint32_t* bg) {
  // print air pressure
  //do not average different pressure sensors, as they are at different altitudes
  //  tempval = valSensorType(13, true,-1,-1, now()-2*60*60); //BME pressure, average multiple values, ignore flagged, ignore outside, within 2 hrs
  //  if (tempval <= 0)   tempval = valSensorType(9, true,-1,-1,now()-2*60*60); //BMP pressure
  double tempval = find_sensor_name("Outside", 13); //bme pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 9); //bmp pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 19); //bme680 pressure

  if (tempval !=255) {
    tempval = Sensors[(byte) tempval].snsValue;
    if (tempval>LAST_BAR+.5) {
      snprintf(tempPres,10,"%dhPa^",(int) tempval);
      *fg=TFT_RED;
      *bg=BG_COLOR;
    } else {
      if (tempval<LAST_BAR-.5) {
        snprintf(tempPres,10,"%dhPa_",(int) tempval);
        *fg=TFT_BLUE;
        *bg=BG_COLOR;
      } else {
        snprintf(tempPres,10,"%dhPa~",(int) tempval);
        *fg=FG_COLOR;
        *bg=BG_COLOR;
      }
    }

    if (tempval>=1022) {
      *fg=TFT_RED;
      *bg=TFT_WHITE;
    }

    if (tempval<=1000) {
      *fg=TFT_BLUE;
      *bg=TFT_WHITE;
    }

  }
  return;
}

void fcnPredictionTxt(char* tempPred, uint32_t* fg, uint32_t* bg) {

  double tempval = find_sensor_name("Outside",12);
  if (tempval!=255) tempval = (int) Sensors[(byte) tempval].snsValue;
  else tempval = 0; //currently holds predicted weather

  if ((int) tempval!=0) {
    
    switch ((int) tempval) {
      case 3:
        snprintf(tempPred,10,"GALE");
        *fg=TFT_RED;*bg=TFT_BLACK;
        break;
      case 2:
        snprintf(tempPred,10,"WIND");
        *fg=TFT_RED;*bg=BG_COLOR;
        break;
      case 1:
        snprintf(tempPred,10,"POOR");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case 0:
        snprintf(tempPred,10,"");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case -1:
        snprintf(tempPred,10,"RAIN");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case -2:
        snprintf(tempPred,10,"RAIN");
        *fg=TFT_BLUE;*bg=BG_COLOR;

        break;
      case -3:
        snprintf(tempPred,10,"STRM");
        *fg=TFT_BLACK;*bg=TFT_CYAN;

        break;
      case -4:
        snprintf(tempPred,10,"STRM");
        *fg=TFT_YELLOW;*bg=TFT_CYAN;
        break;
      case -5:
        snprintf(tempPred,10,"STRM");
        *fg=TFT_RED;*bg=TFT_CYAN;
        break;
      case -6:
        snprintf(tempPred,10,"GALE");
        *fg=TFT_YELLOW;*bg=TFT_BLUE;
        break;
      case -7:
        snprintf(tempPred,10,"TSTRM");
        *fg=TFT_RED;*bg=TFT_BLUE;
        break;
      case -8:
        snprintf(tempPred,10,"BOMB");
        *fg=TFT_RED;*bg=TFT_BLUE;
        break;
    }
  } 

  return;
}

void fcnDrawWeather(time_t t) {

int X=0,Y = 30,Z=0; //header ends at 30
int i=0,j=0;
char tempbuf[32];
//double Pred = valSensorType(12, true,1,-1,now()-4*60*60); //weather prediction, isflagged=true
double tempval;
uint8_t FNTSZ = 0;

byte deltaY = 0;
byte section_spacer = 1;



//draw icon for today
int iconID = ID2Icon(daily_weatherID[0]);
snprintf(tempbuf,31,"/BMP120x120/%d.bmp",iconID);
drawBmp(tempbuf,0,Y);

//add info below icon
FNTSZ=0;
if (Next_Precip > 0 && Next_Precip < (uint16_t) ((24-hour(t))*60*60 + (59 - minute(t))*60 + 59 - second(t))) { //is the next rain before midnight?
  if (daily_snow[0]>0) {
    tft.setTextColor(TFT_RED,TFT_WHITE);
    snprintf(tempbuf,30,"Snow@%s %.1f\"",dateify(Next_Precip,"hh"),daily_snow[0]);
    fcnPrintTxtCenter(tempbuf,FNTSZ,60,120-7);
  } else {
    tft.setTextColor(TFT_YELLOW,TFT_BLUE);
    snprintf(tempbuf,30,"Rain@%s",dateify(Next_Precip,"hh:nn"));
    if (FNTSZ==0) deltaY = 8;
    else deltaY = tft.fontHeight(FNTSZ);


    fcnPrintTxtCenter(tempbuf,FNTSZ,60,120-deltaY);
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);
} 

//add info to side of icon  
Z = 0;
X=120+(tft.width()-120)/2; //middle of area on side of icon

tft.setTextFont(FNTSZ);
tft.setTextColor(FG_COLOR,BG_COLOR);

// draw current temp
  FNTSZ = 8;
//  if (tft.fontHeight(FNTSZ) > 120-Z-section_spacer-tft.fontHeight(FNTSZ)) FNTSZ = 4; //check if the number can fit!

  tft.setTextFont(FNTSZ);
    if (FNTSZ==0) deltaY = 8;
    else deltaY = tft.fontHeight(FNTSZ);
  
  if (I.localWeather<255) {    
    tempval = Sensors[I.localWeather].snsValue;
    fcnPrintTxtColor(tempval,FNTSZ,X,Y+Z+deltaY/2);
  } else {
    fcnPrintTxtColor(hourly_temp[0],FNTSZ,X,Y+Z+deltaY/2);
  }  
  Z=Z+tft.fontHeight( FNTSZ)+section_spacer;

//print daily max / min
  FNTSZ = 4;  
  if (FNTSZ==0) deltaY = 8;
  else deltaY = tft.fontHeight(FNTSZ);

  tft.setTextFont(FNTSZ);
  fcnPrintTxtColor2(daily_tempMax[0],daily_tempMin[0],FNTSZ,X,Y+Z+deltaY/2);

  tft.setTextColor(FG_COLOR,BG_COLOR);

  Z=Z+tft.fontHeight(FNTSZ)+section_spacer;

//print pressure info
  uint32_t fg,bg;
  FNTSZ = 1;
  tempbuf[0]=0;
  fcnPredictionTxt(tempbuf,&fg,&bg);
  tft.setTextColor(fg,bg);
  if (tempbuf[0]==0) {//no gale
    FNTSZ = 2;
  } else {
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);
    Z=Z+tft.fontHeight(FNTSZ)+section_spacer;
    FNTSZ=1;
  }

  if (FNTSZ==0) deltaY = 8;
  else deltaY = tft.fontHeight(FNTSZ);

  tft.setTextFont(FNTSZ);  
  tempbuf[0]=0;
  fcnPressureTxt(tempbuf,&fg,&bg);
  tft.setTextColor(fg,bg);
  fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+deltaY/2);

  FNTSZ = 2;
  tft.setTextFont(FNTSZ);
  tft.setTextColor(FG_COLOR,BG_COLOR);


//end current wthr


//if isflagged, then show rooms with flags. otherwise, show daily weather
  if (I.isFlagged) {
    if (I.ScreenNum==1) {
      fcnDrawSensors(30+120+section_spacer);
      return;
    }
  } 
  //now draw weather icons 
  Y = 30+120+section_spacer;
  tft.setCursor(0,Y);

  FNTSZ = 4;
  tft.setTextFont(FNTSZ);
  
  tft.setTextColor(FG_COLOR,BG_COLOR);

  for (i=1;i<7;i++) {
    Z=0;
    X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
    #ifdef _DEBUG
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
  snprintf(tempbuf,31,"%s",dateify(t,"hh:nn"));
  tft.setTextFont(FNTSZ);

  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+tft.fontHeight(FNTSZ)/2);
  
}


void loop() {
    // #ifdef _DEBUG
    //   Serial.printf("Loop start: Time is: %s\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"));
    // #endif

  ArduinoOTA.handle();
  server.handleClient();
  updateTime(1,0); //just try once

  time_t t = now(); // store the current time in time variable t
    
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second    

    if (I.wifi>0) I.wifi--;
    if (I.redraw>0) I.redraw--;
    fcnDrawScreen();
  }
  
  if (OldTime[1] != minute()) {

    OldTime[1] = minute();
    //do stuff every minute

    I.isFlagged = false;
    if (countFlagged(-1,B00000111,B00000011,0)>0) I.isFlagged = true; //only flag for soil or temp or battery
    
    I.isAC = false;
    if (countFlagged(56,B00000001,B00000001,(t>3600)?t-3600:0)>0) I.isAC = true;

    I.isHeat = false;
    if (countFlagged(50,B00000001,B00000001,(t>3600)?t-3600:0)>0) I.isHeat = true;

    I.isSoilDry = false;
    if (countFlagged(3,B00000111,B00000011,(t>3600)?t-3600:0)>0) I.isSoilDry = true;

    I.isHot = false;
    if (countFlagged(-2,B00100111,B00100011,(t>3600)?t-3600:0)>0) I.isHot = true;

    I.isCold = false;
    if (countFlagged(-2,B00100111,B00000011,(t>3600)?t-3600:0)>0) I.isCold = true;

    I.isLeak = false;
    if (countFlagged(70,B00000001,B00000001,(t>3600)?t-3600:0)>0) I.isLeak = true;


    I.redraw = 0;

    if(WiFi.status()== WL_CONNECTED){
      I.wifi=10;
    } else {
      I.wifi=0;
    }

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

    //expire any measurements that are older than N hours.
    initSensor(OLDESTSENSORHR*60);

    //get weather
    if (GetWeather()==false) {
      #ifdef _DEBUG
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

  }
  
  if (OldTime[3] != weekday()) {
    if (ALIVESINCE-t > 604800) ESP.restart(); //reset every week
    
    OldTime[3] = weekday();
  }

    //  #ifdef _DEBUG
    // Serial.printf("Loop: exiting at %s, unixtime: %i\n", dateify(now(),"mm/dd/yyyy hh:mm:nn"),now());
    // #endif

}


void handleReboot() {
  server.send(200, "text/plain", "Rebooting in 10 sec");  //This returns to the main page
  delay(10000);
  ESP.restart();
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

void handleGETSTATUS() {
  //someone requested the server's status
  WEBHTML = "STATUS:" + (String) LAST_WEB_REQUEST + ";";
  WEBHTML += "ALIVESINCE:" + (String) ALIVESINCE + ";";
  WEBHTML += "NUMDEVS:" + (String) countDev() + ";";
  
  server.send(200, "text/plain", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

  return;

}

void handleREQUESTWEATHER() {
//if no parameters passed, return current temp, max, min, today weather ID, pop, and snow amount
//otherwise, return the index value for the requested value

  WEBHTML = "";

  if (server.args()==0) {
        WEBHTML += (String) hourly_temp[0] + ";"; //current temp
        WEBHTML += (String) daily_tempMax[0] + ";"; //dailymax
        WEBHTML += (String) daily_tempMin[0] + ";"; //dailymin
        WEBHTML += (String) daily_weatherID[0] + ";"; //dailyID
        WEBHTML += (String) daily_pop[0] + ";"; //POP
        WEBHTML += (String) daily_snow[0] + ";"; //snow
        WEBHTML += (String) sunrise + ";"; //snow
        WEBHTML += (String) sunset + ";"; //snow
  } else {
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i)=="hourly_temp") WEBHTML += (String) hourly_temp[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_tempMax") WEBHTML += (String) daily_tempMax[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_tempMin") WEBHTML += (String) daily_tempMin[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_weatherID") WEBHTML += (String) daily_weatherID[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="hourly_weatherID") WEBHTML += (String) hourly_weatherID[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_pop") WEBHTML += (String) daily_pop[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="hourly_pop") WEBHTML += (String) hourly_pop[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="daily_snow") WEBHTML += (String) daily_snow[server.arg(i).toInt()] + ";";
      if (server.argName(i)=="sunrise") WEBHTML += (String) sunrise + ";";
      if (server.argName(i)=="sunset") WEBHTML += (String) sunset + ";";
      if (server.argName(i)=="hour") {
        uint32_t temptime = server.arg(i).toDouble();
        if (temptime==0) WEBHTML += (String) hour() + ";";
        else WEBHTML += (String) hour(temptime) + ";";
      }
      if (server.argName(i)=="isFlagged") WEBHTML += (String) ((I.isFlagged==true)?1:0) + ";";
      if (server.argName(i)=="isAC") WEBHTML += (String) ((I.isAC==true)?1:0) + ";";
      if (server.argName(i)=="isHeat") WEBHTML += (String) ((I.isHeat==true)?1:0) + ";";
      if (server.argName(i)=="isSoilDry") WEBHTML += (String) ((I.isSoilDry==true)?1:0) + ";";
      if (server.argName(i)=="isHot") WEBHTML += (String) ((I.isHot==true)?1:0) + ";";
      if (server.argName(i)=="isCold") WEBHTML += (String) ((I.isCold==true)?1:0) + ";";
      if (server.argName(i)=="isLeak") WEBHTML += (String) ((I.isLeak==true)?1:0) + ";";

    }
  }
  
  server.send(200, "text/plain", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

  return;
}

void handleTIMEUPDATE() {


  timeClient.forceUpdate();

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}


void handleRoot(void) {
  handlerForRoot(false);
}

void handleALL(void) {
  handlerForRoot(true);
}

void handlerForRoot(bool allsensors) {
  WEBHTML.reserve(7000);

  LAST_WEB_REQUEST = now(); //this is the time of the last web request

  WEBHTML = "";
  WEBHTML = "<!DOCTYPE html><html><head><title>Pleasant Weather Server</title>";
  WEBHTML =WEBHTML  + (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}";
  WEBHTML =WEBHTML  + (String) "body {  font-family: arial, sans-serif; }";
  WEBHTML =WEBHTML  + "</style></head>";
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";

  WEBHTML = WEBHTML + "<body>";
  
  WEBHTML = WEBHTML + "<h1>Pleasant Weather Server</h1>";
  WEBHTML = WEBHTML + "<br>";
  WEBHTML = WEBHTML + "<h2>" + dateify(0,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";
  WEBHTML = WEBHTML + "Free stack Memory: " + ESP.getFreeContStack() + "</h2><br>";  

/*  WEBHTML += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  WEBHTML += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  WEBHTML += "<button type=\"submit\">Update Time</button><br>";
  WEBHTML += "</FORM><br>";
*/

  WEBHTML = WEBHTML + "Sunrise " + dateify(sunrise,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";
  WEBHTML = WEBHTML + "Sunset " + dateify(sunset,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";

  WEBHTML += "Number of sensors" + (String) (allsensors==false ? " (showing monitored sensors only)" : "") + ": " + (String) countDev() + " / " + (String) SENSORNUM + "<br>";
  WEBHTML = WEBHTML + "Alive since: " + dateify(ALIVESINCE,"mm/dd/yyyy hh:nn") + "<br>";
  

  if (I.isFlagged || I.isHeat || I.isAC) {
    WEBHTML = WEBHTML + "<br>---------------------<br><font color=\"#EE4B2B\">";      
    
    if (I.isHeat==true) WEBHTML = WEBHTML + "Heat is on<br>";
    if (I.isAC==true) WEBHTML = WEBHTML + "AC is on<br>";
    if (I.isFlagged==true) WEBHTML = WEBHTML + "Critical sensors are flagged:<br>";
    if (I.isLeak==true) WEBHTML = WEBHTML + "     A leak has been detected!!!<br>";
    if (I.isHot==true) WEBHTML = WEBHTML + "     Interior room(s) over max temp<br>";
    if (I.isCold==true) WEBHTML = WEBHTML + "     Interior room(s) below min temp<br>";
    if (I.isSoilDry==true) WEBHTML = WEBHTML + "     Plant(s) dry<br>";

    WEBHTML = WEBHTML + "</font>---------------------<br>";      
  }


  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM;j++)  {
    used[j] = 255;
  }

  byte usedINDEX = 0;  
  time_t t=now();

  WEBHTML = WEBHTML + "<p><table id=\"Logs\" style=\"width:900px\">";      
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><p><button onclick=\"sortTable(0)\">IP Address</button></p></th style=\"width:50px\"><th>ArdID</th><th style=\"width:200px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:100px\"><button onclick=\"sortTable(4)\">Sns Type</button></p></th style=\"width:100px\"><th><button onclick=\"sortTable(5)\">Flagged</button></p></th><th style=\"width:250px\">Last Recvd</th></tr>"; 
  for (byte j=0;j<SENSORNUM;j++)  {
    if (allsensors && bitRead(Sensors[j].Flags,1)==0) continue;
    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) Sensors[j].IP.toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors[j].IP.toString() + "</a></td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].ardID + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsName + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsValue + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) bitRead(Sensors[j].Flags,0) + (String) (bitRead(Sensors[j].Flags,6) ? "*" : "" ) + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</td>";
      WEBHTML = WEBHTML + "</tr>";
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (allsensors && bitRead(Sensors[jj].Flags,1)==0) continue;
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
          WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) Sensors[jj].IP.toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors[jj].IP.toString() + "</a></td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].ardID + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].snsName + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].snsValue + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].snsType+"."+ (String) Sensors[jj].snsID + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) bitRead(Sensors[jj].Flags,0) + "</td>";
          WEBHTML = WEBHTML + "<td>"  + (String) dateify(Sensors[jj].timeLogged,"mm/dd hh:nn:ss") + "</td>";
          WEBHTML = WEBHTML + "</tr>";
        }
      }
    }
  }

  WEBHTML += "</table>";   

  //add chart
  WEBHTML += "<br>-----------------------<br>\n";
  WEBHTML += "<div id=\"myChart\" style=\"width:100%; max-width:800px; height:200px;\"></div>\n";
  WEBHTML += "<br>-----------------------<br>\n";


  WEBHTML += "</p>";

  #ifdef _WEBDEBUG
      WEBHTML += "<p>WEBDEBUG: <br>"+ WEBDEBUG + "</p><br>";
    #endif

  WEBHTML =WEBHTML  + "<script>";

  //chart functions
    WEBHTML =WEBHTML  + "google.charts.load('current',{packages:['corechart']});\n";
    WEBHTML =WEBHTML  + "google.charts.setOnLoadCallback(drawChart);\n";
    
    WEBHTML += "function drawChart() {\n";

    WEBHTML += "const data = google.visualization.arrayToDataTable([\n";
    WEBHTML += "['t','val'],\n";

    for (int jj = 48-1;jj>=0;jj--) {
      WEBHTML += "[" + (String) ((int) ((double) ((LAST_BAT_READ - 60*60*jj)-t)/60)) + "," + (String) batteryArray[jj] + "]";
      if (jj>0) WEBHTML += ",";
      WEBHTML += "\n";
    }
    WEBHTML += "]);\n\n";

    
    // Set Options
    WEBHTML += "const options = {\n";
    WEBHTML += "hAxis: {title: 'min from now'}, \n";
    WEBHTML += "vAxis: {title: 'Battery%'},\n";
    WEBHTML += "legend: 'none'\n};\n";

    WEBHTML += "const chart = new google.visualization.LineChart(document.getElementById('myChart'));\n";
    WEBHTML += "chart.draw(data, options);\n"; 
    WEBHTML += "}\n";


  WEBHTML += "function sortTable(col) {  var table, rows, switching, i, x, y, shouldSwitch;table = document.getElementById(\"Logs\");switching = true;while (switching) {switching = false;rows = table.rows;for (i = 1; i < (rows.length - 1); i++) {shouldSwitch = false;x = rows[i].getElementsByTagName(\"TD\")[col];y = rows[i + 1].getElementsByTagName(\"TD\")[col];if (x.innerHTML.toLowerCase() > y.innerHTML.toLowerCase()) {shouldSwitch = true;break;}}if (shouldSwitch) {rows[i].parentNode.insertBefore(rows[i + 1], rows[i]);switching = true;}}}";
  WEBHTML += "</script> \n";
  WEBHTML += "</body></html>\n";   

   #ifdef _DEBUG
      Serial.println(WEBHTML);
    #endif
    
  server.send(200, "text/html", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void handlePost() {
SensorVal S;
uint8_t tempIP[4] = {0,0,0,0};

  for (byte k=0;k<server.args();k++) {
   #ifdef _WEBDEBUG
       //WEBDEBUG = WEBDEBUG + "POST: " + server.argName(k) + ": " + String(server.arg(k)) + " @" + String(now(),DEC) + "<br>";
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
  time_t t = now();
  S.timeLogged = t; //time logged by me is when I received this.
  if (S.timeRead == 0  || S.timeRead < t-24*60*60 || S.timeRead > t+24*60*60)     S.timeRead = t;
  
  
  int sn = findDev(&S,true);

  //special cases
      
  //bmp temp received... check for AHT
  if (S.snsType == 10 && findSns(4,false)>-1) {
    if (sn>=0) {
      //already stored a bmp, so erase that
      initSensor(findSns(10,false));
    }
    server.send(202, "text/plain", "Received, but rejected the data because this is BMP and you sent AHT"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    return;
  }

  //AHT_T received... check for BMP
  if (S.snsType == 4 && findSns(10,false)>-1) {
    if (sn<0)    sn = findSns(10,false); //replace BMP_t with AHT_t
    else {
      //AHT was present so I will replace with this read, but bmp was ALSO present... erase that
      initSensor(findSns(10,false));
    }
  }

  //battery voltage received... check for bat %
  if (S.snsType == 60 && findSns(61,false)>-1) {
    if (sn>=0) {
      //already stored a voltage, so erase that
      initSensor(findSns(60,false));
    }
    
    server.send(202, "text/plain", "Received, but rejected the data because this is voltage and you sent %"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    return;
  }

  //baT % received... check for voltage
  if (S.snsType == 61 && findSns(60,false)>-1) {
    if (sn<0)    sn = findSns(60,false); //replace volt with %
    else {
      //% was present so I will replace with this read, but volt was ALSO present... erase that
      initSensor(findSns(60,false));
    }
  }

// END special cases

 

  if((S.snsType==9 || S.snsType == 13) && (LAST_BAR_READ==0 || LAST_BAR_READ < t - 60*60)) { //pressure
    LAST_BAR_READ = S.timeRead;
    LAST_BAR = S.snsValue;
  }

  String stemp = S.snsName;
  if((stemp.indexOf("Outside")>-1 && S.snsType==61 ) && (LAST_BAT_READ==0 || LAST_BAT_READ < t - 60*60 || LAST_BAT_READ > t)) { //outside battery
    LAST_BAT_READ = S.timeRead;
    pushDoubleArray(batteryArray,48,S.snsValue);
  }


  if (sn<0) {
    server.send(201, "text/plain", "Received, but rejected the data because I could not add another sensor"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    return;
  }
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
  
  snprintf(holder,5,"%02d",year(t));
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

  snprintf(DATESTRING,25,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

char* Byte2Bin(uint8_t value, char* output, bool invert) {
  snprintf(output,9,"00000000");
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

