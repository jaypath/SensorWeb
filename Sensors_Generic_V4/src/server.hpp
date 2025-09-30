#ifndef SERVER_HPP
#define SERVER_HPP

#define NUMSERVERS 3

#include <Arduino.h>
#include <header.hpp>
#include <sensors.hpp>
#include "../../GLOBAL_LIBRARY/timesetup.hpp"
#include "../../GLOBAL_LIBRARY/globals.hpp"
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
  #include "../../GLOBAL_LIBRARY/BootSecure.hpp"
  extern WebServer server;
  extern WiFiClient wfclient;
  extern HTTPClient http;
#endif




extern struct SensorVal Sensors[SENSORNUM];
extern struct STRUCT_PrefsH Prefs;
extern struct STRUCT_CORE I;

struct IP_TYPE {
  uint32_t IP;
  int server_status;
};

extern IP_TYPE SERVERIP[NUMSERVERS];

// Use shared Prefs instead of legacy WiFi_type

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
void handleReboot(void);
void handleRoot(void);
void handleNotFound(void);
void handleSetThreshold(void);
void handleUpdateSensorParams(void);
void handleUpdateAllSensorReads(void);
void handleUpdateSensorRead(void);
void handleNext(void);
void handleLast(void);
void handleWiFiConfig_RESET(void);
void handleWiFiConfig(void);
void handleWiFiConfig_POST(void);
void handleCONFIG(void);
void handleCONFIG_POST(void);



bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum);
char* strPad(char* str, char* pad, byte L);
bool SendData(struct SnsType*);
void Byte2Bin(uint8_t value, char* output, bool invert = false);
bool inIndex(byte lookfor,byte used[],byte arraysize);
void connectWiFi();
bool wait_ms(uint16_t ms);
String urlEncode(const String& s);
String macToHexNoSep();
void startSoftAP();
String generateAPSSID();
void setupServerRoutes();
#endif