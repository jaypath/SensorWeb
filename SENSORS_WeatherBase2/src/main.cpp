//#define _DEBUG 0
//#define _WEBDEBUG 0


//Version 12 - 
/*
 * v11.1
 * now obtains info from Kiyaan's server
 * TFT attached to esp8266
 * no nrf radio in the loop
 * 
 * v11.2
 * Receives data directly from sensors
 * 
 * v11.3 new sensor sending definition, including flags
 * 
 * v12 - many changes. sends daa to google drive connected arduino
  */

/*
//0 - not defined/not a sensor/do not use
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - 
//7 - distance, HC-SR04
//8 - human presence (mm wave)
//9 - BMP pressure
//10 - BMP temp
//11 - BMP altitude
//12 - Pressure derived prediction (uses an array called BAR_HX containing hourly air pressure for past 24 hours). REquires _USEBARPRED be defined
//13 - BMe pressure
//14 - BMe temp
//15 - BMe humidity
//16 - BMe altitude
//17 - BME680 temp
18 - BME680 rh
19 - BME680 air press
20  - BME680 gas sensor
21 - human present (mmwave)
40 - any binary, 1=yes/true/on
41 = any on/off switch
42 = any yes/no switch
43 = any 3 way switch
50 = total HVAC time
55 - heat on/off {requires N DIO Pins}
56 - a/c  on/off {requires 2 DIO pins... compressor and fan}
57 - a/c fan on/off
60 -  battery power
61 - battery %
70 - leak
99 = any numerical value
*/


#include <Arduino.h>
#include <String.h>

#include <globals.hpp>
#include <TimeLib.h>
#include <timesetup.hpp>
#include <utility.hpp>
#include <server.hpp>
#include <weather.hpp>


#include "ArduinoJson.h"
#include <SD.h>
#include "ArduinoOTA.h"


//#include "free_fonts.h"
//#define BG_COLOR 0xD69A


//#define ESP_SSID "kandy-hispeed" // Your network name here
//#define ESP_PASS "manath77" // Your network password here

#ifdef _WEBDEBUG
  String WEBDEBUG = "";
#endif

//gen global types

/*
//gen unions
union convertULONG {
  char str[4];
  unsigned long val;
};
union convertINT {
  char str[2];
  int16_t val;
};

union convertBYTE {
  char str;
  uint8_t  val;
};
 
union convertSensorVal {
  SensorVal a;
  uint8_t b[14];
};
*/

//globals

extern LGFX tft;
extern uint8_t SECSCREEN;
extern WiFi_type WIFI_INFO;
extern Screen I;
extern String WEBHTML;
extern time_t ALIVESINCE;
extern uint8_t OldTime[4];
extern WeatherInfo WeatherData;
extern SensorVal Sensors[SENSORNUM];
extern uint32_t LAST_WEB_REQUEST;
extern WeatherInfo WeatherData;
extern double LAST_BAR;
  
//fuction declarations
uint32_t set_color(byte r, byte g, byte b);
uint16_t read16(File &f);
uint32_t read32(File &f);
int ID2Icon(int); //provide a weather ID, obtain an icon ID
void drawBmp(const char*, int16_t, int16_t, uint16_t alpha = TRANSPARENT_COLOR);
void fcnDrawHeader(time_t t);
void fcnDrawScreen();
void fcnDrawWeather(time_t t);
void fcnDrawSensors(int Y);
void fcnPrintTxtHeatingCooling(int x,int y);
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ,int x=-1,int y=-1,bool autocontrast = false);
void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1,bool autocontrast=false);
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1);
void fcnPredictionTxt(char* tempPred, uint32_t* fg, uint32_t* bg);
void fcnPressureTxt(char* tempPres, uint32_t* fg, uint32_t* bg);
void drawBox(String roomname, int X, int Y, byte boxsize_x,byte boxsize_y);
uint16_t temp2color(int temp, bool invertgray = false);




void setup()
{
  WEBHTML.reserve(175000); //maximum size of web call is ~170k

  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
tft.init();

  // Setting display to landscape
  tft.setRotation(2);

  
  tft.printf("Running setup\n");

  if(!SD.begin(41)){  //CS=41
    tft.println("SD Mount Failed");
  } 
  else {
    tft.println("SD Mount Succeeded");
  }


  I.lastDraw = 0;
  I.ScreenNum = 0;
  I.redraw = SECSCREEN; 
  I.isFlagged = false;
  I.wasFlagged = false;
  WeatherData.lat = LAT;
  WeatherData.lon = LON;
  


  #ifdef _DEBUG
    Serial.begin(115200);
  #endif

  

  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  tft.println("Setup...");
  tft.setTextSize(1);
  tft.println("TFT OK. ");
    
  WIFI_INFO.DHCP = IPAddress(192,168,68,1);
  WIFI_INFO.DNS = IPAddress(192,168,68,1);
  WIFI_INFO.GATEWAY = IPAddress(192,168,68,1);
  WIFI_INFO.SUBNET = IPAddress(255,255,252,0);
  WIFI_INFO.MYIP = IPAddress (192,168,68,0);




  #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
  #endif

  byte retries = connectWiFi();
  tft.println("Attempting Wifi connection, please wait...");
  if (WiFi.status() != WL_CONNECTED)
  {
    tft.print("Wifi failed after ");
    tft.print(retries);
    tft.println(" attempts.");
  } else {
    tft.print("Wifi cnnected after " );
    tft.print(retries);
    tft.print(" attempts. Wifi IP is ");
    tft.println(WiFi.localIP());  
  }



tft.println("Connecting ArduinoOTA...");

    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname("JayWeatherSrvr");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    tft.fillScreen(BG_COLOR);            // Clear screen
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(0,0);
    tft.println("OTA started, receiving data.");
  });
  ArduinoOTA.onEnd([]() {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.println("OTA End. About to reboot!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef _DEBUG
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    #endif
  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef _DEBUG
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
    tft.setCursor(0,0);
    tft.print("OTA error: ");
    tft.println(error);
  });
  ArduinoOTA.begin();
    #ifdef _DEBUG
      Serial.println("Connected!");
      Serial.println(WiFi.localIP());
    #endif

  


    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/ALLSENSORS", handleALL);               // Call the 'handleall' function when a client requests URI "/"
    server.on("/POST", handlePost);   
    server.on("/REQUESTUPDATE",handleREQUESTUPDATE);
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.on("/REQUESTWEATHER",handleREQUESTWEATHER);
    server.on("/GETSTATUS",handleGETSTATUS);
    server.on("/REBOOT",handleReboot);
    server.on("/UPDATEDEFAULTS",handleUPDATEDEFAULTS);

    server.onNotFound(handleNotFound);
    server.begin();
    //init globals
    initSensor(-256);
    tft.println("Set up TimeClient...");

//    setSyncInterval(600); //set NTP interval for sync in sec
    timeClient.begin(); //time is in UTC
    updateTime(10,250); //check if DST and set time to EST or EDT
    
    tft.print("TimeClient OK.  ");
    tft.println("Starting...");

    delay(250);

    ALIVESINCE = now();

    #ifdef _DEBUG
      Serial.println("Done");
      delay(3000);
    #endif

    tft.println("Done with setup.");
    delay(250);

}




//sensor fcns

void parseLine_to_Sensor(String token) {
  //the token contains:
  String logID; //#.#.# [as string], ARDID, SNStype, SNSID
  SensorVal S;
  uint8_t tempIP[4] = {0,0,0,0};

  String temp;
  int16_t strOffset;

  #ifdef _DEBUG
    Serial.println("I am beginning parsing!");
    Serial.println(token);
  #endif

  strOffset = token.indexOf(",",0);
  if (strOffset == -1) { //did not find the comma, we may have cut the last one. abort.
    return;
  } else {
    //element 1 is IP
    logID= token.substring(0,strOffset); //end index is NOT inclusive
    token.remove(0,strOffset+1);

    IPString2ByteArray(logID,tempIP);
    S.IP = tempIP;

    //element2 is logID
    logID= token.substring(0,strOffset); //end index is NOT inclusive
    token.remove(0,strOffset+1);
    if (breakLOGID(logID,&S.ardID,&S.snsType,&S.snsID) == false) return;

    #ifdef _DEBUG
      Serial.print("token: ");
      Serial.println(token);
      Serial.print("logID: ");
      Serial.println(logID);
      Serial.print("ardid: ");
      Serial.println(S.ardID);
      Serial.print("snsType: ");
      Serial.println(S.snsType);
      Serial.print("snsID: ");
      Serial.println(S.snsID);
    #endif
  }

//timelogged
  strOffset = token.indexOf(",",0);
  temp= token.substring(0,strOffset); //
  token.remove(0,strOffset+1);
  stringToLong(temp,&S.timeRead);

//  uint32_t timeReceived
  strOffset = token.indexOf(",",0);
  temp= token.substring(0,strOffset); //
  token.remove(0,strOffset+1);
  stringToLong(temp,&S.timeLogged);

//  String varName; // [as string]
  strOffset = token.indexOf(",",0);
  temp = token.substring(0,strOffset); 
  token.remove(0,strOffset+1);
  snprintf(S.snsName,29,"%s",temp.c_str());
  #ifdef _DEBUG
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("snsName: ");
    Serial.println(S.snsName);

  #endif

//  double value;
  strOffset = token.indexOf(",",0);
  temp= token.substring(0,strOffset); 
  token.remove(0,strOffset+1);
  S.snsValue = temp.toDouble();
  #ifdef _DEBUG
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("value: ");
    Serial.println(S.snsValue);
  #endif

//bool isFlagged; last one
  strOffset = token.indexOf(",",0);
  if (strOffset==-1) temp= token;
  else temp= token.substring(0,strOffset); 
  //token.remove(0,strOffset+1);
  if (temp.toInt()!=0) {
    bitWrite(S.Flags,0,1);
  }
  #ifdef _DEBUG
    Serial.print("token: ");
    Serial.println(token);
    Serial.print("isFlagged: ");
    Serial.println(S.Flags);
  #endif


  int sn = findDev(&S,true);

  if (sn<0) return;
  Sensors[sn] = S;

  #ifdef _DEBUG
  Serial.print("sn=");
  Serial.print(sn);
  Serial.print(",   ");
  Serial.print(Sensors[sn].snsName);
  Serial.print(", ");
  Serial.print("isFlagged: ");
  Serial.println(Sensors[sn].Flags);
  #endif

}



//weather FCNs

void fcnAssignWeatherArray(uint32_t* timeval, int16_t* wthrArr, byte nval, String payload, double multiplier=1.00) {
byte strOffset = 0;
String tempPayload;
char buf[12] = "";


//process the first value, which is the time
  strOffset = payload.indexOf(",",0);
  tempPayload = payload.substring(0,strOffset);
  tempPayload.toCharArray(buf,12);
  *timeval = strtoul(buf,NULL,0); //do it this way because it is a 32 bit number, so toInt will fail
  payload.remove(0,strOffset+1); //include the comma
  #ifdef _DEBUG
    Serial.println("If the following starts with a comma, then payload removal requires +1");
    Serial.println(payload);
    Serial.println("");
    

  #endif


 //now iterate through the rest of the values!
 byte i=0;
  while (payload.length()>0 && i<nval) {
    strOffset = payload.indexOf(",",0);
    if (strOffset == -1) { //did not find the comma, we may have cut the last one
      wthrArr[i] = int16_t(payload.toDouble()*multiplier);
      payload = "";
    } else {
      tempPayload = payload.substring(0,strOffset);
      wthrArr[i] = int16_t(payload.toDouble()*multiplier);
      payload.remove(0,strOffset+1);
    }
    i++;   
  }

}



int ID2Icon(int weatherID) {
  //provide a weather ID, obtain an icon ID

  if (weatherID>=200 && weatherID<300) { //t-storm group
    if (weatherID==200 || weatherID==201 || weatherID==210 || weatherID==211 ||  weatherID>229) {
      return 200;
    } else {
      return 201;
    }
  }
    
  if (weatherID>=300 && weatherID<400) { //drizzle group
    return 301;
  }

  if (weatherID>=500 && weatherID<600) { //rain group
    if (weatherID==500 || weatherID==520 ) return 500;
    if (weatherID==501 || weatherID==521) return 501;
    if (weatherID==502 || weatherID==522) return 502;
    if (weatherID==503 || weatherID == 504 || weatherID==531) return 503;
    if (weatherID==511) return 611; //511 is freezing rain, but can use sleet icon for this
  }
    
  if (weatherID>=600 && weatherID<700) { //snow group
    if (weatherID==600 || weatherID==615 || weatherID==620 ) return 600;
    if (weatherID==601 || weatherID==616|| weatherID==621) return 601;
    if (weatherID==602 || weatherID==622) return 602;
    return 611; //sleet category
    
  }

  if (weatherID>=700 && weatherID<800) { //atm group
    return 700; //sleet category
  }

  if (weatherID>=800) { //clear group
  
    if (weatherID>=801 && weatherID<803) return 801;
    return weatherID; //otherwise weatherID matches iconID!
    
  }

  return 999;
}


//tft drawing fcn
void drawBmp(const char *filename, int16_t x, int16_t y,  uint16_t alpha) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  File bmpFS;

    // Open requested file on SD card
  bmpFS = SD.open(filename, "r");

  if (!bmpFS)
  {
    tft.print(filename);
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row;
  uint8_t  r, g, b;

  if (read16(bmpFS) == 0x4D42)
  {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
        
          *tptr = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
          if (*tptr==alpha) *tptr = BG_COLOR; //convert background pixels to background
          *tptr++;
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
      //Serial.print("Loaded in "); Serial.print(millis() - startTime);
      //Serial.println(" ms");
    }
    else tft.println("BMP format not recognized.");
  }
  bmpFS.close();
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void fcnPrintTxtCenter(String msg,byte FNTSZ, int x, int y) {
int X,Y;
    
if (x>=0 && y>=0) {
  tft.setTextFont(FNTSZ);
  X = x-tft.textWidth(msg)/2;
  if (X<0) X=0;
  Y = y-tft.fontHeight(FNTSZ)/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}

tft.setTextFont(FNTSZ);
tft.setCursor(X,Y);
tft.print(msg.c_str());
tft.setTextDatum(TL_DATUM);
    
return;


}

void fcnPrintTxtHeatingCooling(int X,int Y) {
  //print:
  //1,2,3
  //4,5,6
  //where the color of each number is orange for heating, blue for cooling, black for none, or orange on blue for both

  byte FNTSZ = 1;
  String temp;


  uint32_t c_heat = tft.color565(255,180,0); //TFT_ORANGE;
  uint32_t c_ac = tft.color565(0,0,255*.7);
  uint32_t c_fan = tft.color565(135,206,235); //TFT_SKYBLUE;135 206 235
  uint32_t c_FG=BG_COLOR, c_BG=BG_COLOR;
  

  tft.setTextFont(FNTSZ);

  int x = X;
  tft.setTextDatum(TL_DATUM);
  for (byte j=1;j<7;j++) {
    c_FG=BG_COLOR;
    c_BG=BG_COLOR;
  
    if (j==4) {
      x=X;
      Y+= tft.fontHeight(FNTSZ)+2; 
    }

    if (bitRead(I.isHeat,j)) {
      c_FG = c_heat;
      c_BG = BG_COLOR;
      if (bitRead(I.isFan,j)) {
        c_BG = c_fan; 
      }
      if (bitRead(I.isAC,j)) {
        c_BG = c_ac; //warning!!
      }
    } else {
      if (bitRead(I.isAC,j)) {
        c_FG = c_ac;
        c_BG = TFT_RED; //if the fan is not on, this is an alarm!
        if (bitRead(I.isFan,j)) c_BG = c_fan;
      } else {
        if (bitRead(I.isFan,j)) {
          c_FG = c_fan;
          c_BG = BG_COLOR;
        }
      }
    }
    temp = (String) j;
    tft.setTextColor(c_FG,c_BG);    
    x +=tft.drawString(temp,x,Y,1) + 5;
    #ifdef _LIGHTMODE
 c_FG=TFT_DARKGREY;
  #else
 c_FG=TFT_LIGHTGREY;
  #endif
    c_BG=BG_COLOR;
    tft.setTextColor(c_FG,c_BG);    
  }

  temp = "4 5 6"; //placeholder to find new X,Y location
  tft.setCursor(X + tft.textWidth(temp) ,Y-tft.fontHeight(FNTSZ));
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background

  return;
}


void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x,int y,bool autocontrast) {
  //print two temperatures with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position
//autocontrast adjusts contrast for the text color to the median RGB value -128 mod 255

uint16_t bgcolor;

String temp = (String) value1 + "/" + (String) value2;
int X,Y;
if (x>=0 && y>=0) {
  X = x-tft.textWidth(temp)/2;
  if (X<0) X=0;
  Y = y-tft.fontHeight(FNTSZ)/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}

tft.setCursor(X,Y);
if (autocontrast) bgcolor = temp2color(value1,true);
else bgcolor = BG_COLOR;
tft.setTextColor(temp2color(value1),bgcolor);
tft.print(value1);

tft.setTextColor(FG_COLOR,BG_COLOR);
tft.print("/");

if (autocontrast) bgcolor = temp2color(value2,true);
else bgcolor = BG_COLOR;
tft.setTextColor(temp2color(value2),bgcolor);
tft.print(value2);
tft.setTextColor(FG_COLOR,BG_COLOR);

return;
}


void fcnPrintTxtColor(int value,byte FNTSZ,int x,int y,bool autocontrast) {
  //print  temperature with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position
uint16_t bgcolor;
String temp = (String) value;
int X,Y;
tft.setTextFont(FNTSZ);
if (x>=0 && y>=0) {
  X = x-tft.textWidth(temp)/2;
  if (X<0) X=0;
  Y = y-tft.fontHeight(FNTSZ)/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}
if (autocontrast) bgcolor = temp2color(value,true);
else bgcolor = BG_COLOR;
tft.setCursor(X,Y);
tft.setTextColor(temp2color(value),bgcolor);
tft.print(value);
tft.setTextColor(FG_COLOR,BG_COLOR);

return;
}

void checkHeat() {
  //heat byte, from RIGHT to LEFT bits are: heat is on, zone 1, zone 2, zone 3... zone 6, unused
  //cool byte from RIGHT to LEFT bits are: AC compressor is on, zone 1, zone 2, zone 3... zone 6, unused
  //cool byte from RIGHT to LEFT bits are: AC compressor is on, zone 1, zone 2, zone 3... zone 6, unused
  //fan byte from RIGHT TO LEFT bits are: fan is on,  zone 1, zone 2, zone 3... zone 6, unused
  //initialize heat and cool variaiables, now all bits are 0

  /*
  ZONES (in system):
  1=office
  2=mast br
  3=lr/dr
  4=bed
  5=kitch/fam
  6=den

  ZONES (per me, this is hardwired into receiver arduino):
  1=FamRm
  2=LR/DR
  3=office
  4=Bedrms
  5=MastBR
  6=Den

  */
I.isHeat=0;
I.isAC=0;
I.isFan=0;

time_t t = now();

  for (byte j=0;j<SENSORNUM;j++) {
    if (Sensors[j].snsType == 55) { //heat
      if ( (Sensors[j].Flags&1) == 1 && (double) t-Sensors[j].timeLogged < 1800 ) bitWrite(I.isHeat,Sensors[j].snsID,1); //Sensors[j].Flags&1 == 1 means that 0th bit is 1, ie flagged. note that 0th bit is that any heater is on or off!. If a reading is >30 minutes it is too old to be believed
      else bitWrite(I.isHeat,Sensors[j].snsID,0);
    } else {
      if (Sensors[j].snsType == 56 && (Sensors[j].Flags&1)==1) { //ac comp        
        //which ac?
        String S = Sensors[j].snsName;         
        if (S.indexOf("ACDown")>-1) {
          bitWrite(I.isAC,1,1);
          bitWrite(I.isAC,2,1);
          bitWrite(I.isAC,3,1);
          bitWrite(I.isAC,6,1);
        } else {
          bitWrite(I.isAC,4,1);
          bitWrite(I.isAC,5,1);
        }
      }
      if (Sensors[j].snsType == 57 && (Sensors[j].Flags&1)==1) { //ac fan
        String S = Sensors[j].snsName;         
        if (S.indexOf("ACDown")>-1) {
          bitWrite(I.isFan,1,1);
          bitWrite(I.isFan,2,1);
          bitWrite(I.isFan,3,1);
          bitWrite(I.isFan,6,1);
        } else {
          bitWrite(I.isFan,4,1);
          bitWrite(I.isFan,5,1);
        }
      }
    }
  }

  if (I.isHeat>0)     bitWrite(I.isHeat,0,1); //any heater is on    

  if (I.isAC>0)     bitWrite(I.isAC,0,1); //any ac is on
    
  if (I.isFan>0) bitWrite(I.isFan,0,1); //any fan is on
  
}


uint16_t temp2color(int temp,bool invertgray) {
  const uint8_t temp_colors[104] = {
  20, 225, 100, 255, //20
  24, 225, 150, 250, //21 - 24
  27, 225, 200, 250, //25 - 27
  29, 250, 225, 250, //28 - 29
  32, 225, 250, 250, //30 - 32
  34, 100, 175, 250, //33 - 34
  37, 50, 175, 250, //35 - 37
  39, 0, 200, 250, //38 - 39
  42, 0, 225, 250, //40 - 42
  44, 0, 250, 250, //43 - 44
  47, 0, 250, 225, //45 - 47
  51, 0, 250, 200, //48 - 51
  54, 0, 250, 175, //52 - 54
  59, 0, 250, 150, //55 - 59
  64, 0, 250, 125, //60 - 64
  69, 0, 250, 100, //65 - 69
  72, 0, 250, 50, //70 - 72
  75, 0, 250, 0, //73 - 75
  79, 100, 250, 0, //76 - 79
  82, 200, 250, 0, //80 - 82
  84, 250, 250, 0, //83 - 84
  87, 250, 175, 0, //85 - 87
  89, 250, 100, 0, //88 - 89
  92, 250, 75, 0, //90 - 92
  94, 250, 50, 0, //93 - 94
  100, 250, 0, 0 //95 - 100
};
  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }

  if (invertgray) {
    int a = (255-(temp_colors[j+1] + temp_colors[j+2] + temp_colors[j+3])/3)%255;
    
    return tft.color565(byte(a), byte(a), byte(a));
  }

  return tft.color565(temp_colors[j+1],temp_colors[j+2],temp_colors[j+3]);
}

void fcnDrawHeader(time_t t) {
  //draw header    
  String st = "";
  uint16_t x = 0,y=0;
  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(4);
  tft.setTextDatum(TL_DATUM);
  st = dateify(t,"dow mm/dd");
  tft.drawString(st,x,y);
  x += tft.textWidth(st)+10;
  
  tft.setTextFont(1);
  fcnPrintTxtHeatingCooling(x,3);
  st = "1 2 3 "; //placeholder to find new X position
  x += tft.textWidth(st)+4;
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background

  y=4;
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);

  st = "LOCAL";
  //draw battery pcnt
  byte tind = find_sensor_name("Outside",61);
  if (tind<255)       st += (String) ((int) Sensors[tind].snsValue) + "%";
  
  if (I.localWeather<255) tft.drawString(st,x,y);

  x += tft.textWidth(st)+4;

//  y+=tft.fontHeight(2)+2;    
  if (I.isFlagged) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR); //without second arg it is transparent background
    tft.drawString("FLAG",x,y);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  }
  x += tft.textWidth("FLAG")+2;

//add heat and AC icons to right 
  if ((I.isHeat&1)==1) {
    st="/HVAC/flame.bmp";
    drawBmp(st.c_str(),tft.width()-35,0);
  }
  if ((I.isAC&1)==1) {
    st="/HVAC/ac30.bmp";
    drawBmp(st.c_str(),tft.width()-70,0);
  }
   
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  

  tft.setTextFont(2);

  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, FG_COLOR);

}

void fcnDrawScreen() {
  time_t t = now();

#ifdef _DEBUG
Serial.println("main: main draw pathway reached");
#endif
  if ((I.redraw > 0 && I.isFlagged==I.wasFlagged) || t-I.lastDraw < 30) return; //not time to redraw and flag status has not changed or screen was just drawn

  I.wasFlagged = I.isFlagged; //change was status to is status because we are drawing now

  I.localWeather = 255; //set to 255 as false, or to snsnum to use outdoor temps    
  I.localWeather=find_sensor_name("Outside", 4);
  if (I.localWeather<255) {
    if (t-Sensors[I.localWeather].timeRead>60*30) I.localWeather = 255; //localweather is only useful if <30 minutes old 
  }

  tft.clear(BG_COLOR);
  I.lastDraw = t;
  //tft.fillScreen(BG_COLOR);            // Clear screen, this also works
  fcnDrawHeader(t);


  if (I.isFlagged) {
    I.redraw = SECSCREEN; //set to screen time for multiple screens... here redraw only refers to the alert area 

    I.ScreenNum = ((I.ScreenNum+1)%NUMSCREEN);

  } else {
    I.redraw = 120; //some arbitrarily large number relative to byte... this will reset every minute on the minute anyway
    I.ScreenNum = 0;
  }
  fcnDrawWeather(t);


  return;
  
}

uint32_t set_color(byte r, byte g, byte b) {
  return tft.color565(r,g, b);
}

void drawBox(String roomname, int X, int Y, byte boxsize_x,byte boxsize_y) {
  /*uint8_t box_nl_border[3] = {0,125,0};
  uint8_t box_nl_fill[3] = {120,255,120};
  uint8_t box_high_border[3] = {150,20,20}; //r,g,b
  uint8_t box_high_fill[3] = {255,100,100};
  uint8_t box_low_border[3] = {20,20,150};
  uint8_t box_low_fill[3] = {150,150,255};
  uint8_t box_dry_border[3] = {65,45,20};
  uint8_t box_dry_fill[3] = {250,170,100};
  uint8_t box_wet_border[3] = {0,0,255};
  uint8_t box_wet_fill[3] = {0,190,255};
  */
  
  uint32_t box_border = set_color(0,125,0);
  uint32_t box_fill = set_color(120,255,120);
  uint32_t text_color = set_color(0,0,0);

  String box_text = "";
  char tempbuf[14];

  byte isHigh = 255,isLow=255;
  byte FNTSZ=2;
  tft.setTextFont(FNTSZ);

  //get sensor vals 1, 4, 3, 70, 61... if any are flagged then set box color
  //temperature
  isHigh = 255;
  isLow = 255;

  find_limit_sensortypes(roomname,1,&isHigh,&isLow);
  if (isLow != 255) {
    box_text += (String) ((int) Sensors[isLow].snsValue) + "F(LOW)|";
    box_border = set_color(20,20,150);
    box_fill = set_color(150,150,255);
    text_color = set_color(255-150,255-150,255-255);
  }
  if (isHigh != 255) {
    box_text += (String) ((int) Sensors[isHigh].snsValue) + "F(HIGH)|";
    box_border = set_color(150,20,20);
    box_fill = set_color(255,100,100);
    text_color = set_color(255-255,255-100,255-100);
  }

  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,4,&isHigh,&isLow);
  if (isLow != 255) {
    box_text += (String) ((int) Sensors[isLow].snsValue) + "F(LOW)|";
    box_border = set_color(20,20,150);
    box_fill = set_color(150,150,255);
    text_color = set_color(255-150,255-150,255-255);
  }
  if (isHigh != 255) {
    box_text += (String) ((int) Sensors[isHigh].snsValue) + "F(HIGH)|";
    box_border = set_color(150,20,20);
    box_fill = set_color(255,100,100);
    text_color = set_color(255-255,255-100,255-100);
  }

  //soil
  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,3,&isHigh,&isLow);    
  if (isHigh != 255) {
    box_text += "DRY|";
    box_border = set_color(65,45,20);
    box_fill = set_color(250,170,100);
    text_color = set_color(255-250,255-170,255-100);
  } 

  //leak
  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,70,&isHigh,&isLow);    
  if (isHigh != 255) { //leak is a 1, no leak is a 0
    box_text += "LEAK|";
    box_border = set_color(0,0,255);
    box_fill = set_color(0,190,255);
    text_color = set_color(255-0,255-190,255-255);
  }
 

  //bat
  isHigh = 255;
  isLow = 255;
  find_limit_sensortypes(roomname,61,&isHigh,&isLow);    
  if (isLow != 255) {
    box_text += (String) ((int) Sensors[isLow].snsValue) + "%(LOW)|";
    box_border = set_color(20,20,150);
    box_fill = set_color(150,150,255);
    text_color = set_color(255-150,255-150,255-255);
  }


  //draw  box
  tft.fillRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_fill);
  tft.drawRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_border);
  tft.setTextColor(0,box_fill);
  
  Y+=  2;

  snprintf(tempbuf,13,"%s",roomname.c_str());
  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X+boxsize_x/2,Y+tft.fontHeight(FNTSZ)/2);

  Y+=3+tft.fontHeight(FNTSZ);
  
  tft.setTextColor(text_color,box_fill);
      
  //print each line to the |, then print on the next line
  FNTSZ=1;
  int strOffset = -1;
  String tempstr;
  while (box_text.length()>1) {
    strOffset = box_text.indexOf("|", 0);
    if (strOffset == -1) { //did not find the break point
      tempstr = box_text;
      box_text = "";
    } else {
      tempstr = box_text.substring(0, strOffset);
      box_text.remove(0, strOffset + 1); //include the trailing "|"
    }

    if (tempstr.length()>1) {
      fcnPrintTxtCenter(tempstr,FNTSZ, X+boxsize_x/2,Y+tft.fontHeight(FNTSZ)/2);
      Y+=2+tft.fontHeight(FNTSZ);
    }
  } 

  
}

void fcnDrawSensors(int Y) {
  /*
  need to show:
  outside r1/c1
  attic r1/c2
  upstairs hall r1/c3
  Upstairs BR r2/c1
  FamRm r2/c2
  DR r2/c3
  LR r3/c1
  Office r3/c2
  Den r3/c3
*/

   
  int X = 0;
  
  /* don't do this anymore...
//print time at top
  tft.setTextColor(FG_COLOR,BG_COLOR);
  byte FNTSZ=4;

  X = 3*tft.width()/4;
  snprintf(tempbuf,6,"%s",dateify(now(),"hh:nn"));
  tft.setTextFont(FNTSZ);

  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,tft.fontHeight(FNTSZ)/2);
  */

  byte boxsize_x=60,boxsize_y=50;
  String roomname;

  X = 0;
 
  byte rows = 3;
  byte cols = 4;

  for (byte r=0;r<rows;r++) {
    for (byte c=0;c<cols;c++) {
      switch ((uint16_t) r<<8 | c) {
        case 0:
          roomname = "Outside";
          break;
        case 0<<8 | 1:
          roomname = "Attic";
          break;
        case 0<<8 | 2:
          roomname = "UpHall";
          break;
        case 0<<8 | 3:
          roomname = "MastBR";
          break;
        case 1<<8 | 0:
          roomname = "FamRm";
          break;
        case 1<<8 | 1:
          roomname = "FRBoP";
          break;
        case 1<<8 | 2:
          roomname = "Dining";
          break;
        case 1<<8 | 3:
          roomname = "LivRm";
          break;
        case 2<<8 | 0:
          roomname = "Office";
          break;
        case 2<<8 | 1:
          roomname = "Den";
          break;
        case 2<<8 | 2:
          roomname = "Garage";
          break;
        case 2<<8 | 3:
          roomname = "Bmnt";
          break;
      }


      drawBox(roomname,  X,  Y,boxsize_x,boxsize_y );
        

      X=X+boxsize_x;
    }
    Y+=boxsize_y;
    X=0;
  }
    tft.setTextColor(FG_COLOR,BG_COLOR);

}


void fcnPressureTxt(char* tempPres, uint32_t* fg, uint32_t* bg) {
  // print air pressure
  double tempval = find_sensor_name("Outside", 13); //bme pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 9); //bmp pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 19); //bme680 pressure

  if (tempval !=255) {
    tempval = Sensors[(byte) tempval].snsValue;
    if (tempval>LAST_BAR+.5) {
      snprintf(tempPres,10,"%dhPa^",(int) tempval);
      *fg=TFT_RED;
      *bg=BG_COLOR;
    } else {
      if (tempval<LAST_BAR-.5) {
        snprintf(tempPres,10,"%dhPa_",(int) tempval);
        *fg=TFT_BLUE;
        *bg=BG_COLOR;
      } else {
        snprintf(tempPres,10,"%dhPa~",(int) tempval);
        *fg=FG_COLOR;
        *bg=BG_COLOR;
      }
    }

    if (tempval>=1022) {
      *fg=TFT_RED;
      *bg=TFT_WHITE;
    }

    if (tempval<=1000) {
      *fg=TFT_BLUE;
      *bg=TFT_WHITE;
    }

  }
  return;
}

void fcnPredictionTxt(char* tempPred, uint32_t* fg, uint32_t* bg) {

  double tempval = find_sensor_name("Outside",12);
  if (tempval!=255) tempval = (int) Sensors[(byte) tempval].snsValue;
  else tempval = 0; //currently holds predicted weather

  if ((int) tempval!=0) {
    
    switch ((int) tempval) {
      case 3:
        snprintf(tempPred,10,"GALE");
        *fg=TFT_RED;*bg=TFT_BLACK;
        break;
      case 2:
        snprintf(tempPred,10,"WIND");
        *fg=TFT_RED;*bg=BG_COLOR;
        break;
      case 1:
        snprintf(tempPred,10,"POOR");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case 0:
        snprintf(tempPred,10,"");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case -1:
        snprintf(tempPred,10,"RAIN");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case -2:
        snprintf(tempPred,10,"RAIN");
        *fg=TFT_BLUE;*bg=BG_COLOR;

        break;
      case -3:
        snprintf(tempPred,10,"STRM");
        *fg=TFT_BLACK;*bg=TFT_CYAN;

        break;
      case -4:
        snprintf(tempPred,10,"STRM");
        *fg=TFT_YELLOW;*bg=TFT_CYAN;
        break;
      case -5:
        snprintf(tempPred,10,"STRM");
        *fg=TFT_RED;*bg=TFT_CYAN;
        break;
      case -6:
        snprintf(tempPred,10,"GALE");
        *fg=TFT_YELLOW;*bg=TFT_BLUE;
        break;
      case -7:
        snprintf(tempPred,10,"TSTRM");
        *fg=TFT_RED;*bg=TFT_BLUE;
        break;
      case -8:
        snprintf(tempPred,10,"BOMB");
        *fg=TFT_RED;*bg=TFT_BLUE;
        break;
    }
  } 

  return;
}

void fcnDrawWeather(time_t t) {

int X=0,Y = 30,Z=0; //header ends at 30
int i=0,j=0;
char tempbuf[32];
uint8_t FNTSZ = 0;

byte deltaY = 0;
byte section_spacer = 1;

#ifdef _DEBUG
Serial.println("main: fcndrawweather reached");
#endif


//draw icon for today
int iconID = ID2Icon(WeatherData.getDailyWeatherID(0));
if (t > WeatherData.sunrise  && t< WeatherData.sunset) snprintf(tempbuf,29,"/BMP180x180day/%d.bmp",iconID);
else snprintf(tempbuf,29,"/BMP180x180night/%d.bmp",iconID);
drawBmp(tempbuf,0,Y);

//add info below icon
FNTSZ=0;
if (WeatherData.getPoP() > 50) {//greater than 50% cumulative precip prob in next 24 h
  uint32_t Next_Precip = WeatherData.nextPrecip();
  uint32_t nextrain = WeatherData.nextRain();
  uint8_t rain =  WeatherData.getRain();
  uint32_t nextsnow = WeatherData.nextSnow();
  uint8_t snow =  WeatherData.getSnow() +   WeatherData.getIce();
  if (nextsnow < t + 86400 && snow > 0) {
      tft.setTextColor(TFT_RED,TFT_WHITE);
      snprintf(tempbuf,30,"Snow@%s %.1f\"",dateify(nextsnow,"hh"),snow);
      fcnPrintTxtCenter(tempbuf,FNTSZ,60,180-7);    
  } else {
    if (rain>0 && nextrain< t + 86400 ) {
      tft.setTextColor(TFT_YELLOW,TFT_BLUE);
      snprintf(tempbuf,30,"Rain@%s",dateify(Next_Precip,"hh:nn"));
      if (FNTSZ==0) deltaY = 8;
      else deltaY = tft.fontHeight(FNTSZ);
    }
  }
  fcnPrintTxtCenter(tempbuf,FNTSZ,60,180-deltaY);
}
tft.setTextColor(FG_COLOR,BG_COLOR);


//add info to side of icon  
Z = 10;
X=180+(tft.width()-180)/2; //middle of area on side of icon

tft.setTextFont(FNTSZ);
tft.setTextColor(FG_COLOR,BG_COLOR);

// draw current temp
  FNTSZ = 8;
//  if (tft.fontHeight(FNTSZ) > 120-Z-section_spacer-tft.fontHeight(FNTSZ)) FNTSZ = 4; //check if the number can fit!

  tft.setTextFont(FNTSZ);
    if (FNTSZ==0) deltaY = 8;
    else deltaY = tft.fontHeight(FNTSZ);

int temp0;

  if (I.localWeather<255) temp0 = Sensors[I.localWeather].snsValue;
  else temp0 = WeatherData.getTemperature(t);
  
  fcnPrintTxtColor(temp0,FNTSZ,X,Y+Z+deltaY/2);
  
  Z=Z+tft.fontHeight( FNTSZ)+section_spacer;

//print today max / min
  FNTSZ = 4;  
  if (FNTSZ==0) deltaY = 8;
  else deltaY = tft.fontHeight(FNTSZ);

  int8_t tempMm[2];
  WeatherData.getDailyTemp(0,tempMm);
  if (tempMm[0] == -125) tempMm[0] = WeatherData.getTemperature(t);
  if (tempMm[0]<temp0) tempMm[0]=temp0;

  if (tempMm[1]>temp0) tempMm[1]=temp0;
  
  tft.setTextFont(FNTSZ);
  fcnPrintTxtColor2(tempMm[0],tempMm[1],FNTSZ,X,Y+Z+deltaY/2);

  tft.setTextColor(FG_COLOR,BG_COLOR);

  Z=Z+tft.fontHeight(FNTSZ)+section_spacer;

//print pressure info
  uint32_t fg,bg;
  FNTSZ = 4;
  tempbuf[0]=0;
  fcnPredictionTxt(tempbuf,&fg,&bg);
  tft.setTextColor(fg,bg);
  if (tempbuf[0]==0) {//no gale
    FNTSZ = 4;
  } else {
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);
    Z=Z+tft.fontHeight(FNTSZ)+section_spacer;
    FNTSZ=2;
  }

  deltaY = tft.fontHeight(FNTSZ);

  tft.setTextFont(FNTSZ);  
  tempbuf[0]=0;
  fcnPressureTxt(tempbuf,&fg,&bg);
  tft.setTextColor(fg,bg);
  fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+deltaY/2);
  Z=Z+tft.fontHeight(FNTSZ)+section_spacer;

  FNTSZ = 4;
  tft.setTextFont(FNTSZ);
  if (WeatherData.flag_snow) snprintf(tempbuf,20,"Snow: %u%%",WeatherData.getDailyPoP(0));
  else {
    if (WeatherData.flag_ice) snprintf(tempbuf,20,"Ice: %u%%",WeatherData.getDailyPoP(0));
    else snprintf(tempbuf,20,"PoP: %u%%",WeatherData.getDailyPoP(0));
  }

    if (WeatherData.getDailyPoP(0)>50)  tft.setTextColor(TFT_RED,TFT_BLUE);
    else tft.setTextColor(FG_COLOR,BG_COLOR);
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+deltaY/2);
  Z=Z+tft.fontHeight(FNTSZ)+section_spacer;

//end current wthr


//if isflagged, then show rooms with flags. otherwise, show daily weather
  if (I.isFlagged && I.ScreenNum==1) {
    fcnDrawSensors(30+120+section_spacer);
    return;
  }
  
  //otherwise draw weather icons, starting with hourly 
  Y = 30+180+10;
  tft.setCursor(0,Y);

  FNTSZ = 4;
  tft.setTextFont(FNTSZ);
  
  tft.setTextColor(FG_COLOR,BG_COLOR);

  for (i=1;i<7;i++) { //draw 6 icons, with HourlyInterval hours between icons. Note that index 1 is  1 hour from now
    if (i*HourlyInterval>23) break; //safety check if user asked for too large an interval (probably 4 or more)... cannot display past 24 at present
    
    Z=0;
    X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
    iconID = ID2Icon(WeatherData.getWeatherID(t+i*HourlyInterval*60*60));
    #ifdef _DEBUG
      Serial.printf("drawing icon #%i, which is id %u\n",i,iconID);
    #endif
    if (t+i*HourlyInterval*3600 > WeatherData.sunrise  && t+i*HourlyInterval*3600< WeatherData.sunset) snprintf(tempbuf,29,"/BMP30x30day/%d.bmp",iconID);
    else snprintf(tempbuf,29,"/BMP30x30night/%d.bmp",iconID);
    drawBmp(tempbuf,X,Y);
    Z+=30;

     tft.setTextColor(FG_COLOR,BG_COLOR);
    X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

    FNTSZ=1;
    snprintf(tempbuf,6,"%s:00",dateify(t+i*HourlyInterval*60*60,"hh"));
    tft.setTextFont(FNTSZ); //small font
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+tft.fontHeight(FNTSZ)/2);

    Z+=tft.fontHeight(FNTSZ)+section_spacer;

    FNTSZ=4;
    tft.setTextFont(FNTSZ); //med font
    fcnPrintTxtColor(WeatherData.getTemperature(t + i*HourlyInterval*3600),FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    Z+=tft.fontHeight(FNTSZ);
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);
  Y+=Z;


//now draw daily weather
Y+=5;

  tft.setCursor(0,Y);
 
  for (i=1;i<6;i++) {
    Z=0;
    X = (i-1)*(tft.width()/5) + ((tft.width()/5)-60)/2; 
    iconID = ID2Icon(WeatherData.getDailyWeatherID(i,true));
    snprintf(tempbuf,29,"/BMP60x60day/%d.bmp",iconID); //alway print day icons for future days

    drawBmp(tempbuf,X,Y);
    Z+=60;
    
    X = (i-1)*(tft.width()/5) + ((tft.width()/5))/2; 
    FNTSZ=2;
    tft.setTextFont(FNTSZ); 
    snprintf(tempbuf,31,"%s",dateify(now()+i*60*60*24,"DOW"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+tft.fontHeight(FNTSZ)/2);
    
    Z+=tft.fontHeight(FNTSZ)+section_spacer;

    tft.setTextFont(FNTSZ);
    WeatherData.getDailyTemp(i,tempMm);
    
    fcnPrintTxtColor2(tempMm[0],tempMm[1],FNTSZ,X,Y+Z+tft.fontHeight(FNTSZ)/2); 
    
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
    
    Z+=tft.fontHeight(FNTSZ)+section_spacer;
    
    j+=45;
  }

  Y+=Z;

 tft.setCursor(0,Y);
 
//print time at bottom
  tft.setTextColor(FG_COLOR,BG_COLOR);
  FNTSZ=8;
  Y = tft.height() - tft.fontHeight(FNTSZ) - 2;
  X = tft.width()/2;
  snprintf(tempbuf,31,"%s",dateify(t,"hh:nn"));
  tft.setTextFont(FNTSZ);

  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+tft.fontHeight(FNTSZ)/2);

  
}


void loop() {
    // #ifdef _DEBUG
    //   Serial.printf("Loop start: Time is: %s\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"));
    // #endif
  time_t t = now(); // store the current time in time variable t


  ArduinoOTA.handle();
  server.handleClient();
  updateTime(1,0); //just try once
  WeatherData.updateWeather();


    
  
  if (OldTime[1] != minute()) {

    OldTime[1] = minute();
    //do stuff every minute

    I.isFlagged = false;
    if (countFlagged(-1,B00000111,B00000011,0)>0) I.isFlagged = true; //-1 means only flag for soil or temp or battery
    
    checkHeat(); //this updates I.isheat and I.isac

    I.isSoilDry = false;
    if (countFlagged(3,B00000111,B00000011,(t>3600)?t-3600:0)>0) I.isSoilDry = true;

    I.isHot = false;
    if (countFlagged(-2,B00100111,B00100011,(t>3600)?t-3600:0)>0) I.isHot = true;

    I.isCold = false;
    if (countFlagged(-2,B00100111,B00000011,(t>3600)?t-3600:0)>0) I.isCold = true;

    I.isLeak = false;
    if (countFlagged(70,B00000001,B00000001,(t>3600)?t-3600:0)>0) I.isLeak = true;


    I.redraw = 0;

    if(WiFi.status()== WL_CONNECTED){
      I.wifi=10;
    } else {
      I.wifi=0;
    }
    
  }

 

  if (OldTime[2] != hour()) {
 
    OldTime[2] = hour(); 

    //expire any measurements that are older than N hours.
    initSensor(OLDESTSENSORHR*60);

      
  }

  if (OldTime[3] != weekday()) {
    if (ALIVESINCE-t > 604800) ESP.restart(); //reset every week
    
    OldTime[3] = weekday();
  }


  //note that second events are last intentionally
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second    

    if (I.wifi>0) I.wifi--;
    if (I.redraw>0) I.redraw--;
    fcnDrawScreen();
  }


}

