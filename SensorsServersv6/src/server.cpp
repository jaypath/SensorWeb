#include "globals.hpp"
#include "server.hpp"
#include "Devices.hpp"
#include "SDCard.hpp"
#ifdef _USETFT
  #include "graphics.hpp"
  extern LGFX tft;
#endif
#ifdef _USELEDMATRIX
#include "LEDMatrix.hpp"
#endif

#include "BootSecure.hpp"

extern int16_t MY_DEVICE_INDEX;

#ifdef _USEUDP
extern WiFiUDP LAN_UDP;
#endif

// Base64 decoding functions
int base64_dec_len(const char* input, int length) {
  int i = 0;
  int numEq = 0;
  for (i = length - 1; input[i] == '='; i--) {
    numEq++;
  }
  return ((6 * length) / 8) - numEq;
}

void base64_decode(const char* input, uint8_t* output) {
  static const char PROGMEM b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int i = 0, j = 0;
  int v;
  
  while (i < strlen(input)) {
    v = 0;
    for (int k = 0; k < 4; k++) {
      v <<= 6;
      if (input[i] == '=') {
        v |= 0;
      } else {
        for (int l = 0; l < 64; l++) {
          if (pgm_read_byte(&b64_alphabet[l]) == input[i]) {
            v |= l;
            break;
          }
        }
      }
      i++;
    }
    output[j++] = (v >> 16) & 0xFF;
    if (input[i-2] != '=') output[j++] = (v >> 8) & 0xFF;
    if (input[i-1] != '=') output[j++] = v & 0xFF;
  }
}

//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif
#ifdef _USE32

    WebServer server(80);
#endif




//server
String WEBHTML;
byte CURRENT_DEVICEVIEWER_DEVINDEX = 0;  // Track current device index in device viewer
byte CURRENT_DEVICEVIEWER_DEVNUMBER = 0; // track the number of devices, which may not align with the index

extern STRUCT_PrefsH Prefs;
//extern bool requestWiFiPassword(const uint8_t* serverMAC);


String getCert(String filename) 
{
  //certificate should be in the SD card
  

  File f = SD.open(filename, FILE_READ);
  String s="";
  if (f) {  
     if (f.available() > 0) {
        s = f.readString(); 
     }
     f.close();
    }    

    
    return s;
}

// Helper function to format bytes into human-readable format
String formatBytes(uint64_t bytes) {
  if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
    return String(bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
  } else if (bytes >= (1024ULL * 1024ULL)) {
    return String(bytes / (1024.0 * 1024.0), 1) + " MB";
  } else if (bytes >= 1024ULL) {
    return String(bytes / 1024.0, 1) + " KB";
  } else {
    return String(bytes) + " bytes";
  }
}

bool Server_SecureMessageEx(String& URL, String& payload, int& httpCode, String& cacert, String& method, String& contentType,  String& body, String& extraHeaders) {
if (WiFi.status() != WL_CONNECTED)
return false;

HTTPClient http;
WiFiClientSecure wfclient;

if (cacert.length() == 0) {
    //don't have the room to install the certificate bundle
    return false;

} else {
  wfclient.setCACert(getCert(cacert).c_str());
}

http.useHTTP10(true);

if (!http.begin(wfclient, URL.c_str()))
return false;

// --- Content-Type header ---
if (contentType.length() > 0) http.addHeader("Content-Type", contentType);

// --- Extra headers: newline-separated list ---
if (extraHeaders.length() > 0) {
int start = 0;
while (true) {
int end = extraHeaders.indexOf('\n', start);
String line;
if (end == -1)
line = extraHeaders.substring(start);
else
line = extraHeaders.substring(start, end);

line.trim();
if (line.length() > 0) {
int colon = line.indexOf(':');
if (colon > 0) {
String key = line.substring(0, colon);
String val = line.substring(colon + 1);
key.trim();
val.trim();
if (key.length() > 0)
http.addHeader(key, val);
}
}
if (end == -1)
break;
start = end + 1;
}
}

// --- Select HTTP method ---
if (method == "GET") {
httpCode = http.GET();
} else if (method == "POST") {
httpCode = http.POST(body);
} else if (method == "DELETE") {
httpCode = http.sendRequest("DELETE", (uint8_t*)nullptr, 0);
} else if (method == "PATCH") {
httpCode = http.sendRequest("PATCH", (uint8_t*)body.c_str(), body.length());
} else {
httpCode = http.sendRequest(method.c_str(), (uint8_t*)body.c_str(), body.length());
}

payload = http.getString();
http.end();
return true;
}

bool Server_Message(String& URL, String& payload, int &httpCode) { 
  
  if (URL.indexOf("https:")>-1) return false; //this cannot connect to https server!
  HTTPClient http;
  WiFiClient wfclient;
  

  if(WiFi.status()== WL_CONNECTED){
    
    http.begin(wfclient,URL.c_str());
    //http.useHTTP10(true);
    httpCode = http.GET();
    
    payload = http.getString();
  
    http.end();
    return true;
  } 

  return false;
}


bool WifiStatus(void) {
  if (WiFi.status() == WL_CONNECTED) {
    
    return true;
  }

    return false;
  
}

int16_t tryWifi(uint16_t delayms, uint16_t retryLimit, bool checkCredentials) {
  //return -1000 if no credentials, or connection attempts if success, or -1*number of attempts if failure
  int16_t retries = 0;
  int16_t i = 0;

  if (checkCredentials) {
    if (Prefs.HAVECREDENTIALS) {
      // force station-only mode for normal operation
      WiFi.mode(WIFI_MODE_STA);

    } else {
      return -1000;
    }
  }
    
  if (Prefs.WIFISSID[0] == 0) return -1000;

  for (i = 0; i < retryLimit; i++) {
    if (!WifiStatus()) {        
      WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
      delay(delayms);
    } else {
      
      Prefs.HAVECREDENTIALS=true;
      Prefs.isUpToDate = false;
      I.wifiFailCount = 0;
  
      return i;
    }
  }

  if (!WifiStatus())       return -1*i;



  return i;
}

int16_t connectWiFi() {
  
  int16_t retries = 0;
  retries = tryWifi(250,50,true);
  if (WifiStatus()) {
    #ifdef _USEUDP
    LAN_UDP.begin(_USEUDP); //start the UDP server on the port defined (WiFiUDP automatically binds to device IP)
    #endif
  
    return retries;
  }
  if (retries == -1000 || Prefs.HAVECREDENTIALS == false) {
    SerialPrint("No credentials, starting AP Station Mode",true);
    APStation_Mode();
    return -1000;
  }

  return retries;
}

void APStation_Mode() {
  registerHTTPMessage("APStation");
  
  //wifi ID and pwd will be set in connectsoftap
  String wifiPWD;
  String wifiID;
  IPAddress apIP;
  

  SerialPrint("Init AP Station Mode... Please wait... ",false);

  #ifdef _USELEDMATRIX
  Matrix_Init();
  Matrix_Draw(false, "WiFi?");

  #endif
  // Add delay to ensure system is ready
  delay(500);
  
  connectSoftAP(&wifiID,&wifiPWD,&apIP);

  delay(500);


  SerialPrint("AP Station ID started.",false);
  
  
  // Add delay before starting server
  delay(500);
  server.begin(); //start server


  SerialPrint("AP Station ID: ",false);
  SerialPrint(wifiID,true);
  SerialPrint("AP Station Password: ",false);
  SerialPrint(wifiPWD,true);
  SerialPrint("AP Station IP: ",false);
  SerialPrint(apIP.toString(),true);

    #ifdef _USETFT
    tft.clear();
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    if (Prefs.HAVECREDENTIALS) tft.printf("Failed to connect to %s.\n", Prefs.WIFISSID);
    else tft.printf("No stored credentials.\n");
    tft.printf("Setup may be required \n(or wifi down). Please join\n\nWiFi: %s\npwd: %s\n\nand go to website:\n\n%s\n\n", 
               wifiID.c_str(), wifiPWD.c_str(), apIP.toString().c_str());
    
    #endif

    uint32_t m=millis();
    uint32_t startTime = m;
    const uint32_t timeoutMs = 600000; // 10 minute timeout
    uint32_t last60Seconds = m;
    uint32_t lastServerStatusUpdate = I.HTTP_LAST_INCOMINGMSG_TIME;
    
    do {
      //reset the watchdog timer
      esp_task_wdt_reset();
      m=millis();
      //draw the timeout left at the botton right of the screen
      
      if (m - last60Seconds > 1000) {
        last60Seconds = m;
        I.currentTime = m/1000; //arbitrary time measure, just to keep track of clock

        #ifdef _USETFT
        //clear the bottom left of the screen
        tft.fillRect(0, tft.height()-40, tft.width(), 100, TFT_BLACK);
        tft.setCursor(0, tft.height()-40);
        tft.setTextColor(TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.printf("Seconds before reboot: %ds\n", ((timeoutMs - (m - startTime))/1000));
        tft.printf("WiFi: %s, Location: %s, Time: %s",
          (Prefs.HAVECREDENTIALS)?"Y":"N",
          (Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0)?"Y":"N",
          (Prefs.TimeZoneOffset!=0)?"Y":"N");
        #endif

      }

      //reset the start time if the server status has been updated
      if (I.HTTP_LAST_INCOMINGMSG_TIME != lastServerStatusUpdate) {
        lastServerStatusUpdate = I.HTTP_LAST_INCOMINGMSG_TIME;
        startTime = m;
      }
      
      server.handleClient(); //check if user has set up WiFi
            
      // Check for timeout
      if ( m - startTime > timeoutMs) {

        //if credentials are stored, but havecredentials was false, then try rebooting with those credentials on next boot
        if (Prefs.WIFISSID[0] != 0 && Prefs.WIFIPWD[0] != 0) {
          Prefs.HAVECREDENTIALS = true;
          Prefs.isUpToDate = false;
        }
        #ifdef _USETFT
        tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
        tft.setCursor(0, tft.height()-200);
        tft.setTextColor(TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.println("Timeout waiting for credentials. Rebooting...");        
        #endif
        delay(5000);
        controlledReboot("WiFi credentials timeout", RESET_WIFI, true);
        break;
      }
    }     while (true); //just keep looping until time runs out, or setup completed

}


// Helper function to URL encode strings
String urlEncode(const String& str) {
  String encoded = "";
  for (unsigned int i = 0; i < str.length(); i++) {
      char c = str.charAt(i);
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
          encoded += c;
      } else if (c == ' ') {
          encoded += '+';
      } else {
          encoded += '%';
          if (c < 16) {
              encoded += '0';
          }
          encoded += String(c, HEX);
      }
  }
  return encoded;
}


// ==================== STREAMLINED SETUP SYSTEM ====================
//
// This section provides a modern, streamlined setup experience with:
//
// 1. HELPER FUNCTIONS - Reusable logic for WiFi, location, and timezone
//    - connectToWiFi()              : Connect to WiFi network
//    - lookupLocationFromAddress()  : Get coordinates from address
//    - autoDetectTimezone()         : Auto-detect timezone settings
//    - saveTimezoneSettings()       : Save timezone to preferences
//
// 2. API HANDLERS - RESTful JSON endpoints for AJAX calls
//    - POST /api/wifi               : Connect to WiFi (returns JSON)
//    - POST /api/location           : Lookup location (returns JSON)
//    - GET  /api/timezone           : Auto-detect timezone (returns JSON)
//    - POST /api/timezone           : Save timezone (returns JSON)
//    - POST /api/complete-setup     : Complete setup (returns JSON)
//    - GET  /api/setup-status       : Get setup completion status (returns JSON)
//
// 3. SETUP WIZARD - Single-page guided setup interface
//    - GET /InitialSetup            : Interactive wizard with 3 steps
//      Step 1: WiFi Configuration
//      Step 2: Location Setup (optional)
//      Step 3: Timezone Configuration
//
// 4. LEGACY HANDLERS - Backward compatible (still functional)
//    - handleTimezoneSetup()        : Old timezone page
//    - handleTimezoneSetup_POST()   : Now uses helper functions
//    - handleWeatherAddress()       : Now uses helper functions
//
// Benefits:
//   - Cleaner code separation
//   - Better user experience with wizard interface
//   - Reusable logic across handlers
//   - Modern AJAX-based interactions
//   - Backward compatibility maintained
//
// =====================================================================

/**
 * Helper: Connect to WiFi network
 * Returns: true if connection successful, false otherwise
 */
bool connectToWiFi(const String& ssid, const String& password, const String& lmk_key) {
  if (ssid.length() == 0) {
    SerialPrint("connectToWiFi: Empty SSID provided", true);
    return false;
  }
  
  // Save credentials
  snprintf((char*)Prefs.WIFISSID, sizeof(Prefs.WIFISSID), "%s", ssid.c_str());
  snprintf((char*)Prefs.WIFIPWD, sizeof(Prefs.WIFIPWD), "%s", password.c_str());
  
  // Save LMK key if provided
  if (lmk_key.length() > 0) {
    memset(Prefs.KEYS.ESPNOW_KEY, 0, sizeof(Prefs.KEYS.ESPNOW_KEY));
    strncpy((char*)Prefs.KEYS.ESPNOW_KEY, lmk_key.c_str(), 16);
  }
  
  Prefs.HAVECREDENTIALS = true;
  Prefs.isUpToDate = false;
  
  // Attempt WiFi connection
  SerialPrint("Attempting WiFi connection to: " + ssid, true);

  int retries = tryWifi(250,20,false); //do not check credentials, which will terminate AP mode
  
  if (WiFi.status() == WL_CONNECTED) {
    SerialPrint("WiFi connected! IP: " + WiFi.localIP().toString(), true);
    
    return true;
  }
  
  SerialPrint("WiFi connection failed", true);
  return false;
}

/**
 * Helper: Lookup location coordinates from address
 * Returns: true if lookup successful, false otherwise
 */
bool lookupLocationFromAddress(const String& street, const String& city, const String& state, const String& zipcode, double* lat, double* lon) {
  // Validate inputs
  if (street.length() == 0 || city.length() == 0 || state.length() != 2 || zipcode.length() != 5) {
    SerialPrint("lookupLocationFromAddress: Invalid address parameters", true);
    return false;
  }
  
  // Validate ZIP code is numeric
  for (int i = 0; i < 5; i++) {
    if (!isdigit(zipcode.charAt(i))) {
      SerialPrint("lookupLocationFromAddress: ZIP code must be numeric", true);
      return false;
    }
  }
  
  // Check WiFi status
  if (!WifiStatus()) {
    SerialPrint("lookupLocationFromAddress: WiFi not connected", true);
    return false;
  }
  
  // Call existing handler function
  bool result = handlerForWeatherAddress(street, city, state, zipcode);
  
  if (result && lat != nullptr && lon != nullptr) {
    *lat = Prefs.LATITUDE;
    *lon = Prefs.LONGITUDE;
    Prefs.isUpToDate = false;
    
    // Save to NVS
    BootSecure bootSecure;
    int8_t ret = bootSecure.setPrefs();
    if (ret < 0) {
      SerialPrint("lookupLocationFromAddress: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
    } else {
      SerialPrint("Location lookup successful and saved: " + String(*lat, 6) + ", " + String(*lon, 6), true);
    }
  }
  
  return result;
}

/**
 * Helper: Auto-detect timezone from IP location
 * Returns: true if detection successful, false otherwise
 */
bool autoDetectTimezone(int32_t* utc_offset, bool* dst_enabled, uint8_t* dst_start_month, uint8_t* dst_start_day, uint8_t* dst_end_month, uint8_t* dst_end_day) {
  if (!WifiStatus()) {
    SerialPrint("autoDetectTimezone: WiFi not connected", true);
    return false;
  }
  
  // Call existing timezone detection function
  bool result = getTimezoneInfo(utc_offset, dst_enabled, dst_start_month, dst_start_day, dst_end_month, dst_end_day);
  
  if (result) {
    SerialPrint("Timezone detected: UTC offset = " + String(*utc_offset), true);
  } else {
    SerialPrint("Timezone detection failed, using defaults", true);
    // Set reasonable defaults (EST)
    *utc_offset = -18000;
    *dst_enabled = true;
    *dst_start_month = 3;
    *dst_start_day = 10;
    *dst_end_month = 11;
    *dst_end_day = 3;
  }
  
  return result;
}

/**
 * Helper: Save timezone settings to preferences
 */
void saveTimezoneSettings(int32_t utc_offset, bool dst_enabled, uint8_t dst_start_month, uint8_t dst_start_day, uint8_t dst_end_month, uint8_t dst_end_day) {
  Prefs.TimeZoneOffset = utc_offset;
  Prefs.DST = dst_enabled;
  Prefs.DSTStartMonth = dst_start_month;
  Prefs.DSTStartDay = dst_start_day;
  Prefs.DSTEndMonth = dst_end_month;
  Prefs.DSTEndDay = dst_end_day;
  Prefs.isUpToDate = false;
  
  SerialPrint("Timezone settings saved: UTC offset = " + String(utc_offset), true);
}

// ==================== API HANDLERS (JSON RESPONSES) ====================

/**
 * API: Connect to WiFi
 * POST /api/wifi
 * Parameters: ssid, password, lmk_key (optional)
 * Returns: JSON with status
 */
void apiConnectToWiFi() {
  registerHTTPMessage("API_WiFi");
  
  String ssid = "";
  String password = "";
  String lmk_key = "";
  String deviceName = "";
  
  if (server.hasArg("deviceName")) {
    deviceName = server.arg("deviceName");
    if (deviceName.length() > 0) {
      snprintf((char*)Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", deviceName.c_str());
      Prefs.isUpToDate = false; // Mark as needing to be saved
  
      //now update the DeviceStore with the new device name
      int16_t deviceIndex = Sensors.findMyDeviceIndex();
      if (!Sensors.updateDeviceName(deviceIndex, deviceName)) {
        SerialPrint("Failed to update device name", true);
        storeError("Failed to update device name");
      } 
    }
  
  }
  if (server.hasArg("ssid") ) {
    ssid = server.arg("ssid");
    if (ssid.length() == 0) {

      server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
      return;
    }

  
  }
  if (server.hasArg("password")) {
    password = server.arg("password");
  }
      
  if (server.hasArg("lmk_key")) {
    lmk_key = server.arg("lmk_key");
  }

  if (connectToWiFi(ssid, password, lmk_key)) {
    handleStoreCoreData(); //update prefs and core if needed  
    server.send(200, "application/json", "{\"success\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Connection failed\"}");
  }
  
}

/**
 * API: Lookup location from address
 * POST /api/location
 * Parameters: street, city, state, zipcode
 * Returns: JSON with lat/lon
 */
void apiLookupLocation() {
  registerHTTPMessage("API_Loc");
  
  String street = server.hasArg("street") ? server.arg("street") : "";
  String city = server.hasArg("city") ? server.arg("city") : "";
  String state = server.hasArg("state") ? server.arg("state") : "";
  String zipcode = server.hasArg("zipcode") ? server.arg("zipcode") : "";
  
  double lat = 0, lon = 0;
  bool success = lookupLocationFromAddress(street, city, state, zipcode, &lat, &lon);
  
  if (success) {
    String json = "{\"success\":true,\"latitude\":" + String(lat, 6) + ",\"longitude\":" + String(lon, 6) + "}";
    
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Location lookup failed\"}");
  }
}

/**
 * API: Auto-detect timezone
 * GET /api/timezone
 * Returns: JSON with timezone info
 */
void apiDetectTimezone() {
  registerHTTPMessage("API_TZ");
  int32_t utc_offset;
  bool dst_enabled;
  uint8_t dst_start_month, dst_start_day, dst_end_month, dst_end_day;
  
  bool success = autoDetectTimezone(&utc_offset, &dst_enabled, &dst_start_month, &dst_start_day, &dst_end_month, &dst_end_day);
  
  String json = "{\"success\":" + String(success ? "true" : "false") + 
                ",\"utc_offset\":" + String(utc_offset) +
                ",\"dst_enabled\":" + String(dst_enabled ? "true" : "false") +
                ",\"dst_start_month\":" + String(dst_start_month) +
                ",\"dst_start_day\":" + String(dst_start_day) +
                ",\"dst_end_month\":" + String(dst_end_month) +
                ",\"dst_end_day\":" + String(dst_end_day) + "}";
  
  server.send(200, "application/json", json);
}

/**
 * API: Save timezone settings
 * POST /api/timezone
 * Parameters: utc_offset, dst_enabled, dst_start_month, dst_start_day, dst_end_month, dst_end_day
 * Returns: JSON with status
 */
void apiSaveTimezone() {
  registerHTTPMessage("API_TZ");
  
  int32_t utc_offset = server.hasArg("utc_offset") ? server.arg("utc_offset").toInt() : 0;
  bool dst_enabled = server.hasArg("dst_enabled") ? (server.arg("dst_enabled") == "true" || server.arg("dst_enabled") == "1") : false;
  uint8_t dst_start_month = server.hasArg("dst_start_month") ? server.arg("dst_start_month").toInt() : 3;
  uint8_t dst_start_day = server.hasArg("dst_start_day") ? server.arg("dst_start_day").toInt() : 10;
  uint8_t dst_end_month = server.hasArg("dst_end_month") ? server.arg("dst_end_month").toInt() : 11;
  uint8_t dst_end_day = server.hasArg("dst_end_day") ? server.arg("dst_end_day").toInt() : 3;
  
  saveTimezoneSettings(utc_offset, dst_enabled, dst_start_month, dst_start_day, dst_end_month, dst_end_day);
  
  server.send(200, "application/json", "{\"success\":true}");
}

/**
 * API: Get setup status
 * GET /api/setup-status
 * Returns: JSON with current setup completion status
 */
void apiGetSetupStatus() {
  registerHTTPMessage("API_Setup");

  bool wifi_configured = Prefs.HAVECREDENTIALS && WifiStatus();
  bool location_configured = (Prefs.LATITUDE != 0 || Prefs.LONGITUDE != 0);
  bool timezone_configured = (Prefs.TimeZoneOffset != 0);
  #ifdef _USEWEATHER
  bool setup_complete = wifi_configured && timezone_configured && location_configured;
  #else
  bool setup_complete = wifi_configured && timezone_configured;
  #endif
  
  String json = "{\"wifi_configured\":" + String(wifi_configured ? "true" : "false") +
                ",\"wifi_connected\":" + String(WifiStatus() ? "true" : "false") +
                ",\"location_configured\":" + String(location_configured ? "true" : "false") +
                ",\"timezone_configured\":" + String(timezone_configured ? "true" : "false") +
                ",\"setup_complete\":" + String(setup_complete ? "true" : "false");
  
  if (wifi_configured) {
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  }
  if (location_configured) {
    json += ",\"latitude\":" + String(Prefs.LATITUDE, 6) + ",\"longitude\":" + String(Prefs.LONGITUDE, 6);
  }
  if (timezone_configured) {
    json += ",\"utc_offset\":" + String(Prefs.TimeZoneOffset);
  }
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleApiCompleteSetup() {
  registerHTTPMessage("API_OK");

  if (!WifiStatus()) {
    #ifdef _USETFT
    tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
    tft.setCursor(0, tft.height()-200);
    tft.setTextColor(TFT_RED);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.println("Connection failed...");
    tft.setTextColor(TFT_WHITE);
    #endif
  
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Failed to connect to WiFi\"}");
    return;
  }

  storeCoreData(true);


  #ifdef _USETFT
  tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
  tft.setCursor(0, tft.height()-200);
  tft.setTextColor(TFT_WHITE);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.println("Rebooting with updated credentials...");
  #endif
  //reboot the device
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Setup complete - rejoin regular wifi network\"}");

  controlledReboot("WIFI CREDENTIALS RESET",RESET_NEWWIFI,true);
  
}

/**
 * API: Scan for WiFi networks
 * GET /api/wifi-scan
 * Returns: JSON array of available WiFi networks
 */
void apiScanWiFi() {
  registerHTTPMessage("API_Scan");

  SerialPrint("Scanning for WiFi networks...", true);
  #ifdef _USETFT
    tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
    tft.setCursor(0, tft.height()-200);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.println("Scanning networks...");
    tft.setTextColor(TFT_WHITE);
    #endif
  // Perform WiFi scan
  int numNetworks = WiFi.scanNetworks();

  // Build JSON array of networks
  String json = "{\"success\":true,\"networks\":[";
  String lastnetwork = "";
  byte count=0;
  for (int i = 0; i < numNetworks; i++) {
    if (WiFi.SSID(i)!=lastnetwork) {
      lastnetwork = WiFi.SSID(i);
      if (count>0) json += ",";
      count++;
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\"";
      json += ",\"rssi\":" + String(WiFi.RSSI(i));
      json += ",\"encryption\":" + String(WiFi.encryptionType(i));
      json += "}";      
    }
    
  }
  json += "]}";
  
  if (numNetworks == 0) {
    #ifdef _USETFT
    tft.setTextColor(TFT_RED);
    tft.println("No networks found. Enter Manually.");
    tft.setTextColor(TFT_WHITE);
    #endif
    server.send(200, "application/json", "{\"success\":true,\"networks\":[]}");
    return;
  }
  
  
  // Clean up scan results
  WiFi.scanDelete();
  
  SerialPrint("Found " + String(count) + " WiFi networks", true);
  #ifdef _USETFT
  tft.setTextColor(TFT_GREEN);
  tft.printf("%d networks found.\n",count);
  tft.setTextColor(TFT_WHITE);
  #endif

  server.send(200, "application/json", json);
}

// ==================== SETUP WIZARD PAGE ====================

/**
 * Initial Setup Wizard - Single-page guided setup
 * GET /InitialSetup
 */
void handleInitialSetup() {

  registerHTTPMessage("Init");
  WEBHTML = "";
  serverTextHeader();
  
  WEBHTML += R"===(
<style>
  .setup-container { max-width: 900px; margin: 20px auto; padding: 20px; font-family: Arial, sans-serif; }
  .setup-step { background: #f8f9fa; border: 2px solid #dee2e6; border-radius: 8px; padding: 20px; margin: 15px 0; }
  .setup-step.active { border-color: #2196F3; background: #e3f2fd; }
  .setup-step.completed { border-color: #4CAF50; background: #e8f5e8; }
  .step-header { display: flex; align-items: center; margin-bottom: 15px; cursor: pointer; }
  .step-number { width: 35px; height: 35px; border-radius: 50%; background: #ccc; color: white; display: flex; align-items: center; justify-content: center; font-weight: bold; margin-right: 15px; }
  .active .step-number { background: #2196F3; }
  .completed .step-number { background: #4CAF50; }
  .completed .step-number::after { content: "x"; }
  .step-title { font-size: 18px; font-weight: bold; flex-grow: 1; }
  .step-content { display: none; padding-left: 50px; }
  .active .step-content, .completed .step-content { display: block; }
  .form-group { margin: 15px 0; }
  .form-group label { display: block; margin-bottom: 5px; font-weight: bold; }
  .form-group input, .form-group select { width: 100%; max-width: 400px; padding: 10px; border: 1px solid #ccc; border-radius: 4px; font-size: 14px; }
  .btn { padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 5px; }
  .btn-primary { background: #4CAF50; color: white; }
  .btn-secondary { background: #2196F3; color: white; }
  .btn-danger { background: #f44336; color: white; }
  .btn:disabled { background: #ccc; cursor: not-allowed; }
  .status-message { padding: 12px; border-radius: 4px; margin: 10px 0; display: none; }
  .status-success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
  .status-error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  .status-info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
  .spinner { border: 3px solid #f3f3f3; border-top: 3px solid #3498db; border-radius: 50%; width: 20px; height: 20px; animation: spin 1s linear infinite; display: inline-block; margin-left: 10px; }
  @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
</style>

<body>
<div class="setup-container">
  <h1>Initial System Setup</h1>
  <p>Welcome! Let's configure your system.</p>
  )===";

  //insert what is missing/ why we got to initial setup
  //possible choices include no wifi, no timezone, no location (if using weather), or user reset

  //is there no wifi?
  if (!Prefs.HAVECREDENTIALS || !WifiStatus()) {
    WEBHTML += R"===(
    <p>WiFi credentials not found and required to continue.</p>
    )===";
  } else {
    WEBHTML += "<p>Currently connected to WiFi network: " + WiFi.SSID() + "</p>";
    WEBHTML += "<p>Current IP address: " + WiFi.localIP().toString() + "</p>";
  }

  //is there no timezone?
  if (Prefs.TimeZoneOffset == 0) {
    WEBHTML += R"===(
    <p>Timezone not found and required to continue.</p>
    )===";
  } else {
    WEBHTML += "<p>Timezone: " + String(Prefs.TimeZoneOffset) + "</p>";
  }

  #ifdef _USEWEATHER
  //is there no location?
  if (Prefs.LATITUDE == 0 || Prefs.LONGITUDE == 0) {
    WEBHTML += R"===(
    <p>Location not found and required for weather data (leave blank if not using weather).</p>
    )===";
  } else {
    WEBHTML += "<p>LAT, LON: " + String(Prefs.LATITUDE, 6) + ", " + String(Prefs.LONGITUDE, 6) + "</p>";
  }
  #endif

  WEBHTML += R"===(  
  <!-- Step 1: WiFi Configuration -->
  <div class="setup-step" id="step1">
    <div class="step-header" onclick="toggleStep('step1')">
      <div class="step-number">1</div>
      <div class="step-title">WiFi Configuration</div>
    </div>
    <div class="step-content">
      <p>Connect your device to your WiFi network.</p>
      <div id="wifi-status" class="status-message"></div>
      
      <div class="form-group">
        <label for="ssid">Network Name (SSID) *</label>
        <input type="text" id="ssid" list="ssid-list" placeholder="Select or enter your WiFi network name" autocomplete="off">
        <datalist id="ssid-list">
          <!-- WiFi networks will be populated here -->
        </datalist>
        <button class="btn btn-secondary" onclick="scanWiFiNetworks()" id="scan-btn" style="margin-top: 5px; padding: 8px 16px; font-size: 14px;">
          Scan for Networks
        </button>
      </div>
      
      <div class="form-group">
        <label for="password">Password *</label>
        <input type="password" id="password" placeholder="Enter WiFi password">
      </div>
      
      <div class="form-group">
        <label for="lmk_key">Local Security Key (16 characters)</label>
        <input type="text" id="lmk_key" maxlength="16" placeholder="Optional - for ESPNow encryption">
      </div>

      <div class="form-group">
        <label for="DeviceName">Device Name</label>
        <input type="text" id="DeviceName" maxlength="32" placeholder="Enter a name for your device">
      </div>

      <button class="btn btn-primary" onclick="connectWiFi()" id="wifi-btn">Connect to WiFi</button>
    </div>
  </div>
  
  <!-- Step 2: Location Setup -->
  <div class="setup-step" id="step2">
    <div class="step-header" onclick="toggleStep('step2')">
      <div class="step-number">2</div>
      <div class="step-title">Location Setup (Optional)</div>
    </div>
    <div class="step-content">
      <p>Enter your address to get weather information. This step is optional if not using weather.</p>
      <div id="location-status" class="status-message"></div>
      
      <div class="form-group">
        <label for="street">Street Address</label>
        <input type="text" id="street" placeholder="123 Main St">
      </div>
      
      <div class="form-group">
        <label for="city">City</label>
        <input type="text" id="city" placeholder="Boston">
      </div>
      
      <div class="form-group">
        <label for="state">State (2-letter code)</label>
        <input type="text" id="state" maxlength="2" placeholder="MA" style="width: 80px; text-transform: uppercase;">
      </div>
      
      <div class="form-group">
        <label for="zipcode">ZIP Code</label>
        <input type="text" id="zipcode" maxlength="5" placeholder="02101" pattern="[0-9]{5}">
      </div>
      
      <button class="btn btn-primary" onclick="lookupLocation()" id="location-btn">Lookup Location</button>
      <button class="btn btn-secondary" onclick="skipLocation()">Skip This Step</button>
    </div>
  </div>
  
  <!-- Step 3: Timezone Configuration -->
  <div class="setup-step" id="step3">
    <div class="step-header" onclick="toggleStep('step3')">
      <div class="step-number">3</div>
      <div class="step-title">Timezone Configuration</div>
    </div>
    <div class="step-content">
      <p>Configure your timezone settings.</p>
      <div id="timezone-status" class="status-message"></div>
      
      <button class="btn btn-secondary" onclick="autoDetectTimezone()" id="detect-tz-btn">Auto-Detect Timezone</button>
      
      <div id="timezone-form" style="display:none; margin-top: 20px;">
        <div class="form-group">
          <label for="utc_offset">UTC Offset (seconds)</label>
          <input type="number" id="utc_offset" value="-18000" step="900">
        </div>
        
        <div class="form-group">
          <label for="dst_enabled">Daylight Saving Time</label>
          <select id="dst_enabled">
            <option value="true">Enabled</option>
            <option value="false">Disabled</option>
          </select>
        </div>
        
        <div class="form-group">
          <label for="dst_start">DST Start (Month/Day)</label>
          <input type="number" id="dst_start_month" value="3" min="1" max="12" style="width: 80px;" placeholder="Month">
          <input type="number" id="dst_start_day" value="10" min="1" max="31" style="width: 80px;" placeholder="Day">
        </div>
        
        <div class="form-group">
          <label for="dst_end">DST End (Month/Day)</label>
          <input type="number" id="dst_end_month" value="11" min="1" max="12" style="width: 80px;" placeholder="Month">
          <input type="number" id="dst_end_day" value="3" min="1" max="31" style="width: 80px;" placeholder="Day">
        </div>
        
        <button class="btn btn-primary" onclick="saveTimezone()">Save Timezone</button>
      </div>
    </div>
  </div>
  
  <!-- Complete Setup -->
  <div style="text-align: center; margin: 30px 0;">
    <button class="btn btn-primary" id="complete-btn" onclick="completeSetup()" disabled style="font-size: 18px; padding: 15px 30px;">
      Complete Setup
    </button>
  </div>
</div>

<script>
let setupState = {
  wifiConfigured: false,
  locationConfigured: false,
  timezoneConfigured: false
};

// Toggle step visibility
function toggleStep(stepId) {
  const step = document.getElementById(stepId);
  const content = step.querySelector('.step-content');
  if (content.style.display === 'block') {
    content.style.display = 'none';
  } else {
    content.style.display = 'block';
  }
}

// Show status message
function showStatus(elementId, message, type) {
  const el = document.getElementById(elementId);
  el.textContent = message;
  el.className = 'status-message status-' + type;
  el.style.display = 'block';
}

// Mark step as completed
function completeStep(stepNum) {
  const step = document.getElementById('step' + stepNum);
  step.classList.add('completed');
  step.classList.remove('active');
  if (stepNum < 3) {
    const nextStep = document.getElementById('step' + (stepNum + 1));
    nextStep.classList.add('active');
  }
  checkSetupComplete();
}

// Check if setup is complete
function checkSetupComplete() {
  if (setupState.wifiConfigured && setupState.timezoneConfigured) {
    document.getElementById('complete-btn').disabled = false;
  }
}

// Scan for WiFi networks
async function scanWiFiNetworks() {
  showStatus('wifi-status', 'Scanning for networks...', 'info');
  const scanBtn = document.getElementById('scan-btn');
  scanBtn.disabled = true;
  scanBtn.textContent = 'Scanning...';
  
  try {
    const response = await fetch('/api/wifi-scan');
    const data = await response.json();
    
    if (data.success && data.networks) {
      const datalist = document.getElementById('ssid-list');
      datalist.innerHTML = ''; // Clear existing options
      
      // Sort networks by signal strength (RSSI)
      data.networks.sort((a, b) => b.rssi - a.rssi);
      
      // Add each network to the datalist
      data.networks.forEach(network => {
        const option = document.createElement('option');
        option.value = network.ssid;
        // Show signal strength indicator
        const bars = network.rssi > -50 ? '++++' : network.rssi > -60 ? '+++' : network.rssi > -70 ? '++' : '+';
        const lock = network.encryption > 0 ? '[enc] ' : '[open]';
        option.textContent = lock + network.ssid + ' ' + bars;
        datalist.appendChild(option);
      });
      
      showStatus('wifi-status', 'Found ' + data.networks.length + ' networks. Select from the dropdown.', 'success');
    } else {
      showStatus('wifi-status', 'No networks found. You can still enter SSID manually.', 'info');
    }
  } catch (error) {
    showStatus('wifi-status', 'Scan failed: ' + error.message + '. You can still enter SSID manually.', 'error');
  } finally {
    scanBtn.disabled = false;
    scanBtn.textContent = 'Scan for Networks';
  }
}

// Connect to WiFi
async function connectWiFi() {
  const deviceName = document.getElementById('DeviceName').value;
  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('password').value;
  const lmk_key = document.getElementById('lmk_key').value;
  
  if (!ssid) {
    showStatus('wifi-status', 'Please enter a network name (SSID)', 'error');
    return;
  }
  
  if (!deviceName) {
    showStatus('wifi-status', 'Please enter a device name', 'error');
    return;
  }

  showStatus('wifi-status', 'Connecting to WiFi...', 'info');
  document.getElementById('wifi-btn').disabled = true;
  
  const formData = new FormData();
  formData.append('ssid', ssid);
  formData.append('password', password);
  formData.append('lmk_key', lmk_key);
  formData.append('deviceName', deviceName);

  try {
    const response = await fetch('/api/wifi', { method: 'POST', body: formData });
    const data = await response.json();
    
    if (data.success) {
      showStatus('wifi-status', 'Connected! IP: ' + data.ip, 'success');
      setupState.wifiConfigured = true;
      completeStep(1);
    } else {
      showStatus('wifi-status', 'Connection failed: ' + data.error, 'error');
      document.getElementById('wifi-btn').disabled = false;
    }
  } catch (error) {
    showStatus('wifi-status', 'Error: ' + error.message, 'error');
    document.getElementById('wifi-btn').disabled = false;
  }
}

// Lookup location
async function lookupLocation() {
  const street = document.getElementById('street').value;
  const city = document.getElementById('city').value;
  const state = document.getElementById('state').value;
  const zipcode = document.getElementById('zipcode').value;
  
  if (!street || !city || !state || !zipcode) {
    showStatus('location-status', 'Please fill in all address fields', 'error');
    return;
  }
  
  showStatus('location-status', 'Looking up location...', 'info');
  document.getElementById('location-btn').disabled = true;
  
  const formData = new FormData();
  formData.append('street', street);
  formData.append('city', city);
  formData.append('state', state);
  formData.append('zipcode', zipcode);
  
  try {
    const response = await fetch('/api/location', { method: 'POST', body: formData });
    const data = await response.json();
    
    if (data.success) {
      showStatus('location-status', 'Location found: ' + data.latitude.toFixed(4) + ', ' + data.longitude.toFixed(4), 'success');
      setupState.locationConfigured = true;
      completeStep(2);
    } else {
      showStatus('location-status', 'Lookup failed: ' + data.error, 'error');
      document.getElementById('location-btn').disabled = false;
    }
  } catch (error) {
    showStatus('location-status', 'Error: ' + error.message, 'error');
    document.getElementById('location-btn').disabled = false;
  }
}

// Skip location step
function skipLocation() {
  setupState.locationConfigured = true;
  showStatus('location-status', 'Location setup skipped', 'info');
  completeStep(2);
}

// Auto-detect timezone
async function autoDetectTimezone() {
  showStatus('timezone-status', 'Detecting timezone...', 'info');
  document.getElementById('detect-tz-btn').disabled = true;
  
  try {
    const response = await fetch('/api/timezone');
    const data = await response.json();
    
    if (data.success || data.utc_offset !== 0) {
      // Populate form
      document.getElementById('utc_offset').value = data.utc_offset;
      document.getElementById('dst_enabled').value = data.dst_enabled ? 'true' : 'false';
      document.getElementById('dst_start_month').value = data.dst_start_month;
      document.getElementById('dst_start_day').value = data.dst_start_day;
      document.getElementById('dst_end_month').value = data.dst_end_month;
      document.getElementById('dst_end_day').value = data.dst_end_day;
      
      showStatus('timezone-status', 'Timezone detected. Please review and save.', 'success');
      document.getElementById('timezone-form').style.display = 'block';
    } else {
      showStatus('timezone-status', 'Auto-detection failed. Please set manually.', 'info');
      document.getElementById('timezone-form').style.display = 'block';
    }
  } catch (error) {
    showStatus('timezone-status', 'Detection error. Using defaults.', 'info');
    document.getElementById('timezone-form').style.display = 'block';
  }
}

// Save timezone
async function saveTimezone() {
  const utc_offset = document.getElementById('utc_offset').value;
  const dst_enabled = document.getElementById('dst_enabled').value;
  const dst_start_month = document.getElementById('dst_start_month').value;
  const dst_start_day = document.getElementById('dst_start_day').value;
  const dst_end_month = document.getElementById('dst_end_month').value;
  const dst_end_day = document.getElementById('dst_end_day').value;
  
  const formData = new FormData();
  formData.append('utc_offset', utc_offset);
  formData.append('dst_enabled', dst_enabled);
  formData.append('dst_start_month', dst_start_month);
  formData.append('dst_start_day', dst_start_day);
  formData.append('dst_end_month', dst_end_month);
  formData.append('dst_end_day', dst_end_day);
  
  try {
    const response = await fetch('/api/timezone', { method: 'POST', body: formData });
    const data = await response.json();
    
    if (data.success) {
      showStatus('timezone-status', 'Timezone saved!', 'success');
      setupState.timezoneConfigured = true;
      completeStep(3);
    } else {
      showStatus('timezone-status', 'Failed to save timezone', 'error');
    }
  } catch (error) {
    showStatus('timezone-status', 'Error: ' + error.message, 'error');
  }
}

// Complete setup
function completeSetup() {
  if (confirm('Setup complete!  Continue?')) {
    window.location.href = '/api/complete-setup';
  }
}

// Initialize - check current status
async function initSetup() {
  try {
    const response = await fetch('/api/setup-status');
    const status = await response.json();
    
    if (status.wifi_configured) {
      setupState.wifiConfigured = true;
      document.getElementById('step1').classList.add('completed');
      showStatus('wifi-status', 'Already configured: ' + status.ip, 'success');
    } else {
      document.getElementById('step1').classList.add('active');
    }
    
    if (status.location_configured) {
      setupState.locationConfigured = true;
      document.getElementById('step2').classList.add('completed');
      showStatus('location-status', 'Location: ' + status.latitude.toFixed(4) + ', ' + status.longitude.toFixed(4), 'success');
    }
    
    if (status.timezone_configured) {
      setupState.timezoneConfigured = true;
      document.getElementById('step3').classList.add('completed');
      showStatus('timezone-status', 'Configured: UTC offset = ' + status.utc_offset, 'success');
    }
    
    checkSetupComplete();
  } catch (error) {
    console.error('Failed to load setup status:', error);
    document.getElementById('step1').classList.add('active');
  }
}

// Run on page load
initSetup();
</script>
</body></html>
)===";
  
  serverTextClose(200, true);
}

void handleReboot() {
  snprintf(I.HTTP_LAST_INCOMINGMSG_TYPE, sizeof(I.HTTP_LAST_INCOMINGMSG_TYPE), "REBOOT");
  WEBHTML = "Rebooting in 3 sec";
  
  handleStoreCoreData();

  serverTextClose(200, false);  //This returns to the main page
  delay(3000);
  controlledReboot("Rebooting",RESET_USER,true);
}

void handleCLEARSENSOR() {
  int j=-1;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  initSensor(j);

  server.sendHeader("Location", "/");
  
  WEBHTML= "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}

void handleREQUESTUPDATE() {

  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  String payload;
  int httpCode;
  
  // Get the device IP for the specified sensor
  ArborysDevType* device = Sensors.getDeviceBySnsIndex(j);
  if (device) {
    String URL = device->IP.toString() + "/UPDATEALLSENSORREADS";
    Server_Message(URL, payload, httpCode);
  }

  server.sendHeader("Location", "/");
  WEBHTML= "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}


void handleREQUESTWEATHER() {
  #ifdef _USEWEATHER
  registerHTTPMessage("WTHRREQ");
//if no parameters passed, return current temp, max, min, today weather ID, pop, and snow amount
//otherwise, return the index value for the requested value

  int8_t dailyT[2];

  WEBHTML = "";
  if (server.args()==0) {
        WEBHTML += (String) WeatherData.getTemperature(0,false,true) + ";"; //current temp
        WeatherData.getDailyTemp(0,dailyT);
        WEBHTML += (String) dailyT[0] + ";"; //dailymax
        WEBHTML += (String) dailyT[1] + ";"; //dailymin
        WEBHTML += (String) WeatherData.getDailyWeatherID(0) + ";"; //dailyID
        WEBHTML += (String) WeatherData.getDailyPoP(0) + ";"; //POP
        WEBHTML += (String) WeatherData.flag_snow + ";"; 
        WEBHTML += (String) WeatherData.sunrise + ";"; 
        WEBHTML += (String) WeatherData.sunset + ";"; 
  } else {
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i)=="hourly_temp") WEBHTML += (String) WeatherData.getTemperature(I.currentTime + server.arg(i).toInt()*3600,false,false) + ";";
      WeatherData.getDailyTemp(server.arg(i).toInt(),dailyT);
        if (server.argName(i)=="daily_tempMax") WEBHTML += (String) dailyT[0] + ";";
      if (server.argName(i)=="daily_tempMin") WEBHTML += (String) dailyT[1] + ";";
      if (server.argName(i)=="daily_weatherID") WEBHTML += (String) WeatherData.getDailyWeatherID(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="hourly_weatherID") WEBHTML += (String) WeatherData.getWeatherID(I.currentTime + server.arg(i).toInt()*3600) + ";";
      if (server.argName(i)=="daily_pop") WEBHTML += (String) WeatherData.getDailyPoP(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="daily_snow") WEBHTML += (String) WeatherData.getDailySnow(server.arg(i).toInt()) + ";";
      if (server.argName(i)=="hourly_pop") WEBHTML += (String) WeatherData.getPoP(I.currentTime + server.arg(i).toInt()*3600) + ";";
      if (server.argName(i)=="hourly_snow") WEBHTML += (String) WeatherData.getSnow(I.currentTime + server.arg(i).toInt()*3600) + ";";      //note snow is returned in mm!!
      if (server.argName(i)=="sunrise") WEBHTML += (String) WeatherData.sunrise + ";";
      if (server.argName(i)=="sunset") WEBHTML += (String) WeatherData.sunset + ";";
      if (server.argName(i)=="hour") {
        uint32_t temptime = server.arg(i).toDouble();
        if (temptime==0) WEBHTML += (String) hour() + ";";
        else WEBHTML += (String) hour(temptime) + ";";
      }
      if (server.argName(i)=="isFlagged") WEBHTML += (String) I.isFlagged + ";";
      if (server.argName(i)=="isAC") WEBHTML += (String) I.isAC + ";";
      if (server.argName(i)=="isHeat") WEBHTML += (String) I.isHeat + ";";
      if (server.argName(i)=="isSoilDry") WEBHTML += (String) I.isSoilDry + ";";
      if (server.argName(i)=="isHot") WEBHTML += (String) I.isHot + ";";
      if (server.argName(i)=="isCold") WEBHTML += (String) I.isCold + ";";
      if (server.argName(i)=="isLeak") WEBHTML += (String) I.isLeak + ";";
      if (server.argName(i)=="isExpired") WEBHTML += (String) I.isExpired + ";";

    }
  }
  #else
  WEBHTML = "Weather not enabled on this device.";
  #endif

  serverTextClose(200,false);

  return;
}

void handleTIMEUPDATE() {
  registerHTTPMessage("TIMEUPD");

  timeClient.forceUpdate();

  server.sendHeader("Location", "/");


  WEBHTML = "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}


void handleSTATUS() {
  //registerHTTPMessage("STATUS");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";  
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Status Page</h2>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

  // Navigation buttons
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px;\">Devices</a> ";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a>";
  WEBHTML = WEBHTML + "</div>";

  #ifdef _USE8266
  WEBHTML = WEBHTML + "Free Stack Memory: " + ESP.getFreeContStack() + "<br>";  
  #endif

  #ifdef _USE32
  WEBHTML = WEBHTML + "Free Internal Heap Memory: " + esp_get_free_internal_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "Free Total Heap Memory: " + esp_get_free_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "Minimum Free Heap: " + esp_get_minimum_free_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "PSRAM Size: " + (String) ESP.getFreePsram() + " / " + (String) ESP.getPsramSize() + "<br>"; 
  
  #endif

  WEBHTML += "Number of devices/sensors: " + (String) Sensors.getNumDevices() + " / " + (String) Sensors.getNumSensors() + "<br>";
  WEBHTML = WEBHTML + "Alive since: " + dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss") + "<br>";
  WEBHTML = WEBHTML + "Last error: " + (String) I.lastError + " @" + (String) (I.lastErrorTime ? dateify(I.lastErrorTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML = WEBHTML + "Last known reset: " + (String) lastReset2String()  + " ";
  WEBHTML = WEBHTML + "<a href=\"/REBOOT_DEBUG\" target=\"_blank\" style=\"display: inline-block; padding: 5px 10px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px; cursor: pointer; font-size: 12px;\">Debug</a><br>";
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML = WEBHTML + "<p><strong>WiFi Status:</strong> " + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "</p>";
  WEBHTML = WEBHTML + "<p><strong>WiFi Mode:</strong> " + getWiFiModeString() + "</p>";

  wifi_mode_t t = WiFi.getMode();

  if (t == WIFI_MODE_APSTA) {
    WEBHTML = WEBHTML + "<p><strong>Connected to:</strong> " + WiFi.SSID() + "</p>";
    WEBHTML = WEBHTML + "<p>APIP: " + WiFi.softAPIP().toString() + ", Station IP: " + WiFi.localIP().toString() + "</p>";
    WEBHTML = WEBHTML + "<p>Stations connected to me: " + (String) WiFi.softAPgetStationNum() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>Router Signal Strength:</strong> " + (String) WiFi.RSSI() + " dBm</p>";
    
  } else if (t == WIFI_MODE_STA) {
    WEBHTML = WEBHTML + "<p>Station IP: " + WiFi.localIP().toString() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>Connected to:</strong> " + WiFi.SSID() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>Signal Strength:</strong> " + (String) WiFi.RSSI() + " dBm</p>";
  
  } else if (t == WIFI_MODE_AP) {
    WEBHTML = WEBHTML + "<p>APIP: " + WiFi.softAPIP().toString() + "</p>";
    WEBHTML = WEBHTML + "<p>Stations connected to me: " + (String) WiFi.softAPgetStationNum() + "</p>";
  } 
  

  WEBHTML = WEBHTML + "<p><strong>Stored/Preferred SSID:</strong> " + String((char*)Prefs.WIFISSID) + "</p>";
  WEBHTML = WEBHTML + "<p><strong>Stored Security Key:</strong> " + String((char*)Prefs.KEYS.ESPNOW_KEY) + "</p>";
  WEBHTML = WEBHTML + "---------------------<br>";      
  #ifdef _USEUDP  
  //in this section we will show incoming and then outgoing UDP message traffic info  
  WEBHTML = WEBHTML + "<h3>UDP Message Traffic</h3>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Outgoing Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.UDP_LAST_OUTGOINGMSG_TIME>0 ? dateify(I.UDP_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Outgoing Message target IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_LAST_OUTGOINGMSG_TO_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Outgoing Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_LAST_OUTGOINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Outgoing Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_OUTGOING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Messages Sent today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_SENDS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Incoming Message Received at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.UDP_LAST_INCOMINGMSG_TIME>0 ? dateify(I.UDP_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Incoming Message from IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_LAST_INCOMINGMSG_FROM_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Incoming Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_LAST_INCOMINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Incoming Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_INCOMING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Messages Received today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_RECEIVES + "</td></tr>";
  WEBHTML = WEBHTML + "</table>";
  WEBHTML = WEBHTML + "---------------------<br>";       
  #endif
//in this section show HTTP traffic info
  WEBHTML = WEBHTML + "<h3>HTTP Message Traffic</h3>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Outgoing Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.HTTP_LAST_OUTGOINGMSG_TIME>0 ? dateify(I.HTTP_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Outgoing Message To IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_OUTGOINGMSG_TO_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Outgoing Message Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_OUTGOINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Outgoing Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_OUTGOING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Messages Sent today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_SENDS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Incoming Message Received at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.HTTP_LAST_INCOMINGMSG_TIME>0 ? dateify(I.HTTP_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Incoming Message from IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_INCOMINGMSG_FROM_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Incoming Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_INCOMINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Incoming Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_INCOMING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Messages Received today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_RECEIVES + "</td></tr>";
  WEBHTML = WEBHTML + "</table>";
  WEBHTML = WEBHTML + "---------------------<br>";       

  //in this section we will show incoming and then outgoing ESPNOW message traffic info  
  WEBHTML = WEBHTML + "<h3>LocalLAN Message Traffic</h3>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Outgoing Message Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_OUTGOINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Outgoing Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.ESPNOW_LAST_OUTGOINGMSG_TIME ? dateify(I.ESPNOW_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Outgoing Message To MAC:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Outgoing Message Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_OUTGOINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">ESPNOW Outgoing Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_OUTGOING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">LAN Messages Sent today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_SENDS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Incoming Message Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_INCOMINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Incoming Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.ESPNOW_LAST_INCOMINGMSG_TIME ? dateify(I.ESPNOW_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Incoming Message Sender:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) MACToString(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last ESPNOW Incoming Message Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_INCOMINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">ESPNOW Incoming Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_INCOMING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">LAN Messages Received today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_RECEIVES + "</td></tr>";
  WEBHTML = WEBHTML + "</table>";
  //add a button here to trigger a broadcast (I'm alive) message
  // Button to trigger a broadcast ("I'm alive") message
  WEBHTML = WEBHTML + R"===(
    <form action="/REQUEST_BROADCAST" method="post" style="margin-bottom:15px;">
      <button type="submit" style="padding:10px 20px; background-color:#2196F3; color:white; border:none; border-radius:4px; font-size:16px; cursor:pointer;">
        Broadcast Now
      </button>
    </form>
  )===";
  #ifdef _USEWEATHER
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML += "Weather last retrieved at: " + (String) (WeatherData.lastUpdateT ? dateify(WeatherData.lastUpdateT,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Weather last failure at: " + (String) (WeatherData.lastUpdateError ? dateify(WeatherData.lastUpdateError,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "NOAA address: " + WeatherData.getGrid(0) + "<br>";
  #endif
  #ifdef _USEGSHEET
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML += "GSheets enabled: " + (String) GSheetInfo.useGsheet + "<br>";
  WEBHTML += "Last GSheets upload time: " + (String) (GSheetInfo.lastGsheetUploadTime ? dateify(GSheetInfo.lastGsheetUploadTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Last GSheets status: " + (String) ((GSheetInfo.lastGsheetUploadSuccess==-2)?"error":((GSheetInfo.lastGsheetUploadSuccess==-1)?"not ready":((GSheetInfo.lastGsheetUploadSuccess==0)?"waiting":((GSheetInfo.lastGsheetUploadSuccess==1)?"success":"unknown")))) + "<br>";
  WEBHTML += "Last GSheets fail count: " + (String) GSheetInfo.uploadGsheetFailCount + "<br>";
  WEBHTML += "Last GSheets interval: " + (String) GSheetInfo.uploadGsheetIntervalMinutes + "<br>";
  WEBHTML += "Last GSheets function: " + (String) GSheetInfo.lastGsheetFunction + "<br>";
  WEBHTML += "Last GSheets response: " + (String) GSheetInfo.lastGsheetResponse + "<br>";
  WEBHTML += "Last GSheets error time: " + (String) (GSheetInfo.lastErrorTime ? dateify(GSheetInfo.lastErrorTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Last GSheets interval: " + (String) GSheetInfo.GsheetUploadIntervalMinutes + "<br>";

  // Button to trigger an immediate Google Sheets upload
  WEBHTML += R"===(
  <form action="/GSHEET_UPLOAD_NOW" method="post">
  <p style="font-family:arial, sans-serif">
  <button type="submit">Upload Google Sheets Now</button>
  </p></form><br>)===";
  #endif
  WEBHTML = WEBHTML + "---------------------<br>";      


  // Device viewer link under server status section
  WEBHTML = WEBHTML + "<br><div style=\"text-align: center; padding: 10px;\">";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 10px; padding: 15px 25px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px; font-size: 16px; font-weight: bold;\">Device Viewer</a>";
  WEBHTML = WEBHTML + "</div>";

  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/InitialSetup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/TimezoneSetup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Timezone Setup</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Configuration</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather Data</a> ";
  WEBHTML = WEBHTML + "<a href=\"/REBOOT_DEBUG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #795548; color: white; text-decoration: none; border-radius: 4px;\">Reboot Debug</a>";
  WEBHTML = WEBHTML + "</div>";
        
  serverTextClose(200);
}


void handleRoot(void) {
//  registerHTTPMessage("MainPage");
    handlerForRoot(false);
}

void handleALL(void) {
  handlerForRoot(true);
}


void serverTextHeader() {
  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) Prefs.DEVICENAME + "</title>";
  WEBHTML = R"===(<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}
  body {  font-family: arial, sans-serif; }
  </style></head>
)===";

}


void serverTextClose(int htmlcode, bool asHTML) {
  
  if (asHTML)   {
    WEBHTML += "</body></html>\n";   
    server.send(htmlcode, "text/html", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client
  }
  else server.send(htmlcode, "text/plain", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client
}


void rootTableFill(byte j) {
//j is the snsindex
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(j);    
  if (sensor) {

    WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) Sensors.getDeviceIPBySnsIndex(j).toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors.getDeviceIPBySnsIndex(j).toString() + "</a></td>";
    WEBHTML = WEBHTML + "<td>" + (String) MACToString(Sensors.getDeviceMACBySnsIndex(j)) + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) sensor->snsName + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) sensor->snsValue + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) sensor->snsType+"."+ (String) sensor->snsID + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) bitRead(sensor->Flags,0) + (String) (bitRead(sensor->Flags,6) ? "*" : "" ) + "</td>";
    
    #ifndef _ISPERIPHERAL
    //print flags as a binary string
    String flagsString = "";
    for (int i = 0; i <8 ; i++) {
      flagsString = flagsString + (String) bitRead(sensor->Flags, i);
    }
    WEBHTML = WEBHTML + "<td>" + flagsString + "</td>";    
    #endif
    WEBHTML = WEBHTML + "<td>" + (String) (sensor->timeLogged ? dateify(sensor->timeLogged,"mm/dd hh:nn:ss") : "???") + "</td>";
    #ifdef _ISPERIPHERAL
    WEBHTML = WEBHTML + "<td>" + (String) ((bitRead(sensor->Flags,7))?"Y":"N") + "</td>";
    #else
    WEBHTML = WEBHTML + "<td>" + (String) ((sensor->expired==0)?((bitRead(sensor->Flags,7))?"N*":"n"):((bitRead(sensor->Flags,7))?"<font color=\"#EE4B2B\">Y*</font>":"<font color=\"#EE4B2B\">y</font>")) + "</td>";
    #endif
    WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA_MOVINGAVERAGE?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&windowSize=1800&numPointsX=48\" target=\"_blank\" rel=\"noopener noreferrer\">AvgHx</a></td>";    
    WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&N=50\" target=\"_blank\" rel=\"noopener noreferrer\">History</a></td>";
    
    #ifdef _ISPERIPHERAL
    // Add editable sensor configuration if this is my sensor
    if (Sensors.isMySensor(j)) {
    
      int16_t prefsIndex = Sensors.getPrefsIndex(j);
      
      if (prefsIndex >= 0 && prefsIndex < _SENSORNUM) {
        // Add a configuration cell with expandable form
        WEBHTML = WEBHTML + "<td style=\"padding: 8px;\">";
        WEBHTML = WEBHTML + "<details>";
        WEBHTML = WEBHTML + "<summary style=\"cursor: pointer; font-weight: bold; color: #4CAF50;\">Sensor Details and Config</summary>";
        WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SENSOR_UPDATE\" style=\"margin-top: 10px; padding: 10px; background-color: #f9f9f9; border: 1px solid #ddd;\">";
        WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsType\" value=\"" + String(sensor->snsType) + "\">";
        WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsID\" value=\"" + String(sensor->snsID) + "\">";
        
        // Sensor Name
        WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
        WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Sensor Name:</label>";
        WEBHTML = WEBHTML + "<input type=\"text\" name=\"sensorName\" maxlength=\"29\" value=\"" + String(sensor->snsName) + "\" style=\"width: 200px; padding: 4px;\">";
        WEBHTML = WEBHTML + "</div>";
        
        // Limits
        WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
        WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Max Limit:</label>";
        WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" name=\"limitMax\" value=\"" + String(Prefs.SNS_LIMIT_MAX[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
        WEBHTML = WEBHTML + "</div>";
        
        WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
        WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Min Limit:</label>";
        WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" name=\"limitMin\" value=\"" + String(Prefs.SNS_LIMIT_MIN[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
        WEBHTML = WEBHTML + "</div>";
        
        // Intervals
        WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
        WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Poll Int (s):</label>";
        WEBHTML = WEBHTML + "<input type=\"number\" name=\"intervalPoll\" value=\"" + String(Prefs.SNS_INTERVAL_POLL[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
        WEBHTML = WEBHTML + "</div>";
        
        WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
        WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Send Int (s):</label>";
        WEBHTML = WEBHTML + "<input type=\"number\" name=\"intervalSend\" value=\"" + String(Prefs.SNS_INTERVAL_SEND[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
        WEBHTML = WEBHTML + "</div>";
        
        // Flags (8 bits)
        WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
        WEBHTML = WEBHTML + "<label style=\"font-weight: bold; display: block; margin-bottom: 4px;\">Flags:</label>";
        WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: repeat(2, 1fr); gap: 4px; margin-left: 10px;\">";
        
        uint16_t currentFlags = Prefs.SNS_FLAGS[prefsIndex];
        const char* flagNames[] = {"Flagged", "Monitored", "LowPower", "Derived/Predictive", "Outside", "High/Low", "Changed", "Critical"};
        // Bits 0, 2, 5, 6 are read-only (system-managed)
        bool readOnlyBits[] = {true, false, true, false, false, true, true, false};
        
        for (int i = 0; i < 8; i++) {
          WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 4px;\">";
          WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit" + String(i) + "\" value=\"1\"";
          if (bitRead(currentFlags, i)) {
            WEBHTML = WEBHTML + " checked";
          }
          if (readOnlyBits[i]) {
            WEBHTML = WEBHTML + " disabled";
          }
          WEBHTML = WEBHTML + ">";
          WEBHTML = WEBHTML + "<span style=\"font-size: 11px;";
          if (readOnlyBits[i]) {
            WEBHTML = WEBHTML + " color: #888;";  // Gray out read-only flags
          }
          WEBHTML = WEBHTML + "\">" + String(i) + ":" + String(flagNames[i]);
          if (readOnlyBits[i]) {
            WEBHTML = WEBHTML + " (auto)";  // Indicate it's automatically managed
          }
          WEBHTML = WEBHTML + "</span>";
          WEBHTML = WEBHTML + "</label>";
        }
        
        WEBHTML = WEBHTML + "</div></div>";
        
        // Submit buttons
        WEBHTML = WEBHTML + "<div style=\"display: flex; gap: 8px; margin-top: 8px;\">";
        WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Update</button>";
        WEBHTML = WEBHTML + "</form>";
        
        // Read & Send Now button (separate form)
        WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SENSOR_READ_SEND_NOW\" style=\"display: inline;\">";
        WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsType\" value=\"" + String(sensor->snsType) + "\">";
        WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsID\" value=\"" + String(sensor->snsID) + "\">";
        WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer;\">Read & Send</button>";
        WEBHTML = WEBHTML + "</form>";
        WEBHTML = WEBHTML + "</div>";
        
        WEBHTML = WEBHTML + "</details>";
        WEBHTML = WEBHTML + "</td>";
    
      } else {
        WEBHTML = WEBHTML + "<td style=\"padding: 8px;\">Configuration data not found because Index out of bounds: " + String(prefsIndex) + "</td>";
      }
          
    } else {
      WEBHTML = WEBHTML + "<td style=\"padding: 8px;\">Sensor " + String(j) + " is not my sensor" + "</td>";
    }

    #endif
    
    WEBHTML = WEBHTML + "</tr>";
  }
}



void handlerForRoot(bool allsensors) {
  WEBHTML.clear();
  WEBHTML = "";
  
  // Check if initial setup is complete
  bool wifi_configured = Prefs.HAVECREDENTIALS && WifiStatus();
  bool timezone_configured = (Prefs.TimeZoneOffset != 0);
  
  #ifdef _USEWEATHER
  bool location_configured = (Prefs.LATITUDE != 0 || Prefs.LONGITUDE != 0);
  bool setup_complete = wifi_configured && timezone_configured && location_configured;
  #else
  bool setup_complete = wifi_configured && timezone_configured;
  #endif
  
  // Redirect to Initial Setup Wizard if not configured
  if (!setup_complete) {
    SerialPrint("Setup incomplete, redirecting to Initial Setup Wizard", true);
    server.sendHeader("Location", "/InitialSetup");
    server.send(302, "text/plain", "Setup incomplete. Redirecting to setup wizard...");
    return;
  }
  
  // Normal root page for fully configured systems
  serverTextHeader();
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Main Page</h2>";

  if (Prefs.HAVECREDENTIALS==true && Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) { //note that lat and lon could be any number, but likelihood of both being 0 is very low, and not in the US


  #ifdef _USEROOTCHART
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";
  #endif
  WEBHTML = WEBHTML + "<body>";  
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";
  
  WEBHTML = WEBHTML + "---------------------<br>";      
    
  
    WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
    WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">System Status</th>";
    #ifndef _ISPERIPHERAL
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">Sensor Status</th>";
    #else
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">Local Sensor Status</th>";
    #endif
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">Device Settings</th>";
    WEBHTML = WEBHTML + "</tr>";
    
    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Device Name: " + (String) Prefs.DEVICENAME + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Critical sensors expired: " + (String) I.isExpired + + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/STATUS\">Status</a></td>";
    WEBHTML = WEBHTML + "</tr>";
    
    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Device Type: " + (String) _MYTYPE + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Flagged sensors: " + (String) I.isFlagged + + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/InitialSetup\" style=\"color: #4CAF50; font-weight: bold;\">WiFi and Startup Settings</a></td>";
    WEBHTML = WEBHTML + "</tr>";
    
    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Device IP: " + (String) WiFi.localIP().toString() + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Soil dry: " + (String) I.isSoilDry + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/CONFIG\">System Configuration</a></td>";
    WEBHTML = WEBHTML + "</tr>";
    
    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Device MAC: " + (String) MACToString(Prefs.PROCID) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Leak detected: " + (String) I.isLeak + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/DEVICEVIEWER\">Device Viewer</a></td>";
    WEBHTML = WEBHTML + "</tr>";

    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Device WiFi: " + (String) WiFi.SSID() + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Temp high: " + (String) I.isHot + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/SDCARD\">SD Card Config</a></td>";
    WEBHTML = WEBHTML + "</tr>";

    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Device WiFi Signal: " + (String) WiFi.RSSI() + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Temp low: " + (String) I.isCold + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/GSHEET\">GSheets Config</a></td>";
    WEBHTML = WEBHTML + "</tr>";

    WEBHTML = WEBHTML + "<tr>";
    #ifndef _ISPERIPHERAL
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Connected Devices/Sensors: " + (String) Sensors.getNumDevices()  + "/" + (String) Sensors.getNumSensors() + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Heat: " + (I.isHeat ? "on" : "off") + ", AC: " + (I.isAC ? "on" : "off") + ", Fan: " + (I.isFan ? "on" : "off") + "</td>";
    #else
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Local Devices/Sensors: " + (String) Sensors.getNumDevices()  + "/" + (String) Sensors.getNumSensors() + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"></td>";
    #endif
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WEATHER\">Weather Data</a></td>";
    WEBHTML = WEBHTML + "</tr>";
    
    
    WEBHTML = WEBHTML + "</table>";
  
  
  WEBHTML = WEBHTML + "---------------------<br>";      

    byte used[NUMSENSORS];
    for (byte j=0;j<NUMSENSORS;j++)  {
      used[j] = 255;
    }

    byte usedINDEX = 0;  


  WEBHTML = WEBHTML + "<p><table id=\"Logs\" style=\"width:100%\">";      
  #ifdef _ISPERIPHERAL
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><button onclick=\"sortTable(0)\">IP Address</button></th><th style=\"width:50px\">ArdID</th><th style=\"width:100px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:75px\"><button onclick=\"sortTable(4)\">Sns Type</button></th><th style=\"width:75px\"><button onclick=\"sortTable(5)\">Flagged</button></th><th style=\"width:100px\">Last Send</th><th style=\"width:50px\">Critical</th><th style=\"width:75px\">Plot Avg</th><th style=\"width:75px\">Plot Raw</th><th style=\"width:300px\">Config</th></tr>"; 
  #else
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><button onclick=\"sortTable(0)\">IP Address</button></th><th style=\"width:50px\">ArdID</th><th style=\"width:100px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:75px\"><button onclick=\"sortTable(4)\">Sns Type</button></th><th style=\"width:75px\"><button onclick=\"sortTable(5)\">Flagged</button></th><th style=\"width:75px\">Flags</button></th><th style=\"width:150px\">Last Recvd</th><th style=\"width:50px\">EXP</th><th style=\"width:75px\">Plot Avg</th><th style=\"width:75px\">Plot Raw</th></tr>"; 
  #endif
  for (byte j=0;j<NUMSENSORS;j++)  {
    ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(j);    
    if (sensor && sensor->IsSet) {
      if (allsensors && bitRead(sensor->Flags,1)==0) continue;
      if (sensor->snsID>0 && sensor->snsType>0 && inIndex(j,used,NUMSENSORS) == false)  {
        used[usedINDEX++] = j;
        rootTableFill(j);
        
        for (byte jj=j+1;jj<NUMSENSORS;jj++) {
          ArborysSnsType* sensor2 = Sensors.getSensorBySnsIndex(jj);    
          if (sensor2 && sensor2->IsSet) {
            if (allsensors && bitRead(sensor2->Flags,1)==0) continue;
            if (sensor2->snsID>0 && sensor2->snsType>0 && inIndex(jj,used,NUMSENSORS) == false && sensor2->deviceIndex==sensor->deviceIndex) {
                used[usedINDEX++] = jj;
                rootTableFill(jj);
            }
          }
        }
      }
    }
  }

    
  WEBHTML += "</table>";   

#ifdef _USEROOTCHART
  //add chart
  WEBHTML += "<br>-----------------------<br>\n";
  WEBHTML += "<div id=\"myChart\" style=\"width:100%; max-width:800px; height:200px;\"></div>\n";
  WEBHTML += "<br>-----------------------<br>\n";
#endif

  WEBHTML += "</p>";


  WEBHTML =WEBHTML  + "<script>";
#ifdef _USEROOTCHART
  
  //chart functions
    WEBHTML =WEBHTML  + "google.charts.load('current',{packages:['corechart']});\n";
    WEBHTML =WEBHTML  + "google.charts.setOnLoadCallback(drawChart);\n";
    
    WEBHTML += "function drawChart() {\n";

    WEBHTML += "const data = google.visualization.arrayToDataTable([\n";
    WEBHTML += "['t','val'],\n";

    for (int jj = 48-1;jj>=0;jj--) {
      WEBHTML += "[" + (String) ((int) ((double) ((LAST_BAT_READ - 60*60*jj)-t)/60)) + "," + (String) batteryArray[jj] + "]";
      if (jj>0) WEBHTML += ",";
      WEBHTML += "\n";
    }
    WEBHTML += "]);\n\n";

    
    // Set Options
    WEBHTML += "const options = {\n";
    WEBHTML += "hAxis: {title: 'min from now'}, \n";
    WEBHTML += "vAxis: {title: 'Battery%'},\n";
    WEBHTML += "legend: 'none'\n};\n";

    WEBHTML += "const chart = new google.visualization.LineChart(document.getElementById('myChart'));\n";
    WEBHTML += "chart.draw(data, options);\n"; 
    WEBHTML += "}\n";
  #endif

  WEBHTML += "function sortTable(col) {  var table, rows, switching, i, x, y, shouldSwitch;table = document.getElementById(\"Logs\");switching = true;while (switching) {switching = false;rows = table.rows;for (i = 1; i < (rows.length - 1); i++) {shouldSwitch = false;x = rows[i].getElementsByTagName(\"TD\")[col];y = rows[i + 1].getElementsByTagName(\"TD\")[col];if (x.innerHTML.toLowerCase() > y.innerHTML.toLowerCase()) {shouldSwitch = true;break;}}if (shouldSwitch) {rows[i].parentNode.insertBefore(rows[i + 1], rows[i]);switching = true;}}}";
  WEBHTML += "</script> \n";

} else {
  if (Prefs.HAVECREDENTIALS==false) {
    SerialPrint("Prefs.HAVECREDENTIALS==false, redirecting to InitialSetup");
    //redirect to WiFi config page
    server.sendHeader("Location", "/InitialSetup");
    serverTextClose(302,false);
    return;
  } else {
    SerialPrint("Prefs.HAVECREDENTIALS==true, redirecting to WEATHER");
    //redirect to weather config page
    server.sendHeader("Location", "/WEATHER");  
    serverTextClose(302,false);
    return;
  }
}

serverTextClose(200);

}




void handleNotFound(){
  registerHTTPMessage("404");
  I.HTTP_INCOMING_ERRORS++;
  WEBHTML= "404: Not found"; // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
  serverTextClose(404,false);

}

void handleRETRIEVEDATA() {
  registerHTTPMessage("DataHx");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  byte  N=100;
  uint8_t snsType = 0, snsID = 0;
  uint64_t deviceMAC = 0;
  uint32_t starttime=0, endtime=0;

  if (server.args()==0) {
    WEBHTML="Inappropriate call... use RETRIEVEDATA?MAC=1234567890AB&type=1&id=1&N=100&starttime=1234567890&endtime=1731761847";
    serverTextClose(401,false);
    return;
  }

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"MAC") {
      String macStr = server.arg(k);
      deviceMAC = strtoull(macStr.c_str(), NULL, 10);
      
    }
    if ((String)server.argName(k) == (String)"type")  snsType=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"id")  snsID=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"N")  N=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"starttime")  starttime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"endtime")  endtime=server.arg(k).toInt(); 
  }

  if (deviceMAC==0 || snsType == 0 || snsID==0) {
    WEBHTML="Inappropriate call... you provided MAC=" + (String) deviceMAC + ", snsType=" + snsType + ", and snsID=" + snsID + ". None of these can be zero";
    serverTextClose(302,false);
    return;
  }

  if (N>100) N=100; //don't let the array get too large!
  if (N==0) N=50;
  
  if (endtime<=0) endtime=-1; //this makes endtime the max value, will just read N values.

  uint32_t t[N]={0};
  double v[N]={0};
  uint8_t f[N]={0};
  uint32_t sampn=0; //sample number
  
  bool success=false;
  if (starttime>0) {
    #ifdef _ISPERIPHERAL
    success = retrieveSensorDataFromMemory(deviceMAC, snsType, snsID, &N, t, v, f, starttime, endtime,true);
    
    #else
    success = retrieveSensorDataFromSD(deviceMAC, snsType, snsID, &N, t, v, f, starttime, endtime,true);
    
    #endif
  
  } else    {
    #ifdef _ISPERIPHERAL
    success = retrieveSensorDataFromMemory(deviceMAC, snsType, snsID, &N, t, v, f, 0,endtime,true);
    
    #else
    success = retrieveSensorDataFromSD(deviceMAC, snsType, snsID, &N, t, v, f, 0,endtime,true); //this fills from tn backwards to N*delta samples
    
    #endif
  }
  if (success == false)  {
    WEBHTML= "Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  SerialPrint("handleRETRIEVEDATA: N=" + (String) N + " t[0]=" + (String) t[0] + " t[N-1]=" + (String) t[N-1] + " v[0]=" + (String) v[0] + " v[N-1]=" + (String) v[N-1]);
  
  addPlotToHTML(t,v,N,deviceMAC,snsType,snsID);
}

void handleRETRIEVEDATA_MOVINGAVERAGE() {
  registerHTTPMessage("AvgHx");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  if (server.args()==0) {
    WEBHTML="Inappropriate call... use RETRIEVEDATA_MOVINGAVERAGE?MAC=1234567890AB&type=1&id=1&starttime=1234567890&endtime=1731761847&windowSize=10&numPointsX=10 or RETRIEVEDATA_MOVINGAVERAGE?MAC=1234567890AB&type=1&id=1&endtime=1731761847&windowSize=10&numPointsX=10";
    serverTextClose(401,false);
    return;
  }


  uint64_t deviceMAC = 0;
  uint8_t snsType = 0, snsID = 0;
  uint32_t starttime=0, endtime=0;
  uint32_t windowSizeN=0;
  uint16_t numPointsX=0;

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"MAC") {
      String macStr = server.arg(k);
       deviceMAC = strtoull(macStr.c_str(), NULL, 10);
    }
    if ((String)server.argName(k) == (String)"type")  snsType=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"id")  snsID=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"starttime")  starttime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"endtime")  endtime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"windowSizeN")  windowSizeN=server.arg(k).toInt();
    if ((String)server.argName(k) == (String)"windowSize")  windowSizeN=server.arg(k).toInt(); // New parameter name 
    if ((String)server.argName(k) == (String)"numPointsX")  numPointsX=server.arg(k).toInt(); 
  }

  if (deviceMAC==0 || snsType == 0 || snsID==0) {
    WEBHTML="Inappropriate call... invalid device MAC or sensor ID";
    serverTextClose(302,false);
    return;
  }

  if (windowSizeN==0) windowSizeN=30*60; //in minutes
  if (numPointsX==0 || numPointsX>100) numPointsX=0;

  if (endtime==0) endtime=-1;
  if (starttime>=endtime) {
    WEBHTML="Inappropriate call... starttime >= endtime";
    serverTextClose(302,false);
    return;
  }

  uint32_t t[100]={0};
  double v[100]={0};
  uint8_t f[100]={0};
  bool success=false;

  #ifdef _ISPERIPHERAL
  success = retrieveMovingAverageSensorDataFromMemory(deviceMAC, snsType, snsID, starttime, endtime, windowSizeN, &numPointsX, v, t, f,true);
  #else
  success = retrieveMovingAverageSensorDataFromSD(deviceMAC, snsType, snsID, starttime, endtime, windowSizeN, &numPointsX, v, t, f,true);
  #endif
  
  if (success == false)  {
    WEBHTML= "handleRETRIEVEDATA_MOVINGAVERAGE: Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  addPlotToHTML(t,v,numPointsX,deviceMAC,snsType,snsID);
}

void addPlotToHTML(uint32_t t[], double v[], byte N, uint64_t deviceMAC, uint8_t snsType, uint8_t snsID) {


  // Find sensor in Devices_Sensors class
  int16_t sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);

  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) Prefs.DEVICENAME + "</title>\n";
  WEBHTML =WEBHTML  + (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}";
  WEBHTML =WEBHTML  + (String) "body {  font-family: arial, sans-serif; }";
  WEBHTML =WEBHTML  + "</style></head>";
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h1>" + (String) Prefs.DEVICENAME + "</h1>";
  WEBHTML = WEBHTML + "<br>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br>\n";

  WEBHTML = WEBHTML + "<p>";

  if (sensorIndex<0)   WEBHTML += "WARNING!! Device: " + String(deviceMAC, HEX) + " sensor type: " + (String) snsType + " id: " + (String) snsID + " was NOT found in the active list, though I did find an associated file. <br>";
  else {
    WEBHTML += "Request for Device: " + String(deviceMAC, HEX) + " sensor: " + (String) sensor->snsName + " type: " + (String) sensor->snsType + " id: " + (String) sensor->snsID + "<br>";
  }

  WEBHTML += "Start time: " + (String) dateify(t[0],"mm/dd/yyyy hh:nn:ss") + " to " + (String) dateify(t[N-1],"mm/dd/yyyy hh:nn:ss")  +  "<br>";
  
  //add chart
  WEBHTML += "<br>-----------------------<br>\n";
  WEBHTML += "<div id=\"myChart\" style=\"width:100%; max-width:800px; height:600px;\"></div>\n";
  WEBHTML += "<br>-----------------------<br>\n";

  WEBHTML += "</p>\n";

  WEBHTML =WEBHTML  + "<script>";

  //chart functions
    WEBHTML =WEBHTML  + "google.charts.load('current',{packages:['corechart']});\n";
    WEBHTML =WEBHTML  + "google.charts.setOnLoadCallback(drawChart);\n";
    
    WEBHTML += "function drawChart() {\n";

    WEBHTML += "const data = google.visualization.arrayToDataTable([\n";
    WEBHTML += "['t','val'],\n";

    for (byte jj = 0;jj<N;jj++) {
      if (isTimeValid(t[jj])==false) continue;
      WEBHTML += "[" + (String) (int64_t) (((int64_t) t[jj] - (int64_t) t[N-1])/60) + "," + (String) v[jj] + "]";
      if (jj<N-1) WEBHTML += ",";
      WEBHTML += "\n";
    }
    WEBHTML += "]);\n\n";

        // Set Options
    WEBHTML += "const options = {\n";
    if (sensor) {
      WEBHTML += "hAxis: {title: 'Historical data for " + (String) sensor->snsName + ", minutes from last'}, \n";
    } else {
      WEBHTML += "hAxis: {title: 'Historical data in hours'}, \n";
    }
    WEBHTML += "vAxis: {title: 'Value'},\n";
    WEBHTML += "legend: 'none'\n};\n";

    WEBHTML += "const chart = new google.visualization.LineChart(document.getElementById('myChart'));\n";
    WEBHTML += "chart.draw(data, options);\n"; 
    WEBHTML += "}\n";  

  WEBHTML += "</script> \n";
    WEBHTML += "Returned " + (String) N + " avg samples. <br>\n";

    WEBHTML += "unixtime,value<br>\n";
  for (byte j=0;j<N;j++)     WEBHTML += (String) t[j] + "," + (String) v[j] + "<br>\n";
  WEBHTML += "</body></html>";
  
  serverTextClose(200);
}



void handleCONFIG() {
  registerHTTPMessage("CONFIG");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
//  SerialPrint("config: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " System Configuration</h2>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

  // Navigation buttons
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px;\">Devices</a> ";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a>";
  WEBHTML = WEBHTML + "</div>";

  WEBHTML = WEBHTML + "<p>This page is used to configure editable system parameters.</p>";
  
  // Start the form and grid container
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/CONFIG\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Editable fields from STRUCT_CORE I in specified order
  // Timezone configuration is now handled through the timezone setup page
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Timezone Configuration</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><a href=\"/TimezoneSetup\" style=\"padding: 8px 16px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">Configure Timezone Settings</a></div>";
  
  #ifdef _USETFT
  // CYCLE fields
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">cycleHeaderMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleHeaderMinutes\" value=\"" + (String) I.cycleHeaderMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">cycleWeatherMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleWeatherMinutes\" value=\"" + (String) I.cycleWeatherMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">cycleCurrentConditionMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleCurrentConditionMinutes\" value=\"" + (String) I.cycleCurrentConditionMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">cycleFutureConditionsMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleFutureConditionsMinutes\" value=\"" + (String) I.cycleFutureConditionsMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">cycleFlagSeconds</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleFlagSeconds\" value=\"" + (String) I.cycleFlagSeconds + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">IntervalHourlyWeather</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"IntervalHourlyWeather\" value=\"" + (String) I.IntervalHourlyWeather + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  #endif

  // Device Name Configuration
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Device Name</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" name=\"deviceName\" value=\"" + String(Prefs.DEVICENAME) + "\" maxlength=\"32\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  #ifdef _USETFT
  // showTheseFlags - 8 individual checkboxes for each bit
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">showTheseFlags (Display Settings)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px;\">";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit0\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 0) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 0 (Show if flagged)</label>";
  
  // Bit 1 - Set for expired only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit1\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 1) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 1 (Show if expired)</label>";
  
  // Bit 2 - Set for soil dry only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit2\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 2) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 2 (Soil)</label>";
  
  // Bit 3 - Set for leak only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit3\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 3) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 3 (Leak)</label>";
  
  // Bit 4 - Set for temperature only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit4\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 4) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 4 (Temperature)</label>";
  
  // Bit 5 - Set for humidity only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit5\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 5) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 5 (Humidity)</label>";
  
  // Bit 6 - Set for pressure only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit6\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 6) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 6 (Pressure)</label>";
  
  // Bit 7 - Set for battery only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit7\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 7) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 7 (Battery)</label>";

  // Bit 8 - Set for HVAC only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit8\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 8) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 8 (HVAC)</label>";

  WEBHTML = WEBHTML + "</div>";

  WEBHTML = WEBHTML + "</div>";
  #endif
  
  
  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Reset button
  WEBHTML = WEBHTML + "<br><form method=\"POST\" action=\"/CONFIG_DELETE\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Reset All Settings\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleREADONLYCOREFLAGS() {
  registerHTTPMessage("CoreFlags");

  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Read-Only Core Flags</h2>";
  WEBHTML = WEBHTML + "<p>This page displays non-editable system parameters.</p>";
  
  // Start the grid container
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Non-editable fields from STRUCT_CORE I in specified order
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">currentTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.currentTime ? dateify(I.currentTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">ALIVESINCE</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.ALIVESINCE ?  dateify(I.ALIVESINCE) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastResetTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastResetTime ?  dateify(I.lastResetTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">wifiFailCount</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.wifiFailCount + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastError</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastError) + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastErrorTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastErrorTime ?  dateify(I.lastErrorTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastErrorCode</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.lastErrorCode + "</div>";
  

  //HTTP message traffic
  WEBHTML = WEBHTML + "<h3>HTTP Message Traffic</h3>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Incoming Message Received at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.HTTP_LAST_INCOMINGMSG_TIME>0 ? dateify(I.HTTP_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Incoming Message from IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_INCOMINGMSG_FROM_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Incoming Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_INCOMINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Incoming Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_INCOMING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Outgoing Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.HTTP_LAST_OUTGOINGMSG_TIME>0 ? dateify(I.HTTP_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Outgoing Message To IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_OUTGOINGMSG_TO_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last HTTP Outgoing Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.HTTP_LAST_OUTGOINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "</table>";

  //LAN message traffic
  WEBHTML = WEBHTML + "<h3>LAN Message Traffic</h3>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last LAN Incoming Message Received at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.ESPNOW_LAST_INCOMINGMSG_TIME>0 ? dateify(I.ESPNOW_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last LAN Incoming Message from MAC:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) MACToString(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last LAN Incoming Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_INCOMINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">LAN Incoming Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_INCOMING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last LAN Outgoing Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.ESPNOW_LAST_OUTGOINGMSG_TIME>0 ? dateify(I.ESPNOW_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last LAN Outgoing Message To MAC:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last LAN Outgoing Message:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_OUTGOINGMSG_TYPE + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">LAN Outgoing Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_OUTGOING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "</table>";

  //UDP message traffic
  WEBHTML = WEBHTML + "<h3>UDP Message Traffic</h3>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Message Received at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.UDP_LAST_INCOMINGMSG_TIME>0 ? dateify(I.UDP_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Message from IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_LAST_INCOMINGMSG_FROM_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Incoming Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_INCOMING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Outgoing Message Sent at:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) (I.UDP_LAST_OUTGOINGMSG_TIME>0 ? dateify(I.UDP_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last UDP Outgoing Message To IP:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_LAST_OUTGOINGMSG_TO_IP.toString() + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Outgoing Errors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + (String) I.UDP_OUTGOING_ERRORS + "</td></tr>";
  WEBHTML = WEBHTML + "</table>";


  // Remaining non-editable fields from I in order of appearance
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">resetInfo</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.resetInfo + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">rebootsSinceLast</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.rebootsSinceLast + "</div>";

  #ifdef _USETFT
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">CLOCK_Y</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.CLOCK_Y + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">HEADER_Y</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.HEADER_Y + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastHeaderTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (String) (I.lastHeaderTime ? dateify(I.lastHeaderTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastWeatherTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastWeatherTime ? dateify(I.lastWeatherTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastClockDrawTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastClockDrawTime ? dateify(I.lastClockDrawTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastFlagViewTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastFlagViewTime ? dateify(I.lastFlagViewTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastCurrentConditionTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastCurrentConditionTime ? dateify(I.lastCurrentConditionTime) : "???") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">IntervalHourlyWeather</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.IntervalHourlyWeather + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">touchX</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.touchX + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">touchY</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.touchY + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">alarmIndex</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.alarmIndex + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">ScreenNum</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ScreenNum + "</div>";
  #endif

  #ifdef _USEWEATHER
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">currentTemp</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.currentTemp + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Tmax</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.Tmax + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Tmin</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.Tmin + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">localWeatherIndex</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.localWeatherIndex + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastCurrentTemp</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.lastCurrentTemp + "</div>";
  #endif

  #ifdef _USEBATTERY
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">localBatteryIndex</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.localBatteryIndex + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">localBatteryLevel</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.localBatteryLevel + "</div>";
  #endif

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isExpired</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.isExpired ? "true" : "false") + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isFlagged</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isFlagged + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">wasFlagged</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.wasFlagged ? "true" : "false") + "</div>";

  #ifndef _ISPERIPHERAL
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isHeat</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isHeat + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isAC</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isAC + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isFan</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isFan + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">wasHeat</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.wasHeat + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">wasAC</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.wasAC + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">wasFan</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.wasFan + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isHot</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isHot + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isCold</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isCold + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isSoilDry</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isSoilDry + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">isLeak</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.isLeak + "</div>";
  #endif

  WEBHTML = WEBHTML + "</div>";
  
  // Navigation links
  WEBHTML = WEBHTML + "<br><br><a href=\"/CONFIG\">Edit Configuration</a> | ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\">View Google Sheets Configuration</a> | ";
  WEBHTML = WEBHTML + "<a href=\"/\">Back to Main Page</a>";
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleGSHEET() {
  registerHTTPMessage("GSHEET");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  #if defined(_USEGSHEET)


  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Google Sheets Configuration</h2>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

  // Navigation buttons
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px;\">Devices</a> ";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a>";
  WEBHTML = WEBHTML + "</div>";

  WEBHTML = WEBHTML + "<p>This page displays Google Sheets configuration and status.</p>";
  
  // Start the form and grid container
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/GSHEET\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Google Sheets Configuration Section (Editable)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9; font-weight: bold;\">Google Sheets Configuration (Editable)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">useGsheet</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"checkbox\" name=\"useGsheet\" value=\"1\"" + ((GSheetInfo.useGsheet) ? " checked" : "") + " style=\"width: 20px; height: 20px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">uploadGsheetIntervalMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"uploadGsheetIntervalMinutes\" value=\"" + (String) GSheetInfo.uploadGsheetIntervalMinutes + "\" min=\"1\" max=\"1440\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  // Google Sheets Credentials Section (Read-only)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9; font-weight: bold;\">Google Sheets Credentials (Read-only)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Project ID</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.projectID) + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Client Email</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.clientEmail) + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">User Email</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.userEmail) + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Private Key</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">[Hidden for security]</div>";
  
  // Google Sheets Status Information (Read-only)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9; font-weight: bold;\">Google Sheets Status Information</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9;\"></div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetUploadSuccess</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) GSheetInfo.lastGsheetUploadSuccess + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">uploadGsheetFailCount</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) GSheetInfo.uploadGsheetFailCount + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastErrorTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) ((GSheetInfo.lastErrorTime) ? dateify(GSheetInfo.lastErrorTime) : "N/A") + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetResponse</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.lastGsheetResponse) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetFunction</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.lastGsheetFunction) + "</div>";


  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Google Sheets Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Upload now button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_UPLOAD_NOW\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Upload to Gsheets Immediately\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Share all sheets button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_SHARE_ALL\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Share Device Sheets\" style=\"padding: 10px 20px; background-color: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Delete all sheets button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_DELETE_ALL\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete All Sheets\" style=\"padding: 10px 20px; background-color: #F44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  #else
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>Google Sheets Configuration</h2>";
  WEBHTML = WEBHTML + "<p>Google Sheets upload is not available on this device</p>";
  #endif
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleGSHEET_POST() {
  registerHTTPMessage("GSHEETOut");

  #if defined(_USEGSHEET) 
  // Process Google Sheets configuration
  if (server.hasArg("useGsheet")) {
    GSheetInfo.useGsheet = true;
  } else {
    GSheetInfo.useGsheet = false;
  }
  if (server.hasArg("uploadGsheetIntervalMinutes")) {
    GSheetInfo.uploadGsheetIntervalMinutes = server.arg("uploadGsheetIntervalMinutes").toInt();
  }
  
  // Validate Google Sheets configuration
  if (GSheetInfo.uploadGsheetIntervalMinutes < 1) {
    GSheetInfo.uploadGsheetIntervalMinutes = 1;
  }
  if (GSheetInfo.uploadGsheetIntervalMinutes > 1440) {
    GSheetInfo.uploadGsheetIntervalMinutes = 1440;
  }
  
  #if defined(_USESDCARD)
  // Save to SD card
  if (storeGsheetInfoSD()) {
    SerialPrint("Google Sheets configuration saved to SD card", true);
  } else {
    SerialPrint("Failed to save Google Sheets configuration to SD card", true);
  }
  #endif

  // Redirect back to the Google Sheets configuration page
  server.sendHeader("Location", "/GSHEET");
  #else
  server.sendHeader("Location", "/CONFIG");
  #endif
  
  server.send(302, "text/plain", "");
}

void handleGSHEET_UPLOAD_NOW() {
  registerHTTPMessage("GSHEETUp");
  #ifdef _USEGSHEET
  int8_t result = Gsheet_uploadData();
  String msg = "Triggered immediate upload. Result: " + String(result) + ", " + GsheetUploadErrorString();
  
  #else
  String msg = "GSHEET upload not enabled on this device";
  #endif
  server.send(200, "text/plain", msg);
}

void handleGSHEET_SHARE_ALL() {
  registerHTTPMessage("GSHEETShare");
  #ifdef _USEGSHEET
  bool result = shareAllGsheets();
  String msg = "Share all sheets triggered. Result: " + String(result ? "Success" : "Failed");
  SerialPrint(msg, true);
  #else
  String msg = "GSHEET upload not enabled on this device";
  #endif
  server.send(200, "text/plain", msg);
}

void handleGSHEET_DELETE_ALL() {
  registerHTTPMessage("GSHEETDelete");
  #ifdef _USEGSHEET
  file_deleteAllSheets();
  String msg = "Delete all sheets triggered.";
  SerialPrint(msg, true);
  #else
  String msg = "GSHEET upload not enabled on this device";
  #endif
  server.send(200, "text/plain", msg);
}

void handleREQUEST_BROADCAST() {
  registerHTTPMessage("Broadcast");
  
  // Trigger broadcast by calling the broadcastServerPresence function
  bool result = broadcastServerPresence(true,2);
  
  String msg = "Broadcast triggered. Result: " + String(result ? "Success" : "Failed");
  SerialPrint(msg, true);
  
  // Redirect back to the status page
  server.sendHeader("Location", "/STATUS");
  server.send(302, "text/plain", (result)?"Success":"Failed");
  
}

// Generate AP SSID based on MAC address: "SensorNet-" + last 3 bytes of MAC in hex
String generateAPSSID() {
    char ssid[20];
    // Format: "SensorNet-" + last 3 bytes of MAC in hex (6 characters)
    snprintf(ssid, sizeof(ssid), "SensorNet-%02X%02X%02X", 
             getPROCIDByte(Prefs.PROCID, 3), getPROCIDByte(Prefs.PROCID, 4), getPROCIDByte(Prefs.PROCID, 5));
    return String(ssid);
}

// Connect to Soft AP mode (combined AP-station mode)
void connectSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP) {
  if (WiFi.getMode() != WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_MODE_APSTA); // Combined AP and station mode
  }

  *wifiID = generateAPSSID();
  *apIP = IPAddress(192, 168, 4, 1);
  *wifiPWD = "S3nsor.N3t!";
  

  SerialPrint("Setting config for soft AP", true);
  // Configure AP with proper error handling
  if (!WiFi.softAPConfig(*apIP, *apIP, IPAddress(255, 255, 255, 0))) {
    SerialPrint("Failed to configure AP", true);
    return;
  } 
  SerialPrint("Config for soft AP set", true);
  
  if (!WiFi.softAP(wifiID->c_str(), wifiPWD->c_str())) {
    SerialPrint("Failed to start AP", true);
    return;
  }
  SerialPrint("AP starting (please wait)", true);

  
}

void handleCONFIG_POST() {
  registerHTTPMessage("ConfigIn");
  
  // Process form submissions and update editable fields
  // Timezone configuration is now handled through the timezone setup page

  #if defined(_USEWEATHER) && defined(_USETFT)
  if (server.hasArg("cycleCurrentConditionMinutes")) {
    I.cycleCurrentConditionMinutes = server.arg("cycleCurrentConditionMinutes").toInt();
  }
  if (server.hasArg("cycleFlagSeconds")) {
    I.cycleFlagSeconds = server.arg("cycleFlagSeconds").toInt();
  }
  if (server.hasArg("cycleFutureConditionsMinutes")) {
    I.cycleFutureConditionsMinutes = server.arg("cycleFutureConditionsMinutes").toInt();
  }
  if (server.hasArg("IntervalHourlyWeather")) {
    I.IntervalHourlyWeather = server.arg("IntervalHourlyWeather").toInt();
  }
  if (server.hasArg("screenChangeTimer")) {
    I.screenChangeTimer = server.arg("screenChangeTimer").toInt();
  }
  if (server.hasArg("cycleHeaderMinutes")) {
    I.cycleHeaderMinutes = server.arg("cycleHeaderMinutes").toInt();
  }
  if (server.hasArg("cycleWeatherMinutes")) {
    I.cycleWeatherMinutes = server.arg("cycleWeatherMinutes").toInt();
  }
  #endif
  
  // Process device name
  if (server.hasArg("deviceName")) {
    String newDeviceName = server.arg("deviceName");
    if (newDeviceName.length() > 0 && newDeviceName.length() <= 32) {
      snprintf((char*)Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", newDeviceName.c_str());
      Prefs.isUpToDate = false; // Mark as needing to be saved

      //now update the DeviceStore with the new device name
      int16_t deviceIndex = Sensors.findMyDeviceIndex();
      if (!Sensors.updateDeviceName(deviceIndex, newDeviceName)) {
        SerialPrint("Failed to update device name", true);
        storeError("Failed to update device name");
      } 
    }
  }
  
#if defined(_USETFT) && !defined(_ISPERIPHERAL)
  // Process showTheseFlags checkboxes - reconstruct byte from individual bits
  I.showTheseFlags = 0; // Start with all bits cleared
  
  if (server.hasArg("flag_bit0")) {
    bitSet(I.showTheseFlags, 0);
  }
  if (server.hasArg("flag_bit1")) {
    bitSet(I.showTheseFlags, 1);
  }
  if (server.hasArg("flag_bit2")) {
    bitSet(I.showTheseFlags, 2);
  }
  if (server.hasArg("flag_bit3")) {
    bitSet(I.showTheseFlags, 3);
  }
  if (server.hasArg("flag_bit4")) {
    bitSet(I.showTheseFlags, 4);
  }
  if (server.hasArg("flag_bit5")) {
    bitSet(I.showTheseFlags, 5);
  }
  if (server.hasArg("flag_bit6")) {
    bitSet(I.showTheseFlags, 6);
  }
  if (server.hasArg("flag_bit7")) {
    bitSet(I.showTheseFlags, 7);
  }
#endif


#if defined(_USEWEATHER) && defined(_USETFT)
  //check for invalid values
if (I.cycleHeaderMinutes < 1) {
  I.cycleHeaderMinutes = 1;
}
if (I.cycleWeatherMinutes < 1) {
  I.cycleWeatherMinutes = 1;
}
if (I.cycleFutureConditionsMinutes < 1) {
  I.cycleFutureConditionsMinutes = 1;
}
if (I.cycleCurrentConditionMinutes < 1) {
  I.cycleCurrentConditionMinutes = 1;
}
if (I.screenChangeTimer < 1) {
  I.screenChangeTimer = 1;
}
if (I.IntervalHourlyWeather < 1) {
  I.IntervalHourlyWeather = 1;
}
if (I.cycleHeaderMinutes > 120) {
  I.cycleHeaderMinutes = 120;
}
if (I.cycleWeatherMinutes > 120) {
  I.cycleWeatherMinutes = 120;
}
if (I.cycleFutureConditionsMinutes > 120) {
  I.cycleFutureConditionsMinutes = 120;
}
if (I.cycleCurrentConditionMinutes > 60) {
  I.cycleCurrentConditionMinutes = 60;
}
if (I.screenChangeTimer > 120) {
  I.screenChangeTimer = 120;
}
if (I.IntervalHourlyWeather > 4) {
  I.IntervalHourlyWeather = 4;
}
#endif

  // Save the updated configuration to persistent storage
  I.isUpToDate = false;
  
  // Redirect back to the configuration page
  server.sendHeader("Location", "/CONFIG");
  server.send(302, "text/plain", "Configuration updated. Redirecting...");
}

void handleCONFIG_DELETE() {
  registerHTTPMessage("ConfigDel");
  
  // Delete all config data
  int8_t ret =delete_all_core_data(true,true); 
   //just reboot, regardless of any failures
   
  Prefs.isUpToDate = true; //this prevents prefs from updating in controlled reboot
  controlledReboot("User Request",RESET_USER,true);
}

#ifdef _ISPERIPHERAL
void handleSENSOR_UPDATE_POST() {
  registerHTTPMessage("SnsUpd");
  
  // Get sensor identifiers from form
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing sensor identifiers");
    return;
  }
  
  uint8_t snsType = server.arg("snsType").toInt();
  uint8_t snsID = server.arg("snsID").toInt();
  
  // Get the prefs index for this sensor
  int16_t prefsIndex = Sensors.getPrefsIndex(snsType, snsID,-1);
  SerialPrint("Updating sensor: " + String(snsType) + "." + String(snsID) + " with Prefs index: " + String(prefsIndex),true);
  
  if (prefsIndex < 0 || prefsIndex >= _SENSORNUM) {
    server.send(400, "text/plain", "Invalid sensor or sensor not found");
    return;
  }
  
  // First, get the sensor object so we can update it
  int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  ArborysSnsType* sensor = nullptr;
  if (snsIndex >= 0) {
    sensor = Sensors.getSensorBySnsIndex(snsIndex);
  }
  
  // Update sensor name
  if (server.hasArg("sensorName") && sensor) {
    String newName = server.arg("sensorName");
    if (newName.length() > 0 && newName.length() < 30) {
      strncpy(sensor->snsName, newName.c_str(), 29);
      sensor->snsName[29] = '\0'; // Ensure null termination
      SerialPrint("Updated sensor name to: " + newName, true);
    }
  }
  
  // Update sensor limits
  if (server.hasArg("limitMax")) {
    Prefs.SNS_LIMIT_MAX[prefsIndex] = server.arg("limitMax").toDouble();
  }
  if (server.hasArg("limitMin")) {
    Prefs.SNS_LIMIT_MIN[prefsIndex] = server.arg("limitMin").toDouble();
  }
  
  // Update intervals
  if (server.hasArg("intervalPoll")) {
    Prefs.SNS_INTERVAL_POLL[prefsIndex] = server.arg("intervalPoll").toInt();
  }
  if (server.hasArg("intervalSend")) {
    Prefs.SNS_INTERVAL_SEND[prefsIndex] = server.arg("intervalSend").toInt();
  }
  
  // Update flags - reconstruct from individual bits
  uint16_t flags = 0;
  for (int i = 0; i < 8; i++) {
    String flagName = "flag_bit" + String(i);
    if (server.hasArg(flagName)) {
      bitSet(flags, i);
    }
  }
  Prefs.SNS_FLAGS[prefsIndex] = flags;
  
  // Also update the sensor's flags in the Sensors array
  if (sensor) {
    sensor->Flags = flags;
    sensor->SendingInt = Prefs.SNS_INTERVAL_SEND[prefsIndex];
  }
  
  // Mark Prefs as needing to be saved
  Prefs.isUpToDate = false;
  
  // Redirect back to root page
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Sensor configuration updated. Redirecting...");
}

void handleSENSOR_READ_SEND_NOW() {
  registerHTTPMessage("ReadReq");
    
  // Get sensor identifiers from form
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing sensor identifiers");
    return;
  }
  
  uint8_t snsType = server.arg("snsType").toInt();
  uint8_t snsID = server.arg("snsID").toInt();
  
  // Find the sensor
  int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  if (snsIndex < 0) {
    server.send(400, "text/plain", "Sensor not found");
    return;
  }
  
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
  if (!sensor) {
    server.send(400, "text/plain", "Invalid sensor");
    return;
  }
  
  // Force read the sensor data
  int8_t readResult = ReadData(sensor, true); // forceRead = true

  SendData(snsIndex,true,-1,true); //send the data to the servers using broadcast udp

  String resultMsg = "";
  if (readResult == 1) {
    resultMsg = "Sensor read successfully. Value: " + String(sensor->snsValue);
    SerialPrint("Forced sensor read: " + String(sensor->snsName) + " = " + String(sensor->snsValue), true);
  } else if (readResult == -1) {
    resultMsg = "Error: Not my sensor";
  } else if (readResult == -2) {
    resultMsg = "Error: Not registered";
  } else if (readResult == -10) {
    resultMsg = "Error: Reading invalid";
  } else {
    resultMsg = "Sensor read status: " + String(readResult);
  }
  
  
  SerialPrint(resultMsg, true);
  
  // Redirect back to root page
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", resultMsg);
}
#endif


// Timezone detection function
bool getTimezoneInfo(int32_t* utc_offset, bool* dst_enabled, uint8_t* dst_start_month, uint8_t* dst_start_day, uint8_t* dst_end_month, uint8_t* dst_end_day) {
  HTTPClient http;
  WiFiClient client;
  
  String url = "http://worldtimeapi.org/api/ip";
  http.begin(client, url);
  
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    
    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      SerialPrint("JSON parsing failed: " + String(error.c_str()), true);
      return false;
    }
    
    // Extract timezone information
    String utc_offset_str = doc["raw_offset"]; //note that raw offset does not include the dst offset, and is already in seconds
    bool dst = doc["dst"];
    String dst_from = doc["dst_from"];
    String dst_until = doc["dst_until"];
    
    /*
    // Parse UTC offset (format: "+05:00" or "-04:00")
    if (utc_offset_str.length() >= 6) {
      int hours = utc_offset_str.substring(1, 3).toInt();
      int minutes = utc_offset_str.substring(4, 6).toInt();
      *utc_offset = (utc_offset_str[0] == '-' ? -1 : 1) * (hours * 3600 + minutes * 60);
    }
    */
    *utc_offset = utc_offset_str.toInt();
    *dst_enabled = dst;
    
    // Parse DST start and end dates (format: "2025-03-09T07:00:00+00:00")
    if (dst_from.length() >= 10) {
      *dst_start_month = dst_from.substring(5, 7).toInt();
      *dst_start_day = dst_from.substring(8, 10).toInt();
    }
    
    if (dst_until.length() >= 10) {
      *dst_end_month = dst_until.substring(5, 7).toInt();
      *dst_end_day = dst_until.substring(8, 10).toInt();
    }

    return true;
  }
  
  http.end();
  return false;
}


void handleTimezoneSetup() {
  registerHTTPMessage("TZSetup");
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>Timezone Configuration</h2>";
  

  // Get timezone information from API
  int32_t utc_offset = Prefs.TimeZoneOffset;
  bool dst_enabled = Prefs.DST;
  uint8_t dst_start_month = Prefs.DSTStartMonth;
  uint8_t dst_start_day = Prefs.DSTStartDay;
  uint8_t dst_end_month = Prefs.DSTEndMonth;
  uint8_t dst_end_day = Prefs.DSTEndDay;

  if (server.hasArg("utc_offset_seconds")) {
    utc_offset = server.arg("utc_offset_seconds").toInt();
  }
  if (server.hasArg("dst_enabled")) {
    dst_enabled = server.arg("dst_enabled").toInt();
  }
  if (server.hasArg("dst_start_month")) {
    dst_start_month = server.arg("dst_start_month").toInt();
  }
  if (server.hasArg("dst_start_day")) {
    dst_start_day = server.arg("dst_start_day").toInt();
  }
  if (server.hasArg("dst_end_month")) {
    dst_end_month = server.arg("dst_end_month").toInt();
  }
  if (server.hasArg("dst_end_day")) {
    dst_end_day = server.arg("dst_end_day").toInt();
  }
  
  
  if (Prefs.TimeZoneOffset == 0 && WifiStatus()) {
    WEBHTML = WEBHTML + "<p>Detecting timezone information...</p>";
    if (getTimezoneInfo(&utc_offset, &dst_enabled, &dst_start_month, &dst_start_day, &dst_end_month, &dst_end_day)) {
      WEBHTML = WEBHTML + "<p style=\"color: green;\">Timezone information detected successfully!</p>";
    } else {
      WEBHTML = WEBHTML + "<p style=\"color: red;\">Failed to detect timezone information. Using defaults.</p>";
      // Set some reasonable defaults
      utc_offset = -18000; // EST (UTC-5)
      dst_enabled = true;
      dst_start_month = 3; dst_start_day = 10;
      dst_end_month = 11; dst_end_day = 3;
    }
  }  
  
  // Convert UTC offset to hours and minutes for display
  int32_t offset_seconds = utc_offset;
  
  WEBHTML = WEBHTML + "<p><strong>UTC Offset:</strong> " + offset_seconds + "</p>";
  WEBHTML = WEBHTML + "<p><strong>DST Enabled:</strong> " + (dst_enabled ? "Yes" : "No") + "</p>";
  if (dst_enabled) {
    WEBHTML = WEBHTML + "<p><strong>DST Start:</strong> " + String(dst_start_month) + "/" + String(dst_start_day) + "</p>";
    WEBHTML = WEBHTML + "<p><strong>DST End:</strong> " + String(dst_end_month) + "/" + String(dst_end_day) + "</p>";
  }
  
  WEBHTML = WEBHTML + "<h3>Confirm Timezone Settings</h3>";
  WEBHTML = WEBHTML + "<p>Please review and edit the timezone settings below if needed:</p>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/TimezoneSetup_POST\">";
  WEBHTML = WEBHTML + "<p><label for=\"utc_offset_seconds\">UTC Offset (seconds):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"utc_offset_seconds\" name=\"utc_offset_seconds\" value=\"" + String(offset_seconds) + "\" min=\"-86400\" max=\"86400\" step=\"15\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_enabled\">DST Enabled:</label><br>";
  WEBHTML = WEBHTML + "<select id=\"dst_enabled\" name=\"dst_enabled\" style=\"width: 150px; padding: 8px; margin: 5px 0;\">";
  WEBHTML = WEBHTML + "<option value=\"1\"" + (dst_enabled ? " selected" : "") + ">Yes</option>";
  WEBHTML = WEBHTML + "<option value=\"0\"" + (!dst_enabled ? " selected" : "") + ">No</option>";
  WEBHTML = WEBHTML + "</select></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_start_month\">DST Start Month:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_start_month\" name=\"dst_start_month\" value=\"" + String(dst_start_month) + "\" min=\"1\" max=\"12\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_start_day\">DST Start Day:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_start_day\" name=\"dst_start_day\" value=\"" + String(dst_start_day) + "\" min=\"1\" max=\"31\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_end_month\">DST End Month:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_end_month\" name=\"dst_end_month\" value=\"" + String(dst_end_month) + "\" min=\"1\" max=\"12\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_end_day\">DST End Day:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_end_day\" name=\"dst_end_day\" value=\"" + String(dst_end_day) + "\" min=\"1\" max=\"31\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><input type=\"submit\" value=\"Save Timezone Settings and Reboot\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\"></p>";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<p><a href=\"/InitialSetup\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">Back to WiFi Config</a></p>";
  
  WEBHTML = WEBHTML + "</body></html>";
  serverTextClose(200, true);
}

void handleTimezoneSetup_POST() {
  registerHTTPMessage("TZSetupP");
  #ifdef _USETFT
  tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
  tft.setCursor(0, tft.height()-200);
  tft.setTextColor(TFT_WHITE);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.printf("Saving timezone settings...\n");
  #endif

  if (server.hasArg("utc_offset_seconds") && 
      server.hasArg("dst_enabled") && server.hasArg("dst_start_month") && 
      server.hasArg("dst_start_day") && server.hasArg("dst_end_month") && 
      server.hasArg("dst_end_day")) {
    
    // Parse timezone settings
    int32_t offset_seconds = server.arg("utc_offset_seconds").toInt();
    bool dst_enabled = server.arg("dst_enabled").toInt() == 1;
    uint8_t dst_start_month = server.arg("dst_start_month").toInt();
    uint8_t dst_start_day = server.arg("dst_start_day").toInt();
    uint8_t dst_end_month = server.arg("dst_end_month").toInt();
    uint8_t dst_end_day = server.arg("dst_end_day").toInt();
    
    // Calculate total UTC offset in seconds
    int32_t total_offset = offset_seconds;
    
    // Use helper function to save timezone settings
    saveTimezoneSettings(total_offset, dst_enabled, dst_start_month, dst_start_day, dst_end_month, dst_end_day);
    
    // Set DST offset
    updateTime();
    
    // Save to NVS
    BootSecure bootSecure;
    int8_t ret = bootSecure.setPrefs();
    SerialPrint("setPrefs returned: " + String(ret), true);
    if (ret==-1) {
      SerialPrint("Failed to encode Prefs", true);
      #ifdef _USETFT
      tft.setTextColor(TFT_RED);
      tft.printf("Failed to encode settings!\n");
      tft.setTextColor(FG_COLOR);
      delay(3000);
      #endif
    } 
    else if (ret==0) {
      SerialPrint("Could not open Prefs", true);
      #ifdef _USETFT
      tft.printf("Could not open Prefs!\n");
      delay(3000);
      #endif
    } else {
      #ifdef _USETFT
      tft.printf("Timezone settings saved!\n");
      tft.printf("Rebooting...\n");
      #endif
      controlledReboot("Timezone settings saved successfully", RESET_TIME,true);
    }
  
    
    // Redirect to main page
    server.sendHeader("Location", "/TimezoneSetup");
    server.send(302, "text/plain", "Failed to save timezone data. Please try again.");
  } else {
    #ifdef _USETFT
    tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
    tft.setCursor(0, tft.height()-200);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    if (!server.hasArg("UTC_OFFSET_SECONDS")) tft.println("No UTC offset seconds");
    if (!server.hasArg("DST_ENABLED")) tft.println("No DST enabled");
    if (!server.hasArg("DST_START_MONTH")) tft.println("No DST start month");
    if (!server.hasArg("DST_START_DAY")) tft.println("No DST start day");
    if (!server.hasArg("DST_END_MONTH")) tft.println("No DST end month");
    if (!server.hasArg("DST_END_DAY")) tft.println("No DST end day");
    tft.setTextColor(FG_COLOR);
    #endif
    server.sendHeader("Location", "/TimezoneSetup");
    server.send(302, "text/plain", "Invalid timezone data. Please try again.");
  }
}


void handleWeather() {
  registerHTTPMessage("Weather");
  WEBHTML = "";
  serverTextHeader();
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Weather Data</h2>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

  // Navigation buttons
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px;\">Devices</a> ";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a>";
  WEBHTML = WEBHTML + "</div>";

  #ifdef _USEWEATHER
  
  // Display current weather data
  if (Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) {
    WEBHTML = WEBHTML + "<h3>Current Weather Information</h3>";
  } else {
    WEBHTML = WEBHTML + "<p>Location not set, Weather data is not available.</p>";
    WEBHTML = WEBHTML + "<p>Please set your location below (enter your address or lat/lon).</p>";
  }
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Field</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Value</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  //last update time
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Last Update Time</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateT) ? dateify(WeatherData.lastUpdateT) : "???") + "</td></tr>";

  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Last Update Attempt Time</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateAttempt) ? dateify(WeatherData.lastUpdateAttempt) : "???") + "</td></tr>";


  // Basic weather data
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Latitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Prefs.LATITUDE, 6) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Longitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Prefs.LONGITUDE, 6) + "</td></tr>";
    
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Sunrise</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.sunrise) ? dateify(WeatherData.sunrise) : "???") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Sunset</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.sunset) ? dateify(WeatherData.sunset) : "???") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Rain Flag</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + (WeatherData.flag_rain ? "Yes" : "No") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Snow Flag</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + (WeatherData.flag_snow ? "Yes" : "No") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Ice Flag</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + (WeatherData.flag_ice ? "Yes" : "No") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Grid X</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.getGridX()) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Grid Y</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.getGridY()) + "</td></tr>";
  
  // Current weather icon
  int16_t currentWeatherID = WeatherData.getWeatherID(0);
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Current Weather</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + WeatherData.nameWeatherIcon(currentWeatherID) + "</td></tr>";
  
  WEBHTML = WEBHTML + "</table>";
  
  // Daily forecast for next 3 days
  WEBHTML = WEBHTML + "<h3>3-Day Forecast</h3>";
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Day</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Max Temp</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Min Temp</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Weather</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  for (int day = 0; day < 3; day++) {
    int8_t dailyT[2];
    WeatherData.getDailyTemp(day, dailyT);
    int16_t weatherID = WeatherData.getDailyWeatherID(day);
    
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Day " + String(day + 1) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[0]) + "F</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[1]) + "F</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + WeatherData.nameWeatherIcon(weatherID) + "</td></tr>";
  }
  
  WEBHTML = WEBHTML + "</table>";
  
  // Configuration form for lat/lon
  WEBHTML = WEBHTML + "<h3>Update Location</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WEATHER\">";
  WEBHTML = WEBHTML + "<p><label for=\"lat\">Latitude:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"lat\" name=\"lat\" step=\"0.000001\" min=\"-90\" max=\"90\" value=\"" + String(Prefs.LATITUDE, 14) + "\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"lon\">Longitude:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"lon\" name=\"lon\" step=\"0.000001\" min=\"-180\" max=\"180\" value=\"" + String(Prefs.LONGITUDE, 14) + "\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Update Location\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Address lookup form (submits directly to server)
  WEBHTML = WEBHTML + "<h3>Lookup Coordinates from Address</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WeatherAddress\">";
  WEBHTML = WEBHTML + "<p><label for=\"street\">Street Address:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"street\" name=\"street\" placeholder=\"123 Main St\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"city\">City:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"city\" name=\"city\" placeholder=\"Boston\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"state\">State (2-letter code):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"state\" name=\"state\" pattern=\"[A-Za-z]{2}\" maxlength=\"2\" placeholder=\"MA\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"zipcode\">ZIP Code (5 digits):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"zipcode\" name=\"zipcode\" pattern=\"[0-9]{5}\" maxlength=\"5\" placeholder=\"12345\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Lookup Coordinates\" style=\"padding: 10px 20px; background-color: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Refresh weather button
  WEBHTML = WEBHTML + "<h3>Weather Actions</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WeatherRefresh\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Refresh Weather Now\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
#else
  WEBHTML = WEBHTML + "Weather not enabled on this device.";
  #endif
  
  WEBHTML = WEBHTML + "</body>";
  WEBHTML = WEBHTML + "</html>";

    
  serverTextClose(200, true);
}

void handleWeather_POST() {
  registerHTTPMessage("WthrLoc");
  #ifdef _USEWEATHER
  // Handle location update
  if (server.hasArg("lat") && server.hasArg("lon")) {
    double newLat = server.arg("lat").toDouble();
    double newLon = server.arg("lon").toDouble();
    
    // Validate coordinates
    if (newLat >= -90.0 && newLat <= 90.0 && newLon >= -180.0 && newLon <= 180.0) {
      // Update Prefs only (WeatherData now uses Prefs.LATITUDE/LONGITUDE)
      Prefs.LATITUDE = newLat;
      Prefs.LONGITUDE = newLon;
      Prefs.isUpToDate = false;
      
      // Save to NVS
      BootSecure bootSecure;
      int8_t ret = bootSecure.setPrefs();
      if (ret < 0) {
        SerialPrint("handleWeather_POST: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
      } else {
        SerialPrint("Coordinates updated and saved: " + String(newLat, 6) + ", " + String(newLon, 6), true);
      }
      
      // Force weather update with new coordinates
      WeatherData.updateWeather(0);
      
      server.sendHeader("Location", "/WEATHER");
      server.send(302, "text/plain", "Location updated successfully. Weather data refreshed.");
    } else {
      server.sendHeader("Location", "/WEATHER");
      server.send(302, "text/plain", "Invalid coordinates. Please check your input.");
    }
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Missing coordinates. Please try again.");
  }
  #else
  WEBHTML = "Weather not enabled on this device.";
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Weather not enabled on this device.");
  #endif
}

void handleWeatherRefresh() {
  registerHTTPMessage("WthrRef");
  #ifdef _USEWEATHER
  
  // Force immediate weather update
  bool updateResult = WeatherData.updateWeather(0);
  
  if (updateResult) {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather data refreshed successfully.");
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather update failed. Please try again.");
  }

  #else
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Weather not enabled on this device.");
  #endif
}

void handleWeatherZip() {
  registerHTTPMessage("WthrZip");
  
  // Handle ZIP code lookup (legacy function for backward compatibility)
  if (server.hasArg("zipcode")) {
    String zipCode = server.arg("zipcode");
    
    // Validate ZIP code format
    if (zipCode.length() != 5) {
      server.sendHeader("Content-Type", "application/json");
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid ZIP code format. Must be 5 digits.\"}");
      return;
    }
    
    for (int i = 0; i < 5; i++) {
      if (!isdigit(zipCode.charAt(i))) {
        server.sendHeader("Content-Type", "application/json");
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid ZIP code format. Must contain only digits.\"}");
        return;
      }
    }
    
    // Lookup coordinates from ZIP code
    bool success = getCoordinatesFromZipCode(zipCode);
    if (success) {
      // Return JSON response with coordinates (don't update WeatherData yet)
      String jsonResponse = "{\"success\":true,\"latitude\":" + String(Prefs.LATITUDE, 14) + ",\"longitude\":" + String(Prefs.LONGITUDE, 14) + "}";
      server.sendHeader("Content-Type", "application/json");
      server.send(200, "application/json", jsonResponse);
    } else {
      server.sendHeader("Content-Type", "application/json");
      server.send(500, "application/json", "{\"success\":false,\"message\":\"ZIP code lookup failed. Please check the ZIP code or try again later.\"}");
    }
  } else {
    server.sendHeader("Content-Type", "application/json");
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No ZIP code provided. Please try again.\"}");
  }
  
}


void handleWeatherAddress() {
  registerHTTPMessage("WthrAddr");

  if (server.hasArg("street") && server.hasArg("city") && server.hasArg("state") && server.hasArg("zipcode")) {
    String street = server.arg("street");
    String city = server.arg("city");
    String state = server.arg("state");
    String zipCode = server.arg("zipcode");

    // Use helper function for validation and lookup
    double lat = 0, lon = 0;
    bool success = lookupLocationFromAddress(street, city, state, zipCode, &lat, &lon);

    if (success) {
      // Redirect back to weather page with success message
      String redirectUrl = "/WEATHER?success=address_lookup&lat=" + String(lat, 6) + "&lon=" + String(lon, 6);
      server.sendHeader("Location", redirectUrl);
      server.send(302, "text/plain", "Address lookup successful. Coordinates updated and weather refreshed.");
      // Prefs already saved by lookupLocationFromAddress
    } else {
      server.sendHeader("Location", "/WEATHER?error=lookup_failed");
      server.send(302, "text/plain", "Address lookup failed. Please check the address or try again later.");
    } 
  } else {  
    server.sendHeader("Location", "/WEATHER?error=missing_fields");
    server.send(302, "text/plain", "Missing required address fields. Please provide street, city, state, and ZIP code.");
  }
  
}

bool handlerForWeatherAddress(String street, String city, String state, String zipCode) {
  
  //assume data is valid   
  if (WifiStatus()) {
    // Lookup coordinates from complete address
    #ifdef _USETFT
    tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
    tft.setCursor(0, tft.height()-200);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN);
    tft.println("Getting coordinates from address");
    tft.setTextColor(FG_COLOR);
    #endif
  
    if (getCoordinatesFromAddress(street, city, state, zipCode)) {
      
      if (Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) {
        #ifdef _USETFT
        tft.setTextColor(TFT_GREEN);
        tft.println("Geolocation set");
        tft.setTextColor(FG_COLOR);
        #endif
    
        return true;
      } else {
        #ifdef _USETFT
        tft.setTextColor(TFT_RED);
        tft.println("Geolocation not set");
        tft.setTextColor(FG_COLOR);
        #endif    
        return false;
      }
    } else {
      #ifdef _USETFT
      tft.setTextColor(TFT_RED);
      tft.println("Geolocation lookup failed");
      tft.setTextColor(FG_COLOR);
      #endif
      return false;
    }
  } else {
    #ifdef _USETFT
    tft.setTextColor(TFT_RED);
    tft.println("WiFi is down, cannot lookup geolocation");
    tft.setTextColor(FG_COLOR);
    #endif
    return false;
  }
  
  return false;
  
}

void handleSDCARD() {
  
  
  registerHTTPMessage("SDCard");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " SD Card Configuration</h2>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

  // Navigation buttons
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px;\">Devices</a> ";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a>";
  WEBHTML = WEBHTML + "</div>";
  
  // SD Card Information
  WEBHTML = WEBHTML + "<h3>SD Card Information</h3>";
  
  #ifndef _USESDCARD
    WEBHTML = WEBHTML + "There is no SD card installed or it is not enabled.<br>"; 
      
  #else
  
  // Error log button
    WEBHTML = WEBHTML + "<a href=\"/ERROR_LOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View Error Log</a><br><br>";

    WEBHTML = WEBHTML + "</font>---------------------<br>";      
  
  // Get SD card size information
  uint64_t cardSize = SD.cardSize();
  uint64_t totalBytes = cardSize;  // Use cardSize instead of SD.totalBytes()
  uint64_t usedBytes = SD.usedBytes();  // Direct usage, not subtraction
  
  // Sanity check: if usedBytes is larger than cardSize, something is wrong
  if (usedBytes > cardSize) {
    SerialPrint("WARNING: usedBytes > cardSize! This indicates a library issue.", true);
    SerialPrint("usedBytes: " + String(usedBytes) + ", cardSize: " + String(cardSize), true);
    
    // Try alternative approach - estimate used space based on actual files
    uint64_t estimatedUsedBytes = 0;
    File root = SD.open("/Data");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        estimatedUsedBytes += file.size();
        file = root.openNextFile();
      }
      root.close();
      
      if (estimatedUsedBytes < cardSize) {
        usedBytes = estimatedUsedBytes;
        SerialPrint("Using estimated used space from file sizes: " + String(usedBytes) + " bytes", true);
      }
    }
  }
  

  
  // Additional debugging for potential issues
  if (cardSize < (1024ULL * 1024ULL * 1024ULL)) {
    SerialPrint("WARNING: Card size is less than 1GB - this might indicate a detection issue", true);
  }
  if (usedBytes > (1024ULL * 1024ULL * 1024ULL * 100ULL)) {
    SerialPrint("WARNING: Used bytes is extremely large (>100GB) - this might indicate a calculation bug", true);
  }
  
  // Check if this looks like a 16GB card (common size)
  if (cardSize >= (15ULL * 1024ULL * 1024ULL * 1024ULL) && cardSize <= (16ULL * 1024ULL * 1024ULL * 1024ULL)) {
    SerialPrint("Detected ~16GB card - this should show ~14.8GB free space", true);
    
      // If this is a 16GB card but usedBytes is suspiciously large, try to estimate
      if (usedBytes > (2ULL * 1024ULL * 1024ULL * 1024ULL)) {
        SerialPrint("WARNING: 16GB card showing >2GB used - likely a library bug", true);
        SerialPrint("Attempting to estimate actual used space from files...", true);
        
        // Force recalculation of used space from actual files
        uint64_t actualUsedBytes = 0;
        File root = SD.open("/Data");
        if (root && root.isDirectory()) {
          File file = root.openNextFile();
          while (file) {
            actualUsedBytes += file.size();
            file = root.openNextFile();
          }
          root.close();
          
          if (actualUsedBytes < usedBytes && actualUsedBytes < cardSize) {
            usedBytes = actualUsedBytes;
            SerialPrint("Corrected used space from " + String(usedBytes) + " to " + String(actualUsedBytes) + " bytes", true);
          }
        }
      }
  }
  
  SerialPrint("==========================", true);
  
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Property</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Value</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Card Size</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(cardSize) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Total Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(totalBytes) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Used Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(usedBytes) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Free Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(totalBytes - usedBytes) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Number of Devices</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Sensors.getNumDevices()) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Number of Sensors</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Sensors.getNumSensors()) + "</td></tr>";
  
  WEBHTML = WEBHTML + "</table>";
  
  // Debug information (raw values)
  WEBHTML = WEBHTML + "<details style=\"margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<summary style=\"cursor: pointer; color: #666;\"><strong>Debug Information (Raw Values)</strong></summary>";
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0; font-family: monospace; font-size: 12px;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f8f8f8;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">Property</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">Raw Bytes</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">MB</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">GB</th>";
  WEBHTML = WEBHTML + "</tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\"><strong>SD.cardSize()</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(cardSize) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(cardSize / (1024.0 * 1024.0), 2) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(cardSize / (1024.0 * 1024.0 * 1024.0), 2) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\"><strong>SD.usedBytes()</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(usedBytes) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(usedBytes / (1024.0 * 1024.0), 2) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(usedBytes / (1024.0 * 1024.0 * 1024.0), 2) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\"><strong>Free Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(totalBytes - usedBytes) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String((totalBytes - usedBytes) / (1024.0 * 1024.0), 2) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String((totalBytes - usedBytes) / (1024.0 * 1024.0 * 1024.0), 2) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\">";
  // Additional debugging notes
  WEBHTML = WEBHTML + "<div style=\"margin: 10px 0; padding: 10px; background-color: #f0f8ff; border-left: 4px solid #0066cc;\">";
  WEBHTML = WEBHTML + "The debug information shows raw values returned by the file table. ";
  WEBHTML = WEBHTML + "<strong>Note:</strong> Free space may be incorrect in the following cases:";
  WEBHTML = WEBHTML + "<ul style=\"margin: 5px 0; padding-left: 20px;\">";
  WEBHTML = WEBHTML + "<li>Issues with large cards (>16GB)</li>";
  WEBHTML = WEBHTML + "<li>SD card filesystem limitations (FAT16/FAT32)</li>";
  WEBHTML = WEBHTML + "<li>Card formatting or partition issues</li>";
  WEBHTML = WEBHTML + "<li>Files not being properly closed or deleted</li>";
  WEBHTML = WEBHTML + "<li>Fragmentation or corruption of the filesystem</li>";
  WEBHTML = WEBHTML + "</ul>";
  WEBHTML = WEBHTML + "A warning will follow this message if an error is likely.";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "</td></tr>";
  // Status indicator
  if (usedBytes > (2ULL * 1024ULL * 1024ULL * 1024ULL) && cardSize >= (15ULL * 1024ULL * 1024ULL * 1024ULL)) {
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\">";
    WEBHTML = WEBHTML + "<div style=\"margin: 10px 0; padding: 10px; background-color: #fff3cd; border-left: 4px solid #ffc107;\">";
    WEBHTML = WEBHTML + "<strong>WARNING:</strong> Large card detected with suspicious used space values. ";
    WEBHTML = WEBHTML + "The system has attempted to correct this by scanning actual files. ";
    WEBHTML = WEBHTML + "You should manually check SD card usage if concerned about the values.";
    WEBHTML = WEBHTML + "</div>";
    WEBHTML = WEBHTML + "</td></tr>";
  }

  WEBHTML = WEBHTML + "</table>";
  WEBHTML = WEBHTML + "</details>";
  
  
  
  // Combined Files Table
  WEBHTML = WEBHTML + "<h3>All Files on SD Card</h3>";
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Filename</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Size (KB)</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Last Updated</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Type</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  // List all files in a single pass
  File root = SD.open("/Data");
  int fileCount = 0;  // Moved declaration to correct scope
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    
    while (file && fileCount < 100) { // Increased limit to show more files
      String filename = file.name();
      
      if (filename.endsWith(".dat") || filename.endsWith(".txt")) {
        // Determine file type and category
        String fileType = "Unknown";
        String fileCategory = "Unknown";
        
        if (filename == "ScreenFlags.dat") {
          fileType = "Screen Flags";
          fileCategory = "Core Data";
        } else if (filename == "SensorBackupv2.dat") {
          fileType = "Sensor Backup";
          fileCategory = "Core Data";
        } else if (filename == "GsheetInfo.dat") {
          fileType = "Google Sheets";
          fileCategory = "Core Data";
        } else if (filename == "WeatherData.dat") {
          fileType = "Weather Data";
          fileCategory = "Weather Data";
        } else if (filename == "DevicesSensors.dat") {
          fileType = "Devices & Sensors";
          fileCategory = "Devices";
        } else if (filename == "FileTimestamps.dat") {
          fileType = "File Timestamps";
          fileCategory = "Core Data";
        } else if (filename == "ErrorLog.txt") {
          fileType = "Error Log";
          fileCategory = "Core Data";
        } else {
          // Individual sensor file (format: sns_MAC_TYPE_ID.dat)
          fileCategory = "Individual Sensors";          
          fileType = "Individual Sensor";
        }
        
        // Calculate size in KB
        uint32_t sizeBytes = file.size();
        float sizeKB = sizeBytes / 1024.0;
        
        // Get file modification time (if available)
        String lastUpdated = "Unknown";
        time_t fileTime = file.getLastWrite();
        
        // For sensor data files, try to read the actual data timestamp
        if (filename.endsWith(".dat") && filename.startsWith("sns_")) {
            // This is a sensor data file, try to read the timestamp from content
            if (sizeBytes >= sizeof(SensorDataPoint)) {
                // Read the last data point to get the timestamp
                file.seek(sizeBytes - sizeof(SensorDataPoint));
                SensorDataPoint dataPoint;
                if (file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint)) == sizeof(SensorDataPoint)) {
                    // Debug: log what we read
                    //SerialPrint("Read SensorDataPoint from " + filename + ": timestamp=" + String(dataPoint.fileWriteTimestamp) + ", size=" + String(sizeof(SensorDataPoint)), true);
                    
                    if (dataPoint.fileWriteTimestamp > 0) {
                        // Use the timestamp from the file content
                        struct tm *timeinfo = localtime((time_t*)&dataPoint.fileWriteTimestamp);
                        if (timeinfo) {
                            char timeStr[25];
                            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
                            lastUpdated = String(timeStr) + " (Content)";
                        } else {
                            // Fallback to raw timestamp if localtime fails
                            lastUpdated = String(dataPoint.fileWriteTimestamp) + " (Content Raw)";
                        }
                    } else {
                        // Debug: log when timestamp is 0
                        //SerialPrint("Sensor file " + filename + " has fileWriteTimestamp = 0", true);
                    }
                } else {
                    // Debug: log when reading fails
                    SerialPrint("Failed to read SensorDataPoint from " + filename + ", bytes read: " + String(file.read((uint8_t*)&dataPoint, sizeof(SensorDataPoint))), true);
                }
                // Reset file position for next operations
                file.seek(0);
            } else {
                // Debug: log when file is too small
                SerialPrint("Sensor file " + filename + " is too small: " + String(sizeBytes) + " bytes, need " + String(sizeof(SensorDataPoint)), true);
            }
        }
        
        // If we still don't have a timestamp, try file system timestamp
        if (lastUpdated == "Unknown" && fileTime > 0) {
            struct tm *timeinfo = localtime(&fileTime);
            if (timeinfo) {
                char timeStr[25];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
                lastUpdated = String(timeStr) + " (File)";
            }
        }
        
        // Add row to table
        WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">" + filename + "</td>";
        WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(sizeKB, 1) + " KB</td>";
        WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + lastUpdated + "</td>";
        WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + fileCategory + "</td></tr>";
        
        fileCount++;
      }
      file = root.openNextFile();
    }
    root.close();
    
    if (fileCount == 0) {
      WEBHTML = WEBHTML + "<tr><td colspan=\"4\" style=\"border: 1px solid #ddd; padding: 8px; text-align: center; color: #666;\">No .dat files found</td></tr>";
    } else if (fileCount >= 100) {
      WEBHTML = WEBHTML + "<tr><td colspan=\"4\" style=\"border: 1px solid #ddd; padding: 8px; text-align: center; color: #666;\">Showing first 100 files. More files may exist.</td></tr>";
    }
  }
  
  WEBHTML = WEBHTML + "</table>";
  
  // File Summary
  WEBHTML = WEBHTML + "<h3>File Summary</h3>";
  WEBHTML = WEBHTML + "<p><strong>Total Files Listed:</strong> " + String(fileCount) + " .dat files</p>";
  
  // Individual Sensor Files Status
  WEBHTML = WEBHTML + "<p><strong>Note:</strong> Individual sensor files are automatically created when new sensor data is received.</p>";
  

  WEBHTML = WEBHTML + "</font>---------------------<br>";      
  // Error log button
    WEBHTML = WEBHTML + "<a href=\"/ERROR_LOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View Error Log</a><br><br>";
  
  // Action Buttons
  WEBHTML = WEBHTML + "<h3>SD Card Actions</h3>";
  
  // Delete buttons
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_SCREENFLAGS\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete ScreenFlags.dat\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_WEATHERDATA\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete WeatherData.dat\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_DEVICES\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete DevicesSensors Data\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_SENSORS\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete All Sensor History Files\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_ERRORLOG\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete Error Log\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_TIMESTAMPS\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete Timestamps Log\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_GSHEET\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete GSheet Data\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Save buttons
  WEBHTML = WEBHTML + "<br><br>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_SAVE_SCREENFLAGS\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Save ScreenFlags.dat Now\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_SAVE_WEATHERDATA\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Save WeatherData.dat Now\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_STORE_DEVICES\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Store DevicesSensors Data Now\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // View timestamps button
  WEBHTML = WEBHTML + "<br><br>";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD_TIMESTAMPS\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">List File Timestamps</a>";
  #endif
  
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200, true);

  return;

}

void handleSDCARD_DELETE_DEVICES() {
  #ifdef _USESDCARD

  
  registerHTTPMessage("SDDelDev");
  
  // Delete the sensors data file
  uint32_t nDeleted = deleteDataFiles(false, false, false, true);
  if (nDeleted > 0) {
    SerialPrint("Devices data deleted", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Devices data deleted successfully.");
  } else {
    SerialPrint("Failed to delete Devices data", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to delete Devices data.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_DELETE_SENSORS() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDDelSns");
  
  uint16_t deletedCount = deleteSensorDataSD();
  if (deletedCount > 0) {
    SerialPrint("Deleted " + String(deletedCount) + " sensor history files", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Deleted " + String(deletedCount) + " sensor history files.");
  } else {
    SerialPrint("No files found.", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "No files found.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_STORE_DEVICES() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDStoreDev");
  
  // Store the devices and sensors data to SD card
  if (storeDevicesSensorsSD()) {
    SerialPrint("DevicesSensors data stored to SD card successfully", true);
    server.send(302, "text/plain", "DevicesSensors data stored to SD card successfully.");
  } else {
    SerialPrint("Failed to store DevicesSensors data to SD card", true);
    server.send(302, "text/plain", "Failed to store DevicesSensors data to SD card.");
  }
  #endif
}

void handleSDCARD_DELETE_SCREENFLAGS() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDDelScr");

  
  // Delete the ScreenFlags.dat file
  uint32_t nDeleted = deleteCoreStruct();
  if (nDeleted == UINT32_MAX) {
    SerialPrint("No SD card found", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Core data structure reset.");
    return;
  }

  if (nDeleted > 0) {
    SerialPrint("Core data deleted", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Core data struct reset and files deleted successfully.");
  } else {
    SerialPrint("Failed to delete ScreenFlags.dat", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Core data reset, but files were not deleted..");
  } 
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_DELETE_WEATHERDATA() {
  registerHTTPMessage("SDDelWthr");
  #ifdef _USESDCARD
  // Delete the WeatherData.dat file
  uint32_t nDeleted = deleteDataFiles(false, true, false, false);
  if (nDeleted > 0) {
    SerialPrint("Weather data deleted", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Weather data deleted successfully.");
  } else {
    SerialPrint("Failed to delete WeatherData.dat", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to delete Weather data.");
  }
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_SAVE_SCREENFLAGS() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDSaveScr");
  
  // Save ScreenFlags.dat immediately
  if (storeScreenInfoSD()) {
    SerialPrint("ScreenFlags.dat saved to SD card successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "ScreenFlags.dat saved to SD card successfully.");
  } else {
    SerialPrint("Failed to save ScreenFlags.dat to SD card", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to save ScreenFlags.dat to SD card.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_SAVE_WEATHERDATA() {
  #ifdef _USESDCARD
  #ifdef _USEWEATHER
  registerHTTPMessage("SDSaveWthr");
  
  // Save WeatherData.dat immediately
  if (storeWeatherDataSD()) {
    SerialPrint("WeatherData.dat saved to SD card successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "WeatherData.dat saved to SD card successfully.");
  } else {
    SerialPrint("Failed to save WeatherData.dat to SD card", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to save WeatherData.dat to SD card.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
  #endif
}

void handleSDCARD_TIMESTAMPS() {
  registerHTTPMessage("SDTmStmp");

  #ifdef _USESDCARD
  // Open and read the FileTimestamps.dat file
  String filename = "/Data/FileTimestamps.dat";
  File file = SD.open(filename, FILE_READ);
  
  if (!file) {
    // File doesn't exist or can't be opened
    server.send(200, "text/html", 
      "<html><head><title>File Timestamps</title></head><body>"
      "<h1>File Timestamps</h1>"
      "<p>The FileTimestamps.dat file does not exist or cannot be opened.</p>"
      "<p>This file is created automatically when files are written to the SD card.</p>"
      "<br><a href=\"/SDCARD\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to SD Card</a>"
      "</body></html>");
    return;
  }
  
  // Read the file content
  String content = "";
  while (file.available()) {
    content += file.readString();
  }
  file.close();
  
  if (content.length() == 0) {
    // File is empty
    server.send(200, "text/html", 
      "<html><head><title>File Timestamps</title></head><body>"
      "<h1>File Timestamps</h1>"
      "<p>The FileTimestamps.dat file is empty.</p>"
      "<p>This file will be populated as files are written to the SD card.</p>"
      "<br><a href=\"/SDCARD\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to SD Card</a>"
      "</body></html>");
    return;
  }
  
  // Format the content for display
  String htmlContent = "<html><head><title>File Timestamps</title>";
  htmlContent += "<style>";
  htmlContent += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  htmlContent += "h1 { color: #333; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }";
  htmlContent += ".timestamp-entry { background-color: white; padding: 10px; margin: 5px 0; border-radius: 5px; border-left: 4px solid #4CAF50; }";
  htmlContent += ".filename { font-weight: bold; color: #2196F3; }";
  htmlContent += ".timestamp { color: #666; font-family: monospace; }";
  htmlContent += ".back-button { padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; display: inline-block; margin-top: 20px; }";
  htmlContent += "</style></head><body>";
  htmlContent += "<h1>File Timestamps</h1>";
  htmlContent += "<p>This shows when files were last written to the SD card:</p>";
  
  // Parse and format each line
  int lineStart = 0;
  int lineEnd = content.indexOf('\n');
  int entryCount = 0;
  
  while (lineEnd >= 0 && entryCount < 1000) { // Limit to 1000 entries to prevent memory issues
    String line = content.substring(lineStart, lineEnd);
    line.trim();
    
    if (line.length() > 0) {
      int commaPos = line.indexOf(',');
      if (commaPos > 0) {
        String filename = line.substring(0, commaPos);
        String timestamp = line.substring(commaPos + 1);
        
        // Convert timestamp to readable format
        uint32_t timestampValue = timestamp.toInt();
        String readableTime = "Unknown";
        if (timestampValue > 0) {
          readableTime = dateify(timestampValue);
        }
        
        htmlContent += "<div class=\"timestamp-entry\">";
        htmlContent += "<span class=\"filename\">" + filename + "</span><br>";
        htmlContent += "<span class=\"timestamp\">Written: " + readableTime + " (" + timestamp + ")</span>";
        htmlContent += "</div>";
        
        entryCount++;
      }
    }
    
    lineStart = lineEnd + 1;
    lineEnd = content.indexOf('\n', lineStart);
  }
  
  if (entryCount == 0) {
    htmlContent += "<p>No timestamp entries found in the file.</p>";
  } else {
    htmlContent += "<p><strong>Total entries: " + String(entryCount) + "</strong></p>";
  }
  
  htmlContent += "<a href=\"/SDCARD\" class=\"back-button\">Back to SD Card</a>";
  htmlContent += "</body></html>";
  
  server.send(200, "text/html", htmlContent);
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleERROR_LOG() {
  registerHTTPMessage("ErrorLog");

  #ifdef _USESDCARD
  // Format the content for display
  String htmlContent = "<html><head><title>Error Log</title>";
  htmlContent += "<style>";
  htmlContent += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  htmlContent += "h1 { color: #333; border-bottom: 2px solid #f44336; padding-bottom: 10px; }";
  htmlContent += ".error-entry { background-color: white; padding: 10px; margin: 5px 0; border-radius: 5px; border-left: 4px solid #f44336; }";
  htmlContent += ".error-time { font-weight: bold; color: #f44336; }";
  htmlContent += ".error-code { color: #FF9800; font-family: monospace; }";
  htmlContent += ".error-message { color: #333; margin-top: 5px; }";
  htmlContent += ".back-button { padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; display: inline-block; margin-top: 20px; }";
  htmlContent += "pre { background-color: #f8f8f8; padding: 15px; border-radius: 5px; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }";
  htmlContent += "</style></head><body>";
  htmlContent += "<h1>Device Error Log</h1>";
  htmlContent += "<p>This shows the last 50 errors that have occurred on the device:</p>";
  

  // Read the file content
  String content = "";
  int8_t linesRead = readErrorFromSD(&content, 50);

  if (linesRead < 0) {
    htmlContent += "<p>Failed to read error log file</p>";
    htmlContent += "<a href=\"/STATUS\" class=\"back-button\">Back to Status</a>";
    htmlContent += "</body></html>";
    server.send(200, "text/html", htmlContent);
    return;
  }

  if (linesRead == 0) {
    htmlContent += "<p>No errors have been logged yet.</p>";
    htmlContent += "<a href=\"/STATUS\" class=\"back-button\">Back to Status</a>";
    htmlContent += "</body></html>";
    server.send(200, "text/html", htmlContent);
    return;
  }

  
  // Parse and format each line
  int lineStart = 0;
  int lineEnd = content.indexOf('\n');
  int entryCount = 0;
  
  while (lineEnd >= 0 && entryCount < 100) { // Limit to 1000 entries to prevent memory issues
    String line = content.substring(lineStart, lineEnd);
    line.trim();
    
    if (line.length() > 0) {
      // Parse the error log format: timestamp,errorcode,errormessage
      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);
      
      if (firstComma > 0 && secondComma > firstComma) {
        String timestamp = line.substring(0, firstComma);
        String errorCode = line.substring(firstComma + 1, secondComma);
        String errorMessage = line.substring(secondComma + 1);
        
        htmlContent += "<div class=\"error-entry\">";
        htmlContent += "<span class=\"error-time\">" + timestamp + "</span><br>";
        htmlContent += "<span class=\"error-code\">Error Code: " + errorCode + "</span><br>";
        htmlContent += "<div class=\"error-message\">" + errorMessage + "</div>";
        htmlContent += "</div>";
        
        entryCount++;
      } else {
        // If the format doesn't match, just display the raw line
        htmlContent += "<div class=\"error-entry\">";
        htmlContent += "<div class=\"error-message\">" + line + "</div>";
        htmlContent += "</div>";
        entryCount++;
      }
    }
    
    lineStart = lineEnd + 1;
    lineEnd = content.indexOf('\n', lineStart);
  }
  
  if (entryCount == 0) {
    htmlContent += "<p>No error entries found in the file.</p>";
  } else {
    htmlContent += "<p><strong>Total entries: " + String(entryCount) + "</strong></p>";
  }
  
  // Also show the raw file content for debugging
  htmlContent += "<h3>Raw File Content (for debugging)</h3>";
  htmlContent += "<pre>" + content + "</pre>";
  
  htmlContent += "<a href=\"/STATUS\" class=\"back-button\">Back to Status</a>";
  htmlContent += "</body></html>";
  
  server.send(200, "text/html", htmlContent);
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_DELETE_ERRORLOG() {
  registerHTTPMessage("DelErrLg");
  #ifdef _USESDCARD
  
  // Delete the DeviceErrors.txt file
  String filename = "/Data/DeviceErrors.txt";
  if (SD.exists(filename)) {
    if (SD.remove(filename)) {
      // Remove the error log file from the timestamp index
      removeFromTimestampIndex(filename.c_str());
      SerialPrint("Error log file deleted successfully", true);
      server.sendHeader("Location", "/SDCARD");
      server.send(302, "text/plain", "Error log file deleted successfully.");
    } else {
      SerialPrint("Failed to delete error log file", true);
      server.sendHeader("Location", "/SDCARD");
      server.send(302, "text/plain", "Failed to delete error log file.");
    }
  } else {
    SerialPrint("Error log file does not exist", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Error log file does not exist.");
  }
  
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_DELETE_TIMESTAMPS() {
  registerHTTPMessage("DelTmStmp");
  #ifdef _USESDCARD
  
  // Delete the FileTimestamps.dat file
  String filename = "/Data/FileTimestamps.dat";
  if (SD.exists(filename)) {
    if (SD.remove(filename)) {
      SerialPrint("File timestamps log deleted successfully", true);
      server.sendHeader("Location", "/SDCARD");
      server.send(302, "text/plain", "File timestamps log deleted successfully.");
    } else {
      SerialPrint("Failed to delete file timestamps log", true);
      server.sendHeader("Location", "/SDCARD");
      server.send(302, "text/plain", "Failed to delete file timestamps log.");
    }
  } else {
    SerialPrint("File timestamps log does not exist", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "File timestamps log does not exist.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_DELETE_GSHEET() {
  registerHTTPMessage("DelGsheet");
  #ifdef _USESDCARD
  
  // Delete the GsheetInfo.dat file
  String filename = "/Data/GsheetInfo.dat";
  if (SD.exists(filename)) {
    if (SD.remove(filename)) {
      // Remove the GSheet info file from the timestamp index
      removeFromTimestampIndex(filename.c_str());
      SerialPrint("GSheet info file deleted successfully", true);
      server.sendHeader("Location", "/SDCARD");
      server.send(302, "text/plain", "GSheet info file deleted successfully.");
    } else {
      SerialPrint("Failed to delete GSheet info file", true);
      server.sendHeader("Location", "/SDCARD");
      server.send(302, "text/plain", "Failed to delete GSheet info file.");
    }
  } else {
    SerialPrint("GSheet info file does not exist", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "GSheet info file does not exist.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}



void handleREBOOT_DEBUG() {
  registerHTTPMessage("RebootDebug");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Reboot Debug Information</h2>";
  
  // Display reboot debug information
  WEBHTML = WEBHTML + "<div style=\"margin: 20px 0; padding: 20px; background-color: #f5f5f5; border-radius: 8px;\">";
  WEBHTML = WEBHTML + "<pre style=\"font-family: monospace; white-space: pre-wrap;\">";
  WEBHTML = WEBHTML + getRebootDebugInfo();
  WEBHTML = WEBHTML + "</pre>";
  WEBHTML = WEBHTML + "</div>";
  
  // Navigation links
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Navigation</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200, true);
}

/**
 * @brief Setup all HTTP server routes.
 * This function registers all the server handlers and should be called from main.cpp
 * instead of having the routes defined there.
 */
void handleUPDATETIMEZONE() {
  registerHTTPMessage("TZUpdate");
  
  // Check if WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    server.sendHeader("Location", "/InitialSetup");
    server.send(302, "text/plain", "WiFi not connected. Please configure WiFi first.");
    return;
  }
  
  // Get current timezone information
  int32_t utc_offset = Prefs.TimeZoneOffset;
  bool dst_enabled = Prefs.DST;
  uint8_t dst_start_month = Prefs.DSTStartMonth;
  uint8_t dst_start_day = Prefs.DSTStartDay;
  uint8_t dst_end_month = Prefs.DSTEndMonth;
  uint8_t dst_end_day = Prefs.DSTEndDay;
  
  // Try to get timezone info from API
  if (Prefs.TimeZoneOffset == 0) {
    if (getTimezoneInfo(&utc_offset, &dst_enabled, &dst_start_month, &dst_start_day, &dst_end_month, &dst_end_day)) {
      // Update Prefs with detected values
      Prefs.TimeZoneOffset = utc_offset;
      Prefs.DST = dst_enabled ? 1 : 0;
      Prefs.DSTStartMonth = dst_start_month;
      Prefs.DSTStartDay = dst_start_day;
      Prefs.DSTEndMonth = dst_end_month;
      Prefs.DSTEndDay = dst_end_day;
      Prefs.isUpToDate = false;
    }
  }
  
  
  // Display timezone configuration page
  serverTextHeader();
  String WEBHTML = "";
  
  WEBHTML = WEBHTML + "<h2>Timezone Configuration</h2>";
  WEBHTML = WEBHTML + "<p>WiFi connection established! Please review and confirm the detected timezone settings:</p>";
  
  WEBHTML = WEBHTML + "<form id=\"timezoneForm\" method=\"POST\" action=\"/UPDATETIMEZONE\">";
  
  // Current time display
  WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; border: 1px solid #4CAF50; padding: 15px; margin: 10px 0; border-radius: 4px;\">";
  WEBHTML = WEBHTML + "<h3>Current Time</h3>";
  WEBHTML = WEBHTML + "<p><strong>Local Time:</strong> " + String(dateify(I.currentTime, "mm/dd/yyyy hh:mm:ss")) + "</p>";
  WEBHTML = WEBHTML + "<p><strong>UTC Time:</strong> " + String(dateify(I.currentTime - (Prefs.TimeZoneOffset + (Prefs.DST ? 3600 : 0)), "mm/dd/yyyy hh:mm:ss")) + "</p>";
  WEBHTML = WEBHTML + "</div>";
  
  // Timezone configuration
  WEBHTML = WEBHTML + "<h3>Timezone Settings</h3>";
  WEBHTML = WEBHTML + "<p><label for=\"utc_offset_seconds\">UTC Offset (seconds):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"utc_offset_seconds\" name=\"utc_offset_seconds\" value=\"" + String(utc_offset) + "\" min=\"-12\" max=\"14\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_enabled\">DST Enabled:</label><br>";
  WEBHTML = WEBHTML + "<select id=\"dst_enabled\" name=\"dst_enabled\" style=\"width: 150px; padding: 8px; margin: 5px 0;\">";
  WEBHTML = WEBHTML + "<option value=\"1\" " + (dst_enabled ? "selected" : "") + ">Yes</option>";
  WEBHTML = WEBHTML + "<option value=\"0\" " + (!dst_enabled ? "selected" : "") + ">No</option>";
  WEBHTML = WEBHTML + "</select></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_start_month\">DST Start Month:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_start_month\" name=\"dst_start_month\" value=\"" + String(dst_start_month) + "\" min=\"1\" max=\"12\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_start_day\">DST Start Day:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_start_day\" name=\"dst_start_day\" value=\"" + String(dst_start_day) + "\" min=\"1\" max=\"31\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_end_month\">DST End Month:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_end_month\" name=\"dst_end_month\" value=\"" + String(dst_end_month) + "\" min=\"1\" max=\"12\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<p><label for=\"dst_end_day\">DST End Day:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"dst_end_day\" name=\"dst_end_day\" value=\"" + String(dst_end_day) + "\" min=\"1\" max=\"31\" style=\"width: 100px; padding: 8px; margin: 5px 0;\"></p>";
  
  WEBHTML = WEBHTML + "<div style=\"margin: 20px 0;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Save Timezone Settings\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"padding: 10px 20px; background-color: #757575; color: white; text-decoration: none; border-radius: 4px;\">Cancel</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</form>";
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200);
}

void handleUPDATETIMEZONE_POST() {
  registerHTTPMessage("TZUpdateP");
  
  // Parse timezone settings
  if (server.hasArg("utc_offset_seconds")) {
    int32_t utc_offset = server.arg("utc_offset_seconds").toInt();
    Prefs.TimeZoneOffset = utc_offset;
  }
  
  if (server.hasArg("dst_enabled")) {
    Prefs.DST = server.arg("dst_enabled").toInt();
    Prefs.DSTOffset = Prefs.DST ? 3600 : 0;
  }
  
  if (server.hasArg("dst_start_month")) {
    Prefs.DSTStartMonth = server.arg("dst_start_month").toInt();
  }
  
  if (server.hasArg("dst_start_day")) {
    Prefs.DSTStartDay = server.arg("dst_start_day").toInt();
  }
  
  if (server.hasArg("dst_end_month")) {
    Prefs.DSTEndMonth = server.arg("dst_end_month").toInt();
  }
  
  if (server.hasArg("dst_end_day")) {
    Prefs.DSTEndDay = server.arg("dst_end_day").toInt();
  }
  
  // Mark prefs as needing to be saved
  Prefs.isUpToDate = false;
  
  // Save prefs to NVS
  #ifdef _USE32
  BootSecure bootSecure;
  if (bootSecure.setPrefs()<0) {
    SerialPrint("Failed to store core data",true);
  }
  #endif
  
  // Update time client with new settings
  checkDST();
  
  // Display confirmation and reboot
  serverTextHeader();
  String WEBHTML = "";
  WEBHTML = WEBHTML + "<h2>Timezone Settings Saved</h2>";
  WEBHTML = WEBHTML + "<p>Timezone settings have been saved successfully!</p>";
  WEBHTML = WEBHTML + "<p><strong>UTC Offset:</strong> " + String(Prefs.TimeZoneOffset/3600) + "h " + String((Prefs.TimeZoneOffset%3600)/60) + "m</p>";
  WEBHTML = WEBHTML + "<p><strong>DST Enabled:</strong> " + (Prefs.DST ? "Yes" : "No") + "</p>";
  if (Prefs.DST) {
    WEBHTML = WEBHTML + "<p><strong>DST Period:</strong> " + String(Prefs.DSTStartMonth) + "/" + String(Prefs.DSTStartDay) + " to " + String(Prefs.DSTEndMonth) + "/" + String(Prefs.DSTEndDay) + "</p>";
  }
  WEBHTML = WEBHTML + "<script>setTimeout(function(){ window.location.href = '/'; }, 3000);</script>";
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200);

}

// ==================== DEVICE VIEWER FUNCTIONS ====================

void handleDeviceViewer() {
    registerHTTPMessage("DeviceViewer");
    WEBHTML = "";
    serverTextHeader();
    
    WEBHTML = WEBHTML + "<body>";
    WEBHTML = WEBHTML + "<h2>Device Viewer</h2>";
    WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

    // Navigation buttons to config pages
    WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
    WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
    WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
    WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets</a> ";
    WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
    WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather</a> ";
    WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a>";
    WEBHTML = WEBHTML + "</div>";

    // Device-specific navigation buttons
    WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 10px; background-color: #e8e8e8; margin-bottom: 20px;\">";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PREV\" style=\"display: inline-block; margin: 5px; padding: 8px 16px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">Previous</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_NEXT\" style=\"display: inline-block; margin: 5px; padding: 8px 16px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Next</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING\" style=\"display: inline-block; margin: 5px; padding: 8px 16px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">Ping</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_DELETE\" style=\"display: inline-block; margin: 5px; padding: 8px 16px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px;\" onclick=\"return confirm('Are you sure you want to delete this device and all its sensors? This action cannot be undone.');\">Delete</a>";
    WEBHTML = WEBHTML + "</div>";
    
    
    // Check for status messages from ping operations
    if (server.hasArg("ping")) {
        String pingStatus = server.arg("ping");
        if (pingStatus == "success") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Success:</strong> Ping request sent successfully to the device.";
            WEBHTML = WEBHTML + "</div>";
        } else if (pingStatus == "failed") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Error:</strong> Failed to send ping request to the device.";
            WEBHTML = WEBHTML + "</div>";
        }
    }
    
    // Check for status messages from delete operations
    if (server.hasArg("delete")) {
        String deleteStatus = server.arg("delete");
        if (deleteStatus == "success") {
            String deviceName = server.hasArg("device") ? server.arg("device") : "Unknown";
            String sensorCount = server.hasArg("sensors") ? server.arg("sensors") : "0";
            WEBHTML = WEBHTML + "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Success:</strong> Device '" + deviceName + "' and " + sensorCount + " associated sensors have been deleted.";
            WEBHTML = WEBHTML + "</div>";
            CURRENT_DEVICEVIEWER_DEVINDEX = 0;
            CURRENT_DEVICEVIEWER_DEVNUMBER = 0;  //reset the device number
        }
    }
    
    if (server.hasArg("error")) {
        String errorType = server.arg("error");
        if (errorType == "no_devices") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; color: #856404; padding: 15px; margin: 10px 0; border: 1px solid #ffeaa7; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Warning:</strong> No devices available to ping.";
            WEBHTML = WEBHTML + "</div>";
        } 
        else if (errorType == "device_not_found") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Error:</strong> Device not found.";
            WEBHTML = WEBHTML + "</div>";
        }
        else if (errorType == "cannot_delete_myself") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Error:</strong> Cannot delete this device.";
            WEBHTML = WEBHTML + "</div>";
        }
    }
    
    // Reset to first device if no devices exist
    if (Sensors.getNumDevices() == 0) {
        WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; color: #856404; padding: 15px; margin: 10px 0; border: 1px solid #ffeaa7; border-radius: 4px;\">";
        WEBHTML = WEBHTML + "<strong>No devices found.</strong> No devices are currently registered in the system.";
        WEBHTML = WEBHTML + "</div>";
        WEBHTML = WEBHTML + "<br><a href=\"/\" style=\"display: inline-block; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
        WEBHTML = WEBHTML + "</body></html>";
        serverTextClose(200, true);
        return;
    }
    
    // Ensure there is at least 1 device in the queue, and I am in it!
    uint16_t myDeviceIndex = Sensors.findMyDeviceIndex();
    if (myDeviceIndex == -1) {
        WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
        WEBHTML = WEBHTML + "<strong>Error:</strong> Could not retrieve device information.";
        WEBHTML = WEBHTML + "</div>";
        WEBHTML = WEBHTML + "<br><a href=\"/\" style=\"display: inline-block; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
        WEBHTML = WEBHTML + "</body></html>";
        serverTextClose(200, true);
        return;
    }

    while (Sensors.isDeviceInit(CURRENT_DEVICEVIEWER_DEVINDEX)==false) {
        CURRENT_DEVICEVIEWER_DEVINDEX++;
        CURRENT_DEVICEVIEWER_DEVNUMBER++;
        if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES ) {
          CURRENT_DEVICEVIEWER_DEVINDEX = 0;
          CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
        }
    }
    
    // Get current device
    ArborysDevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICEVIEWER_DEVINDEX);
    if (!device) {
        WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
        WEBHTML = WEBHTML + "<strong>Error:</strong> Could not retrieve device information.";
        WEBHTML = WEBHTML + "</div>";
        WEBHTML = WEBHTML + "<br><a href=\"/\" style=\"display: inline-block; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
        WEBHTML = WEBHTML + "</body></html>";
        serverTextClose(200, true);
        return;
    }
    
    // Device information header
    WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; color: #2e7d32; padding: 15px; margin: 10px 0; border: 1px solid #4caf50; border-radius: 4px;\">";
    bool isThisDevice = (CURRENT_DEVICEVIEWER_DEVINDEX == myDeviceIndex);
    WEBHTML = WEBHTML + "<h3>Device " + String(CURRENT_DEVICEVIEWER_DEVNUMBER + 1) + " of " + String(Sensors.getNumDevices()) + ((isThisDevice) ? " (this device)" : "") +"</h3>";
    WEBHTML = WEBHTML + "Local Device Index: " + String(CURRENT_DEVICEVIEWER_DEVINDEX);
    WEBHTML = WEBHTML + "</div>";
    
    // Device details
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h4>Device Information</h4>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 30%;\">Device Name:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devName) +  "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">MAC Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(device->MAC)) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">IP Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + device->IP.toString() + "\" target=\"_blank\">" + device->IP.toString() + "</a></td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Device Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devType) + "</td></tr>";
    if (isThisDevice) {
      //show alive since
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Alive Since:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ALIVESINCE ? dateify(I.ALIVESINCE, "mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
    } else {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Time Data Sent:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->dataSent ? dateify(device->dataSent, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Time Data Received:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->dataReceived ? dateify(device->dataReceived, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Sending Interval:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->SendingInt) + " seconds</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Status:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->expired ? "Expired" : "Active") + "</td></tr>";
    }
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    
    // Count sensors for this device
    uint8_t sensorCount = 0;
    for (int16_t i = 0; i < NUMSENSORS; i++) {
        ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(i);
        if (sensor && sensor->deviceIndex == CURRENT_DEVICEVIEWER_DEVINDEX) {
            sensorCount++;
        }
    }
    
    WEBHTML = WEBHTML + "<div style=\"background-color: #e3f2fd; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #2196f3;\">";
    WEBHTML = WEBHTML + "<h4>Sensors (" + String(sensorCount) + " total)</h4>";
    
    if (sensorCount == 0) {
        WEBHTML = WEBHTML + "<p>No sensors found for this device.</p>";
    } else {
        // Sensor table
        WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse; margin-top: 10px;\">";
        WEBHTML = WEBHTML + "<tr style=\"background-color: #2196f3; color: white;\">";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Sensor Name</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type Name</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Sensor ID</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Last Value</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Last Logged</th>";
        WEBHTML = WEBHTML + "</tr>";
        
        // Add sensor rows
        for (int16_t i = 0; i < NUMSENSORS; i++) {
            ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(i);
            if (sensor && sensor->deviceIndex == CURRENT_DEVICEVIEWER_DEVINDEX) {                
                WEBHTML = WEBHTML + "<tr>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsName) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsType) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + Sensors.sensorIsOfType(sensor) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsID) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsValue, 2) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->timeLogged ? dateify(sensor->timeLogged, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td>";
                WEBHTML = WEBHTML + "</tr>";
            }
        }
        WEBHTML = WEBHTML + "</table>";
    }
    WEBHTML = WEBHTML + "</div>";
    
    
    WEBHTML = WEBHTML + "</body></html>";
    serverTextClose(200, true);
}

void handleDeviceViewerNext() {
    registerHTTPMessage("DVNext");
    
    if (Sensors.getNumDevices() > 0) {
        
        CURRENT_DEVICEVIEWER_DEVINDEX++;
        CURRENT_DEVICEVIEWER_DEVNUMBER++;
        if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
          CURRENT_DEVICEVIEWER_DEVINDEX = 0;
          CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
        }
        //move forward until we find a valid  device 
        while (Sensors.isDeviceInit(CURRENT_DEVICEVIEWER_DEVINDEX)==false) {
          CURRENT_DEVICEVIEWER_DEVINDEX++; 
          //note that we do not increment device number here, because we have not found another valid device yet!
          if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES-1) {
            CURRENT_DEVICEVIEWER_DEVINDEX = 0;
            CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
          }
        }
    }
    
    server.sendHeader("Location", "/DEVICEVIEWER");
    server.send(302, "text/plain", "Redirecting to next device...");
}

void handleDeviceViewerPrev() {
    registerHTTPMessage("DVPrev");
    
    if (Sensors.getNumDevices() == 0) {
      
      server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
      server.send(302, "text/plain", "No devices available to view.");
    }

    if (CURRENT_DEVICEVIEWER_DEVINDEX == 0) {
      CURRENT_DEVICEVIEWER_DEVINDEX = NUMDEVICES - 1;
      CURRENT_DEVICEVIEWER_DEVNUMBER = Sensors.getNumDevices()-1;
    } else {
      CURRENT_DEVICEVIEWER_DEVINDEX--;
      CURRENT_DEVICEVIEWER_DEVNUMBER--;
    }

    //now move back until we find a valid  device 
    while (Sensors.isDeviceInit(CURRENT_DEVICEVIEWER_DEVINDEX)==false) {
      if (CURRENT_DEVICEVIEWER_DEVINDEX <= 0) {
        CURRENT_DEVICEVIEWER_DEVINDEX = NUMDEVICES - 1;
        CURRENT_DEVICEVIEWER_DEVNUMBER = Sensors.getNumDevices()-1;
      } else {
        //note that we only decrement viewer, not number
        CURRENT_DEVICEVIEWER_DEVINDEX--;      
      }
    }

    
    server.sendHeader("Location", "/DEVICEVIEWER");
    server.send(302, "text/plain", "Redirecting to previous device...");
}

void handleDeviceViewerPing() {
    registerHTTPMessage("DVPing");
    
    // Check if we have any devices
    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
        server.send(302, "text/plain", "No devices available to ping.");
        return;
    }
    
    // Ensure current device index is valid
    if (CURRENT_DEVICEVIEWER_DEVINDEX >= Sensors.getNumDevices()) {
        CURRENT_DEVICEVIEWER_DEVINDEX = 0;
    }
    
    // Get current device
    ArborysDevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICEVIEWER_DEVINDEX);
    if (!device) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }
    
    // Send ping request
    bool success = sendESPNowPingRequest(device, 3, true);
    
    if (success) {
        server.sendHeader("Location", "/DEVICEVIEWER?ping=success");
        server.send(302, "text/plain", "Ping sent successfully.");
    } else {
        server.sendHeader("Location", "/DEVICEVIEWER?ping=failed");
        server.send(302, "text/plain", "Failed to send ping.");
    }
}

void handleDeviceViewerDelete() {
    registerHTTPMessage("DVDelete");  
    
    // Check if we have any devices
    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
        server.send(302, "text/plain", "No devices available to delete.");
        return;
    }

    
    // Ensure current device index is valid
    if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
        CURRENT_DEVICEVIEWER_DEVINDEX = 0;
        server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
        server.send(302, "text/plain", "Invalid device index.");
        return;
    }
    
    
    // Get current device
    ArborysDevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICEVIEWER_DEVINDEX);
    if (!device) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }

    if (device->MAC == Sensors.getDeviceMACByDevIndex(Sensors.findMyDeviceIndex())) {
      server.sendHeader("Location", "/DEVICEVIEWER?error=cannot_delete_myself");
      server.send(302, "text/plain", "Cannot delete this device.");
      return;
    }

    // Store device name for confirmation message
    String deviceName = String(device->devName);
    uint8_t deletedSensors = Sensors.initDevice(CURRENT_DEVICEVIEWER_DEVINDEX);
    
    // Adjust current device index if needed
    if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
        CURRENT_DEVICEVIEWER_DEVINDEX = 0;
    }
    
    // Redirect back to device viewer with success message
    if (!device->IsSet) {
        server.sendHeader("Location", "/DEVICEVIEWER?delete=success&device=" + deviceName + "&sensors=" + String(deletedSensors));
        server.send(302, "text/plain", "Device and sensors deleted successfully.");
      } else {
        server.sendHeader("Location", "/DEVICEVIEWER?error=delete_failed");
        server.send(302, "text/plain", "Failed to delete device.");
    }
}

void setupServerRoutes() {
    // Main routes
    server.on("/", handleRoot);
    server.on("/ALLSENSORS", handleALL);
    server.on("/POST", handlePost);
    server.on("/REQUESTUPDATE", handleREQUESTUPDATE);
    server.on("/CLEARSENSOR", handleCLEARSENSOR);
    server.on("/TIMEUPDATE", handleTIMEUPDATE);
    server.on("/REQUESTWEATHER", handleREQUESTWEATHER);
    server.on("/REBOOT", handleReboot);
    
    server.on("/STATUS", handleSTATUS);
    server.on("/RETRIEVEDATA", handleRETRIEVEDATA);
    server.on("/RETRIEVEDATA_MOVINGAVERAGE", handleRETRIEVEDATA_MOVINGAVERAGE);
    
    // Configuration routes
    server.on("/CONFIG", HTTP_GET, handleCONFIG);
    server.on("/CONFIG", HTTP_POST, handleCONFIG_POST);
    server.on("/CONFIG_DELETE", HTTP_POST, handleCONFIG_DELETE);
    server.on("/READONLYCOREFLAGS", HTTP_GET, handleREADONLYCOREFLAGS);
    
    #ifdef _ISPERIPHERAL
    // Sensor configuration routes for peripheral devices
    server.on("/SENSOR_UPDATE", HTTP_POST, handleSENSOR_UPDATE_POST);
    server.on("/SENSOR_READ_SEND_NOW", HTTP_POST, handleSENSOR_READ_SEND_NOW);
    #endif
    
    // Google Sheets routes
    server.on("/GSHEET", HTTP_GET, handleGSHEET);
    server.on("/GSHEET", HTTP_POST, handleGSHEET_POST);
    server.on("/GSHEET_UPLOAD_NOW", HTTP_POST, handleGSHEET_UPLOAD_NOW);
    server.on("/GSHEET_SHARE_ALL", HTTP_POST, handleGSHEET_SHARE_ALL);
    server.on("/GSHEET_DELETE_ALL", HTTP_POST, handleGSHEET_DELETE_ALL);
    
    // ESP-NOW Broadcast route
    server.on("/REQUEST_BROADCAST", HTTP_POST, handleREQUEST_BROADCAST);
    
    // New API routes for streamlined setup
    server.on("/api/wifi", HTTP_POST, apiConnectToWiFi);
    server.on("/api/wifi-scan", HTTP_GET, apiScanWiFi);
    server.on("/api/location", HTTP_POST, apiLookupLocation);
    server.on("/api/timezone", HTTP_GET, apiDetectTimezone);
    server.on("/api/timezone", HTTP_POST, apiSaveTimezone);
    server.on("/api/setup-status", HTTP_GET, apiGetSetupStatus);
    server.on("/InitialSetup", HTTP_GET, handleInitialSetup);
    server.on("/api/complete-setup", HTTP_POST, handleApiCompleteSetup);
    server.on("/api/complete-setup", HTTP_GET, handleApiCompleteSetup);
        
    // Timezone configuration routes (legacy - kept for backward compatibility)
    server.on("/TimezoneSetup", HTTP_GET, handleTimezoneSetup);
    server.on("/TimezoneSetup_POST", HTTP_POST, handleTimezoneSetup_POST);
    server.on("/UPDATETIMEZONE", HTTP_GET, handleUPDATETIMEZONE);
    server.on("/UPDATETIMEZONE", HTTP_POST, handleUPDATETIMEZONE_POST);
    
    // Weather routes
    server.on("/WEATHER", HTTP_GET, handleWeather);
    server.on("/WEATHER", HTTP_POST, handleWeather_POST);
    server.on("/WeatherRefresh", HTTP_POST, handleWeatherRefresh);
    server.on("/WeatherZip", HTTP_POST, handleWeatherZip);
    server.on("/WeatherAddress", HTTP_POST, handleWeatherAddress);
    
    // SD Card routes
    server.on("/SDCARD", HTTP_GET, handleSDCARD);
    server.on("/SDCARD_DELETE_DEVICES", HTTP_POST, handleSDCARD_DELETE_DEVICES);
    server.on("/SDCARD_DELETE_SENSORS", HTTP_POST, handleSDCARD_DELETE_SENSORS);
    server.on("/SDCARD_STORE_DEVICES", HTTP_POST, handleSDCARD_STORE_DEVICES);
    server.on("/SDCARD_DELETE_SCREENFLAGS", HTTP_POST, handleSDCARD_DELETE_SCREENFLAGS);
    server.on("/SDCARD_DELETE_WEATHERDATA", HTTP_POST, handleSDCARD_DELETE_WEATHERDATA);
    server.on("/SDCARD_SAVE_SCREENFLAGS", HTTP_POST, handleSDCARD_SAVE_SCREENFLAGS);
    server.on("/SDCARD_SAVE_WEATHERDATA", HTTP_POST, handleSDCARD_SAVE_WEATHERDATA);
    server.on("/SDCARD_TIMESTAMPS", HTTP_GET, handleSDCARD_TIMESTAMPS);
    server.on("/ERROR_LOG", HTTP_GET, handleERROR_LOG);
    server.on("/REBOOT_DEBUG", HTTP_GET, handleREBOOT_DEBUG);
    server.on("/SDCARD_DELETE_ERRORLOG", HTTP_POST, handleSDCARD_DELETE_ERRORLOG);
    server.on("/SDCARD_DELETE_TIMESTAMPS", HTTP_POST, handleSDCARD_DELETE_TIMESTAMPS);
    server.on("/SDCARD_DELETE_GSHEET", HTTP_POST, handleSDCARD_DELETE_GSHEET);
    
    // Device viewer routes
    server.on("/DEVICEVIEWER", HTTP_GET, handleDeviceViewer);
    server.on("/DEVICEVIEWER_NEXT", HTTP_GET, handleDeviceViewerNext);
    server.on("/DEVICEVIEWER_PREV", HTTP_GET, handleDeviceViewerPrev);
    server.on("/DEVICEVIEWER_PING", HTTP_GET, handleDeviceViewerPing);
    server.on("/DEVICEVIEWER_DELETE", HTTP_GET, handleDeviceViewerDelete);

    // 404 handler
    server.onNotFound(handleNotFound);
}

//___________________START OF JSON___________________
//json builders 
String JSONbuilder_device(ArborysDevType* device) {
  String deviceJSON = "\"senderDevice\":{\"mac\":\"";
  deviceJSON += MACToString(device->MAC, '\0', true);
  deviceJSON += "\",\"ip\":\"";
  deviceJSON += device->IP.toString();
  deviceJSON += "\",\"name\":\"";
  deviceJSON += String(device->devName);
  deviceJSON += "\",\"devType\":";
  deviceJSON += device->devType;
  deviceJSON += "}";
  return deviceJSON;
}

String JSONbuilder_sensorData(ArborysSnsType* S) {
  String sensorJSON = "\"sensorData\":" + JSONbuilder_sensorObject(S);
  return sensorJSON;
}

String JSONbuilder_sensorObject(ArborysSnsType* S) {
  String sensorJSON = "{\"type\":";
  sensorJSON += S->snsType;
  sensorJSON += ",\"id\":";
  sensorJSON += S->snsID;
  sensorJSON += ",\"name\":\"";
  sensorJSON += String(S->snsName);
  sensorJSON += "\",\"value\":";
  if (isnan(S->snsValue)) sensorJSON += "null";
  else sensorJSON += S->snsValue;
  sensorJSON += ",\"timeRead\":";
  sensorJSON += S->timeRead;
  sensorJSON += ",\"sendingInt\":";
  sensorJSON += S->SendingInt;
  sensorJSON += ",\"flags\":";
  sensorJSON += S->Flags;
  sensorJSON += "}";
  return sensorJSON;
}

void JSONbuilder_sensorMSG(ArborysSnsType* S, char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP) {
  // Build full json sensor type message
  ArborysDevType* device = Sensors.getDeviceByDevIndex(S->deviceIndex);
  if (!device) {
    SerialPrint("JSONbuilder_sensorMSG: Device not found",true);
    storeError("JSONbuilder_sensorMSG: Device not found", ERROR_JSON_PARSE, true);
    return;
  } 

  String tempJSON = "{\"msgType\":\"snsData\"," + JSONbuilder_device(device) + "," + JSONbuilder_sensorData(S) + "}";

   // Build http body if needed
   if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);
   snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());

   return;
}

void JSONbuilder_sensorMSG_all(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP) {
   
  int16_t mydeviceIndex = Sensors.findMyDeviceIndex();
  ArborysDevType* device = Sensors.getDeviceByDevIndex(mydeviceIndex);
  if (!device) {
    SerialPrint("JSONbuilder_sensorMSG_all: My device not found", true);
    storeError("JSONbuilder_sensorMSG_all: My device not found", ERROR_JSON_PARSE, true);
    return;
  }

  String tempJSON;
  tempJSON.reserve(256);
  tempJSON = "{\"msgType\":\"snsData\"," + JSONbuilder_device(device) + ",\"sensors\":[";

  bool first = true;
  for (int16_t i = 0; i < NUMSENSORS; ++i) {
    ArborysSnsType* S = Sensors.getSensorBySnsIndex(i);
    if (!S || S->deviceIndex != mydeviceIndex) continue;

    if (!first) tempJSON += ",";
    tempJSON += JSONbuilder_sensorObject(S);
    first = false;
  }

  tempJSON += "]}";

  if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);

  if (tempJSON.length() >= jsonBufferSize) {
    SerialPrint("JSONbuilder_sensorMSG_all: Buffer too small for JSON payload", true);
    storeError("JSONbuilder_sensorMSG_all: Buffer too small", ERROR_JSON_PARSE, true);
    return;
  }

  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());
}

void JSONbuilder_DataRequestMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP, int16_t snsIndex) {
  ArborysDevType* device = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  if (!device) {
    SerialPrint("JSONbuilder_DataRequestMSG: My Device not found",true);
    storeError("JSONbuilder_DataRequestMSG: My Device not found", ERROR_JSON_PARSE, true);
    return;
  }

  int16_t snsType = -1;
  int16_t snsID = -1;
  if (snsIndex >= 0) {
    ArborysSnsType* S = Sensors.getSensorBySnsIndex(snsIndex);
    if (!S) {
      SerialPrint("JSONbuilder_DataRequestMSG: Sensor " + String(snsIndex) + " not found",true);
      storeError("JSONbuilder_DataRequestMSG: Sensor " + String(snsIndex) + " not found", ERROR_JSON_PARSE, true);
      return;
    }
    snsType = S->snsType;
    snsID = S->snsID;
  } else {
    //indicate all sensors for that device should be sent
    snsType = -1;
    snsID = -1;
  }

  String tempJSON = "{\"msgType\":\"sendSensorDataNow\",\"snsType\":" + String(snsType) + ",\"snsID\":" + String(snsID) + "," + JSONbuilder_device(device) + "}";

  if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);
  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());

  return;
}

void JSONbuilder_pingMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP, bool isAck) {
  ArborysDevType* device = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  if (!device) {
    SerialPrint("JSONbuilder_pingMSG: Device not found",true);
    storeError("JSONbuilder_pingMSG: Device not found", ERROR_JSON_PARSE, true);
    return;
  }

  String tempJSON = "{\"msgType\":" + String(isAck ? "\"ackPing\"" : "\"helloPing\"") + "," + JSONbuilder_device(device) + "}";

  if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);
  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());

  return;
}

uint16_t JSONbuilder_encodeHTTP(String& jsonBuffer) {
  String encodedJson = urlEncode(jsonBuffer);
  jsonBuffer = "JSON=" + encodedJson;
  return encodedJson.length();
}

//----------------------------- json handlers for receiving data -----------------------------
//json handlers for receiving data
void processJSONMessage(String& postData, String& responseMsg) {
 //this is called when json data is received.

  SerialPrint("Processing sensor data JSON: " + postData,true);
   //process the sensor data JSON buffer

   StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, postData);
    if (err) {
      SerialPrint("Error deserializing JSON: " + String(err.c_str()),true);
      storeError("Error deserializing JSON: " + String(err.c_str()), ERROR_JSON_PARSE, true);
      responseMsg = "Error deserializing JSON: " + String(err.c_str());
      return;
    }

    String msgType = "NOTSET";
  // Backward compatibility: legacy packets have no msgtype
  if (!doc.containsKey("msgType") || doc["msgType"].isNull()) {
    msgType = "snsData";        // legacy ⇒ assume sensor update
  } else {
      msgType = doc["msgType"].as<String>();
  }
  JsonObject root = doc.as<JsonObject>();   

  if (msgType == "snsData") {
    //we have received sensor data
    processJSONMessage_sensorData(root, responseMsg);
  } 
  else if (msgType == "sendSensorDataNow") {
    //we have received a request to send a single sensor
    processJSONMessage_DataRequest(root, responseMsg);
  }
  else if (msgType == "helloPing") {
    //we have received a hello ping
    processJSONMessage_ping(root, responseMsg, true);
  }
  else if (msgType == "ackPing") {
    //we have received a ping ack
    processJSONMessage_ping(root, responseMsg, false);
  }
  else {
    SerialPrint("Unknown message type: " + msgType,true);
    SerialPrint("Erroneous Post data: " + postData,true);
    storeError("Unknown message type: " + msgType, ERROR_JSON_PARSE, true);
    responseMsg = "Unknown message type: " + msgType;
    return;
  }
}


int16_t processJSONMessage_addDevice(JsonObject root, String& responseMsg) {
  IPAddress devIP = IPAddress(0,0,0,0);
  uint64_t devMAC = 0;
  String devName = "Unknown";
  uint8_t devType = 0;

  int16_t senderIndex = -1;

  if (root["senderDevice"].is<JsonObject>()) {
    JsonObject senderDevice = root["senderDevice"];
    if (senderDevice["ip"].is<JsonVariantConst>())    devIP.fromString(senderDevice["ip"].as<String>());
    if (senderDevice["mac"].is<JsonVariantConst>())    stringToUInt64(senderDevice["mac"].as<String>(), &devMAC, true);
    if (senderDevice["name"].is<JsonVariantConst>())    devName = senderDevice["name"].as<String>();
    if (senderDevice["devType"].is<JsonVariantConst>())    devType = senderDevice["devType"];
    //register the device sending me the request
    senderIndex = Sensors.addDevice(devMAC, devIP, devName.c_str(), 0, 0, devType);
  }

  if (senderIndex < 0) {
    SerialPrint("Failed to register sender device",true);
    responseMsg = "Failed to register sender device";
    storeError("Failed to register sender device: " + devName, ERROR_JSON_PARSE, true);
  }

  return senderIndex;

}


void processJSONMessage_ping(JsonObject root, String& responseMsg, bool isAck) {
  responseMsg = "OK";

  int16_t senderIndex = processJSONMessage_addDevice(root, responseMsg);

  if (senderIndex == -1) {
    return;
  }

  if (!isAck) {
    //not the ack, respond with a ping ack
    char jsonBuffer[512];
    JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), true, true);
    if (sendHTTPJSON(senderIndex, jsonBuffer, "pingAck")==false) {      
      responseMsg = "Failed to return HTTP ping";
      storeError("Failed to return HTTP ping", ERROR_JSON_PARSE, true);
      return;
    }
  }
  return;
}


void processJSONMessage_DataRequest(JsonObject root, String& responseMsg) {
  responseMsg = "OK";
  

  int16_t senderIndex = processJSONMessage_addDevice(root, responseMsg);
  if (senderIndex <0) {
    return;
  }

  int16_t snsType = root["snsType"].as<int16_t>();
  int16_t snsID = root["snsID"].as<int16_t>();


  if (snsType >= 0 && snsID >= 0) {
    //now send the sensor data to the sender
    int16_t sensorIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
    if (sensorIndex >= 0) {
      SendData(sensorIndex, true, senderIndex, false); //force send data to sensorindex using http
    } else {
      SerialPrint("Sensor not found",true);
      responseMsg = "Sensor not found";
      storeError("Sensor not found", ERROR_JSON_PARSE, true);
      return;
    }
  } else {
    //now send all sensors to the sender
    sendAllSensors(true, senderIndex, false);
  }
  return;
}



void processJSONMessage_sensorData(JsonObject root, String& responseMsg) {

  int16_t deviceIndex = processJSONMessage_addDevice(root, responseMsg);
  if (deviceIndex < 0) {
    SerialPrint("Failed to register sender device",true);
    responseMsg = "Failed to register sender device";
    storeError("snsData failed: " + responseMsg, ERROR_JSON_PARSE, true);
    return;
  }
  
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) {
    responseMsg = "Sender device not found";
    storeError("Sender device not found while processing sensor data", ERROR_JSON_PARSE, true);
    return;
  }

  
  responseMsg = "OK";

  // Legacy payload: single sensor object
  if (root["sensorData"].is<JsonObject>()) {
    JsonObject sensor = root["sensorData"];
    handleSingleSensor(d, sensor, responseMsg);
    return;
  }

  // New payload: array of sensors
  if (root["sensors"].is<JsonArray>()) {
      JsonArray sensors = root["sensors"];
      for (JsonObject sensor : sensors) {
          if (responseMsg != "OK") break;     // stop on first error
          handleSingleSensor(d, sensor, responseMsg);
      }
      return;
  }

  responseMsg = "Invalid JSON message format - missing sensor data";
}


void handleSingleSensor(ArborysDevType* dev, JsonObject sensor, String& responseMsg) {
  if (!sensor.containsKey("type") || !sensor.containsKey("id") || !sensor.containsKey("name") || !sensor.containsKey("value")) {
      responseMsg = "Invalid sensor entry";
      return;
  }

  uint8_t snsType = sensor["type"];
  uint8_t snsID   = sensor["id"];
  String snsName  = sensor["name"].as<String>();
  double value = NAN;
  if (sensor.containsKey("value")) {
    (sensor["value"].isNull()) ? value = NAN : value =  sensor["value"].as<double>();
  }
  uint32_t timeRead   = sensor["timeRead"]   | I.currentTime;
  uint32_t sendingInt = sensor["sendingInt"] | 3600;
  uint8_t flags       = sensor["flags"]      | 0;

  uint8_t ret = registerSensorData(
      dev->MAC, dev->IP, dev->devName, dev->devType, dev->Flags,
      snsType, snsID, snsName, value, timeRead, I.currentTime, sendingInt, flags
  );

  if (ret == 0) responseMsg = "Failed to add sensor";

}

 uint8_t registerSensorData(uint64_t deviceMAC, IPAddress deviceIP, String devName, uint8_t devType, uint8_t devFlags, uint8_t snsType, uint8_t snsID, String snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, uint32_t sendingInt, uint8_t flags) {
   //returns 0 if failed to add sensor, 1 if sensor was added, 2 if sensor was already in the database and is updated
 
   uint8_t ret = 0;
   bool addToSD = false;
   int16_t sensorIndex = -1;
   if (deviceMAC!=0 && deviceIP!=IPAddress(0,0,0,0) && snsType!=0 ) {
     if (devName.length() == 0) devName = "Unknown";
 
     //is this sensor already in the database?
     sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
     if (sensorIndex >= 0) {
       ret = 2; //sensor was found in the database and is updated
       if (Sensors.getSensorFlag(sensorIndex) != flags) {
         SerialPrint("Sensor found, flags changed, adding to SD",true);
         addToSD = true;
       }
     } else {
       ret =1; //sensor was not found in the database and is added
       SerialPrint("Sensor not found, adding to Devices_Sensors class",true);
       addToSD = true;
     }   

     //is this a low power sensor?
     if (bitRead(flags,2) == 1) {
      //low power sensors do not know if they switched flag status... get the last reading, which has not yet been updated
      ArborysSnsType* OldS = Sensors.getSensorBySnsIndex(Sensors.findSensor(deviceMAC, snsType, snsID));
      if (OldS) {
        if (OldS->Flags != flags && (bitRead(flags,0) !=  bitRead(flags,0))) { //flag status changed
          bitWrite(flags,6,1); //flag changed since last read
        }          
      }
     }

     // Add sensor to Devices_Sensors class
     sensorIndex = Sensors.addSensor(deviceMAC, deviceIP, snsType, snsID, 
                                           snsName.c_str(), snsValue, 
                                           timeRead, timeLogged, sendingInt, flags, devName.c_str(), devType);
 
     if (sensorIndex < 0) {
       SerialPrint("Failed to add sensor",true);
       return 0; //failed to add sensor
     } 
   }
 
   #ifdef _USESDCARD
   if (sensorIndex >= 0 && addToSD) {
     //message was successfully added to Devices_Sensors class
     //save to SD card if addToSD is true [flag was changed]. Otherwise it will save hourly
     if (storeSensorDataSD(sensorIndex)) {
       SerialPrint("Individual sensor data saved to SD: " + snsName + " (index " + String(sensorIndex) + ")", true);
     } else {
       SerialPrint("Warning: Failed to save individual sensor data to SD: " + snsName + " (index " + String(sensorIndex) + ")", true);
     }
   }
   #endif
  
   #ifdef _USEGSHEET
   //we got a sensor reading, so upload the data to the spreadsheet if time is appropriate
   if (GSheetInfo.useGsheet && sensorIndex >= 0) {
    ArborysSnsType* S = Sensors.getSensorBySnsIndex(sensorIndex);
 
     if (!Gsheet_uploadSensorDataFunction(S)) {
        SerialPrint("Failed to upload sensor " + String(S->snsName) + " to spreadsheet",true);
        storeError("Gsheet: failed for sns:" + String(sensorIndex), ERROR_GSHEET_UPLOAD, true);
     }
    }
   #endif

  
   return ret;
 }

 //___________________END OF JSON handlers___________________
 
 void handlePost() {
    String responseMsg = "OK";

    registerHTTPMessage("snsData");
    // Check if this is JSON data (new format)
    if (server.hasArg("JSON")) {
        String postData = server.arg("JSON");
        processJSONMessage(postData, responseMsg); 
    } else {
      SerialPrint("No JSON data received",true);
      responseMsg = "No JSON data received";
    }


    if (responseMsg == "OK") {
      server.send(200, "text/plain", responseMsg);
    } else {
      SerialPrint("Failed to add sensor with response message: " + responseMsg + "\n");
      server.send(500, "text/plain", responseMsg);
    }
    return;
 
 }
 


//handlers for sending data
bool checkThisSensorTime(ArborysSnsType* Si) {
  //return true if it is time to send this sensor
  if (!Si || Si->deviceIndex != MY_DEVICE_INDEX) return false;
  if (Si->timeLogged != 0 && Si->timeLogged < I.currentTime && I.currentTime - Si->timeLogged < 60*60*24 && bitRead(Si->Flags, 6) == 0 ) {
    if (Si->timeLogged + Si->SendingInt > I.currentTime) return false; //not time
  }
    return true;
}

bool isSensorSendTime(int16_t snsIndex) {
  // When snsIndex == -1, check all sensors and return true if ANY sensor is due
  if (snsIndex < 0) {
    for (int16_t i = 0; i < NUMSENSORS; i++) {
      ArborysSnsType* Si = Sensors.getSensorBySnsIndex(i);
      if (checkThisSensorTime(Si)) return true;
    }
    return false;
  }

  // Single sensor path
  ArborysSnsType* S = Sensors.getSensorBySnsIndex(snsIndex);
  if (checkThisSensorTime(S)) return true;
  return false;
}

bool isDeviceSendTime(ArborysDevType* D, bool forceSend) {
  
  //basic check - does this device exist?
  if (!D || !D->IsSet) return false;
  if (D->IP==WiFi.localIP()) return false; //do not send to myself
  if (forceSend) return true;
  if (D->devType < 100) return false; //not a server

  return true;
}

//___________________START OF HTTP SEND HANDLERS___________________

void wrapupSendData(ArborysSnsType* S) {
  if (!S) {
    //special case, all sensors sent
    for (int16_t i = 0; i < NUMSENSORS; i++) {
      ArborysSnsType* S = Sensors.getSensorBySnsIndex(i);
      if (!S) continue;
      if (S->deviceIndex != MY_DEVICE_INDEX) continue; //don't send others sensors
      bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I sent the value so this is the new baseline. Set bit 6 (change in flag) to zero
      S->timeLogged = I.currentTime;
    }
    return;
  }
  bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I sent the value so this is the new baseline. Set bit 6 (change in flag) to zero
  S->timeLogged = I.currentTime;
}

int16_t sendHTTPJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002; //device not found
  return sendHTTPJSON(d->IP, jsonBuffer, msgType);
}

int16_t sendHTTPJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType) {
  //http method

  if (WifiStatus() ==false) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1001; //not connected to wifi
  }

  static char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", ip.toString().c_str());

  // Create HTTP client with proper cleanup
  WiFiClient wfclient;
  HTTPClient http;
  
  // Set timeout to prevent hanging
  http.setTimeout(5000); // 5 second timeout
  
  http.useHTTP10(true);

  if (http.begin(wfclient, urlBuffer)) {
  http.addHeader("Content-Type","application/x-www-form-urlencoded");
  int httpCode = http.POST((uint8_t*)jsonBuffer, strlen(jsonBuffer));
  //String payload = http.getString();
    
    // Always end the connection
  http.end();

  registerHTTPSend(ip, msgType);
  return httpCode;
  }
  I.HTTP_OUTGOING_ERRORS++;
  return -1000; //failed to begin connection
}  

uint8_t sendAllSensors(bool forceSend, int16_t sendToDeviceIndex, bool useUDP) {
  //can use -1 to send to broadcast (if using HTTP, then send to all servers), or a specific device index to send to a specific device
  
  return SendData(-1,forceSend,sendToDeviceIndex,useUDP);

  
}


bool SendData(int16_t snsIndex, bool forceSend, int16_t sendToDeviceIndex, bool useUDP) {
  //forcesend will always send. If sentodeviceindex is >=0, it will send to the device at that index.
  if (!forceSend) {
    //should we send unmonitored sensors? Uncomment this to skip sending unmonitored sensors. The advantage of sending is that the server will have all sensor data, but these take up space.
    //if (bitRead(S->Flags,1) == 0) return false; //not monitored
    if (!isSensorSendTime(snsIndex)) return false; //not time to send
  }


  bool isGood = false;

  if(WiFi.status() == WL_CONNECTED){
    // Use static buffers to reduce memory fragmentation
    static char jsonBuffer[SNSDATA_JSON_BUFFER_SIZE];

    ArborysSnsType* S = Sensors.getSensorBySnsIndex(snsIndex);
    if (S == nullptr) { //send all sensors
      if (useUDP)  JSONbuilder_sensorMSG_all(jsonBuffer, SNSDATA_JSON_BUFFER_SIZE,false);
      else JSONbuilder_sensorMSG_all(jsonBuffer, SNSDATA_JSON_BUFFER_SIZE,true);
  
    } else {
      if (useUDP)  JSONbuilder_sensorMSG(S, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE,false);
      else JSONbuilder_sensorMSG(S, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE,true);
  
    }
  

    if (useUDP && sendToDeviceIndex <0) { //special value for sending to broadcast UDP to everyone
      SerialPrint("Sending to all devices", true);
  
      sendUDPMessage((uint8_t*)jsonBuffer, IPAddress(255,255,255,255), strlen(jsonBuffer),"snsBrdcst");
      #ifndef _USELOWPOWER
      for (int16_t i=0; i<NUMDEVICES ; i++) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
        if (d && d->IsSet && d->IP != WiFi.localIP()) d->dataSent = I.currentTime;
      }
      wrapupSendData(S);
      #endif
      return true;
    }

    if (sendToDeviceIndex >= 0) {
      //send data to a specific device
      ArborysDevType* d = Sensors.getDeviceByDevIndex(sendToDeviceIndex);
      if (isDeviceSendTime(d, forceSend)) {
        if (useUDP) {
          isGood = sendUDPMessage((uint8_t*)jsonBuffer, d->IP, strlen(jsonBuffer),"snsMsg");
        }
        else if (sendHTTPJSON(d->IP, jsonBuffer, "snsMsg") == 200) {
          isGood = true;                  
        }
        if (isGood) d->dataSent = I.currentTime;
      }
    } else {
      // now send to all servers I know of. iterate all known devices to find servers  (devType >=100). forcesend MUST be false here (we are not going to send to every device)
      for (int16_t i=0; i<NUMDEVICES ; i++) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
        if (!isDeviceSendTime(d, false)) continue; //not time to send to this server yet because we have recently sent data
        
        //udp method, targetted
        if (useUDP) {
          SerialPrint("Sending UDP sensor data to " + d->IP.toString(), true);
          if (sendUDPMessage((uint8_t*)jsonBuffer, d->IP, strlen(jsonBuffer), "snsMsg") == true) {
            d->dataSent = I.currentTime;
            isGood = true;
          } else {
            SerialPrint("Failed to send UDP sensor data to " + d->IP.toString(), true);
            //set the sent time such that it will trigger a retry send in 10 minutes
            d->dataSent = I.currentTime -d->SendingInt + 10*60;
          }
        } else {
          if (sendHTTPJSON(d->IP, jsonBuffer, "snsMsg") == 200) {
            d->dataSent = I.currentTime;
            isGood = true;
          } else {
            SerialPrint("Failed to send HTTP sensor data to " + d->IP.toString(), true);
            //set the sent time such that it will trigger a retry send in 10 minutes
            d->dataSent = I.currentTime -d->SendingInt + 10*60;
          }
        }
        
        // Small delay to prevent overwhelming servers
        delay(10);
      }
    }
  
    if (isGood) {
      wrapupSendData(S);
    }  else {
      I.makeBroadcast = true; //broadcast my presence if I failed to send the data
    }
    return isGood;
  }

  return false;
}


int16_t sendMSG_ping(IPAddress& ip, bool viaHTTP) {
  char jsonBuffer[512];
  JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), viaHTTP, false);
  if (viaHTTP) {
    return sendHTTPJSON(ip, jsonBuffer, "pingMsg");
  } else {
    return sendUDPMessage((uint8_t*)jsonBuffer, ip, strlen(jsonBuffer), "pingMsg");
  }
}

int16_t sendMSG_DataRequest(int16_t deviceIndex, int16_t snsIndex, bool viaHTTP) {
  //send a data request to a specific device and sensor, or use snsIndex = -1 to request all sensors
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);  
  return sendMSG_DataRequest(d, snsIndex, viaHTTP);
}

int16_t sendMSG_DataRequest(ArborysDevType* d, int16_t snsIndex, bool viaHTTP) { //snsindex is which sensor we want, or -1 for all sensors
  if (!d) {
    SerialPrint("sendMSG_DataRequest: Device not found: " + String(d->devName), true);
    return -1002; //device not found
  }
  char jsonBuffer[512];
  JSONbuilder_DataRequestMSG(jsonBuffer, sizeof(jsonBuffer), viaHTTP, snsIndex);
  SerialPrint("sendMSG_DataRequest: " + String(jsonBuffer), true);
  SerialPrint("sendMSG_DataRequest sent to: " + String(d->IP.toString()), true);
  d->dataSent = I.currentTime;
  if (viaHTTP) {
    return sendHTTPJSON(d->IP, jsonBuffer, "snsReq");
  } else {
    return sendUDPMessage((uint8_t*)jsonBuffer, d->IP, strlen(jsonBuffer), "snsReq");
  }
}

//___________________END OF HTTP SEND HANDLERS___________________

String getWiFiModeString() {
  //possible modes are WIFI_MODE_OFF, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA
  switch (WiFi.getMode()) {
    case WIFI_MODE_NULL:
      return "WIFI_MODE_OFF";
    case WIFI_MODE_STA:
      return "WIFI_MODE_STA";
    case WIFI_MODE_AP:
      return "WIFI_MODE_AP";
    case WIFI_MODE_APSTA:
      return "WIFI_MODE_APSTA";
  }
  return "UNKNOWN";
}



// Complete address to coordinates conversion using US Census Bureau Geocoding API
bool getCoordinatesFromAddress(const String& street, const String& city, const String& state, const String& zipCode) {

  double lat = 0, lon = 0;

  // Validate ZIP code format (5 digits)
  if (zipCode.length() != 5) {
      SerialPrint("Invalid ZIP code format. Must be 5 digits.", true);
      return false;
  }
  
  for (int i = 0; i < 5; i++) {
      if (!isdigit(zipCode.charAt(i))) {
          SerialPrint("Invalid ZIP code format. Must contain only digits.", true);
          return false;
      }
  }
  
  // Validate state format (2 letters)
  if (state.length() != 2) {
      SerialPrint("Invalid state format. Must be 2 letters.", true);
      return false;
  }
  
  // Build the URL for the Census Bureau Geocoding API
  String url = "https://geocoding.geo.census.gov/geocoder/locations/address?";
  url += "street=" + urlEncode(street);
  url += "&city=" + urlEncode(city);
  url += "&state=" + urlEncode(state);
  url += "&zip=" + zipCode;
  url += "&benchmark=Public_AR_Current&format=json";
  
  JsonDocument doc;
  int httpCode;
  
  SerialPrint(("Fetching coordinates for address: " + street + ", " + city + ", " + state + " " + zipCode).c_str(), true);
  SerialPrint(("API URL: " + url).c_str(), true);
  
  // Make HTTP request to Census Geocoding API
  HTTPClient http;
  http.begin(url);
  http.setTimeout(15000); // 15 second timeout for geocoding
  
  httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      
      SerialPrint("Received response from Census Geocoding API", true);
      
      // Parse JSON response
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
          SerialPrint("Failed to parse JSON response from Census Geocoding API", true);
          SerialPrint(("JSON Error: " + String(error.c_str())).c_str(), true);
          storeError("Failed to parse JSON response from Census Geocoding API", ERROR_JSON_GEOCODING,true);
          return false;
      }
      
      // Check if we have address matches
      if (doc["result"]["addressMatches"].is<JsonArray>()) {
          JsonArray addressMatches = doc["result"]["addressMatches"];
          
          if (addressMatches.size() > 0) {
              // Get the first (best) match
              JsonObject match = addressMatches[0];
              
              if (match["coordinates"]["x"].is<double>() && match["coordinates"]["y"].is<double>()) {
                  lon = match["coordinates"]["x"].as<double>();
                  lat = match["coordinates"]["y"].as<double>();
                  
                  SerialPrint(("Coordinates found: " + String(lat, 6) + ", " + String(lon, 6)).c_str(), true);
                  Prefs.LATITUDE = lat;
                  Prefs.LONGITUDE = lon;
                  Prefs.isUpToDate = false;
                  // Log the matched address for verification
                  if (match["matchedAddress"].is<String>()) {
                      String matchedAddress = match["matchedAddress"].as<String>();
                      SerialPrint(("Matched Address: " + matchedAddress).c_str(), true);
                  }
                  
                  return true;
              }
          }
      }
      
      SerialPrint("No address matches found in the response", true);
  } else {
      SerialPrint(("HTTP request failed with code: " + String(httpCode)).c_str(), true);
      http.end();
  }
  
  // Fallback to ZIP code only method
  SerialPrint("Falling back to ZIP code only method", true);
  return getCoordinatesFromZipCodeFallback(zipCode);
}

// ZIP code to coordinates conversion using US Census Bureau Geocoding API (legacy function)
bool getCoordinatesFromZipCode(const String& zipCode) {
  double lat = 0, lon = 0;
  // Validate ZIP code format (5 digits)
  if (zipCode.length() != 5) {
      SerialPrint("Invalid ZIP code format. Must be 5 digits.", true);
      storeError("Invalid ZIP code format. Must be 5 digits.", ERROR_JSON_GEOCODING,true);
      return false;
  }
  
  for (int i = 0; i < 5; i++) {
      if (!isdigit(zipCode.charAt(i))) {
          SerialPrint("Invalid ZIP code format. Must contain only digits.", true);
          storeError("Invalid ZIP code format. Must contain only digits.", ERROR_JSON_GEOCODING,true);
          return false;
      }
  }
  
  // For now, we'll use a default address structure since we only have ZIP code
  // In a full implementation, you would collect street, city, state from the user
  String street = "1 Main St";  // Default street address
  String city = "Unknown";      // Default city
  String state = "MA";          // Default state (you might want to make this configurable)
  
  // Build the URL for the Census Bureau Geocoding API
  String url = "https://geocoding.geo.census.gov/geocoder/locations/address?";
  url += "street=" + urlEncode(street);
  url += "&city=" + urlEncode(city);
  url += "&state=" + urlEncode(state);
  url += "&zip=" + zipCode;
  url += "&benchmark=Public_AR_Current&format=json";
  
  JsonDocument doc;
  int httpCode;
  
  SerialPrint(("Fetching coordinates for ZIP code: " + zipCode).c_str(), true);
  
  // Make HTTP request to Census Geocoding API
  HTTPClient http;
  http.begin(url);
  http.setTimeout(15000); // 15 second timeout for geocoding
  
  httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      
      SerialPrint("Received response from Census Geocoding API", true);
      
      // Parse JSON response
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
          SerialPrint("Failed to parse JSON response from Census Geocoding API", true);
          storeError("Failed to parse JSON response from Census Geocoding API", ERROR_JSON_GEOCODING,true);
          return false;
      }
      
      // Check if we have address matches
      if (doc["result"]["addressMatches"].is<JsonArray>()) {
          JsonArray addressMatches = doc["result"]["addressMatches"];
          
          if (addressMatches.size() > 0) {
              // Get the first (best) match
              JsonObject match = addressMatches[0];
              
              if (match["coordinates"]["x"].is<double>() && match["coordinates"]["y"].is<double>()) {
                  lon = match["coordinates"]["x"].as<double>();
                  lat = match["coordinates"]["y"].as<double>();
                  
                  SerialPrint(("Coordinates found: " + String(lat, 6) + ", " + String(lon, 6)).c_str(), true);
                  Prefs.LATITUDE = lat;
                  Prefs.LONGITUDE = lon;
                  Prefs.isUpToDate = false;
                  
                  // Save to NVS
                  BootSecure bootSecure;
                  int8_t ret = bootSecure.setPrefs();
                  if (ret < 0) {
                    SerialPrint("getCoordinatesFromZipCode: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
                  }
                  
                  // Log the matched address for verification
                  if (match["matchedAddress"].is<String>()) {
                      String matchedAddress = match["matchedAddress"].as<String>();
                      SerialPrint(("Matched Address: " + matchedAddress).c_str(), true);
                  }
                  
                  return true;
              }
          }
      }
      storeError("No address matches found in the response", ERROR_JSON_GEOCODING,true);
      SerialPrint("No address matches found in the response", true);

  } else {
      SerialPrint(("HTTP request failed with code: " + String(httpCode)).c_str(), true);

      String error = "HTTP request failed with code: " + String(httpCode);
      storeError(error.c_str(), ERROR_JSON_GEOCODING,true);
      http.end();
  }
  
  // Fallback to ZIP code only method
  SerialPrint("Falling back to ZIP code only method", true);
  return getCoordinatesFromZipCodeFallback(zipCode);
}


// Fallback method using a simple geocoding service
bool getCoordinatesFromZipCodeFallback(const String& zipCode) {
  // Use a simple geocoding service (example with a free API)
  // Note: This is a simplified approach. In production, you might want to use
  // a more reliable service like Google Geocoding API (requires API key)
  
  double lat = 0, lon = 0;
  String url = "http://api.zippopotam.us/US/" + zipCode;
  
  JsonDocument doc;
  int httpCode;
  
  SerialPrint(("Using fallback geocoding service for ZIP: " + zipCode).c_str(), true);
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000);
  
  httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
          SerialPrint("Failed to parse JSON response from geocoding service", true);
          storeError("Failed to parse JSON response from geocoding service", ERROR_JSON_GEOCODING,true);

          return false;
      }
      
      // Extract coordinates from the response
      if (doc["places"][0]["latitude"].is<String>() && doc["places"][0]["longitude"].is<String>()) {
          lat = doc["places"][0]["latitude"].as<double>();
          lon = doc["places"][0]["longitude"].as<double>();

          Prefs.LATITUDE = lat;
          Prefs.LONGITUDE = lon;
          Prefs.isUpToDate = false;
          
          // Save to NVS
          BootSecure bootSecure;
          int8_t ret = bootSecure.setPrefs();
          if (ret < 0) {
            SerialPrint("getCoordinatesFromZipCodeFallback: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
          }
          
          SerialPrint(("Coordinates found: " + String(lat, 6) + ", " + String(lon, 6)).c_str(), true);
          return true;
      }
  } else {
      SerialPrint(("Fallback geocoding failed with HTTP code: " + String(httpCode)).c_str(), true);
      String error = "Fallback geocoding failed with HTTP code: " + String(httpCode);
      storeError(error.c_str(), ERROR_JSON_GEOCODING,true);
      http.end();
  }
  
  // If all methods fail, return false
  SerialPrint("All geocoding methods failed for ZIP code: " + zipCode, true);
  return false;
} 

#ifdef _USEUDP
//In addition to ESPnow, I will also use UDP for all of the above message types
//In addition to ESPnow, I will also use UDP for all of the above message types

bool receiveUDPMessage() {
  //receive a message via UDP
  //return true if message is received, false if no message is received
  #ifdef _USEUDP

  int packetSize = LAN_UDP.parsePacket(); //>0 if message received!
  if (packetSize > 0) {
    IPAddress remoteIP = LAN_UDP.remoteIP();
    if (remoteIP == WiFi.localIP()) {
      return false;
    }
    SerialPrint("UDP message from: " + remoteIP.toString(),true);
    registerUDPMessage(remoteIP,0);


    if (packetSize == sizeof(ESPNOW_type)) {
      ESPNOW_type msg = {};
      LAN_UDP.read((uint8_t*)&msg, packetSize);        
      processLANMessage(&msg);        
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "ESP:%d", msg.msgType);
        
      return true;
    } else {
      //assume this is a sensor data json buffer
      //am I a server type? Should I interpret this?
      if (_MYTYPE < 100) {
          return false;
      }
      if (packetSize+1 > SNSDATA_JSON_BUFFER_SIZE) {
        SerialPrint("UDP message is too large", true);
        snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "TooLarge");
        return false;
      }
      char buffer[packetSize+1];
      LAN_UDP.read(buffer, packetSize);
      buffer[packetSize] = '\0'; //ensure the buffer is null terminated
      String responseMsg = "OK";
      String postData = (String)buffer;
      processJSONMessage(postData, responseMsg);
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "JSON");

      if (responseMsg == "OK") {
        return true;
      }
      else {
        return false;  
      }
    }
  } 
  return false;
  #endif
  return false;
}

bool sendUDPMessage(const uint8_t* buffer,  IPAddress ip, uint16_t bufferSize, const char* msgType) {
  //send the provided jsonbuffer via UDP
  //return true if successful, false if failed
  #ifdef _USEUDP
  SerialPrint("Sending UDP message to " + ip.toString(),true);
  SerialPrint("Buffer contains: " + String((char*)buffer),true);
  if (I.UDP_LAST_OUTGOINGMSG_TIME == I.currentTime) delay(50); //wait if the last message was sent within the last second
  if (bufferSize==0) bufferSize = strlen((const char*)buffer);
  if (ip == IPAddress(0,0,0,0)) ip = IPAddress(255,255,255,255); //broadcast if no ip is provided
  LAN_UDP.beginPacket(ip, _USEUDP);
  LAN_UDP.write(buffer, bufferSize);
  LAN_UDP.endPacket();
  registerUDPSend(ip, msgType);

  return true;
  #else
  I.UDP_OUTGOING_ERRORS++;
  return false;
  #endif
}

void registerUDPMessage(IPAddress ip, const char* messageType) {
  I.UDP_LAST_INCOMINGMSG_FROM_IP = ip;
  if (messageType != 0)   snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), messageType);
  I.UDP_LAST_INCOMINGMSG_TIME = I.currentTime;
  I.UDP_RECEIVES++;
}

void registerUDPSend(IPAddress ip, const char* messageType) {
  I.UDP_LAST_OUTGOINGMSG_TO_IP = ip;
  snprintf(I.UDP_LAST_OUTGOINGMSG_TYPE, sizeof(I.UDP_LAST_OUTGOINGMSG_TYPE), messageType);
  I.UDP_LAST_OUTGOINGMSG_TIME = I.currentTime;  
  I.UDP_SENDS++;
}

void registerHTTPMessage(const char* messageType) {
  I.HTTP_LAST_INCOMINGMSG_TIME = I.currentTime;
  snprintf(I.HTTP_LAST_INCOMINGMSG_TYPE, sizeof(I.HTTP_LAST_INCOMINGMSG_TYPE), messageType);
  I.HTTP_LAST_INCOMINGMSG_FROM_IP = server.client().remoteIP();
  I.HTTP_RECEIVES++;
}

void registerHTTPSend(IPAddress ip, const char* messageType) {
  I.HTTP_LAST_OUTGOINGMSG_TO_IP = ip;
  I.HTTP_LAST_OUTGOINGMSG_TIME = I.currentTime;
  I.HTTP_SENDS++;
  snprintf(I.HTTP_LAST_OUTGOINGMSG_TYPE, sizeof(I.HTTP_LAST_OUTGOINGMSG_TYPE), messageType);
}
void delayWithNetwork(uint16_t delayTime, uint8_t maxChecks) {
//do not delay a highspeed device, such as a TFLuna, as it will lock out wifi
  for (uint8_t i=0; i<maxChecks; i++) {
    delay(delayTime);
    receiveUDPMessage();
    server.handleClient();
  }
}
#endif
