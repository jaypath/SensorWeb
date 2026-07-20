#if defined(_USETFT) && _IS_SERVER_HUB
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

#define MAXSCREENELEMENTS 8
#define HEADER_INFO_ALERT_MAXLEN 48
#define HEADER_INFO_ALERT_TTL_SEC 300 // acute header messages auto-clear after 5 minutes


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


  //enumerate the screens
  typedef enum {
    SCREEN_NONE, //no screen selected
    SCREEN_MAIN,
    SCREEN_ALERT,
    SCREEN_HOURLY,
    SCREEN_DAILY,
    SCREEN_SENSORS,  
    SCREEN_STATUS
  } SCREENS;

// Graphics implementation (graphics.cpp) — declarations for other translation units
bool initGraphics();
uint16_t set_color(byte r, byte g, byte b);
uint16_t invert_color(uint16_t color);
uint16_t convertColor565ToGrayscale(uint16_t color);
uint32_t setFont(uint8_t FNTSZ);
uint16_t temp2color(int temp, bool invertgray = false);
int8_t drawBmp(const char *filename, int16_t x, int16_t y, LGFX_Sprite *sprite = nullptr);
void drawBox(int16_t sensorIndex, int X, int Y, byte boxsize_x, byte boxsize_y);
void fcnPrintTxtCenter(int msg, byte FNTSZ, int x, int y, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR, LGFX_Device *target = nullptr);
void fcnPrintTxtCenter(String msg, byte FNTSZ, int x, int y, uint16_t color1=FG_COLOR, uint16_t color2=FG_COLOR, uint16_t bgcolor=BG_COLOR, LGFX_Device *target = nullptr);
void fcnPrintTxtHeatingCooling(int X, int Y);
void fcnPrintTxtColor2(int value1, int value2, byte FNTSZ, int x, int y, bool autocontrast);
void fcnPrintTxtColor(int value, byte FNTSZ, int x, int y, bool autocontrast);
byte fcnGetAlarms(uint16_t sensorTypes, uint8_t checkTheseFlags,uint8_t sensorFlags,  byte rows, byte cols, bool useOverrideFlags = true);
void fcnDrawSensors(int X, int Y, uint8_t rows = 0, uint8_t cols = 0);
void fcnPressureTxt(char *tempPres, uint16_t *fg, uint16_t *bg);
void fcnPredictionTxt(char *tempPred, uint16_t *fg, uint16_t *bg);
void updateGraphics();
void fcnDrawScreen();
void loadFunctionsForScreen();
void fcnSwitchScreen(int16_t index);
void fcnSwitchToNewScreen(SCREENS newScreen = SCREEN_MAIN);
void fcnSwitchSubScreen(int16_t index);
int8_t helper_findIndex(void (*drawFunction)(int16_t));
int16_t helper_runScreenElementFunction(int16_t index, bool failOnTimeLeft);
void fcnDrawNavButtons(int16_t index);
void fcnDrawAlertScreen(int16_t index);
void fcnDrawAlertText(int16_t index);
void fcnDrawMainScreen(int16_t index);
void fcnDrawHeader(int16_t index);
void fcnDrawHeaderInfo(int16_t index);
#define _USE_HEADER_INFO_ALERT 1

/** Show an acute right-header message (or clear with empty string) and redraw immediately. ttlSec defaults to 5 minutes; 0 = hold until cleared. */
void HeaderInfoAlert(const char* msg, uint16_t fgColor = TFT_RED, uint16_t bgColor = TFT_BLACK, uint32_t ttlSec = HEADER_INFO_ALERT_TTL_SEC);
void HeaderInfoAlert(const String& msg, uint16_t fgColor = TFT_RED, uint16_t bgColor = TFT_BLACK, uint32_t ttlSec = HEADER_INFO_ALERT_TTL_SEC);

/** RAII helper: shows HeaderInfoAlert on construct, clears it on destroy (all return paths). */
class HeaderInfoAlertGuard {
public:
  explicit HeaderInfoAlertGuard(const char* msg,
      uint16_t fgColor = TFT_YELLOW,
      uint16_t bgColor = TFT_BLACK,
      uint32_t ttlSec = HEADER_INFO_ALERT_TTL_SEC)
    : active_(msg != nullptr && msg[0] != '\0') {
    if (active_) HeaderInfoAlert(msg, fgColor, bgColor, ttlSec);
  }
  ~HeaderInfoAlertGuard() {
    if (active_) HeaderInfoAlert("");
  }
  HeaderInfoAlertGuard(const HeaderInfoAlertGuard&) = delete;
  HeaderInfoAlertGuard& operator=(const HeaderInfoAlertGuard&) = delete;
private:
  bool active_;
};

void fcnDrawClock(int16_t index);
void fcnDrawCurrentWeatherText(int16_t index);
void fcnDrawCurrentWeatherIconOrAlert(int16_t index);
bool fcnDrawWeatherSprite180(uint16_t X, uint16_t Y, LGFX_Sprite &sprite, bool useAlerts = false, uint8_t daysfromnow = 0);
bool fillSprite180(int16_t X, int16_t Y, LGFX_Sprite &sprite, bool useAlerts = false, uint8_t daysfromnow = 0);
void fcnDrawValuePlot(int16_t plotX, int16_t plotY, int16_t plotW, int16_t plotH,
  const int8_t *values, uint8_t count, int16_t yMin, int16_t yMax,
  uint16_t lineColor, bool colorDotsByValue, uint8_t lineWidth = 1);
void fcnTouchWeatherForecast(int16_t index);
void fcnTouchDailyDetailNav(int16_t index);
void fcnDrawHourlyWeather(int16_t index);
void fcnDrawDailyWeather(int16_t index);
void fcnDrawDailyDetailScreen(int16_t index);
void fcnDrawDailyDetailIcon(int16_t index);
void fcnDrawDailyDetailText(int16_t index);
void fcnDrawDailyDetailPlots(int16_t index);
void fcnDrawDailyDetailNavBar(int16_t index);
void fcnDrawSensorScreen(int16_t index);
void fcnConvertTouchToSensorSubscreen(int16_t index);
void fcnDrawSensorDelSnsConfirm(int16_t index);
void fcnDrawSensorDelSns(int16_t index);
void fcnDrawStatusWeatherUpdate(int16_t index);
void fcnDrawSensorDetailsSubscreen(int16_t index);
void fcnDrawSensorSummarySubcreen(int16_t index);
void fcnDrawSensorBroadcast(int16_t index);
void fcnDrawStatusScreen(int16_t index);
void fcnDrawStatusDelCoreConfirm(int16_t index);
void fcnDrawStatusDelCore(int16_t index);
void fcnDrawStatusReboot(int16_t index);
void fcnDrawStatusResetWiFiConfirm(int16_t index);
void fcnDrawStatusResetWiFi(int16_t index);
void fcnDrawStatusText(int16_t index);
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

extern LGFX tft;

  
  struct SECOND_TIMER_STRUCT {
    uint8_t lastSecond=0;
    
    uint8_t Timers[10]={0}; //each screen has 10 possible elements [0-9]
    
    bool decimateTimers(uint8_t currentSecond) {
      if (currentSecond == this->lastSecond) return false;
      this->lastSecond = currentSecond;


      for (int i=0; i<10; i++) {
       if (Timers[i] > 0) Timers[i]--;
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
    void (*drawFunction)(int16_t); //pointer to the draw function for each element. takes the index of the element as an argument
    uint16_t X; //upper left corner X position
    uint16_t Y; //upper left corner Y position
    uint16_t W; //width
    uint16_t H; //height
    SCREENS Screen_Next; //the next screen to show if this element is touched
    int16_t Local_Code; //Local code for this element. May store subscreen data, current icon set, etc.
    uint8_t TimerIndex; //timer index for this element
    uint8_t Timer_RESET; //timer reset value for this element
    uint8_t ScreenElement; //index of this element in the SCREEN_DATA array. 255 = erased
    void (*RunFcnOnTouch)(int16_t); //pointer to a function to run the next time this element is loaded
    

    void loadScreenElements(void (*drawFunction)(int16_t), uint16_t X, uint16_t Y, uint16_t W, uint16_t H, SCREENS Screen_Next, int16_t Local_Code, uint8_t TimerIndex, uint8_t Timer_RESET, uint8_t ScreenElement, void (*RunFcnOnTouch)(int16_t) = nullptr) {
      this->drawFunction = drawFunction;
      this->X = X;
      this->Y = Y;
      this->W = W;
      this->H = H;
      this->Screen_Next = Screen_Next;
      this->Local_Code = Local_Code;
      this->TimerIndex = TimerIndex;
      this->Timer_RESET = Timer_RESET;
      this->ScreenElement = ScreenElement; //this allows the draw functions to be agnostic to their order
      this->RunFcnOnTouch = RunFcnOnTouch;
    }

    void clearScreenElementArea() {
      tft.fillRect(this->X,this->Y,this->W,this->H,BG_COLOR);
    }

    void initScreenElement() {
      this->drawFunction = nullptr;
      this->X = 0;
      this->Y = 0;
      this->W = 0;
      this->H = 0;
      this->Screen_Next = SCREEN_NONE;
      this->Local_Code = 0;
      this->TimerIndex = 0;
      this->Timer_RESET = 0;
      this->ScreenElement = 255;
      this->RunFcnOnTouch = nullptr;
    }
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

    SCREEN_ELEMENTS SCREEN_DATA[MAXSCREENELEMENTS];
    SPRITE_DETAILS SPRITE_180x180 = {0,0,0,0,0}; //where does the sprite currently live? X,Y,W,H,filename where X,Y are the upper left corner coordinates, W and H are used width and height (normally 180x180) and filename is the filename of the sprite bmp
    
    SECOND_TIMER_STRUCT GRAPHICS_TIMERS;
  
    byte buttonWidth = 50;
    byte buttonInteriorMargin=2;
  
    uint8_t IconSet=0; //using this icon set
    uint8_t StatusFlags=0; //bit 0 - sensors currently flagged (ack for sensor flags), bit 1 = weather alert flag ack. These are set and stay until main flag is cleared.
    uint8_t StatusFlagsChanged=0; //bit 0 - sensors changed, bit 1 = weather alert flag changed, bit 2 = HVAC changed. If any action is taken based on these, they will be cleared.

    // Acute right-header banner (HeaderInfoAlert). Non-empty text overrides IP/dawn cycling until cleared or TTL expires.
    char headerInfoAlert[HEADER_INFO_ALERT_MAXLEN] = {0};
    uint16_t headerInfoAlertFg = TFT_RED;
    uint16_t headerInfoAlertBg = TFT_BLACK;
    uint32_t headerInfoAlertStartMs = 0;
    uint32_t headerInfoAlertTtlMs = 0; // 0 = inactive / no timed expiry
    
    uint8_t IntervalHourlyWeatherDisplay = 2; //hours between daily weather display
    
    int16_t touchX;
    int16_t touchY;
    uint8_t alarmIndex;
    uint8_t alarmCount;
    SCREENS Screen_Now;
    SCREENS Screen_Next;
    int16_t SubScreen_Now;
    int16_t SubScreen_Next;

    int16_t miscData; //misc data for the current screen

    bool clearScreenArea (uint8_t element) {
      SCREEN_DATA[element].clearScreenElementArea();
      this->GRAPHICS_TIMERS.Timers[element]=0;
      return true;
    }

    void initRemainingScreenElements(byte elementStart) {
      for (int i=elementStart; i<MAXSCREENELEMENTS; i++) SCREEN_DATA[i].initScreenElement();      
    }

    void zeroChildTimers() {
      for (int i = 1; i < 10; i++) GRAPHICS_TIMERS.Timers[i] = 0;
    }
   
  };
  
  





//__________________________Graphics



#endif 
#endif