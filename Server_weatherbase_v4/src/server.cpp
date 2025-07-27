#include "server.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "SDCard.hpp"
#ifdef _USETFT
  #include "graphics.hpp"
  extern LGFX tft;
#endif
#include "BootSecure.hpp"
#include <esp_system.h>

#define WIFI_CONFIG_KEY "Kj8mN2pQ9rS5tU7vW3xY1zA4bC6dE8fG"

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

extern STRUCT_PrefsH Prefs;
//extern bool requestWiFiPassword(const uint8_t* serverMAC);

String getCert(String filename) 
{

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

bool Server_SecureMessage(String& URL, String& payload, int& httpCode,  String& cacert) { 
  HTTPClient http;
  WiFiClientSecure wfclient;
  wfclient.setCACert(cacert.c_str());

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

int16_t tryWifi(uint16_t delayms) {
  int16_t retries = 0;
  int16_t i = 0;
  if (Prefs.HAVECREDENTIALS) {
    WiFi.mode(WIFI_STA);
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
    Station_Mode();
    return -1000;
  }

    return retries;
}

void Station_Mode() {
  
  String wifiPWD = "";
  String wifiID = "";
  IPAddress apIP;
  connectSoftAP(&wifiID,&wifiPWD,&apIP);
  server.begin(); //start server

    #ifdef _USETFT
    tft.clear();
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.printf("No WiFi credentials. Please join WiFi \n%s\nusing pwd \n%s\nand go to website \n%s\nto set up WiFi.\n", 
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

/*
  void TEMP_CYCLE_SERVERS(void) {
//this is a template for a function that will cycle through servers and attempt to connect to them. It is a remnant of the original connectwifi function
  static uint32_t lastAttemptTime = 0;
  static uint32_t lastServerAttemptTime = 0;
  static uint32_t lastAPAttemptTime = 0;
  static uint32_t lastRebootTime = 0;
  static int16_t lastServerIndex = 0;
  static int16_t attemptCount = 0;
  static bool inAPMode = false;
  
  uint32_t nowt = now();
  
  // Step 1: Check if SSID and PWD are stored in Prefs
  bool hasSSID = false;
  bool hasPWD = false;
  for (int i = 0; i < 33; ++i) if (Prefs.WIFISSID[i] != 0) hasSSID = true;
  for (int i = 0; i < 65; ++i) if (Prefs.WIFIPWD[i] != 0) hasPWD = true;
  
  if ((!hasSSID || !hasPWD) && Prefs.HAVECREDENTIALS) {  
    Prefs.HAVECREDENTIALS = false;
    Prefs.isUpToDate = false;
    return 0;
  }

  if (tryWifi()) {
    return 0;
  }

  if (!hasSSID) {
    // No SSID stored, go to step 6
    if (!inAPMode) {
      connectSoftAP();
      inAPMode = true;
    }
    return -1; // failure due to no SSID
  }
  
  if (!hasPWD) {
    // No password stored, go to step 6
    if (!inAPMode) {
      connectSoftAP();
      inAPMode = true;
    }
    return -2; // failure due to wrong pwd (no pwd stored)
  }
  
  // Step 2 & 3: Try to connect with stored credentials
  if (nowt - lastAttemptTime >= 2000) { // Every 2 seconds
    lastAttemptTime = nowt;
    attemptCount++;
    
    #ifdef HAS_TFT
    if (attemptCount == 1) {
      tft.print("Trying WiFi");
    } else {
      tft.print(".");
    }
    #endif
    
    // Try to connect
    if (getWiFiCredentials()) {
      WiFi.mode(WIFI_STA);
      WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
      delay(250);
    }
    
    if (WifiStatus()) {
      Prefs.MYIP = WiFi.localIP();
      Prefs.isUpToDate = false;
      // Switch back to station mode if we were in AP mode
      if (inAPMode) {
        WiFi.mode(WIFI_STA);
        inAPMode = false;
      }
      // Reset static variables for next time
      lastAttemptTime = 0;
      lastServerAttemptTime = 0;
      lastAPAttemptTime = 0;
      lastRebootTime = 0;
      lastServerIndex = 0;
      attemptCount = 0;
      return attemptCount; // Return number of attempts needed
    }
    
    // If 10 attempts made, proceed to step 4
    if (attemptCount >= 10) {
      #ifdef HAS_TFT
      tft.println();
      tft.println("Attempting to retrieve WiFi credentials");
      #endif
      
      // Step 4: Try to get credentials from servers
      if (nowt - lastServerAttemptTime >= 10000) { // Every 10 seconds
        lastServerAttemptTime = nowt;
        
        // Find servers (devices of type 100 or higher, but not me)
        int16_t numDevices = Sensors.getNumDevices();
        int16_t serversFound = 0;
        int16_t currentIndex = lastServerIndex;
        
        // Look for servers starting from lastServerIndex
        for (int16_t i = 0; i < numDevices; i++) {
          int16_t deviceIndex = (currentIndex + i) % numDevices;
          DevType* device = Sensors.getDeviceByDevIndex(deviceIndex);
          
          if (device && device->IsSet && device->devType >= 100) {
            // Check if this is not me (compare MAC addresses)
            bool isMe = (device->MAC == Prefs.PROCID);
            
            if (!isMe) {
              serversFound++;
              lastServerIndex = deviceIndex;
              
              // Convert MAC to byte array
              uint8_t serverMAC[6];
              for (int j = 0; j < 6; j++) {
                serverMAC[5-j] = (device->MAC >> (8*j)) & 0xFF;
              }
              
              #ifdef HAS_TFT
              tft.printf("Contacting server: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                        serverMAC[0], serverMAC[1], serverMAC[2], 
                        serverMAC[3], serverMAC[4], serverMAC[5]);
              #endif
              
              // Send WiFi password request
              uint8_t nonce[8];
              esp_fill_random(nonce, 8);
              requestWiFiPassword(serverMAC, nonce);
              
              // Wait 10 seconds for response
              delay(10000);
              
                             // Check if we got new credentials
               if (getWiFiCredentials()) {
                 // Try to connect with new credentials
                 WiFi.mode(WIFI_STA);
                 WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
                 delay(250);
                
                if (WifiStatus()) {
                  Prefs.MYIP = WiFi.localIP();
                  Prefs.isUpToDate = false;
                  // Switch back to station mode if we were in AP mode
                  if (inAPMode) {
                    WiFi.mode(WIFI_STA);
                    inAPMode = false;
                  }
                  // Reset static variables
                  lastAttemptTime = 0;
                  lastServerAttemptTime = 0;
                  lastAPAttemptTime = 0;
                  lastRebootTime = 0;
                  lastServerIndex = 0;
                  attemptCount = 0;
                  return attemptCount; // Return number of attempts needed
                }
              }
              
              break; // Try one server at a time
            }
          }
        }
        
        // If no servers found or no response, proceed to step 5
        if (serversFound == 0) {
          #ifdef HAS_TFT
          tft.println("No servers responded, please check wifi and manually enter credentials.");
          #endif
          
          // Step 5: Continue trying to connect every minute
          if (nowt - lastAPAttemptTime >= 60000) { // Every 1 minute
            lastAPAttemptTime = nowt;
            
            if (getWiFiCredentials()) {
              WiFi.mode(WIFI_STA);
              WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
              delay(250);
              
              if (WifiStatus()) {
                Prefs.MYIP = WiFi.localIP();
                Prefs.isUpToDate = false;
                // Switch back to station mode if we were in AP mode
                if (inAPMode) {
                  WiFi.mode(WIFI_STA);
                  inAPMode = false;
                }
                // Reset static variables
                lastAttemptTime = 0;
                lastServerAttemptTime = 0;
                lastAPAttemptTime = 0;
                lastRebootTime = 0;
                lastServerIndex = 0;
                attemptCount = 0;
                return attemptCount; // Return number of attempts needed
              }
            }
          }
          
          // Step 6: Enter AP mode
          if (!inAPMode) {
            connectSoftAP();
            inAPMode = true;
          }
          
          // If credentials are available, try every 30 seconds
          if (hasSSID && hasPWD) {
            if (nowt - lastAPAttemptTime >= 30000) { // Every 30 seconds
              lastAPAttemptTime = nowt;
              
              if (getWiFiCredentials()) {
                WiFi.mode(WIFI_STA);
                WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
                delay(250);
                
                if (WifiStatus()) {
                  Prefs.MYIP = WiFi.localIP();
                  Prefs.isUpToDate = false;
                  // Switch back to station mode if we were in AP mode
                  if (inAPMode) {
                    WiFi.mode(WIFI_STA);
                    inAPMode = false;
                  }
                  // Reset static variables
                  lastAttemptTime = 0;
                  lastServerAttemptTime = 0;
                  lastAPAttemptTime = 0;
                  lastRebootTime = 0;
                  lastServerIndex = 0;
                  attemptCount = 0;
                  return attemptCount; // Return number of attempts needed
                }
              }
            }
          }
          
          // Reboot every 30 minutes
          if (nowt - lastRebootTime >= 1800000) { // 30 minutes
            lastRebootTime = nowt;
            controlledReboot("WiFi failed for 30 min, rebooting", RESET_WIFI, true);
          }
        }
      }
    }
  }
  
  return 0; // failure due to unable to connect to network
}
*/


void handleReboot() {
  WEBHTML = "Rebooting in 10 sec";
  serverTextClose(200, false);  //This returns to the main page
  delay(10000);
  ESP.restart();
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
  DevType* device = Sensors.getDeviceBySnsIndex(j);
  if (device) {
    String URL = IPToString(device->IP) + "/UPDATEALLSENSORREADS";
    Server_Message(URL, payload, httpCode);
  }

  server.sendHeader("Location", "/");
  WEBHTML= "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}


void handleREQUESTWEATHER() {
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
  

  serverTextClose(200,false);

  return;
}

void handleTIMEUPDATE() {


  timeClient.forceUpdate();

  server.sendHeader("Location", "/");


  WEBHTML = "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}


void handleSETWIFI() {
  if (server.uri() != "/SETWIFI") return;


  initCreds(&Prefs);

  String creds = "";

  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SSID") {
      snprintf((char *) Prefs.WIFISSID,64,"%s",server.arg(i).c_str());
      Prefs.isUpToDate = false;
    }
    if (server.argName(i)=="PWD") {
      snprintf((char *) Prefs.WIFIPWD,64,"%s",server.arg(i).c_str());
      Prefs.isUpToDate = false;
    }
  }
  Prefs.HAVECREDENTIALS = true;
  Prefs.isUpToDate = true;
	BootSecure bs;

  if (bs.setPrefs()) {
    WEBHTML= "OK, stored new credentials.\nSSID:" + (String) (char *) Prefs.WIFISSID + "\nPWD: NOT_SHOWN";  
    serverTextClose(201,false);

  }   else {
    WEBHTML= "OK, Cleared credentials!";  
    Prefs.HAVECREDENTIALS = false;
    Prefs.isUpToDate = false;
    serverTextClose(201,false);
//    controlledReboot("User cleared credentials",RESET_USER);
  }


//  server.sendHeader("Location", "/");//This Line goes to root page
  

}



void handleSTATUS() {
  
  LAST_WEB_REQUEST = I.currentTime; //this is the time of the last web request
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";  
  WEBHTML = WEBHTML + "<h2>" + (String) MYNAME + " Status Page</h2>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

  #ifdef _USE8266
  WEBHTML = WEBHTML + "Free Stack Memory: " + ESP.getFreeContStack() + "<br>";  
  #endif

  #ifdef _USE32
  WEBHTML = WEBHTML + "Free Internal Heap Memory: " + esp_get_free_internal_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "Free Total Heap Memory: " + esp_get_free_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "Minimum Free Heap: " + esp_get_minimum_free_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "PSRAM Size: " + (String) ESP.getFreePsram() + " / " + (String) ESP.getPsramSize() + "<br>"; 
  
  #endif


  /*  WEBHTML += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  WEBHTML += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  WEBHTML += "<button type=\"submit\">Update Time</button><br>";
  WEBHTML += "</FORM><br>";
*/

WEBHTML += "Number of devices/sensors: " + (String) Sensors.getNumDevices() + " / " + (String) Sensors.getNumSensors() + "<br>";
WEBHTML = WEBHTML + "Alive since: " + dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss") + "<br>";
WEBHTML = WEBHTML + "Last error: " + (String) I.lastError + " @" + (String) (I.lastErrorTime ? dateify(I.lastErrorTime) : "???") + "<br>";
WEBHTML = WEBHTML + "Last known reset: " + (String) lastReset2String()  + "<br>";
WEBHTML = WEBHTML + "---------------------<br>";      

WEBHTML += "Weather last retrieved at: " + (String) (WeatherData.lastUpdateT ? dateify(WeatherData.lastUpdateT) : "???") + "<br>";
WEBHTML += "Weather last failure at: " + (String) (WeatherData.lastUpdateError ? dateify(WeatherData.lastUpdateError) : "???") + "<br>";
WEBHTML += "NOAA address: " + WeatherData.getGrid(0) + "<br>";
WEBHTML = WEBHTML + "---------------------<br>";      
WEBHTML = WEBHTML + "Previous errors (up to 10)" + "<br>";
for (uint8_t i=1;i<11;i++) {
  String error="";
  if (readErrorFromSD(&error,i)) WEBHTML += (String) i + ": " + error + "<br>";
  else break;

}


WEBHTML = WEBHTML + "</font>---------------------<br>";      





  WEBHTML += R"===(
  <FORM action="/FLUSHSD" method="get">
  <p style="font-family:arial, sans-serif">
  Type a number and Click FLUSH to download or delete SD card data and reboot. !!!WARNING!!! Cannot be undone! <br>
  <label for="FLUSHCONFIRM">1=Delete screen data, 2=Delete sensor data, 3=Download variables to SD. (1 and 2 will result in reboot) </label>
  <input type="text" id="FLUSHCONFIRM" name="FLUSHCONFIRM" value="0" maxlength="5"><br>
  <br>
  <button type="submit">FLUSH</button><br>
  </p></font></form>
  ---------------------<br>)===";


        
  serverTextWiFiForm();
  serverTextClose(200);


}


void handleRoot(void) {
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

void serverTextWiFiForm() {


  WEBHTML = WEBHTML + R"===(---------------------<br>
  <FORM action="/SETWIFI" method="post" target="_blank" id="WiFiSetForm">
  <p style="font-family:arial, sans-serif">
  To set or change WiFi, enter SSID and PWD.<br>
  <label for="SSID">WiFi SSID: </label>
  <input type="text" id="SSID" name="SSID" value="" maxlength="32"><br>  
  <label for="PWD">WiFi PWD: </label>
  <input type="text" id="PWD" name="PWD" value="" maxlength="64"><br>    
  <br>
  <button type="submit">STORE</button><br>
  </p></font></form>
  ---------------------<br>
  <br>

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
  SnsType* sensor = Sensors.getSensorBySnsIndex(j);    
  if (sensor) {

    WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) IPToString(Sensors.getDeviceIPBySnsIndex(j)) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) IPToString(Sensors.getDeviceIPBySnsIndex(j)) + "</a></td>";
    WEBHTML = WEBHTML + "<td>" + (String) MACToString(Sensors.getDeviceMACBySnsIndex(j)) + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) sensor->snsName + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) sensor->snsValue + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) sensor->snsType+"."+ (String) sensor->snsID + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) bitRead(sensor->Flags,0) + (String) (bitRead(sensor->Flags,6) ? "*" : "" ) + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) dateify(sensor->timeLogged,"mm/dd hh:nn:ss") + "</td>";
    WEBHTML = WEBHTML + "<td>" + (String) ((sensor->expired==0)?((bitRead(sensor->Flags,7))?"N*":"n"):((bitRead(sensor->Flags,7))?"<font color=\"#EE4B2B\">Y*</font>":"<font color=\"#EE4B2B\">y</font>")) + "</td>";
    
    byte delta=2;
    if (sensor->snsType==4 || sensor->snsType==1 || sensor->snsType==10) delta = 10;
    if (sensor->snsType==3) delta = 1;
    if (sensor->snsType==9) delta = 3; //bmp
    if (sensor->snsType==60 || sensor->snsType==61) delta = 3; //batery
    if (sensor->snsType>=50 && sensor->snsType<60) delta = 15; //HVAC
    
    WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA_MOVINGAVERAGE?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&windowSize=1800&numPointsX=48\" target=\"_blank\" rel=\"noopener noreferrer\">AvgHx</a></td>";
    WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&N=50\" target=\"_blank\" rel=\"noopener noreferrer\">History</a></td>";
    WEBHTML = WEBHTML + "</tr>";
  }
}



void handlerForRoot(bool allsensors) {
  
  LAST_WEB_REQUEST = I.currentTime; //this is the time of the last web request
  WEBHTML = "";
  serverTextHeader();
  WEBHTML = WEBHTML + "<h2>" + (String) MYNAME + " Main Page</h2>";

  if (Prefs.HAVECREDENTIALS==true) {



  #ifdef _USEROOTCHART
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";
  #endif
  WEBHTML = WEBHTML + "<body>";  
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

 
  WEBHTML = WEBHTML + "---------------------<br>";      

  if (I.isFlagged || I.isHeat || I.isAC) {
    WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
    WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">System Status</th>";
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">Server Status</th>";
    WEBHTML = WEBHTML + "</tr>";
    
    // First row - Configuration link and Heat status
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "Heat is " + (I.isHeat ? "on" : "off");
    
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/CONFIG\">Configuration</a></td></tr>";
    
    // Second row - Weather data link and AC status
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "AC is " + (I.isAC ? "on" : "off");
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WEATHER\">Weather Data</a></td></tr>";
    
    // Third row - Critical flags
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Critical flags: " + (String) I.isFlagged + "</td></td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WiFiConfig\">WiFiConfig</a></td></tr>";
    
    // Fourth row - Leak detected
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Leak detected: " + (String) I.isLeak + "</td></td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/STATUS\">Status</a></td></tr>";
    
    // Fifth row - Hot rooms
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Interior rooms over max temp: " + (String) I.isHot + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
    
    // Sixth row - Cold rooms
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Interior rooms below min temp: " + (String) I.isCold + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
    
    // Seventh row - Dry plants
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Plants dry: " + (String) I.isSoilDry + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
    
    // Eighth row - Expired sensors
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Critical sensors expired: " + (String) I.isExpired + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
    
    
    
    WEBHTML = WEBHTML + "</table>";
  }

  WEBHTML = WEBHTML + "---------------------<br>";      


    byte used[SENSORNUM];
    for (byte j=0;j<SENSORNUM;j++)  {
      used[j] = 255;
    }

    byte usedINDEX = 0;  


  WEBHTML = WEBHTML + "<p><table id=\"Logs\" style=\"width:900px\">";      
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><p><button onclick=\"sortTable(0)\">IP Address</button></p></th style=\"width:50px\"><th>ArdID</th><th style=\"width:200px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:100px\"><button onclick=\"sortTable(4)\">Sns Type</button></p></th style=\"width:100px\"><th><button onclick=\"sortTable(5)\">Flagged</button></p></th><th style=\"width:250px\">Last Recvd</th><th style=\"width:50px\">EXP</th><th style=\"width:100px\">Plot Avg</th><th style=\"width:100px\">Plot Raw</th></tr>"; 
  for (byte j=0;j<SENSORNUM;j++)  {
    SnsType* sensor = Sensors.getSensorBySnsIndex(j);    
    if (sensor) {
      if (allsensors && bitRead(sensor->Flags,1)==0) continue;
      if (sensor->snsID>0 && sensor->snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
        used[usedINDEX++] = j;
        rootTableFill(j);
        
        for (byte jj=j+1;jj<SENSORNUM;jj++) {
          SnsType* sensor2 = Sensors.getSensorBySnsIndex(jj);    
          if (sensor2) {
            if (allsensors && bitRead(sensor2->Flags,1)==0) continue;
            if (sensor2->snsID>0 && sensor2->snsType>0 && inIndex(jj,used,SENSORNUM) == false && sensor2->deviceIndex==sensor->deviceIndex) {
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

}

serverTextWiFiForm();


serverTextClose(200);

}

void handleFLUSHSD() {
  if (server.args()==0) return;
  if (server.argName(0)=="FLUSHCONFIRM") {

    if (server.arg(0).toInt()== 3) {
      I.isUpToDate = false;
      Sensors.isUpToDate = false;
      Prefs.isUpToDate = false;
    } else {    
      if (server.arg(0).toInt()== 1) {
        deleteFiles("ScreenFlags.dat","/Data");
      }
      if (server.arg(0).toInt()== 2) {
        deleteFiles("SensorBackupv2.dat","/Data");
      }
      WEBHTML = "text/plain","OK, about to reset";
      
      serverTextClose(200,false);
      
      controlledReboot("User Request",RESET_USER);
    }
  }

  server.sendHeader("Location", "/");//This Line goes to root page

  WEBHTML= "Attempted.";  
  serverTextClose(302,false);

}



void handleNotFound(){
  WEBHTML= "404: Not found"; // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
  serverTextClose(404,false);

}

void handleRETRIEVEDATA() {
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
  uint32_t sampn=0; //sample number
  
  bool success=false;
  if (starttime>0)  success = retrieveSensorDataFromSD(deviceMAC, snsType, snsID, &N, t, v, starttime, endtime,true);
  else    success = retrieveSensorDataFromSD(deviceMAC, snsType, snsID, &N, t, v, 0,endtime,true); //this fills from tn backwards to N*delta samples

  if (success == false)  {
    WEBHTML= "Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  SerialPrint("handleRETRIEVEDATA: N=" + (String) N + " t[0]=" + (String) t[0] + " t[N-1]=" + (String) t[N-1] + " v[0]=" + (String) v[0] + " v[N-1]=" + (String) v[N-1]);
  
  addPlotToHTML(t,v,N,deviceMAC,snsType,snsID);
}

void handleRETRIEVEDATA_MOVINGAVERAGE() {
  uint32_t starttime=0, endtime=0;
  uint32_t windowSizeN=0;
  uint16_t numPointsX=0;
  uint64_t deviceMAC = 0;
  uint8_t snsType = 0, snsID = 0;

  bool success=false;


  if (server.args()==0) {
    WEBHTML="Inappropriate call... use RETRIEVEDATA_MOVINGAVERAGE?MAC=1234567890AB&type=1&id=1&starttime=1234567890&endtime=1731761847&windowSize=10&numPointsX=10 or RETRIEVEDATA_MOVINGAVERAGE?MAC=1234567890AB&type=1&id=1&endtime=1731761847&windowSize=10&numPointsX=10";
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

  success = retrieveMovingAverageSensorDataFromSD(deviceMAC, snsType, snsID, starttime, endtime, windowSizeN, &numPointsX, v, t,true);
  
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
  SnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);

  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) MYNAME + "</title>\n";
  WEBHTML =WEBHTML  + (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}";
  WEBHTML =WEBHTML  + (String) "body {  font-family: arial, sans-serif; }";
  WEBHTML =WEBHTML  + "</style></head>";
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h1>Pleasant Weather Server</h1>";
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
      WEBHTML += "[" + (String) t[jj] + "," + (String) v[jj] + "]";
      if (jj<N-1) WEBHTML += ",";
      WEBHTML += "\n";
    }
    WEBHTML += "]);\n\n";

        // Set Options
    WEBHTML += "const options = {\n";
    if (sensor) {
      WEBHTML += "hAxis: {title: 'Historical data for " + (String) sensor->snsName + " in hours'}, \n";
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




void handlePost() {
    String responseMsg = "";
    String senderIP = "";
    uint64_t deviceMAC = 0;
    uint32_t deviceIP = 0;

    int16_t sensorIndex = -1;

    uint8_t ardID = 0; //legacy field, not used anymore
    String devName = ""; //optional field
    uint8_t snsType = 0;
    uint8_t snsID = 0;
    String snsName = "";
    double snsValue = 0;
    uint32_t timeRead = 0;
    uint32_t timeLogged = I.currentTime;
    uint32_t sendingInt = 0;
    uint8_t flags = 0;

    // Check if this is JSON data (new format)
    if (server.hasArg("JSON")) {
        String postData = server.arg("JSON");
        SerialPrint("Received POST data in JSON format: " + postData,true);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, postData);
        if (!error) {
            // New-style MAC-based messages
            if (doc["ip"].is<JsonVariantConst>()==false && doc["mac"].is<JsonVariantConst>()==false) {
              SerialPrint("Invalid JSON message format",true);
              responseMsg = "Invalid JSON message format";
            } else {
              if (doc["ip"].is<JsonVariantConst>()) {
                String ipStr = doc["ip"];
                deviceIP = IPAddress().fromString(ipStr);
              }
              if (doc["mac"].is<JsonVariantConst>()) {
                String macStr = doc["mac"];
                deviceMAC = strtoull(macStr.c_str(), NULL, 16);
              } else {
                deviceMAC = IPToMACID(deviceIP);
              }

              if (doc["sensor"].is<JsonArray>()) {
                  JsonArray sensorsArray = doc["sensor"];
                  for (JsonObject sensor : sensorsArray) {
                      if (sensor["type"].is<JsonVariantConst>() && sensor["id"].is<JsonVariantConst>() && 
                          sensor["name"].is<JsonVariantConst>() && sensor["value"].is<JsonVariantConst>()) {
                          if (sensor["devName"].is<JsonVariantConst>()) {
                            devName = sensor["devName"].as<String>();
                          }
                          snsType = sensor["type"];
                          snsID = sensor["id"];
                          snsName = sensor["name"].as<String>();
                          snsValue = sensor["value"];
                          timeRead = sensor["timeRead"].is<JsonVariantConst>() ? sensor["timeRead"] : 0;
                          timeLogged = sensor["timeLogged"].is<JsonVariantConst>() ? sensor["timeLogged"] : I.currentTime;
                          sendingInt = sensor["sendingInt"].is<JsonVariantConst>() ? sensor["sendingInt"] : 3600;
                          flags = sensor["flags"].is<JsonVariantConst>() ? sensor["flags"] : 0;                            
                      }
                  }
              } else {
                deviceMAC=0;
                deviceIP=0;
                snsType = 0;
                SerialPrint("Invalid JSON message format",true);
                responseMsg = "Invalid JSON message format";
              }
             
            }
        } else {
          SerialPrint("Could not deserialize JSON message",true);
          responseMsg = "Could not deserialize JSON message";
        }
    } else {
        // Handle old HTML form data (backward compatibility)
        if (server.args() > 0) {
          
          // Parse old-style HTML form parameters
          
          for (byte k = 0; k < server.args(); k++) {
              String argName = server.argName(k);
              String argValue = server.arg(k);
              
              if (argName == "MAC") {
                  deviceMAC = strtoull(argValue.c_str(), NULL, 16);
              } else if (argName == "IP") {
                argValue.replace(",",""); //remove unneeded erroneous commas //this was an error in the old code
                deviceIP = StringToIP(argValue);              
              } else if (argName == "logID") { //legacy code, still used by some devices
                  ardID = breakString(&argValue,".",true).toInt(); //this is a legacy field, not used anymore
                  snsType = breakString(&argValue,".",true).toInt();
                  snsID = argValue.toInt();
              } else if (argName == "devName") {
                  devName = argValue;
              } else if (argName == "snsType") {
                  snsType = argValue.toInt();
              } else if (argName == "snsID") {
                  snsID = argValue.toInt();
              } else if (argName == "varName"|| argName == "snsName") {
                  snsName = argValue;
              } else if (argName == "varValue" || argName == "snsValue") {
                  snsValue = argValue.toDouble();
              } else if (argName == "timeRead") {
                  timeRead = argValue.toInt();
              } else if (argName == "timeLogged") {
                  timeLogged = argValue.toInt();
              } else if (argName == "SendingInt") {
                  sendingInt = argValue.toInt();
              } else if (argName == "Flags") {
                  flags = argValue.toInt();
              }
          }

          
          if (deviceMAC==0 && deviceIP!=0) deviceMAC = IPToMACID(deviceIP);
          if (deviceMAC==0 && deviceIP==0) {
            snsType = 0;
            SerialPrint("No MAC or IP provided",true);
            responseMsg = "No MAC or IP provided";
          }
          if (snsType == 0 || snsName.length() == 0 || (deviceIP==0 && deviceMAC==0)) {
            snsType = 0;
            deviceMAC = 0;
            deviceIP = 0;
            SerialPrint("sensor missing critical elements, skipping.\n");
            responseMsg = "Missing required fields: snsType and varName";
          }  

        } else {
          SerialPrint("No data received",true);
          responseMsg = "No valid HTML form data received";
        }
    }
    
    if (deviceMAC!=0 || deviceIP!=0 || snsType!=0 || snsName.length()!=0) {

        // Add sensor to Devices_Sensors class
        sensorIndex = Sensors.addSensor(deviceMAC, deviceIP, snsType, snsID, 
                                              snsName.c_str(), snsValue, 
                                              timeRead, timeLogged, sendingInt, flags, devName.c_str());
        if (sensorIndex < 0) {
          SerialPrint("Failed to add sensor",true);
          responseMsg = "Failed to add sensor";
        } else {
          //message was successfully added to Devices_Sensors class
          responseMsg = "OK";
        }
    }

    if (sensorIndex >= 0) {
      //message was successfully added to Devices_Sensors class
      // Store individual sensor data to SD card
        storeSensorDataSD(sensorIndex);
        //SerialPrint((String) "handlePost: Added sensor data to SD card: index " + sensorIndex + ", MAC =" + deviceMAC + ", IP =" + deviceIP + ", type=" + snsType + ", id=" + snsID + ", name=" + snsName + ", value=" + snsValue + "\n");
        server.send(200, "text/plain", responseMsg);
    } else {
        SerialPrint("Failed to add sensor with response message: " + responseMsg + "\n");
        server.send(500, "text/plain", responseMsg);
    }
    return;

}

void handleCONFIG() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) MYNAME + " Configuration Page</h2>";
  
  // Start the form for editable fields
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/CONFIG\">";
  
  // Start the table
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Field Name</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Description</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Value</th>";
  WEBHTML = WEBHTML + "</tr>";

  // Non-editable fields in specified order
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">currentTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Current system time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.currentTime ? dateify(I.currentTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">ALIVESINCE</td><td style=\"border: 1px solid #ddd; padding: 8px;\">System alive since</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.ALIVESINCE ?  dateify(I.ALIVESINCE) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastResetTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last system reset time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastResetTime ?  dateify(I.lastResetTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">wifiFailCount</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Number of WiFi connection failures</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.wifiFailCount + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastError</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last error message</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastError) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastErrorTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last error occurrence time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastErrorTime ?  dateify(I.lastErrorTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastErrorCode</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last error code</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.lastErrorCode + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastESPNOW</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last ESPNow communication time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastESPNOW  ?  dateify(I.lastESPNOW ) : "???")+ "</td></tr>";

  // Editable fields in specified order
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">GLOBAL_TIMEZONE_OFFSET</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Global timezone offset in seconds</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"GLOBAL_TIMEZONE_OFFSET\" value=\"" + (String) I.GLOBAL_TIMEZONE_OFFSET + "\" style=\"width: 100%;\"></td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">DSTOFFSET</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Daylight Saving Time offset in seconds</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"DSTOFFSET\" value=\"" + (String) I.DSTOFFSET + "\" style=\"width: 100%;\"></td></tr>";
  
  // CYCLE fields
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">cycleHeaderMinutes</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Minutes between header updates</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"cycleHeaderMinutes\" value=\"" + (String) I.cycleHeaderMinutes + "\" style=\"width: 100%;\"></td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">cycleWeatherMinutes</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Minutes between redraw of main weather panel</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"cycleWeatherMinutes\" value=\"" + (String) I.cycleWeatherMinutes + "\" style=\"width: 100%;\"></td></tr>";
WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">cycleCurrentConditionMinutes</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Minutes between redraw of today conditions</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"cycleCurrentConditionMinutes\" value=\"" + (String) I.cycleCurrentConditionMinutes + "\" style=\"width: 100%;\"></td></tr>";
WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">cycleFutureConditionsMinutes</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Minutes between redraw of future conditions</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"cycleFutureConditionsMinutes\" value=\"" + (String) I.cycleFutureConditionsMinutes + "\" style=\"width: 100%;\"></td></tr>";
WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">cycleFlagSeconds</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Seconds to show flag information</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"cycleFlagSeconds\" value=\"" + (String) I.cycleFlagSeconds + "\" style=\"width: 100%;\"></td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">IntervalHourlyWeather</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Hours between hourly weather displays</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"IntervalHourlyWeather\" value=\"" + (String) I.IntervalHourlyWeather + "\" style=\"width: 100%;\"></td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">screenChangeTimer</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Screen change timer value</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><input type=\"number\" name=\"screenChangeTimer\" value=\"" + (String) I.screenChangeTimer + "\" style=\"width: 100%;\"></td></tr>";

  // Remaining fields from I in order of appearance
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">resetInfo</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Reset cause information</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.resetInfo + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">rebootsSinceLast</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Number of reboots since last reset</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.rebootsSinceLast + "</td></tr>";

  #ifdef _USETFT
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">CLOCK_Y</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Clock Y position</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.CLOCK_Y + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">HEADER_Y</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Header Y position</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.HEADER_Y + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastHeaderTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last header update time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (String) (I.lastHeaderTime ? dateify(I.lastHeaderTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastWeatherTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last weather update time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastWeatherTime ? dateify(I.lastWeatherTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastClockDrawTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last clock update time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastClockDrawTime ? dateify(I.lastClockDrawTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastFlagViewTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last flag view time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastFlagViewTime ? dateify(I.lastFlagViewTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastCurrentConditionTime</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last current condition update time</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.lastCurrentConditionTime ? dateify(I.lastCurrentConditionTime) : "???") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">IntervalHourlyWeather</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Hours between daily weather display</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.IntervalHourlyWeather + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">touchX</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Touch X coordinate</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.touchX + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">touchY</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Touch Y coordinate</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.touchY + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">line_clear</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Clear line position</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.line_clear + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">line_keyboard</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Keyboard line position</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.line_keyboard + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">line_submit</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Submit line position</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.line_submit + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">alarmIndex</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Alarm index</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.alarmIndex + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">ScreenNum</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Current screen number</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.ScreenNum + "</td></tr>";
  #endif

  #ifdef _USEWEATHER
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">currentTemp</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Current temperature</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.currentTemp + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Tmax</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Maximum temperature</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.Tmax + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Tmin</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Minimum temperature</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.Tmin + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">localWeatherIndex</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Local weather sensor index</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.localWeatherIndex + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">lastCurrentTemp</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Last current temperature</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.lastCurrentTemp + "</td></tr>";
  #endif

  #ifdef _USEBATTERY
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">localBatteryIndex</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Local battery sensor index</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.localBatteryIndex + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">localBatteryLevel</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Local battery level</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.localBatteryLevel + "</td></tr>";
  #endif

  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isExpired</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Are any critical sensors expired</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.isExpired ? "true" : "false") + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isFlagged</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Number of flagged sensors</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isFlagged + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">wasFlagged</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Previous flagged state</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) (I.wasFlagged ? "true" : "false") + "</td></tr>";

  #ifndef _ISPERIPHERAL
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isHeat</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Heat system status</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isHeat + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isAC</td><td style=\"border: 1px solid #ddd; padding: 8px;\">AC system status</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isAC + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isFan</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Fan system status</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isFan + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">wasHeat</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Previous heat status</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.wasHeat + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">wasAC</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Previous AC status</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.wasAC + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">wasFan</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Previous fan status</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.wasFan + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isHot</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Hot room count</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isHot + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isCold</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Cold room count</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isCold + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isSoilDry</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Dry plant count</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isSoilDry + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">isLeak</td><td style=\"border: 1px solid #ddd; padding: 8px;\">Leak count</td><td style=\"border: 1px solid #ddd; padding: 8px;\">" + (String) I.isLeak + "</td></tr>";
  #endif

  WEBHTML = WEBHTML + "</table>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Delete SD card contents button
  WEBHTML = WEBHTML + "<br><br><form method=\"POST\" action=\"/CONFIG_DELETE\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete SD Card Contents\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<br><a href=\"/\">Back to Main Page</a>";
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
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
  if (I.WiFiMode != WIFI_AP_STA) {
    *wifiID = generateAPSSID();
    byte lastByte = getPROCIDByte(Prefs.PROCID, 5); // Last byte of MAC for IP address
    *apIP = IPAddress(192, 168, 4, lastByte);
    WiFi.mode(WIFI_AP_STA); // Combined AP and station mode
    I.WiFiMode = WIFI_AP_STA;
    *wifiPWD = "S3nsor.N3t!";
    WiFi.softAPConfig(*apIP, *apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(wifiID->c_str(), wifiPWD->c_str());
    delay(100);
  }
}

void handleCONFIG_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Process form submissions and update editable fields
  if (server.hasArg("GLOBAL_TIMEZONE_OFFSET")) {
    I.GLOBAL_TIMEZONE_OFFSET = server.arg("GLOBAL_TIMEZONE_OFFSET").toInt();
  }
  if (server.hasArg("DSTOFFSET")) {
    I.DSTOFFSET = server.arg("DSTOFFSET").toInt();
  }
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



  // Save the updated configuration to persistent storage
  I.isUpToDate = false;
  
  // Redirect back to the configuration page
  server.sendHeader("Location", "/CONFIG");
  server.send(302, "text/plain", "Configuration updated. Redirecting...");
}

void handleCONFIG_DELETE() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Delete the ScreenFlags.dat file from the /Data directory
  deleteFiles("ScreenFlags.dat", "/Data");
  
  // Redirect back to the configuration page with a success message
  server.sendHeader("Location", "/CONFIG");
  server.send(302, "text/plain", "SD card contents deleted. Redirecting...");
}

void handleWiFiConfig() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) MYNAME + " WiFi Configuration</h2>";
  
  // Display current WiFi status and configuration
  WEBHTML = WEBHTML + "<h3>Current WiFi Status</h3>";
  WEBHTML = WEBHTML + "<p><strong>WiFi Status:</strong> " + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "</p>";
  if (WiFi.status() == WL_CONNECTED) {
    WEBHTML = WEBHTML + "<p><strong>Connected to:</strong> " + WiFi.SSID() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
    WEBHTML = WEBHTML + "<p><strong>Signal Strength:</strong> " + (String) WiFi.RSSI() + " dBm</p>";
  }
  WEBHTML = WEBHTML + "<p><strong>Stored SSID:</strong> " + String((char*)Prefs.WIFISSID) + "</p>";
  WEBHTML = WEBHTML + "<p><strong>Stored LMK Key:</strong> " + String((char*)Prefs.LMK_KEY) + "</p>";
  
  // WiFi configuration form
  WEBHTML = WEBHTML + "<h3>Configure WiFi</h3>";
  WEBHTML = WEBHTML + "<form id=\"wifiForm\" method=\"POST\" action=\"/WiFiConfig\">";
  WEBHTML = WEBHTML + "<p><label for=\"ssid\">SSID:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"ssid\" name=\"ssid\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"password\">Password:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"password\" id=\"password\" name=\"password\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"lmk_key\">LMK Key:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"lmk_key\" name=\"lmk_key\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<input type=\"hidden\" id=\"BLOB\" name=\"BLOB\">";
  WEBHTML = WEBHTML + "<input type=\"button\" value=\"Update WiFi Configuration\" onclick=\"submitEncryptedForm()\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // JavaScript for AES128 encryption and form submission
  WEBHTML = WEBHTML + "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.1.1/crypto-js.min.js\"></script>";
  WEBHTML = WEBHTML + "<script>";
  WEBHTML = WEBHTML + "function submitEncryptedForm() {";
  WEBHTML = WEBHTML + "  var ssid = document.getElementById('ssid').value;";
  WEBHTML = WEBHTML + "  var password = document.getElementById('password').value;";
  WEBHTML = WEBHTML + "  var lmk_key = document.getElementById('lmk_key').value;";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  if (!ssid || !password || !lmk_key) {";
  WEBHTML = WEBHTML + "    alert('Please fill in all fields');";
  WEBHTML = WEBHTML + "    return;";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Create data string with CRC";
  WEBHTML = WEBHTML + "  var dataString = ssid + '|' + password + '|' + lmk_key;";
  WEBHTML = WEBHTML + "  var crc = 0;";
  WEBHTML = WEBHTML + "  for (var i = 0; i < dataString.length; i++) {";
  WEBHTML = WEBHTML + "    crc += dataString.charCodeAt(i);";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  dataString += '|' + crc;";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Convert string to bytes";
  WEBHTML = WEBHTML + "  var dataBytes = [];";
  WEBHTML = WEBHTML + "  for (var i = 0; i < dataString.length; i++) {";
  WEBHTML = WEBHTML + "    dataBytes.push(dataString.charCodeAt(i));";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Pad to 16-byte boundary";
  WEBHTML = WEBHTML + "  var padding = 16 - (dataBytes.length % 16);";
  WEBHTML = WEBHTML + "  for (var i = 0; i < padding; i++) {";
  WEBHTML = WEBHTML + "    dataBytes.push(0);";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Convert to WordArray for CryptoJS";
  WEBHTML = WEBHTML + "  var dataWordArray = CryptoJS.lib.WordArray.create(dataBytes);";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Generate random IV";
  WEBHTML = WEBHTML + "  var iv = CryptoJS.lib.WordArray.random(16);";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // AES key for WiFi configuration (dedicated key)";
  WEBHTML = WEBHTML + "  var key = CryptoJS.enc.Utf8.parse('" + (String)WIFI_CONFIG_KEY +  "');";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Encrypt using AES-CBC";
  WEBHTML = WEBHTML + "  var encrypted = CryptoJS.AES.encrypt(dataWordArray, key, {";
  WEBHTML = WEBHTML + "    iv: iv,";
  WEBHTML = WEBHTML + "    mode: CryptoJS.mode.CBC,";
  WEBHTML = WEBHTML + "    padding: CryptoJS.pad.NoPadding";
  WEBHTML = WEBHTML + "  });";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Combine IV and encrypted data";
  WEBHTML = WEBHTML + "  var ivBytes = iv.words;";
  WEBHTML = WEBHTML + "  var encryptedBytes = encrypted.ciphertext.words;";
  WEBHTML = WEBHTML + "  var combinedBytes = [];";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Add IV bytes";
  WEBHTML = WEBHTML + "  for (var i = 0; i < 4; i++) {";
  WEBHTML = WEBHTML + "    var word = ivBytes[i];";
  WEBHTML = WEBHTML + "    combinedBytes.push((word >>> 24) & 0xFF);";
  WEBHTML = WEBHTML + "    combinedBytes.push((word >>> 16) & 0xFF);";
  WEBHTML = WEBHTML + "    combinedBytes.push((word >>> 8) & 0xFF);";
  WEBHTML = WEBHTML + "    combinedBytes.push(word & 0xFF);";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Add encrypted data bytes";
  WEBHTML = WEBHTML + "  for (var i = 0; i < encryptedBytes.length; i++) {";
  WEBHTML = WEBHTML + "    var word = encryptedBytes[i];";
  WEBHTML = WEBHTML + "    combinedBytes.push((word >>> 24) & 0xFF);";
  WEBHTML = WEBHTML + "    combinedBytes.push((word >>> 16) & 0xFF);";
  WEBHTML = WEBHTML + "    combinedBytes.push((word >>> 8) & 0xFF);";
  WEBHTML = WEBHTML + "    combinedBytes.push(word & 0xFF);";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Convert to base64";
  WEBHTML = WEBHTML + "  var encryptedBlob = btoa(String.fromCharCode.apply(null, combinedBytes));";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  document.getElementById('BLOB').value = encryptedBlob;";
  WEBHTML = WEBHTML + "  document.getElementById('wifiForm').submit();";
  WEBHTML = WEBHTML + "}";
  WEBHTML = WEBHTML + "</script>";
  
  // Additional WiFi management options
  WEBHTML = WEBHTML + "<h3>WiFi Management</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WiFiConfig_RESET\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Reset WiFi Configuration\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<br><br><a href=\"/\">Back to Main Page</a>";
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleWiFiConfig_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Check if we have the encrypted BLOB
  if (server.hasArg("BLOB")) {
    String encryptedBlob = server.arg("BLOB");
    
    // Decode base64
    int decodedLength = base64_dec_len(encryptedBlob.c_str(), encryptedBlob.length());
    uint8_t* decodedData = new uint8_t[decodedLength];
    base64_decode(encryptedBlob.c_str(), decodedData);
    
    // Decrypt the data using BootSecure functions
    uint8_t* decryptedData = new uint8_t[decodedLength];
    int8_t decryptResult = BootSecure::decrypt(decodedData, (char*)WIFI_CONFIG_KEY, decryptedData, decodedLength);
    
    if (decryptResult == 0) {
      // Decryption successful, parse the data
      // Expected format: SSID|PASSWORD|LMK_KEY|CRC
      String decryptedString = String((char*)decryptedData);
      int pos1 = decryptedString.indexOf('|');
      int pos2 = decryptedString.indexOf('|', pos1 + 1);
      int pos3 = decryptedString.indexOf('|', pos2 + 1);
      
      if (pos1 > 0 && pos2 > pos1 && pos3 > pos2) {
        String ssid = decryptedString.substring(0, pos1);
        String password = decryptedString.substring(pos1 + 1, pos2);
        String lmk_key = decryptedString.substring(pos2 + 1, pos3);
        String crc = decryptedString.substring(pos3 + 1);
        
        // Verify CRC using BootSecure CRC calculator
        uint16_t calculatedCRC = BootSecure::CRCCalculator((uint8_t*)decryptedString.substring(0, pos3).c_str(), pos3);
        uint16_t receivedCRC = crc.toInt();
        
        if (calculatedCRC == receivedCRC) {
          // CRC is valid, update Prefs
          strncpy((char*)Prefs.WIFISSID, ssid.c_str(), sizeof(Prefs.WIFISSID) - 1);
          Prefs.WIFISSID[sizeof(Prefs.WIFISSID) - 1] = '\0';
          
          strncpy((char*)Prefs.WIFIPWD, password.c_str(), sizeof(Prefs.WIFIPWD) - 1);
          Prefs.WIFIPWD[sizeof(Prefs.WIFIPWD) - 1] = '\0';
          
          strncpy((char*)Prefs.LMK_KEY, lmk_key.c_str(), sizeof(Prefs.LMK_KEY) - 1);
          Prefs.LMK_KEY[sizeof(Prefs.LMK_KEY) - 1] = '\0';
          
          // Mark Prefs as needing update and save to NVS
          Prefs.isUpToDate = false;
          Prefs.HAVECREDENTIALS = true;
          
          // Save to NVS using BootSecure
            server.sendHeader("Location", "/WiFiConfig");
            server.send(302, "text/plain", "WiFi configuration updated successfully and saved to NVS. Attempting to connect...");
          
          // Attempt to connect to new WiFi
          WiFi.disconnect();
          delay(1000);
        } else {
          server.sendHeader("Location", "/WiFiConfig");
          server.send(302, "text/plain", "CRC validation failed. Configuration not updated.");
        }
      } else {
        server.sendHeader("Location", "/WiFiConfig");
        server.send(302, "text/plain", "Invalid data format. Configuration not updated.");
      }
    } else {
      server.sendHeader("Location", "/WiFiConfig");
      server.send(302, "text/plain", "Decryption failed. Configuration not updated.");
    }
    
    // Clean up
    BootSecure::zeroize(decodedData, decodedLength);
    BootSecure::zeroize(decryptedData, decodedLength);
    delete[] decodedData;
    delete[] decryptedData;
  } else {
    // No BLOB provided, redirect back
    server.sendHeader("Location", "/WiFiConfig");
    server.send(302, "text/plain", "No encrypted data provided. Please try again.");
  }
}

void handleWiFiConfig_RESET() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Clear WiFi credentials and LMK key
  memset(Prefs.WIFISSID, 0, sizeof(Prefs.WIFISSID));
  memset(Prefs.WIFIPWD, 0, sizeof(Prefs.WIFIPWD));
  memset(Prefs.LMK_KEY, 0, sizeof(Prefs.LMK_KEY));
  Prefs.HAVECREDENTIALS = false;
  Prefs.isUpToDate = false;
  
  // Disconnect from current WiFi
  WiFi.disconnect();
  
  // Redirect back to WiFi config page
  server.sendHeader("Location", "/WiFiConfig");
  server.send(302, "text/plain", "WiFi configuration and LMK key reset. Please reconfigure.");
}

void handleWeather() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) MYNAME + " Weather Data</h2>";
  
  // Display current weather data
  WEBHTML = WEBHTML + "<h3>Current Weather Information</h3>";
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
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.lat, 6) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Longitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.lon, 6) + "</td></tr>";
    
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
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Current Weather Icon</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(currentWeatherID) + "</td></tr>";
  
  WEBHTML = WEBHTML + "</table>";
  
  // Daily forecast for next 3 days
  WEBHTML = WEBHTML + "<h3>3-Day Forecast</h3>";
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Day</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Max Temp</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Min Temp</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Weather ID</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  for (int day = 0; day < 3; day++) {
    int8_t dailyT[2];
    WeatherData.getDailyTemp(day, dailyT);
    int16_t weatherID = WeatherData.getDailyWeatherID(day);
    
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Day " + String(day + 1) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[0]) + "°C</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[1]) + "°C</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(weatherID) + "</td></tr>";
  }
  
  WEBHTML = WEBHTML + "</table>";
  
  // Configuration form for lat/lon
  WEBHTML = WEBHTML + "<h3>Update Location</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/handleWeather\">";
  WEBHTML = WEBHTML + "<p><label for=\"lat\">Latitude:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"lat\" name=\"lat\" step=\"0.000001\" min=\"-90\" max=\"90\" value=\"" + String(WeatherData.lat, 14) + "\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"lon\">Longitude:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"lon\" name=\"lon\" step=\"0.000001\" min=\"-180\" max=\"180\" value=\"" + String(WeatherData.lon, 14) + "\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Update Location\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Address lookup form (updates lat/lon fields but doesn't submit)
  WEBHTML = WEBHTML + "<h3>Lookup by Lattitude and Longitude from Address</h3>";
  WEBHTML = WEBHTML + "<form id=\"addressForm\" onsubmit=\"return lookupAddress(event)\">";
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
  
  // JavaScript for address lookup
  WEBHTML = WEBHTML + "<script>";
  WEBHTML = WEBHTML + "function lookupAddress(event) {";
  WEBHTML = WEBHTML + "  event.preventDefault();";
  WEBHTML = WEBHTML + "  var street = document.getElementById('street').value.trim();";
  WEBHTML = WEBHTML + "  var city = document.getElementById('city').value.trim();";
  WEBHTML = WEBHTML + "  var state = document.getElementById('state').value.trim().toUpperCase();";
  WEBHTML = WEBHTML + "  var zipCode = document.getElementById('zipcode').value;";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Validate required fields";
  WEBHTML = WEBHTML + "  if (!street || !city || !state || !zipCode) {";
  WEBHTML = WEBHTML + "    alert('Please fill in all address fields');";
  WEBHTML = WEBHTML + "    return false;";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Validate state format";
  WEBHTML = WEBHTML + "  if (state.length !== 2 || !/^[A-Z]{2}$/.test(state)) {";
  WEBHTML = WEBHTML + "    alert('State must be a 2-letter code (e.g., MA, NY, CA)');";
  WEBHTML = WEBHTML + "    return false;";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Validate ZIP code format";
  WEBHTML = WEBHTML + "  if (zipCode.length !== 5 || !/^\\d{5}$/.test(zipCode)) {";
  WEBHTML = WEBHTML + "    alert('ZIP code must be 5 digits');";
  WEBHTML = WEBHTML + "    return false;";
  WEBHTML = WEBHTML + "  }";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Show loading message";
  WEBHTML = WEBHTML + "  var submitBtn = event.target.querySelector('input[type=\"submit\"]');";
  WEBHTML = WEBHTML + "  var originalText = submitBtn.value;";
  WEBHTML = WEBHTML + "  submitBtn.value = 'Looking up...';";
  WEBHTML = WEBHTML + "  submitBtn.disabled = true;";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Make AJAX request to lookup coordinates";
  WEBHTML = WEBHTML + "  var xhr = new XMLHttpRequest();";
  WEBHTML = WEBHTML + "  xhr.open('POST', '/WeatherAddress', true);";
  WEBHTML = WEBHTML + "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  xhr.onreadystatechange = function() {";
  WEBHTML = WEBHTML + "    if (xhr.readyState === 4) {";
  WEBHTML = WEBHTML + "      submitBtn.value = originalText;";
  WEBHTML = WEBHTML + "      submitBtn.disabled = false;";
  WEBHTML = WEBHTML + "      ";
  WEBHTML = WEBHTML + "      if (xhr.status === 200) {";
  WEBHTML = WEBHTML + "        try {";
  WEBHTML = WEBHTML + "          var response = JSON.parse(xhr.responseText);";
  WEBHTML = WEBHTML + "          if (response.success) {";
  WEBHTML = WEBHTML + "            // Update the lat/lon fields in the main form";
  WEBHTML = WEBHTML + "            document.getElementById('lat').value = response.latitude;";
  WEBHTML = WEBHTML + "            document.getElementById('lon').value = response.longitude;";
  WEBHTML = WEBHTML + "            alert('Coordinates updated! Please review and submit the main form to apply changes.');";
  WEBHTML = WEBHTML + "          } else {";
  WEBHTML = WEBHTML + "            alert('Address lookup failed: ' + response.message);";
  WEBHTML = WEBHTML + "          }";
  WEBHTML = WEBHTML + "        } catch (e) {";
  WEBHTML = WEBHTML + "          alert('Error parsing response from server');";
  WEBHTML = WEBHTML + "        }";
  WEBHTML = WEBHTML + "      } else {";
  WEBHTML = WEBHTML + "        alert('Address lookup failed. Please try again.');";
  WEBHTML = WEBHTML + "      }";
  WEBHTML = WEBHTML + "    }";
  WEBHTML = WEBHTML + "  };";
  WEBHTML = WEBHTML + "  ";
  WEBHTML = WEBHTML + "  // Build form data";
  WEBHTML = WEBHTML + "  var formData = 'street=' + encodeURIComponent(street) + '&city=' + encodeURIComponent(city) + '&state=' + encodeURIComponent(state) + '&zipcode=' + encodeURIComponent(zipCode);";
  WEBHTML = WEBHTML + "  xhr.send(formData);";
  WEBHTML = WEBHTML + "  return false;";
  WEBHTML = WEBHTML + "}";
  WEBHTML = WEBHTML + "</script>";
  
  // Refresh weather button
  WEBHTML = WEBHTML + "<h3>Weather Actions</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WeatherRefresh\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Refresh Weather Now\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "</body>";
  WEBHTML = WEBHTML + "</html>";
  
  serverTextClose(200, true);
}

void handleWeather_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Handle location update
  if (server.hasArg("lat") && server.hasArg("lon")) {
    double newLat = server.arg("lat").toDouble();
    double newLon = server.arg("lon").toDouble();
    
    // Validate coordinates
    if (newLat >= -90.0 && newLat <= 90.0 && newLon >= -180.0 && newLon <= 180.0) {
      WeatherData.lat = newLat;
      WeatherData.lon = newLon;
      
      // Force weather update with new coordinates
      WeatherData.updateWeather(0);
      
      server.sendHeader("Location", "/WEATHER");
      server.send(302, "text/plain", "Location updated successfully. Weather data refreshed.");
      Prefs.isUpToDate = false;
    } else {
      server.sendHeader("Location", "/WEATHER");
      server.send(302, "text/plain", "Invalid coordinates. Please check your input.");
    }
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Missing coordinates. Please try again.");
  }
}

void handleWeatherRefresh() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Force immediate weather update
  bool updateResult = WeatherData.updateWeather(0);
  
  if (updateResult) {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather data refreshed successfully.");
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather update failed. Please try again.");
  }
}

void handleWeatherZip() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
    double latitude, longitude;
    bool success = WeatherData.getCoordinatesFromZipCode(zipCode, latitude, longitude);
    
    if (success) {
      // Return JSON response with coordinates (don't update WeatherData yet)
      String jsonResponse = "{\"success\":true,\"latitude\":" + String(latitude, 14) + ",\"longitude\":" + String(longitude, 14) + "}";
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
  LAST_WEB_REQUEST = I.currentTime;
  
  // Handle complete address lookup
  if (server.hasArg("street") && server.hasArg("city") && server.hasArg("state") && server.hasArg("zipcode")) {
    String street = server.arg("street");
    String city = server.arg("city");
    String state = server.arg("state");
    String zipCode = server.arg("zipcode");
    
    // Validate required fields
    if (street.length() == 0 || city.length() == 0 || state.length() == 0 || zipCode.length() == 0) {
      server.sendHeader("Content-Type", "application/json");
      server.send(400, "application/json", "{\"success\":false,\"message\":\"All address fields are required.\"}");
      return;
    }
    
    // Validate state format (2 letters)
    if (state.length() != 2) {
      server.sendHeader("Content-Type", "application/json");
      server.send(400, "application/json", "{\"success\":false,\"message\":\"State must be a 2-letter code.\"}");
      return;
    }
    
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
    
    // Lookup coordinates from complete address
    double latitude, longitude;
    bool success = WeatherData.getCoordinatesFromAddress(street, city, state, zipCode, latitude, longitude);
    
    if (success) {
      // Return JSON response with coordinates (don't update WeatherData yet)
      String jsonResponse = "{\"success\":true,\"latitude\":" + String(latitude, 14) + ",\"longitude\":" + String(longitude, 14) + "}";
      server.sendHeader("Content-Type", "application/json");
      server.send(200, "application/json", jsonResponse);
    } else {
      server.sendHeader("Content-Type", "application/json");
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Address lookup failed. Please check the address or try again later.\"}");
    }
  } else {
    server.sendHeader("Content-Type", "application/json");
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing required address fields. Please provide street, city, state, and ZIP code.\"}");
  }
}


