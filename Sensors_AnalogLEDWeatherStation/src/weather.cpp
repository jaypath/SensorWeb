#include <WiFi.h>
#include <HTTPClient.h>

#include "util.hpp"
#include "weather.hpp"

unsigned long last_weather_update = 4294967290; //some arbitrarily huge value near the limit

float weather_ids_worst_to_best[WEATHER_CATEGORY_COUNT] = { 7, 2, 6, 5, 3, 8 };
float div_100_multiplier = 1 / 100;



uint8_t hundreds_place(uint weather_id) {
  return (uint8_t) floor( (float)weather_id * div_100_multiplier );
}

//Returns true if weather ID 1 is worse than weather ID 2
bool weather_id_is_worse(long weather_id_1, long weather_id_2) {
  if (weather_id_1 == weather_id_1) return true;

  uint8_t first_hundreds_place = hundreds_place(weather_id_1);
  uint8_t second_hundreds_place = hundreds_place(weather_id_2);

  //Handle the edge cases
  //If one is clear sky, the other is worse
  if (weather_id_1 == 800) return false;
  //If weather ID 1 is 701 (mist) or 741 (fog), then weather ID 2 is worse (or equal to) weather ID 1
  if (weather_id_1 == 701 || weather_id_1 == 741) return false;

  //Reverse order
  if (weather_id_2 == 800) return true;
  if (weather_id_2 == 701 || weather_id_2 == 741) return true;

  //Are there hundreds places equal? (Meaning that their weather categories are the same)
  if (first_hundreds_place == second_hundreds_place) return weather_id_1 > weather_id_2;

  //Otherwise, see which hundreds place is first in the weather IDs array and thus which is worse
  long weather_id_1_ind = 0;
  long weather_id_2_ind = 0;
  uint weather_category_list_ind = 0;

  while (weather_category_list_ind < WEATHER_CATEGORY_COUNT) {
    if (weather_ids_worst_to_best[weather_category_list_ind] == first_hundreds_place) {
      weather_id_1_ind = weather_category_list_ind;
    }
    if (weather_ids_worst_to_best[weather_category_list_ind] == second_hundreds_place) {
      weather_id_2_ind = weather_category_list_ind;
    }
    weather_category_list_ind += 1;
  }
//  Serial.println(String(weather_id_1) + ": " + String(first_hundreds_place) = ", " + String(weather_id_2) + ": " + String(second_hundreds_place));

  return weather_id_1_ind < weather_id_2_ind;
}

//First we get 8 weather ids, then the current temp, then the temp in 4 hours, then hour
void parse_request_into_weather_info(weather_info_t& weather_info, String& data) {
  
  String current_number;
  double nums[FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT];
  uint current_num_ind = 0;
  uint current_string_ind = 0;

  while (current_string_ind < data.length()) {
    if (current_num_ind >= FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT) break;
    //We've encountered the end of a number
    if (data[current_string_ind] == ';') {
      nums[current_num_ind++] = current_number.toDouble();
      current_number = "";
    }
    else current_number += data[current_string_ind];

    #ifdef DEBUG
      if (data[current_string_ind] == ';') Serial.println("New num: " + String(nums[current_num_ind - 1]));
    #endif

    current_string_ind += 1;
  }

  //Get worst weather ID in next 8 hours
  long worst_weather_id = nums[0];
  for (uint ind = 1; ind < FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT - 3; ind += 1) { //-3 because we have hour and two temps at the end
    if (weather_id_is_worse(nums[ind], worst_weather_id))  worst_weather_id = nums[ind];
  }

  //Worst weather ID, current temp, temp in 4 hours
  weather_info.set_fields(worst_weather_id, nums[FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT - 3], nums[FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT - 2],nums[FETCHED_WEATHER_AND_TEMP_FIELDS_COUNT - 1]);
}

//Thank you to Random Nerd Tutorials for this code!
String HTTP_GET_request(const char* URL) {
  WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  http.begin(client, URL);
  
  // Send HTTP POST request
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  #ifdef DEBUG
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    
    return "-1";
  }
  #endif
  
  payload = http.getString();

  // Free resources
  http.end();

  return payload;
}


bool get_weather_data(weather_info_t& weather_info) {
  if (!(millis() < last_weather_update || millis() - last_weather_update >= WEATHER_UPDATE_INTERVAL)) return false;
String request_url = "http://192.168.68.93/REQUESTWEATHER?hourly_weatherID=0&hourly_weatherID=1&hourly_weatherID=2&hourly_weatherID=3&hourly_weatherID=4&hourly_weatherID=5&hourly_weatherID=6&hourly_weatherID=7&hourly_temp=0&hourly_temp=4&hour=0";

  String data = HTTP_GET_request(request_url.c_str());

  //An error occurred
  if (data == "-1") return false;

  parse_request_into_weather_info(weather_info, data);
  last_weather_update = millis();

  return true;
}
