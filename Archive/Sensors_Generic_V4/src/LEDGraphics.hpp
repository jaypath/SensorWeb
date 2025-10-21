#ifndef LEDGRAPHICS_HPP
#define LEDGRAPHICS_HPP

#include <FastLED.h>

#define LEDCOUNT 57 //the number of LEDs
extern CRGB LEDARRAY[LEDCOUNT]; //the array of LEDs
extern Animation_type LEDs; //the animation type


void LED_animation_defaults(uint8_t style); //set the default animation style
uint32_t LED_choose_color(uint8_t brightness); //choose a color between 1 and 2 (and sets brightness)
uint32_t LED_scale_brightness(uint32_t color, uint8_t brightness); //scale the brightness of a color
uint32_t LED_setLED(byte j, double L1, double T1, int8_t DIR, uint32_t m); //set the color and brightness of an LED
void LED_update(void); //update the LEDs
void LED_set_color(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2); //set the color of the LEDs



#endif