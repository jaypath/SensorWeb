#ifndef ANIMATOR_HPP
#define ANIMATOR_HPP

#include <FastLED.h>

#include "setLED.hpp"
#include "util.hpp"

#define LIGHT_RAIN 0
#define HEAVY_RAIN 1
#define LIGHTNING 2

#define LIGHT_SNOW 0
#define HEAVY_SNOW 1

#define LIGHT_CLOUDS 0
#define HEAVY_CLOUDS 1

#define PRECIP_ANIM_ARRAY_LENGTH 40

#define rain_top_color 0x00d9ff
#define rain_middle_color 0x0000ff
#define rain_bottom_color 0x000055

#define LIGHT_RAIN_UPDATE_INTERVAL 30 //ms
#define HEAVY_RAIN_UPDATE_INTERVAL 30 //ms

#define LIGHT_SNOW_UPDATE_INTERVAL 80 //ms
#define HEAVY_SNOW_UPDATE_INTERVAL 50 //ms

#define STATIONARY_WEATHER_UPDATE_INTERVAL 100 //ms

#define LIGHTNING_DURATION 100 //ms
#define LIGHTNING_INTERVAL 5000 //ms

#define SUN_BRIGHTNESS 100
#define SNOW_BRIGHTNESS 75
#define RAIN_BRIGHTNESS 150
#define CLOUD_BRIGHTNESS 40
#define STAR_BRIGHTNESS 10 // out of 255
#define STAR_DIM_RANGE .1 //0 to 1; 0 is no dimming, 1 is range of 100% dimming

#define LED_Sky_Color 0x02ccfe
#define LED_Sun_Color 0xd4db04
#define LED_Cloud_Color 0x7f8079

#define Sun_Top_Strip_Color 0x02ccfe //sky blue
#define PartlyCloud_Top_Strip_Color 0x757575
#define Cloud_Top_Strip_Color 0x121212

#define Heavy_Precipitation_Top_Strip_Color 0xff0000
#define Light_Precipitation_Top_Strip_Color 0xcf2323

#define LEDHEIGHT 22
#define NUMSTARS 11
extern byte stars_x[NUMSTARS];
extern byte stars_y[NUMSTARS] ;
extern CRGB stars_c[NUMSTARS] ;

#define CLOUDWIDTH 20
extern byte clouds_x[LEDHEIGHT]; //the x position of clouds in  each row of the matrix
extern byte clouds_d[LEDHEIGHT] ; //cloud "diameter", really ellipse diameter, where d1 = clouds_d and d2 = d1/3
extern CRGB clouds_c[LEDHEIGHT] ; //cloud color
extern uint32_t Frame[3][LEDHEIGHT];

extern byte SUNRAY_STATE; 

void clearLEDs(CRGB LEDs, uint board_size);

class Temp_Displayer;
class LED_Weather_Animation {
  public:
  CRGB* LEDs;
  CRGB top_strip_color;

  led_side side;
  uint* column_lengths;

  uint rain_status;
  uint snow_status;
  uint cloud_status;

  uint anim_status;
  uint last_anim_status = 0;

  uint32_t animation_columns[COLUMN_COUNT_PER_SIDE][PRECIP_ANIM_ARRAY_LENGTH]; //note this array is TALLER than the actual LED array. When used for non vertical animation, only the bottom elements will be used (bottom starts at 0)
  uint8_t raindrop_statuses[COLUMN_COUNT_PER_SIDE];
  uint32_t animation_ind = 0;

  //Millis time of last update.
  unsigned long last_anim_update = 0;
  unsigned long last_stationary_weather_update = 0;
  unsigned long last_lightning_strike = 0;

  void progress_anim_columns();
  uint32_t color2uint(CRGB color, byte BRIGHTNESS_SCALE=100);
  void animate_precipitation(byte brightness=100);
  void animate_sky(byte current_hour, bool add_rays=true);
  void animate_rain(byte current_hour);
  void flash_lightning();
  void animate_snow();
  void animate_sun(byte current_hour);
  void progress_anim_clouds(byte current_hour,bool rain=false);
  void animate_clouds(byte current_hour,bool rain=false);
  void frame_fill(CRGB color,byte BRIGHTNESS_SCALE=100);
  void frame_draw(byte brightness=100);
  void frame_pixel(uint16_t x, uint16_t y, CRGB color,byte  brightness);
  //On our LED strip, the LEDs are wired such that the last strip of one of them goes to the other side of the base.
  //Basically, if we want to control the last column in our strip, we need to control the last strip of the other LED strip
  LED_Weather_Animation(led_side side_);

  void setHSV(uint x, uint y, CHSV color);

  void create_raindrop_arrays(uint32_t* arr, uint rain_status);
  void create_snowflake_arrays(uint32_t* arr, uint snow_status);

  void set_rainy_status(uint rain_status);
  void set_snowy_status(uint snow_status);
  void set_sunny_status(byte current_hour);
  void set_cloudy_status(uint cloud_status);

  void update_weather_status(long weather_status, byte current_hour);

  //Returns whether or not enough time has passed to update the LEDs
  bool should_update_LEDs();
  //Returns true if LEDs were updated, returns false if they weren't
  bool update_LEDs(byte current_hour);
};

#endif
