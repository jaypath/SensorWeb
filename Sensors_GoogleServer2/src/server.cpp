#include "server.hpp"

extern WebServer server;
extern String GsheetID; //file ID for this month's spreadsheet
extern String GsheetName; //file name for this month's spreadsheet
extern String lastError;
extern weathertype WeatherData;

extern SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
extern struct ScreenFlags ScreenInfo;


bool uploadData(bool gReady) {
    //check upload status here.

  if (ScreenInfo.lastGsheetUploadSuccess && ScreenInfo.t-ScreenInfo.lastGsheetUploadTime<(ScreenInfo.uploadGsheetInterval*60)) return true; //everything is ok, just not time to upload!

  if (gReady==false) return false; //gsheets not ready!

  #ifdef _DEBUG
    Serial.println("uploading to sheets!");
  #endif

  if (file_uploadSensorData()==false) {
    ScreenInfo.lastGsheetUploadSuccess=false;
    ScreenInfo.uploadGsheetFailCount++; //an actual error prevented upload!
  }
  else {
    ScreenInfo.lastGsheetUploadSuccess=true;
    ScreenInfo.lastGsheetUploadTime = ScreenInfo.t;
    ScreenInfo.uploadGsheetFailCount=0; //upload success!
  }

  return true;
}


bool SendData() {
  
  //special case for senddata... just send info relative to google server (ie, was last upload successful)

#ifdef  ARDID
   byte arduinoID = ARDID;
#else
byte arduinoID = WiFi.localIP()[3];
#endif

//set flags value
byte Flags=0;

if (ScreenInfo.lastGsheetUploadSuccess==false)  bitWrite(Flags,0,1);
bitWrite(Flags,1,1); //monitored
bitWrite(Flags,7,1); //critical


WiFiClient wfclient;
HTTPClient http;

if(WiFi.status() == WL_CONNECTED){
  String payload;
  String URL;
  String tempstring;
  int httpCode=404;
  tempstring = "/POST?IP=" + WiFi.localIP().toString() + "," + "&varName=GSheets";
  tempstring = tempstring + "&varValue=";
  tempstring = tempstring + String(ScreenInfo.uploadGsheetFailCount, DEC);
  tempstring = tempstring + "&Flags=";
  tempstring = tempstring + String(Flags, DEC);
  tempstring = tempstring + "&logID=";
  tempstring = tempstring + String(arduinoID, DEC);
  tempstring = tempstring + ".99.1" + "&timeLogged=" + String(ScreenInfo.lastGsheetUploadTime, DEC) + "&isFlagged=" + String(bitRead(Flags,0), DEC) + "&SendingInt=" + String(3600, DEC);

  URL = "http://192.168.68.93";
  URL = URL + tempstring;

  http.useHTTP10(true);
//note that I'm coverting lastreadtime to GMT

  #ifdef _DEBUG
      Serial.print("sending this message: ");
      Serial.println(URL.c_str());
  #endif

  http.begin(wfclient,URL.c_str());
  httpCode = (int) http.GET();
  payload = http.getString();
  http.end();


  if (httpCode == 200) {
    ScreenInfo.LastServerUpdate = ScreenInfo.t;
    return true;
  }
}

return false;

}

bool getWeather() {


  if (ScreenInfo.t-WeatherData.lastWeatherFetch<(WeatherData.WeatherFetchInterval*60)) return false; //not time to update
  WiFiClient wfclient;
  HTTPClient http;
 
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String tempstring;
    int httpCode=404;
    

    tempstring = "http://192.168.68.93/REQUESTWEATHER?";
    for (byte j=0;j<24;j++) tempstring += "hourly_temp=" + (String) j + "&";
    for (byte j=0;j<24;j++) tempstring += "hourly_weatherID=" + (String) j + "&";
    for (byte j=0;j<WeatherData.daystoget;j++) tempstring += "daily_weatherID=" + (String) j + "&";
    for (byte j=0;j<WeatherData.daystoget;j++) tempstring += "daily_tempMax=" + (String) j + "&";
    for (byte j=0;j<WeatherData.daystoget;j++) tempstring += "daily_tempMin=" + (String) j + "&";
    for (byte j=0;j<WeatherData.daystoget;j++) tempstring += "daily_pop=" + (String) j + "&";
    tempstring += "isFlagged=1&sunrise=1&sunset=1";

    http.useHTTP10(true);    
    http.begin(wfclient,tempstring.c_str());
    httpCode = http.GET();
    payload = http.getString();
    http.end();

    if (!(httpCode >= 200 && httpCode < 300)) return false;
    
    for (byte j=0;j<24;j++) {
      WeatherData.hourly_temp[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<24;j++) {
      WeatherData.hourly_weatherID[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<WeatherData.daystoget;j++) {
      WeatherData.daily_weatherID[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<WeatherData.daystoget;j++) {
      WeatherData.daily_tempMAX[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of ";"
    }

    for (byte j=0;j<WeatherData.daystoget;j++) {
      WeatherData.daily_tempMIN[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<WeatherData.daystoget;j++) {
      WeatherData.daily_PoP[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    //ScreenInfo.isFlagged=payload.substring(0, payload.indexOf(";",0)).toInt(); //do not store isflagged here, just remove it from the list
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    WeatherData.sunrise=payload.substring(0, payload.indexOf(";",0)).toInt();
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    WeatherData.sunset = payload.substring(0, payload.indexOf(";",0)).toInt();

    WeatherData.lastWeatherFetch=t;
  }

  //update hPA
  uint16_t tempval = find_sensor_name("Outside", 13); //bme pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 9); //bmp pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 19); //bme680 pressure
  if (tempval !=255) {
    tempval = Sensors[(byte) tempval].snsValue;
    ScreenInfo.LAST_hPA = ScreenInfo.CURRENT_hPA;
    ScreenInfo.CURRENT_hPA = tempval;
  }


  return true;
  
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



void handleTIMEUPDATE() {


  updateTime(10,20);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}


void handleRoot() {

  String currentLine;
  currentLine.reserve(5000);

  currentLine = "<!DOCTYPE html><html><head><title>Pleasant Sensor Server</title></head><body>";
  
  currentLine = currentLine + "<h2>" + (String) dateify(0,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br><p>";
  currentLine += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  currentLine += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  currentLine += "<button type=\"submit\">Update Time</button><br>";
  currentLine += "</FORM><br>";

  
  currentLine += "<a href=\"/LIST\">List all sensors</a><br>";
  currentLine = currentLine + "isFlagged: " + (String) ScreenInfo.isFlagged + "<br>";
  currentLine = currentLine + "Sensor Data: <br>";

  currentLine = currentLine + "----------------------------------<br>";      

  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM-1;j++)  {
    used[j] = 255;
  }

  byte usedINDEX = 0;  
  char padder[2] = ".";
  char tempchar[9] = "";
  char temp[26];
        
  for (byte j=0;j<SENSORNUM;j++)  {
//    currentLine += "inIndex: " + (String) j + " " + (String) inIndex(j,used,SENSORNUM) + "<br>";
    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      currentLine += "<FORM action=\"/REQUESTUPDATE\" method=\"get\">";
      currentLine += "<p style=\"font-family:monospace\">";

      currentLine += "<input type=\"hidden\" name=\"SensorNum\" value=\"" + (String) j + "\"><br>";  

      currentLine = currentLine + "Arduino: " + Sensors[j].ardID+"<br>";
      sprintf(temp,"%s","IP Address");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + "</label>";
      currentLine += " <a href=\"http://" + IP2String(Sensors[j].IP) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + IP2String(Sensors[j].IP) + "</a><br>";
      
      currentLine = currentLine +  String("..............................") + String("<br>");

      sprintf(temp,"%s","Sensor Name");
      strPad(temp,padder,25);
      currentLine += (String) "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsName + "</label><br>";

      sprintf(temp,"%s","Value");
      strPad(temp,padder,25);
      currentLine += (String)"<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsValue + "</label><br>";

      sprintf(temp,"%s","Flagged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[j].Flags,0) + "</label><br>";
      
      sprintf(temp,"%s","Sns Type");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";

      sprintf(temp,"%s","Last Recvd");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeRead + " " + (String) dateify(Sensors[j].timeRead,"mm/dd hh:nn:ss") +  "</label><br>";
      
      sprintf(temp,"%s","Last Logged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeLogged + " " + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";

      Byte2Bin(Sensors[j].Flags,tempchar,true);
      sprintf(temp,"%s","Flags");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";
      currentLine += "<br>";
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
 
    
          sprintf(temp,"%s","Sensor Name");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsName + "</label><br>";
    
          sprintf(temp,"%s","Value");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsValue + "</label><br>";
    
          sprintf(temp,"%s","Flagged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[jj].Flags,0) + "</label><br>";
          
          sprintf(temp,"%s","Sns Type");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";
    
          sprintf(temp,"%s","Last Recvd");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeRead + " " + (String) dateify(Sensors[jj].timeRead,"mm/dd hh:nn:ss") + "</label><br>";
          
          sprintf(temp,"%s","Last Logged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeLogged + " " + (String) dateify(Sensors[jj].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";
    
          Byte2Bin(Sensors[jj].Flags,tempchar,true);
          sprintf(temp,"%s","Flags");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";      
          currentLine += "<br>";
              
        }

      }

      currentLine += "<button type=\"submit\" formaction=\"/CLEARSENSOR\">Clear ArdID Sensors</button><br>";
    
      currentLine += "<button type=\"submit\">Request Update</button><br>";

      currentLine += "</p></form><br>----------------------------------<br>";   
    }

  }
  
      currentLine += "</p></body></html>";   

   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif
    
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void handlePost() {

        #ifdef _DEBUG
          Serial.println("handlePOST hit.");
        #endif

SensorVal S;
uint8_t tempIP[4] = {0,0,0,0};

  for (byte k=0;k<server.args();k++) {   

      #ifdef _DEBUG
      Serial.print("Received this argname/arg: ");
      Serial.printf("%s --> %s\n",server.argName(k),server.arg(k).c_str());
      #endif

      if ((String)server.argName(k) == (String)"logID")  breakLOGID(String(server.arg(k)),&S.ardID,&S.snsType,&S.snsID);
      if ((String)server.argName(k) == (String)"IP") {
        IPString2ByteArray(String(server.arg(k)),S.IP);        
      }

      if ((String)server.argName(k) == (String)"varName") snprintf(S.snsName,30,"%s", server.arg(k).c_str());
      if ((String)server.argName(k) == (String)"varValue") S.snsValue = server.arg(k).toDouble();
      if ((String)server.argName(k) == (String)"timeLogged") S.timeRead = server.arg(k).toDouble();      //time logged at sensor is time read by me
      if ((String)server.argName(k) == (String)"Flags") S.Flags = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"SendingInt") S.SendingInt = server.arg(k).toInt();
  }
  

          #ifdef _DEBUG
          Serial.println("handlePOST parsed input.");
        #endif

  S.timeLogged = ScreenInfo.t; //time logged by me is when I received this.
  if (S.timeRead == 0)     S.timeRead = ScreenInfo.t;
  

  S.localFlags=0;

  storeSensorSD(&S);

  int16_t sn = findDev(&S, true); //true makes sure finddev always returns a valid entry


  if (sn<0) {


    server.send(401, "text/plain", "Critical failure... I could not find the sensor or enter a new value. This should not be possible, so this error is undefined."); // Send HTTP status massive error
    lastError = "Out of sensor space";
    ScreenInfo.lastErrorTime = ScreenInfo.t;

    return;
  }

    String stemp = S.snsName;
    if ((stemp.indexOf("Outside")>-1 && S.snsType==4 ) ) { //outside temp
      ScreenInfo.localTemp = S.snsValue;
      ScreenInfo.localTempIndex = sn;
      ScreenInfo.localTempTime = ScreenInfo.t;
    }


  S.snsValue_MIN= Sensors[sn].snsValue_MIN;
  S.snsValue_MAX= Sensors[sn].snsValue_MAX;

  if ((double) S.snsValue_MAX<S.snsValue) S.snsValue_MAX=S.snsValue;
  if ((double) S.snsValue_MIN>S.snsValue) S.snsValue_MIN=S.snsValue;

    
        #ifdef _DEBUG
          Serial.println("handlePOST about to enter finddev.");
        #endif

  if (sn<0 || sn >= SENSORNUM) return;
  Sensors[sn] = S;

        #ifdef _DEBUG
          Serial.println("handlePOST assigned val.");
        #endif

  server.send(200, "text/plain", "Received Data"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request


}





//--------------------------------------
//--------------------------------------
//--------------------------------------
//--------------------------------------
//SPREADSHEET FUNCTIONS

bool file_deleteSpreadsheetByID(String fileID) {
  FirebaseJson response;
  
   bool success= GSheet.deleteFile(&response /* returned response */, fileID /* spreadsheet Id to delete */);
   //String tmp;
   //response.toString(tmp, true);
//  tft.printf("Delete result: %s",tmp.c_str());
  return success;
}

void file_deleteSpreadsheetByName(String filename){
  FirebaseJson response;
  
  String fileID;


  do {
    fileID = file_findSpreadsheetIDByName(filename,false);
    if (fileID!="" && fileID.substring(0,5)!="ERROR") {
      tft.setTextFont(SMALLFONT);
      sprintf(ScreenInfo.tempbuf,"Deletion of %s status: %s\n",fileID.c_str(),file_deleteSpreadsheetByID(fileID) ? "OK" : "FAIL");
      tft.drawString(ScreenInfo.tempbuf, 0, ScreenInfo.Ypos);
      ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(SMALLFONT)+2;
    }
  } while (fileID!="" && fileID.substring(0,5)!="ERROR");
  return;
}


String file_findSpreadsheetIDByName(String sheetname, bool createfile) {
  //returns the file ID. Note that sheetname is NOT unique, so multiple spreadsheets could have the same name. Only ID is unique.
  //returns "" if no such filename
          
  FirebaseJson filelist;

  String resultstring = "ERROR:";  
  String thisFileID = "";
  
  String tmp;
  bool success = GSheet.listFiles(&filelist /* returned list of all files */);

    #ifdef NOISY
      tft.setTextFont(SMALLFONT);
      snprintf(ScreenInfo.tempbuf,60,"%s search result: %s\n",sheetname.c_str(),success?"OK":"FAIL");
      tft.drawString(ScreenInfo.tempbuf, 0, ScreenInfo.Ypos);
      ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(SMALLFONT)+2;
    #endif
  
  if (success) {
    #ifdef _DEBUG
      Serial.println("Got the obj list!");
    #endif

    if (sheetname == "***") {
      //special case, return the entire file index!
      filelist.toString(resultstring,true);
      tft.printf("%s\n",resultstring.c_str());
      return "";
    }

    //put file array into data object
    FirebaseJsonData result;
    filelist.get(result,"files");

    if (result.success) {
      #ifdef _DEBUG
        Serial.println("File list in firebase!");
      #endif

      FirebaseJsonArray thisfile;
      result.get<FirebaseJsonArray /* type e.g. FirebaseJson or FirebaseJsonArray */>(thisfile /* object that used to store value */);
            
      int fileIndex=0;
      bool foundit=false;
  
      do {
        thisfile.get(result,fileIndex++); //iterate through each file
        if (result.success) {
          FirebaseJson fileinfo;

          //Get FirebaseJson data
          result.get<FirebaseJson>(fileinfo);

//          fileinfo.toString(tmp,true);

          size_t count = fileinfo.iteratorBegin();
          
          for (size_t i = 0; i < count; i++) {
            
            FirebaseJson::IteratorValue value = fileinfo.valueAt(i);
            String s1(value.key);
            String s2(value.value);
            s2=s2.substring(1,s2.length()-1);

            if (s1 == "id") thisFileID = s2;
            if (s1 == "name" && (s2 == sheetname || sheetname == "*")) foundit=true; //* is a special case where any file returned
          }
          fileinfo.iteratorEnd();
          if (foundit) {
                #ifdef _DEBUG
                  Serial.println("Found file of interest!");
                  Serial.printf("FileID is %s\n", thisFileID.c_str());

                #endif

              #ifdef NOISY
                tft.setTextFont(SMALLFONT);
                snprintf(ScreenInfo.tempbuf,60,"FileID is %s\n",thisFileID.c_str());
                tft.drawString(ScreenInfo.tempbuf, 0, ScreenInfo.Ypos);
                ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(SMALLFONT)+2;
              #endif
           return thisFileID;
          }
        }
      } while (result.success);

    }

    resultstring= "ERROR: [" + sheetname + "] not  found,\nwith createfile=" + (String) createfile;
                #ifdef _DEBUG
                  Serial.println("Did not find the file.");
                #endif

    lastError = resultstring;
  
    if (createfile) {
                  #ifdef _DEBUG
                  Serial.printf("Trying to create file %s\n", GsheetName.c_str());

                #endif

      resultstring = file_createSpreadsheet(sheetname,false);
                  #ifdef _DEBUG
                  Serial.printf("result: %s\n", resultstring.c_str());

                #endif

      resultstring = file_createHeaders(resultstring,"SnsID,IP Add,SnsName,Time Logged,Time Read,HumanTime,Flags,Reading");
      #ifdef NOISY
        tft.setTextFont(SMALLFONT);
        snprintf(ScreenInfo.tempbuf,60,"new fileid: %s\n",resultstring.c_str());
        tft.drawString(ScreenInfo.tempbuf, 0, ScreenInfo.Ypos);
        ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(SMALLFONT)+2;
      #endif
    }         
       
    
  } else { //gsheet error
      #ifdef NOISY
        tft.setTextFont(SMALLFONT);
        snprintf(ScreenInfo.tempbuf,60,"Gsheet error:%s\n",GSheet.errorReason().c_str());
        tft.drawString(ScreenInfo.tempbuf, 0, ScreenInfo.Ypos);
        ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(SMALLFONT)+2;
      #endif
    resultstring = "ERROR: gsheet error";
    lastError = resultstring;
    ScreenInfo.lastErrorTime = ScreenInfo.t;
  }

  return resultstring; 

}


String file_createSpreadsheet(String sheetname, bool checkFile=true) {


  String fileID = "";
  if (checkFile) {
    file_findSpreadsheetIDByName(sheetname,false);
  
    if (fileID.substring(0,5)!="ERROR") {
      lastError = "Tried to create a file that exists!";   
      ScreenInfo.lastErrorTime = ScreenInfo.t;
      return fileID;
    }
  } 


  FirebaseJson spreadsheet;
  spreadsheet.set("properties/title", sheetname);
  spreadsheet.set("sheets/properties/title", "Sheet1");
  spreadsheet.set("sheets/properties/sheetId", 1); //readonly
  spreadsheet.set("sheets/properties/sheetType", "GRID");
  spreadsheet.set("sheets/properties/sheetType", "GRID");
  spreadsheet.set("sheets/properties/gridProperties/rowCount", 200000);
  spreadsheet.set("sheets/properties/gridProperties/columnCount", 10);

  spreadsheet.set("sheets/developerMetadata/[0]/metadataValue", "Jaypath");
  spreadsheet.set("sheets/developerMetadata/[0]/metadataKey", "Creator");
  spreadsheet.set("sheets/developerMetadata/[0]/visibility", "DOCUMENT");

  FirebaseJson response;

  bool success = GSheet.create(&response /* returned response */, &spreadsheet /* spreadsheet object */, USER_EMAIL /* your email that this spreadsheet shared to */);

  if (success==false) {
    #ifdef NOISY
      tft.setTextFont(SMALLFONT);
      snprintf(ScreenInfo.tempbuf,60,"CREATE: gsheet failed to create object\n");
      tft.drawString(ScreenInfo.tempbuf, 0, ScreenInfo.Ypos);
      ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(SMALLFONT)+2;
    #endif

    lastError = "ERROR: Gsheet failed to create";
    ScreenInfo.lastErrorTime = ScreenInfo.t;
    
  
    return "ERROR: Gsheet failed to create";
  }

  return fileID;
}

bool file_createHeaders(String fileID,String Headers) {

  /* only accept file ID
  String fileID = file_findSpreadsheetIDByName(sheetname,true);
  fileID = file_findSpreadsheetIDByName(sheetname);
  */

  if (fileID.substring(0,5)=="ERROR") {
    lastError = fileID;
    ScreenInfo.lastErrorTime = ScreenInfo.t;
     return false;
  }

  uint8_t cnt = 0;
  int strOffset=-1;
  String temp;
  FirebaseJson valueRange;
  FirebaseJson response;
  valueRange.add("majorDimension", "ROWS");
  while (Headers.length()>0 && Headers!=",") {
    strOffset = Headers.indexOf(",", 0);
    if (strOffset == -1) { //did not find the "," so the remains are the last header.
      temp=Headers;
    } else {
      temp = Headers.substring(0, strOffset);
      Headers.remove(0, strOffset + 1);
    }

    if (temp.length()>0) valueRange.set("values/[0]/[" + (String) (cnt++) + "]", temp);
  }


  bool success = GSheet.values.append(&response /* returned response */, fileID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);

  return success;
  
} 
  

bool file_uploadSensorData(void) {

  FirebaseJson valueRange;
  FirebaseJson response;

  //time_t t = now(); now a global!


  if (GsheetID=="" || ScreenInfo.lastGsheetUploadSuccess==0) {
    //need to reset file ID
    GsheetName = "ArduinoLog" + (String) dateify(ScreenInfo.t,"yyyy-mm");
    GsheetID = file_findSpreadsheetIDByName(GsheetName,true);
    if (GsheetID=="" || GsheetID.substring(0,5)=="ERROR") {
      lastError = GsheetID;
      ScreenInfo.lastErrorTime = ScreenInfo.t;
      return false;
    }

  }
  

  String logid="";
  byte rowInd = 0;
  byte snsArray[SENSORNUM] = {255};
  for (byte j=0;j<SENSORNUM;j++) snsArray[j]=255;

  for (byte j=0;j<SENSORNUM;j++) {
    if (isSensorInit(j) && Sensors[j].timeLogged>0 && bitRead(Sensors[j].localFlags,0)==0) {
      logid = (String) Sensors[j].ardID + "." + (String)  Sensors[j].snsType + "." + (String)  Sensors[j].snsID;

      valueRange.add("majorDimension", "ROWS");
      valueRange.set("values/[" + (String) rowInd + "]/[0]", logid);
      valueRange.set("values/[" + (String) rowInd + "]/[1]", IP2String(Sensors[j].IP));
      valueRange.set("values/[" + (String) rowInd + "]/[2]", (String) Sensors[j].snsName);
      valueRange.set("values/[" + (String) rowInd + "]/[3]", (String) Sensors[j].timeLogged);
      valueRange.set("values/[" + (String) rowInd + "]/[4]", (String) Sensors[j].timeRead);
      valueRange.set("values/[" + (String) rowInd + "]/[5]", (String) dateify(Sensors[j].timeLogged,"mm/dd/yy hh:nn:ss")); //use timelogged, since some sensors have no clock
      valueRange.set("values/[" + (String) rowInd + "]/[6]", (String) bitRead(Sensors[j].Flags,0));
      valueRange.set("values/[" + (String) rowInd + "]/[7]", (String) Sensors[j].snsValue);
  
      snsArray[rowInd++] = j;
    }
  }

        // For Google Sheet API ref doc, go to https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/append

  bool success = GSheet.values.append(&response /* returned response */, GsheetID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
  if (success) {
//            response.toString(Serial, true);
    for (byte j=0;j<SENSORNUM;j++) {
      if (snsArray[j]!=255)     bitWrite(Sensors[snsArray[j]].localFlags,0,1);
    }
    ScreenInfo.lastGsheetUploadTime = now(); 
  } else {
    lastError = "ERROR Failed to update: " + GsheetID;
    ScreenInfo.lastErrorTime = ScreenInfo.t;
    return false;
  }

return success;

        
}

void tokenStatusCallback(TokenInfo info)
{
    if (info.status == token_status_error)
    {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
        GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
    }
    else
    {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
    }
}


