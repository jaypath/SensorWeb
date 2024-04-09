#include "util.hpp"
#include "voltage_clock.hpp"

#include <Arduino.h>

struct {
  uint8_t sunny = 5;
  uint8_t slight_clouds = 41;
  uint8_t clouds = 75;
  uint8_t light_rain = 107;
  uint8_t heavy_rain = 148;
  uint8_t light_snow = 180;
  uint8_t heavy_snow = 215;
  } weather_voltages;

#define SET_WEATHER(type) dacWrite(WEATHER_CLOCK_PIN, weather_voltages.type)

void update_weather_clock(long worst_weather_id) {
  
#ifdef DEBUG
Serial.println("WEATHER VOLTAGES: " + String(weather_voltages.slight_clouds));

  Serial.println("\nUpdating weather clock: " + String(worst_weather_id) + '\n');
  #endif
  
  //Clear sky
  if (worst_weather_id == 800) {
    SET_WEATHER(sunny);
  }
  //Slight clouds
  else if (worst_weather_id > 800 && worst_weather_id <= 803) {
    SET_WEATHER(slight_clouds);
  }
  //Overcast clouds
  else if (worst_weather_id == 804) {
    SET_WEATHER(clouds);
  }
  //Drizzle
  else if (worst_weather_id >= 300 && worst_weather_id < 400) {
    SET_WEATHER(light_rain);
  }
  //Heavy rain
  else if ((worst_weather_id >= 502 && worst_weather_id <= 504) || worst_weather_id == 522) {
    SET_WEATHER(heavy_rain);
  }
  //Light rain
  else if (worst_weather_id >= 500 && worst_weather_id < 600) {
    SET_WEATHER(light_rain);
  }
  //Thunderstorms
  else if (worst_weather_id >= 200 && worst_weather_id < 300) {
    SET_WEATHER(heavy_rain);
  }
  //Heavy snow
  else if (worst_weather_id == 602 || worst_weather_id == 622) {
    SET_WEATHER(heavy_snow);
  }
  //Light snow
  else if (worst_weather_id >= 600 && worst_weather_id < 700) {
    SET_WEATHER(light_snow);
  }
  //Bad weather events that haven't been implemented yet (like volcanic ash)
  //are implemented as thunderstorms
  else if ( (worst_weather_id >= 700 || worst_weather_id < 800) &&
       (worst_weather_id != 701 && worst_weather_id != 741)
    ) {
      SET_WEATHER(heavy_rain);
    }
}
void update_temp_clock(double current_temp) {
  //Set current temp within valid temperature range
  current_temp = max(MIN_TEMP, current_temp);
  current_temp = min(MAX_TEMP, current_temp);

  uint voltage = floor( (double) ( (MAX_TEMP_VOLTAGE - MIN_TEMP_VOLTAGE) * (current_temp - MIN_TEMP) / (MAX_TEMP - MIN_TEMP) ) ) + MIN_TEMP_VOLTAGE;
  dacWrite(TEMP_CLOCK_PIN, voltage);
}