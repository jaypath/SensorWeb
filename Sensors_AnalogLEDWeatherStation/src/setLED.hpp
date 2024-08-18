#ifndef SET_LED_HPP
#define SET_LED_HPP

#include "util.hpp"
#include <FastLED.h>

extern CRGB* top_led_strip;
extern uint top_strip_length;
extern CRGB* left_leds[COLUMN_COUNT_PER_SIDE];
extern CRGB* right_leds[COLUMN_COUNT_PER_SIDE];
extern uint left_strip_lengths[COLUMN_COUNT_PER_SIDE];
extern uint right_strip_lengths[COLUMN_COUNT_PER_SIDE];

enum led_side {
  LEFT,
  RIGHT
};

void init_led_info(
  CRGB* top_led_strip_,
  uint top_strip_length_,
  CRGB* left_leds_[COLUMN_COUNT_PER_SIDE],
  CRGB* right_leds_[COLUMN_COUNT_PER_SIDE],
  uint left_strip_lengths_[COLUMN_COUNT_PER_SIDE],
  uint right_strip_lengths_[COLUMN_COUNT_PER_SIDE]
);

void fill_top_strip(CRGB color, byte brightness=100);
void fill_side(led_side side, CRGB color, byte brightness=100);
void fill_side_HSV(led_side side, CHSV color);

void set_top_strip(uint x, CRGB color);
void setLED(led_side side, uint x, uint y, CRGB color);
bool setLED_check(led_side side, uint x, uint y, CRGB color, byte brightness=100);
void setLED_HSV(led_side side, uint x, uint y, CHSV color);

#endif
