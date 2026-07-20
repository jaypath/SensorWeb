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

#if _IS_SERVER_HUB
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

// Boot: 5 blocking tries × 45 s, then non-blocking AP.
// Runtime: wait WIFI_DOWN_AP_THRESHOLD_SEC before opening soft-AP for credential recovery;
// while AP is up, retry known STA credentials every WIFI_AP_STA_RECONNECT_SEC (time-debounced
// WiFi.begin; do not gate on WL_IDLE_STATUS). Soft-AP stays up until STA is usable.
// AP-mode ESP-NOW channel scans run only when credentials are missing (avoid RF hops during STA retry).
static constexpr uint8_t WIFI_BOOT_RETRY_LIMIT = 5;
static constexpr uint16_t WIFI_BOOT_TRY_MS = 45000;
static constexpr uint16_t WIFI_DOWN_AP_THRESHOLD_SEC = 300; // 5 minutes of continuous STA failure → enter AP+STA
static constexpr uint16_t WIFI_AP_STA_RECONNECT_SEC = 60;   // while in AP+STA, retry known SSID this often
// Prefer strongest BSSID for the configured SSID at connect time; re-evaluate periodically.
// Chosen BSSID is never persisted — only the best AP at the moment of selection.
static constexpr uint16_t WIFI_BSSID_OPTIMIZE_INTERVAL_SEC = 1800; // 30 minutes
static constexpr int8_t WIFI_BSSID_ROAM_MIN_IMPROVEMENT_DB = 6;    // hysteresis to avoid AP flapping
static constexpr uint16_t AP_ESP_NOW_STALE_PING_SEC = 60;
static constexpr uint16_t AP_CHANNEL_SCAN_IDLE_SEC = 120;
static constexpr uint16_t AP_CHANNEL_SCAN_STEP_MS = 100;
static constexpr uint8_t AP_WIFI_CHANNEL_MIN = 1;
static constexpr uint8_t AP_WIFI_CHANNEL_MAX = 13;
static constexpr char AP_STATION_PASSWORD[] = "S3nsor.N3t!";

enum WifiCheckMode : uint8_t {
  WIFI_CHECK_NORMAL = 0,
  WIFI_CHECK_BOOT = 1,
};

int8_t measureWifiLinkStatus();
int8_t CheckWifiStatus(WifiCheckMode mode = WIFI_CHECK_NORMAL);
// Single gate for HTTP/LAN/internet traffic (STA associated with router).
bool wifiReadyForNetwork();
bool softApRunning();
void updateRSSI(bool forceUpdate = false);
void syncDeviceIPFromWifi();
void startWifiConnectAsync();
void maybeOptimizeWifiBssid();
void enterAPStationMode();
void exitAPStationMode();
void maybeExitAPStationMode();
void syncInitialSetupState();
void resetEphemeralCoreWifiState();
void reconcileWifiStateAfterCoreLoad();
void serviceAPStationMode();
bool setWifiRfChannel(uint8_t channel);
void noteApModeServerPingResponse(uint8_t wifiChannel);
int16_t connectWiFi(uint8_t retryLimit=WIFI_BOOT_RETRY_LIMIT, uint16_t tryTimeoutMs=WIFI_BOOT_TRY_MS);
bool closeUDP(bool returnStatus);
bool connectUDP();
#ifdef _USEUDP
void refreshIGMPMembership();
void maybeRefreshIGMPMembership();
#endif
int16_t tryWifi(uint16_t delayms = WIFI_BOOT_TRY_MS, bool checkCredentials = true);
void connectSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP);
String WiFiEventtoString(WiFiEvent_t event);
String urlEncode(const String& str);
String getPublicIP(uint16_t timeoutMs = 10000);

bool SendHTTPMessage(HTTPMessage& M);
// cacert: SD path (e.g. "/Certificates/NOAA.crt") or "" / "*" / "bundle" to use embedded CA bundle (requires sdkconfig.defaults)
void handleReboot();
void handleNotFound();

void handleRoot(void);
void appendStandardPageNav(bool includeWiFiConfig = false);
void renderDeviceViewerPage();
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
void serverTextStreamBegin(int htmlcode=200, bool asHTML=true);
void serverTextFlush(bool force=false);
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
#if _IS_SERVER_HUB
void handleSENSOR_OVERRIDE_UPDATE();
#endif
#if _HAS_LOCAL_SENSORS
void handleSENSOR_UPDATE_POST();
void handleSENSOR_READ_SEND_NOW();
void handleSNS_CALIBRATION();
void handleSensorSetup();
#endif
void handleGSHEET();
void handleGSHEET_POST();
void handleGSHEET_UPLOAD_NOW();
void handleGSHEET_SHARE_ALL();
void handleGSHEET_DELETE_ALL();
void handleREQUEST_BROADCAST();
void handleREQUEST_BROADCAST_ESP();
void handleREQUEST_BROADCAST_UDP();
void handleSDCARD();
void handleSDCARD_DELETE_SENSORS();
void handleSDCARD_STORE_DEVICES();
void handleSDCARD_SAVE_SCREENFLAGS();
void handleSDCARD_SAVE_WEATHERDATA();
void handleSDCARD_SYSTEMLOG();
void handleERROR_LOG();
void handleREBOOT_DEBUG();
#ifdef _USESDCARD
void handleSDCARD_UPLOAD();
void handleSDCARD_UPLOADFile();
#endif
void handleSDCARD_DIR();
void handleSDCARD_DOWNLOAD();
void handleDeviceViewer();
void handleDeviceViewerNext();
void handleDeviceViewerPrev();
void handleDeviceViewerPing();
void handleDeviceViewerDelete();
void handleREGISTER_DEVICE();
void handleREGISTER_DEVICE_POST();
bool handlerForWeatherAddress(String street, String city, String state, String zipCode);

void delayWithNetwork(uint16_t delayTime, uint8_t maxChecks);
uint8_t registerSensorData(uint64_t deviceMAC, IPAddress deviceIP, String devName, uint8_t devType, uint8_t devFlags, uint8_t snsType, uint8_t snsID, String snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, uint32_t sendingInt, uint8_t flags);


//raw send/receiving data
bool sendUDPMessage(const uint8_t* buffer,  IPAddress ip, uint16_t bufferSize=0, const char* msgType="Unknown");
bool receiveUDPMessage();
void handlePost();
void handlePostEnc();
#ifdef _USE32
void handlePostEncRaw();
void absorbBinaryPostRaw(size_t maxLen);
bool takeBinaryPostBody(uint8_t** out, size_t* outLen, size_t minLen, size_t maxLen);
#endif
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
int16_t sendHTTPSJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType);
int16_t sendHTTPSJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType);
int16_t sendHTTPSBinary(IPAddress& ip, const uint8_t* data, uint16_t dataLen, const char* msgType);
int16_t sendHTTPSBinary(int16_t deviceIndex, const uint8_t* data, uint16_t dataLen, const char* msgType);
uint8_t sendAllSensors(bool forceSend, int16_t sendToDeviceIndex, bool useUDP);
bool SendData(int16_t snsIndex, bool forceSend=false, int16_t sendToDeviceIndex=-1, bool useUDP=false);

//send json messages
int16_t sendMSG_ping(IPAddress& ip, bool viaHTTP);
int16_t sendMSG_DataRequest(int16_t deviceIndex, int16_t snsIndex, bool viaHTTP);
int16_t sendMSG_DataRequest(ArborysDevType* d, int16_t snsIndex, bool viaHTTP);

// Connectivity ping metrics: ESPNow + UDP every ~10 min; HTTP only if both fail.
// Pass startCycle=true on the 10-minute mark; call every second while a cycle is active.
void serviceDeviceConnectivityPings(bool startCycle = false);

//add json handlers
void JSONbuilder_pingMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool viaHTTP, bool isAck);
void JSONbuilder_DataRequestMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool viaHTTP, int16_t snsIndex);
void JSONbuilder_sensorMSG(ArborysSnsType* S, char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP);
void JSONbuilder_sensorMSG_all(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP);
bool JSONbuilder_sensorMSG_list(const int16_t* snsIndices, uint8_t count, char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP);
void wrapupSendDataList(const int16_t* snsIndices, uint8_t count);
String JSONbuilder_sensorObject(ArborysSnsType* S);
uint16_t JSONbuilder_encodeHTTP(String& jsonBuffer);

//json processors
void processJSONMessage(String& postData, String& responseMsg);
void processJSONMessage_ping(JsonObject root, String& responseMsg);
void processJSONMessage_DataRequest(JsonObject root, String& responseMsg);
void processDeferredDataRequest();
void processJSONMessage_sensorData(JsonObject root, String& responseMsg);
void processJSONMessage_setFlagsReq(JsonObject root, String& responseMsg);
int16_t processJSONMessage_addDevice(JsonObject root, String& responseMsg);
static void handleSingleSensor(ArborysDevType* dev, JsonObject sensor, String& responseMsg);

void WiFiEvent(WiFiEvent_t event);

bool connectToWiFi(const String& ssid, const String& password, const String& lmk_key);
void apiConnectToWiFi();
void apiScanWiFi();
void apiClearWiFi();
void apiLookupLocation();
void apiDetectTimezone();
void apiDetectDST();
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