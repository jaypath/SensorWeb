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

bool SendData(struct SensorVal *snsreading) {
  if (bitRead(snsreading->Flags,1) == 0) return false;
  
#ifdef _DEBUG
  Serial.printf("SENDDATA: Sending data. Sensor is currently named %s. \n", snsreading->snsName);
#endif

  WiFiClient wfclient;
  HTTPClient http;
    
    byte ipindex=0;
    bool isGood = false;

      #ifdef _DEBUG
        Serial.println(" ");
      Serial.println("*****************");
      Serial.println("Sending Data");
      Serial.print("Device: ");
          Serial.println(snsreading->snsName);
      Serial.print("SnsType: ");
          Serial.println(snsreading->snsType);
      Serial.print("Value: ");
          Serial.println(snsreading->snsValue);
      Serial.print("LastLogged: ");
          Serial.println(snsreading->LastReadTime);
      Serial.print("isFlagged: ");
          Serial.println(bitRead(snsreading->Flags,0));
      Serial.print("isMonitored: ");
          Serial.println(bitRead(snsreading->Flags,1));
      Serial.print("Flags: ");
          Serial.println(snsreading->Flags);          

      #endif


  
  if(WiFi.status()== WL_CONNECTED){
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
    tempstring = tempstring + String(ARDID, DEC);
    tempstring = tempstring + "." + String(snsreading->snsType, DEC) + "." + String(snsreading->snsID, DEC) + "&timeLogged=" + String(snsreading->LastReadTime, DEC) + "&isFlagged=" + String(bitRead(snsreading->Flags,0), DEC);

    do {
      URL = "http://" + SERVERIP[ipindex].IP.toString();
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

        #ifdef _DEBUG
          Serial.print("Received: ");
          Serial.println(payload);
          Serial.print("Code: ");
          Serial.println(httpCode);
          Serial.println("*****************");
          Serial.println(" ");
        #endif

    #ifdef _DEBUG
      Serial.printf("------>SENDDATA: Sensor is now named %s. \n", snsreading->snsName);
    #endif

        if (httpCode == 200) {
          isGood = true;
          SERVERIP[ipindex].server_status = httpCode;
        } 
    #ifdef _DEBUG
      Serial.printf("------>SENDDATA: Sensor is now named %s. \n", snsreading->snsName);
    #endif

    #ifdef _DEBUG
      Serial.printf("SENDDATA: Sent to %d. Sensor is now named %s. \n", ipindex,snsreading->snsName);
    #endif

      ipindex++;

    } while(ipindex<NUMSERVERS);
  
    
  }
#ifdef _DEBUG
  Serial.printf("SENDDATA: End of sending data. Sensor is now named %s. \n", snsreading->snsName);
#endif

     return isGood;


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

  ReadData(&Sensors[CURRENTSENSOR_WEB-1]);
  SendData(&Sensors[CURRENTSENSOR_WEB-1]);

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
byte ardid, snsNum, snsType;
String strTemp;
double limitUpper=-1, limitLower=-1;
uint16_t PollingInt=0;
  uint16_t SendingInt=0;
byte k;

  for (k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"LOGID") breakLOGID(server.arg(k),&ardid,&snsType,&snsNum);
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

    currentLine = currentLine + "ARDID:" + String(ARDID, DEC) + "; snsType:"+(String) Sensors[j].snsType+"; snsID:"+ (String) Sensors[j].snsID + "; SnsName:"+ (String) Sensors[j].snsName + "; LastRead:"+(String) Sensors[j].LastReadTime+"; LastSend:"+(String) Sensors[j].LastSendTime + "; snsVal:"+(String) Sensors[j].snsValue + "; UpperLim:"+(String) Sensors[j].limitUpper + "; LowerLim:"+(String) Sensors[j].limitLower + "; Flag:"+(String) bitRead(Sensors[j].Flags,0) + "; Monitored: " + (String) bitRead(Sensors[j].Flags,1) + "\n";
    server.send(400, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
  } else {
    server.send(400, "text/plain", "That sensor was not found");   // Send HTTP status 400 as error
  }
}

void handleLIST() {
String currentLine = "";
currentLine =  "IP:" + WiFi.localIP().toString() + "\nARDID:" + String( ARDID, DEC) + "\n";
currentLine += (String) dateify(now(),"mm/dd/yyyy hh:nn:ss") + "\n";

  for (byte j=0;j<SENSORNUM;j++)  {
    currentLine += "     ";
    currentLine +=  "snsType: ";
    currentLine += String(Sensors[j].snsType,DEC);
    currentLine += "\n";

    currentLine += "     ";
    currentLine += "snsID: ";
    currentLine +=  String(Sensors[j].snsID,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "SnsName: ";
    currentLine +=  (String) Sensors[j].snsName;
    currentLine += "\n";

    currentLine += "     ";
    currentLine +=  "snsVal: ";
    currentLine +=  String(Sensors[j].snsValue,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "LastRead: ";
    currentLine += String(Sensors[j].LastReadTime,DEC);
    currentLine += " = ";
    currentLine += (String) dateify(Sensors[j].LastReadTime);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "LastSend: ";
    currentLine +=  String(Sensors[j].LastSendTime,DEC);
    currentLine += " = ";
    currentLine += (String) dateify(Sensors[j].LastSendTime);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "UpperLim: ";
    currentLine += String(Sensors[j].limitUpper,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "LowerLim: ";
    currentLine +=  String(Sensors[j].limitLower,DEC);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "Flag: ";
    currentLine +=  (String) bitRead(Sensors[j].Flags,0);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "Monitored: ";
    currentLine +=  (String) bitRead(Sensors[j].Flags,1);
    currentLine +=  "\n";

    currentLine += "     ";
    currentLine +=  "Flags: ";
    char cbuff[9] = "";
    Byte2Bin(Sensors[j].Flags,cbuff,1);

    #ifdef _DEBUG
        Serial.print("SpecType after byte2bin: ");
        Serial.println(cbuff);
    #endif

    currentLine +=  cbuff;
    currentLine += "\n\n";
  }
   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif
  server.send(200, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleRoot() {
char temp[30] = "";

String currentLine = "<!DOCTYPE html><html><head><title>Arduino Server Page</title></head><body>";

//currentLine += "<h1></h1>";

currentLine =  "<h2>Arduino: " + (String) ARDNAME + "<br>IP:" + WiFi.localIP().toString() + "<br>ARDID:" + String( ARDID, DEC) + "<br></h2><p>";
currentLine += "Current time ";
currentLine += (String) now();
currentLine += " = ";
currentLine += (String) dateify(now(),"mm/dd/yyyy hh:nn:ss");
currentLine += "<br>";
currentLine += "<a href=\"/LIST\">List all sensors</a><br>";
currentLine += "<a href=\"/UPDATEALLSENSORREADS\">Update all sensors</a><br>";

currentLine += "<br>-----------------------<br>";

  byte j=CURRENTSENSOR_WEB-1;
  currentLine += "SENSOR NUMBER: " + String(j,DEC);
  if (bitRead(Sensors[j].Flags,0)) currentLine += " !!!!!ISFLAGGED!!!!!";
  currentLine += "<br>";
  

  currentLine += "-----------------------<br>";


/*
  temp = FORM_page;
  
  temp.replace("@@SNSNUM@@",String(j,DEC));
  temp.replace("@@FLAG1@@",String(bitRead(Sensors[j].Flags,1),DEC));
  temp.replace("@@FLAG2@@",String(bitRead(Sensors[j].Flags,2),DEC));
  temp.replace("@@UPPERLIM@@",String(Sensors[j].limitUpper,DEC));
  temp.replace("@@LOWERLIM@@",String(Sensors[j].limitLower,DEC));
  temp.replace("@@POLL@@",String(Sensors[j].PollingInt,DEC));
  temp.replace("@@SEND@@",String(Sensors[j].SendingInt,DEC));
  temp.replace("@@CURRENTSENSOR@@",String(j,DEC));

  currentLine += temp;
*/

  currentLine += "<FORM action=\"/UPDATESENSORPARAMS\" method=\"get\">";
  currentLine += "<p style=\"font-family:monospace\">";

  currentLine += "<input type=\"hidden\" name=\"SensorNum\" value=\"" + (String) j + "\"><br>";  


  char padder[2] = ".";
  snprintf(temp,29,"%s","Sensor Name");
  strPad(temp,padder,25);
  currentLine += "<label for=\"MyName\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"MyName\" name=\"SensorName\" value=\"" + String(Sensors[j].snsName) + "\" maxlength=\"30\"><br>";  

  snprintf(temp,29,"%s","Sensor Value");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Val\">" + (String) temp + " " + String(Sensors[j].snsValue,DEC) + "</label>";
  currentLine +=  "<br>";
  
  snprintf(temp,29,"%s","Is Monitored");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Mon\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Mon\" name=\"Monitored\" value=\"" + String(bitRead(Sensors[j].Flags,1),DEC) + "\"><br>";  
  
snprintf(temp,29,"%s","Is Outside");
strPad(temp,padder,25);
  currentLine += "<label for=\"Out\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Out\" name=\"Outside\" value=\"" + String(bitRead(Sensors[j].Flags,2),DEC) + "\"><br>";  

snprintf(temp,29,"%s","Upper Limit");
strPad(temp,padder,25);
  currentLine += "<label for=\"UL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"UL\" name=\"UpperLim\" value=\"" + String(Sensors[j].limitUpper,DEC) + "\"><br>";

snprintf(temp,29,"%s","Lower Limit");
strPad(temp,padder,25);
  currentLine += "<label for=\"LL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"LL\" name=\"LowerLim\" value=\"" + String(Sensors[j].limitLower,DEC) + "\"><br>";

snprintf(temp,29,"%s","Poll Int (sec)");
strPad(temp,padder,25);
  currentLine += "<label for=\"POLL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"POLL\" name=\"PollInt\" value=\"" + String(Sensors[j].PollingInt,DEC) + "\"><br>";

snprintf(temp,29,"%s","Send Int (Sec)");
strPad(temp,padder,25);
  currentLine += "<label for=\"SEND\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"SEND\" name=\"SendInt\" value=\"" + String(Sensors[j].SendingInt,DEC) + "\"><br>";

/*
  snprintf(temp,29,"%s","Recheck Sensor");
  strPad(temp,padder,25);
  currentLine += "<label for=\"recheck\" class=\"switch\">" + (String) temp;
  currentLine += "<input id=\"recheck\" type=\"checkbox\" name=\"recheckSensor\"><span class=\"slider round\"></span></label><br>";
 */
 
  currentLine += "<button type=\"submit\">Submit</button><br><br>";
  currentLine += "<button type=\"submit\" formaction=\"/UPDATESENSORREAD\">Recheck this sensor</button><br><br>";
  currentLine += "<button type=\"submit\" formaction=\"/LASTSNS\">Prior Sensor</button>";
  currentLine += "<button type=\"submit\" formaction=\"/NEXTSNS\">Next Sensor</button>";
  currentLine += "</p>";
  currentLine += "</form>";
  
  currentLine += "<br>";

  currentLine += "     ";
  currentLine +=  "snsType: ";
  currentLine += String(Sensors[j].snsType,DEC);
  currentLine += "<br>";

  currentLine += "     ";
  currentLine += "snsID: ";
  currentLine +=  String(Sensors[j].snsID,DEC);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "LastRead: ";
  currentLine += String(Sensors[j].LastReadTime,DEC);
  currentLine += " = ";
  currentLine += (String) dateify(Sensors[j].LastReadTime);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "LastSend: ";
  currentLine +=  String(Sensors[j].LastSendTime,DEC);
  currentLine += " = ";
  currentLine += (String) dateify(Sensors[j].LastSendTime);
  currentLine +=  "<br>";

/*
  currentLine += "     ";
  currentLine +=  "Monitored: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,1);
  currentLine +=  "<br>";
*/

  currentLine += "     ";
  currentLine +=  "IsFlagged: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,0);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "Calculated Value: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,3);
  currentLine += "<br>";

  currentLine += "     ";
  currentLine +=  "Predictive Value: ";
  currentLine +=  (String) bitRead(Sensors[j].Flags,4);
  currentLine += "<br>";

  currentLine += "     ";
  currentLine +=  "UpperLim: ";
  currentLine += String(Sensors[j].limitUpper,DEC);
  currentLine +=  "<br>";

  currentLine += "     ";
  currentLine +=  "LowerLim: ";
  currentLine +=  String(Sensors[j].limitLower,DEC);
  currentLine +=  "<br>";


  currentLine += "-----------------------<br>";

  #ifdef _USEBARPRED
    currentLine += "Hourly_air_pressures (most recent [top] entry was ";
    currentLine +=  (String) dateify(LAST_BAR_READ);
    currentLine += "):<br>";

    for (byte j=0;j<24;j++)  {
      currentLine += "     ";
      currentLine += String(BAR_HX[j],DEC);
      currentLine += "<br>"; 
    }

  #endif 

currentLine += "</p></body></html>";

   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif

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
  snprintf(output,8,"00000000");
  for (byte i = 0; i < 8; i++) {
    if (invert) {
      if (value & (1 << i)) output[i] = '1';
      else output[i] = '0';
    } else {
      if (value & (1 << i)) output[8-i] = '1';
      else output[8-i] = '0';
    }
  }

  return;
}
