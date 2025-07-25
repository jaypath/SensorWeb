#ifndef SERVER_HPP
#define SERVER_HPP

//#define _DEBUG 0

#include <Arduino.h>
#include <WiFiClient.h>

#include "timesetup.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"
#include "globals.hpp"
#include "Weather_Optimized.hpp"
#include "SDCard.hpp"
#include "AddESPNOW.hpp"


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
  uint32_t IP;
  int server_status;
};


//declared as global constants
extern STRUCT_CORE I;
extern uint32_t LAST_WEB_REQUEST;
extern WeatherInfoOptimized WeatherData;
extern uint32_t LAST_BAR_READ;
extern uint32_t LAST_BAT_READ;
extern double batteryArray[48];
extern double LAST_BAR;
// SECSCREEN and HourlyInterval are now members of Screen struct (I.SECSCREEN, I.HourlyInterval)




#ifdef _DEBUG
extern uint16_t TESTRUN;
extern uint32_t WTHRFAIL;
#endif








void SerialWrite(String);
bool WifiStatus(void);
int16_t connectWiFi();
int16_t tryWifi(uint16_t delayms = 250);

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
void handleRETRIEVEDATA_MOVINGAVERAGE();
void handleFLUSHSD();
void handleSETWIFI();
void addPlotToHTML(uint32_t t[], double v[], byte N, uint64_t deviceMAC, uint8_t snsType, uint8_t snsID);
void serverTextHeader();
void serverTextWiFiForm();
void serverTextClose(int htmlcode=200, bool asHTML=true);

bool SendData(struct SensorVal*);

void connectSoftAP();

// Generate AP SSID based on MAC address: "SensorNet-" + last 3 bytes of MAC in hex
String generateAPSSID();

#endif