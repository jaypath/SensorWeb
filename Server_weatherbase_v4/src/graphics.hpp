#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP


#include <Arduino.h>
#include <SPI.h>
#include <LovyanGFX.hpp>
#include "globals.hpp"
#include "utility.hpp"
#include "Devices.hpp"
#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include "SDCard.hpp"

#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
#endif

// drawing
#define NUMSCREEN 2

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



// Graphics utility functions
uint16_t set_color(byte r, byte g, byte b);
uint32_t setFont(uint8_t FNTSZ);
uint16_t temp2color(int temp, bool invertgray = false);

// Drawing functions
void drawBmp(const char* filename, int16_t x, int16_t y, uint16_t alpha = TRANSPARENT_COLOR);
void drawBox(int16_t sensorIndex, int X, int Y, byte boxsize_x, byte boxsize_y);

// Text drawing functions
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR);
void fcnPrintTxtCenter(int msg,byte FNTSZ, int x=-1, int y=-1, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR);
void fcnPrintTxtColor(int value, byte FNTSZ, int x = -1, int y = -1, bool autocontrast = false);
void fcnPrintTxtColor2(int value1, int value2, byte FNTSZ, int x = -1, int y = -1, bool autocontrast = false);
void fcnPrintTxtHeatingCooling(int x, int y);

// Screen drawing functions
void fcnDrawScreen();
void fcnDrawHeader();
void fcnDrawClock();
void fcnDrawCurrentWeather();
void fcnDrawFutureWeather();
void fcnDrawSensors(int X,int Y, uint8_t rows=0, uint8_t cols=0, int32_t whichSensors = -1);
void fncDrawCurrentCondition();
void fcnDrawStatus();

// Weather text functions
void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg);
void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg);

// WiFi keypad functions
void drawKeyPad4WiFi(uint32_t y, uint8_t keyPage, uint8_t WiFiSet);
bool isTouchKey(int16_t* keyval, uint8_t* keypage);

// Setup display functions
void displaySetupProgress(bool success = true);
void displayWiFiStatus(byte retries, bool success);
void displayOTAProgress(unsigned int progress, unsigned int total);
void displayOTAError(int error);
void screenWiFiDown();
void checkTouchScreen();

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



#endif 