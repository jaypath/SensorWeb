#include "server.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "SDCard.hpp"
#include <esp_system.h>




//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif
#ifdef _USE32

    WebServer server(80);
#endif




#ifdef _DEBUG
void SerialWrite(String msg) {
    Serial.printf("%s",msg.c_str());
  return;
}
#endif


//server
String WEBHTML;
WiFi_type WIFI_INFO;

extern uint32_t I_currentTime;
extern uint32_t lastPwdRequestMinute;
extern STRUCT_PrefsH Prefs;
extern bool requestWiFiPassword(const uint8_t* serverMAC);

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
    WIFI_INFO.HAVECREDENTIALS = true;
    return true;
  }
  //just because I don't have wifi doesn't mean I don't have creds
//    WIFI_INFO.HAVECREDENTIALS = false;

    return false;
  
}


uint8_t connectWiFi() {
  static uint32_t lastRequestTime = 0;
  static uint32_t lastCycleTime = 0;
  static uint8_t backoff = 1;
  static uint32_t lastBackoffTime = 0;
  uint32_t nowt = now();
  bool hasServer = false;
  for (int i = 0; i < 6; ++i) if (Prefs.LAST_SERVER[i] != 0) hasServer = true;
  bool hasSSID = false;
  for (int i = 0; i < 33; ++i) if (Prefs.WIFISSID[i] != 0) hasSSID = true;

  // Exponential backoff for WiFi reconnects
  if (WifiStatus()) {
    WIFI_INFO.MYIP = WiFi.localIP();
    backoff = 1;
    Prefs.WIFI_RECOVERY_STAGE = 0;
    Prefs.WIFI_RECOVERY_SERVER_INDEX = 0;
    return 0;
  } else {
    if (hasServer && hasSSID) {
      // Stage 0: Try Prefs.LAST_SERVER for 3 minutes
      if (Prefs.WIFI_RECOVERY_STAGE == 0) {
        if (nowt - lastRequestTime >= 60) {
          uint8_t nonce[8]; esp_fill_random(nonce, 8);
          requestWiFiPassword(Prefs.LAST_SERVER, nonce);
          lastRequestTime = nowt;
        }
        if (nowt - lastRequestTime >= 180) {
          Prefs.WIFI_RECOVERY_STAGE = 1;
          Prefs.WIFI_RECOVERY_SERVER_INDEX = 0;
          lastCycleTime = nowt;
        }
      }
      // Stage 1: Cycle through all known servers every minute
      if (Prefs.WIFI_RECOVERY_STAGE == 1) {
        if (nowt - lastCycleTime >= 60) {
          // Find all servers
          int numServers = 0;
          uint8_t serverMACs[10][6] = {};
          for (int16_t i = 0; i < Sensors.getNumDevices() && numServers < 10; ++i) {
            DevType* dev = Sensors.getDeviceByIndex(i);
            if (dev && dev->IsSet && dev->devType >= 100) {
              for (int j = 0; j < 6; ++j)
                serverMACs[numServers][5-j] = (dev->MAC >> (8*j)) & 0xFF;
              numServers++;
            }
          }
          if (numServers > 0) {
            uint8_t idx = Prefs.WIFI_RECOVERY_SERVER_INDEX % numServers;
            uint8_t nonce[8]; esp_fill_random(nonce, 8);
            requestWiFiPassword(serverMACs[idx], nonce);
            Prefs.WIFI_RECOVERY_SERVER_INDEX = (idx + 1) % numServers;
            lastCycleTime = nowt;
          }
        }
      }
    }
    // Fallback to AP mode at 5 minutes
    if (nowt - lastRequestTime >= 300) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(generateAPSSID().c_str(), "S3nsor.N3t!");
      delay(100);
      WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
      #ifdef HAS_TFT
      tft.clear();
      tft.setCursor(0, 0);
      tft.setTextColor(TFT_RED);
      tft.printf("WiFi failed.\nAP mode enabled.\nConnect to: %s\nIP: 192.168.4.1", WiFi.softAPSSID().c_str());
      #endif
    }
    // Force reboot at 30 minutes
    if (nowt - lastRequestTime >= 1800) {
      controlledReboot("WiFi/ESPNow failed for 30 min, rebooting", RESET_WIFI, true);
    }
    // Exponential backoff for WiFi reconnects
    if (nowt - lastBackoffTime >= backoff) {
      if (getWiFiCredentials()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin((char *) WIFI_INFO.SSID, (char *) WIFI_INFO.PWD);
        delay(250);
        if (WifiStatus()) {
          WIFI_INFO.MYIP = WiFi.localIP();
          backoff = 1;
          Prefs.WIFI_RECOVERY_STAGE = 0;
          Prefs.WIFI_RECOVERY_SERVER_INDEX = 0;
          return 0;
        }
      }
      lastBackoffTime = nowt;
      if (backoff < 60) backoff *= 2;
      if (backoff > 60) backoff = 60;
    }
    return 255;
  }
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
  String URL = IPToString(Sensors[j].IP) + "/UPDATEALLSENSORREADS";
  Server_Message(URL, payload, httpCode);

  server.sendHeader("Location", "/");
  WEBHTML= "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}

void handleGETSTATUS() {
  //someone requested the server's status
  WEBHTML = "STATUS:" + (String) LAST_WEB_REQUEST + ";";
  WEBHTML += "ALIVESINCE:" + (String) I.ALIVESINCE + ";";
  WEBHTML += "NUMDEVS:" + (String) countDev() + ";";
  

  serverTextClose(200,false);   // Send HTTP status 200 (Ok) and send some text to the browser/client

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


  initCreds(&WIFI_INFO);

  String creds = "";

  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SSID") snprintf((char *) WIFI_INFO.SSID,64,"%s",server.arg(i).c_str());
    if (server.argName(i)=="PWD") snprintf((char *) WIFI_INFO.PWD,64,"%s",server.arg(i).c_str());
  }

	
  if (putWiFiCredentials()) {
    WIFI_INFO.HAVECREDENTIALS = true;
    getWiFiCredentials();
    WEBHTML= "OK, stored new credentials.\nSSID:" + (String) (char *) WIFI_INFO.SSID + "\nPWD: NOT_SHOWN";  
    serverTextClose(201,false);

  }   else {
    WEBHTML= "OK, Cleared credentials!";  
    WIFI_INFO.HAVECREDENTIALS = false;
    serverTextClose(201,false);
    controlledReboot("User cleared credentials",RESET_USER);
  }


//  server.sendHeader("Location", "/");//This Line goes to root page
  

}



void handleSETTINGS() {
  
  LAST_WEB_REQUEST = I.currentTime; //this is the time of the last web request
  WEBHTML = "";
  serverTextHeader();

  WEBHTML = WEBHTML + "<body>";  
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

WEBHTML += "Number of sensors: " + (String) countDev() + " / " + (String) SENSORNUM + "<br>";
WEBHTML = WEBHTML + "Alive since: " + dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss") + "<br>";
WEBHTML = WEBHTML + "Last error: " + (String) I.lastError + " @" + (String) dateify(I.lastErrorTime) + "<br>";
WEBHTML = WEBHTML + "Last known reset: " + (String) lastReset2String()  + "<br>";
WEBHTML = WEBHTML + "---------------------<br>";      

WEBHTML += "Weather last retrieval attempted: " + (String) dateify(WeatherData.lastUpdateAttempt);
if (WeatherData.lastUpdateAttempt != WeatherData.lastUpdateT) WEBHTML += " [last successful @" + (String) dateify(WeatherData.lastUpdateT) + "]";
else WEBHTML += " [succeeded]";
WEBHTML += "<br>";
WEBHTML += "NOAA address: " + WeatherData.getGrid(0) + "<br>";
WEBHTML += "Weather now: " + (String) WeatherData.getTemperature() + "F and code " +  (String) WeatherData.getWeatherID() + "<br>";
WEBHTML += "Weather in 1 hour: " + (String) WeatherData.getTemperature(I.currentTime+3600) + "F and code " +  (String) WeatherData.getWeatherID(I.currentTime+3600) + "<br>";
WEBHTML = WEBHTML + "Sunrise " + dateify(WeatherData.sunrise,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";
WEBHTML = WEBHTML + "Sunset " + dateify(WeatherData.sunset,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";
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


  WEBHTML += "<FORM action=\"/UPDATEDEFAULTS\" method=\"get\">";
  WEBHTML += "<p style=\"font-family:arial, sans-serif\">";
  WEBHTML += "<label for=\"HoulyInterval\">Hourly interval for display</label>";
  WEBHTML += "<input type=\"text\" id=\"HourlyInterval\" name=\"HourlyInterval\" value=\"" + String(I.HourlyInterval) + "\" maxlength=\"15\"><br>";  

  WEBHTML += "<label for=\"FLAGVIEW\">Seconds to show Flag screen</label>";
  WEBHTML += "<input type=\"text\" id=\"FLAGVIEW\" name=\"FLAGVIEW\" value=\"" + String(I.flagViewTime) + "\" maxlength=\"15\"><br>";  

  WEBHTML += "<label for=\"CURRENTCONDITIONTIME\">MINUTES to show Current Conditions</label>";
  WEBHTML += "<input type=\"text\" id=\"CURRENTCONDITIONTIME\" name=\"CURRENTCONDITIONTIME\" value=\"" + String(I.currentConditionTime) + "\" maxlength=\"15\"><br>";  

  WEBHTML += "<label for=\"WEATHERTIME\">MINUTES before weather screen update</label>";
  WEBHTML += "<input type=\"text\" id=\"WEATHERTIME\" name=\"WEATHERTIME\" value=\"" + String(I.weatherTime) + "\" maxlength=\"15\"><br>";  
  
  #ifdef _DEBUG
    WEBHTML += "<label for=\"TESTRUN\">Debugging mode</label>";
    WEBHTML += "<input type=\"text\" id=\"TESTRUN\" name=\"TESTRUN\" value=\"" + (String) TESTRUN + "\" maxlength=\"15\"><br>";  
  #endif

  WEBHTML +=  "<br>";
  WEBHTML += "<button type=\"submit\">Submit</button><br>";

  WEBHTML += "</p></font></form>";

  WEBHTML = WEBHTML + "---------------------<br>";      
        
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
  WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) IPToString(Sensors[j].IP) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) IPToString(Sensors[j].IP) + "</a></td>";
  WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].ardID + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsName + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsValue + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) bitRead(Sensors[j].Flags,0) + (String) (bitRead(Sensors[j].Flags,6) ? "*" : "" ) + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) ((Sensors[j].expired==0)?((bitRead(Sensors[j].Flags,7))?"N*":"n"):((bitRead(Sensors[j].Flags,7))?"<font color=\"#EE4B2B\">Y*</font>":"<font color=\"#EE4B2B\">y</font>")) + "</td>";
  
  byte delta=2;
  if (Sensors[j].snsType==4 || Sensors[j].snsType==1 || Sensors[j].snsType==10) delta = 10;
  if (Sensors[j].snsType==3) delta = 1;
  if (Sensors[j].snsType==9) delta = 3; //bmp
  if (Sensors[j].snsType==60 || Sensors[j].snsType==61) delta = 3; //batery
  if (Sensors[j].snsType>=50 && Sensors[j].snsType<60) delta = 15; //HVAC
  
  WEBHTML = WEBHTML + "<td><a href=\"http://192.168.68.93/RETRIEVEDATA?ID=" + (String) Sensors[j].ardID + "." + (String) Sensors[j].snsType + "." +(String) Sensors[j].snsID + "&endtime=" + (String) (I.currentTime) + "&N=100&delta=" + (String) delta + "\" target=\"_blank\" rel=\"noopener noreferrer\">History</a></td>";
  WEBHTML = WEBHTML + "<td>" + (String) IPToString(Sensors[j].MAC,6) + "</td>";
  WEBHTML = WEBHTML + "</tr>";
}



void handlerForRoot(bool allsensors) {
  
  LAST_WEB_REQUEST = I.currentTime; //this is the time of the last web request
  WEBHTML = "";
  serverTextHeader();

  if (WIFI_INFO.HAVECREDENTIALS==true) {



  #ifdef _USEROOTCHART
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";
  #endif
  WEBHTML = WEBHTML + "<body>";  
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br></h2>";

 
  WEBHTML = WEBHTML + "---------------------<br>";      

  if (I.isFlagged || I.isHeat || I.isAC) {
    WEBHTML = WEBHTML + "<font color=\"#EE4B2B\">";      
    
    if (I.isHeat) WEBHTML = WEBHTML + "Heat is on<br>";
    if (I.isAC) WEBHTML = WEBHTML + "AC is on<br>";
    if (I.isFlagged) WEBHTML = WEBHTML + "Critical flags  (" + (String) I.isFlagged + "):<br>";
    if (I.isLeak) WEBHTML = WEBHTML + "     Leak has been detected (" + (String) I.isLeak + ")!!!<br>";
    if (I.isHot) WEBHTML = WEBHTML + "     Interior room(s) over max temp (" + (String) I.isHot + ")<br>";
    if (I.isCold) WEBHTML = WEBHTML + "     Interior room(s) below min temp (" + (String) I.isCold + ")<br>";
    if (I.isSoilDry) WEBHTML = WEBHTML + "     Plant(s) dry (" + (String) I.isSoilDry + ")<br>";
    if (I.isExpired) WEBHTML = WEBHTML + "     Critical Sensors Expired (" + (String) I.isExpired + ")<br>";

    WEBHTML = WEBHTML + "</font>---------------------<br>";      
  }


    byte used[SENSORNUM];
    for (byte j=0;j<SENSORNUM;j++)  {
      used[j] = 255;
    }

    byte usedINDEX = 0;  


  WEBHTML = WEBHTML + "<p><table id=\"Logs\" style=\"width:900px\">";      
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><p><button onclick=\"sortTable(0)\">IP Address</button></p></th style=\"width:50px\"><th>ArdID</th><th style=\"width:200px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:100px\"><button onclick=\"sortTable(4)\">Sns Type</button></p></th style=\"width:100px\"><th><button onclick=\"sortTable(5)\">Flagged</button></p></th><th style=\"width:250px\">Last Recvd</th><th style=\"width:50px\">EXP</th><th style=\"width:100px\">Plot</th><th style=\"width:100px\">MAC</th></tr>"; 
  for (byte j=0;j<SENSORNUM;j++)  {
    if (allsensors && bitRead(Sensors[j].Flags,1)==0) continue;
    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      rootTableFill(j);
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (allsensors && bitRead(Sensors[jj].Flags,1)==0) continue;
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
          rootTableFill(jj);
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

  #ifdef _WEBDEBUG
      WEBHTML += "<p>WEBDEBUG: <br>"+ WEBDEBUG + "</p><br>";
    #endif

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
      storeScreenInfoSD();
      writeSensorsSD();
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


void handleUPDATEDEFAULTS() {
  uint8_t temp = 0;

  if (server.args()==0) return;
  for (uint8_t i = 0; i < server.args(); i++) {
    //error check for hourly interval.
    if (server.argName(i)=="HourlyInterval") server.arg(i).toInt();

    if (server.argName(i)=="FLAGVIEW") I.flagViewTime =  server.arg(i).toInt();

    if (server.argName(i)=="CURRENTCONDITIONTIME") I.currentConditionTime =  server.arg(i).toInt();
    
    if (server.argName(i)=="WEATHERTIME") I.weatherTime =  server.arg(i).toInt();
   
    
    #ifdef _DEBUG
    if (server.argName(i)=="TESTRUN") {
      TESTRUN =  server.arg(i).toInt();

      Serial.printf("TESRUN is now %u\n",TESTRUN);
    }
    #endif

  }
  //force screen redraw
  I.lastClock = 0; 
  I.lastCurrentCondition=0;
  I.lastFlagView=0;
  I.lastHeader=0;
  I.lastWeather=0;


  server.sendHeader("Location", "/");//This Line goes to root page
  WEBHTML= "Updated defaults.";  
  serverTextClose(302,false);


}

void handleNotFound(){
  WEBHTML= "404: Not found"; // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
  serverTextClose(404,false);

}

void handleRETRIEVEDATA() {
  byte  N=0,delta=1;
  uint8_t snsType = 0, snsID = 0;
  uint64_t deviceMAC = 0;
  uint32_t starttime=0, endtime=0;

  if (server.args()==0) {
    WEBHTML="Inappropriate call... use RETRIEVEDATA?MAC=1234567890AB&type=1&id=1&N=100&endtime=1731761847&delta=1 or RETRIEVEDATA?MAC=1234567890AB&type=1&id=1&N=100&starttime=1731700000&endtime=1731761847&delta=10";
    serverTextClose(401,false);
    return;
  }

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"MAC") {
      String macStr = server.arg(k);
      deviceMAC = strtoull(macStr.c_str(), NULL, 16);
    }
    if ((String)server.argName(k) == (String)"type")  snsType=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"id")  snsID=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"N")  N=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"starttime")  starttime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"endtime")  endtime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"delta")  delta=server.arg(k).toInt(); //read only every Nth value
  }

  if (deviceMAC==0 || snsType == 0 || snsID==0) {
    WEBHTML="Inappropriate call... invalid device MAC or sensor ID";
    serverTextClose(302,false);
    return;
  }

  if (N>100) N=100; //don't let the array get too large!
  if (N==0) N=50;
  if (endtime==0) endtime--; //this make endtime the max value, will just read N values.
  if (endtime<starttime) endtime = -1;
  if (delta==0) delta=1;

  uint32_t t[N]={0};
  double v[N]={0};
  uint32_t sampn=0; //sample number
  
  bool success=false;
  if (starttime>0)  success = readSensorDataSD(deviceMAC, snsType, snsID, t, v, &N, &sampn, starttime, endtime, delta);
  else    success = readSensorDataSD(deviceMAC, snsType, snsID, t, v, &N, &sampn, endtime, delta); //this fills from tn backwards to N*delta samples

  if (success == false)  {
    WEBHTML= "Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  // Find sensor in Devices_Sensors class
  int16_t sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
  SnsType* sensor = Sensors.getSensorByIndex(sensorIndex);

  WEBHTML = "<!DOCTYPE html><html><head><title>Pleasant Weather Server</title>\n";
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
      WEBHTML += "[" + (String) (((float) -1*(I.currentTime-t[jj]))/3600) + "," + (String) v[jj] + "]";
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
    WEBHTML += "Returned " + (String) N + " avg samples out of " + (String) sampn + " individual samples. <br>\n";

    WEBHTML += "unixtime,value<br>\n";
  for (byte j=0;j<N;j++)     WEBHTML += (String) t[j] + "," + (String) v[j] + "<br>\n";

  serverTextClose(200);
}




void handlePost() {
    // Check if this is JSON data (new format)
    if (server.hasArg("plain")) {
        String postData = server.arg("plain");
        Serial.println("Received POST data: " + postData);
        
        // Try to parse as JSON first
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, postData);
        
        if (!error) {
            // Handle new-style MAC-based messages
            if (doc.containsKey("mac")) {
                // Extract MAC address
                String macStr = doc["mac"];
                uint64_t mac = strtoull(macStr.c_str(), NULL, 16);
                
                // Extract IP address (optional)
                uint32_t ip = 0;
                if (doc.containsKey("ip")) {
                    String ipStr = doc["ip"];
                    ip = IPAddress().fromString(ipStr);
                }
                
                // Extract device name (optional)
                String devName = "";
                if (doc.containsKey("devName")) {
                    devName = doc["devName"].as<String>();
                }
                
                // Extract sensors array
                if (doc.containsKey("sensors") && doc["sensors"].is<JsonArray>()) {
                    JsonArray sensorsArray = doc["sensors"];
                    
                    for (JsonObject sensor : sensorsArray) {
                        if (sensor.containsKey("type") && sensor.containsKey("id") && 
                            sensor.containsKey("name") && sensor.containsKey("value")) {
                            
                            uint8_t snsType = sensor["type"];
                            uint8_t snsID = sensor["id"];
                            String snsName = sensor["name"];
                            double snsValue = sensor["value"];
                            
                            // Optional fields
                            uint32_t timeRead = sensor.containsKey("timeRead") ? sensor["timeRead"] : I.currentTime;
                            uint32_t timeLogged = sensor.containsKey("timeLogged") ? sensor["timeLogged"] : I.currentTime;
                            uint32_t sendingInt = sensor.containsKey("sendingInt") ? sensor["sendingInt"] : 3600;
                            uint8_t flags = sensor.containsKey("flags") ? sensor["flags"] : 0;
                            
                            // Add sensor to Devices_Sensors class
                            int16_t sensorIndex = Sensors.addSensor(mac, ip, snsType, snsID, 
                                                                   snsName.c_str(), snsValue, 
                                                                   timeRead, timeLogged, sendingInt, flags);
                            
                            if (sensorIndex >= 0) {
                                // Store individual sensor data to SD card
                                storeSensorDataSD(sensorIndex);
                                Serial.printf("Added sensor: type=%d, id=%d, name=%s, value=%.2f\n", 
                                            snsType, snsID, snsName.c_str(), snsValue);
                            } else {
                                Serial.printf("Failed to add sensor: type=%d, id=%d\n", snsType, snsID);
                            }
                        }
                    }
                }
                
                server.send(200, "text/plain", "Data received successfully");
                return;
            }
            
            // Handle old-style IP-based messages (legacy compatibility)
            if (doc.containsKey("ip")) {
                String ipStr = doc["ip"];
                uint32_t ip = IPAddress().fromString(ipStr);
                
                // Use IP address as MAC address for better device identification
                uint64_t deviceMAC = ((uint64_t)ip << 32) | 0x00000000FF000000; // IP-based MAC with prefix
                
                if (doc.containsKey("sensors") && doc["sensors"].is<JsonArray>()) {
                    JsonArray sensorsArray = doc["sensors"];
                    
                    for (JsonObject sensor : sensorsArray) {
                        if (sensor.containsKey("type") && sensor.containsKey("id") && 
                            sensor.containsKey("name") && sensor.containsKey("value")) {
                            
                            uint8_t snsType = sensor["type"];
                            uint8_t snsID = sensor["id"];
                            String snsName = sensor["name"];
                            double snsValue = sensor["value"];
                            
                            // Optional fields
                            uint32_t timeRead = sensor.containsKey("timeRead") ? sensor["timeRead"] : I.currentTime;
                            uint32_t timeLogged = sensor.containsKey("timeLogged") ? sensor["timeLogged"] : I.currentTime;
                            uint32_t sendingInt = sensor.containsKey("sendingInt") ? sensor["sendingInt"] : 3600;
                            uint8_t flags = sensor.containsKey("flags") ? sensor["flags"] : 0;
                            
                            // Add sensor to Devices_Sensors class using IP-derived MAC
                            int16_t sensorIndex = Sensors.addSensor(deviceMAC, ip, snsType, snsID, 
                                                                   snsName.c_str(), snsValue, 
                                                                   timeRead, timeLogged, sendingInt, flags);
                            
                            if (sensorIndex >= 0) {
                                // Store individual sensor data to SD card
                                storeSensorDataSD(sensorIndex);
                                Serial.printf("Added legacy sensor: type=%d, id=%d, name=%s, value=%.2f\n", 
                                            snsType, snsID, snsName.c_str(), snsValue);
                            }
                        }
                    }
                }
                
                server.send(200, "text/plain", "Legacy data received successfully");
                return;
            }
            
            server.send(400, "text/plain", "Invalid JSON message format");
            return;
        }
    }
    
    // Handle old HTML form data (backward compatibility)
    if (server.args() > 0) {
        Serial.println("Received HTML form data");
        
        // Parse old-style HTML form parameters
        uint64_t deviceMAC = 0;
        uint32_t deviceIP = 0;
        String devName = "";
        uint8_t snsType = 0;
        uint8_t snsID = 0;
        String snsName = "";
        double snsValue = 0;
        uint32_t timeRead = I.currentTime;
        uint32_t timeLogged = I.currentTime;
        uint32_t sendingInt = 3600;
        uint8_t flags = 0;
        
        for (byte k = 0; k < server.args(); k++) {
            String argName = server.argName(k);
            String argValue = server.arg(k);
            
            if (argName == "MAC") {
                deviceMAC = strtoull(argValue.c_str(), NULL, 16);
            } else if (argName == "IP") {
                deviceIP = IPAddress().fromString(argValue);
            } else if (argName == "devName") {
                devName = argValue;
            } else if (argName == "snsType") {
                snsType = argValue.toInt();
            } else if (argName == "snsID") {
                snsID = argValue.toInt();
            } else if (argName == "varName") {
                snsName = argValue;
            } else if (argName == "varValue") {
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
        
        // Validate required fields
        if (snsType == 0 || snsName.length() == 0) {
            server.send(400, "text/plain", "Missing required fields: snsType and varName");
            return;
        }
        
        // If no MAC provided but IP is available, use IP as MAC address
        if (deviceMAC == 0 && deviceIP != 0) {
            // Create a unique MAC address from the IP address
            // Format: 0x00000000FF000000 | (IP << 32)
            // This creates a unique identifier that's clearly IP-based
            deviceMAC = ((uint64_t)deviceIP << 32) | 0x00000000FF000000;
            Serial.printf("Using IP %s as MAC address: %016llX\n", 
                         IPAddress(deviceIP).toString().c_str(), deviceMAC);
        }
        
        // Add sensor to Devices_Sensors class
        int16_t sensorIndex = Sensors.addSensor(deviceMAC, deviceIP, snsType, snsID, 
                                               snsName.c_str(), snsValue, 
                                               timeRead, timeLogged, sendingInt, flags);
        
        if (sensorIndex >= 0) {
            // Store individual sensor data to SD card
            storeSensorDataSD(sensorIndex);
            Serial.printf("Added HTML form sensor: type=%d, id=%d, name=%s, value=%.2f\n", 
                        snsType, snsID, snsName.c_str(), snsValue);
            server.send(200, "text/plain", "HTML form data received successfully");
        } else {
            server.send(500, "text/plain", "Failed to add sensor");
        }
        return;
    }
    
    server.send(400, "text/plain", "No data received");
}

// Generate AP SSID based on MAC address: "SensorNet-" + last 3 bytes of MAC in hex
String generateAPSSID() {
    char ssid[20];
    // Format: "SensorNet-" + last 3 bytes of MAC in hex (6 characters)
    snprintf(ssid, sizeof(ssid), "SensorNet-%02X%02X%02X", 
             WIFI_INFO.MAC[3], WIFI_INFO.MAC[4], WIFI_INFO.MAC[5]);
    return String(ssid);
}


