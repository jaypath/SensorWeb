#ifndef TEMP_DISPLAY_HPP
#define TEMP_DISPLAY_HPP

#include "setLED.hpp"
#include "util.hpp"

#include <FastLED.h>

#define TEMP_DISPLAY_UPDATE_INTERVAL 100 //ms

#define MIN_TEMP_DIFFERENCE_FOR_DIRECTION_SHOW 2 //Fahrenheit
#define NO_TEMP_CHANGE 0
#define TEMP_INC 1
#define TEMP_DEC 3

#define MIN_TEMP 20.0 //Fahrenheit
#define MAX_TEMP 100.0 //Fahrenheit

class LED_Weather_Animation;
class Temp_Displayer {
  public:
  CRGB* LEDs;
  uint* column_lengths;

  led_side side;

  //Millis time of last update.
  unsigned long last_anim_update = 0;

  uint total_LED_count = 0;

  double min_expected_temp;
  double max_expected_temp;

  //0 = no change
  //1 = going up
  //3 = going down
  uint8_t temp_change_direction = 0;
  uint8_t brighter_led_y = 0;
  uint8_t brighter_led_x;

  //On our LED strip, the LEDs are wired such that the last strip of one of them goes to the other side of the base.
  //Basically, if we want to control the last column in our strip, we need to control the last strip of the other LED strip
  //(which is the LED weather animation one)
  Temp_Displayer(led_side side, double min_expected_temperature, double max_expected_temperature);

  CRGB temp_to_rgb(double temp);

  //Returns whether or not enough time has passed to update the LEDs
  bool should_update_LEDs();
  //Returns true if LEDs were updated, returns false if they weren't
  bool update_LEDs(double current_temp, double next_temp);
};

#endif