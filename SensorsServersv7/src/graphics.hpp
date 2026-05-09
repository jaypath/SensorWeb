#if defined(_USETFT) && !defined(_ISPERIPHERAL)
#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP


#include <Arduino.h>
#include <SPI.h>
#include <LovyanGFX.hpp>

// Forward declarations - avoid circular includes since globals.hpp includes this file
struct STRUCT_CORE;
struct STRUCT_PrefsH;
struct ArborysDevType;
struct ArborysSnsType;
class Devices_Sensors;
class WeatherInfoOptimized;




#ifdef _USEGSHEET
struct STRUCT_GOOGLESHEET;
#endif


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
bool initGraphics();
uint16_t set_color(byte r, byte g, byte b);
uint16_t invert_color(uint16_t color);
uint32_t setFont(uint8_t FNTSZ);
uint16_t temp2color(int temp, bool invertgray = false);
uint16_t convertColor565ToGrayscale(uint16_t color) ;

// Drawing functions
void drawBmp(const char *filename, int16_t x, int16_t y, LGFX_Sprite *sprite = nullptr);
//void drawBmp(const char* filename, int16_t x, int16_t y, uint16_t alpha = TRANSPARENT_COLOR);
void drawBox(int16_t sensorIndex, int X, int Y, byte boxsize_x, byte boxsize_y);
byte fcnGetAlarms(int32_t whichSensors=-1, byte rows=0, byte cols=0);

// Text drawing functions
void fcnPrintTxtCenter(String msg, byte FNTSZ, int x=-1, int y=-1, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR, LGFX_Device* target = nullptr);
//void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR);
void fcnPrintTxtCenter(int msg,byte FNTSZ, int x=-1, int y=-1, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR);
void fcnPrintTxtColor(int value, byte FNTSZ, int x = -1, int y = -1, bool autocontrast = false);
void fcnPrintTxtColor2(int value1, int value2, byte FNTSZ, int x = -1, int y = -1, bool autocontrast = false);
void fcnPrintTxtHeatingCooling(int x, int y);

// Screen drawing functions
void fcnDrawScreen();
void fcnDrawMainScreen();
void fcnDrawHeader();
void fcnDrawClock();
void fcnDrawCurrentWeatherIconOrAlert();
void fcnDrawFutureWeather();
void fcnDrawSensors(int X,int Y, uint8_t rows=0, uint8_t cols=0);
void fcnDrawCurrentWeatherText();
void fcnDrawStatus();
void fcnDrawAlertScreen();
void fcnDrawSensorScreen();
void fcnDrawSensorInfo();
void fcnDrawDailyWeather();
void fcnDrawHourlyWeather();
void fcnDrawTextBox(String text, int16_t X, int16_t Y, int16_t width, int16_t height, int16_t datum, uint16_t color, bool textwrap);
void fcnDrawWeatherAlerts(int16_t X, int16_t Y, int16_t width, int16_t height);
// Weather text functions
void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg);
void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg);
void fcnDrawWeatherAlertsOnMainScreen(int16_t X, int16_t Y, int16_t width, int16_t height);

// Setup display functions
void checkTouchScreen();
void fcnDrawNavButtons(const char* buttonText[6]);
void fcnCheckGraphicsTimers();
void updateGraphics();
void fcnHandleTouch();

//sprite functions
bool fcnDrawWeatherSprite180(uint16_t X, uint16_t Y, LGFX_Sprite &sprite);
bool fillSprite180(int16_t X, int16_t Y, LGFX_Sprite &sprite);


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

  //enumerate the screens
  typedef enum {
    SCREEN_MAIN,
    SCREEN_ALERT,
    SCREEN_HOURLY,
    SCREEN_DAILY,
    SCREEN_SENSORS,  
    SCREEN_STATUS
  } SCREENS;
  
  struct SECOND_TIMER_STRUCT {
    time_t lastSecond=0;
    
    uint8_t Timers[10]={0}; //each screen has 10 possible elements [0-9]
    uint8_t Timer0_RESET[10]={255,15,30,120,120,180,60,0,0,0,0}; //main screen defaults, note that element 0 is the entire screen timer (global redraw)
    uint8_t TimerN_RESET=30; //other screens defaults - only 1 element on those screens

    bool decimateTimers(uint32_t currentTime, uint8_t currentSecond) {
      if (currentTime == this->lastSecond) return false;
      this->lastSecond = currentTime;

      if (Timers[0] > 0) Timers[0]--;
      
      if (Timers[0] == 0) {
        for (int i=1; i<10; i++) {
          Timers[i] = 0;
        }
      } else {
        for (int i=0; i<10; i++) {
          if (Timers[i] > 0) Timers[i]--;
        }
      }


      return true;
    }
  
    void zeroTimers() {
      for (int i=0; i<10; i++) {
        Timers[i] = 0;
      }
    }
  };
  
  
  struct SCREEN_ELEMENTS {
    uint16_t X; //upper left corner X position
    uint16_t Y; //upper left corner Y position
    uint16_t W; //width
    uint16_t H; //height
  };

  struct SPRITE_DETAILS {
    uint8_t X;
    uint16_t Y; //upper left corner Y position
    uint16_t W; //width
    uint16_t H; //height
    char filename[50]; //filename of the sprite bmp
  };

  struct STRUCT_GRAPHICS {
    //define the screen elements for each screen that has adjustable/reusable elements

    SCREEN_ELEMENTS SCREEN_0[6] = {{0,0,320,30},{0,32,180,180},{181,32,139,180},{0,214,320,60},{0,276,320,100},{0,377,320,103}}; //main screen elements
    SCREEN_ELEMENTS SCREEN_N_NAVBUTTONS = {0,400,320,80};
    SPRITE_DETAILS SPRITE_180x180 = {0,0,0,0,0}; //where does the sprite currently live? X,Y,W,H,filename where X,Y are the upper left corner coordinates, W and H are used width and height (normally 180x180) and filename is the filename of the sprite bmp
    SPRITE_DETAILS SPRITE_320x100 = {0,0,0,0,0}; //where does the sprite currently live? X,Y,W,H,filename where X,Y are the upper left corner coordinates, W and H are used width and height (normally 320x100) and filename is the filename of the sprite bmp
  
    SECOND_TIMER_STRUCT GRAPHICS_TIMERS;
  
    byte buttonWidth = 50;
    byte buttonInteriorMargin=2;
  
    uint8_t IconSet=0; //using this icon set
    uint8_t FlagsAndAlerts=0; //bit 0 - there are flagged sensors, bit 1 - there are weather alerts, bit 2 - I showed flags last, bit 3 - show icon next
//if bits 0 and 1 are set, then alternate icon, flags, and alerts. keeping track of this... if bit 3 is one then icon is next. Otherwise, if bit 2 is set, then alerts are next.

    uint8_t IntervalHourlyWeatherDisplay = 2; //hours between daily weather display
    uint8_t IntervalSecondsFlags = 5; //seconds between flag switch
  
    int16_t touchX;
    int16_t touchY;
    uint8_t alarmIndex;
    SCREENS Screen_Now;
    SCREENS Screen_Next;
    int16_t SubScreen_Now;
    int16_t SubScreen_Next;
    
    bool clearScreenArea (uint8_t screen, uint8_t element) {
      if (screen == 0) { //main screen
        tft.fillRect(SCREEN_0[element].X,SCREEN_0[element].Y,SCREEN_0[element].W,SCREEN_0[element].H,BG_COLOR);
        GRAPHICS.GRAPHICS_TIMERS.Timers[element]=0;
        if (element == 0) {
          tft.setTextColor(FG_COLOR,BG_COLOR);
          tft.setCursor(0,0);
        }      
      } else {
        tft.fillRect(0,0,320,480,BG_COLOR);
      }
      return true;
    }
   
  };
  
  





//__________________________Graphics



#endif 
#endif