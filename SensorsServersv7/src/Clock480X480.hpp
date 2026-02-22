#ifdef _ISCLOCK480X480
#ifndef CLOCK480X480_HPP
#define CLOCK480X480_HPP

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>
#include "Devices.hpp"
#include "server.hpp"
#include "globals.hpp"
#include <SD.h>



//#define _LIGHTMODE
#ifdef _LIGHTMODE
const uint16_t FG_COLOR = TFT_BLACK; //Foreground color
const uint16_t BG_COLOR = TFT_LIGHTGREY; //light gray = 211,211,211
//const uint16_t TRANSPARENT_COLOR = 0xffff; //white
#else
const uint16_t FG_COLOR = TFT_WHITE; //Foreground color
const uint16_t BG_COLOR = TFT_BLACK; //light gray = 211,211,211
//const uint16_t TRANSPARENT_COLOR = 0; //black
#endif



class LGFX : public lgfx::LGFX_Device
{
public:
	lgfx::Bus_RGB _bus_instance;
	lgfx::Panel_ST7701_guition_esp32_4848S040 _panel_instance;
	lgfx::Touch_GT911 _touch_instance;
	lgfx::Light_PWM _light_instance;

	LGFX(void)
	{
		{
			auto cfg = _panel_instance.config();

			cfg.memory_width  = 480;
			cfg.memory_height = 480;
			cfg.panel_width  = 480;
			cfg.panel_height = 480;

			cfg.offset_x = 0;
			cfg.offset_y = 0;
			// If everything has a reddish (or blueish) hue, try toggling rgb_order: panel R/B swap
			//cfg.rgb_order = true;

			_panel_instance.config(cfg);
		}

		{
			auto cfg = _panel_instance.config_detail();

			cfg.pin_cs = GPIO_NUM_39;
			cfg.pin_sclk = GPIO_NUM_48;
			cfg.pin_mosi = GPIO_NUM_47;

			_panel_instance.config_detail(cfg);
		}

		{
			auto cfg = _bus_instance.config();
			cfg.panel = &_panel_instance;
			cfg.pin_d0 = GPIO_NUM_4;  // B0
			cfg.pin_d1 = GPIO_NUM_5;  // B1
			cfg.pin_d2 = GPIO_NUM_6;  // B2
			cfg.pin_d3 = GPIO_NUM_7;  // B3
			cfg.pin_d4 = GPIO_NUM_15;   // B4

			cfg.pin_d5 = GPIO_NUM_8;   // G0
			cfg.pin_d6 = GPIO_NUM_20;  // G1
			cfg.pin_d7 = GPIO_NUM_3;   // G2
			cfg.pin_d8 = GPIO_NUM_46;  // G3
			cfg.pin_d9 = GPIO_NUM_9;   // G4
			cfg.pin_d10 = GPIO_NUM_10; // G5

			cfg.pin_d11 = GPIO_NUM_11;  // R0
			cfg.pin_d12 = GPIO_NUM_12;  // R1
			cfg.pin_d13 = GPIO_NUM_13;  // R2
			cfg.pin_d14 = GPIO_NUM_14;  // R3
			cfg.pin_d15 = GPIO_NUM_0; // R4

			cfg.pin_henable = GPIO_NUM_18;
			cfg.pin_vsync = GPIO_NUM_17;
			cfg.pin_hsync = GPIO_NUM_16;
			cfg.pin_pclk = GPIO_NUM_21;
			cfg.freq_write  = 16000000;


			cfg.hsync_polarity = 0;
			cfg.hsync_front_porch = 10;
			cfg.hsync_pulse_width = 8;
			cfg.hsync_back_porch = 50;

			cfg.vsync_polarity = 0;
			cfg.vsync_front_porch = 10;
			cfg.vsync_pulse_width = 8;
			cfg.vsync_back_porch = 20;

			cfg.pclk_idle_high = 0;
			cfg.de_idle_high = 0;
			cfg.pclk_active_neg = 0;

			_bus_instance.config(cfg);
		}
		_panel_instance.setBus(&_bus_instance);

		{
			auto cfg = _touch_instance.config();
			cfg.x_min      = 0;
			cfg.x_max      = 480;
			cfg.y_min      = 0;
			cfg.y_max      = 480;
			cfg.bus_shared = false;
			cfg.offset_rotation = 0;			

			cfg.i2c_port   = I2C_NUM_0;

			cfg.pin_int    = GPIO_NUM_NC;
			cfg.pin_sda    = GPIO_NUM_19;
			cfg.pin_scl    = GPIO_NUM_45;
			cfg.pin_rst    = GPIO_NUM_NC;

			cfg.freq = 40000;
			_touch_instance.config(cfg);
			_panel_instance.setTouch(&_touch_instance);
		}

		{
			auto cfg = _light_instance.config();
			cfg.pin_bl = GPIO_NUM_38;
			_light_instance.config(cfg);
		}
		_panel_instance.light(&_light_instance);

		setPanel(&_panel_instance);
	}
};


struct Screen {
    uint8_t num_images;
    int8_t lastPic;
   
  
    uint16_t FG_COLOR;
    uint16_t BG_COLOR;

    int16_t lastX;
    int16_t lastY;


    uint32_t time_lastPic;
    uint32_t time_lastClock;

	uint32_t time_lastWeather;
    uint8_t int_Weather_MIN; //min
    int8_t wthr_currentTemp;
    uint16_t wthr_currentWeatherID;
    int8_t wthr_DailyHigh;
    int8_t wthr_DailyLow;
    int16_t wthr_DailyWeatherID;
    int16_t wthr_DailyPoP;
    uint32_t wthr_sunrise,wthr_sunset;

	uint8_t screenNum; //current screen number
	uint8_t oldScreenNum;
	uint32_t screenChangeTimer; //time spent on this screen
	uint8_t screenChangeTimerMax; //time to stay on a screen before going back to main screen (0)
	uint16_t touchX;
	uint16_t touchY;
	uint32_t touchDebounceMS;   // ignore touches for this many ms after handling one
	uint32_t lastTouchMS;      // millis() when last touch was processed
	uint32_t clockChangeTimer; //time spent on the clock screen
	uint8_t clockChangeTimerMax; //time to stay on the clock screen before going back to main screen (0)
	uint32_t lastclockChange; //last time the clock screen was changed

	bool relayStates[3] = {false, false, false}; // Track ON/OFF for Relays 1, 2, and 3
};

void initRelays();

byte fcnDrawClock();
void drawBmp(const char *filename, int16_t x, int16_t y,int32_t transparent=-1, uint8_t luminosity=100);
void fcnDrawPic(byte luminosity=0) ;
byte fcnDrawStatusScreen();
byte fcnDrawWeatherScreen();
void checkTouchScreen();
void fcnCheckScreen();
String weatherID2string(uint16_t weatherID);
void scaleColor(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t luminosity);
int countFilesInDirectory(String dirName);
String getNthFileName(String dirName, uint8_t filenum);
void initClock480X480Graphics();
bool getWeather();
void clockLoop();
uint16_t read16(File &f);
uint32_t read32(File &f);
String fcnMMM(time_t t, bool abb);
void tftGraphicsTest(String testType="", byte secondstoshow=5);

#endif //ifndef CLOCK480X480_HPP
#endif //ifdef _ISCLOCK480X480
