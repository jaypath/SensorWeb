#include "server.hpp"


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
    Prefs.status = 1; // Set status to indicate WiFi is connected
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
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">GLOBAL_TIMEZONE_OFFSET</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"GLOBAL_TIMEZONE_OFFSET\" value=\"" + (String) I.GLOBAL_TIMEZONE_OFFSET + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">DSTOFFSET</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"DSTOFFSET\" value=\"" + (String) I.DSTOFFSET + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  
  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  

  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
 
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}




void handleCONFIG_POST() {
  I.lastServerStatusUpdate = I.currentTime;
  
  // Process form submissions and update editable fields
  if (server.hasArg("GLOBAL_TIMEZONE_OFFSET")) {
    I.GLOBAL_TIMEZONE_OFFSET = server.arg("GLOBAL_TIMEZONE_OFFSET").toInt();
  }
  if (server.hasArg("DSTOFFSET")) {
    I.DSTOFFSET = server.arg("DSTOFFSET").toInt();
  }
  
  
  

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
  
  // Display current WiFi status and configuration
  WEBHTML = WEBHTML + "<h3>Current WiFi Status</h3>";

  if (!Prefs.HAVECREDENTIALS) {
    WEBHTML = WEBHTML + "<p>Wifi credentials are not set, and are required.</p>";
    WEBHTML = WEBHTML + "<p>Set your address for weather lookup (weather information will be matched to the address provided).<br></p>";
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

  

  //hidden field for BLOB is needed for old encryption code
//  WEBHTML = WEBHTML + "<input type=\"hidden\" id=\"BLOB\" name=\"BLOB\">";
//  WEBHTML = WEBHTML + "<input type=\"button\" value=\"Update WiFi Configuration\" onclick=\"submitEncryptedForm()\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";

  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Update WiFi Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
}


void handleWiFiConfig_POST() { //updated code
  I.lastServerStatusUpdate = I.currentTime;


  SerialPrint("Updating Wifi Settings...",true);
#ifdef _USETFT
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(2);
  tft.printf("Updating Wifi Settings...\n");
#endif

  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("lmk_key")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String lmk_key = server.arg("lmk_key");
    
    
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


  if (connectWiFi()>0) {
    Prefs.MYIP = (uint32_t) WiFi.localIP(); //update this here just in case

  } else {    
    server.sendHeader("Location", "/WiFiConfig");
    server.send(302, "text/plain", "Failed to connect to wifi, manual reboot required.");
  }

    server.sendHeader("Location", "/WiFiConfig");
    server.send(302, "text/plain", "WiFi configuration updated successfully and saved to NVS. Attempting to connect...");

  
    //force prefs to save here
#ifdef _USETFT
  tft.println("Prefs updated, rebooting");
#endif
  delay(3000);

  //controlled reboot will store prefs and reboot
  controlledReboot("Wifi credentials updated",RESETCAUSE::RESET_NEWWIFI,true);
  
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

