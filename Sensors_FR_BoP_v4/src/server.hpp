#ifndef SERVER_HPP
#define SERVER_HPP

#define NUMSERVERS 3

#include <Arduino.h>

#include "globals.hpp"
#include "sensors.hpp"
#include "utility.hpp"

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
  #include "BootSecure.hpp"
  extern WebServer server;
  extern WiFiClient wfclient;
  extern HTTPClient http;
#endif


extern Devices_Sensors Sensors;

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
void handleSetup(void);
void handleSetup_POST(void);
void handleDeviceViewer(void);
void handleDeviceViewerNext(void);
void handleDeviceViewerPrev(void);
void handleDeviceViewerPing(void);
void handleDeviceViewerDelete(void);
void handleForceBroadcast(void);
void handleStatus(void);
void handleTimezoneSetup(void);
void handleTimezoneSetup_POST(void);
void handleCONFIG(void);
void handleCONFIG_POST(void);

void serverTextHeader();
void serverTextClose(int htmlcode, bool asHTML);
void addWiFiConfigForm();
bool getTimezoneInfo(int32_t* utc_offset, bool* dst_enabled, uint8_t* dst_start_month, uint8_t* dst_start_day, uint8_t* dst_end_month, uint8_t* dst_end_day);

bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum);
bool SendData(struct SnsType*);

int16_t connectWiFi();
bool wait_ms(uint16_t ms);
static String urlEncode(const String& s);
static String macToHexNoSep();
void APStation_Mode();
void startSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP);
String generateAPSSID();
void setupServerRoutes();
#endif