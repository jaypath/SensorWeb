#include "server.hpp"




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
  //rerturn 0 if connected, else number of times I tried and failed
  uint8_t retries = 100; //max retries
  byte connected = 0;
  IPAddress temp;


  if (WifiStatus()) {
    WIFI_INFO.MYIP = WiFi.localIP();

    //WiFi.config(WIFI_INFO.MYIP, WIFI_INFO.DNS, WIFI_INFO.GATEWAY, WIFI_INFO.SUBNET);
    return connected;
  } else {

    if (getWiFiCredentials()) {

      #ifdef IGNORETHIS
      if (WIFI_INFO.MYIP[0]==0 || WIFI_INFO.MYIP[4]==0) {
        WIFI_INFO.MYIP[0]=0;    //will reassign this shortly
      } else {

        WiFi.config(WIFI_INFO.MYIP,  WIFI_INFO.GATEWAY, WIFI_INFO.SUBNET,WIFI_INFO.DNS);
      }
      #endif
      
      WiFi.mode(WIFI_STA);
      #ifdef _DEBUG
      SerialWrite((String) "wifi begin\n");
      #endif

      
      WiFi.begin((char *) WIFI_INFO.SSID, (char *) WIFI_INFO.PWD);


      if (WiFi.status() != WL_CONNECTED)  {
        #ifdef _DEBUG
          SerialWrite((String) "Connecting\n");
             #endif
          
        #ifdef _USESSD1306
          oled.print("Connecting");
        #endif
        for (byte j=0;j<retries;j++) {
          #ifdef _DEBUG
            SerialWrite((String) ".");
          #endif
          #ifdef _USESSD1306
            oled.print(".");
          #endif
    
          delay(250);
          if (WifiStatus()) {
            WIFI_INFO.MYIP = WiFi.localIP();
                
            #ifdef _DEBUG
              SerialWrite((String) "\nWifi OK. IP is " + (String) WIFI_INFO.MYIP.toString() + ".\n");
            #endif

            #ifdef _USESSD1306
              oled.clear();
              oled.setCursor(0,0);
              oled.println("WiFi OK.");
              oled.println("timesetup next.");      
            #endif
    
            return 0;
          }
          connected = j;
        }
              
      }

    } else {
      storeError("connectWiFi: no saved pwd/ssid");

      WiFi.mode(WIFI_AP);
      WiFi.softAP("ESPSERVER","CENTRAL.server1");

      delay(100);
      IPAddress Ip(192, 168, 10, 1);    //setto IP Access Point same as gateway
      IPAddress NMask(255, 255, 255, 0);
      WiFi.softAPConfig(Ip, Ip, NMask);

      return 255; //255 indicates that we are in AP mode!      
    }
  }


  storeError("connectWiFi: failed to connect");

  #ifdef _DEBUG
  SerialWrite((String) "Failed to connect after " + (String) connected + " trials.\n");
       #endif
          

  return connected;

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
  String URL = IPbytes2String(Sensors[j].IP) + "/UPDATEALLSENSORREADS";
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
  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) I.SERVERNAME + "</title>";
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
  WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) IPbytes2String(Sensors[j].IP) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) IPbytes2String(Sensors[j].IP) + "</a></td>";
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
  WEBHTML = WEBHTML + "<td>" + (String) IPbytes2String(Sensors[j].MAC,6) + "</td>";
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
  struct SensorVal S;
  uint32_t starttime=0, endtime=0;


    if (server.args()==0) {
      

      WEBHTML="Inappropriate call... use RETRIEVEDATA?ID=1.1.1&N=100&endtime=1731761847&delta=1 or RETRIEVEDATA?ID=1.1.1&N=100&starttime=1731700000&endtime=1731761847&delta=10";
      serverTextClose(401,false);

      return;
    }

    for (byte k=0;k<server.args();k++) {
        if ((String)server.argName(k) == (String)"ID")  breakLOGID(server.arg(k),&S);
        if ((String)server.argName(k) == (String)"MAC")  breakMAC(server.arg(k),&S);
        if ((String)server.argName(k) == (String)"ardID")  S.ardID=server.arg(k).toInt(); 
        if ((String)server.argName(k) == (String)"snsType")  S.snsType=server.arg(k).toInt(); 
        if ((String)server.argName(k) == (String)"snsID")  S.snsID=server.arg(k).toInt(); 
        if ((String)server.argName(k) == (String)"N")  N=server.arg(k).toInt(); 
        if ((String)server.argName(k) == (String)"starttime")  starttime=server.arg(k).toInt(); 
        if ((String)server.argName(k) == (String)"endtime")  endtime=server.arg(k).toInt(); 
        if ((String)server.argName(k) == (String)"delta")  delta=server.arg(k).toInt(); //read only every Nth value
    }

    if (S.ardID==0 || S.snsType == 0 || S.snsID==0) {
      WEBHTML="Inappropriate call... invalid arduino sensor ID";
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
  if (starttime>0)  success =   readSensorSD(&S,t,v,&N,&sampn,starttime,endtime,delta);
  else    success =   readSensorSD(&S,t,v,&N,&sampn,endtime,delta); //this fills from tn backwards to N*delta samples

  if (success == false)  {
    WEBHTML= "Failed to read associated file.";
    serverTextClose(401,false);

    return;
  }


  int sn = findDev(&S,false);


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

  if (sn<0 || sn>=SENSORNUM)   WEBHTML += "WARNING!! Arduino: " + (String) S.ardID + "." + (String) S.snsType + "." + (String) S.snsID + " was NOT found in the active list, though I did find an associated file. <br>";
  else {
      WEBHTML += "Request for Arduino: " + (String) Sensors[sn].snsName + " " + (String) Sensors[sn].ardID + "." + (String) (String) Sensors[sn].snsType + "." + (String) Sensors[sn].snsID + "<br>";
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
    WEBHTML += "hAxis: {title: 'Historical data for " + (String) Sensors[sn].snsName + " in hours'}, \n";
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
SensorVal S;
S.SendingInt = 86400; //default is 1 day for expiration (note that if flag bit 7 is 1 then an alarm should be raised if no response)

  if (server.args()==0) return;

  for (byte k=0;k<server.args();k++) {
      if ((String)server.argName(k) == (String)"logID")  breakLOGID(server.arg(k),&S);
      if ((String)server.argName(k) == (String)"IP") {
        IPString2ByteArray(String(server.arg(k)),S.IP);
      }
      if ((String)server.argName(k) == (String)"MAC")  breakMAC(server.arg(k),&S);
      if ((String)server.argName(k) == (String)"varName") snprintf(S.snsName,29,"%s", server.arg(k).c_str());
      if ((String)server.argName(k) == (String)"varValue") S.snsValue = server.arg(k).toDouble();
      if ((String)server.argName(k) == (String)"timeLogged") S.timeRead = server.arg(k).toDouble();      //time logged at sensor is time read by me
      if ((String)server.argName(k) == (String)"Flags") S.Flags = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"SendingInt") S.SendingInt = server.arg(k).toInt();
  }

  S.timeLogged = I.currentTime; //time logged by me is when I received this.
  S.expired = false;
  
  if (S.timeRead == 0  || S.timeRead < I.currentTime-24*60*60 || S.timeRead > I.currentTime+24*60*60)     S.timeRead = I.currentTime;

  storeSensorSD(&S); //store this reading on SD
  
  int sn = findDev(&S,true);

     //special cases
      
  //bmp temp received... check for AHT
  if (S.snsType == 10 && findSns(4,false)>-1) {
    if (sn>=0) {
      //already stored a bmp, so erase that
      initSensor(findSns(10,false));
    }
    WEBHTML= "Received, but rejected the data because this is BMP and you sent AHT"; // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    serverTextClose(202,false);

    return;
  }

  //AHT_T received... check for BMP
  if (S.snsType == 4 && findSns(10,false)>-1) {
    if (sn<0)    sn = findSns(10,false); //replace BMP_t with AHT_t
    else {
      //AHT was present so I will replace with this read, but bmp was ALSO present... erase that
      initSensor(findSns(10,false));
    }
  }

  //battery voltage received... check for bat %
  if (S.snsType == 60 && findSns(61,false)>-1) {
    if (sn>=0) {
      //already stored a voltage, so erase that
      initSensor(findSns(60,false));
    }
    
    WEBHTML= "Received, but rejected the data because this is voltage and you sent %"; // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    serverTextClose(202,false);

    return;
  }

  //baT % received... check for voltage
  if (S.snsType == 61 && findSns(60,false)>-1) {
    if (sn<0)    sn = findSns(60,false); //replace volt with %
    else {
      //% was present so I will replace with this read, but volt was ALSO present... erase that
      initSensor(findSns(60,false));
    }
  }

// END special cases

 

  if((S.snsType==9 || S.snsType == 13) && (LAST_BAR_READ==0 || LAST_BAR_READ < I.currentTime - 60*60)) { //pressure
    LAST_BAR_READ = S.timeRead;
    LAST_BAR = S.snsValue;
  }

  String stemp = S.snsName;
  if((stemp.indexOf("Outside")>-1 && S.snsType==61 ) && (LAST_BAT_READ==0 || LAST_BAT_READ < I.currentTime - 60*60 || LAST_BAT_READ > I.currentTime)) { //outside battery
    LAST_BAT_READ = S.timeRead;
    pushDoubleArray(batteryArray,48,S.snsValue);
  }


  if (sn<0) {
    WEBHTML = "Received, but rejected the data because I could not add another sensor"; // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    serverTextClose(201,false);

    return;
  }
  Sensors[sn] = S;
  
  WEBHTML="Received Data";
  serverTextClose(200,false);

  
}

