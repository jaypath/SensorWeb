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

  // Add delay to ensure system is ready
  delay(500);
  
  startSoftAP(&wifiID,&wifiPWD,&apIP);

  delay(500);


  SerialPrint("AP Station ID started.",false);
  
  // Add delay before starting server
  delay(500);

  //add server routes temporarily
  server.on("/", handleRoot);
  server.on("/WiFiConfig", handleWiFiConfig);
  server.on("/WiFiConfig_POST", handleWiFiConfig_POST);
  server.on("/WiFiConfig_RESET", handleWiFiConfig_RESET);
  server.on("/TimezoneSetup", handleTimezoneSetup);
  server.on("/TimezoneSetup_POST", handleTimezoneSetup_POST);

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

    uint32_t startTime = millis();
    const uint32_t timeoutMs = 600000; // 10 minute timeout
    RegistrationCompleted = false;

    do {
      server.handleClient(); //check if user has set up WiFi
            
      // Check for timeout
      if (false && millis() - startTime > timeoutMs) {
        #ifdef _USETFT
        tft.println("Timeout waiting for credentials. Rebooting...");
        delay(6000);
        #endif
        controlledReboot("WiFi credentials timeout", RESET_WIFI, true);
        break;
      }

    } while (Prefs.HAVECREDENTIALS == false );
    
    // Only reboot if we have credentials but registration is not completed
    if ( RegistrationCompleted == false) {
      // User has submitted WiFi credentials but hasn't completed timezone setup
      // Continue running the server to allow timezone configuration
      #ifdef _USETFT
      tft.println("WiFi credentials saved. Please configure timezone settings.");
      tft.println("Server will continue running for timezone setup...");
      #endif
      
      // Keep the server running indefinitely until timezone setup is complete
      while (RegistrationCompleted == false) {
        server.handleClient();
        delay(50);
      }
    }
    
    #ifdef _USETFT
    tft.println("Rebooting with updated credentials...");
    delay(5000);
    #endif
    controlledReboot("WIFI CREDENTIALS RESET",RESET_NEWWIFI);

   
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

WiFiClient wfclient;
HTTPClient http;
  
byte ipindex=0;
bool isGood = false;


  if(Prefs.status>0){
    String URL;
    int httpCode=404;
    String macHex = macToHexNoSep();
    String ipStr = WiFi.localIP().toString();
    String json = "{";
    json += "\"mac\":\"" + macHex + "\",";
    json += "\"ip\":\"" + ipStr + "\",";
    json += "\"sensor\":[{";
    json += "\"type\":" + String(S->snsType);
    json += ",\"id\":" + String(S->snsID);
    json += ",\"name\":\"" + String(S->snsName) + "\"";
    json += ",\"value\":" + String(S->snsValue, 6);
    json += ",\"timeRead\":" + String(S->timeRead);
    json += ",\"timeLogged\":" + String(S->timeLogged);
    json += ",\"sendingInt\":" + String(S->SendingInt);
    json += ",\"flags\":" + String(S->Flags);
    json += "}]}";

    // iterate all known servers from Sensors (devType >=100)
    for (int16_t i=0; i<NUMDEVICES && i<Sensors.getNumDevices(); ++i) {
      DevType* d = Sensors.getDeviceByDevIndex(i);
      if (!d || !d->IsSet || d->devType < 100) continue;
      IPAddress ip(d->IP);
      URL = String("http://") + ip.toString() + "/POST";

      http.useHTTP10(true);

      #ifdef _DEBUG
        Serial.print("POST "); Serial.print(URL); Serial.print(" body: "); Serial.println(json);
      #endif

      http.begin(wfclient,URL.c_str());
      http.addHeader("Content-Type","application/x-www-form-urlencoded");
      String body = "JSON=" + urlEncode(json);
      httpCode = (int) http.POST(body);
      String payload = http.getString();
      http.end();
      SerialPrint("httpCode: " + (String) httpCode,false);
      SerialPrint("payload: " + payload,true);
      if (httpCode == 200) { isGood = true; }
    }
  }

  if (isGood) bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I wrote the value so set bit 6 (change in flag) to zero

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
  WEBHTML = WEBHTML + "<form id=\"wifiForm\" method=\"POST\" action=\"/WiFiConfig_POST\">";
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
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/TimezoneSetup_POST\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // UTC Offset
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><strong>UTC Offset (seconds):</strong></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"utc_offset\" value=\"" + String(utc_offset) + "\" style=\"width: 100%; padding: 8px;\"></div>";
  
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
  
  SerialPrint("Saving timezone configuration...", true);
  
  // Update timezone settings
  if (server.hasArg("utc_offset")) {
    Prefs.TimeZoneOffset = server.arg("utc_offset").toInt();
  }
  if (server.hasArg("dst_enabled")) {
    Prefs.DST = server.arg("dst_enabled").toInt();
    if (Prefs.DST) {
      Prefs.DSTOffset = 3600;
    } else {
      Prefs.DSTOffset = 0;
    }
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
  
  // Mark prefs as needing update
  Prefs.isUpToDate = false;
  
  // Save prefs to NVS
  #ifdef _USE32
  BootSecure::setPrefs();
  #endif
  
  SerialPrint("Timezone configuration saved. Rebooting...", true);
  
  // Show confirmation page
  WEBHTML = "";
  serverTextHeader();
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>Configuration Saved</h2>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; color: #2e7d32; padding: 15px; margin: 10px 0; border: 1px solid #4caf50; border-radius: 4px;\">";
  WEBHTML = WEBHTML + "<strong>Success!</strong> Timezone configuration has been saved. The device will reboot in 5 seconds...";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<p>UTC Offset: " + String(Prefs.TimeZoneOffset) + " seconds</p>";
  WEBHTML = WEBHTML + "<p>DST Enabled: " + String(Prefs.DST ? "Yes" : "No") + "</p>";
  if (Prefs.DST) {
    WEBHTML = WEBHTML + "<p>DST Start: Month " + String(Prefs.DSTStartMonth) + ", Day " + String(Prefs.DSTStartDay) + "</p>";
    WEBHTML = WEBHTML + "<p>DST End: Month " + String(Prefs.DSTEndMonth) + ", Day " + String(Prefs.DSTEndDay) + "</p>";
  }
  WEBHTML = WEBHTML + "<script>setTimeout(function(){ window.location.href = '/'; }, 5000);</script>";
  WEBHTML = WEBHTML + "</body></html>";
  serverTextClose(200, true);
  
  // Set registration completed flag to exit APStation_Mode loop
  RegistrationCompleted = true;
  
}


void handleWiFiConfig_POST() {
  I.lastServerStatusUpdate = I.currentTime;
  
  SerialPrint("Processing WiFi request...", true);

  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String lmk_key = server.arg("lmk_key");
  String address = server.arg("address");
  
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
    SerialPrint("WiFi connection failed!", true);
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
  server.on("/WiFiConfig_POST", HTTP_POST, handleWiFiConfig_POST);
  server.on("/TimezoneSetup", HTTP_GET, handleTimezoneSetup);
  server.on("/TimezoneSetup_POST", HTTP_POST, handleTimezoneSetup_POST);
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

