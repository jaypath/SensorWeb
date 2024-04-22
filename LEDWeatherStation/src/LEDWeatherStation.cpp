#define DEBUG
#include <Arduino.h>
#include <FastLED.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "animator.hpp"
#include "setLED.hpp"
#include "server.hpp"
#include "temp_display.hpp"
#include "util.hpp"
#include "voltage_clock.hpp"
#include "weather.hpp"


uint left_column_lengths[3] = { 17, 22, 19 };
uint right_column_lengths[3] = { 17, 22, 19 };

#define ARDNAME "LEDWeather"
#define TOP_STRIP_LENGTH 6
#define LEFT_LED_COUNT 64
#define RIGHT_LED_COUNT 58

CRGB Left_LEDs[LEFT_LED_COUNT];
CRGB Right_LEDs[RIGHT_LED_COUNT];


CRGB* top_strip = &(Left_LEDs[58]);
CRGB* left_led_pointers[3] = { Left_LEDs, &( Left_LEDs[ left_column_lengths[0] ] ), &( Right_LEDs[ right_column_lengths[0] + right_column_lengths[1] ] )};
CRGB* right_led_pointers[3] = { Right_LEDs, &( Right_LEDs[ right_column_lengths[0] ] ), &( Left_LEDs[ left_column_lengths[0] + left_column_lengths[1] ] )};

LED_Weather_Animation animator(LEFT);
Temp_Displayer temp_displayer(RIGHT, MIN_TEMP, MAX_TEMP);

// Replace with your network credentials
const char* ssid = "CoronaRadiata_Guest";
const char* password = "snakesquirrel";

weather_info_t weather_info;

uint32_t LASTSERVERUPDATE = 0;


#define RIGHT_DATA_PIN 12
#define LEFT_DATA_PIN 13

void main_handleRoot();
void main_handleSETWEATHER();
void main_handleUPDATEWEATHER();
void setup() {
  init_led_info(top_strip, TOP_STRIP_LENGTH, left_led_pointers, right_led_pointers, left_column_lengths, right_column_lengths);

  // if analog input pin 0 is unconnected, random analog
  // noise will cause the call to randomSeed() to generate
  // different seed numbers each time the sketch runs.
  // randomSeed() will then shuffle the random function.
  //Credit to https://www.arduino.cc/reference/en/language/functions/random-numbers/random/ for the comment explaining this.
  randomSeed(analogRead(32));
  
  FastLED.addLeds<WS2812B, LEFT_DATA_PIN, GRB>(Left_LEDs, LEFT_LED_COUNT).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<WS2812B, RIGHT_DATA_PIN, GRB>(Right_LEDs, RIGHT_LED_COUNT).setCorrection(TypicalLEDStrip);

  #ifdef DEBUG
    Serial.begin(115200);
    Serial.println("Fast LED and random seed setup complete.");
  #endif

  WiFi.begin(ssid, password);
  #ifdef DEBUG
    Serial.print("Connecting to WiFi");
  #endif

bool iswhite = true;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (iswhite)     fill_top_strip(0xFFFFFF);
    else fill_top_strip(0x000000);

  #ifdef DEBUG
    Serial.print(".");
  #endif
  }
  #ifdef DEBUG
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  #endif

   ArduinoOTA.setHostname(ARDNAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    #ifdef DEBUG
    Serial.println("OTA started");
    #endif
  });
  ArduinoOTA.onEnd([]() {
    #ifdef DEBUG
    Serial.println("OTA End");
    #endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef DEBUG
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
    #endif

  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
  });
  ArduinoOTA.begin();
    #ifdef DEBUG
      Serial.println("OTA OK.");      
    #endif



    server.on("/", main_handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/UPDATEWEATHER",main_handleUPDATEWEATHER);
    server.on("/SETWEATHER",main_handleSETWEATHER);
    server.onNotFound(handleNotFound);
    server.begin();

  #ifdef DEBUG
    Serial.println("Server has been started.");
  #endif

  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);

  animator.set_rainy_status(HEAVY_RAIN);
  fill_top_strip(0x000000);
  fill_side(LEFT,0x000000);
  fill_side(RIGHT,0x000000);
};

void main_handleUPDATEWEATHER() {
  handleUPDATEWEATHER();
  
}
void main_handleRoot() {
  #ifdef DEBUG
    Serial.print ("Test root");
    
  #endif
  handleRoot(weather_info);
  #ifdef DEBUG
    Serial.println(" completed Test");
    
  #endif
}
void main_handleSETWEATHER() {
  #ifdef DEBUG
  Serial.println("html setweather");
  #endif
  handleSETWEATHER(weather_info,animator);
}


void loop() {
  #ifdef DEBUG
    // unsigned long now = millis();
  #endif
  ArduinoOTA.handle();

  server.handleClient();
  
  bool updated_weather = get_weather_data(weather_info);


  if (updated_weather) animator.update_weather_status(weather_info.get_weather_id(), weather_info.get_time());
  
  if (animator.update_LEDs(weather_info.get_time()) || temp_displayer.update_LEDs(weather_info.get_current_temp(), weather_info.get_next_temp())) {
    FastLED.show();
  }

  #ifdef DEBUG
    // Serial.println("After updating animator LEDs: " + String(millis() - now));
  #endif

  if (updated_weather) {
    update_temp_clock(weather_info.get_current_temp());
    update_weather_clock(weather_info.get_weather_id());
  }

  #ifdef DEBUG
    // Serial.println("After updating clocks: " + String(millis() - now));
  #endif

/*  WiFiClient client = server.available();
  if (client) handleClient(client, animator);

  #ifdef DEBUG
    if (client) Serial.println("Client attempted to connect");
  
    // Serial.println("After checking for client on server: " + String(millis() - now));
  #endif
  */
 if (millis()-LASTSERVERUPDATE > 60*1000*10 || LASTSERVERUPDATE==0 || millis()<LASTSERVERUPDATE) {
  LASTSERVERUPDATE = millis();
  SendData();
 }
};
