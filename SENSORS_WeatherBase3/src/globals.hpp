#ifndef GLOBALS_HPP
#define GLOBALS_HPP
//#define _DEBUG 0
//#define _WEBDEBUG 0



#define SENSORNUM 60

#include <Arduino.h>
#include <String.h>
#include "SPI.h"
#include <Weather.hpp>

#define LGFX_USE_V1         // set to use new version of library
#include <LovyanGFX.hpp>    // main library

//------------------


// drawing
#define NUMSCREEN 2

//weather
//wellesley, MA
#define LAT 42.30210392783453  //only 4 decimal pts
#define LON -71.29822225979105 //only 4 decimal pts allowed with NOAA


#define RESET_ENUM_TO_STRING(enum_val) (#enum_val)

//potential reset issues
typedef enum {
  RESET_DEFAULT, //reset due to some unknown fault, where the reset state could not be set (probably triggered by a hang and watchdog timer kicked)
  RESET_SD, //reset because of SD card ... this can't be saved so it's actually pointless :)
  RESET_WEATHER, //reset because couldn't get weather
  RESET_USER, //user reset
  RESET_OTA, //ota reset
  RESET_WIFI, //no wifi so reset
  RESET_TIME, //time based reset 
  RESET_UNKNOWN //unknown cause
  } RESETCAUSE;
  

struct Screen {
    RESETCAUSE resetInfo;
    time_t lastResetTime;
    byte rebootsSinceLast=0;
    time_t ALIVESINCE;
    uint8_t wifiFailCount;
    time_t currentTime;
    byte CLOCK_Y = 105;
    byte HEADER_Y = 30;

    uint32_t lastHeader=0;
    uint32_t lastWeather=0;
    uint32_t lastCurrentCondition=0;
    uint32_t lastClock=0; //last time clock was updated, whether flag or not
    uint32_t lastFlagView=0; //last time clock was updated, whether flag or not

    uint8_t HourlyInterval = 2; //hours between daily weather display
    uint8_t currentConditionTime = 10; //how many minutes to show current condition?
    uint8_t flagViewTime = 10; //how many seconds to show flag values?
    uint8_t weatherTime = 60; //how many MINUTES to show weather values?

    uint8_t ScreenNum;
    uint16_t touchX;
    uint16_t touchY;
    uint16_t line_clear;
    uint16_t line_keyboard;
    uint16_t line_submit;
    uint8_t alarmIndex;
    
    uint8_t isExpired = false; //are any critical sensors expired?
    uint8_t isFlagged=false;
    uint8_t wasFlagged=false;
    uint8_t isHeat=false; //first bit is heat on, bits 1-6 are zones
    uint8_t isAC=false; //first bit is compressor on, bits 1-6 are zones
    uint8_t isFan=false; //first bit is fan on, bits 1-6 are zones
    uint8_t wasHeat=false; //first bit is heat on, bits 1-6 are zones
    uint8_t wasAC=false; //first bit is compressor on, bits 1-6 are zones
    uint8_t wasFan=false; //first bit is fan on, bits 1-6 are zones

    uint8_t isHot;
    uint8_t isCold;
    uint8_t isSoilDry;
    uint8_t isLeak;
    uint8_t localWeather; //index of outside sensor

    int8_t currentTemp;
    int8_t Tmax;
    int8_t Tmin;

    char lastError[76];
    time_t lastErrorTime;

    time_t lastESPNOW;
};

//#define _LIGHTMODE
#ifdef _LIGHTMODE
  const uint16_t FG_COLOR = TFT_BLACK; //Foreground color
  const uint16_t BG_COLOR = TFT_LIGHTGREY; //light gray = 211,211,211
  const uint16_t TRANSPARENT_COLOR = 0xffff; //white
#else
  const uint16_t FG_COLOR = TFT_WHITE; //Foreground color
  const uint16_t BG_COLOR = TFT_BLACK; //light gray = 211,211,211
  const uint16_t TRANSPARENT_COLOR = 0; //black
#endif


//sensors
#define OLDESTSENSORHR 24 //hours before a sensor is removed

struct SensorVal {
  uint8_t MAC[6]; //mac address [v3 ID]
  uint8_t IP[4];
  uint8_t ardID; //legacy use from V1 and V2
  uint8_t  snsType ;
  uint8_t snsID;
  char snsName[32];
  double snsValue;
  uint32_t timeRead;
  uint32_t timeLogged;  
  uint32_t SendingInt;  
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = this is a critically monitored sensor, so alarm if I don't get a read on time!
  uint8_t expired; //set to 1 if this sensor is  expired by time
};


//server

struct IP_TYPE {
  IPAddress IP;
  int server_status;
};


struct WiFi_type {
  //IPAddress DHCP;  // 4 byte,   4 in total
  //IPAddress GATEWAY;// 4 bytes, 8 in total
  //IPAddress DNS; //4 bytes, 16 in total
  //IPAddress SUBNET;
  IPAddress MYIP; //4 bytes
  byte MAC[6];
  byte SSID[33];
  byte PWD[65];
  bool HAVECREDENTIALS;
};




//


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


#endif