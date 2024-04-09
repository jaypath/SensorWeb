#ifndef SERVER_HPP
#define SERVER_HPP

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "animator.hpp"
#include "weather.hpp"


extern WebServer server;
extern HTTPClient http;
extern   WiFiClient wfclient;

//extern const char HTTP_OK_response_header[60];

bool SendData(void);
void handleRoot(weather_info_t& weather_info);
void handleUPDATEWEATHER();
void handleSETWEATHER(weather_info_t& weather_info,LED_Weather_Animation& animator);
void handleNotFound();

//void handleClient(WiFiClient& client, LED_Weather_Animation& animator);

#endif