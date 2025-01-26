#ifndef GLOBALS_HPP
#define GLOBALS_HPP
//#define _DEBUG 0
//#define _WEBDEBUG 0


//automatically detect arduino type
#if defined (ARDUINO_ARCH_ESP8266)
  #define _USE8266 1
  #define _ADCRATE 1023
#elif defined(ESP32)
  #define _USE32
  #define _ADCRATE 4095
#else
  #error Arduino architecture unrecognized by this code.
#endif




#define SENSORNUM 60

#include <Arduino.h>
#include <String.h>
#include <SPI.h>

#define LGFX_USE_V1         // set to use new version of library
#include <LovyanGFX.hpp>    // main library

//wifi
#define WIFI_SSID "CoronaRadiata_Guest"
#define WIFI_PASSWORD "snakesquirrel"
//------------------

//__________________________Graphics

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

//__________________________Graphics



#define SENSORNUM 60
//#define FG_COLOR  (uint16_t) TFT_BLACK //Foreground color
//#define BG_COLOR  (uint16_t) TFT_LIGHTGREY //light gray

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
  uint16_t SendingInt;
  uint8_t localFlags; //RMB to LMB... 0= issent, 1=isexpired, 2= 
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value  
};

struct TKiyaansBetter {
  uint16_t BG_COLOR;
    uint16_t FG_COLOR;
};
struct ScreenFlags {
    // So we can have initializers and thiscan still bein a union
    ScreenFlags();

    uint16_t BG_COLOR = TFT_BLACK;
    uint16_t FG_COLOR = TFT_LIGHTGRAY;
    uint8_t BG_luminance=0;

    uint8_t localTempIndex; //index of outside sensor
    int8_t localTemp = -100; //based on local weather
    uint32_t localTempTime=0; //last time the current condition was updated (this comes from a local sensor)
    uint32_t sunset,sunrise;
    uint32_t lastWeatherUpdate=0; //last time weather was updated

    uint8_t RedrawClock = 0; //seconds left before redraw
    uint16_t RedrawWeather = 0; //seconds left before redraw
    uint8_t RedrawList=60; //SECONDS left for this screen (then  goes to main)
    uint8_t WeatherTime = 5; //MINUTES between weather redraws    

    uint8_t ForceRedraw=0; //redraw the screen now to screennum, regardless of timing
    
    uint8_t ScreenNum=0; //screens: main with clock and weather --> list with buttons on bottom (scrolls N pages to get all of them) --> main
    uint8_t settingsLine=0; //index to  line  selected on subscreens
    uint8_t settingsSelectable[10] = {3,4,5,8,9,13,14,99,99,99};
    int8_t settingsSelected=0;
    uint8_t snsLastDisplayed=0; //index to the last sensor displayed 
    uint32_t snsListLastTime=0;
    uint8_t snsListArray[2][SENSORNUM]; //index to the last sensor displayed 


    uint8_t showAlarmedOnly = 0; //in list mode, show only alarmed sns?

    uint8_t HourlyInterval = 2; //hours between daily weather display
    long DSTOFFSET;

    uint8_t isExpired = 0; //#expired
    uint8_t isFlagged=0; //#flagged
    uint8_t isCritical=0; //#flagged
    uint8_t isHeat=0; //first bit is heat on, bits 1-6 are zones
    uint8_t isAC=0; //first bit is compressor on, bits 1-6 are zones
    uint8_t isFan=0; //first bit is fan on, bits 1-6 are zones
    uint8_t isHot;
    uint8_t isCold;
    uint8_t isSoilDry;
    uint8_t isLeak;


    uint32_t lastUploadTime = 0;
    bool lastUploadSuccess = false;
    uint8_t uploadFailCount = 0;
    uint16_t uploadInterval = 20; //MINUTES between uploads
    uint32_t LastServerUpdate = 0; //last time data was sent to main server
    //do not keep strings in this variable!
    //String GsheetID= ""; //file ID for this month's spreadsheet
    //String GsheetName= ""; //file name for this month's spreadsheet
    //String lastError;
    uint32_t lastErrorTime=0;

};


    

const     uint8_t temp_colors[104] = {
    20, 255, 0, 255, //20
    24, 200, 0, 200, //21 - 24
    27, 250, 150, 150, //25 - 27
    29, 250, 200, 200, //28 - 29
    32, 255, 255, 255, //30 - 32
    34, 150, 150, 255, //33 - 34
    37, 50, 50, 200, //35 - 37
    39, 50, 100, 200, //38 - 39
    42, 0, 125, 210, //40 - 42
    44, 0, 150, 150, //43 - 44
    47, 0, 200, 150, //45 - 47
    51, 0, 200, 100, //48 - 51
    54, 0, 150, 100, //52 - 54
    59, 0, 100, 100, //55 - 59
    64, 0, 120, 60, //60 - 64
    69, 0, 150, 0, //65 - 69
    72, 10, 175, 0, //70 - 72
    75, 20, 200, 0, //73 - 75
    79, 67, 220, 0, //76 - 79
    82, 100, 220, 0, //80 - 82
    84, 150, 200, 0, //83 - 84
    87, 200, 100, 50, //85 - 87
    89, 200, 100, 100, //88 - 89
    92, 220, 50, 50, //90 - 92
    94, 240, 50, 50, //93 - 94
    100, 250, 0, 0 //95 - 100
    };



//this server
#ifdef _USE8266
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>

  extern ESP8266WebServer server;
#endif
#ifdef _USE32
  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>
  extern WebServer server;
#endif


#endif