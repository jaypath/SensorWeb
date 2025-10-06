#include "server.hpp"
#include <ArduinoJson.h>


//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif

#ifdef _USE32

    WebServer server(80);
#endif


String WEBHTML;
byte CURRENTSENSOR_WEB = 1;
byte CURRENT_DEVICE_VIEWER = 0;  // Track current device in device viewer
extern STRUCT_PrefsH Prefs;

extern Devices_Sensors Sensors;


bool RegistrationCompleted = false;

#ifdef _USE8266
#include <ESP8266HTTPClient.h>
#endif

static String macToHexNoSep() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

 String generateAPSSID() {
  uint64_t mac64;
  mac64 = ESP.getEfuseMac();
  uint8_t mac[6];
  uint64ToMAC(mac64, mac);
  char ssid[20];
  snprintf(ssid, sizeof(ssid), "SensorNet-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(ssid);
}

void startSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP) {
   String ssid = generateAPSSID();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid.c_str(), "S3nsor.N3t!");
}

static String urlEncode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c=='-' || c=='_' || c=='.' || c=='~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

#if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
void initHVAC(void){
  for (int16_t si=0; si<Sensors.getNumSensors(); ++si) {
    SnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s) continue;
    if (s->snsType >=50 && s->snsType <=57) {    
      SendData(s);
      s->snsValue = 0;
    }
  }
}
#endif

bool WifiStatus(void) {
  if (WiFi.status() == WL_CONNECTED) {
    I.WiFiMode = WIFI_STA;
    if (Prefs.status <= 0) {
      Prefs.status = 1; // Set status to indicate WiFi is connected
      Prefs.HAVECREDENTIALS = true;
      Prefs.MYIP = IPToUint32(WiFi.localIP());
      Prefs.isUpToDate = false;
    }
    return true;
  }

    return false;
  
}
int16_t tryWifi(uint16_t delayms) {
  //return -1000 if no credentials, or connection attempts if success, or -1*number of attempts if failure
  int16_t retries = 0;
  int16_t i = 0;
  if (Prefs.HAVECREDENTIALS) {
    if (Prefs.WIFISSID[0] != 0 ) {
    SerialPrint("Trying WiFi connection to " + String((char*)Prefs.WIFISSID) + "...", true);
    } else {
      SerialPrint("Invalid WiFi credentials found", true);

      Prefs.HAVECREDENTIALS = false;
      Prefs.WIFIPWD[0] = 0;
      Prefs.KEYS.ESPNOW_KEY[0] = 0;
      BootSecure::setPrefs();
      return -1000;
    }
    WiFi.mode(WIFI_STA);
    I.WiFiMode = WIFI_STA;
    for (i = 1; i < 51; i++) {
      if (!WifiStatus()) {        
        WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
        delay(delayms);
      } else {
        return i;
      }
    }

    if (!WifiStatus())       return -1*i;
    else return i;
  } else {
    return -1000; //no credentials
  }

}

int16_t connectWiFi() {
  
  int16_t retries = tryWifi(250);
  if (retries == -1000) {
    SerialPrint("No credentials, starting AP Station Mode",true);
    APStation_Mode();
    return -1000;
  }

    return retries;
}

void APStation_Mode() {
  I.WiFiMode = WIFI_AP_STA;
  
  //wifi ID and pwd will be set in startSoftAP
  String wifiPWD;
  String wifiID;
  IPAddress apIP;
  

  SerialPrint("Init AP Station Mode... Please wait... ",false);

  #ifdef _USE32
  SerialPrint("ESP32 detected...",true);
  #endif
// Initialize watchdog timer for ESP32
  // Add delay to ensure system is ready
  delay(500);
  
  startSoftAP(&wifiID,&wifiPWD,&apIP);

  delay(500);


  SerialPrint("AP Station ID started.",false);
  
  // Add delay before starting server
  delay(500);

  //add server routes temporarily
  server.on("/", handleRoot);
  
  // Add a catch-all POST handler for debugging
  server.onNotFound([](){
    SerialPrint("404 - Route not found: " + server.uri() + " Method: " + String(server.method()), true);
    server.send(404, "text/plain", "Not Found");
  });
  
  server.on("/WiFiConfig", HTTP_GET, handleWiFiConfig);
  server.on("/WiFiConfig", HTTP_POST, handleWiFiConfig_POST);
  server.on("/WiFiConfig_RESET", handleWiFiConfig_RESET);
  server.on("/Setup", HTTP_GET, handleSetup);
  server.on("/Setup", HTTP_POST, handleSetup_POST);
  server.on("/TimezoneSetup", HTTP_GET, handleTimezoneSetup);
  server.on("/TimezoneSetup", HTTP_POST, handleTimezoneSetup_POST);
  
  SerialPrint("Server routes registered:", true);
  SerialPrint("- GET /WiFiConfig -> handleWiFiConfig", true);
  SerialPrint("- POST /WiFiConfig -> handleWiFiConfig_POST", true);
  SerialPrint("- GET /Setup -> handleSetup", true);
  SerialPrint("- POST /Setup -> handleSetup_POST", true);
  SerialPrint("- GET /DEVICEVIEWER -> handleDeviceViewer", true);
  SerialPrint("- GET /DEVICEVIEWER_NEXT -> handleDeviceViewerNext", true);
  SerialPrint("- GET /DEVICEVIEWER_PREV -> handleDeviceViewerPrev", true);
  SerialPrint("- GET /DEVICEVIEWER_PING -> handleDeviceViewerPing", true);
  SerialPrint("- GET /DEVICEVIEWER_DELETE -> handleDeviceViewerDelete", true);
  SerialPrint("- GET /FORCEBROADCAST -> handleForceBroadcast", true);
  SerialPrint("- GET /STATUS -> handleStatus", true);

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
    tft.printf("Setup Required. Please join\n\nWiFi: %s\npwd: %s\n\nand go to website:\n\n%s\n\n", 
               wifiID.c_str(), wifiPWD.c_str(), apIP.toString().c_str());
    #endif

    uint32_t m=millis();
    uint32_t startTime = m;
    const uint32_t timeoutMs = 600000; // 10 minute timeout
    uint32_t last60Seconds = m;
    RegistrationCompleted = false;

    do {
      //reset the watchdog timer
      esp_task_wdt_reset();
      m=millis();
      //draw the timeout left at the botton right of the screen
      //wait for every 5 seconds
      if (m - last60Seconds > 5000) {
        last60Seconds = m;

        #ifdef _USETFT
        //clear the bottom left of the screen
        tft.fillRect(0, tft.height()-100, tft.width(), 100, TFT_BLACK);
        tft.setCursor(0, tft.height()-90);
        tft.setTextColor(TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.printf("Seconds before reboot: %ds", ((timeoutMs - (m - startTime))/1000));
        #else
        SerialPrint("Seconds before reboot: " + String((timeoutMs - (m - startTime))/1000),true);
        #endif
      }

      server.handleClient(); //check if user has set up WiFi
            
      // Check for timeout
      if ( m - startTime > timeoutMs) {
        #ifdef _USETFT
        tft.println("Timeout waiting for credentials. Rebooting...");
        #endif

        //if credentials are stored, but havecredentials was false, then try rebooting with those credentials
        if (Prefs.WIFISSID[0] != 0 && Prefs.WIFIPWD[0] != 0) {
          Prefs.HAVECREDENTIALS = true;
          BootSecure::setPrefs();
        }
        #ifdef _USETFT
        tft.println("Rebooting due to timeout...");
        #endif
        SerialPrint("Rebooting due to timeout...",true);
        delay(5000);
        controlledReboot("WiFi credentials timeout", RESET_WIFI, true);
        break;
      }

    } while (Prefs.HAVECREDENTIALS == false);
    
    // Only reboot if we have credentials but registration is not completed
    if ( RegistrationCompleted == false) {
      // User has submitted WiFi credentials but hasn't completed timezone setup
      // Continue running the server to allow timezone configuration
      #ifdef _USETFT
      tft.println("WiFi credentials saved. Please configure timezone settings.");
      tft.println("Server will continue running for timezone setup...");
    #else
    SerialPrint("WiFi credentials saved. Please configure timezone settings.",false);
    SerialPrint("Server will continue running for timezone setup...",true);
      #endif
      
      // Keep the server running indefinitely until timezone setup is complete
      while (RegistrationCompleted == false) {
        server.handleClient();
        delay(50);
      }
    }

  SerialPrint("Timezone configuration saved. Rebooting...", true);
    
    #ifdef _USETFT
    tft.println("Rebooting with updated credentials...");
  #else
  SerialPrint("Rebooting with updated credentials...",true);
    #endif
  controlledReboot("WIFI CREDENTIALS RESET",RESET_NEWWIFI,true);
   
}

bool Server_Message(String URL, String* payload, int* httpCode) {
  WiFiClient wfclient;
  HTTPClient http;


  if (Prefs.status >0 ){
     http.useHTTP10(true);
     http.begin(wfclient,URL.c_str());
     *httpCode = http.GET();
     *payload = http.getString();
     http.end();
     return true;
  } 

  return false;
}


bool SendData(struct SnsType *S) {

  if (bitRead(S->Flags,1) == 0) return false; //not monitored
  if (!(S->timeLogged ==0 || S->timeLogged>I.currentTime || S->timeLogged + S->SendingInt < I.currentTime || bitRead(S->Flags,6) /* isflagged changed since last read*/ || I.currentTime - S->timeLogged >60*60*24)) return false; //not time

  S->timeLogged = I.currentTime;
bool isGood = false;

  if(Prefs.status>0){
    // Use static buffers to reduce memory fragmentation
    static char jsonBuffer[512];
    static char urlBuffer[64];
    static char bodyBuffer[768];
    
    // Build JSON more efficiently using snprintf
    snprintf(jsonBuffer, sizeof(jsonBuffer), 
      "{\"mac\":\"%s\",\"ip\":\"%s\",\"sensor\":[{\"type\":%d,\"id\":%d,\"name\":\"%s\",\"value\":%.6f,\"timeRead\":%u,\"timeLogged\":%u,\"sendingInt\":%u,\"flags\":%d}]}",
      macToHexNoSep().c_str(),
      WiFi.localIP().toString().c_str(),
      S->snsType,
      S->snsID,
      S->snsName,
      S->snsValue,
      S->timeRead,
      S->timeLogged,
      S->SendingInt,
      S->Flags
    );

    // iterate all known servers from Sensors (devType >=100)
    for (int16_t i=0; i<NUMDEVICES && i<Sensors.getNumDevices(); ++i) {
      DevType* d = Sensors.getDeviceByDevIndex(i);
      if (!d || !d->IsSet || d->devType < 100) continue;
      
      IPAddress ip(d->IP);
      snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", ip.toString().c_str());

      SerialPrint("SENDDATA: POST to " + String(urlBuffer) , true);

      // Create HTTP client with proper cleanup
      WiFiClient wfclient;
      HTTPClient http;
      
      // Set timeout to prevent hanging
      http.setTimeout(5000); // 5 second timeout
      http.useHTTP10(true);

      if (http.begin(wfclient, urlBuffer)) {
      http.addHeader("Content-Type","application/x-www-form-urlencoded");
        
        // Build body efficiently
        String encodedJson = urlEncode(jsonBuffer);
        snprintf(bodyBuffer, sizeof(bodyBuffer), "JSON=%s", encodedJson.c_str());
        
        int httpCode = http.POST(bodyBuffer);
      String payload = http.getString();
        
        // Always end the connection
      http.end();
        
        if (httpCode == 200) { 
          isGood = true; 
        }
        else SerialPrint("SENDDATA: POST to " + String(urlBuffer) + " failed with code " + String(httpCode), true);

      } else {
        SerialPrint("Failed to begin HTTP connection", true);
        http.end(); // Ensure cleanup even on failure
      }
      
      
      // Small delay to prevent overwhelming servers
      delay(10);
    }
  }

  if (isGood) bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I wrote the value so set bit 6 (change in flag) to zero
  else I.makeBroadcast = true; //broadcast my presence if I failed to send the data
  return isGood;
}

void handleWiFiConfig_RESET() {
  I.lastServerStatusUpdate = I.currentTime;
  
  // Clear WiFi credentials and LMK key
  memset(Prefs.WIFISSID, 0, sizeof(Prefs.WIFISSID));
  memset(Prefs.WIFIPWD, 0, sizeof(Prefs.WIFIPWD));
  memset(Prefs.KEYS.ESPNOW_KEY, 0, sizeof(Prefs.KEYS.ESPNOW_KEY));
  Prefs.HAVECREDENTIALS = false;
  Prefs.isUpToDate = false;
  
  // Disconnect from current WiFi
  WiFi.disconnect();
  
  // Redirect back to WiFi config page
  server.sendHeader("Location", "/WiFiConfig");
  server.send(302, "text/plain", "WiFi configuration and LMK key reset. Please reconfigure.");
}

void handleUpdateSensorParams() {
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;
  
  String stateSensornum = server.arg("SensorNum");
  int16_t j = stateSensornum.toInt();

  for (uint8_t i = 0; i < server.args(); i++) {
    SnsType* s = Sensors.getSensorBySnsIndex(j);
    if (!s || s->IsSet == false) continue;

    int16_t prefsIndex = Sensors.getPrefsIndex(s->snsType, s->snsID);

    if (server.argName(i)=="SensorName") snprintf(s->snsName,31,"%s", server.arg(i).c_str());

    if (server.argName(i)=="Monitored") {
      if (server.arg(i) == "0") bitWrite(s->Flags,1,0);
      else bitWrite(s->Flags,1,1);
    }

    if (server.argName(i)=="Critical") {
      if (server.arg(i) == "0") bitWrite(s->Flags,7,0);
      else bitWrite(s->Flags,7,1);
    }

    if (server.argName(i)=="Outside") {
      if (server.arg(i)=="0") bitWrite(s->Flags,2,0);
      else bitWrite(s->Flags,2,1);
    }

    if (server.argName(i)=="UpperLim") {
      double val = server.arg(i).toDouble();
      Prefs.SNS_LIMIT_MAX[prefsIndex] = val;
      Prefs.isUpToDate = false;
    }

    if (server.argName(i)=="LowerLim") {
      double val = server.arg(i).toDouble();
      Prefs.SNS_LIMIT_MIN[prefsIndex] = val;
      Prefs.isUpToDate = false;
    }

    if (server.argName(i)=="SendInt") {
      uint16_t val = server.arg(i).toInt();
      Prefs.SNS_INTERVAL_SEND[prefsIndex] = val;
      Prefs.isUpToDate = false;
    }
    if (server.argName(i)=="PollInt") {
      uint16_t val = server.arg(i).toInt();
      Prefs.SNS_INTERVAL_POLL[prefsIndex] = val;
      Prefs.isUpToDate = false;
    }

  }



  #ifdef _USE32
  // Persist updates immediately if any changes were made
  if (!Prefs.isUpToDate) {
    BootSecure::setPrefs();
  }
  #endif

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page
}

void handleNEXT() {
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;

  CURRENTSENSOR_WEB++;
  if (CURRENTSENSOR_WEB>SENSORNUM) CURRENTSENSOR_WEB = 1;


  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleLAST() {
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;

  if (CURRENTSENSOR_WEB==1) CURRENTSENSOR_WEB = SENSORNUM;
  else CURRENTSENSOR_WEB--;
   

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleUpdateSensorRead() {
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;
  
  int16_t j = server.arg("SensorNum").toInt();

  SerialPrint("j: " + (String) j,true);
  SnsType* s = Sensors.getSensorBySnsIndex(j);
  if (s && s->IsSet) {
    ReadData(s, true);
    SerialPrint("ReadData(s) done",false);
    SerialPrint(" new value: " + (String) s->snsValue,true);
    SendData(s);
  }

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page
}

void handleUpdateAllSensorReads() {
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;
  
  String WEBHTML = "";

WEBHTML += "Current time ";
WEBHTML += (String) now();
WEBHTML += " = ";
WEBHTML += (String) dateify() + "\n";


for (int16_t k=0; k<Sensors.getNumSensors(); k++) {
  SnsType* s = Sensors.getSensorBySnsIndex(k);
  if (!s || s->IsSet == false) continue;
  ReadData(s, true);
  WEBHTML +=  "Sensor " + (String) s->snsName + " data sent to at least 1 server: " + SendData(s) + "\n";
}
  server.send(200, "text/plain", "Status...\n" + WEBHTML);   // Send HTTP status 200 (Ok) and send some text to the browser/client

}


void handleSetThreshold() {
  // Updated to use HTTP POST and new Devices_Sensors framework with MAC ID instead of ARDID
  
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;
  
  String WEBHTML = "";
  
  // Parse POST parameters - only snsType and snsID are required
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Error: Missing required parameters (snsType, snsID)");
    return;
  }
  
  // Use this device's MAC address (since it received the message)
  uint64_t deviceMAC = ESP.getEfuseMac();
  
  uint8_t snsType = server.arg("snsType").toInt();
  uint8_t snsID = server.arg("snsID").toInt();
  
  // Find the sensor using the new framework
  int16_t sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
  
  if (sensorIndex < 0) {
    WEBHTML = "Error: Sensor not found for Type: " + String(snsType) + ", ID: " + String(snsID);
    server.send(404, "text/plain", WEBHTML);
    return;
  }
  
  // Check if sensor index is valid
  if (sensorIndex >= SENSORNUM) {
    server.send(400, "text/plain", "Error: Sensor index out of range");
    return;
  }
  
  // Get the sensor and check if it's set
  SnsType* s = Sensors.getSensorBySnsIndex(sensorIndex);
  if (!s || !s->IsSet) {
    WEBHTML = "Error: Sensor not initialized for Type: " + String(snsType) + ", ID: " + String(snsID);
    server.send(400, "text/plain", WEBHTML);
    return;
  }
  
  // Get optional threshold and interval parameters - default to current values from Prefs
  // Note: limits and pollingInt are stored in Prefs, sendingInt is in both Prefs and sensor
  double limitUpper = Prefs.SNS_LIMIT_MAX[sensorIndex];
  double limitLower = Prefs.SNS_LIMIT_MIN[sensorIndex];
  uint16_t pollingInt = Prefs.SNS_INTERVAL_POLL[sensorIndex];
  uint16_t sendingInt = s->SendingInt;  // Get from sensor, not Prefs
  
  // Only update if new values are provided
  if (server.hasArg("UPPER")) {
    limitUpper = server.arg("UPPER").toDouble();
  }
  if (server.hasArg("LOWER")) {
    limitLower = server.arg("LOWER").toDouble();
  }
  if (server.hasArg("POLLINGINT")) {
    pollingInt = server.arg("POLLINGINT").toInt();
  }
  if (server.hasArg("SENDINGINT")) {
    sendingInt = server.arg("SENDINGINT").toInt();
  }
  
  // Update both Prefs and sensor values
  Prefs.SNS_LIMIT_MIN[sensorIndex] = limitLower;
  Prefs.SNS_LIMIT_MAX[sensorIndex] = limitUpper;
  Prefs.SNS_INTERVAL_POLL[sensorIndex] = pollingInt;
  Prefs.SNS_INTERVAL_SEND[sensorIndex] = sendingInt;
  
  // Update the sensor's SendingInt in Sensors
  s->SendingInt = sendingInt;
  
  Prefs.isUpToDate = false;
  
  // Recheck sensor flags with new thresholds
  checkSensorValFlag(s);
  
  // Build response
  WEBHTML += (String) dateify() + "\n";
  WEBHTML += "MAC:" + String((unsigned long)(deviceMAC >> 32), HEX) + String((unsigned long)(deviceMAC & 0xFFFFFFFF), HEX);
  WEBHTML += "; snsType:" + String(s->snsType);
  WEBHTML += "; snsID:" + String(s->snsID);
  WEBHTML += "; SnsName:" + String(s->snsName);
  WEBHTML += "; LastRead:" + String(s->timeRead);
  WEBHTML += "; LastSend:" + String(s->timeLogged);
  WEBHTML += "; snsVal:" + String(s->snsValue);
  WEBHTML += "; UpperLim:" + String(Prefs.SNS_LIMIT_MAX[sensorIndex]);
  WEBHTML += "; LowerLim:" + String(Prefs.SNS_LIMIT_MIN[sensorIndex]);
  WEBHTML += "; PollingInt:" + String(Prefs.SNS_INTERVAL_POLL[sensorIndex]);
  WEBHTML += "; SendingInt:" + String(Prefs.SNS_INTERVAL_SEND[sensorIndex]);
  WEBHTML += "; Flag:" + String(bitRead(s->Flags, 0));
  WEBHTML += "; Monitored:" + String(bitRead(s->Flags, 1)) + "\n";
  
  server.send(200, "text/plain", WEBHTML);
}

void handleReboot() {
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;
  
  WEBHTML = "Rebooting in 10 sec";
  serverTextClose(200, false);  //This returns to the main page
  delay(10000);
  ESP.restart();
}

void handleRoot() {


  if (Prefs.HAVECREDENTIALS==false) {
    SerialPrint("Prefs.HAVECREDENTIALS==false, redirecting to WiFiConfig");
    //redirect to WiFi config page
    server.sendHeader("Location", "/WiFiConfig");
    serverTextClose(302,false);
    return;
  }
  
  // Update server status timestamp
  I.lastServerStatusUpdate = I.currentTime;
  
  // Use device MAC address instead of ARDID
  uint64_t deviceMAC = ESP.getEfuseMac();

String WEBHTML = "<!DOCTYPE html><html>\n";

#if defined(_WEBCHART) || defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";
#endif



WEBHTML +=  "<head><title>" + (String) MYNAME + " Page</title>\n";
WEBHTML += (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}\n";
WEBHTML += (String) "input[type='text'] { font-family: arial, sans-serif; font-size: 10px; }\n";
WEBHTML += (String) "body {  font-family: arial, sans-serif; }\n";
WEBHTML += "</style></head>\n";




WEBHTML += "<body>";

// Check for broadcast status messages
if (server.hasArg("broadcast")) {
    String broadcastStatus = server.arg("broadcast");
    if (broadcastStatus == "success") {
        WEBHTML += "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">";
        WEBHTML += "<strong>Success:</strong> Broadcast message sent successfully.";
        WEBHTML += "</div>";
    } else if (broadcastStatus == "failed") {
        WEBHTML += "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
        WEBHTML += "<strong>Error:</strong> Failed to send broadcast message.";
        WEBHTML += "</div>";
    }
}

WEBHTML +=  "<h2>Arduino: " + (String) MYNAME + "<br>\nIP:" + WiFi.localIP().toString() + "<br>\nMAC:" + String((unsigned long)(deviceMAC >> 32), HEX) + String((unsigned long)(deviceMAC & 0xFFFFFFFF), HEX) + "<br></h2>\n";
WEBHTML += "<p>Started on: " + (String) dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn") + "<br>\n";
WEBHTML += "Current time: " + (String) now() + " = " +  (String) dateify(now(),"mm/dd/yyyy hh:nn:ss") + "<br>\n";

#ifdef _USE32
  WEBHTML += "Free Stack: " + (String) uxTaskGetStackHighWaterMark(NULL) + "<br>\n";
#endif
#ifdef _USE8266
  WEBHTML += "Free Stack: " + (String) ESP.getFreeContStack() + "<br>\n";
#endif
WEBHTML += "Num Sensors: " + (String) Sensors.getNumSensors() + "<br>\n";
WEBHTML += "<a href=\"/UPDATEALLSENSORREADS\">Update all sensors</a><br>\n";
WEBHTML += "<a href=\"/FORCEBROADCAST\" style=\"color: #9C27B0; text-decoration: none; padding: 5px 10px; border: 1px solid #9C27B0; border-radius: 3px;\">Force Broadcast</a><br>\n";
WEBHTML += "<a href=\"/STATUS\" style=\"color: #FF9800; text-decoration: none; padding: 5px 10px; border: 1px solid #FF9800; border-radius: 3px;\">System Status</a><br>\n";
WEBHTML += "<a href=\"/Setup\" style=\"color: #2196F3; text-decoration: none; padding: 5px 10px; border: 1px solid #2196F3; border-radius: 3px;\">System Setup</a><br>\n";
WEBHTML += "</p>\n";
WEBHTML += "<br>-----------------------<br>\n";



  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM;j++)  used[j] = 255;
  for (int16_t si = 0; si < Sensors.getNumSensors(); si++)  {

    if (Sensors.isMySensor(si)<=0) continue;
    SnsType* s = Sensors.getSensorBySnsIndex(si);
    WEBHTML += "<form action=\"/UPDATESENSORPARAMS\" method=\"GET\" id=\"frm_SNS" + (String) si + "\"><input form=\"frm_SNS"+ (String) si + "\"  id=\"SNS" + (String) si + "\" type=\"hidden\" name=\"SensorNum\" value=\"" + (String) si + "\"></form>\n";
  }
      
  byte usedINDEX = 0;  
  
  char tempchar[9] = "";

  WEBHTML += "<p>";
  WEBHTML = WEBHTML + "<table id=\"Logs\" style=\"width:70%\">";      
  WEBHTML = WEBHTML + "<tr><th>SnsType</th><th>SnsID</th><th>Value</th><th><button onclick=\"sortTable(3)\">UpperLim</button></th><th><button onclick=\"sortTable(4)\">LowerLim</button></th>";
  WEBHTML = WEBHTML + "<th><button onclick=\"sortTable(5)\">PollInt</button></th><th><button onclick=\"sortTable(6)\">SendInt</button></th><th><button onclick=\"sortTable(7)\">Flag</button></th>";
  WEBHTML = WEBHTML + "<th><button onclick=\"sortTable(8)\">LastLog</button></th><th><button onclick=\"sortTable(9)\">LastSend</button></th><th>IsMonit</th><th>IsCritical</th><th>IsOut</th><th>Flags</th><th>SnsName</th>";
  WEBHTML = WEBHTML + "<th>Submit</th><th>Recheck</th></tr>\n"; 
  for (int16_t si=0; si<Sensors.getNumSensors(); si++)  {
    if (Sensors.isMySensor(si)<=0) continue;
    SnsType* s = Sensors.getSensorBySnsIndex(si);

    if (inIndex(si,used,SENSORNUM) == false)  {
      used[usedINDEX++] = si;
      WEBHTML = WEBHTML + "<tr>";
      WEBHTML = WEBHTML + "<td>" + (String) s->snsType + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) s->snsID + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) s->snsValue + "</td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"UpperLim\" value=\"" + String(Prefs.SNS_LIMIT_MAX[si],DEC) + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"LowerLim\" value=\"" + String(Prefs.SNS_LIMIT_MIN[si],DEC) + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"PollInt\" value=\"" + String(Prefs.SNS_INTERVAL_POLL[si],DEC) + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"SendInt\" value=\"" + String(Prefs.SNS_INTERVAL_SEND[si],DEC) + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td>" + (String) bitRead(s->Flags,0) + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) dateify(s->timeRead) + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) dateify(s->timeLogged) + "</td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"Monitored\" value=\"" + String(bitRead(s->Flags,1),DEC) + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"Critical\" value=\"" + String(bitRead(s->Flags,7),DEC) + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td>" + (String) bitRead(s->Flags,2) + "</td>";
      Byte2Bin(s->Flags,tempchar,true);
      WEBHTML = WEBHTML + "<td>" + (String) tempchar + "</td>";
      WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"SensorName\" value=\"" +  (String) s->snsName + "\" form = \"frm_SNS" + (String) si + "\"></td>";
      WEBHTML = WEBHTML + "<td><button type=\"submit\" form = \"frm_SNS" + (String) si + "\" name=\"Sub" + (String) si + "\">Submit</button></td>";
      WEBHTML += "<td><button type=\"submit\" formaction=\"/UPDATESENSORREAD\" form = \"frm_SNS" + (String) si + "\" name=\"Upd" + (String) si + "\">Update</button></td>";
      WEBHTML += "</tr>\n";

      
      for (int16_t sj=si+1; sj<Sensors.getNumSensors(); sj++) {
        if (Sensors.isMySensor(sj)<=0) continue;
        SnsType* s2 = Sensors.getSensorBySnsIndex(sj);
        if ( inIndex(sj,used,SENSORNUM) == false && s2->snsType==s->snsType) {
          used[usedINDEX++] = sj;
          WEBHTML = WEBHTML + "<tr>";
          WEBHTML = WEBHTML + "<td>" + (String) s2->snsType + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) s2->snsID + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) s2->snsValue + "</td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"UpperLim\" value=\"" + String(Prefs.SNS_LIMIT_MAX[sj],DEC) + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"LowerLim\" value=\"" + String(Prefs.SNS_LIMIT_MIN[sj],DEC) + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"PollInt\" value=\"" + String(Prefs.SNS_INTERVAL_POLL[sj],DEC) + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"SendInt\" value=\"" + String(Prefs.SNS_INTERVAL_SEND[sj],DEC) + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td>" + (String) bitRead(s2->Flags,0) + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) dateify(s2->timeRead) + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) dateify(s2->timeLogged) + "</td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"Monitored\" value=\"" + String(bitRead(s2->Flags,1),DEC) + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"Critical\" value=\"" + String(bitRead(s2->Flags,7),DEC) + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td>" + (String) bitRead(s2->Flags,2) + "</td>";
          Byte2Bin(s2->Flags,tempchar,true);
          WEBHTML = WEBHTML + "<td>" + (String) tempchar + "</td>";
          WEBHTML = WEBHTML + "<td><input type=\"text\"  name=\"SensorName\" value=\"" +  (String) s2->snsName + "\" form = \"frm_SNS" + (String) sj + "\"></td>";
          WEBHTML = WEBHTML + "<td><button type=\"submit\" form = \"frm_SNS" + (String) sj + "\" name=\"Sub" + (String) sj + "\">Submit</button></td>";
          WEBHTML += "<td><button type=\"submit\" formaction=\"/UPDATESENSORREAD\" form = \"frm_SNS" + (String) sj + "\" name=\"Upd" + (String) sj + "\">Update</button></td>";
          WEBHTML += "</tr>\n";
        }
      }
    }
  }
  
  WEBHTML += "</table>\n";   


  WEBHTML += "</p>\n";


#if defined(_WEBCHART) || defined(_CHECKHEAT) || defined(_CHECKAIRCON)
  //add charts if indicated
  WEBHTML += "<br>-----------------------<br>\n";
  #if defined(_WEBCHART)
    for (byte j=0;j<_WEBCHART;j++)    WEBHTML += "<div id=\"myChart" + (String) j + "\" style=\"width:100%; max-width:800px; height:500px;\"></div>\n";
  #endif
  
  #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
    WEBHTML += "<div id=\"myChart_HVAC\" style=\"width:100%; max-width:1500px; height:500px;\"></div>\n";
  #endif
  WEBHTML += "<br>-----------------------<br>\n";
#endif


  WEBHTML =WEBHTML  + "<script>\n";

  #if defined(_WEBCHART) || defined(_CHECKHEAT) || defined(_CHECKAIRCON)
    WEBHTML =WEBHTML  + "google.charts.load('current',{packages:['corechart']});\n";
    WEBHTML =WEBHTML  + "google.charts.setOnLoadCallback(drawChart);\n";


    #ifdef _WEBCHART
      
      WEBHTML += "function drawChart() {\n";

      for (byte j=0;j<_WEBCHART;j++) {
        WEBHTML += "const data" + (String) j + " = google.visualization.arrayToDataTable([\n";
        WEBHTML += "['t','val'],\n";

        for (int jj = _NUMWEBCHARTPNTS-1;jj>=0;jj--) {
          WEBHTML += "[" + (String) ((int) ((uint32_t) (SensorCharts[j].lastRead - SensorCharts[j].interval*jj)-now())/60) + "," + (String) SensorCharts[j].values[jj] + "]";
          if (jj>0) WEBHTML += ",";
          WEBHTML += "\n";
        }
        WEBHTML += "]);\n\n";

      
      // Set Options
        WEBHTML += "const options" + (String) j + " = {\n";
        WEBHTML += "hAxis: {title: 'min from now'}, \n";
        {
          int idx = SensorCharts[j].snsNum;
          SnsType* s = Sensors.getSensorBySnsIndex(idx);
          WEBHTML += "vAxis: {title: '" + (String) (s ? s->snsName : "") + "'},\n";
        }
        WEBHTML += "legend: 'none'\n};\n";

        WEBHTML += "const chart" + (String) j + " = new google.visualization.LineChart(document.getElementById('myChart" + (String) j + "'));\n";
        WEBHTML += "chart" + (String) j + ".draw(data" + (String) j + ", options" + (String) j + ");\n"; 
      }
        WEBHTML += "}\n";
    #endif

    #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
      
      WEBHTML += "function drawChart() {\n";

      WEBHTML += "const data_HVAC = google.visualization.arrayToDataTable([\n";
      //header
      WEBHTML += "['HoursAgo',";
      
      for (byte j=0;j<HVACSNSNUM;j++) {
        SnsType* sh = Sensors.getSensorBySnsIndex(j);
        WEBHTML += "'" + (String) (sh ? sh->snsName : "") + "'";
        if (j<HVACSNSNUM-1) WEBHTML += ",";
        else WEBHTML += "],\n";
      }
      //data points
      for (int jj = 0;jj<_HVACHXPNTS;jj++) {
        WEBHTML += "[" + (String) (jj+1) + ",";        
        for (byte j=0;j<HVACSNSNUM;j++) {
          WEBHTML += (String)  HVACHX[j].values[jj];
          if (j<HVACSNSNUM-1) WEBHTML += ",";
          else WEBHTML += "]";
        }
        if (jj<_HVACHXPNTS-1) WEBHTML += ",\n";
        else WEBHTML += "\n";
      }
      WEBHTML += "]);\n\n";

      
      // Set Options
      WEBHTML += "const options_HVAC = {\n";
      WEBHTML += "hAxis: {title: 'Time'}, \n";
      WEBHTML += "vAxis: {title: 'MinutesOn'},\n";
      WEBHTML += "legend: { position: 'bottom' }\n};\n";

      WEBHTML += "const chart_HVAC = new google.visualization.LineChart(document.getElementById('myChart_HVAC'));\n";
      WEBHTML += "chart_HVAC.draw(data_HVAC, options_HVAC);\n"; 
      WEBHTML += "}\n";
    #endif
  #endif

  WEBHTML += "function sortTable(col) {\nvar table, rows, switching, i, x, y, shouldSwitch;\ntable = document.getElementById(\"Logs\");\nswitching = true;\nwhile (switching) {\nswitching = false;\nrows = table.rows;\nfor (i = 1; i < (rows.length - 1); i++) {\nshouldSwitch = false;\nx = rows[i].getElementsByTagName(\"TD\")[col];\ny = rows[i + 1].getElementsByTagName(\"TD\")[col];\nif (x.innerHTML.toLowerCase() > y.innerHTML.toLowerCase()) {\nshouldSwitch = true;\nbreak;\n}\n}\nif (shouldSwitch) {\nrows[i].parentNode.insertBefore(rows[i + 1], rows[i]);\nswitching = true;\n}\n}\n}\n";
  WEBHTML += "</script>\n ";

  // Navigation links to other pages
  WEBHTML += "<br><br><div style=\"text-align: center; padding: 20px; border-top: 2px solid #ddd; margin-top: 20px;\">";
  WEBHTML += "<h3>Navigation</h3>";
  WEBHTML += "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML += "<a href=\"/TimezoneSetup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">Timezone Setup</a> ";
  WEBHTML += "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML += "<br><br>";
  WEBHTML += "<a href=\"/UPDATEALLSENSORREADS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Update All Sensors</a> ";
  WEBHTML += "<a href=\"/ResetWiFi\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF5722; color: white; text-decoration: none; border-radius: 4px;\">Reset WiFi</a> ";
  WEBHTML += "<a href=\"/REBOOT\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px;\">Reboot Device</a> ";
  WEBHTML += "</div>";

  WEBHTML += "</body>\n</html>\n";


    //IF USING PROGMEM: use send_p   !!
  server.send(200, "text/html", WEBHTML);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}



void handleCONFIG() {
  I.lastServerStatusUpdate = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  
  
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " System Configuration</h2>";
  WEBHTML = WEBHTML + "<p>This page is used to configure editable system parameters.</p>";
  
  // Start the form and grid container
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/CONFIG\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Editable fields from STRUCT_CORE I in specified order
  // Note: Timezone settings are now managed through Prefs and the /TimezoneSetup page
  
  
  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  

  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/TimezoneSetup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">Timezone Setup</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
 
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}




void handleCONFIG_POST() {
  I.lastServerStatusUpdate = I.currentTime;
  
  // Process form submissions and update editable fields
  // Timezone settings are now managed through Prefs and the /TimezoneSetup page
  
  
  

  // Save the updated configuration to persistent storage
  I.isUpToDate = false;
  
  // Redirect back to the configuration page
  server.sendHeader("Location", "/CONFIG");
  server.send(302, "text/plain", "Configuration updated. Redirecting...");
}



void handleWiFiConfig() {
  I.lastServerStatusUpdate = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " WiFi Configuration</h2>";
  
  // Check for error messages
  if (server.hasArg("error")) {
    String error = server.arg("error");
    if (error == "connection_failed") {
      WEBHTML = WEBHTML + "<div style=\"background-color: #ffebee; color: #c62828; padding: 15px; margin: 10px 0; border: 1px solid #ef5350; border-radius: 4px;\">";
      WEBHTML = WEBHTML + "<strong>Connection Failed:</strong> WiFi credentials are incorrect. Please check your SSID and password and try again.";
      WEBHTML = WEBHTML + "</div>";
    }
  }
  
  // Display current WiFi status and configuration
  WEBHTML = WEBHTML + "<h3>Current WiFi Status</h3>";

  if (!Prefs.HAVECREDENTIALS) {
    WEBHTML = WEBHTML + "<p>Wifi credentials are not set, and are required.</p>";
    WEBHTML = WEBHTML + "<p>After connecting to WiFi, the system will automatically detect your timezone information.</p>";
  }

  WEBHTML = WEBHTML + "<p><strong>WiFi Status:</strong> " + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "</p>";
  if (WiFi.status() == WL_CONNECTED) {
    WEBHTML = WEBHTML + "<p><strong>Connected to:</strong> " + WiFi.SSID() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>Signal Strength:</strong> " + (String) WiFi.RSSI() + " dBm</p>";
  }
  
  // Add WiFi mode information
  String wifiModeStr = "Unknown";
  if (I.WiFiMode == WIFI_AP_STA) {
    wifiModeStr = "Access Point + Station (AP+STA)";
  } else if (I.WiFiMode == WIFI_STA) {
    wifiModeStr = "Station Mode Only";
  } else if (I.WiFiMode == WIFI_AP) {
    wifiModeStr = "Access Point Mode Only";
  } else if (I.WiFiMode == WIFI_OFF) {
    wifiModeStr = "WiFi Off";
  }
  WEBHTML = WEBHTML + "<p><strong>WiFi Mode:</strong> " + wifiModeStr + "</p>";
  
  WEBHTML = WEBHTML + "<p><strong>Stored SSID:</strong> " + String((char*)Prefs.WIFISSID) + "</p>";
  WEBHTML = WEBHTML + "<p><strong>Stored Security Key:</strong> " + String((char*)Prefs.KEYS.ESPNOW_KEY) + "</p>";

  addWiFiConfigForm();

  WEBHTML = WEBHTML + "<h3>WiFi Reset</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WiFiConfig_RESET\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Reset WiFi Configuration\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Configuration</a> ";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void addWiFiConfigForm() {
  // WiFi configuration form
  WEBHTML = WEBHTML + "<h3>Configure WiFi</h3>";
  WEBHTML = WEBHTML + "<form id=\"wifiForm\" method=\"POST\" action=\"/WiFiConfig\">";
  WEBHTML = WEBHTML + "<p><label for=\"ssid\">SSID:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"ssid\" name=\"ssid\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"password\">Password:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"password\" id=\"password\" name=\"password\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"lmk_key\">Local Security Key (up to 16 chars) - must be same for all devices:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"lmk_key\" name=\"lmk_key\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";

  // Disconnection notice
  WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; border: 1px solid #ffeaa7; color: #856404; padding: 15px; margin: 10px 0; border-radius: 4px;\">";
  WEBHTML = WEBHTML + "<strong>Important:</strong> After submitting WiFi credentials, there will be a brief disconnection as the device connects to your WiFi network. ";
  WEBHTML = WEBHTML + "Once submitted, a 'Check Timezone' button will appear to proceed to timezone configuration.";
  WEBHTML = WEBHTML + "</div>";

  // Status and action buttons
  WEBHTML = WEBHTML + "<div id=\"statusSection\" style=\"margin: 20px 0;\">";
  WEBHTML = WEBHTML + "<div id=\"statusMessage\" style=\"padding: 10px; margin: 10px 0; border-radius: 4px; display: none;\"></div>";
  WEBHTML = WEBHTML + "<input type=\"submit\" id=\"submitButton\" value=\"Connect WiFi and Configure\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "<a href=\"/TimezoneSetup\" id=\"timezoneButton\" style=\"display: none; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px; cursor: pointer; margin-left: 10px;\">Check Timezone</a>";
  WEBHTML = WEBHTML + "</div>";

  WEBHTML = WEBHTML + "</form>";

  // Add JavaScript for form submission handling
  WEBHTML = WEBHTML + R"===(<script>
  function showStatus(message, isError = false) {
    const statusDiv = document.getElementById('statusMessage');
    statusDiv.textContent = message;
    statusDiv.style.display = 'block';
    statusDiv.style.backgroundColor = isError ? '#ffebee' : '#e8f5e8';
    statusDiv.style.color = isError ? '#c62828' : '#2e7d32';
    statusDiv.style.border = isError ? '1px solid #f44336' : '1px solid #4CAF50';
  }

  function showTimezoneButton() {
    // Hide submit button and show timezone button
    document.getElementById('submitButton').style.display = 'none';
    document.getElementById('timezoneButton').style.display = 'inline-block';
    showStatus('WiFi credentials submitted successfully! Click "Check Timezone" to configure timezone settings.', false);
  }

  // Override form submission to handle the process
  document.getElementById('wifiForm').addEventListener('submit', function(e) {
    e.preventDefault();
    
    showStatus('Submitting WiFi credentials...', false);
    
    // Submit the form normally
    const formData = new FormData(this);
    
    fetch('/WiFiConfig', {
      method: 'POST',
      body: formData
    })
    .then(response => {
      if (response.ok) {
        // WiFi credentials submitted successfully
        showTimezoneButton();
      } else {
        showStatus('Failed to submit WiFi credentials. Please try again.', true);
      }
    })
    .catch(error => {
      // Network error - this might be expected during WiFi transition
      showStatus('WiFi credentials submitted. If WiFi connection succeeds, you can proceed to timezone configuration.', false);
      showTimezoneButton();
    });
  });
  </script>)===";
}

// Function to get timezone information from worldtimeapi.org
bool getTimezoneInfo(int32_t* utc_offset, bool* dst_enabled, uint8_t* dst_start_month, uint8_t* dst_start_day, uint8_t* dst_end_month, uint8_t* dst_end_day) {
  WiFiClient client;
  HTTPClient http;
  
  SerialPrint("Fetching timezone information from worldtimeapi.org...", true);
  
  if (http.begin(client, "http://worldtimeapi.org/api/ip")) {
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      SerialPrint("Timezone API response: " + payload, true);
      
      // Parse JSON response
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
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
        
        http.end();
        return true;
      } else {
        SerialPrint("JSON parsing failed: " + String(error.c_str()), true);
      }
    } else {
      SerialPrint("HTTP request failed with code: " + String(httpCode), true);
    }
    http.end();
  } else {
    SerialPrint("Failed to begin HTTP request", true);
  }
  
  return false;
}

void handleTimezoneSetup() {
  I.lastServerStatusUpdate = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>Timezone Configuration</h2>";
  
  // Get timezone information
  int32_t utc_offset = 0;
  bool dst_enabled = false;
  uint8_t dst_start_month = 3, dst_start_day = 9;
  uint8_t dst_end_month = 11, dst_end_day = 2;
  
  if (getTimezoneInfo(&utc_offset, &dst_enabled, &dst_start_month, &dst_start_day, &dst_end_month, &dst_end_day)) {
    WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; color: #2e7d32; padding: 15px; margin: 10px 0; border: 1px solid #4caf50; border-radius: 4px;\">";
    WEBHTML = WEBHTML + "<strong>Timezone detected successfully!</strong><br>";
    WEBHTML = WEBHTML + "Please review and confirm the timezone information below.";
    WEBHTML = WEBHTML + "</div>";
  } else {
    WEBHTML = WEBHTML + "<div style=\"background-color: #fff3e0; color: #ef6c00; padding: 15px; margin: 10px 0; border: 1px solid #ff9800; border-radius: 4px;\">";
    WEBHTML = WEBHTML + "<strong>Warning:</strong> Could not automatically detect timezone. Using default values. Please verify and adjust as needed.";
    WEBHTML = WEBHTML + "</div>";
    // Set default values (Eastern Time)
    utc_offset = -5 * 3600; // -5 hours in seconds
    dst_enabled = true;
    dst_start_month = 3; dst_start_day = 9;
    dst_end_month = 11; dst_end_day = 2;
  }
  
  WEBHTML = WEBHTML + "<h3>Detected Timezone Information</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/TimezoneSetup\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // UTC Offset
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>UTC Offset (seconds):</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"utc_offset\" value=\"" + String(utc_offset) + "\" style=\"width: 100%; padding: 8px;\"></div>";
  SerialPrint("UTC Offset: " + String(utc_offset),true);
  
  // DST Enabled
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>Daylight Saving Time:</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><select name=\"dst_enabled\" style=\"width: 100%; padding: 8px;\">";
  WEBHTML = WEBHTML + "<option value=\"0\"" + (dst_enabled ? "" : " selected") + ">Disabled</option>";
  WEBHTML = WEBHTML + "<option value=\"1\"" + (dst_enabled ? " selected" : "") + ">Enabled</option>";
  WEBHTML = WEBHTML + "</select></div>";
  
  // DST Start Month
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>DST Start Month:</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"dst_start_month\" value=\"" + String(dst_start_month) + "\" min=\"1\" max=\"12\" style=\"width: 100%; padding: 8px;\"></div>";
  
  // DST Start Day
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>DST Start Day:</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"dst_start_day\" value=\"" + String(dst_start_day) + "\" min=\"1\" max=\"31\" style=\"width: 100%; padding: 8px;\"></div>";
  
  // DST End Month
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>DST End Month:</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"dst_end_month\" value=\"" + String(dst_end_month) + "\" min=\"1\" max=\"12\" style=\"width: 100%; padding: 8px;\"></div>";
  
  // DST End Day
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>DST End Day:</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"dst_end_day\" value=\"" + String(dst_end_day) + "\" min=\"1\" max=\"31\" style=\"width: 100%; padding: 8px;\"></div>";
  
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Save Configuration and Reboot\" style=\"padding: 15px 30px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<p><em>Note: The system will reboot after saving the configuration to apply the new timezone settings.</em></p>";
  
  WEBHTML = WEBHTML + "</body></html>";
  serverTextClose(200, true);
}

void handleTimezoneSetup_POST() {
  I.lastServerStatusUpdate = I.currentTime;
  
  #ifdef _USETFT
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(2);
  tft.printf("Saving timezone settings...\n");
  #else
  SerialPrint("Saving timezone settings...",true);
  #endif

  
  if (server.hasArg("utc_offset") && 
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
    
    
    // Save to Prefs
    Prefs.TimeZoneOffset = offset_seconds;
    Prefs.DST = dst_enabled ? 1 : 0;
    Prefs.DSTOffset = dst_enabled ? 3600 : 0;
    Prefs.DSTStartMonth = dst_start_month;
    Prefs.DSTStartDay = dst_start_day;
    Prefs.DSTEndMonth = dst_end_month;
    Prefs.DSTEndDay = dst_end_day;
    
    // Mark Prefs as needing update and save to NVS

    Prefs.HAVECREDENTIALS = true; //we saved credentials earlier
    Prefs.isUpToDate = false;
    if (!BootSecure::setPrefs()) {
      SerialPrint("Failed to save timezone settings to NVS", true);
      
      #ifdef _USETFT
      tft.printf("Failed to save timezone settings!\n");
      delay(3000);
      #endif
    } else {
      SerialPrint("Timezone settings saved successfully", true);
      #ifdef _USETFT
      tft.printf("Timezone settings saved!\n");
      tft.printf("Rebooting in 3 seconds...\n");
      delay(3000);
      #endif
      controlledReboot("Timezone settings saved successfully", RESET_TIME,true);
      SerialPrint("Rebooting failed...", true);
      RegistrationCompleted = true;

    }
    
    // Redirect to main page
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Timezone settings saved. Rebooting...");
    } else {

    //report what is missing
    if (!server.hasArg("utc_offset")) {
      SerialPrint("Missing utc_offset", true);
    }
    if (!server.hasArg("dst_enabled")) {
      SerialPrint("Missing dst_enabled", true);
    }
    if (!server.hasArg("dst_start_month")) {
      SerialPrint("Missing dst_start_month", true);
    }
    if (!server.hasArg("dst_start_day")) {
      SerialPrint("Missing dst_start_day", true);
    }
    if (!server.hasArg("dst_end_month")) {
      SerialPrint("Missing dst_end_month", true);
    }
    if (!server.hasArg("dst_end_day")) {
      SerialPrint("Missing dst_end_day", true);
    }
    SerialPrint("Invalid timezone data. Please try again.", true);
    server.sendHeader("Location", "/TimezoneSetup");
    server.send(302, "text/plain", "Invalid timezone data. Please try again.");
  }
  
}


void handleSetup() {
  I.lastServerStatusUpdate = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " System Configuration</h2>";
  WEBHTML = WEBHTML + "<p>This page is used to configure editable system parameters.</p>";
  
  // Start the form and grid container
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/Setup\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Device Name Configuration
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Device Name</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" name=\"deviceName\" value=\"" + String(Prefs.DEVICENAME) + "\" maxlength=\"32\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  #ifdef _USETFT
  // Display cycle configuration (only for TFT devices)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Display Cycle Configuration</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Header Cycle (minutes)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleHeaderMinutes\" value=\"" + (String) I.cycleHeaderMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Weather Cycle (minutes)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleWeatherMinutes\" value=\"" + (String) I.cycleWeatherMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Current Condition Cycle (minutes)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleCurrentConditionMinutes\" value=\"" + (String) I.cycleCurrentConditionMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Future Conditions Cycle (minutes)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleFutureConditionsMinutes\" value=\"" + (String) I.cycleFutureConditionsMinutes + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Flag Display Cycle (seconds)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"cycleFlagSeconds\" value=\"" + (String) I.cycleFlagSeconds + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Hourly Weather Interval (hours)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"IntervalHourlyWeather\" value=\"" + (String) I.IntervalHourlyWeather + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  #endif
  
  #ifndef _ISPERIPHERAL
  // showTheseFlags - individual checkboxes for each bit
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Display Flags (showTheseFlags)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px;\">";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit0\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 0) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 0 (Flagged only)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit1\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 1) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 1 (Expired only)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit2\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 2) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 2 (Soil alarms)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit3\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 3) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 3 (Leak alarms)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit4\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 4) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 4 (Temperature)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit5\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 5) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 5 (Humidity)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit6\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 6) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 6 (Pressure)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit7\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 7) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 7 (Battery)</label>";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit8\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 8) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 8 (HVAC)</label>";
  
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<div style=\"margin-top: 8px; font-size: 12px; color: #666;\">Current value: " + String(I.showTheseFlags) + " (0x" + String(I.showTheseFlags, HEX) + ")</div>";
  WEBHTML = WEBHTML + "</div>";
  #endif
  
  // ESPNow/LAN Message Status Section
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">LAN Message Status (ESPNow)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\"></div>";
  
  // ESPNow Statistics
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Messages Received Today</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(I.ESPNOW_RECEIVES) + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Messages Sent Today</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(I.ESPNOW_SENDS) + "</div>";
  
  // Last Incoming Message Info
  if (I.ESPNOW_LAST_INCOMINGMSG_TIME > 0) {
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Last Message Received</div>";
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
    WEBHTML = WEBHTML + "Time: " + String(I.ESPNOW_LAST_INCOMINGMSG_TIME) + "<br>";
    WEBHTML = WEBHTML + "From MAC: " + String((unsigned long)(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC >> 32), HEX) + String((unsigned long)(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC & 0xFFFFFFFF), HEX) + "<br>";
    WEBHTML = WEBHTML + "From IP: " + String(I.ESPNOW_LAST_INCOMINGMSG_FROM_IP) + "<br>";
    WEBHTML = WEBHTML + "Type: " + String(I.ESPNOW_LAST_INCOMINGMSG_TYPE) + "<br>";
    WEBHTML = WEBHTML + "Sender Type: " + String(I.ESPNOW_LAST_INCOMINGMSG_FROM_TYPE) + "<br>";
    WEBHTML = WEBHTML + "Payload: " + String(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD) + "<br>";
    WEBHTML = WEBHTML + "State: " + String(I.ESPNOW_LAST_INCOMINGMSG_STATE);
    WEBHTML = WEBHTML + "</div>";
  } else {
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Last Message Received</div>";
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">No messages received</div>";
  }
  
  // Last Outgoing Message Info
  if (I.ESPNOW_LAST_OUTGOINGMSG_TIME > 0) {
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Last Message Sent</div>";
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
    WEBHTML = WEBHTML + "Time: " + String((I.ESPNOW_LAST_OUTGOINGMSG_TIME)?dateify(I.ESPNOW_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss"):"???") + "<br>";
    WEBHTML = WEBHTML + "To MAC: " + String(MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC)) + "<br>";
    WEBHTML = WEBHTML + "Type: " + String(I.ESPNOW_LAST_OUTGOINGMSG_TYPE) + "<br>";
    WEBHTML = WEBHTML + "Payload: " + String(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD) + "<br>";
    WEBHTML = WEBHTML + "State: " + String(I.ESPNOW_LAST_OUTGOINGMSG_STATE ? "Success" : "Failed");
    WEBHTML = WEBHTML + "</div>";
  } else {
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">Last Message Sent</div>";
    WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">No messages sent</div>";
  }
  
  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main Page</a> ";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/Setup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Setup</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">Device Viewer</a> ";
  WEBHTML = WEBHTML + "<a href=\"/TimezoneSetup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">Timezone Setup</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";
  serverTextClose(200, true);
}

void handleSetup_POST() {
  I.lastServerStatusUpdate = I.currentTime;
  
  SerialPrint("=== Setup POST handler called ===", true);
  
  // Update device name
  if (server.hasArg("deviceName")) {
    String deviceName = server.arg("deviceName");
    if (deviceName.length() > 0 && deviceName.length() <= 32) {
      strncpy(Prefs.DEVICENAME, deviceName.c_str(), sizeof(Prefs.DEVICENAME) - 1);
      Prefs.DEVICENAME[sizeof(Prefs.DEVICENAME) - 1] = '\0'; // Ensure null termination
      SerialPrint("Device name updated to: " + deviceName, true);
    }
  }
  
  #ifdef _USETFT
  // Update display cycle settings
  if (server.hasArg("cycleHeaderMinutes")) {
    I.cycleHeaderMinutes = server.arg("cycleHeaderMinutes").toInt();
    if (I.cycleHeaderMinutes < 1) I.cycleHeaderMinutes = 1;
    if (I.cycleHeaderMinutes > 60) I.cycleHeaderMinutes = 60;
  }
  
  if (server.hasArg("cycleWeatherMinutes")) {
    I.cycleWeatherMinutes = server.arg("cycleWeatherMinutes").toInt();
    if (I.cycleWeatherMinutes < 1) I.cycleWeatherMinutes = 1;
    if (I.cycleWeatherMinutes > 60) I.cycleWeatherMinutes = 60;
  }
  
  if (server.hasArg("cycleCurrentConditionMinutes")) {
    I.cycleCurrentConditionMinutes = server.arg("cycleCurrentConditionMinutes").toInt();
    if (I.cycleCurrentConditionMinutes < 1) I.cycleCurrentConditionMinutes = 1;
    if (I.cycleCurrentConditionMinutes > 60) I.cycleCurrentConditionMinutes = 60;
  }
  
  if (server.hasArg("cycleFutureConditionsMinutes")) {
    I.cycleFutureConditionsMinutes = server.arg("cycleFutureConditionsMinutes").toInt();
    if (I.cycleFutureConditionsMinutes < 1) I.cycleFutureConditionsMinutes = 1;
    if (I.cycleFutureConditionsMinutes > 60) I.cycleFutureConditionsMinutes = 60;
  }
  
  if (server.hasArg("cycleFlagSeconds")) {
    I.cycleFlagSeconds = server.arg("cycleFlagSeconds").toInt();
    if (I.cycleFlagSeconds < 1) I.cycleFlagSeconds = 1;
    if (I.cycleFlagSeconds > 30) I.cycleFlagSeconds = 30;
  }
  
  if (server.hasArg("IntervalHourlyWeather")) {
    I.IntervalHourlyWeather = server.arg("IntervalHourlyWeather").toInt();
    if (I.IntervalHourlyWeather < 1) I.IntervalHourlyWeather = 1;
    if (I.IntervalHourlyWeather > 24) I.IntervalHourlyWeather = 24;
  }
  #endif
  
  // Update showTheseFlags
  uint16_t newFlags = 0;
  for (int i = 0; i < 9; i++) {
    String argName = "flag_bit" + String(i);
    if (server.hasArg(argName) && server.arg(argName) == "1") {
      bitSet(newFlags, i);
    }
  }
  I.showTheseFlags = newFlags;
  
  // Mark prefs as needing update
  Prefs.isUpToDate = false;
  
  // Save to NVS if available
  #ifdef _USE32
  BootSecure::setPrefs();
  #endif
  
  SerialPrint("Configuration updated successfully", true);
  
  // Show confirmation page
  WEBHTML = "";
  serverTextHeader();
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>Configuration Updated</h2>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; color: #2e7d32; padding: 15px; margin: 10px 0; border: 1px solid #4caf50; border-radius: 4px;\">";
  WEBHTML = WEBHTML + "<strong>Success!</strong> System configuration has been updated.";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<p><a href=\"/Setup\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">Back to Setup</a></p>";
  WEBHTML = WEBHTML + "<p><a href=\"/\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main Page</a></p>";
  WEBHTML = WEBHTML + "</body></html>";
  serverTextClose(200, true);
}

// ==================== DEVICE VIEWER FUNCTIONS ====================

void handleDeviceViewer() {
    I.lastServerStatusUpdate = I.currentTime;
    WEBHTML = "";
    serverTextHeader();
    
    WEBHTML = WEBHTML + "<body>";
    WEBHTML = WEBHTML + "<h2>Device Viewer</h2>";
    
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
        }
    }
    
    if (server.hasArg("error")) {
        String errorType = server.arg("error");
        if (errorType == "no_devices") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; color: #856404; padding: 15px; margin: 10px 0; border: 1px solid #ffeaa7; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Warning:</strong> No devices available to ping.";
            WEBHTML = WEBHTML + "</div>";
        } else if (errorType == "device_not_found") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Error:</strong> Device not found.";
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
    
    // Ensure current device index is valid
    if (CURRENT_DEVICE_VIEWER >= Sensors.getNumDevices()) {
        CURRENT_DEVICE_VIEWER = 0;
    }
    
    // Get current device
    DevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICE_VIEWER);
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
    WEBHTML = WEBHTML + "<h3>Device " + String(CURRENT_DEVICE_VIEWER + 1) + " of " + String(Sensors.getNumDevices()) + "</h3>";
    WEBHTML = WEBHTML + "</div>";
    
    // Device details
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h4>Device Information</h4>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 30%;\">Device Name:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devName) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">MAC Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(device->MAC)) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">IP Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + String(IPToString(device->IP)) + "\" target=\"_blank\">" + String(IPToString(device->IP)) + "</a></td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Device Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devType) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Seen:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->timeLogged ? dateify(device->timeLogged, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Data:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->timeRead ? dateify(device->timeRead, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Sending Interval:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->SendingInt) + " seconds</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Status:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->expired ? "Expired" : "Active") + "</td></tr>";
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    
    // Count sensors for this device
    uint8_t sensorCount = 0;
    for (int16_t i = 0; i < Sensors.getNumSensors(); i++) {
        SnsType* sensor = Sensors.getSensorBySnsIndex(i);
        if (sensor && sensor->deviceIndex == CURRENT_DEVICE_VIEWER) {
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
        for (int16_t i = 0; i < Sensors.getNumSensors(); i++) {
            SnsType* sensor = Sensors.getSensorBySnsIndex(i);
            if (sensor && sensor->deviceIndex == CURRENT_DEVICE_VIEWER) {
                // Determine sensor type name
                String typeName = "Unknown";
                if (Sensors.isSensorOfType(i, "temperature")) typeName = "Temperature";
                else if (Sensors.isSensorOfType(i, "humidity")) typeName = "Humidity";
                else if (Sensors.isSensorOfType(i, "pressure")) typeName = "Pressure";
                else if (Sensors.isSensorOfType(i, "battery")) typeName = "Battery";
                else if (Sensors.isSensorOfType(i, "HVAC")) typeName = "HVAC";
                else if (Sensors.isSensorOfType(i, "soil")) typeName = "Soil";
                else if (Sensors.isSensorOfType(i, "leak")) typeName = "Leak";
                else if (Sensors.isSensorOfType(i, "human")) typeName = "Human";
                else if (Sensors.isSensorOfType(i, "server")) typeName = "Server";
                
                WEBHTML = WEBHTML + "<tr>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsName) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsType) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + typeName + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsID) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->snsValue, 2) + "</td>";
                WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(sensor->timeLogged ? dateify(sensor->timeLogged, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td>";
                WEBHTML = WEBHTML + "</tr>";
            }
        }
        WEBHTML = WEBHTML + "</table>";
    }
    WEBHTML = WEBHTML + "</div>";
    
    // Navigation buttons
    WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px;\">";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PREV\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">Previous Device</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_NEXT\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Next Device</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">Ping Device</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_DELETE\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px;\" onclick=\"return confirm('Are you sure you want to delete this device and all its sensors? This action cannot be undone.');\">Delete Device</a> ";
    WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
    WEBHTML = WEBHTML + "</div>";
    
    WEBHTML = WEBHTML + "</body></html>";
    serverTextClose(200, true);
}

void handleDeviceViewerNext() {
    I.lastServerStatusUpdate = I.currentTime;
    
    if (Sensors.getNumDevices() > 0) {
        CURRENT_DEVICE_VIEWER = (CURRENT_DEVICE_VIEWER + 1) % Sensors.getNumDevices();
    }
    
    server.sendHeader("Location", "/DEVICEVIEWER");
    server.send(302, "text/plain", "Redirecting to next device...");
}

void handleDeviceViewerPrev() {
    I.lastServerStatusUpdate = I.currentTime;
    
    if (Sensors.getNumDevices() > 0) {
        if (CURRENT_DEVICE_VIEWER == 0) {
            CURRENT_DEVICE_VIEWER = Sensors.getNumDevices() - 1;
        } else {
            CURRENT_DEVICE_VIEWER--;
        }
    }
    
    server.sendHeader("Location", "/DEVICEVIEWER");
    server.send(302, "text/plain", "Redirecting to previous device...");
}

void handleDeviceViewerPing() {
    I.lastServerStatusUpdate = I.currentTime;
    
    // Check if we have any devices
    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
        server.send(302, "text/plain", "No devices available to ping.");
        return;
    }
    
    // Ensure current device index is valid
    if (CURRENT_DEVICE_VIEWER >= Sensors.getNumDevices()) {
        CURRENT_DEVICE_VIEWER = 0;
    }
    
    // Get current device
    DevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICE_VIEWER);
    if (!device) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }
    
    // Send ping via ESPNow (if ESPNow is available)
    #ifdef _USEESPNOW
    bool pingResult = sendESPNOWPing(device->MAC);
    if (pingResult) {
        server.sendHeader("Location", "/DEVICEVIEWER?ping=success");
        server.send(302, "text/plain", "Ping sent successfully.");
    } else {
        server.sendHeader("Location", "/DEVICEVIEWER?ping=failed");
        server.send(302, "text/plain", "Failed to send ping.");
    }
    #else
    server.sendHeader("Location", "/DEVICEVIEWER?ping=failed");
    server.send(302, "text/plain", "ESPNow not available.");
    #endif
}

void handleDeviceViewerDelete() {
    I.lastServerStatusUpdate = I.currentTime;
    
    // Check if we have any devices
    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
        server.send(302, "text/plain", "No devices available to delete.");
        return;
    }
    
    // Ensure current device index is valid
    if (CURRENT_DEVICE_VIEWER >= Sensors.getNumDevices()) {
        CURRENT_DEVICE_VIEWER = 0;
    }
    
    // Get current device
    DevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICE_VIEWER);
    if (!device) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }
    
    String deviceName = String(device->devName);
    
    // Count sensors for this device
    uint8_t sensorCount = 0;
    for (int16_t i = 0; i < Sensors.getNumSensors(); i++) {
        SnsType* sensor = Sensors.getSensorBySnsIndex(i);
        if (sensor && sensor->deviceIndex == CURRENT_DEVICE_VIEWER) {
            sensorCount++;
        }
    }
    
    // Delete the device and all its sensors
    int16_t deleteResult = Sensors.initDevice(CURRENT_DEVICE_VIEWER);
    
    if (deleteResult > 0) {
        // Reset to first device if we deleted the current one
        if (CURRENT_DEVICE_VIEWER >= Sensors.getNumDevices()) {
            CURRENT_DEVICE_VIEWER = 0;
        }
        
        String redirectUrl = "/DEVICEVIEWER?delete=success&device=" + urlEncode(deviceName) + "&sensors=" + String(sensorCount);
        server.sendHeader("Location", redirectUrl);
        server.send(302, "text/plain", "Device deleted successfully.");
    } else {
        server.sendHeader("Location", "/DEVICEVIEWER?error=delete_failed");
        server.send(302, "text/plain", "Failed to delete device.");
    }
}

void handleForceBroadcast() {
    I.lastServerStatusUpdate = I.currentTime;
    
    SerialPrint("Force broadcast requested from web interface", true);
    
    // Force a broadcast message
     I.makeBroadcast = true;
    
        server.sendHeader("Location", "/?broadcast=success");
        server.send(302, "text/plain", "Broadcast sent successfully.");
    
}

void handleStatus() {
    I.lastServerStatusUpdate = I.currentTime;
    WEBHTML = "";
    serverTextHeader();
    
    WEBHTML = WEBHTML + "<body>";
    WEBHTML = WEBHTML + "<h2>System Status</h2>";
    
    // System Information
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h3>System Information</h3>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    
    // Device MAC address
    uint64_t deviceMAC = ESP.getEfuseMac();
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 30%;\">Device MAC:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String((unsigned long)(deviceMAC >> 32), HEX) + String((unsigned long)(deviceMAC & 0xFFFFFFFF), HEX) + "</td></tr>";
    
    // IP Address
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">IP Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + WiFi.localIP().toString() + "</td></tr>";
    
    // Device Name
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Device Name:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(Prefs.DEVICENAME) + "</td></tr>";
    
    // Device Type
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Device Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MYTYPE) + "</td></tr>";
    
    // Uptime
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Uptime:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(now() - I.ALIVESINCE) + " seconds</td></tr>";
    
    // Current Time
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Current Time:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(dateify(now(), "mm/dd/yyyy hh:nn:ss")) + "</td></tr>";
    
    // Free Memory
    #ifdef _USE32
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Free Heap:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Min Free Heap:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(ESP.getMinFreeHeap()) + " bytes</td></tr>";
    #endif
    #ifdef _USE8266
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Free Heap:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
    #endif
    
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    
    // Sensor Statistics
    WEBHTML = WEBHTML + "<div style=\"background-color: #e3f2fd; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #2196f3;\">";
    WEBHTML = WEBHTML + "<h3>Sensor Statistics</h3>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e3f2fd; font-weight: bold; width: 30%;\">Total Sensors:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(Sensors.getNumSensors()) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e3f2fd; font-weight: bold;\">Total Devices:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(Sensors.getNumDevices()) + "</td></tr>";
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    
    // ESPNow Status
    WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #4caf50;\">";
    WEBHTML = WEBHTML + "<h3>ESPNow Communication Status</h3>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold; width: 30%;\">Messages Received Today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ESPNOW_RECEIVES) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Messages Sent Today:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ESPNOW_SENDS) + "</td></tr>";
    
    if (I.ESPNOW_LAST_INCOMINGMSG_TIME > 0) {
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Last Message Received:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(dateify(I.ESPNOW_LAST_INCOMINGMSG_TIME, "mm/dd/yyyy hh:nn:ss")) + "</td></tr>";
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">From MAC:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC)) + "</td></tr>";
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Message Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ESPNOW_LAST_INCOMINGMSG_TYPE) + "</td></tr>";
    } else {
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Last Message Received:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">Never</td></tr>";
    }
    
    if (I.ESPNOW_LAST_OUTGOINGMSG_TIME > 0) {
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Last Message Sent:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(dateify(I.ESPNOW_LAST_OUTGOINGMSG_TIME, "mm/dd/yyyy hh:nn:ss")) + "</td></tr>";
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">To MAC:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC)) + "</td></tr>";
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Status:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ESPNOW_LAST_OUTGOINGMSG_STATE ? "Success" : "Failed") + "</td></tr>";
    } else {
        WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #e8f5e8; font-weight: bold;\">Last Message Sent:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">Never</td></tr>";
    }
    
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    
    // System Flags
    WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #ffeaa7;\">";
    WEBHTML = WEBHTML + "<h3>System Flags</h3>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #fff3cd; font-weight: bold; width: 30%;\">Show These Flags:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.showTheseFlags) + " (0x" + String(I.showTheseFlags, HEX) + ")</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #fff3cd; font-weight: bold;\">Is Expired:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.isExpired ? "Yes" : "No") + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #fff3cd; font-weight: bold;\">Is Flagged:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.isFlagged ? "Yes" : "No") + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #fff3cd; font-weight: bold;\">Was Flagged:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.wasFlagged ? "Yes" : "No") + "</td></tr>";
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    
    // Navigation
    WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px;\">";
    WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main Page</a> ";
    WEBHTML = WEBHTML + "<a href=\"/Setup\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">System Setup</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">Device Viewer</a>";
    WEBHTML = WEBHTML + "</div>";
    
    WEBHTML = WEBHTML + "</body></html>";
    serverTextClose(200, true);
}

void handleWiFiConfig_POST() {
  I.lastServerStatusUpdate = I.currentTime;
  
  SerialPrint("=== WiFiConfig POST handler called ===", true);
  SerialPrint("Processing WiFi request...", true);

  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String lmk_key = server.arg("lmk_key");
  String address = server.arg("address");
  
  SerialPrint("Received arguments:", true);
  SerialPrint("SSID: " + ssid, true);
  SerialPrint("Password length: " + String(password.length()), true);
  SerialPrint("LMK Key: " + lmk_key, true);
  

  SerialPrint("Saving WiFi credentials to prefs...", true);
  SerialPrint("SSID: " + ssid, true);
  SerialPrint("Password: " + password, true);
  SerialPrint("ESPNow key: " + lmk_key, true);
  // Save WiFi credentials and ESPNow key
  if (ssid.length() > 0) {
    if (lmk_key.length() > 16) {
      lmk_key = lmk_key.substring(0, 16);
    } 
    if (lmk_key.length() < 16) {
      lmk_key = lmk_key + String(16 - lmk_key.length(), '0'); //pad short keys with 0s
    } 

    snprintf((char*)Prefs.WIFISSID, sizeof(Prefs.WIFISSID), "%s", ssid.c_str());
    snprintf((char*)Prefs.WIFIPWD, sizeof(Prefs.WIFIPWD), "%s", password.c_str());
    snprintf((char*)Prefs.KEYS.ESPNOW_KEY, sizeof(Prefs.KEYS.ESPNOW_KEY), "%s", lmk_key.c_str());
    Prefs.HAVECREDENTIALS = true;
    Prefs.isUpToDate = false;
  }

  // Try to connect to WiFi - note that we are already in  mixed AP+STA mode
  WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
  delay(250);

  if (WiFi.status()) {
    // WiFi connection successful
    Prefs.MYIP = (uint32_t) WiFi.localIP();
    Prefs.status = 1; // Mark WiFi as connected
    Prefs.HAVECREDENTIALS = true;
    SerialPrint("WiFi connected successfully! Saving prefs...", true);
    BootSecure::setPrefs(); //save the info we have so far
    
    // Reset WiFi failure count since we successfully connected
    I.wifiFailCount = 0;
    
    SerialPrint("WiFi connected successfully!", true);
    
    // Note: Weather address handling is not implemented in this project
    SerialPrint("WiFi connection established", true);
    // Save prefs to NVS
    #ifdef _USE32
    BootSecure::setPrefs();
    #endif
    
    // Return success response (no redirect - let user click "Check Timezone" button)
    server.send(200, "text/plain", "WiFi credentials submitted successfully!");
  } else {
    // WiFi connection failed, redirect back to config page
    SerialPrint("WiFi connection failed! Credentials not saved yet.", true);
    server.sendHeader("Location", "/WiFiConfig?error=connection_failed");
    server.send(302, "text/plain", "WiFi connection failed. Please check credentials and try again.");
  }
}





void handleNotFound(){
  server.send(404, "text/plain", "Arduino says 404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
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


void setupServerRoutes() {
  // Main routes
  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/UPDATEALLSENSORREADS", handleUpdateAllSensorReads);               
  server.on("/UPDATESENSORREAD",handleUpdateSensorRead);
  server.on("/SETTHRESH", HTTP_POST, handleSetThreshold);               
  server.on("/UPDATESENSORPARAMS", handleUpdateSensorParams);
  server.on("/NEXTSNS", handleNext);
  server.on("/LASTSNS", handleLast);
  server.on("/REBOOT", handleReboot);
  server.on("/ResetWiFi", handleWiFiConfig_RESET);
  server.on("/WiFiConfig", HTTP_GET, handleWiFiConfig);
  server.on("/WiFiConfig", HTTP_POST, handleWiFiConfig_POST);
  server.on("/Setup", HTTP_GET, handleSetup);
  server.on("/Setup", HTTP_POST, handleSetup_POST);
  server.on("/DEVICEVIEWER", HTTP_GET, handleDeviceViewer);
  server.on("/DEVICEVIEWER_NEXT", HTTP_GET, handleDeviceViewerNext);
  server.on("/DEVICEVIEWER_PREV", HTTP_GET, handleDeviceViewerPrev);
  server.on("/DEVICEVIEWER_PING", HTTP_GET, handleDeviceViewerPing);
  server.on("/DEVICEVIEWER_DELETE", HTTP_GET, handleDeviceViewerDelete);
  server.on("/FORCEBROADCAST", HTTP_GET, handleForceBroadcast);
  server.on("/STATUS", HTTP_GET, handleStatus);
  server.on("/TimezoneSetup", HTTP_GET, handleTimezoneSetup);
  server.on("/TimezoneSetup", HTTP_POST, handleTimezoneSetup_POST);
  server.on("/CONFIG", HTTP_GET, handleCONFIG);
  server.on("/CONFIG", HTTP_POST, handleCONFIG_POST);

  
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call


}




bool wait_ms(uint16_t ms) {


  uint32_t mend = millis() + ms;
  if ((uint32_t) (0-ms)<mend) return false; //0-ms should be some massive number... if it is less than mend then we are near the millis rollover
  

  while (millis() < mend) {
    //handle these tasks
    //be available for interrupts
    #ifndef _USELOWPOWER      
      server.handleClient();
    #endif

  }

  return true;
}

void handleNext(void) {
  // Simple placeholder implementation
  server.send(200, "text/plain", "Next sensor");
}

void handleLast(void) {
  // Simple placeholder implementation  
  server.send(200, "text/plain", "Last sensor");
}

