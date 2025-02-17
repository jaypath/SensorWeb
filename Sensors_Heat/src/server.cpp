#include <Arduino.h>

#include <header.hpp>
#include "server.hpp"
#include <timesetup.hpp>


//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif
#ifdef _USE32

    WebServer server(80);
#endif

bool KiyaanServer = false;
byte CURRENTSENSOR_WEB = 1;
IP_TYPE SERVERIP[NUMSERVERS];

WiFi_type WIFI_INFO;

  
#if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
void initHVAC(void){
  
  
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsType >=50 && Sensors[j].snsType <=57) {
      SendData(&Sensors[j]);
      Sensors[j].snsValue = 0;
    }      
  }
}
#endif


void assignIP(byte ip[4],IPAddress IPA) {
   assignIP(ip, IPA[0],IPA[1],IPA[2],IPA[3]);

}
void assignIP(byte ip[4], byte m1, byte m2, byte m3, byte m4) {
  //ip is a 4 byte array
  ip[0] = m1;
  ip[1] = m2;
  ip[2] = m3;
  ip[3] = m4;
}


bool WifiStatus(void) {
  if (WiFi.status() != WL_CONNECTED) 
  {
    WIFI_INFO.status = 0;
    return false;
  }
  WIFI_INFO.status = WL_CONNECTED;
  return true; 
}

void connectWiFi()
{
//rerturn nothing, as event trigger async
  

  //rerturn 0 if connected, else number of times I tried and failed
  IPAddress temp;

  assignIP(WIFI_INFO.DHCP,192,168,68,1);
  assignIP(WIFI_INFO.DNS,192,168,1,1);
  assignIP(WIFI_INFO.DNS2,192,168,68,1);
  assignIP(WIFI_INFO.GATEWAY,192,168,68,1);
  assignIP(WIFI_INFO.SUBNET,255,255,252,0);


  if (WifiStatus()) {
    assignIP(WIFI_INFO.MYIP, WiFi.localIP());
    
    return;
  } else {
    if (ASSIGNEDIP[0]==0) {
      WIFI_INFO.MYIP[0]=0;    //will reassign this shortly
    } else {
      WIFI_INFO.MYIP[0] = ASSIGNEDIP[0];    
      WIFI_INFO.MYIP[1] = ASSIGNEDIP[1];    
      WIFI_INFO.MYIP[2] = ASSIGNEDIP[2];    
      WIFI_INFO.MYIP[3] = ASSIGNEDIP[3];    

      WiFi.config(WIFI_INFO.MYIP, WIFI_INFO.GATEWAY,  WIFI_INFO.SUBNET,WIFI_INFO.DNS,WIFI_INFO.DNS2);
      
    }
  }
  
  WiFi.mode(WIFI_STA);
  #ifdef _DEBUG
  Serial.printf( "wifi begin\n");
       #endif

  #ifdef _USE32
    WiFi.onEvent(onWiFiEvent); 
  #endif
  WiFi.begin(ESP_SSID, ESP_PASS);

  #ifndef _USE32
    //cannot do this async, so wait for wifi here
    while ( WiFi.status() != WL_CONNECTED) {
      #ifdef _DEBUG
        Serial.print(".");
      #endif
      delay(500);
    }

    WifiStatus(); //assign wifi status
    
  #endif
  
}

#ifdef _USE32
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_DISCONNECTED:
      {
        #ifdef _DEBUG 
          Serial.print("WiFi begin failed ");
        #endif
        WIFI_INFO.status =  0;
      break;
      }
    case SYSTEM_EVENT_STA_GOT_IP:
      {
        #ifdef _DEBUG 
        Serial.print("WiFi begin succeeded ");
        #endif
    
        // Connected successfully
        WifiStatus(); //assign wifi status
        assignIP(WIFI_INFO.MYIP, WiFi.localIP());    
        break;
      }
    default:
       // Display additional events???
       break;      
  }
}
#endif

bool Server_Message(String URL, String* payload, int* httpCode) {
  WiFiClient wfclient;
  HTTPClient http;


  if (WIFI_INFO.status >0 ){
     http.useHTTP10(true);
     http.begin(wfclient,URL.c_str());
     *httpCode = http.GET();
     *payload = http.getString();
     http.end();
     return true;
  } 

  return false;
}


bool SendData(struct SensorVal *snsreading) {



#ifdef  ARDID
   byte arduinoID = ARDID;
#else
byte arduinoID = WIFI_INFO.MYIP[3];
#endif

if (bitRead(snsreading->Flags,1) == 0) return false; //not monitored


WiFiClient wfclient;
HTTPClient http;
  
byte ipindex=0;
bool isGood = false;


  if(WIFI_INFO.status>0){
    String payload;
    String URL;
    String tempstring;
    int httpCode=404;
    tempstring = "/POST?IP=" + WiFi.localIP().toString() + "," + "&varName=" + String(snsreading->snsName);
    tempstring = tempstring + "&varValue=";
    tempstring = tempstring + String(snsreading->snsValue, DEC);
    tempstring = tempstring + "&Flags=";
    tempstring = tempstring + String(snsreading->Flags, DEC);
    tempstring = tempstring + "&logID=";
    tempstring = tempstring + String(arduinoID, DEC);
    tempstring = tempstring + "." + String(snsreading->snsType, DEC) + "." + String(snsreading->snsID, DEC) + "&timeLogged=" + String(snsreading->LastReadTime, DEC) + "&isFlagged=" + String(bitRead(snsreading->Flags,0), DEC) + "&SendingInt=" + String(snsreading->SendingInt, DEC);

    while(ipindex<NUMSERVERS) {
      if (SERVERIP[ipindex].IP[0]==0 || SERVERIP[ipindex].IP[3]==0) {
        ipindex++;
        continue;
      }
      URL = "http://" + IP2String( SERVERIP[ipindex].IP);
      URL = URL + tempstring;
    
      http.useHTTP10(true);
    //note that I'm coverting lastreadtime to GMT
  
      snsreading->LastSendTime = now();
        #ifdef _DEBUG
            Serial.print("sending this message: ");
            Serial.println(URL.c_str());
        #endif

      http.begin(wfclient,URL.c_str());
      httpCode = (int) http.GET();
      payload = http.getString();
      http.end();


        if (httpCode == 200) {
          isGood = true;
          SERVERIP[ipindex].server_status = httpCode;
        } 

      ipindex++;

    } ;
  
    
  }

  if (isGood) bitWrite(snsreading->Flags,6,0); //even if there was no change in the flag status, I wrote the value so set bit 6 (change in flag) to zero

  return isGood;


}

bool inIndex(byte lookfor,byte used[],byte arraysize) {
  for (byte i=0;i<arraysize;i++) {
    if (used[i] == lookfor) return true;
  }

  return false;
}

bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum) {
    
    String temp;
    
    byte strLen = logID.length();
    byte strOffset = logID.indexOf(".", 0);
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


void handleUPDATESENSORPARAMS() {
  String stateSensornum = server.arg("SensorNum");

  byte j = stateSensornum.toInt();

  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorName") snprintf(Sensors[j].snsName,31,"%s", server.arg(i).c_str());

    if (server.argName(i)=="Monitored") {
      if (server.arg(i) == "0") bitWrite(Sensors[j].Flags,1,0);
      else bitWrite(Sensors[j].Flags,1,1);
    }

    if (server.argName(i)=="Critical") {
      if (server.arg(i) == "0") bitWrite(Sensors[j].Flags,7,0);
      else bitWrite(Sensors[j].Flags,7,1);
    }

    if (server.argName(i)=="Outside") {
      if (server.arg(i)=="0") bitWrite(Sensors[j].Flags,2,0);
      else bitWrite(Sensors[j].Flags,2,1);
    }

    if (server.argName(i)=="UpperLim") Sensors[j].limitUpper = server.arg(i).toDouble();

    if (server.argName(i)=="LowerLim") Sensors[j].limitLower = server.arg(i).toDouble();

    if (server.argName(i)=="SendInt") Sensors[j].SendingInt = server.arg(i).toDouble();
    if (server.argName(i)=="PollInt") Sensors[j].PollingInt = server.arg(i).toDouble();

  }

  ReadData(&Sensors[j]);
  SendData(&Sensors[j]);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page
}

void handleNEXT() {

  CURRENTSENSOR_WEB++;
  if (CURRENTSENSOR_WEB>SENSORNUM) CURRENTSENSOR_WEB = 1;


  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleLAST() {

  if (CURRENTSENSOR_WEB==1) CURRENTSENSOR_WEB = SENSORNUM;
  else CURRENTSENSOR_WEB--;
   

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleUPDATESENSORREAD() {
  byte j = server.arg("SensorNum").toInt();


  ReadData(&Sensors[j]);
  SendData(&Sensors[j]);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page
}

void handleUPDATEALLSENSORREADS() {
String currentLine = "";

currentLine += "Current time ";
currentLine += (String) now();
currentLine += " = ";
currentLine += (String) dateify() + "\n";


for (byte k=0;k<SENSORNUM;k++) {
  ReadData(&Sensors[k]);      
  currentLine +=  "Sensor " + (String) Sensors[k].snsName + " data sent to at least 1 server: " + SendData(&Sensors[k]) + "\n";
}
  server.send(200, "text/plain", "Status...\n" + currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client

}


void handleSETTHRESH() {
byte snsNum, snsType;
String strTemp;
double limitUpper=-1, limitLower=-1;
uint16_t PollingInt=0;
  uint16_t SendingInt=0;
byte k;
#ifdef  ARDID
byte   arduinoID = ARDID;
#else
byte arduinoID = WIFI_INFO.MYIP[3];

#endif



  for (k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"LOGID") breakLOGID(server.arg(k),&arduinoID,&snsType,&snsNum);
    if ((String)server.argName(k) == (String)"UPPER") {
      strTemp = server.arg(k);      
      limitUpper=strTemp.toDouble();
    }
    if ((String)server.argName(k) == (String)"LOWER") {
      strTemp = server.arg(k);      
      limitLower=strTemp.toDouble();
    }
    if ((String)server.argName(k) == (String)"POLLINGINT") {
      strTemp = server.arg(k);      
      PollingInt=strTemp.toInt();
    }
    if ((String)server.argName(k) == (String)"SENDINGINT") {
      strTemp = server.arg(k);      
      SendingInt=strTemp.toInt();
    }

    
  }

  k = findSensor(snsType,snsNum);
  if (k<100) {
    if (limitLower != -1) Sensors[k].limitLower = limitLower;
    if (limitUpper != -1) Sensors[k].limitUpper = limitUpper;
    if (PollingInt>0) Sensors[k].PollingInt = PollingInt;
    if (SendingInt>0) Sensors[k].SendingInt = SendingInt;
    checkSensorValFlag(&Sensors[k]);
    String currentLine = "";
    byte j=k;
    currentLine += (String) dateify() + "\n";

    currentLine = currentLine + "ARDID:" + String(arduinoID, DEC) + "; snsType:"+(String) Sensors[j].snsType+"; snsID:"+ (String) Sensors[j].snsID + "; SnsName:"+ (String) Sensors[j].snsName + "; LastRead:"+(String) Sensors[j].LastReadTime+"; LastSend:"+(String) Sensors[j].LastSendTime + "; snsVal:"+(String) Sensors[j].snsValue + "; UpperLim:"+(String) Sensors[j].limitUpper + "; LowerLim:"+(String) Sensors[j].limitLower + "; Flag:"+(String) bitRead(Sensors[j].Flags,0) + "; Monitored: " + (String) bitRead(Sensors[j].Flags,1) + "\n";
    server.send(400, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
  } else {
    server.send(400, "text/plain", "That sensor was not found");   // Send HTTP status 400 as error
  }
}

void handleREBOOT() {

    server.send(400, "text/plain", "Rebooting...");   // Send HTTP status 400 as error

ESP. restart();
}

void handleRoot() {

#ifdef _DEBUG
  Serial.println("Hit handleroot.");
#endif

#ifdef  ARDID
  byte arduinoID = ARDID;
#else
  byte arduinoID = WiFi.localIP()[3];

#endif

String currentLine = "<!DOCTYPE html><html>\n";

#if defined(_WEBCHART) || defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
  currentLine =currentLine  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";
#endif



currentLine +=  "<head><title>" + (String) ARDNAME + " Page</title>\n";
currentLine += (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}\n";
currentLine += (String) "input[type='text'] { font-family: arial, sans-serif; font-size: 10px; }\n";
currentLine += (String) "body {  font-family: arial, sans-serif; }\n";
currentLine += "</style></head>\n";
currentLine += "<body>";

currentLine +=  "<h2>Arduino: " + (String) ARDNAME + "<br>\nIP:" + WiFi.localIP().toString() + "<br>\nARDID:" + String(arduinoID, DEC) + "<br></h2>\n";
currentLine += "<p>Started on: " + (String) dateify(ALIVESINCE,"mm/dd/yyyy hh:nn") + "<br>\n";
currentLine += "Current time: " + (String) now() + " = " +  (String) dateify(now(),"mm/dd/yyyy hh:nn:ss") + "<br>\n";

#ifdef _USE32
  currentLine += "Free Stack: " + (String) uxTaskGetStackHighWaterMark(NULL) + "<br>\n";
#endif
#ifdef _USE8266
  currentLine += "Free Stack: " + (String) ESP.getFreeContStack() + "<br>\n";
#endif
currentLine += "<a href=\"/UPDATEALLSENSORREADS\">Update all sensors</a><br>\n";
currentLine += "</p>\n";
currentLine += "<br>-----------------------<br>\n";



  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM;j++)  {
    used[j] = 255;

    if (Sensors[j].snsID>0 && Sensors[j].snsType>0)
        currentLine += "<form action=\"/UPDATESENSORPARAMS\" method=\"GET\" id=\"frm_SNS" + (String) j + "\"><input form=\"frm_SNS"+ (String) j + "\"  id=\"SNS" + (String) j + "\" type=\"hidden\" name=\"SensorNum\" value=\"" + (String) j + "\"></form>\n";
  
    //add form tags
  }
      
  byte usedINDEX = 0;  
  
  char tempchar[9] = "";

  currentLine += "<p>";
  currentLine = currentLine + "<table id=\"Logs\" style=\"width:70%\">";      
  currentLine = currentLine + "<tr><th>SnsType</th><th>SnsID</th><th>Value</th><th><button onclick=\"sortTable(3)\">UpperLim</button></th><th><button onclick=\"sortTable(4)\">LowerLim</button></th>";
  currentLine = currentLine + "<th><button onclick=\"sortTable(5)\">PollInt</button></th><th><button onclick=\"sortTable(6)\">SendInt</button></th><th><button onclick=\"sortTable(7)\">Flag</button></th>";
  currentLine = currentLine + "<th><button onclick=\"sortTable(8)\">LastLog</button></th><th><button onclick=\"sortTable(9)\">LastSend</button></th><th>IsMonit</th><th>IsCritical</th><th>IsOut</th><th>Flags</th><th>SnsName</th>";
  currentLine = currentLine + "<th>Submit</th><th>Recheck</th></tr>\n"; 
  for (byte j=0;j<SENSORNUM;j++)  {

    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      currentLine = currentLine + "<tr>";
      currentLine = currentLine + "<td>" + (String) Sensors[j].snsType + "</td>";
      currentLine = currentLine + "<td>" + (String) Sensors[j].snsID + "</td>";
      currentLine = currentLine + "<td>" + (String) Sensors[j].snsValue + "</td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"UpperLim\" value=\"" + String(Sensors[j].limitUpper,DEC) + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"LowerLim\" value=\"" + String(Sensors[j].limitLower,DEC) + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"PollInt\" value=\"" + String(Sensors[j].PollingInt,DEC) + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"SendInt\" value=\"" + String(Sensors[j].SendingInt,DEC) + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td>" + (String) bitRead(Sensors[j].Flags,0) + "</td>";
      currentLine = currentLine + "<td>" + (String) dateify(Sensors[j].LastReadTime) + "</td>";
      currentLine = currentLine + "<td>" + (String) dateify(Sensors[j].LastSendTime) + "</td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"Monitored\" value=\"" + String(bitRead(Sensors[j].Flags,1),DEC) + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"Critical\" value=\"" + String(bitRead(Sensors[j].Flags,7),DEC) + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td>" + (String) bitRead(Sensors[j].Flags,2) + "</td>";
      Byte2Bin(Sensors[j].Flags,tempchar,true);
      currentLine = currentLine + "<td>" + (String) tempchar + "</td>";
      currentLine = currentLine + "<td><input type=\"text\"  name=\"SensorName\" value=\"" +  (String) Sensors[j].snsName + "\" form = \"frm_SNS" + (String) j + "\"></td>";
      currentLine = currentLine + "<td><button type=\"submit\" form = \"frm_SNS" + (String) j + "\" name=\"Sub" + (String) j + "\">Submit</button></td>";
      currentLine += "<td><button type=\"submit\" formaction=\"/UPDATESENSORREAD\" form = \"frm_SNS" + (String) j + "\" name=\"Upd" + (String) j + "\">Update</button></td>";
      currentLine += "</tr>\n";

      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].snsType==Sensors[j].snsType) {
          used[usedINDEX++] = jj;
          currentLine = currentLine + "<tr>";
          currentLine = currentLine + "<td>" + (String) Sensors[jj].snsType + "</td>";
          currentLine = currentLine + "<td>" + (String) Sensors[jj].snsID + "</td>";
          currentLine = currentLine + "<td>" + (String) Sensors[jj].snsValue + "</td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"UpperLim\" value=\"" + String(Sensors[jj].limitUpper,DEC) + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"LowerLim\" value=\"" + String(Sensors[jj].limitLower,DEC) + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"PollInt\" value=\"" + String(Sensors[jj].PollingInt,DEC) + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"SendInt\" value=\"" + String(Sensors[jj].SendingInt,DEC) + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td>" + (String) bitRead(Sensors[jj].Flags,0) + "</td>";
          currentLine = currentLine + "<td>" + (String) dateify(Sensors[jj].LastReadTime) + "</td>";
          currentLine = currentLine + "<td>" + (String) dateify(Sensors[jj].LastSendTime) + "</td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"Monitored\" value=\"" + String(bitRead(Sensors[jj].Flags,1),DEC) + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"Critical\" value=\"" + String(bitRead(Sensors[jj].Flags,7),DEC) + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td>" + (String) bitRead(Sensors[jj].Flags,2) + "</td>";
          Byte2Bin(Sensors[jj].Flags,tempchar,true);
          currentLine = currentLine + "<td>" + (String) tempchar + "</td>";
          currentLine = currentLine + "<td><input type=\"text\"  name=\"SensorName\" value=\"" +  (String) Sensors[jj].snsName + "\" form = \"frm_SNS" + (String) jj + "\"></td>";
          currentLine = currentLine + "<td><button type=\"submit\" form = \"frm_SNS" + (String) jj + "\" name=\"Sub" + (String) jj + "\">Submit</button></td>";
          currentLine += "<td><button type=\"submit\" formaction=\"/UPDATESENSORREAD\" form = \"frm_SNS" + (String) jj + "\" name=\"Upd" + (String) jj + "\">Update</button></td>";
          currentLine += "</tr>\n";
        }
      }
    }
  }
  
  currentLine += "</table>\n";   


  currentLine += "</p>\n";


#if defined(_WEBCHART) || defined(_CHECKHEAT) || defined(_CHECKAIRCON)
  //add charts if indicated
  currentLine += "<br>-----------------------<br>\n";
  #if defined(_WEBCHART)
    for (byte j=0;j<_WEBCHART;j++)    currentLine += "<div id=\"myChart" + (String) j + "\" style=\"width:100%; max-width:800px; height:500px;\"></div>\n";
  #endif
  
  #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
    currentLine += "<div id=\"myChart_HVAC\" style=\"width:100%; max-width:1500px; height:500px;\"></div>\n";
  #endif
  currentLine += "<br>-----------------------<br>\n";
#endif
/* do not do this. Use sensor charts instead

  #ifdef _USEBARPRED
    currentLine += "<p>";
    currentLine += "Hourly_air_pressures (most recent [top] entry was ";
    currentLine +=  (String) dateify(LAST_BAR_READ);
    currentLine += "):<br>\n";

    for (byte j=0;j<24;j++)  {
      currentLine += "     ";
      currentLine += String(BAR_HX[j],DEC);
      currentLine += "<br>\n"; 
    }
  currentLine += "</p>\n";


  #endif
  */ 

  currentLine =currentLine  + "<script>\n";

  #if defined(_WEBCHART) || defined(_CHECKHEAT) || defined(_CHECKAIRCON)
    currentLine =currentLine  + "google.charts.load('current',{packages:['corechart']});\n";
    currentLine =currentLine  + "google.charts.setOnLoadCallback(drawChart);\n";


    #ifdef _WEBCHART
      
      currentLine += "function drawChart() {\n";

      for (byte j=0;j<_WEBCHART;j++) {
        currentLine += "const data" + (String) j + " = google.visualization.arrayToDataTable([\n";
        currentLine += "['t','val'],\n";

        for (int jj = _NUMWEBCHARTPNTS-1;jj>=0;jj--) {
          currentLine += "[" + (String) ((int) ((uint32_t) (SensorCharts[j].lastRead - SensorCharts[j].interval*jj)-now())/60) + "," + (String) SensorCharts[j].values[jj] + "]";
          if (jj>0) currentLine += ",";
          currentLine += "\n";
        }
        currentLine += "]);\n\n";

      
      // Set Options
        currentLine += "const options" + (String) j + " = {\n";
        currentLine += "hAxis: {title: 'min from now'}, \n";
        currentLine += "vAxis: {title: '" + (String) Sensors[SensorCharts[j].snsNum].snsName + "'},\n";
        currentLine += "legend: 'none'\n};\n";

        currentLine += "const chart" + (String) j + " = new google.visualization.LineChart(document.getElementById('myChart" + (String) j + "'));\n";
        currentLine += "chart" + (String) j + ".draw(data" + (String) j + ", options" + (String) j + ");\n"; 
      }
        currentLine += "}\n";
    #endif

    #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
      
      currentLine += "function drawChart() {\n";

      currentLine += "const data_HVAC = google.visualization.arrayToDataTable([\n";
      //header
      currentLine += "['HoursAgo',";
      
      for (byte j=0;j<HVACSNSNUM;j++) {
        currentLine += "'" + (String) Sensors[j].snsName + "'";
        if (j<HVACSNSNUM-1) currentLine += ",";
        else currentLine += "],\n";
      }
      //data points
      for (int jj = 0;jj<_HVACHXPNTS;jj++) {
        currentLine += "[" + (String) (jj+1) + ",";        
        for (byte j=0;j<HVACSNSNUM;j++) {
          currentLine += (String)  HVACHX[j].values[jj];
          if (j<HVACSNSNUM-1) currentLine += ",";
          else currentLine += "]";
        }
        if (jj<_HVACHXPNTS-1) currentLine += ",\n";
        else currentLine += "\n";
      }
      currentLine += "]);\n\n";

      
      // Set Options
      currentLine += "const options_HVAC = {\n";
      currentLine += "hAxis: {title: 'Time'}, \n";
      currentLine += "vAxis: {title: 'MinutesOn'},\n";
      currentLine += "legend: { position: 'bottom' }\n};\n";

      currentLine += "const chart_HVAC = new google.visualization.LineChart(document.getElementById('myChart_HVAC'));\n";
      currentLine += "chart_HVAC.draw(data_HVAC, options_HVAC);\n"; 
      currentLine += "}\n";
    #endif
  #endif

  currentLine += "function sortTable(col) {\nvar table, rows, switching, i, x, y, shouldSwitch;\ntable = document.getElementById(\"Logs\");\nswitching = true;\nwhile (switching) {\nswitching = false;\nrows = table.rows;\nfor (i = 1; i < (rows.length - 1); i++) {\nshouldSwitch = false;\nx = rows[i].getElementsByTagName(\"TD\")[col];\ny = rows[i + 1].getElementsByTagName(\"TD\")[col];\nif (x.innerHTML.toLowerCase() > y.innerHTML.toLowerCase()) {\nshouldSwitch = true;\nbreak;\n}\n}\nif (shouldSwitch) {\nrows[i].parentNode.insertBefore(rows[i + 1], rows[i]);\nswitching = true;\n}\n}\n}\n";
  currentLine += "</script>\n ";

  currentLine += "</body>\n</html>\n";


    //IF USING PROGMEM: use send_p   !!
  server.send(200, "text/html", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}


void handleNotFound(){
  server.send(404, "text/plain", "Arduino says 404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}


char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}

void Byte2Bin(uint8_t value, char* output, bool invert) {
  snprintf(output,9,"00000000");
  for (byte i = 0; i < 8; i++) {
    if (invert) {
      if (value & (1 << i)) output[i] = '1';
      else output[i] = '0';
    } else {
      if (value & (1 << i)) output[7-i] = '1';
      else output[7-i] = '0';
    }
  }

  return;
}


bool wait_ms(uint16_t ms) {


  uint32_t mend = millis() + ms;
  if ((uint32_t) (0-ms)<mend) return false; //0-ms should be some massive number... if it is less than mend then we are near the millis rollover
  

  while (millis() < mend) {
    //handle these tasks
    //be available for interrupts
    #ifndef _USELOWPOWER
      ArduinoOTA.handle();
      server.handleClient();
    #endif

  }

  return true;
}

String IP2String(byte IP[]) {
  String s = (String) IP[0] + "." + (String) IP[1] + "." + (String) IP[2] + "." + (String) IP[3];
  return s;
}