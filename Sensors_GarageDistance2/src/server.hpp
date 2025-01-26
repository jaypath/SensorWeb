#ifndef SERVER_HPP
#define SERVER_HPP

#define NUMSERVERS 3

#include <Arduino.h>
#include <header.hpp>
#include <sensors.hpp>
#include <timesetup.hpp>
#include "ArduinoOTA.h"

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
extern struct SensorVal Sensors[SENSORNUM];
extern time_t ALIVESINCE;

struct IP_TYPE {
  byte IP[4];
  int server_status;
};

extern IP_TYPE SERVERIP[NUMSERVERS];

struct WiFi_type {
  uint8_t DHCP[4];  // 4 byte,   4 in total
  uint8_t GATEWAY[4];// 4 bytes, 8 in total
  uint8_t DNS[4]; //4 bytes, 16 in total
  uint8_t DNS2[4]; //4 bytes, 16 in total
  uint8_t SUBNET[4];
  uint8_t MYIP[4]; //4 bytes
  uint8_t status;
};

extern WiFi_type WIFI_INFO;

//extern const char HTTP_OK_response_header[60];
#if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
void initHVAC(void);
#endif

String IP2String(byte IP[]);
void assignIP(byte ip[4],IPAddress IPA);
void assignIP(byte ip[4], byte m1, byte m2, byte m3, byte m4);
bool WifiStatus(void);
#ifdef _USE32
void onWiFiEvent(WiFiEvent_t event);
#endif

bool Server_Message(String URL, String* payload, int* httpCode);
void handleREBOOT(void);
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
void connectWiFi();
bool wait_ms(uint16_t ms);

#endif