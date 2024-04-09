#include "WString.h"
#include "HardwareSerial.h"
#include <FastLED.h>

#include "util.hpp"
#include "animator.hpp"
#include "temp_display.hpp"
#include "setLED.hpp"

const uint rainy = 0;
const uint snowy = 1;
const uint sunny = 2;
const uint cloudy = 3;

const float pi = 3.14159;
const float top_bar_pulse_period = pi / 500;
const float top_bar_amplitude = 0.425;
const float top_bar_midline = 0.575;

byte SUNRAY_STATE=0; //which set of sun rays are to be drawn? 


byte stars_x[NUMSTARS] = {0,0,0,1,1,1,1,2,2,2,2};
byte stars_y[NUMSTARS] = {3,9,14,1,5,11,20,3,7,13,18};
CRGB stars_c[NUMSTARS] = {0xE9967A,0xFFFAF0,0xFFEBCD,0x7FFFD4,0xF08080,0xF5F5F5,0xFFE42D,0xFFFFFF,0xFFFF11,0xFAF0E6,0xFFFFFF};

uint32_t Frame[3][LEDHEIGHT] = {0};


byte clouds_x[LEDHEIGHT] = {0}; //cloud x position, for each y position 
byte clouds_d[LEDHEIGHT] = {0}; //cloud "diameter", really ellipse diameter, where d1 = clouds_d and d2 = d1/3
CRGB clouds_c[LEDHEIGHT] = {0}; //cloud color

#define MAKE_WHITE_COLOR(intensity) intensity | (intensity << 8) | (intensity << 16)

#define LIGHT_RAINDROP_DISPLAY_PROBABILITY 4
#define HEAVY_RAINDROP_DISPLAY_PROBABILITY 12

#define LIGHT_SNOWFLAKE_DISPLAY_PROBABILITY 7 // / 100
#define HEAVY_SNOWFLAKE_DISPLAY_PROBABILITY 25 // / 100

#define MIN_SNOWFLAKE_INTENSITY 15
#define MAX_SNOWFLAKE_INTENSITY 225

#define MIN_SUN_BRIGHTNESS 20.0
#define MAX_SUN_BRIGHTNESS 200.0

double base_sun_brightness = MIN_SUN_BRIGHTNESS / 255.0;

LED_Weather_Animation::LED_Weather_Animation(led_side side_)
                        : side(side_)
{
  for (uint col = 0; col < COLUMN_COUNT_PER_SIDE; col += 1) {
    this->raindrop_statuses[col] = 0;
    for (uint y = 0; y < PRECIP_ANIM_ARRAY_LENGTH; y += 1) {
      this->animation_columns[col][y] = 0;
    }
  }

  this->column_lengths = side_ == LEFT ? left_strip_lengths : right_strip_lengths;
};

void LED_Weather_Animation::set_rainy_status(uint rain_status_) {
  for (uint ind = 0; ind < COLUMN_COUNT_PER_SIDE; ind += 1) {
    this->raindrop_statuses[ind] = 0;
  }

  for (uint col = 0; col < COLUMN_COUNT_PER_SIDE; col += 1) {
    this->create_raindrop_arrays(this->animation_columns[col], rain_status_);
  }

  this->anim_status = rainy;
  this->rain_status = rain_status_;

  if (rain_status_ == LIGHT_RAIN) this->top_strip_color = Light_Precipitation_Top_Strip_Color;
  else this->top_strip_color = Heavy_Precipitation_Top_Strip_Color;
};
void LED_Weather_Animation::set_snowy_status(uint snow_status_) {
  for (uint col = 0; col < COLUMN_COUNT_PER_SIDE; col += 1) {
    this->create_snowflake_arrays(this->animation_columns[col], snow_status_);
  }

  this->anim_status = snowy;
  this->snow_status = snow_status_;

  if (snow_status_ == LIGHT_SNOW) this->top_strip_color = Light_Precipitation_Top_Strip_Color;
  else this->top_strip_color = Heavy_Precipitation_Top_Strip_Color;
};
void LED_Weather_Animation::set_sunny_status(byte current_hour) {
  this->anim_status = sunny;
  if (current_hour>19 || current_hour<7) {
    this->top_strip_color = (CRGB)0;
    fill_top_strip(this->top_strip_color);

  } else{
    this->top_strip_color = (CRGB)Sun_Top_Strip_Color;
    fill_top_strip(this->top_strip_color);
  }
}
void LED_Weather_Animation::set_cloudy_status(uint cloud_status_) {
  this->anim_status = cloudy;
  this->cloud_status = cloud_status_;
  if (cloud_status_ == HEAVY_CLOUDS)   this->top_strip_color = (CRGB)Cloud_Top_Strip_Color;
  if (cloud_status_ == LIGHT_CLOUDS)   this->top_strip_color = (CRGB)PartlyCloud_Top_Strip_Color;
  fill_top_strip(this->top_strip_color);
}

void LED_Weather_Animation::update_weather_status(long weather_status, byte current_hour) {
  #ifdef DEBUG
    Serial.println("Weather status: " + String(weather_status));
  #endif

  //Thunderstorm
  if (weather_status >= 200 && weather_status < 300) {
    this->set_rainy_status(LIGHTNING);
    return;
  }
  //Drizzle
  if (weather_status >= 300 && weather_status < 400) {
    switch (weather_status) {
      //Heavy drizzle
      case 302:
      case 312:
      case 314:
        this->set_rainy_status(HEAVY_RAIN);
        break;
      default:
        this->set_rainy_status(LIGHT_RAIN);
    }
    return;
  }
  //Rain
  if (weather_status >= 500 && weather_status < 600) {
    switch (weather_status) {
      //Heavy rain
      case 502:
      case 503:
      case 504:
      case 522:
        this->set_rainy_status(HEAVY_RAIN);
        break;
      default:
        this->set_rainy_status(LIGHT_RAIN);
    }
    return;
  }
  //Snow
  if (weather_status >= 600 && weather_status < 700) {
    switch (weather_status) {
      //Heavy snow
      case 602:
      case 622:
        this->set_snowy_status(HEAVY_SNOW);
        break;
      default:
        this->set_snowy_status(LIGHT_SNOW);
    }
  }
  //Bad weather events that haven't been implemented
  if (
      (weather_status >= 700 && weather_status < 800) &&
      (weather_status != 701 && weather_status != 741)
    ) {
    this->set_rainy_status(LIGHTNING);
  }
  //Clear sky
  if (weather_status == 800) this->set_sunny_status(current_hour);
  //Clouds
  if (weather_status >= 801 && weather_status < 900) {
    switch (weather_status) {
      //Heavy clouds
      case 804:
        this->set_cloudy_status(HEAVY_CLOUDS);
        break;
      default:
        this->set_cloudy_status(LIGHT_CLOUDS);
    }
  }
};

void LED_Weather_Animation::create_raindrop_arrays(uint32_t* arr, uint rain_status) {
  for (uint arr_ind = 0; arr_ind < PRECIP_ANIM_ARRAY_LENGTH; arr_ind += 1) {
    arr[arr_ind] = 0;
  }

  uint ind = 1;
  while (ind < PRECIP_ANIM_ARRAY_LENGTH - 3) {
    if (
      random(0, 100) >=
      (rain_status == LIGHT_RAIN ? LIGHT_RAINDROP_DISPLAY_PROBABILITY : HEAVY_RAINDROP_DISPLAY_PROBABILITY)
    ) {
      ind += 1;
      continue;
    };

    arr[ind] = rain_top_color;
    ind += 1;
    arr[ind] = rain_middle_color;
    ind += 1;
    arr[ind] = rain_bottom_color;

    ind += 2;
  }
}
void LED_Weather_Animation::create_snowflake_arrays(uint32_t* arr, uint snow_status) {
  for (uint arr_ind = 0; arr_ind < PRECIP_ANIM_ARRAY_LENGTH; arr_ind += 1) {
    arr[arr_ind] = 0;
  }

  //Create light snowflake array
  uint ind = 1;
  while (ind < PRECIP_ANIM_ARRAY_LENGTH) {
    if (
      random(0, 100) >=
      (snow_status == LIGHT_SNOW ? LIGHT_SNOWFLAKE_DISPLAY_PROBABILITY : HEAVY_SNOWFLAKE_DISPLAY_PROBABILITY)
    ) {
      ind += 1;
      continue;
    }

    arr[ind] = MAKE_WHITE_COLOR(random(MIN_SNOWFLAKE_INTENSITY, MAX_SNOWFLAKE_INTENSITY));

    //We want to make sure that two snowflakes don't generate in a row
    ind += 2;
  }
}

void LED_Weather_Animation::progress_anim_columns() {
  uint32_t original_anim_ind = this->animation_ind;
  this->animation_ind = (this->animation_ind + 1) % PRECIP_ANIM_ARRAY_LENGTH;

  if (this->anim_status == rainy) {
    for (uint col = 0; col < COLUMN_COUNT_PER_SIDE; col += 1) {
      uint32_t color = 0x000000;
      
      uint chance = random(0, 100);

      switch (this->rain_status) {
        case LIGHT_RAIN:
          if (chance >= LIGHT_RAINDROP_DISPLAY_PROBABILITY) goto after_raindrop;
          break;
        case HEAVY_RAIN:
        case LIGHTNING:
          if (chance >= HEAVY_RAINDROP_DISPLAY_PROBABILITY) goto after_raindrop;
          break;
      }

      this->raindrop_statuses[col] = 1;

      after_raindrop:
        if (this->raindrop_statuses[col] != 0) {
          switch (this->raindrop_statuses[col]) {
            case 1:
              color = rain_top_color;
              break;
            case 2:
              color = rain_middle_color;
              break;
            case 3:
              color = rain_bottom_color;
          }
          this->raindrop_statuses[col] = (this->raindrop_statuses[col] + 1) % 4;
        }
      this->animation_columns[col][original_anim_ind] = color;
    }
  }
  else if (this->anim_status == snowy) {
    for (uint col = 0; col < COLUMN_COUNT_PER_SIDE; col += 1) {
      uint32_t color = 0x000000;

      bool make_snowflake = true;
      uint chance = random(0, 100);

      if (this->snow_status == LIGHT_SNOW) {
        if (chance >= LIGHT_SNOWFLAKE_DISPLAY_PROBABILITY) make_snowflake = false;
      }
      if (this->snow_status == HEAVY_SNOW) {
          if (chance >= HEAVY_SNOWFLAKE_DISPLAY_PROBABILITY) make_snowflake = false;
      }

      if (make_snowflake) {
        uint8_t intensity = random(MIN_SNOWFLAKE_INTENSITY, MAX_SNOWFLAKE_INTENSITY);

        color = intensity;
        color |= (intensity << 8);
        color |= (intensity << 16);
      }

      this->animation_columns[col][original_anim_ind] = color;
    }
  }
}

void LED_Weather_Animation::animate_precipitation(byte brightness) {
  //Animate each column
  for (uint x = 0; x < COLUMN_COUNT_PER_SIDE; x += 1) {
//    for (uint y = 0; y < this->column_lengths[x]; y += 1) {
      for (uint y = 0; y < LEDHEIGHT; y += 1) {
      
      uint32_t c = color2uint(this->animation_columns[x][(this->animation_ind + y) % PRECIP_ANIM_ARRAY_LENGTH],brightness);

      if (c!=0)   Frame[x][y]=c;
      
    }
  }
}

uint32_t LED_Weather_Animation::color2uint(CRGB color, byte BRIGHTNESS_SCALE) {
  return  (uint32_t) (color.red*BRIGHTNESS_SCALE/100) << 16 | (uint32_t) (color.green*BRIGHTNESS_SCALE/100) << 8 | (uint32_t) (color.blue*BRIGHTNESS_SCALE/100);
}
void LED_Weather_Animation::frame_fill(CRGB color,byte BRIGHTNESS_SCALE){
  uint32_t c =  color2uint(color,BRIGHTNESS_SCALE);
  for (byte x=0;x<COLUMN_COUNT_PER_SIDE;x++) {
    for (byte y=0;y<LEDHEIGHT;y++) {
      //note that the actual drawn frame is only LEDHEIGHT tall. But the total frame size is PRECIP_ANIMARRAY_LENGTH
      Frame[x][y] = c;
    }
  }
}
void LED_Weather_Animation::frame_pixel(uint16_t x, uint16_t y, CRGB color,byte  brightness){
  Frame[x][y] = color2uint(color,brightness);
  
}

void LED_Weather_Animation::frame_draw(byte brightness){
  for (byte x=0;x<COLUMN_COUNT_PER_SIDE;x++) {
    for (byte y=0;y<LEDHEIGHT;y++) {
      setLED_check(this->side,x,y,Frame[x][y],brightness);
    }
  }
}


void LED_Weather_Animation::animate_sky(byte current_hour, bool add_rays) {
  
  byte global_brightness = 30;

  //fill the appropriate background... dark with stars 7p - 7a or blue for 7a to 7p
  if (current_hour>19 || current_hour<7) {  
    frame_fill(0,0);

    //add and twinkle stars
    for (uint x = 0; x < NUMSTARS; x++) {
      CRGB color;
      byte brightness = (byte)  random(floor((double)(1-STAR_DIM_RANGE)*STAR_BRIGHTNESS),floor((double)(1+STAR_DIM_RANGE)*STAR_BRIGHTNESS));
      
      frame_pixel(stars_x[x],stars_y[x],color, brightness);
    }
  } else {
    //fill sky
    frame_fill(LED_Sky_Color, global_brightness);

    //add sun at the apporpriate position...
    byte sun_y = (22-current_hour);
    //the point at which the sun is will be white
    frame_pixel(1,sun_y,0xffffff, 100); //center of sun is full bright!

    //surrounding LEDs will be light yellow
    frame_pixel(0,sun_y,0xfdfd22, global_brightness);
    frame_pixel(2,sun_y,0xfdfd22, global_brightness);
    frame_pixel(1,sun_y+1,0xfdfd22, global_brightness);
    frame_pixel(1,sun_y-1,0xfdfd22, global_brightness);


    //add center light yellow above and below
    frame_pixel(1,sun_y-2,0xeeee00, global_brightness);
    frame_pixel(1,sun_y+2,0xeeee00, global_brightness);

    frame_pixel(0,sun_y-1,0xeeee00, global_brightness);
    frame_pixel(0,sun_y+1,0xeeee00, global_brightness);

    frame_pixel(2,sun_y-1,0xeeee00, global_brightness);
    frame_pixel(2,sun_y+1,0xeeee00, global_brightness);
        

    if (add_rays==false) return;

    //add the rays
    if (SUNRAY_STATE==0) {
      //right next to the sun
      frame_pixel(1,sun_y-3,0xdede00, global_brightness);
      frame_pixel(1,sun_y+3,0xdede00, global_brightness);
      frame_pixel(0,sun_y-2,0xdede00, global_brightness);
      frame_pixel(0,sun_y+2,0xdede00, global_brightness);
      frame_pixel(2,sun_y-2,0xdede00, global_brightness);
      frame_pixel(2,sun_y+2,0xdede00, global_brightness);


      frame_pixel(1,sun_y+4,LED_Sky_Color, global_brightness);
      frame_pixel(1,sun_y-4,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y+3,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y-3,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y+3,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y-3,LED_Sky_Color, global_brightness);
      //next surronud
      frame_pixel(1,sun_y+5,LED_Sky_Color, global_brightness);
      frame_pixel(1,sun_y-5,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y+4,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y-4,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y+4,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y-4,LED_Sky_Color, global_brightness);
      //next surround
      frame_pixel(1,sun_y+6,LED_Sky_Color, global_brightness);
      frame_pixel(1,sun_y-6,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y+5,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y-5,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y+5,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y-5,LED_Sky_Color, global_brightness);  
      //next surround
      frame_pixel(1,sun_y+7,LED_Sky_Color, global_brightness);
      frame_pixel(1,sun_y-7,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y+6,LED_Sky_Color, global_brightness);
      frame_pixel(0,sun_y-6,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y+6,LED_Sky_Color, global_brightness);
      frame_pixel(2,sun_y-6,LED_Sky_Color, global_brightness);  

    } else {
      if (SUNRAY_STATE==1) {
        //right next to the sun
        frame_pixel(1,sun_y-3,LED_Sky_Color, global_brightness);
        frame_pixel(1,sun_y+3,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y-2,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y+2,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y-2,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y+2,LED_Sky_Color, global_brightness);


        frame_pixel(1,sun_y+4,0xEEEE00, global_brightness*.8);
        frame_pixel(1,sun_y-4,0xEEEE00, global_brightness*.8);
        frame_pixel(0,sun_y+3,0xEEEE00, global_brightness*.8);
        frame_pixel(0,sun_y-3,0xEEEE00, global_brightness*.8);
        frame_pixel(2,sun_y+3,0xEEEE00, global_brightness*.8);
        frame_pixel(2,sun_y-3,0xEEEE00, global_brightness*.8);
        //next surronud
        frame_pixel(1,sun_y+5,LED_Sky_Color, global_brightness);
        frame_pixel(1,sun_y-5,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y+4,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y-4,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y+4,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y-4,LED_Sky_Color, global_brightness);
        //next surround
        frame_pixel(1,sun_y+6,LED_Sky_Color, global_brightness);
        frame_pixel(1,sun_y-6,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y+5,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y-5,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y+5,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y-5,LED_Sky_Color, global_brightness);  
        //next surround
        frame_pixel(1,sun_y+7,LED_Sky_Color, global_brightness);
        frame_pixel(1,sun_y-7,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y+6,LED_Sky_Color, global_brightness);
        frame_pixel(0,sun_y-6,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y+6,LED_Sky_Color, global_brightness);
        frame_pixel(2,sun_y-6,LED_Sky_Color, global_brightness);  
      } else {
        if (SUNRAY_STATE==2) {
          //right next to the sun
          frame_pixel(1,sun_y-3,LED_Sky_Color, global_brightness);
          frame_pixel(1,sun_y+3,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y-2,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y+2,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y-2,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y+2,LED_Sky_Color, global_brightness);


          frame_pixel(1,sun_y+4,LED_Sky_Color, global_brightness);
          frame_pixel(1,sun_y-4,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y+3,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y-3,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y+3,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y-3,LED_Sky_Color, global_brightness);
          //next surronud
          frame_pixel(1,sun_y+5,0xEEEE00 , global_brightness*.6);
          frame_pixel(1,sun_y-5,0xEEEE00 , global_brightness*.6);
          frame_pixel(0,sun_y+4,0xEEEE00 , global_brightness*.6);
          frame_pixel(0,sun_y-4,0xEEEE00 , global_brightness*.6);
          frame_pixel(2,sun_y+4,0xEEEE00 , global_brightness*.6);
          frame_pixel(2,sun_y-4,0xEEEE00 , global_brightness*.6);
          //next surround
          frame_pixel(1,sun_y+6,LED_Sky_Color, global_brightness);
          frame_pixel(1,sun_y-6,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y+5,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y-5,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y+5,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y-5,LED_Sky_Color, global_brightness);  
          //next surround
          frame_pixel(1,sun_y+7,LED_Sky_Color, global_brightness);
          frame_pixel(1,sun_y-7,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y+6,LED_Sky_Color, global_brightness);
          frame_pixel(0,sun_y-6,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y+6,LED_Sky_Color, global_brightness);
          frame_pixel(2,sun_y-6,LED_Sky_Color, global_brightness);  
        } else {
          if (SUNRAY_STATE==3) {
            //right next to the sun
            frame_pixel(1,sun_y-3,LED_Sky_Color, global_brightness);
            frame_pixel(1,sun_y+3,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y-2,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y+2,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y-2,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y+2,LED_Sky_Color, global_brightness);


            frame_pixel(1,sun_y+4,LED_Sky_Color, global_brightness);
            frame_pixel(1,sun_y-4,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y+3,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y-3,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y+3,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y-3,LED_Sky_Color, global_brightness);
            //next surronud
            frame_pixel(1,sun_y+5,LED_Sky_Color, global_brightness);
            frame_pixel(1,sun_y-5,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y+4,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y-4,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y+4,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y-4,LED_Sky_Color, global_brightness);
            //next surround
            frame_pixel(1,sun_y+6,0xEEEE00 , global_brightness*.4);
            frame_pixel(1,sun_y-6,0xEEEE00 , global_brightness*.4);
            frame_pixel(0,sun_y+5,0xEEEE00 , global_brightness*.4);
            frame_pixel(0,sun_y-5,0xEEEE00 , global_brightness*.4);
            frame_pixel(2,sun_y+5,0xEEEE00 , global_brightness*.4);
            frame_pixel(2,sun_y-5,0xEEEE00 , global_brightness*.4);  
            //next surround
            frame_pixel(1,sun_y+7,LED_Sky_Color, global_brightness);
            frame_pixel(1,sun_y-7,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y+6,LED_Sky_Color, global_brightness);
            frame_pixel(0,sun_y-6,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y+6,LED_Sky_Color, global_brightness);
            frame_pixel(2,sun_y-6,LED_Sky_Color, global_brightness);  
          } else {
            if (SUNRAY_STATE==4) {
              //right next to the sun
              frame_pixel(1,sun_y-3,0xEEEE00, global_brightness*.4);
              frame_pixel(1,sun_y+3,0xEEEE00, global_brightness*.4);
              frame_pixel(0,sun_y-2,0xEEEE00, global_brightness*.4);
              frame_pixel(0,sun_y+2,0xEEEE00, global_brightness*.4);
              frame_pixel(2,sun_y-2,0xEEEE00, global_brightness*.4);
              frame_pixel(2,sun_y+2,0xEEEE00, global_brightness*.4);


              frame_pixel(1,sun_y+4,LED_Sky_Color, global_brightness);
              frame_pixel(1,sun_y-4,LED_Sky_Color, global_brightness);
              frame_pixel(0,sun_y+3,LED_Sky_Color, global_brightness);
              frame_pixel(0,sun_y-3,LED_Sky_Color, global_brightness);
              frame_pixel(2,sun_y+3,LED_Sky_Color, global_brightness);
              frame_pixel(2,sun_y-3,LED_Sky_Color, global_brightness);
              //next surronud
              frame_pixel(1,sun_y+5,LED_Sky_Color, global_brightness);
              frame_pixel(1,sun_y-5,LED_Sky_Color, global_brightness);
              frame_pixel(0,sun_y+4,LED_Sky_Color, global_brightness);
              frame_pixel(0,sun_y-4,LED_Sky_Color, global_brightness);
              frame_pixel(2,sun_y+4,LED_Sky_Color, global_brightness);
              frame_pixel(2,sun_y-4,LED_Sky_Color, global_brightness);
              //next surround
              frame_pixel(1,sun_y+6,LED_Sky_Color, global_brightness);
              frame_pixel(1,sun_y-6,LED_Sky_Color, global_brightness);
              frame_pixel(0,sun_y+5,LED_Sky_Color, global_brightness);
              frame_pixel(0,sun_y-5,LED_Sky_Color, global_brightness);
              frame_pixel(2,sun_y+5,LED_Sky_Color, global_brightness);
              frame_pixel(2,sun_y-5,LED_Sky_Color, global_brightness);  
              //next surround
              frame_pixel(1,sun_y+7,0xEEEE00 , global_brightness*.25);
              frame_pixel(1,sun_y-7,0xEEEE00 , global_brightness*.25);
              frame_pixel(0,sun_y+6,0xEEEE00 , global_brightness*.25);
              frame_pixel(0,sun_y-6,0xEEEE00 , global_brightness*.25);
              frame_pixel(2,sun_y+6,0xEEEE00 , global_brightness*.25);
              frame_pixel(2,sun_y-6,0xEEEE00 , global_brightness*.25);  
            } 
          }
        }
      }
    }
  }


    #ifdef DEBUG 
    Serial.printf("Sunray state is: %d\n",SUNRAY_STATE);
    #endif  
    SUNRAY_STATE = (SUNRAY_STATE+1)%5;

}


void LED_Weather_Animation::animate_rain(byte current_hour) {
  //FastLED.setBrightness(RAIN_BRIGHTNESS);
  this->animate_precipitation(RAIN_BRIGHTNESS);
  this->animate_clouds(current_hour,true);
  this->progress_anim_columns();
  this->frame_draw(100);
}
void LED_Weather_Animation::flash_lightning() {
  //Flash every LED white
  CRGB color;
  color.setRGB(255, 255, 255);
  fill_side(this->side, color);

  fill_top_strip((CRGB)0xffffff);

  this->last_lightning_strike = millis();
}
void LED_Weather_Animation::animate_snow() {
  this->animate_precipitation(SNOW_BRIGHTNESS);
  //FastLED.setBrightness(SNOW_BRIGHTNESS);
  this->progress_anim_columns();
  this->frame_draw(100);
}

void LED_Weather_Animation::animate_sun(byte current_hour) {
  if (millis() - this->last_stationary_weather_update>STATIONARY_WEATHER_UPDATE_INTERVAL) {
    this->last_stationary_weather_update = millis();

    this->animate_sky(current_hour,true);
    frame_draw(SUN_BRIGHTNESS);
  }
}

void LED_Weather_Animation::progress_anim_clouds(byte current_hour, bool rain) {
  byte cloudbase = 0;

  if (rain) cloudbase = 16;
  for (byte j=cloudbase;j<LEDHEIGHT;j++) {
    if (clouds_d[j]>1) clouds_x[j]++; //diameter is >1
    else {
      byte intensity;
      if (this->cloud_status == LIGHT_CLOUDS && rain==false) {
        if (random(1,100)>80) clouds_d[j] = random(6,10);
        intensity = random(15,55);   
      } else {
        if (random(1,100)>60) clouds_d[j] = random(10,18);
        intensity = random(0,45);      
      }
      clouds_c[j].setRGB(intensity,intensity,intensity);
    }
    
    if (clouds_x[j]>CLOUDWIDTH) {
      clouds_x[j] = 0;
      clouds_d[j] = 0;
    }
  }
}

void LED_Weather_Animation::animate_clouds(byte current_hour, bool rain) {
  
  byte cloudbase = 0;
  if (rain==false)  this->animate_sky(current_hour,false); //create sky
  else {
    cloudbase = 16;
    if (current_hour > 20 || current_hour<6) return; //it's night, so don't draw stars or clouds
  }
  //now add clouds
  //cloud array (sparse matrix) passes the LEDs at CLOUDWIDTH/2-1 to CLOUDWIDTH/2+1
  //so march throuhg all rows and see if a cloud is present
  byte x=0, y=0;
  CRGB color = 0;

  for (byte y=cloudbase; y<LEDHEIGHT; y++){
    if (clouds_d[y]>=0) { //a cloud center exists for this row, if  it is the plotting window then add cloud to array
      int x_C = clouds_x[y] - (CLOUDWIDTH/2)+1; //this is the center of the cloud, relative to x=0 is the first LED column
      int x_L = x_C - clouds_d[y]/2; //this is the left edge of the cloud
      int x_R = x_C + clouds_d[y]/2; //this is the right edge of the cloud
      int y_T = y + clouds_d[y]/3/2; //this is the top edge of the cloud
      int y_B = y - clouds_d[y]/3/2; //this is the bottom edge of the cloud
      
      for (byte yy=0;yy<LEDHEIGHT; yy++){
        if (y_T>=yy && y_B<=yy) {
          if (x_R >= 0 && x_L <= 0) frame_pixel(0,yy,clouds_c[y],100);
          if (x_R >= 1 && x_L <= 1) frame_pixel(1,yy,clouds_c[y],100);
          if (x_R >= 2 && x_L <= 2) frame_pixel(2,yy,clouds_c[y],100);
        }
      }
    }
  }

  if (millis() - this->last_stationary_weather_update>STATIONARY_WEATHER_UPDATE_INTERVAL) {
    this->last_stationary_weather_update = millis();
    progress_anim_clouds(current_hour, rain) ;
    if (rain) return; //don't draw screen here
    else       frame_draw(100);
  }

}

bool LED_Weather_Animation::should_update_LEDs() {
  unsigned long time_since_last_update;

  //Millis has reset
  if (millis() < this->last_anim_update) {
    time_since_last_update = ~((unsigned long)0) - time_since_last_update;
    this->last_anim_update = 0;
  }
  else time_since_last_update = millis() - this->last_anim_update;

  switch (this->anim_status) {
    case rainy:
      switch (this->rain_status) {
        case LIGHT_RAIN:
          return time_since_last_update >= LIGHT_RAIN_UPDATE_INTERVAL;
        case HEAVY_RAIN:
          return time_since_last_update >= HEAVY_RAIN_UPDATE_INTERVAL;
        case LIGHTNING:
          //Are we in the middle of a lightning strike?
          if (millis() - this->last_lightning_strike < LIGHTNING_DURATION) return false;
          return time_since_last_update >= HEAVY_RAIN_UPDATE_INTERVAL;
      }
    case snowy:
        switch (this->snow_status) {
          case LIGHT_SNOW:
            return time_since_last_update >= LIGHT_SNOW_UPDATE_INTERVAL;
          case HEAVY_SNOW:
            return time_since_last_update >= HEAVY_SNOW_UPDATE_INTERVAL;
        }

    //If it's sunny or cloudy weather, the LEDs never change, so only update them the first time
    case sunny:
      return time_since_last_update >= STATIONARY_WEATHER_UPDATE_INTERVAL;
    case cloudy:
      return time_since_last_update >= STATIONARY_WEATHER_UPDATE_INTERVAL;
  }

  return false;
}
bool LED_Weather_Animation::update_LEDs(byte current_hour) {
  this->frame_fill(0,0);
  if (!this->should_update_LEDs()) return false;

  if (this->anim_status == rainy || this->anim_status == snowy) {
    float top_bar_intensity = top_bar_amplitude * sin(top_bar_pulse_period * millis()) + top_bar_midline;
    uint8_t red = this->top_strip_color.red;
    uint8_t green = this->top_strip_color.green;
    uint8_t blue = this->top_strip_color.blue;

    red *= top_bar_intensity;
    green *= top_bar_intensity;
    blue *= top_bar_intensity;

    CRGB top_color;
    top_color.setRGB(red, green, blue);

    fill_top_strip(top_color);
  }

  switch (this->anim_status) {
    case rainy:
      //Every five seconds, flash lightning if that weather mode is on
      if (
        this->rain_status == LIGHTNING &&
        (millis() < this->last_lightning_strike || millis() - this->last_lightning_strike > LIGHTNING_INTERVAL)
      ) {
        this->flash_lightning();
        break;
      }

      this->animate_rain(current_hour);
      break;
    case snowy:
      this->animate_snow();
      break;
    case sunny:
      this->animate_sun(current_hour);
      break;
    case cloudy:
      this->animate_clouds(current_hour);
      break;
  }

  this->last_anim_status = this->anim_status;
  this->last_anim_update = millis();

  return true;
};
