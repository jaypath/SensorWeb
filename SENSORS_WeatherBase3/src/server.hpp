#ifndef SERVER_HPP
#define SERVER_HPP

//#define _DEBUG 0

#include <Arduino.h>
#include <WiFiClient.h>

#include "timesetup.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"
#include "globals.hpp"
#include "Weather.hpp"
#include "SDCard.hpp"


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
  #include <esp_wifi.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  extern WebServer server;
#endif


struct IP_TYPE {
  IPAddress IP;
  int server_status;
};


struct WiFi_type {
  //IPAddress DHCP;  // 4 byte,   4 in total
  //IPAddress GATEWAY;// 4 bytes, 8 in total
  //IPAddress DNS; //4 bytes, 16 in total
  //IPAddress SUBNET;
  byte MYIP[4]; //4 bytes
  byte MAC[6];
  byte SSID[33];
  byte PWD[65];
  bool HAVECREDENTIALS;   
  uint8_t statusCode; //from R to L... bit 0  = sensitive info required/sent if 1; bit 1 response required if 1; bit 2 request for sensitive info
};


//declared as global constants
extern time_t ALIVESINCE;
extern Screen I;
extern uint32_t LAST_WEB_REQUEST;
extern WeatherInfo WeatherData;
extern uint32_t LAST_BAR_READ;
extern uint32_t LAST_BAT_READ;
extern double batteryArray[48];
extern double LAST_BAR;
extern uint8_t SECSCREEN;
extern uint8_t HourlyInterval;




#ifdef _DEBUG
extern uint16_t TESTRUN;
extern uint32_t WTHRFAIL;
#endif








void SerialWrite(String);
bool WifiStatus(void);
String getCert(String filename);
bool Server_Message(String &URL, String &payload, int &httpCode);
bool Server_SecureMessage(String& URL, String& payload, int& httpCode,  String& cacert);
void handleReboot();
void handleNotFound();
void handleGETSTATUS();
void handlePost();
void handleRoot(void);
void handleALL(void);
void handlerForRoot(bool allsensors=false);
void handleREQUESTUPDATE();
void handleCLEARSENSOR();
void handleTIMEUPDATE();
void handleREQUESTWEATHER();
void handleUPDATEDEFAULTS();
void handleRETRIEVEDATA();
void handleFLUSHSD();
void handleSETWIFI();

void serverTextHeader();
void serverTextWiFiForm();
void serverTextClose(int htmlcode=200, bool asHTML=true);

bool SendData(struct SensorVal*);

byte connectWiFi();

#endif