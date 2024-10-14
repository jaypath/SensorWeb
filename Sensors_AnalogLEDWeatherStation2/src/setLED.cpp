#include "util.hpp"
#include "setLED.hpp"
#include <FastLED.h>

CRGB* top_led_strip;
uint top_strip_length;
CRGB* left_leds[COLUMN_COUNT_PER_SIDE];
CRGB* right_leds[COLUMN_COUNT_PER_SIDE];
uint left_strip_lengths[COLUMN_COUNT_PER_SIDE];
uint right_strip_lengths[COLUMN_COUNT_PER_SIDE];

void init_led_info(
  CRGB* top_led_strip_,
  uint top_strip_length_,
  CRGB* left_leds_[COLUMN_COUNT_PER_SIDE],
  CRGB* right_leds_[COLUMN_COUNT_PER_SIDE],
  uint left_strip_lengths_[COLUMN_COUNT_PER_SIDE],
  uint right_strip_lengths_[COLUMN_COUNT_PER_SIDE]
) {
  top_led_strip = top_led_strip_;
  top_strip_length = top_strip_length_;

  for (uint col = 0; col < COLUMN_COUNT_PER_SIDE; col += 1) {
    left_leds[col] = left_leds_[col];
    right_leds[col] = right_leds_[col];
    left_strip_lengths[col] = left_strip_lengths_[col];
    right_strip_lengths[col] = right_strip_lengths_[col];
  }
}

void fill_top_strip(CRGB color, byte brightness) {
  if (brightness<100) {
    color.red = floor((double) color.red * brightness/100);
    color.blue = floor((double) color.blue * brightness/100);
    color.green = floor((double) color.green * brightness/100);
  }

  for (uint x = 0; x < top_strip_length; x += 1) {
    top_led_strip[x].setRGB(color.red, color.green, color.blue);
  }
}
void fill_side(led_side side, CRGB color, byte brightness) {
  if (brightness<100) {
    color.red = floor((double) color.red * brightness/100);
    color.blue = floor((double) color.blue * brightness/100);
    color.green = floor((double) color.green * brightness/100);
  }

  if (side == LEFT) {
    for (uint x = 0; x < COLUMN_COUNT_PER_SIDE; x += 1) {
      for (uint y = 0; y < left_strip_lengths[x]; y += 1) {
        left_leds[x][y].setRGB(color.red, color.green, color.blue);
      }
    }
  }
  //Right side
  else {
    for (uint x = 0; x < COLUMN_COUNT_PER_SIDE; x += 1) {
      for (uint y = 0; y < right_strip_lengths[x]; y += 1) {
        right_leds[x][y].setRGB(color.red, color.green, color.blue);
      }
    }
  }
}
void fill_side_HSV(led_side side, CHSV color) {
  if (side == LEFT) {
    for (uint x = 0; x < COLUMN_COUNT_PER_SIDE; x += 1) {
      for (uint y = 0; y < left_strip_lengths[x]; y += 1) {
        left_leds[x][y].setHSV(color.hue, color.saturation, color.value);
      }
    }
  }
  //Right side
  else {
    for (uint x = 0; x < COLUMN_COUNT_PER_SIDE; x += 1) {
      for (uint y = 0; y < right_strip_lengths[x]; y += 1) {
        right_leds[x][y].setHSV(color.hue, color.saturation, color.value);
      }
    }
  }
}

void set_top_strip(uint x, CRGB color) {
  top_led_strip[x] = color;
}
void setLED(led_side side, uint x, uint y, CRGB color) {
  if (side == LEFT) {
    //Serpentine layout
    if (x & 1) left_leds[x][left_strip_lengths[x] - 1 - y] = color;
    else left_leds[x][y] = color;
  }
  //Right side
  else {
    //Serpentine layout
    if (x & 1) right_leds[x][right_strip_lengths[x] - 1 - y] = color;
    else right_leds[x][y] = color;
  }
}
void setLED_HSV(led_side side, uint x, uint y, CHSV color) {
  if (side == LEFT) {
    //Serpentine layout
    if (x & 1) left_leds[x][left_strip_lengths[x] - 1 - y] = color;
    else left_leds[x][y] = color;
  }
  //Right side
  else {
    //Serpentine layout
    if (x & 1) right_leds[x][right_strip_lengths[x] - 1 - y] = color;
    else right_leds[x][y] = color;
  }
}
bool setLED_check(led_side side, uint x, uint y, CRGB color, byte brightness) {
  //Is it a valid index?
  if (brightness<100) {
    color.red = floor((double) color.red * brightness/100);
    color.blue = floor((double) color.blue * brightness/100);
    color.green = floor((double) color.green * brightness/100);
  }

  if (x >= COLUMN_COUNT_PER_SIDE) return false;
  if (side == LEFT) {
    if (y >= left_strip_lengths[x]) return false;
  }
  else if (side == RIGHT) {
    if (y >= right_strip_lengths[x]) return false;
  }
  else return false;

  setLED(side, x, y, color);
  return true;
}
