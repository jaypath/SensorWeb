#ifndef VOLTAGE_CLOCK_HPP
#define VOLTAGE_CLOCK_HPP

#define WEATHER_CLOCK_PIN 26
#define TEMP_CLOCK_PIN 25

#define MIN_TEMP_VOLTAGE 0
#define MAX_TEMP_VOLTAGE 230

#define MIN_TEMP 20.0
#define MAX_TEMP 100.0

void update_weather_clock(long worst_weather_id);
void update_temp_clock(double current_temp);

#endif