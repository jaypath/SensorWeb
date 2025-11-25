#ifdef _USESSD1306

#ifndef SSD1306_GRAPHICS_HPP
#define SSD1306_GRAPHICS_HPP


#include "sensors.hpp"
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>

#define RST_PIN -1
#define I2C_OLED 0x3C
#define _OLEDTYPE &Adafruit128x64

extern SSD1306AsciiWire oled;

void redrawOled();
void invertOled();
void initOled();




#endif
#endif