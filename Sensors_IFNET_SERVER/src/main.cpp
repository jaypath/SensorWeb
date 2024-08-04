#define _DEBUG 99
#define ARDNAME "IFNETSERVER"
#define _USETFT
#define _USETOUCH
#define NOISY false
#define _USEOTA
#define ALTSCREENTIME 30


#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include <String.h>
#include "SPI.h"
#include <SD.h>
#include <FS.h>

#ifdef _USEOTA
  #include <ArduinoOTA.h>
#endif

#include <Wire.h>

//__________________________Graphics

#define LGFX_USE_V1         // set to use new version of library
#include <LovyanGFX.hpp>    // main library
//#include "esp32-hal-psram.h"

// Portrait
#define TFT_WIDTH   320
#define TFT_HEIGHT  480
#define SMALLFONT 1
#define MEDFONT 2


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
int32_t touch_x,touch_y;

//__________________________Graphics



WebServer server(80);



#define WIFI_SSID "IFNET_ELECTRONICS"
#define WIFI_PASSWORD "IFNET2024"


#define SENSORNUM 60


struct SensorVal {
  uint8_t ID;
  bool HASMPU;
  bool HASBMP;
  bool HASOLED;
  uint8_t  QuizScore ;
  uint8_t SimonLevel;
  uint8_t SimonScore;
  char UserName[12];
  uint32_t timeLogged;  
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


uint16_t FG_COLOR = TFT_BLACK; //Foreground color
uint16_t BG_COLOR = TFT_LIGHTGREY; //light gray


uint32_t OldTime[2] = {999999,999999};
bool SHOWMAIN = true;

SensorVal DEVICES[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
uint8_t SIMONCHALLENGE[10] = {0};

int Ypos = 0;

char tempbuf[70];          

bool NEWDATA = false;
uint32_t DRAWALT = 0;

// Set your Static IP address
IPAddress local_IP(192, 168, 100, 100);
IPAddress gateway(192, 168, 100, 1);
IPAddress subnet(255, 255, 255, 0);

void drawScreen_list(bool forceredraw=false);

void handleNotFound();
void handlePost();
void handleRoot();

void initDevices(byte);
uint8_t countDev();
int16_t findDev(struct SensorVal *S, bool registerNew = false);
int16_t findEmptyDevSlot();

void initDevices(byte k) {
  
if (k==255) {
  for (byte j=0;j<SENSORNUM;j++) {
    initDevices(j);
  }
  return;
}

  sprintf(DEVICES[k].UserName,"");
  DEVICES[k].ID=0;
  DEVICES[k].QuizScore=0;
  DEVICES[k].SimonLevel=0;
  DEVICES[k].SimonScore=0;
  DEVICES[k].HASMPU = false;
  DEVICES[k].HASBMP = false;
  DEVICES[k].HASOLED = false;
  DEVICES[k].timeLogged = 0;
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (DEVICES[j].ID > 0) c++; 
  }
  return c;
}

int16_t findEmptyDevSlot() {
      for (int j=0;j<SENSORNUM;j++)  {
        if (DEVICES[j].ID == 0) return j;
      }

  return -1; //no empty slot found!
}

int16_t findDev(struct SensorVal *S, bool registerNew) {
  //provide the desired devID and snsname, and will return the index to sensors array that represents that node
  //special cases:
  //  if register new then find first available slot and add there!
  //if no finddev identified and oldest = true, will return first empty or oldest
  //if no finddev and oldest = false, will return -1

        #ifdef _DEBUG
          Serial.printf("FINDDEV: I am looking for %i.\n",S->ID);
        #endif


  if (S->ID==0) {
        #ifdef _DEBUG
          Serial.println("FINDDEV: Error, you searched for aan ID of zero!");
        #endif

    return -1;  //can't find a zero ID!
  }
  for (int j=0;j<SENSORNUM;j++)  {
      if (DEVICES[j].ID == S->ID) {
        #ifdef _DEBUG
          Serial.print("FINDDEV: I foud this dev, and the index is: ");
          Serial.println(j);
        #endif
        
        return j;
      }
    }

    //got here if the device was not found
    if (registerNew) {
      int16_t sn = findEmptyDevSlot();
      if (sn>=0) DEVICES[sn] = *S;
      return sn;
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

  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
  tft.init();

  // Setting display to landscape
  tft.setRotation(2);

  clearTFT();

  tft.printf("Running setup\n");

  if(!SD.begin(41)){  //CS=41
    tft.println("SD Mount Failed");
  } 
  else {
    tft.println("SD Mount Succeeded");
  }

  // Get flash size and display
  tft.printf("FLASH size: %d bytes",ESP.getMinFreeHeap());

  // Initialize PSRAM (if available) and show the total size
  if (psramFound()) {
    tft.printf("PSRAM size: %d bytes",heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    
  } else {
    tft.printf("No PSRAM found.");
  }


#ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Serial started.");
#endif

    // Set auto reconnect WiFi or network connection
#if defined(ESP32) || defined(ESP8266)
    WiFi.setAutoReconnect(true);
#endif

if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

// Connect to WiFi or network
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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



#ifdef _USEOTA

    ArduinoOTA.setHostname(ARDNAME);
    ArduinoOTA.onStart([]() {
    #ifdef _DEBUG
    Serial.println("OTA started");
    #endif
    #ifdef _USETFT
    tft.drawRect(5,tft.height()/2-5,tft.width()-10,10,TFT_BLACK);
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
      tft.setTextFont(4);
      tft.setTextColor(FG_COLOR,BG_COLOR);
      tft.setTextDatum(TL_DATUM);
      tft.println("Receiving OTA:");
      //String strbuff = "Progress: " + (100*progress / total);
      tft.println("OTA start.");
      
      tft.fillRect(5,tft.height()/2-5,(int) (double (tft.width()-10)*progress / total),10,TFT_BLACK);
       
      //tft.println(strbuff);
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
    server.on("/POST", handlePost);   
    
    server.onNotFound(handleNotFound);
    server.begin();
    //init globals
    initDevices(255);



  clearTFT();
  #ifdef _USETFT
    tft.clear();
    tft.setCursor(0,0);
    tft.println(WiFi.localIP().toString());
    
    
  #endif


}


void loop()
{
  server.handleClient();
  uint32_t m = millis();

  bool forceredraw=false;

#ifdef _USEOTA
  ArduinoOTA.handle();
#endif
#ifdef _USETOUCH
  if (tft.getTouch(&touch_x, &touch_y)) {
      #ifdef _DEBUG
        Serial.printf("Touch: X= %d, Y= %d\n",touch_x, touch_y);
      #endif
      if (m>DRAWALT+5000) {
        DRAWALT = m;
      
      } else {
        DRAWALT = 0;
        
      }
      NEWDATA=true;
      delay(100); //to avoid repeat touch detection
  }

  

#endif

drawScreen_list(forceredraw);
  

  if (OldTime[0]>m || OldTime[0]+1000 <= m) {
    OldTime[0] = m;
    //do stuff every second


    uint32_t b = m/1000;
    
    tft.setTextColor(FG_COLOR,BG_COLOR);

    tft.setTextFont(SMALLFONT);
    tft.setCursor(0,440);
        
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
      }
    }
    tft.print("         ");
      

    tft.setCursor(0,Ypos);
  }
      
  if (OldTime[1]>m || OldTime[1]+60000 <= m) {
    OldTime[1]=m;
    drawScreen_list(true);
  }

  


}





void drawScreen_list(bool forceredraw) {
  if (NEWDATA==false && forceredraw==false)  {
    return;
  }
  NEWDATA=false;
  Ypos=0;

  BG_COLOR = TFT_LIGHTGRAY;
  tft.clear(BG_COLOR);

  tft.setTextColor(TFT_BLACK,BG_COLOR);

  uint32_t m = millis();

  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  snprintf(tempbuf,60,"%i devices alive",countDev());
  Ypos = Ypos + tft.fontHeight(4)/2;
  tft.drawString(tempbuf, TFT_WIDTH / 2, Ypos);
  tft.setTextDatum(TL_DATUM);

  Ypos += tft.fontHeight(4)/2 + 6;
  tft.fillRect(0,Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
  Ypos += 4;

if (m<DRAWALT+5000) {
    tft.setTextFont(3);
    //draw alternate screen with info
    snprintf(tempbuf,60,"WiFi Connected: %s",(WiFi.status() == WL_CONNECTED) ? "T" : "F");
    tft.drawString(tempbuf, 0, Ypos);
    Ypos += tft.fontHeight(3) + 2;
    
    snprintf(tempbuf,60,"MY IP: %s",WiFi.localIP().toString().c_str());
    tft.drawString(tempbuf, 0, Ypos);
    Ypos += tft.fontHeight(3) + 2;

    snprintf(tempbuf,60,"MY MAC: %s",WiFi.macAddress().c_str());
    tft.drawString(tempbuf, 0, Ypos);
    Ypos += tft.fontHeight(3) + 2;


    return;
  }
//            id secago  acc  oled quiz   simlvl simscore 

  uint8_t splits[6] = {5,10,12,14,19,24};

  for (byte j=0;j<6;j++)  tft.fillRect(splits[j]*10,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);

  Ypos += 4;

  tft.setTextFont(SMALLFONT);
  tft.setTextColor(TFT_DARKGRAY,BG_COLOR);


    snprintf(tempbuf,60,"ID");
    tft.drawString(tempbuf, 0, Ypos);

    snprintf(tempbuf,60,"Time");
    tft.drawString(tempbuf, splits[0]*10+4, Ypos);

    snprintf(tempbuf,60,"ACC");
    tft.drawString(tempbuf, splits[1]*10+4, Ypos);

    snprintf(tempbuf,60,"LED");
    tft.drawString(tempbuf, splits[2]*10+4, Ypos);

    snprintf(tempbuf,60,"Quiz");
    tft.drawString(tempbuf, splits[3]*10+4, Ypos);

    snprintf(tempbuf,60,"Level");
    tft.drawString(tempbuf, splits[4]*10+4, Ypos);

    snprintf(tempbuf,60,"Score");
    tft.drawString(tempbuf, splits[5]*10+4, Ypos);

  tft.setTextColor(FG_COLOR,BG_COLOR);

  Ypos += tft.fontHeight(SMALLFONT) + 2;

  tft.setTextFont(MEDFONT); //

  for(byte i=0;i<SENSORNUM;i++) {
    //march through sensors and list by ID
    
    if (DEVICES[i].ID>0 && DEVICES[i].timeLogged>0 ) {
      snprintf(tempbuf,60,"%i",DEVICES[i].ID);
      tft.drawString(tempbuf, 2, Ypos);
      snprintf(tempbuf,60,"%i", int ((m - DEVICES[i].timeLogged)/1000));
      tft.drawString(tempbuf, splits[0]*10+2, Ypos);
      snprintf(tempbuf,60,(DEVICES[i].HASMPU>0) ? "+" : "-");
      tft.drawString(tempbuf, splits[1]*10+2, Ypos);
      snprintf(tempbuf,60,(DEVICES[i].HASOLED>0) ? "+" : "-");
      tft.drawString(tempbuf, splits[2]*10+2, Ypos);
      snprintf(tempbuf,60,"%i",DEVICES[i].QuizScore);
      tft.drawString(tempbuf, splits[3]*10+2, Ypos);
      snprintf(tempbuf,60,"%i",DEVICES[i].SimonLevel);
      tft.drawString(tempbuf, splits[4]*10+2, Ypos);
      snprintf(tempbuf,60,"%i",DEVICES[i].SimonScore);
      tft.drawString(tempbuf, splits[5]*10+2, Ypos);

      Ypos += tft.fontHeight(MEDFONT) + 2;
    }
  }

  
}



void handleRoot() {

  
  char tempchar[9] = "";

  String currentLine = "<!DOCTYPE html><html><head><title>Pleasant Sensor Server LIST</title></head><body><p>";

  for (byte j=0;j<SENSORNUM;j++)  {
      currentLine += (String) DEVICES[j].ID + "<br>";
      currentLine += (String) DEVICES[j].HASBMP + "<br>";
      currentLine += (String) DEVICES[j].HASMPU + "<br>";
      currentLine += (String) DEVICES[j].HASOLED + "<br>";
      currentLine += (String) DEVICES[j].QuizScore + "<br>";
      currentLine += (String) DEVICES[j].SimonLevel + "<br>";
      currentLine += (String) DEVICES[j].SimonScore + "<br>";
      currentLine += (String) DEVICES[j].timeLogged + "<br>";
      currentLine += (String) DEVICES[j].UserName + "<br>";
      currentLine += "<br>";      
  }
 
 
currentLine += "</p></body></html>";
  
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

  for (byte k=0;k<server.args();k++) {   

      #ifdef _DEBUG
      Serial.print("Received this argname/arg: ");
      Serial.printf("%s --> %s\n",server.argName(k),server.arg(k).c_str());
      #endif


      if ((String)server.argName(k) == (String)"ID")  S.ID = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"HASOLED")  S.HASOLED = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"HASBMP")  S.HASBMP = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"HASMPU")  S.HASMPU = server.arg(k).toInt();
      
      if ((String)server.argName(k) == (String)"QuizScore")  S.QuizScore = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"SimonLevel")  S.SimonLevel = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"SimonScore")  S.SimonScore = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"UserName")  snprintf(S.UserName,11,"%s", server.arg(k).c_str());
      
  }
  

          #ifdef _DEBUG
          Serial.println("handlePOST parsed input.");
        #endif

  S.timeLogged = millis(); //time logged by me is when I received this.


  
        #ifdef _DEBUG
          Serial.println("handlePOST about to enter finddev.");
        #endif
    int16_t sn = findDev(&S,false);

        #ifdef _DEBUG
          Serial.printf("Finddev returned %i\n",sn);
        #endif

  if (sn<0 || sn >= SENSORNUM) {
    sn = findEmptyDevSlot();
    if (sn<0 || sn >= SENSORNUM) return; //failed 

  }; //not registered


  DEVICES[sn] = S;
  NEWDATA  = true;

        #ifdef _DEBUG
          Serial.println("handlePOST assigned val.");
        #endif

  server.send(200, "text/plain", "Received Data"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request

}

