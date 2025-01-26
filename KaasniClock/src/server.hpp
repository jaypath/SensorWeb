#ifndef SERVER_HPP
#define SERVER_HPP

//#define _DEBUG 0

#include <Arduino.h>
#include <timesetup.hpp>
#include <globals.hpp>
#include <SDCard.hpp>


//Server requests time out after 2 seconds
#define TIMEOUT_TIME 2000

//this server
#ifdef _USE8266
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>

  extern ESP8266WebServer server;
#endif
#ifdef _USE32
  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>
  extern WebServer server;
#endif

//declared as global constants
extern Screen myScreen;



bool getWeather() ;

#endif