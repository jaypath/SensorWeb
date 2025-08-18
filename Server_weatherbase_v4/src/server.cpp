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
    APStation_Mode();
    return -1000;
  }

    return retries;
}

void APStation_Mode() {
  
  String wifiPWD = "";
  String wifiID = "";
  IPAddress apIP;
  connectSoftAP(&wifiID,&wifiPWD,&apIP);
  server.begin(); //start server

    #ifdef _USETFT
    tft.clear();
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.printf("Setup Required. Please join\n\nWiFi: %s\npwd: \n%s\n\nand go to website:\n\n%s\n\n", 
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
WEBHTML += "GSheets enabled: " + (String) GSheetInfo.useGsheet + "<br>";
WEBHTML += "Last GSheets upload time: " + (String) (GSheetInfo.lastGsheetUploadTime ? dateify(GSheetInfo.lastGsheetUploadTime) : "???") + "<br>";
WEBHTML += "Last GSheets success (-2=error, -1=not ready, 0=waiting, 1=success): " + (String) GSheetInfo.lastGsheetUploadSuccess + "<br>";
WEBHTML += "Last GSheets fail count: " + (String) GSheetInfo.uploadGsheetFailCount + "<br>";
WEBHTML += "Last GSheets interval: " + (String) GSheetInfo.uploadGsheetIntervalMinutes + "<br>";
WEBHTML += "Last GSheets function: " + (String) GSheetInfo.lastGsheetFunction + "<br>";
WEBHTML += "Last GSheets response: " + (String) GSheetInfo.lastGsheetResponse + "<br>";
WEBHTML += "Last GSheets SD save time: " + (String) (GSheetInfo.lastGsheetSDSaveTime ? dateify(GSheetInfo.lastGsheetSDSaveTime) : "???") + "<br>";
WEBHTML += "Last GSheets error time: " + (String) (GSheetInfo.lastErrorTime ? dateify(GSheetInfo.lastErrorTime) : "???") + "<br>";

// Button to trigger an immediate Google Sheets upload
WEBHTML += R"===(
  <form action="/GSHEET_UPLOAD_NOW" method="post">
  <p style="font-family:arial, sans-serif">
  <button type="submit">Upload Google Sheets Now</button>
  </p></form><br>)===";
  
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
  </p></font></form><br>)===";


        
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
  LAST_WEB_REQUEST = I.currentTime;
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Main Page</h2>";

  if (Prefs.HAVECREDENTIALS==true && Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) { //note that lat and lon could be any number, but likelihood of both being 0 is very low, and no where in the US is at 0 for either



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
    
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/CONFIG\">Configuration</a></td></tr>";
    
    // Second row - Weather data link and AC status
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "AC is " + (I.isAC ? "on" : "off");
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WEATHER\">Weather Data</a></td></tr>";
    
    // Third row - Critical flags
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Critical flags: " + (String) I.isFlagged + "</td></td><td style=\"border: 1px solid #ddd; padding: 8px;\"><a href=\"/WiFiConfig\">WiFi Config</a></td></tr>";
    
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

void handleFLUSHSD() {
  if (server.args()==0) return;
  if (server.argName(0)=="FLUSHCONFIRM") {

    if (server.arg(0).toInt()== 3) {
      I.isUpToDate = false;
      Sensors.lastUpdatedTime = 0;
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
  bool success=false;

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
    
    if ((deviceMAC!=0 || deviceIP!=0) && snsType!=0 ) {

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
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader();
  SerialPrint("config: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Configuration Page</h2>";
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
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">screenChangeTimer</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"screenChangeTimer\" value=\"" + (String) I.screenChangeTimer + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";

  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Navigation links
  WEBHTML = WEBHTML + "<br><br><a href=\"/READONLYCOREFLAGS\">View Read-Only Core Flags</a> | ";
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\">View Google Sheets Configuration</a> | ";
  WEBHTML = WEBHTML + "<a href=\"/\">Back to Main Page</a>";
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
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastESPNOW</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) (I.lastESPNOW  ?  dateify(I.lastESPNOW ) : "???")+ "</div>";

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
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9; font-weight: bold;\">Google Sheets Configuration</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">useGsheet</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"checkbox\" name=\"useGsheet\" value=\"1\"" + ((GSheetInfo.useGsheet) ? " checked" : "") + " style=\"width: 20px; height: 20px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">uploadGsheetIntervalMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"uploadGsheetIntervalMinutes\" value=\"" + (String) GSheetInfo.uploadGsheetIntervalMinutes + "\" min=\"1\" max=\"1440\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetSDSaveTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"lastGsheetSDSaveTime\" value=\"" + (String) GSheetInfo.lastGsheetSDSaveTime + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetUploadTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"lastGsheetUploadTime\" value=\"" + (String) GSheetInfo.lastGsheetUploadTime + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
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
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + String(GSheetInfo.GsheetName) + "</div>";

  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Google Sheets Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Navigation links
  WEBHTML = WEBHTML + "<br><br><a href=\"/CONFIG\">Edit Core Configuration</a> | ";
  WEBHTML = WEBHTML + "<a href=\"/READONLYCOREFLAGS\">View Read-Only Core Flags</a> | ";
  WEBHTML = WEBHTML + "<a href=\"/\">Back to Main Page</a>";
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
  if (server.hasArg("lastGsheetSDSaveTime")) {
    GSheetInfo.lastGsheetSDSaveTime = server.arg("lastGsheetSDSaveTime").toInt();
  }
  if (server.hasArg("lastGsheetUploadTime")) {
    GSheetInfo.lastGsheetUploadTime = server.arg("lastGsheetUploadTime").toInt();
  }
  
  // Validate Google Sheets configuration
  if (GSheetInfo.uploadGsheetIntervalMinutes < 1) {
    GSheetInfo.uploadGsheetIntervalMinutes = 1;
  }
  if (GSheetInfo.uploadGsheetIntervalMinutes > 1440) {
    GSheetInfo.uploadGsheetIntervalMinutes = 1440;
  }
  if (GSheetInfo.lastGsheetSDSaveTime < 0) {
    GSheetInfo.lastGsheetSDSaveTime = 0;
  }
  if (GSheetInfo.lastGsheetUploadTime < 0) {
    GSheetInfo.lastGsheetUploadTime = 0;
  }
  
  // Save the updated configuration
  BootSecure::setPrefs();
  
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
    *wifiID = generateAPSSID();
//    byte lastByte = getPROCIDByte(Prefs.PROCID, 5); // Last byte of MAC for IP address
    *apIP = IPAddress(192, 168, 4, 1);
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
  deleteFiles("GsheetInfo.dat", "/Data");
  deleteFiles("WeatherData.dat", "/Data");
  deleteFiles("DevicesSensors.dat", "/Data");
  
  // Redirect back to the configuration page with a success message
  server.sendHeader("Location", "/CONFIG");
  server.send(302, "text/plain", "SD card contents deleted. Redirecting...");
}

void handleWiFiConfig() {
  LAST_WEB_REQUEST = I.currentTime;
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
  WEBHTML = WEBHTML + "<p><strong>Stored SSID:</strong> " + String((char*)Prefs.WIFISSID) + "</p>";
  WEBHTML = WEBHTML + "<p><strong>Stored LMK Key:</strong> " + String((char*)Prefs.LMK_KEY) + "</p>";

  addWiFiConfigForm();

  WEBHTML = WEBHTML + "<h3>WiFi Reset</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WiFiConfig_RESET\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Reset WiFi Configuration\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<br><br><a href=\"/\">Back to Main Page</a>";
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
  WEBHTML = WEBHTML + "<p><label for=\"lmk_key\">Security Key (up to 16 chars):</label><br>";
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

  WEBHTML = WEBHTML + "<input type=\"hidden\" id=\"BLOB\" name=\"BLOB\">";
  WEBHTML = WEBHTML + "<input type=\"button\" value=\"Update WiFi Configuration\" onclick=\"submitEncryptedForm()\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // JavaScript for AES128 encryption and form submission
  WEBHTML = WEBHTML + R"===(
  <script src="https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.1.1/crypto-js.min.js"></script>
  <script>
  function wordArrayToByteArray(wordArray) {
    var byteArray = [];
    var words = wordArray.words;
    var sigBytes = wordArray.sigBytes;
    for (var i = 0; i < sigBytes; i++) {
        byteArray.push((words[Math.floor(i / 4)] >> (24 - 8 * (i % 4))) & 0xFF);
    }
    return byteArray;
  }
  function submitEncryptedForm() {
    var ssid = document.getElementById('ssid').value;
    var password = document.getElementById('password').value;
    var lmk_key = document.getElementById('lmk_key').value;
    
    if (!ssid || !password || !lmk_key) {
      alert('Please fill in all fields');
      return;
    }
    
    // Create data string with CRC
    var dataString = ssid + '|' + password + '|' + lmk_key;
    
    // Calculate CRC using the same algorithm as BootSecure::CRCCalculator
    // Convert string to UTF-8 bytes first, then calculate CRC on bytes
    var utf8Bytes = [];
    for (var i = 0; i < dataString.length; i++) {
        var charCode = dataString.charCodeAt(i);
        if (charCode < 0x80) {
            utf8Bytes.push(charCode);
        } else if (charCode < 0x800) {
            utf8Bytes.push(0xC0 | (charCode >> 6));
            utf8Bytes.push(0x80 | (charCode & 0x3F));
        } else {
            utf8Bytes.push(0xE0 | (charCode >> 12));
            utf8Bytes.push(0x80 | ((charCode >> 6) & 0x3F));
            utf8Bytes.push(0x80 | (charCode & 0x3F));
        }
    }
    
    var curr_crc = 0x0000;
    var sum1 = curr_crc & 0xFF;
    var sum2 = (curr_crc >> 8) & 0xFF;
    for (var i = 0; i < utf8Bytes.length; i++) {
        sum1 = (sum1 + utf8Bytes[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    var crc = (sum2 << 8) | sum1;
    dataString += '|' + crc;
    
    // Convert string to WordArray directly (CryptoJS handles UTF-8 conversion properly)
    var dataWordArray = CryptoJS.enc.Utf8.parse(dataString);
    
    // Pad to 16-byte boundary with zeros using a simpler approach
    var blockSize = 16;
    var currentLength = dataWordArray.sigBytes;
    var paddingNeeded = blockSize - (currentLength % blockSize);
    
    if (paddingNeeded < blockSize) {
        // Create a new WordArray with proper padding
        var paddedBytes = [];
        for (var i = 0; i < currentLength; i++) {
            var wordIndex = Math.floor(i / 4);
            var byteIndex = i % 4;
            var word = dataWordArray.words[wordIndex] || 0;
            var byte = (word >> (24 - 8 * byteIndex)) & 0xFF;
            paddedBytes.push(byte);
        }
        
        // Add zero padding
        for (var i = 0; i < paddingNeeded; i++) {
            paddedBytes.push(0);
        }
        
        // Convert back to WordArray
        var paddedWords = [];
        for (var i = 0; i < paddedBytes.length; i += 4) {
            var word = 0;
            for (var j = 0; j < 4 && (i + j) < paddedBytes.length; j++) {
                word |= (paddedBytes[i + j] << (24 - 8 * j));
            }
            paddedWords.push(word);
        }
        
        dataWordArray = CryptoJS.lib.WordArray.create(paddedWords, paddedBytes.length);
    }
    
    // Debug: Log padding information
    console.log('Original data length:', dataString.length);
    console.log('Original sigBytes:', dataWordArray.sigBytes);
    console.log('Final sigBytes:', dataWordArray.sigBytes);
    
    // Generate random IV
    var iv = CryptoJS.lib.WordArray.random(16);
    
    // AES key for WiFi configuration (dedicated key)
    var key = CryptoJS.enc.Utf8.parse(')===" + String(WIFI_CONFIG_KEY) + R"===(');
    
    // Encrypt using AES-CBC
    var encrypted = CryptoJS.AES.encrypt(dataWordArray, key, {
      iv: iv,
      mode: CryptoJS.mode.CBC,
      padding: CryptoJS.pad.NoPadding
    });
    
    // Combine IV and encrypted data
    var combinedBytes = [];
    
    // Add IV bytes (first 16 bytes)
    var ivArray = wordArrayToByteArray(iv);
    for (var i = 0; i < 16; i++) {
      combinedBytes.push(ivArray[i]);
    }
    
    // Add encrypted data bytes
    var encryptedArray = wordArrayToByteArray(encrypted.ciphertext);
    for (var i = 0; i < encryptedArray.length; i++) {
      combinedBytes.push(encryptedArray[i]);
    }
    
    // Convert to base64 using a more reliable method
    var binaryString = '';
    for (var i = 0; i < combinedBytes.length; i++) {
        binaryString += String.fromCharCode(combinedBytes[i]);
    }
    var encryptedBlob = btoa(binaryString);
    
    // Debug: Log the encrypted data length
    console.log('Combined bytes length:', combinedBytes.length);
    console.log('IV length:', ivArray.length);
    console.log('Encrypted data length:', encryptedArray.length);
    
    document.getElementById('BLOB').value = encryptedBlob;
    document.getElementById('wifiForm').submit();
  }
  </script>)===";

}

void handleWiFiConfig_POST() {
  LAST_WEB_REQUEST = I.currentTime;
  
//  tft.clear();
//  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.printf("Received encrypted data... parsing\n");

  delay(3000);
  

  // Check if we have the encrypted BLOB
  if (server.hasArg("BLOB")) {
    String encryptedBlob = server.arg("BLOB");
    
    // Decode base64
    int decodedLength = base64_dec_len(encryptedBlob.c_str(), encryptedBlob.length());
    uint8_t* decodedData = new uint8_t[decodedLength];
    base64_decode(encryptedBlob.c_str(), decodedData);
    
    // Debug: Check if data length is valid for AES decryption
    if (decodedLength < 32 || (decodedLength % 16) != 0) {
        SerialPrint("ERROR: Invalid data length for AES decryption. Length: " + String(decodedLength) + " (must be >= 32 and multiple of 16)",true);
        server.sendHeader("Location", "/WiFiConfig");
        server.send(302, "text/plain", "Invalid encrypted data format.");
        delete[] decodedData;
        return;
    }

    // Decrypt the data using BootSecure functions
    uint8_t* decryptedData = new uint8_t[decodedLength];
    memset(decryptedData, 0, decodedLength); // Initialize to zero
    // Debug: Check key length
    
    int8_t decryptResult = BootSecure::decrypt(decodedData, (char*)WIFI_CONFIG_KEY, decryptedData, decodedLength);
    
      
    // Calculate actual decrypted data length (excluding IV)
    uint16_t decryptedLength = decodedLength - 16; // Remove IV
    
    // Find the actual data length by looking for the last non-zero byte
    uint16_t actualDataLength = 0;
    for (int i = decryptedLength - 1; i >= 0; i--) {
        if (decryptedData[i] != 0) {
            actualDataLength = i + 1;
            break;
        }
    }
    
    // Ensure null termination
    if (actualDataLength < decodedLength) {
        decryptedData[actualDataLength] = '\0';
    }
    
    // Create a clean string from the actual data length
    String decryptedString = String((char*)decryptedData, actualDataLength);
    

    if (decryptResult == 0 && actualDataLength > 0) {
      // Decryption successful, parse the data
      // Expected format: SSID|PASSWORD|LMK_KEY|CRC
      int pos1 = decryptedString.indexOf('|');
      int pos2 = decryptedString.indexOf('|', pos1 + 1);
      int pos3 = decryptedString.indexOf('|', pos2 + 1);
      
      if (pos1 > 0 && pos2 > pos1 && pos3 > pos2) {
        String ssid = decryptedString.substring(0, pos1);
        String password = decryptedString.substring(pos1 + 1, pos2);
        String lmk_key = decryptedString.substring(pos2 + 1, pos3);
        
        if (lmk_key.length() > 16) {
          lmk_key = lmk_key.substring(0, 16);
        } 
        if (lmk_key.length() < 16) {
          lmk_key = lmk_key + String(16 - lmk_key.length(), '0'); //pad short keys with 0s
        } 

        String crc = decryptedString.substring(pos3 + 1);
        
        // Debug: Print CRC values and string details
        SerialPrint("Received CRC string: '" + crc + "'",true);
        SerialPrint("CRC string length: " + String(crc.length()),true);
        SerialPrint("Data for CRC calculation: '" + decryptedString.substring(0, pos3) + "'",true);
        
        // Debug: Show hex representation of CRC string
        SerialPrint("CRC string hex: ",false);
        for (int i = 0; i < crc.length() && i < 20; i++) {
            SerialPrint(String(crc.charAt(i), HEX) + " ",false);
        }
        SerialPrint("",true);
        
        // Verify CRC using BootSecure CRC calculator
        uint16_t calculatedCRC = BootSecure::CRCCalculator((uint8_t*)decryptedString.substring(0, pos3).c_str(), pos3);
        uint16_t receivedCRC = crc.toInt();
                
        if (calculatedCRC == receivedCRC) {
          // CRC is valid, update Prefs
          tft.println("CRC is valid, updating Prefs");
          strncpy((char*)Prefs.WIFISSID, ssid.c_str(), sizeof(Prefs.WIFISSID) - 1);
          Prefs.WIFISSID[sizeof(Prefs.WIFISSID) - 1] = '\0';
          
          strncpy((char*)Prefs.WIFIPWD, password.c_str(), sizeof(Prefs.WIFIPWD) - 1);
          Prefs.WIFIPWD[sizeof(Prefs.WIFIPWD) - 1] = '\0';
          
          strncpy((char*)Prefs.LMK_KEY, lmk_key.c_str(), sizeof(Prefs.LMK_KEY) - 1);
          Prefs.LMK_KEY[sizeof(Prefs.LMK_KEY) - 1] = '\0';
          
          // Mark Prefs as needing update and save to NVS
          Prefs.isUpToDate = false;
          Prefs.HAVECREDENTIALS = true;

          //attempt to reconnect to wifi
          if (connectWiFi()) {
            
            Prefs.MYIP = (uint32_t) WiFi.localIP(); //update this here just in case
            tft.println("Wifi connected");
              //check if we have weather data
            if (server.hasArg("street") && server.hasArg("city") && server.hasArg("state") && server.hasArg("zipcode")) {
              tft.print("Attempting geocordinate lookup from address...");
              String street = server.arg("street");
              String city = server.arg("city");
              String state = server.arg("state");
              String zipcode = server.arg("zipcode");
              if (handlerForWeatherAddress(street, city, state, zipcode)) {
                tft.println(" OK!");
              } else {
                tft.printf(String(" FAILED!\nLog in to this device at\n" + WiFi.localIP().toString() + "\nto update location!\n").c_str());
              }
            }
          } else {
            tft.printf("Failed to connect to wifi, manual reboot required.");
            while (true) {
              delay(1000);
            }
          }
          
          if (!BootSecure::setPrefs()) SerialPrint("Prefs update failed");
          delay(3000);
        // Save to NVS using BootSecure
          server.sendHeader("Location", "/WiFiConfig");
          server.send(302, "text/plain", "WiFi configuration updated successfully and saved to NVS. Attempting to connect...");
        

        } else {
          server.sendHeader("Location", "/WiFiConfig");
          server.send(302, "text/plain", "CRC validation failed. Configuration not updated.");
        }
      } else {
        server.sendHeader("Location", "/WiFiConfig");
        server.send(302, "text/plain", "Invalid data format. Configuration not updated.");
      }
    } else {
      tft.println("Decryption failed. Configuration not updated.");
      SerialPrint("Decryption failed. Configuration not updated.",true);
      delay(10000);
      server.sendHeader("Location", "/WiFiConfig");
      server.send(302, "text/plain", "Decryption failed. Configuration not updated.");
    }
    
    // Clean up
    BootSecure::zeroize(decodedData, decodedLength);
    BootSecure::zeroize(decryptedData, decodedLength);
    delete[] decodedData;
    delete[] decryptedData;

      //force prefs to save here
      tft.println("Prefs updated, rebooting");
      delay(3000);
      controlledReboot("Wifi credentials updated",RESETCAUSE::RESET_NEWWIFI,true);
    

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
  WEBHTML = WEBHTML + "<h2>" + (String) Prefs.DEVICENAME + " Weather Data</h2>";
  
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
    
    if (WeatherData.getCoordinatesFromAddress(street, city, state, zipCode)) {
      
      // Force weather update with new coordinates
      WeatherData.updateWeather(0);
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }


}


