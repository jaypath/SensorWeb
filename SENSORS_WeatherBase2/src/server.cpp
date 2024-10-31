//#define _DEBUG
#include <Arduino.h>
#include <SD.h>

#include <utility.hpp>
#include "server.hpp"
#include <timesetup.hpp>
#include <WiFiClient.h>





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
  #ifdef _DEBUG
        Serial.printf("getCert: started\n");
  #endif

  File f = SD.open(filename, FILE_READ);
  String s="";
  if (f) {  
     if (f.available() > 0) {
        s = f.readString(); 
     }
     f.close();
    }    

  #ifdef _DEBUG
        Serial.printf("getCert: cert length is %u\n", s.length());
  #endif

    
    return s;
}

bool Server_SecureMessage(String& URL, String& payload, int& httpCode,  String& cacert) { 
  HTTPClient http;
  WiFiClientSecure wfclient;
  wfclient.setCACert(cacert.c_str());
#ifdef _DEBUG
      Serial.printf("Server_message: url is %s\n",URL.c_str());      
    #endif

  if(WiFi.status()== WL_CONNECTED){
    I.wifi=10;
    http.begin(wfclient,URL.c_str());
    //http.useHTTP10(true);
    httpCode = http.GET();
    #ifdef _DEBUG
      Serial.print("Server_message: httpcode: ");
      Serial.println(httpCode);
    #endif

    payload = http.getString();
    #ifdef _DEBUG
      Serial.print("Server_message: httpcode: ");
      Serial.println(payload);
    #endif

    http.end();
    return true;
  } 

  return false;
}

bool Server_Message(String& URL, String& payload, int &httpCode) { 
  
  if (URL.indexOf("https:")>-1) return false; //this cannot connect to https server!
  HTTPClient http;
  WiFiClient wfclient;
  
  #ifdef _DEBUG
        Serial.printf("Server_message: URL Requested is: %s\n", URL.c_str());
  #endif

  if(WiFi.status()== WL_CONNECTED){
    I.wifi=10;
    http.begin(wfclient,URL.c_str());
    //http.useHTTP10(true);
    httpCode = http.GET();
    #ifdef _DEBUG
      Serial.print("Server_message: httpcode: ");
      Serial.println(httpCode);
    #endif

    payload = http.getString();
  
    http.end();
    return true;
  } 

  return false;
}


bool WifiStatus(void) {
  if (WiFi.status() == WL_CONNECTED) return true;
  return false;
  
}
uint8_t connectWiFi()
{
  //rerturn 0 if connected, else number of times I tried and failed
  uint8_t retries = 100;
  byte connected = 0;
  IPAddress temp;


  if (WifiStatus()) {
    WIFI_INFO.MYIP = WiFi.localIP();

    //WiFi.config(WIFI_INFO.MYIP, WIFI_INFO.DNS, WIFI_INFO.GATEWAY, WIFI_INFO.SUBNET);
    return connected;
  } else {
    if (WIFI_INFO.MYIP[0]==0 || WIFI_INFO.MYIP[4]==0) {
      WIFI_INFO.MYIP[0]=0;    //will reassign this shortly
    } else {

      WiFi.config(WIFI_INFO.MYIP,  WIFI_INFO.GATEWAY, WIFI_INFO.SUBNET,WIFI_INFO.DNS);
    }
  }
  
  WiFi.mode(WIFI_STA);
  #ifdef _DEBUG
  SerialWrite((String) "wifi begin\n");
       #endif

  
  WiFi.begin(ESP_SSID, ESP_PASS);

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

  #ifdef _DEBUG
  SerialWrite((String) "Failed to connect after " + (String) connected + " trials.\n");
       #endif
          

  return connected;

}



void handleReboot() {
  server.send(200, "text/plain", "Rebooting in 10 sec");  //This returns to the main page
  delay(10000);
  ESP.restart();
}

void handleCLEARSENSOR() {
  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  initSensor(j);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}

void handleREQUESTUPDATE() {

  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  String payload;
  int httpCode;
  String URL = Sensors[j].IP.toString() + "/UPDATEALLSENSORREADS";
  Server_Message(URL, payload, httpCode);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}

void handleGETSTATUS() {
  //someone requested the server's status
  WEBHTML = "STATUS:" + (String) LAST_WEB_REQUEST + ";";
  WEBHTML += "ALIVESINCE:" + (String) ALIVESINCE + ";";
  WEBHTML += "NUMDEVS:" + (String) countDev() + ";";
  
  server.send(200, "text/plain", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

  return;

}

void handleREQUESTWEATHER() {
//if no parameters passed, return current temp, max, min, today weather ID, pop, and snow amount
//otherwise, return the index value for the requested value

  int8_t dailyT[2];
  time_t t=now();

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
      if (server.argName(i)=="hourly_temp") WEBHTML += (String) WeatherData.getTemperature(server.arg(i).toInt(),false,true) + ";";
      WeatherData.getDailyTemp(server.arg(i).toInt(),dailyT);
        if (server.argName(i)=="daily_tempMax") WEBHTML += (String) dailyT[0] + ";";
      if (server.argName(i)=="daily_tempMin") WEBHTML += (String) dailyT[1] + ";";
      if (server.argName(i)=="daily_weatherID") WEBHTML += (String) WeatherData.getDailyWeatherID(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="hourly_weatherID") WEBHTML += (String) WeatherData.getWeatherID(server.arg(i).toInt()) + ";";
      if (server.argName(i)=="daily_pop") WEBHTML += (String) WeatherData.getDailyPoP(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="hourly_pop") WEBHTML += (String) WeatherData.getPoP(server.arg(i).toInt()) + ";";
      if (server.argName(i)=="hourly_snow") WEBHTML += (String) WeatherData.getSnow(server.arg(i).toInt()) + ";";      
      if (server.argName(i)=="sunrise") WEBHTML += (String) WeatherData.sunrise + ";";
      if (server.argName(i)=="sunset") WEBHTML += (String) WeatherData.sunset + ";";
      if (server.argName(i)=="hour") {
        uint32_t temptime = server.arg(i).toDouble();
        if (temptime==0) WEBHTML += (String) hour() + ";";
        else WEBHTML += (String) hour(temptime) + ";";
      }
      if (server.argName(i)=="isFlagged") WEBHTML += (String) ((I.isFlagged==true)?1:0) + ";";
      if (server.argName(i)=="isAC") WEBHTML += (String) (((I.isAC&1)==1)?1:0) + ";";
      if (server.argName(i)=="isHeat") WEBHTML += (String) (((I.isHeat&1)==1)?1:0) + ";";
      if (server.argName(i)=="isSoilDry") WEBHTML += (String) ((I.isSoilDry==true)?1:0) + ";";
      if (server.argName(i)=="isHot") WEBHTML += (String) ((I.isHot==true)?1:0) + ";";
      if (server.argName(i)=="isCold") WEBHTML += (String) ((I.isCold==true)?1:0) + ";";
      if (server.argName(i)=="isLeak") WEBHTML += (String) ((I.isLeak==true)?1:0) + ";";

    }
  }
  
  server.send(200, "text/plain", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

  return;
}

void handleTIMEUPDATE() {


  timeClient.forceUpdate();

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}


void handleRoot(void) {
  handlerForRoot(false);
}

void handleALL(void) {
  handlerForRoot(true);
}

void handlerForRoot(bool allsensors) {
  time_t t=now();


  LAST_WEB_REQUEST = now(); //this is the time of the last web request

  WEBHTML = "";
  WEBHTML = "<!DOCTYPE html><html><head><title>Pleasant Weather Server</title>";
  WEBHTML =WEBHTML  + (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}";
  WEBHTML =WEBHTML  + (String) "body {  font-family: arial, sans-serif; }";
  WEBHTML =WEBHTML  + "</style></head>";
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";

  WEBHTML = WEBHTML + "<body>";
  
  WEBHTML = WEBHTML + "<h1>Pleasant Weather Server</h1>";
  WEBHTML = WEBHTML + "<br>";
  WEBHTML = WEBHTML + "<h2>" + dateify(t,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";

  #ifdef _USE8266
  WEBHTML = WEBHTML + "Free Stack Memory: " + ESP.getFreeContStack() + "</h2><br>";  
  #endif

  #ifdef _USE32
  WEBHTML = WEBHTML + "Free Stack Memory: " + esp_get_free_internal_heap_size() + "</h2><br>";  
  WEBHTML = WEBHTML + "Lowest Free Stack Memory: " + esp_get_minimum_free_heap_size() + "</h2><br>";  
  #endif


/*  WEBHTML += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  WEBHTML += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  WEBHTML += "<button type=\"submit\">Update Time</button><br>";
  WEBHTML += "</FORM><br>";
*/

  WEBHTML = WEBHTML + "Sunrise " + dateify(WeatherData.sunrise,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";
  WEBHTML = WEBHTML + "Sunset " + dateify(WeatherData.sunset,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";

  WEBHTML += "Number of sensors" + (String) (allsensors==false ? " (showing monitored sensors only)" : "") + ": " + (String) countDev() + " / " + (String) SENSORNUM + "<br>";
  WEBHTML = WEBHTML + "Alive since: " + dateify(ALIVESINCE,"mm/dd/yyyy hh:nn") + "<br>";
  
  WEBHTML = WEBHTML + "---------------------<br>";      

  if (I.isFlagged || (I.isHeat&1)==1 || (I.isAC&1)==1) {
    WEBHTML = WEBHTML + "<font color=\"#EE4B2B\">";      
    
    if ((I.isHeat&1)==1) WEBHTML = WEBHTML + "Heat is on<br>";
    if ((I.isAC&1)==1) WEBHTML = WEBHTML + "AC is on<br>";
    if (I.isFlagged==true) WEBHTML = WEBHTML + "Critical sensors are flagged:<br>";
    if (I.isLeak==true) WEBHTML = WEBHTML + "     A leak has been detected!!!<br>";
    if (I.isHot==true) WEBHTML = WEBHTML + "     Interior room(s) over max temp<br>";
    if (I.isCold==true) WEBHTML = WEBHTML + "     Interior room(s) below min temp<br>";
    if (I.isSoilDry==true) WEBHTML = WEBHTML + "     Plant(s) dry<br>";

    WEBHTML = WEBHTML + "</font>---------------------<br>";      
  }


  WEBHTML += "<FORM action=\"/UPDATEDEFAULTS\" method=\"get\">";
  WEBHTML += "<p style=\"font-family:arial, sans-serif\">";
  WEBHTML += "<label for=\"HoulyInterval\">Hourly interval for display (1-3h)</label>";
  WEBHTML += "<input type=\"text\" id=\"HourlyInterval\" name=\"HourlyInterval\" value=\"" + String(HourlyInterval) + "\" maxlength=\"15\"><br>";  
  WEBHTML += "<label for=\"SecSCreen\">Seconds for alarm screen to show</label>";
  WEBHTML += "<input type=\"text\" id=\"SECSCREEN\" name=\"SECSCREEN\" value=\"" + (String) SECSCREEN + "\" maxlength=\"15\"><br>";  
  
  WEBHTML +=  "<br>";
  WEBHTML += "<button type=\"submit\">Submit</button><br><br>";

  WEBHTML += "</p></font></form>";

    WEBHTML = WEBHTML + "---------------------<br>";      
        



    byte used[SENSORNUM];
    for (byte j=0;j<SENSORNUM;j++)  {
      used[j] = 255;
    }

    byte usedINDEX = 0;  



  WEBHTML = WEBHTML + "<p><table id=\"Logs\" style=\"width:900px\">";      
  WEBHTML = WEBHTML + "<tr><th style=\"width:100px\"><p><button onclick=\"sortTable(0)\">IP Address</button></p></th style=\"width:50px\"><th>ArdID</th><th style=\"width:200px\">Sensor</th><th style=\"width:100px\">Value</th><th style=\"width:100px\"><button onclick=\"sortTable(4)\">Sns Type</button></p></th style=\"width:100px\"><th><button onclick=\"sortTable(5)\">Flagged</button></p></th><th style=\"width:250px\">Last Recvd</th></tr>"; 
  for (byte j=0;j<SENSORNUM;j++)  {
    if (allsensors && bitRead(Sensors[j].Flags,1)==0) continue;
    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) Sensors[j].IP.toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors[j].IP.toString() + "</a></td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].ardID + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsName + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsValue + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) bitRead(Sensors[j].Flags,0) + (String) (bitRead(Sensors[j].Flags,6) ? "*" : "" ) + "</td>";
      WEBHTML = WEBHTML + "<td>" + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</td>";
      WEBHTML = WEBHTML + "</tr>";
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (allsensors && bitRead(Sensors[jj].Flags,1)==0) continue;
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
          WEBHTML = WEBHTML + "<tr><td><a href=\"http://" + (String) Sensors[jj].IP.toString() + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + (String) Sensors[jj].IP.toString() + "</a></td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].ardID + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].snsName + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].snsValue + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) Sensors[jj].snsType+"."+ (String) Sensors[jj].snsID + "</td>";
          WEBHTML = WEBHTML + "<td>" + (String) bitRead(Sensors[jj].Flags,0) + "</td>";
          WEBHTML = WEBHTML + "<td>"  + (String) dateify(Sensors[jj].timeLogged,"mm/dd hh:nn:ss") + "</td>";
          WEBHTML = WEBHTML + "</tr>";
        }
      }
    }
  }

  WEBHTML += "</table>";   

  //add chart
  WEBHTML += "<br>-----------------------<br>\n";
  WEBHTML += "<div id=\"myChart\" style=\"width:100%; max-width:800px; height:200px;\"></div>\n";
  WEBHTML += "<br>-----------------------<br>\n";


  WEBHTML += "</p>";

  #ifdef _WEBDEBUG
      WEBHTML += "<p>WEBDEBUG: <br>"+ WEBDEBUG + "</p><br>";
    #endif

  WEBHTML =WEBHTML  + "<script>";

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


  WEBHTML += "function sortTable(col) {  var table, rows, switching, i, x, y, shouldSwitch;table = document.getElementById(\"Logs\");switching = true;while (switching) {switching = false;rows = table.rows;for (i = 1; i < (rows.length - 1); i++) {shouldSwitch = false;x = rows[i].getElementsByTagName(\"TD\")[col];y = rows[i + 1].getElementsByTagName(\"TD\")[col];if (x.innerHTML.toLowerCase() > y.innerHTML.toLowerCase()) {shouldSwitch = true;break;}}if (shouldSwitch) {rows[i].parentNode.insertBefore(rows[i + 1], rows[i]);switching = true;}}}";
  WEBHTML += "</script> \n";
  WEBHTML += "</body></html>\n";   

   #ifdef _DEBUG
      Serial.println(WEBHTML);
    #endif
    
  server.send(200, "text/html", WEBHTML.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}


void handleUPDATEDEFAULTS() {
  uint8_t temp = 0;

  for (uint8_t i = 0; i < server.args(); i++) {
    //error check for hourly interval.
    if (server.argName(i)=="HourlyInterval") {
      temp =  server.arg(i).toInt();
      if (temp>0 && temp<4) HourlyInterval = temp;
    }

    //no error checking for secscreen... all values are legal.
    if (server.argName(i)=="SECSCREEN") SECSCREEN =  server.arg(i).toInt();
    
  }
  I.redraw = 0; //force screen redraw


  server.sendHeader("Location", "/");//This Line goes to root page
  server.send(302, "text/plain", "Updated.");  

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void handlePost() {
SensorVal S;
uint8_t tempIP[4] = {0,0,0,0};

  for (byte k=0;k<server.args();k++) {
   #ifdef _WEBDEBUG
       //WEBDEBUG = WEBDEBUG + "POST: " + server.argName(k) + ": " + String(server.arg(k)) + " @" + String(now(),DEC) + "<br>";
    #endif
      if ((String)server.argName(k) == (String)"logID")  breakLOGID(String(server.arg(k)),&S.ardID,&S.snsType,&S.snsID);
      if ((String)server.argName(k) == (String)"IP") {
        IPString2ByteArray(String(server.arg(k)),tempIP);
        S.IP = tempIP;
      }
      if ((String)server.argName(k) == (String)"varName") snprintf(S.snsName,29,"%s", server.arg(k).c_str());
      if ((String)server.argName(k) == (String)"varValue") S.snsValue = server.arg(k).toDouble();
      if ((String)server.argName(k) == (String)"timeLogged") S.timeRead = server.arg(k).toDouble();      //time logged at sensor is time read by me
      if ((String)server.argName(k) == (String)"Flags") S.Flags = server.arg(k).toInt();
  }
  time_t t = now();
  S.timeLogged = t; //time logged by me is when I received this.
  if (S.timeRead == 0  || S.timeRead < t-24*60*60 || S.timeRead > t+24*60*60)     S.timeRead = t;

//regardless of mapping sensor or similar sensors, write this to the sd card
  writeSensorSD(S,"/Data/" + (String) year() + ".txt");

  
  int sn = findDev(&S,true);

  //special cases
      
  //bmp temp received... check for AHT
  if (S.snsType == 10 && findSns(4,false)>-1) {
    if (sn>=0) {
      //already stored a bmp, so erase that
      initSensor(findSns(10,false));
    }
    server.send(202, "text/plain", "Received, but rejected the data because this is BMP and you sent AHT"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request
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
    
    server.send(202, "text/plain", "Received, but rejected the data because this is voltage and you sent %"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request
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

 

  if((S.snsType==9 || S.snsType == 13) && (LAST_BAR_READ==0 || LAST_BAR_READ < t - 60*60)) { //pressure
    LAST_BAR_READ = S.timeRead;
    LAST_BAR = S.snsValue;
  }

  String stemp = S.snsName;
  if((stemp.indexOf("Outside")>-1 && S.snsType==61 ) && (LAST_BAT_READ==0 || LAST_BAT_READ < t - 60*60 || LAST_BAT_READ > t)) { //outside battery
    LAST_BAT_READ = S.timeRead;
    pushDoubleArray(batteryArray,48,S.snsValue);
  }


  if (sn<0) {
    server.send(201, "text/plain", "Received, but rejected the data because I could not add another sensor"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request
    return;
  }
  Sensors[sn] = S;

  server.send(200, "text/plain", "Received Data"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request

}


//old IP fcns
String IP2String(byte* IP) {
//reconstruct a string from 4 bytes. If the first or second element is 0 then use 192 or 168
  String IPs = "";

  for (byte j=0;j<3;j++){
    if (IP[j] ==0) {
      if (j==0) IPs = IPs + String(192,DEC) + ".";
      else IPs = IPs + String(168,DEC) + ".";
    } else     IPs = IPs + String(IP[j],DEC) + ".";
  }

  IPs = IPs + String(IP[3],DEC);
  return IPs;
}

bool IPString2ByteArray(String IPstr,byte* IP) {
        
    String temp;
    
    int strOffset; 
    IPstr = IPstr + "."; //need the string to end with .
    for(byte j=0;j<4;j++) {
      strOffset = IPstr.indexOf(".", 0);
      if (strOffset == -1) { //did not find the . , IPstr not correct. abort.
        return false;
      } else {
        temp = IPstr.substring(0, strOffset);
        IP[j] = temp.toInt();
        IPstr.remove(0, strOffset + 1);
      }
    }
    
    return true;
}


bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum) {
    
    String temp;
    
    int strOffset = logID.indexOf(".", 0);
    if (strOffset == -1) { //did not find the . , logID not correct. abort.
      return false;
    } else {
      temp = logID.substring(0, strOffset);
      *ardID = temp.toInt();
      logID.remove(0, strOffset + 1);

      strOffset = logID.indexOf(".", 0);
      temp = logID.substring(0, strOffset);
      *snsID = temp.toInt();
      logID.remove(0, strOffset + 1);

      *snsNum = logID.toInt();
    }
    
    return true;
}
