#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP


#include "GLOBALS.hpp"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>


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


static LGFX tft;



#endif