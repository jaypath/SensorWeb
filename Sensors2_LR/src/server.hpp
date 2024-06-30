#ifndef SERVER_HPP
#define SERVER_HPP

#define NUMSERVERS 3

#include <Arduino.h>
#include <sensors.hpp>
#include <timesetup.hpp>

#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
//#define ESP_SSID "kandy-hispeed" // Your network name here
//#define ESP_PASS "manath77" // Your network password here

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
  extern WiFiClient wfclient;
  extern HTTPClient http;
#endif

extern bool KiyaanServer;
extern SensorVal Sensors[SENSORNUM];
extern time_t ALIVESINCE;

struct IP_TYPE {
  IPAddress IP;
  int server_status;
};

extern IP_TYPE SERVERIP[NUMSERVERS];


//extern const char HTTP_OK_response_header[60];

void handleRoot(void);
void handleNotFound(void);
void handleSETTHRESH(void);
void handleUPDATESENSORPARAMS(void);
void handleUPDATEALLSENSORREADS(void);
void handleUPDATESENSORREAD(void);
void handleNEXT(void);
void handleLAST(void);
bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum);
char* strPad(char* str, char* pad, byte L);
bool SendData(struct SensorVal*);
void Byte2Bin(uint8_t value, char* output, bool invert = false);
bool inIndex(byte lookfor,byte used[],byte arraysize);

#endif