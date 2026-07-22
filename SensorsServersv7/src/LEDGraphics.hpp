#ifdef _USELED
#ifndef LEDGRAPHICS_HPP
#define LEDGRAPHICS_HPP

#include <FastLED.h>

extern CRGB LEDARRAY[_USELED_SIZE]; //the array of LEDs

// Animation class for LED control
class Animation_type {
public:
    uint8_t  animation_style; //1 = counter clockwise rotation (sin) // 2 = clockwise rotation (sin) // 3 = non-moving pulse // 4 = static
    uint16_t sin_T; //in other words, T, ms to move through one wavelength
    uint8_t sin_L; //wavelength, in number of LEDs
    uint8_t MaxBrightness; //sin amp
    uint8_t MinBrightness; //sin amp
    uint32_t last_LED_update;
    uint32_t LEDredrawRefreshMS;
    CRGB color1; //current color start
    CRGB color2; //current color end //color will be a random uniformly distributed value between color 1 and 2
    uint32_t  LED_TIMING[_USELED_SIZE]; //keeps track of the time of each LED in animations where each LED is in a different cycle (like sparkling gaussians, type 5)
    uint32_t  LED_BASE[_USELED_SIZE]; // base color per LED (random between color1/color2). Cleared only on init; sensor updates leave in-flight cycles alone so old colors fade out naturally.
    
    void LED_animation_defaults(uint8_t style, bool resetState = false);
    uint32_t LED_choose_color(uint8_t brightness);
    uint32_t LED_scale_brightness(CRGB colorin, uint8_t brightness);
    uint32_t LED_setLED(byte j, double L1, double T1, int8_t DIR, uint32_t m);
    void LED_update(void);
    void LED_set_color(uint32_t color1, uint32_t color2);
    void LED_set_color(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);
    void LED_set_color_soil(struct ArborysSnsType *sns);
    byte LED_scale_color(byte c1, byte c2);
    void LED_choose_animation_style(String style);
};

extern Animation_type LEDs; //the animation type
void initLEDs(void);
#endif
#endif