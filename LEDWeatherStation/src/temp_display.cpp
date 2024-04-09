#include "util.hpp"
#include "setLED.hpp"
#include "temp_display.hpp"
#include "animator.hpp"

void dull_rgb(CRGB& color, uint8_t intensity) {
  float factor = (float)intensity / 255.0;
  color.setRGB(floor(color.red * factor), floor(color.green * factor), floor(color.blue * factor));
}

//26 colors, times 3 for the r g b values
uint8_t temp_colors[78] = {
  255, 0, 255, //20
  200, 0, 200, //21 - 24
  200, 0, 100, //25 - 27
  200, 100, 100, //28 - 29
  200, 200, 200, //30 - 32
  150, 150, 200, //33 - 34
  0, 0, 150, //35 - 37
  0, 50, 200, //38 - 39
  0, 100, 200, //40 - 42
  0, 150, 150, //43 - 44
  0, 200, 150, //45 - 47
  0, 200, 100, //48 - 51
  0, 150, 100, //52 - 54
  0, 100, 100, //55 - 59
  0, 100, 50, //60 - 64
  0, 100, 0, //65 - 69
  0, 150, 0, //70 - 72
  0, 200, 0, //73 - 75
  50, 200, 0, //76 - 79
  100, 200, 0, //80 - 82
  150, 200, 0, //83 - 84
  200, 100, 50, //85 - 87
  200, 100, 100, //88 - 89
  200, 50, 50, //90 - 92
  250, 50, 50, //93 - 94
  250, 0, 0 //95 - 100
};

#define REPEAT_NUM_2(n) n, n
#define REPEAT_NUM_3(n) n, n, n
#define REPEAT_NUM_4(n) n, n, n, n
#define REPEAT_NUM_5(n) n, n, n, n, n
#define REPEAT_NUM_6(n) n, n, n, n, n, n

//Inds for every number corresponding to index of color map where their color starts.
//Starts at 20
//For example, color_inds[14] = 15, because if the temperature is 34, then that corresponding color
//data begins at index 15 in the color map array. (it's at ind 14 since 34 - 20 = 14)
uint8_t color_inds[81] = {
  0, //20
  REPEAT_NUM_4(3), //21 - 24
  REPEAT_NUM_3(6), //25 - 27
  REPEAT_NUM_2(9), //28 - 29
  REPEAT_NUM_3(12), //30 - 32
  REPEAT_NUM_2(15), //33 - 34
  REPEAT_NUM_3(18), //35 - 37
  REPEAT_NUM_2(21), //38 - 39
  REPEAT_NUM_3(24), //40 - 42
  REPEAT_NUM_2(27), //43 - 44
  REPEAT_NUM_3(30), //45 - 47
  REPEAT_NUM_4(33), //48 - 51
  REPEAT_NUM_3(36), //52 - 54
  REPEAT_NUM_5(39), //55 - 59
  REPEAT_NUM_5(42), //60 - 64
  REPEAT_NUM_5(45), //65 - 69
  REPEAT_NUM_3(48), //70 - 72
  REPEAT_NUM_3(51), //73 - 75
  REPEAT_NUM_4(54), //76 - 79
  REPEAT_NUM_3(57), //80 - 82
  REPEAT_NUM_2(60), //83 - 84
  REPEAT_NUM_3(63), //85 - 87
  REPEAT_NUM_2(66), //88 - 89
  REPEAT_NUM_3(69), //90 - 92
  REPEAT_NUM_2(72), //93 - 94
  REPEAT_NUM_6(75) //95 - 100
};

Temp_Displayer::Temp_Displayer(led_side side_, double min_expected_temperature, double max_expected_temperature)
                        : side(side_), min_expected_temp(min_expected_temperature), max_expected_temp(max_expected_temperature)
{
  this->column_lengths = side_ == LEFT ? left_strip_lengths : right_strip_lengths;
  this->brighter_led_x = COLUMN_COUNT_PER_SIDE >> 1;
};

//Returns the RGB color an LED should be based on a temp
CRGB Temp_Displayer::temp_to_rgb(double temp) {
  temp = min(this->max_expected_temp, temp);
  temp = max(this->min_expected_temp, temp);

  uint8_t ind = color_inds[(uint8_t)floor(temp) - 20];
  CRGB color;
  color.setRGB(temp_colors[ind], temp_colors[ind + 1], temp_colors[ind + 2]);

  return color;
}

bool Temp_Displayer::should_update_LEDs() {
  unsigned long time_since_last_update;

  //Millis has reset
  if (millis() < this->last_anim_update) {
    time_since_last_update = ~((unsigned long)0) - time_since_last_update + millis();
    this->last_anim_update = 0;
  }
  else time_since_last_update = millis() - this->last_anim_update;

  return time_since_last_update >= TEMP_DISPLAY_UPDATE_INTERVAL;
}
bool Temp_Displayer::update_LEDs(double current_temp, double next_temp) {
  if (!this->should_update_LEDs()) return false;

  next_temp = min(this->max_expected_temp, next_temp);
  next_temp = max(this->min_expected_temp, next_temp);

  #define DULL_COLOR_INTENSITY 80
  #define BRIGHT_COLOR_INTENSITY 100

  CRGB color = this->temp_to_rgb(current_temp);
  dull_rgb(color, DULL_COLOR_INTENSITY);
  fill_side(this->side, color);

  if (abs(current_temp - next_temp) >= MIN_TEMP_DIFFERENCE_FOR_DIRECTION_SHOW) {
    if (current_temp > next_temp) this->temp_change_direction = TEMP_DEC;
    else this->temp_change_direction = TEMP_INC;
  }
  else this->temp_change_direction = NO_TEMP_CHANGE;

  if (this->temp_change_direction == TEMP_INC) {
    this->brighter_led_y = (this->brighter_led_y + 1) % this->column_lengths[this->brighter_led_x];
  }
  else if (this->temp_change_direction == TEMP_DEC) {
    this->brighter_led_y = (this->brighter_led_y - 1 + this->column_lengths[this->brighter_led_x]) % this->column_lengths[this->brighter_led_x];
  }

  if (this->temp_change_direction != NO_TEMP_CHANGE) {
    CRGB new_color = this->temp_to_rgb(next_temp);
    dull_rgb(new_color, BRIGHT_COLOR_INTENSITY);
    setLED(this->side, this->brighter_led_x, this->brighter_led_y, new_color);
  }

  this->last_anim_update = millis();

  return true;
};