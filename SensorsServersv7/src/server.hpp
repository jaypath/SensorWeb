#ifndef SERVER_HPP
#define SERVER_HPP

//#define _DEBUG 0


#include <Arduino.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
struct STRUCT_CORE;
class WeatherInfoOptimized;
class Devices_Sensors;
struct ArborysDevType;
struct ArborysSnsType;


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

//the following are to handle memory management for the secure message function
#include <memory>
// This "Deleter" ensures unique_ptr calls free() instead of delete
struct MemoryDeleter {
  void operator()(const void* p) const { 
      if (p) free((void*)p); 
  }
};

  
struct HTTPMessage {
  // --- Managed Memory (Auto-cleanup for ALL char buffers) ---
  std::unique_ptr<char[], MemoryDeleter> url;
  std::unique_ptr<char[], MemoryDeleter> payload; //return payload from server
  std::unique_ptr<char[], MemoryDeleter> cacert;
  std::unique_ptr<char[], MemoryDeleter> method;
  std::unique_ptr<char[], MemoryDeleter> contentType;
  std::unique_ptr<char[], MemoryDeleter> body; //send payload to server
  std::unique_ptr<char[], MemoryDeleter> extraHeaders;

  // --- External References (Calling function manages these) ---
  JsonDocument* responseDoc = nullptr;
  const JsonDocument* filter = nullptr;

  // --- Standard Data ---
  size_t payloadSize = 0;
  int16_t httpCode = 0;
  bool processStream = false;
  bool allowInsecure = false;
  bool success = false;
  bool usePSRAM = false;
  uint16_t timeout = 0; // 15 second timeout for geocoding
  // --- Logic ---
  bool initPayload(size_t size) {
      if (size == 0) return false;
      this->payloadSize = size;
      
      char* rawPtr = this->usePSRAM ? (char*)ps_malloc(size) : (char*)malloc(size);
      if (rawPtr == nullptr) return false;

      this->payload.reset(rawPtr); 
      return true;
  }

  bool resizePayload(size_t newSize) {
    if (newSize == 0) return false;
    
    // 1. Get the current raw pointer from the unique_ptr
    char* oldPtr = payload.release(); // .release() gives us the pointer and stops unique_ptr from managing it
    char* newPtr = nullptr;

    // 2. Perform the realloc based on where the memory lives
    if (this->usePSRAM) {
        newPtr = (char*)ps_realloc(oldPtr, newSize);
    } else {
        newPtr = (char*)realloc(oldPtr, newSize);
    }

    // 3. Check if realloc failed
    if (newPtr == nullptr) {
        // If realloc fails, the OLD memory is still valid! 
        // We must give it back to the unique_ptr so it doesn't leak.
        payload.reset(oldPtr); 
        return false;
    }

    // 4. Success! Give the new (possibly moved) pointer to unique_ptr
    payload.reset(newPtr);
    this->payloadSize = newSize;
    return true;
}

  // Helper to set any string field safely
  void setField(std::unique_ptr<char[], MemoryDeleter>& field, const char* value) {
      if (!value) return;
      char* buf = strdup(value);
      if (buf) field.reset(buf);
  }

  // Sugar for setting specific fields
  void setUrl(const char* val) { setField(url, val); }
  void setMethod(const char* val) { setField(method, val); }
  void setContentType(const char* val) { setField(contentType, val); }
  void setExtraHeaders(const char* val) { setField(extraHeaders, val); }
  void setCacert(const char* val) { setField(cacert, val); }
  void setBody(const char* val) { setField(body, val); }

  // Rule of Five compliance
  HTTPMessage() = default;
  HTTPMessage(const HTTPMessage&) = delete;
  HTTPMessage& operator=(const HTTPMessage&) = delete;
};


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
int8_t CheckWifiStatus(bool trytoconnect=false);
int16_t connectWiFi(uint8_t retryLimit=20);
bool closeUDP(bool returnStatus);
bool connectUDP();
int16_t tryWifi(uint16_t delayms = 250, bool checkCredentials = true);
void connectSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP);
void APStation_Mode();
String WiFiEventtoString(WiFiEvent_t event);
String urlEncode(const String& str);
String getPublicIP();

String getCert(String filename);
bool SendHTTPMessage(HTTPMessage& M);
// cacert: SD path (e.g. "/Certificates/NOAA.crt") or "" / "*" / "bundle" to use embedded CA bundle (requires sdkconfig.defaults)
void handleReboot();
void handleNotFound();

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
void serverTextHeader(String pagename);
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
#ifdef _ISPERIPHERAL
void handleSENSOR_UPDATE_POST();
void handleSENSOR_READ_SEND_NOW();
void handleSNS_CALIBRATION_SOIL_CAPACITANCE();
void handleSensorSetup();
#endif
void handleGSHEET();
void handleGSHEET_POST();
void handleGSHEET_UPLOAD_NOW();
void handleGSHEET_SHARE_ALL();
void handleGSHEET_DELETE_ALL();
void handleREQUEST_BROADCAST();
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

void delayWithNetwork(uint16_t delayTime, uint8_t maxChecks);
uint8_t registerSensorData(uint64_t deviceMAC, IPAddress deviceIP, String devName, uint8_t devType, uint8_t devFlags, uint8_t snsType, uint8_t snsID, String snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, uint32_t sendingInt, uint8_t flags);


//raw send/receiving data
bool sendUDPMessage(const uint8_t* buffer,  IPAddress ip, uint16_t bufferSize=0, const char* msgType="Unknown");
bool receiveUDPMessage();
void handlePost();
void registerUDPMessage(IPAddress ip, const char* messageType);
void registerUDPSend(IPAddress ip, const char* messageType);
void registerHTTPSend(IPAddress ip, const char* messageType);
void registerHTTPMessage(const char* messageType);


//sending data
void wrapupSendData(ArborysSnsType* S);
bool isDeviceSendTime(ArborysDevType* D, bool forceSend);
bool checkThisSensorTime(ArborysSnsType* S);
bool isSensorSendTime(int16_t snsIndex);
int16_t sendHTTPJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType);
int16_t sendHTTPJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType);
uint8_t sendAllSensors(bool forceSend, int16_t sendToDeviceIndex, bool useUDP);
bool SendData(int16_t snsIndex, bool forceSend=false, int16_t sendToDeviceIndex=-1, bool useUDP=false);

//send json messages
int16_t sendMSG_ping(IPAddress& ip, bool viaHTTP);
int16_t sendMSG_DataRequest(int16_t deviceIndex, int16_t snsIndex, bool viaHTTP);
int16_t sendMSG_DataRequest(ArborysDevType* d, int16_t snsIndex, bool viaHTTP);

//add json handlers
void JSONbuilder_pingMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool viaHTTP, bool isAck);
void JSONbuilder_DataRequestMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool viaHTTP, int16_t snsIndex);
void JSONbuilder_sensorMSG(ArborysSnsType* S, char* jsonBuffer, uint16_t jsonBufferSize, bool viaHTTP);
void JSONbuilder_sensorMSG_all(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP);
String JSONbuilder_sensorObject(ArborysSnsType* S);
uint16_t JSONbuilder_encodeHTTP(String& jsonBuffer);

//json processors
void processJSONMessage(String& postData, String& responseMsg);
void processJSONMessage_ping(JsonObject root, String& responseMsg, bool isAck=false);
void processJSONMessage_DataRequest(JsonObject root, String& responseMsg);
void processJSONMessage_sensorData(JsonObject root, String& responseMsg);
int16_t processJSONMessage_addDevice(JsonObject root, String& responseMsg);
static void handleSingleSensor(ArborysDevType* dev, JsonObject sensor, String& responseMsg);

void WiFiEvent(WiFiEvent_t event);

bool connectToWiFi(const String& ssid, const String& password, const String& lmk_key);
void apiConnectToWiFi();
void apiScanWiFi();
void apiClearWiFi();
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
    bool getCoordinatesFromAddress(const String& zipCode, const String& street = "1 Main St", const String& city = "Unknown", const String& state = "MA");
    bool getCoordinatesFromZipCode(const String& zipCode);

#endif