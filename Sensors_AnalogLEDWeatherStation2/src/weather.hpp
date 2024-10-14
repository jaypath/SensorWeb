#ifndef WEATHER_HPP
#define WEATHER_HPP

#define WEATHER_UPDATE_INTERVAL (3600 * 1000) //ms

#define FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT 13 //Weather IDs for next 8 hours, current temp and temp in 4 hours, currentHour, sunrise, sunset

#include "util.hpp"

extern double sunrise;
extern double sunset;

typedef struct weather_info_t {
  //Worst weather ID in the next 8 hours
  long worst_weather_id;
  double current_temp;
  //Temp in 4 hours
  double next_temp;
  double sunrise;
  double sunset;
  byte current_hour;

  void set_fields(long worst_weather_id_, double current_temp_, double next_temp_, double current_hour_, double sunrise, double sunset) {
    this->worst_weather_id = worst_weather_id_;
    this->current_temp = current_temp_;
    this->next_temp = next_temp_;
    this->current_hour = (byte) current_hour_;
    this->sunrise = sunrise;
    this->sunset=sunset;

    #ifdef DEBUG
      Serial.print("Worst weather ID: ");
      Serial.print(worst_weather_id_);
      Serial.print(", current temp: ");
      Serial.print(current_temp_);
      Serial.print(", next temp: ");
      Serial.println(next_temp_);
    #endif
  }

  inline long get_weather_id() {
    return this->worst_weather_id;
  }
  inline double get_current_temp() {
    return this->current_temp;
  }
  inline double get_next_temp() {
    return this->next_temp;
  }
  inline double get_time() {
    return this->current_hour;
  }

} weather_info_t;

bool get_weather_data(weather_info_t& weather_info);


#define WEATHER_CATEGORY_COUNT 7

extern float div_100_multiplier;
extern float weather_ids_worst_to_best[WEATHER_CATEGORY_COUNT];
extern String request_url;

#endif

extern unsigned long last_weather_update;
