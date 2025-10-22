#ifndef SERVER_HPP
#define SERVER_HPP

//#define _DEBUG 0

#include <Arduino.h>
#include <WiFiClient.h>

// Forward declarations - avoid circular includes since globals.hpp includes this file
struct STRUCT_CORE;
class WeatherInfoOptimized;
class Devices_Sensors;


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
  #include <WiFiClientSecure.h>
  extern WebServer server;
#endif



//declared as global constants
extern STRUCT_CORE I;

#ifdef _USEWEATHER
  extern WeatherInfoOptimized WeatherData;
#endif

#ifndef _ISPERIPHERAL
  extern uint32_t LAST_BAR_READ;
  extern uint32_t LAST_BAT_READ;
  extern double batteryArray[48];
  extern double LAST_BAR;
#endif
// SECSCREEN and HourlyInterval are now members of Screen struct (I.SECSCREEN, I.HourlyInterval)




#ifdef _DEBUG
extern uint16_t TESTRUN;
extern uint32_t WTHRFAIL;
#endif

void SerialWrite(String);
bool WifiStatus(void);
int16_t connectWiFi();
int16_t tryWifi(uint16_t delayms = 250, uint16_t retryLimit = 50, bool checkCredentials = true);
void connectSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP);
void APStation_Mode();

String urlEncode(const String& str);

String getCert(String filename);
bool Server_Message(String &URL, String &payload, int &httpCode);
bool Server_SecureMessage(String& URL, String& payload, int& httpCode,  String& cacert);
void handleReboot();
void handleNotFound();
void handlePost();
void handleRoot(void);
void handleALL(void);
void handlerForRoot(bool allsensors=false);
void handleREQUESTUPDATE();
void handleCLEARSENSOR();
void handleTIMEUPDATE();
void handleREQUESTWEATHER();
void handleRETRIEVEDATA();
void handleRETRIEVEDATA_MOVINGAVERAGE();
void handleFLUSHSD();
void handleSETWIFI();
void handleSTATUS();
void addPlotToHTML(uint32_t t[], double v[], byte N, uint64_t deviceMAC, uint8_t snsType, uint8_t snsID);
void serverTextHeader();
void serverTextClose(int htmlcode=200, bool asHTML=true);
// Weather configuration handlers
void handleWeather();
void handleWeather_POST();
void handleWeatherRefresh();
void handleWeatherZip();
void handleWeatherAddress();
void handleCONFIG();
void handleCONFIG_POST();
void handleCONFIG_DELETE();
void handleREADONLYCOREFLAGS();
void handleGSHEET();
void handleGSHEET_POST();
void handleGSHEET_UPLOAD_NOW();
void handleREQUEST_BROADCAST();
void handleTimezoneSetup();
void handleTimezoneSetup_POST();
bool getTimezoneInfo(int32_t* utc_offset, bool* dst_enabled, uint8_t* dst_start_month, uint8_t* dst_start_day, uint8_t* dst_end_month, uint8_t* dst_end_day);
void handleSDCARD();
void handleSDCARD_DELETE_DEVICES();
void handleSDCARD_DELETE_SENSORS();
void handleSDCARD_STORE_DEVICES();
void handleSDCARD_DELETE_SCREENFLAGS();
void handleSDCARD_DELETE_WEATHERDATA();
void handleSDCARD_SAVE_SCREENFLAGS();
void handleSDCARD_SAVE_WEATHERDATA();
void handleSDCARD_TIMESTAMPS();
void handleERROR_LOG();
void handleREBOOT_DEBUG();
void handleSDCARD_DELETE_ERRORLOG();
void handleSDCARD_DELETE_TIMESTAMPS();
void handleSDCARD_DELETE_GSHEET();
void handleDeviceViewer();
void handleDeviceViewerNext();
void handleDeviceViewerPrev();
void handleDeviceViewerPing();
void handleDeviceViewerDelete();
bool handlerForWeatherAddress(String street, String city, String state, String zipCode);
bool SendData(struct SnsType *S, bool forceSend=false);
bool connectToWiFi(const String& ssid, const String& password, const String& lmk_key);
void apiConnectToWiFi();
void apiScanWiFi();
void apiLookupLocation();
void apiDetectTimezone();
void apiSaveTimezone();
void apiGetSetupStatus();
void handleInitialSetup();
void handleApiCompleteSetup();

String getWiFiModeString();

// Generate AP SSID based on MAC address: "SensorNet-" + last 3 bytes of MAC in hex
String generateAPSSID();

// Server route setup function
void setupServerRoutes();

    // Address to coordinates conversion
    bool getCoordinatesFromAddress(const String& street, const String& city, const String& state, const String& zipCode);
    bool getCoordinatesFromZipCode(const String& zipCode);
    bool getCoordinatesFromZipCodeFallback(const String& zipCode);


#endif