//#define _DEBUG 99
#define ARDNAME "GoogleServer"
#define _USETFT
#define NOISY false
//#define _USEOTA

#include <Arduino.h>
#include <String.h>

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <String.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "SPI.h"
#ifdef _USEOTA
  #include <ArduinoOTA.h>
#endif

#include <Wire.h>

#include <ESP_Google_Sheet_Client.h>
#include <time.h>

//__________________________Graphics

#define LGFX_USE_V1         // set to use new version of library
#include <LovyanGFX.hpp>    // main library
//#include "esp32-hal-psram.h"

// Portrait
#define TFT_WIDTH   320
#define TFT_HEIGHT  480
#define SMALLFONT 1

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7796  _panel_instance;  // ST7796UI
  lgfx::Bus_Parallel8 _bus_instance;    // MCU8080 8B
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_FT5x06  _touch_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.freq_write = 40000000;    
      cfg.pin_wr = 47;             
      cfg.pin_rd = -1;             
      cfg.pin_rs = 0;              

      // tft data interface, 8bit MCU (8080)
      cfg.pin_d0 = 9;              
      cfg.pin_d1 = 46;             
      cfg.pin_d2 = 3;              
      cfg.pin_d3 = 8;              
      cfg.pin_d4 = 18;             
      cfg.pin_d5 = 17;             
      cfg.pin_d6 = 16;             
      cfg.pin_d7 = 15;             

      _bus_instance.config(cfg);   
      _panel_instance.setBus(&_bus_instance);      
    }

    { 
      auto cfg = _panel_instance.config();    

      cfg.pin_cs           =    -1;  
      cfg.pin_rst          =    4;  
      cfg.pin_busy         =    -1; 

      cfg.panel_width      =   TFT_WIDTH;
      cfg.panel_height     =   TFT_HEIGHT;
      cfg.offset_x         =     0;
      cfg.offset_y         =     0;
      cfg.offset_rotation  =     0;
      cfg.dummy_read_pixel =     8;
      cfg.dummy_read_bits  =     1;
      cfg.readable         =  false;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();    

      cfg.pin_bl = 45;              
      cfg.invert = false;           
      cfg.freq   = 44100;           
      cfg.pwm_channel = 7;          

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);  
    }

    { 
      auto cfg = _touch_instance.config();

      cfg.x_min      = 0;
      cfg.x_max      = 319;
      cfg.y_min      = 0;  
      cfg.y_max      = 479;
      cfg.pin_int    = 7;  
      cfg.bus_shared = true; 
      cfg.offset_rotation = 0;

      cfg.i2c_port = 1;//I2C_NUM_1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda  = 6;   
      cfg.pin_scl  = 5;   
      cfg.freq = 400000;  

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);  
    }

    setPanel(&_panel_instance); 
  }
};


static LGFX tft;            // declare display variable

// Variables for touch x,y
static int32_t x,y;

//__________________________Graphics


//----------------------------TIme
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"pool.ntp.org",0,60000);
WebServer server(80);

#define GLOBAL_TIMEZONE_OFFSET  -14400
//----------------------------TIme


#define WIFI_SSID "CoronaRadiata_Guest"
#define WIFI_PASSWORD "snakesquirrel"
#define ESP_GOOGLE_SHEET_CLIENT_USE_PSRAM
#define PROJECT_ID "arduinodatalog-415401"
//Service Account's client email
#define CLIENT_EMAIL "espdatalogger@arduinodatalog-415401.iam.gserviceaccount.com"
//Service Account's private key
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC6e4DR7dGHAwnx\nFe9+B/+RyHGOgflQRVSu0G0xX50CQqLBA5rmVmGsqgLY4lZbFXwYF9NNQtjBMbw/\n5aoTYMRmFjzNETt5hOXRW7xE+VH/krnNZgqNqvOarj8dEEupK9LkknlA/8qql8qP\ng7XEIZLir06ZLDDKqrFiJOk5TPUUVit0Jz+SPRWv3UgAnVk5zgGqwIsil+Q7GXPq\nVmtDOieLL468cz7Bhbvm61q16+WsRf091kiP02BxNBc7Ygf/mea7l3b50GZahCmO\nBVD7LdejUDM8cpxs4lJhVm9OXagQwn64FW1pmBXLaw4FuM4gl/wZsqV60qV8kqo1\nDHZw0NIRAgMBAAECggEAB4ytUHpkdpb9N5wV6vQVkPQkokKBapIy+WSNXh/HAEbc\ntkwQQOHXgJUtqjSADt4P3P0WcCjcE/awroTMic4rp08gSSRI5EyYvfz88k0maF6L\nHH1UgSueAvmpyxI8QOoZ9qhWJbcxUA6W/BBGyxzZmKkkmUOCamsGhUDVqwziOV9p\nqpcpmZKnxlbLtWUaa5YNZCGaVG48KN5WqvUqACrGvQag2tprY/DUvNOPFd+ulAki\nOEFBK/q00Ru3A5l8c+yZRhy5U86nOTZKeDMBIePiEd91rfXHUIwspJ6bRO+zE2UO\nIg3A6TStb6daxyHLD2Dj0SL2exJEpnDp/NeTu+lkYQKBgQDw3Tr+/Z0JhJaIa0je\nTVNWErAJL7Oa87V12L0iJJHR9c0F5Zo++5EF1+uBm+l+raYoaTtEmi/vORE0lOPg\nS/W0l7Z7WPa9JfJCZBNpqIHOSaP+6UP0dUHk/pz21u8+bP1Z01RBvi7Ew21xXEM1\n+JTPhDtE6fEW8jrIqMbZBn9fsQKBgQDGM2+49+9FynTJWKthEJq7A6q9rvt1bhtB\nW8KAgHzKkeHKCVzWj/e+0VhlZrcnDnnjFdJXFEduRSpCycJQBKNKTt1EmBoU5g06\nFweit+FOwSivw7ux1PPUHFSp6ARzK70iCWN9ji3cQVGLdtzfOJU82f5ssKAqPUR8\n5H9nWNqQYQKBgBl835xSDAcQz7kZ2Tkk55epHJWsRY41EdOpnsH5KrEUGKDyHfNi\nPYNnyNULQZcVGwsVr57fzgi7ejWdN8vpXdPBZh8BWALF/C/IVUGOAkZpBoCYAIfi\nzJlF1ChOsDxj3h9ePIFEdcB+iZtATyBr8JtQ+9CcDNYHxe6r5XbbuCjRAoGBAIh5\nBmawoZrGqt+xJGBzlHdNMRXnFNJo/G9mhWkCD+tTw8rf44MCIq7Lazh3H4nPF/Jb\nJjg7iGvPSCgw0JFUgDM8VnNS4DKfrV/gV6udPZCCxEcyWV07qqDU2R8c2WOMLHDx\nUgY0DjPo7gM/1xoE1g3OdLfWbpJnGW99zpQUxHpBAoGAeOoiU25hHxpuuyGZ8d6l\nm/AQAz+ORklGyxOi1HnvZr1y1yVK8+f1sNSTsxe3ZFk4lmeUQdhJWk6/75ma86Fx\nPhV42ri06DSXvU/XUFL8IexqFGrxqet3v104tsUY2PBhyzW/aoIDh2lct2GePjx2\nNyCunqbb3vTNqNcIKOCfLXQ=\n-----END PRIVATE KEY-----\n";
#define USER_EMAIL "jaypath@gmail.com"


#define SENSORNUM 30


struct SensorVal {
  uint8_t IP[4];
  uint8_t ardID;
  uint8_t  snsType ;
  uint8_t snsID;
  char snsName[32];
  double snsValue;
  double snsValue_MAX;
  double snsValue_MIN;
  uint32_t timeRead;
  uint32_t timeLogged;
  bool isSent;  
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value  
};

// unions
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


uint16_t FG_COLOR = TFT_LIGHTGREY; //Foreground color
uint16_t BG_COLOR = TFT_BLACK; //light gray

bool taskComplete = false;
SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
uint32_t lastEvent = 0;
byte uploadQ = 20; //upload every __ minutes
int DSTOFFSET = 0;
char DATESTRING[24]="";
byte OldTime[5] = {0,0,0,0,0};
int Ypos = 0;
bool isFlagged = false;
char tempbuf[70];          

void drawScreen();
String file_findSpreadsheetIDByName(String sheetname, bool createfile = false);
bool file_deleteSpreadsheetByID(String fileID);
String file_createSpreadsheet(String sheetname, bool addHeader = true);
void tokenStatusCallback(TokenInfo info);
void file_deleteSpreadsheetByName(String filename);
char* strPad(char* str, char* pad, byte L);
bool inIndex(byte lookfor,byte used[],byte arraysize);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
char* Byte2Bin(uint8_t value, char* , bool invert = false);
void handleLIST();
void handleNotFound();
void handlePost();
void handleRoot();
void handleCLEARSENSOR();
void handleTIMEUPDATE();
void timeUpdate(void);
void initSensor(byte);
int16_t findOldestDev();
uint8_t countDev();
int16_t findDev(struct SensorVal *S, bool oldest = false);
double valSensorType(byte snsType, bool asAvg = false, int isflagged=-1, int isoutdoor=-1, uint32_t MoreRecentThan = 0);
bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum);
bool IPString2ByteArray(String IPstr,byte* IP);
String fcnDOW(time_t t);
bool file_uploadData(void);
void fcnChooseTxtColor(byte snsIndex);
String IP2String(byte* IP);



void initSensor(byte k) {
  sprintf(Sensors[k].snsName,"");
  Sensors[k].IP[0] = 0;
  Sensors[k].IP[1] = 0;
  Sensors[k].IP[2] = 0;
  Sensors[k].IP[3] = 0;
  Sensors[k].ardID=0;
  Sensors[k].snsType=0;
  Sensors[k].snsID=0;
  Sensors[k].snsValue=-1000;
  Sensors[k].snsValue_MAX=-10000;
  Sensors[k].snsValue_MIN=10000;
  Sensors[k].timeRead=0;
  Sensors[k].timeLogged=0;
  Sensors[k].isSent=false;  
  Sensors[k].Flags = 0;
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID > 0) c++; 
  }
  return c;
}

int16_t findOldestDev() {
  //return 0 entry or oldest
  int thisInd = 0;
  int oldestInd = 0;
  int  i=0;
  for (i=0;i<SENSORNUM;i++) {
    if (Sensors[i].ardID == 0 && Sensors[i].snsType == 0 && Sensors[i].snsID ==0) return i;
    
    if (Sensors[i].timeLogged< Sensors[oldestInd].timeLogged ) oldestInd = i;
    
  }
  
//  if (Sensors[oldestInd].timeLogged == 0) oldestInd = -1;

  return oldestInd;
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


void clearTFT() {
  tft.fillScreen(BG_COLOR);                 // Fill with black
  tft.setCursor(0,0);
  Ypos = 0;
}

void setup()
{

 tft.init();

  // Setting display to landscape
  tft.setRotation(2);

  clearTFT();

  tft.printf("Running setup\n");

  // Get flash size and display
  tft.printf("FLASH size: %d bytes",ESP.getMinFreeHeap());

  // Initialize PSRAM (if available) and show the total size
  if (psramFound()) {
    tft.printf("PSRAM size: %d bytes",heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    
  } else {
    tft.printf("No PSRAM found.");
  }
delay(2000);


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
    while (WiFi.status() != WL_CONNECTED)
    {
#ifdef _DEBUG
        Serial.print(".");
#endif
        delay(300);
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        if (millis() - ms > 10000)
            break;
#endif
    }
#ifdef _DEBUG
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();
#endif

  tft.setCursor(0,10);
    tft.print("Connected with IP: ");
    tft.println(WiFi.localIP());


    // The WiFi credentials are required for Pico W
    // due to it does not have reconnect feature.
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    GSheet.clearAP();
    GSheet.addAP(WIFI_SSID, WIFI_PASSWORD);
    // You can add many WiFi credentials here
#endif

    // Set the callback for Google API access token generation status (for debug only)
    GSheet.setTokenCallback(tokenStatusCallback);
    // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
    GSheet.setPrerefreshSeconds(10 * 60);

    //Begin the access token generation for Google API authentication
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    // In case SD/SD_MMC storage file access, mount the SD/SD_MMC card.
    // SD_Card_Mounting(); // See src/GS_SDHelper.h

    // Or begin with the Service Account JSON file that uploaded to the Filesystem image or stored in SD memory card.
    // GSheet.begin("path/to/serviceaccount/json/file", esp_google_sheet_file_storage_type_flash /* or esp_google_sheet_file_storage_type_sd */);


#ifdef _USEOTA

   ArduinoOTA.setHostname(ARDNAME);
  ArduinoOTA.onStart([]() {
    #ifdef _DEBUG
    Serial.println("OTA started");
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
      tft.clear();
      tft.setCursor(0,0);
      tft.println("Receiving OTA:");
      String strbuff = "Progress: " + (100*progress / total);
      tft.println("OTA start.");   
      tft.println(strbuff);
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
      tft.clear();
      tft.setCursor(0,0);
      String strbuff;
      strbuff = "Error[%u]: " + (String) error + " ";
      tft.print(strbuff);
      if (error == OTA_AUTH_ERROR) tft.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) tft.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) tft.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) tft.println("Receive Failed");
      else if (error == OTA_END_ERROR) tft.println("End Failed");
    #endif
  });
  ArduinoOTA.begin();
    #ifdef _USETFT
      tft.clear();
      tft.setCursor(0,0);
      tft.println("OTA OK.");      
    #endif
#endif




    #ifdef _USETFT
      tft.println("start Server.");
    #endif

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/LIST", handleLIST);   
    server.on("/POST", handlePost);   
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    
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

    tft.print("TimeClient OK. I will pull from server... ");

    //init globals
    for ( byte i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }

  clearTFT();
  #ifdef _USETFT
    tft.clear();
    tft.setCursor(0,0);
    tft.println(WiFi.localIP().toString());
    tft.print(hour());
    tft.print(":");
    tft.println(minute());
  #endif



}

void timeUpdate() {
  timeClient.update();
  if (month() < 3 || (month() == 3 &&  day() < 10) || month() ==12 || (month() == 11 && day() >=3)) DSTOFFSET = -1*60*60; //2024 DST
  else DSTOFFSET = 0;

  setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  return;
}


void loop()
{
    //Call ready() repeatedly in loop for authentication checking and processing
    bool ready = GSheet.ready();

#ifdef _USEOTA
  ArduinoOTA.handle();
#endif

  server.handleClient();
  timeClient.update();

  time_t t = now();
  
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second
    uint32_t b = millis()/1000;
    
    #ifdef _DEBUG
    Serial.println("1 second: ");
    Serial.print(dateify(0,"hh:nn:ss"));
    Serial.print("  ");
    Serial.print((uint32_t) heap_caps_get_free_size(MALLOC_CAP_8BIT)/1000);
    Serial.print("k  ");
    Serial.print((uint32_t) ESP.getMinFreeHeap()/1000);
    Serial.print("k");
    
    
    Serial.print("Alive for ");
    if (b > 86400) {
      Serial.print((int) (b)/86400);
      Serial.print("d");
    } else {
      if (b > 3600) {
        Serial.print((int) (b)/3600);
        Serial.print("h");
      } else {
        if (b > 120) {
          Serial.print((int) (b)/60);
          Serial.print("m");  
        } else {
          Serial.print((int) (b));
          Serial.println("s");
        } 

      }
    }      
    #endif    
    tft.setTextColor(FG_COLOR,BG_COLOR);

    tft.setTextFont(SMALLFONT);
    tft.setCursor(0,440);
    tft.print(dateify(0,"hh:nn:ss"));
    tft.print("  ");
    tft.print((uint32_t) heap_caps_get_free_size(MALLOC_CAP_8BIT)/1000);
    tft.print("k  ");
    tft.print((uint32_t) ESP.getMinFreeHeap()/1000);
    tft.print("k");
    
    tft.setCursor(0,440+tft.fontHeight(SMALLFONT)+2);
    
    tft.print("Alive for ");

    if (b > 86400) {
      tft.print((int) (b)/86400);
      tft.print("d");
    } else {
      if (b > 3600) {
        tft.print((int) (b)/3600);
        tft.print("h");
      } else {
        if (b > 120) {
          tft.print((int) (b)/60);
          tft.print("m");  
        } else {
          tft.print((int) (b));
          tft.print("s");
        } 
        tft.print("             ");
      }
    }
    
    tft.setCursor(0,440+2*(tft.fontHeight(SMALLFONT)+2));
    tft.printf("Next Upload in: %d min",uploadQ - minute(t)%uploadQ);


    tft.setCursor(0,Ypos);
      
  }
      
  


  if (OldTime[1] != minute()) {
  OldTime[1] = minute();
  //do stuff every minute
  
    drawScreen();

    if (ready && (OldTime[1]%uploadQ)==0) {
      #ifdef _DEBUG
        Serial.println("uploading to sheets!");
      #endif
      if (file_uploadData()==false) {
        tft.setTextFont(SMALLFONT);
        sprintf(tempbuf,"Failed to upload!");
        tft.drawString(tempbuf, 0, Ypos);
        Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
      }
    }

    if (OldTime[2] != hour()) {
      OldTime[2] = hour(); 

      timeUpdate();
    }

    if (OldTime[3] != weekday()) {
      OldTime[3] = weekday();
      //daily

      for (byte j=0;j<SENSORNUM;j++) {
        Sensors[j].snsValue_MAX = -10000;
        Sensors[j].snsValue_MIN = 10000;
        
        if (t>86400 && Sensors[j].timeLogged<(t-86400)) initSensor(j);

      }

    }
  }
}



void fcnChooseTxtColor(byte snsIndex) {

  if (bitRead(Sensors[snsIndex].Flags,0)) {
    tft.setTextColor(TFT_RED,TFT_BLACK);
  }
  return;
}


void drawScreen() {
  clearTFT();
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  sprintf(tempbuf,"%s",dateify(now(),"hh:nn mm/dd"));
  Ypos = Ypos + tft.fontHeight(4)/2;
  tft.drawString(tempbuf, tft.width() / 2, Ypos);
  tft.setTextDatum(TL_DATUM);

  Ypos += tft.fontHeight(4)/2 + 6;
  tft.fillRect(0,Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
//            ardid time    name    min  cur   max    * for sent

  tft.fillRect(45,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(85,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(170,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(210,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(250,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(290,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  Ypos += 4;

  tft.setTextFont(SMALLFONT);
  tft.setTextColor(TFT_DARKGRAY,BG_COLOR);

    snprintf(tempbuf,60,"ID");
    tft.drawString(tempbuf, 0, Ypos);

    snprintf(tempbuf,60,"Time");
    tft.drawString(tempbuf, 47, Ypos);

    snprintf(tempbuf,60,"Name");
    tft.drawString(tempbuf, 87, Ypos);

    snprintf(tempbuf,60,"MIN");
    tft.drawString(tempbuf, 172, Ypos);

    snprintf(tempbuf,60,"Val");
    tft.drawString(tempbuf, 212, Ypos);

    snprintf(tempbuf,60,"MAX");
    tft.drawString(tempbuf, 252, Ypos);

    snprintf(tempbuf,60,"SNT");
    tft.drawString(tempbuf, 292, Ypos);

  tft.setTextColor(FG_COLOR,BG_COLOR);

  Ypos += tft.fontHeight(SMALLFONT) + 2;

  tft.setTextFont(SMALLFONT); //
  byte usedINDEX = 0;  
  byte used[SENSORNUM];

  for(byte i=0;i<SENSORNUM;i++) {
    //march through sensors and list by ArdID
    
    if (Sensors[i].snsID>0 && Sensors[i].snsType>0 && inIndex(i,used,SENSORNUM) == false && Sensors[i].timeLogged>0 ) {
      used[usedINDEX++] = i;
      fcnChooseTxtColor(i);
      tft.setTextFont(SMALLFONT);

      snprintf(tempbuf,60,"%s", (String) Sensors[i].ardID + (String) "." + (String) Sensors[i].snsType);
      tft.drawString(tempbuf, 0, Ypos);

      snprintf(tempbuf,60,"%s",dateify(Sensors[i].timeLogged,"hh:nn"));
      tft.drawString(tempbuf, 47, Ypos);

      snprintf(tempbuf,60,"%s", Sensors[i].snsName);
      tft.drawString(tempbuf, 87, Ypos);

      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue_MIN);
      tft.drawString(tempbuf, 172, Ypos);

      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue);
      tft.drawString(tempbuf, 212, Ypos);

      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue_MAX);
      tft.drawString(tempbuf, 252, Ypos);

      snprintf(tempbuf,60,"%s",Sensors[i].isSent ? "**" : "");
      tft.drawString(tempbuf, 292, Ypos);

      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;

    // organize by sensor 
      for(byte j=i+1;j<SENSORNUM;j++) {
        if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false && Sensors[j].ardID==Sensors[i].ardID) {
          used[usedINDEX++] = j;
          fcnChooseTxtColor(j);
          tft.setTextFont(SMALLFONT);

          snprintf(tempbuf,60,"%s", (String) Sensors[j].ardID + (String) "." + (String) Sensors[j].snsType);
          tft.drawString(tempbuf, 0, Ypos);

          snprintf(tempbuf,60,"%s",dateify(Sensors[j].timeLogged,"hh:nn"));
          tft.drawString(tempbuf, 47, Ypos);

          snprintf(tempbuf,60,"%s", Sensors[j].snsName);
          tft.drawString(tempbuf, 87, Ypos);

          snprintf(tempbuf,60,"%d",(int) Sensors[j].snsValue_MIN);
          tft.drawString(tempbuf, 172, Ypos);

          snprintf(tempbuf,60,"%d",(int) Sensors[j].snsValue);
          tft.drawString(tempbuf, 212, Ypos);

          snprintf(tempbuf,60,"%d",(int) Sensors[j].snsValue_MAX);
          tft.drawString(tempbuf, 252, Ypos);

          snprintf(tempbuf,60,"%s",Sensors[j].isSent ? "**" : "");
          tft.drawString(tempbuf, 292, Ypos);

          Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
        }
      }
    }
  tft.setTextColor(FG_COLOR,BG_COLOR);
  }
}

bool file_deleteSpreadsheetByID(String fileID){
  FirebaseJson response;
  
   bool success= GSheet.deleteFile(&response /* returned response */, fileID /* spreadsheet Id to delete */);
   //String tmp;
   //response.toString(tmp, true);
//  tft.printf("Delete result: %s",tmp.c_str());
  return success;
}

void file_deleteSpreadsheetByName(String filename){
  FirebaseJson response;
  
  String fileID;


  do {
    fileID = file_findSpreadsheetIDByName(filename);
    if (fileID!="" && fileID.substring(0,5)!="ERROR") {
      tft.setTextFont(SMALLFONT);
      sprintf(tempbuf,"Deletion of %s status: %s\n",fileID.c_str(),file_deleteSpreadsheetByID(fileID) ? "OK" : "FAIL");
      tft.drawString(tempbuf, 0, Ypos);
      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
    }
  } while (fileID!="" && fileID.substring(0,5)!="ERROR");
  return;
}


String file_findSpreadsheetIDByName(String sheetname, bool createfile) {
  //returns the file ID. Note that sheetname is NOT unique, so multiple spreadsheets could have the same name. Only ID is unique.
  //returns "" if no such filename
          
  FirebaseJson filelist;

  String resultstring = "ERROR:";  
  String thisFileID = "";
  
  String tmp;
  bool success = GSheet.listFiles(&filelist /* returned response */);
    if (NOISY) {
      tft.setTextFont(SMALLFONT);
      snprintf(tempbuf,60,"%s search result: %s\n",sheetname.c_str(),success?"OK":"FAIL");
      tft.drawString(tempbuf, 0, Ypos);
      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
    }
  
  if (success) {
    #ifdef _DEBUG
      filelist.toString(Serial, true);
    #endif

    if (sheetname == "***") {
      //special case, return the entire file index!
      filelist.toString(resultstring,true);
      tft.printf("%s\n",resultstring.c_str());
      return "";
    }

    //put file array into data object
    FirebaseJsonData result;
    filelist.get(result,"files");

    if (result.success) {

      FirebaseJsonArray thisfile;
      result.get<FirebaseJsonArray /* type e.g. FirebaseJson or FirebaseJsonArray */>(thisfile /* object that used to store value */);
            
      int fileIndex=0;
      bool foundit=false;
  
      do {
        thisfile.get(result,fileIndex++); //iterate through each file
        if (result.success) {
          FirebaseJson fileinfo;

          //Get FirebaseJson data
          result.get<FirebaseJson>(fileinfo);

//          fileinfo.toString(tmp,true);

          size_t count = fileinfo.iteratorBegin();
          
          for (size_t i = 0; i < count; i++) {
            
            FirebaseJson::IteratorValue value = fileinfo.valueAt(i);
            String s1(value.key);
            String s2(value.value);
            s2=s2.substring(1,s2.length()-1);

            if (s1 == "id") thisFileID = s2;
            if (s1 == "name" && (s2 == sheetname || sheetname == "*")) foundit=true; //* is a special case where any file returned
          }
          fileinfo.iteratorEnd();
          if (foundit) {
              if (NOISY) {
                tft.setTextFont(SMALLFONT);
                snprintf(tempbuf,60,"FileID is %s\n",thisFileID.c_str());
                tft.drawString(tempbuf, 0, Ypos);
                Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
              }
           return thisFileID;
          }
        }
      } while (result.success);

    }

    resultstring= "ERROR: no files found";
  
    if (createfile) {
      resultstring = file_createSpreadsheet(sheetname, true);
      if (NOISY) {
        tft.setTextFont(SMALLFONT);
        snprintf(tempbuf,60,"new fileid: %s\n",resultstring.c_str());
        tft.drawString(tempbuf, 0, Ypos);
        Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
      }
    }         
       
    
  } else { //gsheet error
      if (NOISY) {
        tft.setTextFont(SMALLFONT);
        snprintf(tempbuf,60,"Gsheet error:%s\n",GSheet.errorReason().c_str());
        tft.drawString(tempbuf, 0, Ypos);
        Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
      }
    resultstring = "ERROR: gsheet error";
    
  }
  return resultstring; 

}


String file_createSpreadsheet(String sheetname, bool addHeader) {


String fileID = file_findSpreadsheetIDByName(sheetname);
if (fileID.substring(0,5)!="ERROR") return fileID;

        FirebaseJson spreadsheet;
        spreadsheet.set("properties/title", sheetname);
        spreadsheet.set("sheets/properties/title", "Sheet1");
        spreadsheet.set("sheets/properties/sheetId", 1); //readonly
        spreadsheet.set("sheets/properties/sheetType", "GRID");
        spreadsheet.set("sheets/properties/sheetType", "GRID");
        spreadsheet.set("sheets/properties/gridProperties/rowCount", 200000);
        spreadsheet.set("sheets/properties/gridProperties/columnCount", 10);

        spreadsheet.set("sheets/developerMetadata/[0]/metadataValue", "Jaypath");
        spreadsheet.set("sheets/developerMetadata/[0]/metadataKey", "Creator");
        spreadsheet.set("sheets/developerMetadata/[0]/visibility", "DOCUMENT");

  FirebaseJson response;

  bool success = GSheet.create(&response /* returned response */, &spreadsheet /* spreadsheet object */, USER_EMAIL /* your email that this spreadsheet shared to */);

     
  if (success) {
    fileID = file_findSpreadsheetIDByName(sheetname);
    if (addHeader) {
      FirebaseJson valueRange;
      valueRange.add("majorDimension", "ROWS");
      valueRange.set("values/[0]/[0]", "SnsID");
      valueRange.set("values/[0]/[1]", "IP Add");
      valueRange.set("values/[0]/[2]", "SnsName");
      valueRange.set("values/[0]/[3]", "Time Logged");
      valueRange.set("values/[0]/[4]", "Time Read");
      valueRange.set("values/[0]/[5]", "HumanTime");
      valueRange.set("values/[0]/[6]", "Flags");
      valueRange.set("values/[0]/[7]", "Reading");

      bool success = GSheet.values.append(&response /* returned response */, fileID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
    }
    return fileID;
    
  
  } 
  
      if (NOISY) {
        tft.setTextFont(SMALLFONT);
        snprintf(tempbuf,60,"CREATE: gsheet failed to create object\n");
        tft.drawString(tempbuf, 0, Ypos);
        Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
      }
  
  
    return "ERROR: Gsheet failed to create";

}

bool file_uploadData(void) {

  FirebaseJson valueRange;
  FirebaseJson response;

  time_t t = now();

  String logid;
  String fileID = (String) "ArduinoLog" + dateify(t,"yyyy-mm");
  fileID = file_findSpreadsheetIDByName(fileID,true);

  if (fileID=="" || fileID.substring(0,5)=="ERROR") return false;

  byte rowInd = 0;
  byte snsArray[SENSORNUM];
  for (byte j=0;j<SENSORNUM;j++) snsArray[j]=255;

  for (byte j=0;j<SENSORNUM;j++) {
    if (Sensors[j].timeLogged>0 && Sensors[j].isSent == false) {
      logid = (String) Sensors[j].ardID + "." + (String)  Sensors[j].snsType + "." + (String)  Sensors[j].snsID;

      valueRange.add("majorDimension", "ROWS");
      valueRange.set("values/[" + (String) rowInd + "]/[0]", logid);
      valueRange.set("values/[" + (String) rowInd + "]/[1]", IP2String(Sensors[j].IP));
      valueRange.set("values/[" + (String) rowInd + "]/[2]", (String) Sensors[j].snsName);
      valueRange.set("values/[" + (String) rowInd + "]/[3]", (String) Sensors[j].timeLogged);
      valueRange.set("values/[" + (String) rowInd + "]/[4]", (String) Sensors[j].timeRead);
      valueRange.set("values/[" + (String) rowInd + "]/[5]", (String) dateify(Sensors[j].timeRead,"mm/dd/yy hh:nn:ss"));
      valueRange.set("values/[" + (String) rowInd + "]/[6]", (String) bitRead(Sensors[j].Flags,0));
      valueRange.set("values/[" + (String) rowInd + "]/[7]", (String) Sensors[j].snsValue);
  
      snsArray[rowInd++] = j;
    }
  }

        // For Google Sheet API ref doc, go to https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/append

  bool success = GSheet.values.append(&response /* returned response */, fileID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
  if (success) {
//            response.toString(Serial, true);
    for (byte j=0;j<SENSORNUM;j++) {
      if (snsArray[j]!=255)     Sensors[snsArray[j]].isSent = true;
    }
    return true;
}

return false;

          

}

void tokenStatusCallback(TokenInfo info)
{
    if (info.status == token_status_error)
    {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
        GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
    }
    else
    {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
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
      currentLine += IP2String(Sensors[j].IP) + "<br>";
      currentLine += (String) Sensors[j].snsID + "<br>";
      currentLine += (String) Sensors[j].snsType + "<br>";
      currentLine += (String) Sensors[j].snsName + "<br>";
      currentLine += (String) Sensors[j].snsValue + "<br>";
      currentLine += (String) Sensors[j].snsValue_MAX + "<br>";
      currentLine += (String) Sensors[j].snsValue_MIN + "<br>";
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
      sprintf(temp,"%s","IP Address");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + "</label>";
      currentLine += " <a href=\"http://" + IP2String(Sensors[j].IP) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + IP2String(Sensors[j].IP) + "</a><br>";
      
      currentLine = currentLine +  String("..............................") + String("<br>");

      sprintf(temp,"%s","Sensor Name");
      strPad(temp,padder,25);
      currentLine += (String) "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsName + "</label><br>";

      sprintf(temp,"%s","Value");
      strPad(temp,padder,25);
      currentLine += (String)"<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsValue + "</label><br>";

      sprintf(temp,"%s","Flagged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[j].Flags,0) + "</label><br>";
      
      sprintf(temp,"%s","Sns Type");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";

      sprintf(temp,"%s","Last Recvd");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeRead + " " + (String) dateify(Sensors[j].timeRead,"mm/dd hh:nn:ss") +  "</label><br>";
      
      sprintf(temp,"%s","Last Logged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeLogged + " " + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";

      Byte2Bin(Sensors[j].Flags,tempchar,true);
      sprintf(temp,"%s","Flags");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";
      currentLine += "<br>";
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
 
    
          sprintf(temp,"%s","Sensor Name");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsName + "</label><br>";
    
          sprintf(temp,"%s","Value");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsValue + "</label><br>";
    
          sprintf(temp,"%s","Flagged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[jj].Flags,0) + "</label><br>";
          
          sprintf(temp,"%s","Sns Type");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";
    
          sprintf(temp,"%s","Last Recvd");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeRead + " " + (String) dateify(Sensors[jj].timeRead,"mm/dd hh:nn:ss") + "</label><br>";
          
          sprintf(temp,"%s","Last Logged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeLogged + " " + (String) dateify(Sensors[jj].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";
    
          Byte2Bin(Sensors[jj].Flags,tempchar,true);
          sprintf(temp,"%s","Flags");
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

   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif
    
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void handlePost() {

        #ifdef _DEBUG
          Serial.println("handlePOST hit.");
        #endif

SensorVal S;
uint8_t tempIP[4] = {0,0,0,0};

  for (byte k=0;k<server.args();k++) {
   #ifdef _WEBDEBUG
       WEBDEBUG = WEBDEBUG + server.argName(k) + ": " + String(server.arg(k)) + " @" + String(now(),DEC) + "\n";
    #endif

      #ifdef _DEBUG
      Serial.print("Received this arg: ");
      Serial.println(server.arg(k));
      #endif

      if ((String)server.argName(k) == (String)"logID")  breakLOGID(String(server.arg(k)),&S.ardID,&S.snsType,&S.snsID);
      if ((String)server.argName(k) == (String)"IP") {
        IPString2ByteArray(String(server.arg(k)),tempIP);
        S.IP[0] = tempIP[0];
        S.IP[1] = tempIP[1];
        S.IP[2] = tempIP[2];
        S.IP[3] = tempIP[3];
        
      }
      if ((String)server.argName(k) == (String)"varName") sprintf(S.snsName,"%s", server.arg(k).substring(0,30).c_str());
      if ((String)server.argName(k) == (String)"varValue") S.snsValue = server.arg(k).toDouble();
      if ((String)server.argName(k) == (String)"timeLogged") S.timeRead = server.arg(k).toDouble();      //time logged at sensor is time read by me
      if ((String)server.argName(k) == (String)"Flags") S.Flags = server.arg(k).toInt();
  }
  

          #ifdef _DEBUG
          Serial.println("handlePOST parsed input.");
        #endif

  S.timeLogged = now(); //time logged by me is when I received this.
  if (S.timeRead == 0)     S.timeRead = now();
  
  
  S.isSent = false;

  int16_t sn = findDev(&S,true);
  S.snsValue_MIN= Sensors[sn].snsValue_MIN;
  S.snsValue_MAX= Sensors[sn].snsValue_MAX;

  if ((double) S.snsValue_MAX<S.snsValue) S.snsValue_MAX=S.snsValue;
  
  if ((double) S.snsValue_MIN>S.snsValue) S.snsValue_MIN=S.snsValue;
  
  
        #ifdef _DEBUG
          Serial.println("handlePOST about to enter finddev.");
        #endif

  if (sn<0 || sn >= SENSORNUM) return;
  Sensors[sn] = S;

        #ifdef _DEBUG
          Serial.println("handlePOST assigned val.");
        #endif

  server.send(200, "text/plain", "Received Data"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request


}

char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  sprintf(holder,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  sprintf(holder,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  sprintf(holder,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  sprintf(holder,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  sprintf(holder,"%02d",hour(t));
  dateformat.replace("hh",holder);

  sprintf(holder,"%02d",minute(t));
  dateformat.replace("nn",holder);

  sprintf(holder,"%02d",second(t));
  dateformat.replace("ss",holder);

  sprintf(holder,"%s",fcnDOW(t));
  dateformat.replace("DOW",holder);
  
  sprintf(DATESTRING,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

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

char* Byte2Bin(uint8_t value, char* output, bool invert) {
  sprintf(output,"00000000");
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

    if (IP[0] ==0)       IPs = IPs + String(192,DEC) + ".";
    else IPs = IPs + String(IP[0],DEC) + ".";

    if (IP[1] ==0)       IPs = IPs + String(168,DEC) + ".";
    else IPs = IPs + String(IP[1],DEC) + ".";

    IPs = IPs + String(IP[2],DEC) + ".";

    IPs = IPs + String(IP[3],DEC);
  return IPs;
}

bool IPString2ByteArray(String IPstr,byte* IP) {
        
    String temp;
    
    byte strLen = IPstr.length();
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
    
    byte strLen = logID.length();
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
  int16_t strLen,strOffset;

  #ifdef _DEBUG
    Serial.println("I am beginning parsing!");
    Serial.println(token);
  #endif

  strLen = token.length();
  strOffset = token.indexOf(",",0);
  if (strOffset == -1) { //did not find the comma, we may have cut the last one. abort.
    return;
  } else {
    //element 1 is IP
    logID= token.substring(0,strOffset); //end index is NOT inclusive
    token.remove(0,strOffset+1);
    IPString2ByteArray(logID,tempIP);
        S.IP[0] = tempIP[0];
        S.IP[1] = tempIP[1];
        S.IP[2] = tempIP[2];
        S.IP[3] = tempIP[3];
 
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
  sprintf(S.snsName,"%s",temp.c_str());
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


  int16_t sn = findDev(&S,true);

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


void fcnPrintTxtColor(int value,int alarmMin = 40, int alarmMax = 88,int x=-1,int y=-1) {

  if (value<alarmMin) {
    tft.setTextColor(TFT_BLUE,TFT_WHITE);
  }
  if (value>alarmMax) {
    tft.setTextColor(TFT_BLACK,TFT_RED);
  }
  if (x<0) {
    tft.print(value);
  } else {
    
    
    sprintf(tempbuf,"%d",value);
    tft.drawString(tempbuf, x,y);
  }  
  tft.setTextColor(FG_COLOR,BG_COLOR);
  return;
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
