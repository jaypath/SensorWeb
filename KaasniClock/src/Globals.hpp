#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include <Arduino.h>
#include <string.h>
#include "SPI.h"
#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
//------------------


//weather
//wellesley, MA
#define LAT 42.30210392783453  //only 4 decimal pts
#define LON -71.29822225979105 //only 4 decimal pts allowed with NOAA


#include "graphics.hpp"



struct Screen {
    uint8_t wifi;

    uint8_t num_images;
    int8_t lastPic;

    
    uint32_t t;
    uint32_t DSTOFFSET;

    uint16_t FG_COLOR;
    uint16_t BG_COLOR;

    int16_t lastX;
    int16_t lastY;

    uint8_t timer_s,timer_m,timer_h,timer_d;


    uint32_t time_lastPic;
    uint32_t time_lastClock;
    uint32_t time_lastWeather;
    uint8_t int_Weather_MIN; //min


    int8_t wthr_currentTemp;
    uint16_t wthr_currentWeatherID;
    int8_t wthr_DailyHigh;
    int8_t wthr_DailyLow;
    int16_t wthr_DailyWeatherID;
    int16_t wthr_DailyPoP;
    uint32_t wthr_sunrise,wthr_sunset;

    uint32_t ALIVESINCE;
};

extern struct Screen myScreen;


String breakString(String *inputstr,String token);



//automatically detect arduino type
#if defined (ARDUINO_ARCH_ESP8266)
  #define _USE8266 1
  #define _ADCRATE 1023
#elif defined(ESP32)
  #define _USE32
  #define _ADCRATE 4095
#else
  #error Arduino architecture unrecognized by this code.
#endif

#endif