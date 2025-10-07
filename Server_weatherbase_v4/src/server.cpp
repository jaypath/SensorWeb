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

#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
extern STRUCT_GOOGLESHEET GSheetInfo;
#endif


bool RegistrationCompleted = false;


#define WIFI_CONFIG_KEY "Kj8mN2pQ9rS5tU7vW3xY1zA4bC6dE8fG" //key to encrypt/decrypt wifi config

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
byte CURRENT_DEVICE_VIEWER = 0;  // Track current device in device viewer

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
    I.WiFiMode = WIFI_STA;
    Prefs.MYIP =   WiFi.localIP(); 

    return true;
  }

    return false;
  
}

int16_t tryWifi(uint16_t delayms ) {
  //return -1000 if no credentials, or connection attempts if success, or -1*number of attempts if failure
  int16_t retries = 0;
  int16_t i = 0;
  if (Prefs.HAVECREDENTIALS) {
      // Use station-only mode for normal operation
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
  
  int16_t retries = 0;
  if (Prefs.HAVECREDENTIALS) {
    retries = tryWifi(250);
    if (retries>0) return retries;
    Prefs.isUpToDate = false;
  }

  if (retries == -1000 || Prefs.HAVECREDENTIALS == false) {
    SerialPrint("No credentials, starting AP Station Mode",true);
    APStation_Mode();
    return -1000;
  }

  return retries;
}

void APStation_Mode() {
  I.WiFiMode = WIFI_AP_STA;
  
  //wifi ID and pwd will be set in connectsoftap
  String wifiPWD;
  String wifiID;
  IPAddress apIP;
  

  SerialPrint("Init AP Station Mode... Please wait... ",false);

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
    tft.printf("Setup may be required \n(or wifi down). Please join\n\nWiFi: %s\npwd: %s\n\nand go to website:\n\n%s\n\n", 
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
      #ifdef _USETFT
      //wait for every 5 seconds
      if (m - last60Seconds > 5000) {
        last60Seconds = m;

        //clear the bottom left of the screen
        tft.fillRect(0, tft.height()-100, tft.width(), 100, TFT_BLACK);
        tft.setCursor(0, tft.height()-90);
        tft.setTextColor(TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.printf("Seconds before reboot: %ds", ((timeoutMs - (m - startTime))/1000));
      }
      #endif

      server.handleClient(); //check if user has set up WiFi
            
      // Check for timeout
      if ( m - startTime > timeoutMs) {
        #ifdef _USETFT
        tft.println("Timeout waiting for credentials. Rebooting...");        
        #endif

        //if credentials are stored, but havecredentials was false, then try rebooting with those credentials
        if (Prefs.WIFISSID[0] != 0 && Prefs.WIFIPWD[0] != 0) {
          Prefs.HAVECREDENTIALS = true;
          Prefs.isUpToDate = false;
        }
        tft.println("Rebooting due to timeout...");
        delay(5000);
        controlledReboot("WiFi credentials timeout", RESET_WIFI, true);
        break;
      }

    } while (Prefs.HAVECREDENTIALS == false);
    
  // Only reboot if we have credentials but registration is not completed
  if ( RegistrationCompleted == false) {
    // User has submitted WiFi credentials but hasn't completed timezone setup
    // Continue running the server to allow timezone configuration
    tft.println("WiFi credentials saved. Please configure timezone settings.");
    tft.println("Server will continue running for timezone setup...");
    
    // Keep the server running indefinitely until timezone setup is complete
    while (RegistrationCompleted == false) {
      server.handleClient();
      delay(50);
    }
  }

  
  BootSecure bootSecure;
  if (bootSecure.setPrefs()<0) {
    SerialPrint("Failed to store core data",true);
  }

  tft.println("Rebooting with updated credentials...");
  delay(5000);
  controlledReboot("WIFI CREDENTIALS RESET",RESET_NEWWIFI);
   
}


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
    String URL = device->IP.toString() + "/UPDATEALLSENSORREADS";
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




void handleSTATUS() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";  
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Status Page</h2>";
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

  WEBHTML += "Number of devices/sensors: " + (String) Sensors.getNumDevices() + " / " + (String) Sensors.getNumSensors() + "<br>";
  WEBHTML = WEBHTML + "Alive since: " + dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss") + "<br>";
  WEBHTML = WEBHTML + "Last error: " + (String) I.lastError + " @" + (String) (I.lastErrorTime ? dateify(I.lastErrorTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML = WEBHTML + "Last known reset: " + (String) lastReset2String()  + " ";
  WEBHTML = WEBHTML + "<a href=\"/REBOOT_DEBUG\" target=\"_blank\" style=\"display: inline-block; padding: 5px 10px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px; cursor: pointer; font-size: 12px;\">Debug</a><br>";
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML = WEBHTML + "Last LAN Incoming Message Type: " + (String) I.ESPNOW_LAST_INCOMINGMSG_TYPE + "<br>";
  WEBHTML = WEBHTML + "Last LAN Incoming Message Sent at: " +  (String) (I.ESPNOW_LAST_INCOMINGMSG_TIME ? dateify(I.ESPNOW_LAST_INCOMINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML = WEBHTML + "Last LAN Incoming Message Sender: " + (String) MACToString(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC) + "<br>";
  WEBHTML = WEBHTML + "Last LAN Incoming Message Sender IP: " + (String) I.ESPNOW_LAST_INCOMINGMSG_FROM_IP.toString() + "<br>";
  WEBHTML = WEBHTML + "Last LAN Incoming Message Sender Type: " + (String) I.ESPNOW_LAST_INCOMINGMSG_FROM_TYPE + "<br>";
  I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63]=0; //just in case
  WEBHTML = WEBHTML + "Last LAN Incoming Message Payload: " + (String) I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD + "<br>";
  WEBHTML = WEBHTML + "Last LAN Incoming Message State: " + (String) ((I.ESPNOW_LAST_INCOMINGMSG_STATE==2)?"Receive Success":((I.ESPNOW_LAST_INCOMINGMSG_STATE==1)?"Send Success":((I.ESPNOW_LAST_INCOMINGMSG_STATE==0)?"Indeterminate":((I.ESPNOW_LAST_INCOMINGMSG_STATE==-1)?"Send Fail":((I.ESPNOW_LAST_INCOMINGMSG_STATE==-2)?"Receive Fail": "Unknown"))))) + "<br>";
  WEBHTML = WEBHTML + "<br>";      
  WEBHTML = WEBHTML + "Last LAN Outgoing Message Type: " + (String)  I.ESPNOW_LAST_OUTGOINGMSG_TYPE + "<br>";
  WEBHTML = WEBHTML + "Last LAN Outgoing Message Sent at: " +  (String) (I.ESPNOW_LAST_OUTGOINGMSG_TIME ? dateify(I.ESPNOW_LAST_OUTGOINGMSG_TIME,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML = WEBHTML + "Last LAN Outgoing Message To MAC: " + (String) MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC) + "<br>";
  I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD[63]=0; //just in case
  WEBHTML = WEBHTML + "Last LAN Outgoing Message Payload: " + (String) I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD + "<br>";
  WEBHTML = WEBHTML + "Last LAN Outgoing Message State: " + (String) ((I.ESPNOW_LAST_OUTGOINGMSG_STATE==1)?"Send Success":((I.ESPNOW_LAST_OUTGOINGMSG_STATE==0)?"Indeterminate":((I.ESPNOW_LAST_OUTGOINGMSG_STATE==-1)?"Send Fail": "Unknown"))) + "<br>";
  WEBHTML = WEBHTML + "<br>";      
  WEBHTML = WEBHTML + "LAN Messages Sent today: " + (String) I.ESPNOW_SENDS + "<br>";
  WEBHTML = WEBHTML + "LAN Messages Received today: " + (String) I.ESPNOW_RECEIVES + "<br>";
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML += "Weather last retrieved at: " + (String) (WeatherData.lastUpdateT ? dateify(WeatherData.lastUpdateT,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Weather last failure at: " + (String) (WeatherData.lastUpdateError ? dateify(WeatherData.lastUpdateError,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "NOAA address: " + WeatherData.getGrid(0) + "<br>";
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML += "GSheets enabled: " + (String) GSheetInfo.useGsheet + "<br>";
  WEBHTML += "Last GSheets upload time: " + (String) (GSheetInfo.lastGsheetUploadTime ? dateify(GSheetInfo.lastGsheetUploadTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Last GSheets status: " + (String) ((GSheetInfo.lastGsheetUploadSuccess==-2)?"error":((GSheetInfo.lastGsheetUploadSuccess==-1)?"not ready":((GSheetInfo.lastGsheetUploadSuccess==0)?"waiting":((GSheetInfo.lastGsheetUploadSuccess==1)?"success":"unknown")))) + "<br>";
  WEBHTML += "Last GSheets fail count: " + (String) GSheetInfo.uploadGsheetFailCount + "<br>";
  WEBHTML += "Last GSheets interval: " + (String) GSheetInfo.uploadGsheetIntervalMinutes + "<br>";
  WEBHTML += "Last GSheets function: " + (String) GSheetInfo.lastGsheetFunction + "<br>";
  WEBHTML += "Last GSheets response: " + (String) GSheetInfo.lastGsheetResponse + "<br>";
  WEBHTML += "Last GSheets SD save time: " + (String) (GSheetInfo.lastGsheetSDSaveTime ? dateify(GSheetInfo.lastGsheetSDSaveTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Last GSheets error time: " + (String) (GSheetInfo.lastErrorTime ? dateify(GSheetInfo.lastErrorTime,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";

  // Button to trigger an immediate Google Sheets upload
  WEBHTML += R"===(
  <form action="/GSHEET_UPLOAD_NOW" method="post">
  <p style="font-family:arial, sans-serif">
  <button type="submit">Upload Google Sheets Now</button>
  </p></form><br>)===";
  
  WEBHTML = WEBHTML + "---------------------<br>";      

  // Error log button
  WEBHTML = WEBHTML + "<a href=\"/ERROR_LOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View Error Log</a><br><br>";

  WEBHTML = WEBHTML + "</font>---------------------<br>";      

  // Device viewer link under server status section
  WEBHTML = WEBHTML + "<br><div style=\"text-align: center; padding: 10px;\">";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER\" style=\"display: inline-block; margin: 10px; padding: 15px 25px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px; font-size: 16px; font-weight: bold;\">Device Viewer</a>";
  WEBHTML = WEBHTML + "</div>";

  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
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
  SnsType* sensor = Sensors.getSensorBySnsIndex(j);    
  if (sensor) {

    WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) Sensors.getDeviceIPBySnsIndex(j).toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors.getDeviceIPBySnsIndex(j).toString() + "</a></td>";
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
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
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
    WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left; color: #EE4B2B;\">Server Status</th>";
    WEBHTML = WEBHTML + "</tr>";
    
    // First row - Configuration link and Heat status
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "Heat is " + (I.isHeat ? "on" : "off");
    
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/STATUS\">Status</a></td></tr>";
    
    // Second row - Weather data link and AC status
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "AC is " + (I.isAC ? "on" : "off");
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WiFiConfig\">WiFi Config</a></td></tr>";
    
    // Third row - Critical flags
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Critical flags: " + (String) I.isFlagged + "</td></td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/CONFIG\">System Configuration</a></td></tr>";
    
    // Fourth row - Leak detected
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Leak detected: " + (String) I.isLeak + "</td></td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/GSHEET\">GSheets Config</a></td></tr>";
    
    // Fifth row - Hot rooms
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Interior rooms over max temp: " + (String) I.isHot + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/SDCARD\">SD Card Config</a></td></tr>";
    
    // Sixth row - Cold rooms
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Interior rooms below min temp: " + (String) I.isCold + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WEATHER\">Weather Data</a></td></tr>";
    
    // Seventh row - Dry plants
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Plants dry: " + (String) I.isSoilDry + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
    
    // Eighth row - Expired sensors
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Critical sensors expired: " + (String) I.isExpired + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
    
    
    
    WEBHTML = WEBHTML + "</table>";
  
  WEBHTML = WEBHTML + "---------------------<br>";      


    byte used[SENSORNUM];
    for (byte j=0;j<SENSORNUM;j++)  {
      used[j] = 255;
    }

    byte usedINDEX = 0;  


  WEBHTML = WEBHTML + "<p><table id=\"Logs\" style=\"width:900px\">";      
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><button onclick=\"sortTable(0)\">IP Address</button></th><th style=\"width:50px\">ArdID</th><th style=\"width:200px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:100px\"><button onclick=\"sortTable(4)\">Sns Type</button></th><th style=\"width:100px\"><button onclick=\"sortTable(5)\">Flagged</button></th><th style=\"width:250px\">Last Recvd</th><th style=\"width:50px\">EXP</th><th style=\"width:100px\">Plot Avg</th><th style=\"width:100px\">Plot Raw</th></tr>"; 
  for (byte j=0;j<SENSORNUM;j++)  {
    SnsType* sensor = Sensors.getSensorBySnsIndex(j);    
    if (sensor && sensor->IsSet) {
      if (allsensors && bitRead(sensor->Flags,1)==0) continue;
      if (sensor->snsID>0 && sensor->snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
        used[usedINDEX++] = j;
        rootTableFill(j);
        
        for (byte jj=j+1;jj<SENSORNUM;jj++) {
          SnsType* sensor2 = Sensors.getSensorBySnsIndex(jj);    
          if (sensor2 && sensor2->IsSet) {
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

} else {
  if (Prefs.HAVECREDENTIALS==false) {
    SerialPrint("Prefs.HAVECREDENTIALS==false, redirecting to WiFiConfig");
    //redirect to WiFi config page
    server.sendHeader("Location", "/WiFiConfig");
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
  WEBHTML= "404: Not found"; // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
  serverTextClose(404,false);

}

void handleRETRIEVEDATA() {
  LAST_WEB_REQUEST = I.currentTime;
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
  if (starttime>0)  success = retrieveSensorDataFromSD(deviceMAC, snsType, snsID, &N, t, v, f, starttime, endtime,true);
  else    success = retrieveSensorDataFromSD(deviceMAC, snsType, snsID, &N, t, v, f, 0,endtime,true); //this fills from tn backwards to N*delta samples

  if (success == false)  {
    WEBHTML= "Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  SerialPrint("handleRETRIEVEDATA: N=" + (String) N + " t[0]=" + (String) t[0] + " t[N-1]=" + (String) t[N-1] + " v[0]=" + (String) v[0] + " v[N-1]=" + (String) v[N-1]);
  
  addPlotToHTML(t,v,N,deviceMAC,snsType,snsID);
}

void handleRETRIEVEDATA_MOVINGAVERAGE() {
  LAST_WEB_REQUEST = I.currentTime;
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

  success = retrieveMovingAverageSensorDataFromSD(deviceMAC, snsType, snsID, starttime, endtime, windowSizeN, &numPointsX, v, t, f,true);
  
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

  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) Prefs.DEVICENAME + "</title>\n";
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
    IPAddress deviceIP = IPAddress(0,0,0,0);

    int16_t sensorIndex = -1;

    uint8_t ardID = 0; //legacy field, not used anymore
    String devName = ""; //optional field
    uint8_t snsType = 0;
    uint8_t snsID = 0;
    String snsName = "";
    double snsValue = 0;
    uint32_t timeRead = 0;
    uint32_t timeLogged = 0;
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
                 deviceIP.fromString(ipStr);              }
              if (doc["mac"].is<JsonVariantConst>()) {
                String macStr = doc["mac"];
                //mac should be 12 hex digits
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
                deviceIP=IPAddress(0,0,0,0);
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
                  deviceMAC = strtoull(argValue.c_str(), NULL, 16);//must be a uint64_t
              } else if (argName == "IP") {
                argValue.replace(",",""); //remove unneeded erroneous commas //this was an error in the old code
                 deviceIP.fromString(argValue);              
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
          
          if (deviceMAC==0 && deviceIP!=IPAddress(0,0,0,0)) deviceMAC = IPToMACID(deviceIP);
          if (deviceMAC==0 && deviceIP==IPAddress(0,0,0,0)) {
            snsType = 0;
            SerialPrint("No MAC or IP provided",true);
            responseMsg = "No MAC or IP provided";
          }
          if (snsType == 0 || snsName.length() == 0 || (deviceIP==IPAddress(0,0,0,0) && deviceMAC==0)) {
            snsType = 0;
            deviceMAC = 0;
            deviceIP = IPAddress(0,0,0,0);
            SerialPrint("sensor missing critical elements, skipping.\n");
            responseMsg = "Missing required fields: snsType and varName";
          }  

        } else {
          SerialPrint("No data received",true);
          responseMsg = "No valid HTML form data received";
        }
    }

    if (timeRead == 0) timeRead = I.currentTime;
    timeLogged = I.currentTime; //always set timelogged to current time, regardless of what is sent

    bool addToSD = false;
    if ((deviceMAC!=0 || deviceIP!=IPAddress(0,0,0,0)) && snsType!=0 ) {
      if (devName.length() > 0) Sensors.addDevice(deviceMAC, deviceIP, devName.c_str());
      else Sensors.addDevice(deviceMAC, deviceIP, "");
        sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
        if (sensorIndex < 0) {
          SerialPrint("Sensor not found, adding to Devices_Sensors class",true);
          addToSD = true;
        } else {
          if (Sensors.getSensorFlag(sensorIndex) != flags) {
            SerialPrint("Sensor found, flags changed, adding to SD",true);
            addToSD = true;
          }
        }

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

    if (responseMsg == "OK") {
      server.send(200, "text/plain", responseMsg);
    } else {
        SerialPrint("Failed to add sensor with response message: " + responseMsg + "\n");
        server.send(500, "text/plain", responseMsg);
    }
    return;

}


void handleCONFIG() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  SerialPrint("config: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);

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
  // Timezone configuration is now handled through the timezone setup page
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Timezone Configuration</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><a href=\"/TimezoneSetup\" style=\"padding: 8px 16px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">Configure Timezone Settings</a></div>";
  
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
  
  // Device Name Configuration
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Device Name</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" name=\"deviceName\" value=\"" + String(Prefs.DEVICENAME) + "\" maxlength=\"32\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  // showTheseFlags - 8 individual checkboxes for each bit
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">showTheseFlags (Display Settings)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px;\">";
  
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit0\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 0) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 0 (Set for flagged only)</label>";
  
  // Bit 1 - Set for expired only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit1\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 1) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 1 (Set for expired only)</label>";
  
  // Bit 2 - Set for soil dry only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit2\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 2) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 2 (soil dry)</label>";
  
  // Bit 3 - Set for leak only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit3\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 3) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 3 (leak)</label>";
  
  // Bit 4 - Set for temperature only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit4\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 4) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 4 (temperature)</label>";
  
  // Bit 5 - Set for humidity only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit5\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 5) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 5 (humidity)</label>";
  
  // Bit 6 - Set for pressure only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit6\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 6) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 6 (pressure)</label>";
  
  // Bit 7 - Set for battery only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit7\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 7) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 7 (battery)</label>";

  // Bit 8 - Set for HVAC only
  WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
  WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit8\" value=\"1\"" + (String)(bitRead(I.showTheseFlags, 8) ? " checked" : "") + ">";
  WEBHTML = WEBHTML + "Bit 8 (HVAC)</label>";
  
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<div style=\"margin-top: 8px; font-size: 12px; color: #666;\">Current value: " + String(I.showTheseFlags) + " (0x" + String(I.showTheseFlags, HEX) + ")</div>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Reset button
  WEBHTML = WEBHTML + "<br><form method=\"POST\" action=\"/CONFIG_DELETE\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Reset All Settings\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather Data</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleREADONLYCOREFLAGS() {
  LAST_WEB_REQUEST = I.currentTime;
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
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANIncomingMessageTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.ESPNOW_LAST_INCOMINGMSG_TIME  ?  dateify(I.ESPNOW_LAST_INCOMINGMSG_TIME ) : "???")+ "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANIncomingMessageState</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_INCOMINGMSG_STATE + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANIncomingMessageType</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_INCOMINGMSG_TYPE + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANIncomingMessageFromMAC</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) MACToString(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANIncomingMessageFromIP</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_INCOMINGMSG_FROM_IP.toString() + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANOutgoingMessageTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.ESPNOW_LAST_OUTGOINGMSG_TIME  ?  dateify(I.ESPNOW_LAST_OUTGOINGMSG_TIME ) : "???")+ "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANOutgoingMessageState</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_OUTGOINGMSG_STATE + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANOutgoingMessageType</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_LAST_OUTGOINGMSG_TYPE + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastLANOutgoingMessageToMAC</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">LAN Messages Received since 00:00</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_RECEIVES + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">LAN Messages Sent since 00:00</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.ESPNOW_SENDS + "</div>";

  // Remaining non-editable fields from I in order of appearance
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">resetInfo</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.resetInfo + "</div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">rebootsSinceLast</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) I.rebootsSinceLast + "</div>";

  #ifdef _USETFTdddd
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
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  SerialPrint("gsheet: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);
  SerialPrint("gsheet: file ID is " + (strlen(GSheetInfo.GsheetID) > 0 ? String(GSheetInfo.GsheetID) : "N/A"),true);

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Google Sheets Configuration</h2>";
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

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">GsheetID</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.GsheetID) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">GsheetName</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.GsheetName) + "</div>";

  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Google Sheets Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Upload now button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_UPLOAD_NOW\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Upload to Gsheets Immediately\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Configuration</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather Data</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleGSHEET_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
  
  // Save to SD card
  if (storeGsheetInfoSD()) {
    SerialPrint("Google Sheets configuration saved to SD card", true);
  } else {
    SerialPrint("Failed to save Google Sheets configuration to SD card", true);
  }
  
  // Redirect back to the Google Sheets configuration page
  server.sendHeader("Location", "/GSHEET");
  server.send(302, "text/plain", "");
}

void handleGSHEET_UPLOAD_NOW() {
  LAST_WEB_REQUEST = I.currentTime;
  int8_t result = Gsheet_uploadData();
  String msg = "Triggered immediate upload. Result: " + String(result) + ", " + GsheetUploadErrorString();
  server.send(200, "text/plain", msg);
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
    I.WiFiMode = WIFI_AP_STA;
    I.isUpToDate = false;
  }

  *wifiID = generateAPSSID();
//    byte lastByte = getPROCIDByte(Prefs.PROCID, 5); // Last byte of MAC for IP address
  *apIP = IPAddress(192, 168, 4, 1);
  
  // Ensure WiFi is properly initialized
  WiFi.disconnect(true);
  delay(200);
  
  WiFi.mode(WIFI_OFF);
  delay(200);
  
  WiFi.mode(WIFI_AP_STA); // Combined AP and station mode
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
  LAST_WEB_REQUEST = I.currentTime;
  
  // Process form submissions and update editable fields
  // Timezone configuration is now handled through the timezone setup page
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
  
  // Process device name
  if (server.hasArg("deviceName")) {
    String newDeviceName = server.arg("deviceName");
    if (newDeviceName.length() > 0 && newDeviceName.length() <= 32) {
      snprintf((char*)Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", newDeviceName.c_str());
      Prefs.isUpToDate = false; // Mark as needing to be saved
    }
  }
  
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
  deleteCoreStruct(); //delete the screen flags file
  deleteFiles("GsheetInfo.dat", "/Data");
  deleteFiles("WeatherData.dat", "/Data");
  deleteFiles("DevicesSensors.dat", "/Data");
  BootSecure bootSecure;
  //flush Prefs
  int8_t ret = bootSecure.flushPrefs();
  if (ret < 0) {
    SerialPrint("Failed to flush Prefs: " + String(ret), true);
    
    #ifdef _USETFT
    tft.clear();
    tft.setCursor(0, 0);
    tft.printf("Failed to flush Prefs: %d\n", ret);
    #endif
    server.send(500, "text/plain", "Failed to flush Prefs");

  } else {
    SerialPrint("Prefs flushed", true);
    #ifdef _USETFT
    tft.clear();
    tft.setCursor(0, 0);
    tft.println("Prefs flushed");
    
    #endif
  }
  Prefs.isUpToDate = true; //this prevents prefs from updating in controlled reboot
  controlledReboot("User Request",RESET_USER);

}


void handleWiFiConfig() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " WiFi Configuration</h2>";
  
  // Check for error messages
  if (server.hasArg("error")) {
    String error = server.arg("error");
    if (error == "connection_failed") {
      WEBHTML = WEBHTML + "<div style=\"background-color: #ffebee; border: 1px solid #f44336; color: #c62828; padding: 15px; margin: 10px 0; border-radius: 4px;\">";
      WEBHTML = WEBHTML + "<strong>WiFi Connection Failed!</strong><br>";
      WEBHTML = WEBHTML + "Please check your WiFi credentials and try again.";
      WEBHTML = WEBHTML + "</div>";
    }
  }
  
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
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather Data</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

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

  //add weather address lookup if no credentials
  if (!Prefs.HAVECREDENTIALS) {
  WEBHTML = WEBHTML + "<p><label for=\"street\">Street Address:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"street\" name=\"street\" placeholder=\"123 Main St\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"city\">City:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"city\" name=\"city\" placeholder=\"Boston\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"state\">State (2-letter code):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"state\" name=\"state\" pattern=\"[A-Za-z]{2}\" maxlength=\"2\" placeholder=\"MA\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"zipcode\">ZIP Code (5 digits):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"zipcode\" name=\"zipcode\" pattern=\"[0-9]{5}\" maxlength=\"5\" placeholder=\"12345\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  }

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


void handleWiFiConfig_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(2);
  tft.printf("Processing WiFi request...\n");

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
  }

  // Try to connect to WiFi - note that we are already in  mixed AP+STA mode
  tft.println("Attempting WiFi connection...");
  WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
  delay(2000); //allow wifi to connect

  if (WiFi.status()) {
    // WiFi connection successful
    Prefs.status = 1; // Mark WiFi as connected
    Prefs.HAVECREDENTIALS = true;
    Prefs.isUpToDate = false;
    
    // Reset WiFi failure count since we successfully connected
    I.wifiFailCount = 0;
    
    tft.println("WiFi connected successfully!");
    
    // Handle weather address if provided
    if (server.hasArg("street") && server.hasArg("city") && server.hasArg("state") && server.hasArg("zipcode")) {
      String street = server.arg("street");
      String city = server.arg("city");
      String state = server.arg("state");
      String zipcode = server.arg("zipcode");
      if (handlerForWeatherAddress(street, city, state, zipcode)) {
        SerialPrint("Obtained geolocation from address", true);
        delay(1000);
      } else {
        #ifdef _USETFT
        tft.setTextColor(TFT_RED);
        tft.println("Geolocation from address failed, correct in settings later.");
        tft.setTextColor(FG_COLOR);
        #endif
        SerialPrint("Geolocation from address failed, correct in settings later.", true);
        delay(1000);
      }
    }
    
    // Return success response (no redirect - let user click "Check Timezone" button)
    server.send(200, "text/plain", "WiFi credentials submitted successfully!");
  } else {
    // WiFi connection failed, redirect back to config page
    tft.println("WiFi connection failed, re-enter credentials");
    server.sendHeader("Location", "/WiFiConfig?error=connection_failed");
    server.send(404, "text/plain", "WiFi connection failed. Please check credentials and try again.");
  }
}



void handleWiFiConfig_RESET() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Clear WiFi credentials and LMK key
  memset(Prefs.WIFISSID, 0, sizeof(Prefs.WIFISSID));
  memset(Prefs.WIFIPWD, 0, sizeof(Prefs.WIFIPWD));
  memset(Prefs.KEYS.ESPNOW_KEY, 0, sizeof(Prefs.KEYS.ESPNOW_KEY));
  Prefs.HAVECREDENTIALS = false;
  Prefs.isUpToDate = false;
  
  
  // Redirect back to WiFi config page
  server.sendHeader("Location", "/WiFiConfig");
  server.send(302, "text/plain", "WiFi configuration and LMK key reset. Please reconfigure.");

    // Disconnect from current WiFi
    WiFi.disconnect();

}

void handleTimezoneSetup() {
  LAST_WEB_REQUEST = I.currentTime;
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
  }  else   WEBHTML = WEBHTML + "<p style=\"color: green;\">Timezone information from stored data</p>";
  
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
  
  WEBHTML = WEBHTML + "<p><a href=\"/WiFiConfig\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">Back to WiFi Config</a></p>";
  
  WEBHTML = WEBHTML + "</body></html>";
  serverTextClose(200, true);
}

void handleTimezoneSetup_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  #ifdef _USETFT
  
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(2);
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
    
    // Save to Prefs
    Prefs.TimeZoneOffset = total_offset;
    Prefs.DST = dst_enabled ? 1 : 0;
    Prefs.DSTOffset = dst_enabled ? 3600 : 0;
    Prefs.DSTStartMonth = dst_start_month;
    Prefs.DSTStartDay = dst_start_day;
    Prefs.DSTEndMonth = dst_end_month;
    Prefs.DSTEndDay = dst_end_day;
    
    // Mark Prefs as needing update and save to NVS
    Prefs.isUpToDate = false;
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
    tft.clear();
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(1);
    tft.setTextFont(2);
    if (!server.hasArg("UTC_OFFSET_SECONDS")) tft.println("No UTC offset seconds");
    if (!server.hasArg("DST_ENABLED")) tft.println("No DST enabled");
    if (!server.hasArg("DST_START_MONTH")) tft.println("No DST start month");
    if (!server.hasArg("DST_START_DAY")) tft.println("No DST start day");
    if (!server.hasArg("DST_END_MONTH")) tft.println("No DST end month");
    if (!server.hasArg("DST_END_DAY")) tft.println("No DST end day");
    tft.setTextColor(FG_COLOR);

    server.sendHeader("Location", "/TimezoneSetup");
    server.send(302, "text/plain", "Invalid timezone data. Please try again.");
  }
}

// Removed handleWiFiConfirm and handleWiFiConfirm_POST - now using AJAX approach

void handleWeather() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Weather Data</h2>";
  
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
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateAttempt) ? dateify(WeatherData.lastUpdateAttempt) : "???") + " " + (String) WeatherData.lastUpdateAttempt + "</td></tr>";


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
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[0]) + "F</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[1]) + "F</td>";
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
  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Configuration</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather Data</a>";
  WEBHTML = WEBHTML + "</div>";
  
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
    bool success = WeatherData.getCoordinatesFromZipCode(zipCode);
    if (success) {
      // Return JSON response with coordinates (don't update WeatherData yet)
      String jsonResponse = "{\"success\":true,\"latitude\":" + String(WeatherData.lat, 14) + ",\"longitude\":" + String(WeatherData.lon, 14) + "}";
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

  if (server.hasArg("street") && server.hasArg("city") && server.hasArg("state") && server.hasArg("zipcode")) {
    String street = server.arg("street");
    String city = server.arg("city");
    String state = server.arg("state");
    String zipCode = server.arg("zipcode");

     // Validate required fields
     if (street.length() == 0 || city.length() == 0 || state.length() == 0 || zipCode.length() == 0) {
      server.sendHeader("Location", "/WEATHER?error=missing_fields");
      server.send(302, "text/plain", "All address fields are required.");
      return;
    }
    
    // Validate state format (2 letters)
    if (state.length() != 2) {
      server.sendHeader("Location", "/WEATHER?error=invalid_state");
      server.send(302, "text/plain", "State must be a 2-letter code.");
      return;
    }
    
    // Validate ZIP code format
    if (zipCode.length() != 5) {
      server.sendHeader("Location", "/WEATHER?error=invalid_zip");
      server.send(302, "text/plain", "Invalid ZIP code format. Must be 5 digits.");
      return;
    }
    
    for (int i = 0; i < 5; i++) {
      if (!isdigit(zipCode.charAt(i))) {
        server.sendHeader("Location", "/WEATHER?error=invalid_zip");
        server.send(302, "text/plain", "Invalid ZIP code format. Must contain only digits.");
        return;
      }
    }

    if (handlerForWeatherAddress(street, city, state, zipCode)) {
      // Redirect back to weather page with success message
      String redirectUrl = "/WEATHER?success=address_lookup&lat=" + String(WeatherData.lat, 14) + "&lon=" + String(WeatherData.lon, 14);
      server.sendHeader("Location", redirectUrl);
      server.send(302, "text/plain", "Address lookup successful. Coordinates updated and weather refreshed.");
      Prefs.isUpToDate = false;
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
    tft.setTextColor(TFT_GREEN);
    tft.println("Getting coordinates from address");
    tft.setTextColor(FG_COLOR);
    #endif
    if (WeatherData.getCoordinatesFromAddress(street, city, state, zipCode)) {
      
      // Force weather update with new coordinates
      //WeatherData.updateWeather(0);
      if (Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) return true;
    } else {
      #ifdef _USETFT
      tft.setTextColor(TFT_RED);
      tft.println("Failed to fetch coordinates from address");
      tft.setTextColor(FG_COLOR);
      #endif
      return false;
    }
  } else {
    #ifdef _USETFT
    tft.setTextColor(TFT_RED);
    tft.println("WiFi is down, cannot fetch coordinates from address");
    tft.setTextColor(FG_COLOR);
    #endif
    return false;
  }
    return false;

}

void handleSDCARD() {
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " SD Card Configuration</h2>";
  
  // SD Card Information
  WEBHTML = WEBHTML + "<h3>SD Card Information</h3>";
  
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
  
  // Debug output to Serial
  SerialPrint("=== SD Card Debug Info ===", true);
  SerialPrint("SD.cardSize(): " + String(cardSize) + " bytes (" + formatBytes(cardSize) + ")", true);
  SerialPrint("SD.usedBytes(): " + String(usedBytes) + " bytes (" + formatBytes(usedBytes) + ")", true);
  SerialPrint("Calculated free space: " + String(totalBytes - usedBytes) + " bytes (" + formatBytes(totalBytes - usedBytes) + ")", true);
  
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
  
  // Navigation links to other config pages
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Configuration Pages</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WiFiConfig\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Configuration</a> ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather Data</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200, true);
}

void handleSDCARD_DELETE_DEVICES() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Delete the sensors data file
  uint16_t nDeleted = deleteFiles("DevicesSensors.dat", "/Data");
  if (nDeleted > 0) {
    SerialPrint("DevicesSensors data file deleted " + String(nDeleted) + " files successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "DevicesSensors data file deleted " + String(nDeleted) + " files successfully.");
  } else {
    SerialPrint("Failed to delete DevicesSensors data file", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to delete DevicesSensors data file.");
  }
}

void handleSDCARD_DELETE_SENSORS() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Delete all individual sensor history files
  File root = SD.open("/Data");
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    int deletedCount = 0;
    while (file) {
      String filename = file.name();
      if (filename.endsWith(".dat") && filename != "ScreenFlags.dat" && filename != "SensorBackupv2.dat" && filename != "GsheetInfo.dat" && filename != "FileTimestamps.dat" && filename != "WeatherData.dat" && filename != "DevicesSensors.dat") {
        if (SD.remove("/Data/" + filename)) {
          deletedCount++;
        }
      }
      file = root.openNextFile();
    }
    root.close();
    
    SerialPrint("Deleted " + String(deletedCount) + " sensor history files", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Deleted " + String(deletedCount) + " sensor history files.");
  } else {
    SerialPrint("Failed to access Data directory", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to access Data directory.");
  }
}

void handleSDCARD_STORE_DEVICES() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Store the devices and sensors data to SD card
  if (storeDevicesSensorsSD()) {
    SerialPrint("DevicesSensors data stored to SD card successfully", true);
    server.send(302, "text/plain", "DevicesSensors data stored to SD card successfully.");
  } else {
    SerialPrint("Failed to store DevicesSensors data to SD card", true);
    server.send(302, "text/plain", "Failed to store DevicesSensors data to SD card.");
  }
}

void handleSDCARD_DELETE_SCREENFLAGS() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Delete the ScreenFlags.dat file
  uint16_t nDeleted = deleteCoreStruct();

  if (nDeleted > 0) {
    SerialPrint("ScreenFlags.dat deleted " + String(nDeleted) + " files successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "ScreenFlags.dat deleted " + String(nDeleted) + " files successfully.");
  } else {
    SerialPrint("Failed to delete ScreenFlags.dat", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to delete ScreenFlags.dat.");
  }

}

void handleSDCARD_DELETE_WEATHERDATA() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Delete the WeatherData.dat file
  uint16_t nDeleted = deleteFiles("WeatherData.dat", "/Data");
  if (nDeleted > 0) {
    SerialPrint("WeatherData.dat deleted " + String(nDeleted) + " files successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "WeatherData.dat deleted " + String(nDeleted) + " files successfully.");
  } else {
    SerialPrint("Failed to delete WeatherData.dat", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to delete WeatherData.dat.");
  }
}

void handleSDCARD_SAVE_SCREENFLAGS() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
}

void handleSDCARD_SAVE_WEATHERDATA() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
}

void handleSDCARD_TIMESTAMPS() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
}

void handleERROR_LOG() {
  LAST_WEB_REQUEST = I.currentTime;
  
  // Open and read the DeviceErrors.txt file
  String filename = "/Data/DeviceErrors.txt";
  File file = SD.open(filename, FILE_READ);
  
  if (!file) {
    // File doesn't exist or can't be opened
    server.send(200, "text/html", 
      "<html><head><title>Error Log</title></head><body>"
      "<h1>Error Log</h1>"
      "<p>The DeviceErrors.txt file does not exist or cannot be opened.</p>"
      "<p>This file is created automatically when errors occur on the device.</p>"
      "<br><a href=\"/STATUS\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Status</a>"
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
      "<html><head><title>Error Log</title></head><body>"
      "<h1>Error Log</h1>"
      "<p>The DeviceErrors.txt file is empty.</p>"
      "<p>No errors have been logged yet.</p>"
      "<br><a href=\"/STATUS\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Status</a>"
      "</body></html>");
    return;
  }
  
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
  htmlContent += "<p>This shows all errors that have occurred on the device:</p>";
  
  // Parse and format each line
  int lineStart = 0;
  int lineEnd = content.indexOf('\n');
  int entryCount = 0;
  
  while (lineEnd >= 0 && entryCount < 1000) { // Limit to 1000 entries to prevent memory issues
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
}

void handleSDCARD_DELETE_ERRORLOG() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
}

void handleSDCARD_DELETE_TIMESTAMPS() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
}

void handleSDCARD_DELETE_GSHEET() {
  LAST_WEB_REQUEST = I.currentTime;
  
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
}



void handleREBOOT_DEBUG() {
  LAST_WEB_REQUEST = I.currentTime;
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
  LAST_WEB_REQUEST = I.currentTime;
  
  // Check if WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    server.sendHeader("Location", "/WiFiConfig");
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
  LAST_WEB_REQUEST = I.currentTime;
  
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

  RegistrationCompleted = true;  
}

// ==================== DEVICE VIEWER FUNCTIONS ====================

void handleDeviceViewer() {
    LAST_WEB_REQUEST = I.currentTime;
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

    while (Sensors.isDeviceInit(CURRENT_DEVICE_VIEWER)==false) {
        CURRENT_DEVICE_VIEWER++;
        if (CURRENT_DEVICE_VIEWER >= NUMDEVICES ) CURRENT_DEVICE_VIEWER = 0;
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
    WEBHTML = WEBHTML + "<h3>Device " + String(CURRENT_DEVICE_VIEWER + 1) + " of " + String(Sensors.getNumDevices()) + ((CURRENT_DEVICE_VIEWER == myDeviceIndex) ? " (this device)" : "") +"</h3>";
    WEBHTML = WEBHTML + "</div>";
    
    // Device details
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h4>Device Information</h4>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 30%;\">Device Name:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devName) +  "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">MAC Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(device->MAC)) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">IP Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + device->IP.toString() + "\" target=\"_blank\">" + device->IP.toString() + "</a></td></tr>";
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
    LAST_WEB_REQUEST = I.currentTime;
    
    if (Sensors.getNumDevices() > 0) {
        CURRENT_DEVICE_VIEWER = (CURRENT_DEVICE_VIEWER + 1) % Sensors.getNumDevices();
    }
    
    server.sendHeader("Location", "/DEVICEVIEWER");
    server.send(302, "text/plain", "Redirecting to next device...");
}

void handleDeviceViewerPrev() {
    LAST_WEB_REQUEST = I.currentTime;
    
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
    LAST_WEB_REQUEST = I.currentTime;
    
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
    
    // Convert uint64_t MAC to uint8_t array for 
    uint8_t targetMAC[6];
    uint64ToMAC(device->MAC, targetMAC);
    
    // Send ping request
    bool success = sendPingRequest(targetMAC);
    
    if (success) {
        server.sendHeader("Location", "/DEVICEVIEWER?ping=success");
        server.send(302, "text/plain", "Ping sent successfully.");
    } else {
        server.sendHeader("Location", "/DEVICEVIEWER?ping=failed");
        server.send(302, "text/plain", "Failed to send ping.");
    }
}

void handleDeviceViewerDelete() {
    LAST_WEB_REQUEST = I.currentTime;
    
    // Check if we have any devices
    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=no_devices");
        server.send(302, "text/plain", "No devices available to delete.");
        return;
    }
    
    // Ensure current device index is valid
    if (CURRENT_DEVICE_VIEWER >= NUMDEVICES) {
        CURRENT_DEVICE_VIEWER = 0;
    }
    
    // Get current device
    DevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICE_VIEWER);
    if (!device) {
        server.sendHeader("Location", "/DEVICEVIEWER?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }
    
    // Store device name for confirmation message
    String deviceName = String(device->devName);
    uint8_t deletedSensors = Sensors.initDevice(CURRENT_DEVICE_VIEWER);
    
    // Adjust current device index if needed
    if (CURRENT_DEVICE_VIEWER >= NUMDEVICES) {
        CURRENT_DEVICE_VIEWER = 0;
    }
    
    // Redirect back to device viewer with success message
    if (device->IsSet) {
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
    
    // Google Sheets routes
    server.on("/GSHEET", HTTP_GET, handleGSHEET);
    server.on("/GSHEET", HTTP_POST, handleGSHEET_POST);
    server.on("/GSHEET_UPLOAD_NOW", HTTP_POST, handleGSHEET_UPLOAD_NOW);
    
    // WiFi configuration routes
    server.on("/WiFiConfig", HTTP_GET, handleWiFiConfig);
    server.on("/WiFiConfig", HTTP_POST, handleWiFiConfig_POST);
    server.on("/WiFiConfig_RESET", HTTP_POST, handleWiFiConfig_RESET);
    
    // Timezone configuration routes
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