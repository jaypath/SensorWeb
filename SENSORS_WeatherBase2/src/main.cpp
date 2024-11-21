

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
//extern uint8_t OldTime[4];
extern WeatherInfo WeatherData;
extern SensorVal Sensors[SENSORNUM];
extern uint32_t LAST_WEB_REQUEST;
extern WeatherInfo WeatherData;
extern double LAST_BAR;

uint32_t FONTHEIGHT = 0;


//time
uint8_t OldTime[4] = {0,0,0,0}; //s,m,h,d


//fuction declarations

uint16_t set_color(byte r, byte g, byte b);
uint32_t setFont(uint8_t FNTSZ);
int ID2Icon(int); //provide a weather ID, obtain an icon ID
void drawBmp(const char*, int16_t, int16_t, uint16_t alpha = TRANSPARENT_COLOR);
void fcnDrawHeader(time_t t);
void fcnDrawClock(time_t t);
void fcnDrawScreen(time_t t);
void fcnDrawWeather(time_t t);
void fcnDrawSensors(int Y);
void fncDrawCurrentCondition(time_t t);
void fcnPrintTxtHeatingCooling(int x,int y);
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ,int x=-1,int y=-1,bool autocontrast = false);
void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1,bool autocontrast=false);
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1);
void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg);
void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg);
void drawBox(String roomname, int X, int Y, byte boxsize_x,byte boxsize_y);
uint16_t temp2color(int temp, bool invertgray = false);




void setup()
{
  WEBHTML.reserve(7000); 
  #ifdef _DEBUG
    Serial.begin(115200);

  #endif


  #ifdef _DEBUG
    Serial.print("SPI begin and screen begin... ");
  #endif

  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI
tft.init();

  #ifdef _DEBUG
    Serial.println("ok ");
  #endif

  // Setting display to landscape
  tft.setRotation(2);


  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM); 
  
  tft.printf("Running setup\n");

      //init globals
    initSensor(-256);

  tft.printf("SD Card mounting...");

  if(!SD.begin(41)){  //CS=41
    tft.setTextColor(TFT_RED);
    tft.println("SD Mount Failed");
    tft.setTextColor(FG_COLOR);
    #ifdef _DEBUG
      Serial.println("SD mount failed... ");
    #endif

  } 
  else {
    #ifdef _DEBUG
      Serial.println("SD mounted... ");
    #endif

    tft.println("OK.\n");
     tft.print("Loading historical data from SD... ");
     bool sdread = readSensorsSD("/Data/SensorBackup.dat");
     if (sdread) {
      tft.setTextColor(TFT_GREEN);
      tft.println("OK\n");
      tft.setTextColor(FG_COLOR);
     } else {
      tft.setTextColor(TFT_RED);
      tft.println("FAIL\n");
      tft.setTextColor(FG_COLOR);
     }

    #ifdef _DEBUG
      Serial.print("Sensor data loaded? ");
      Serial.println((sdread==true)?"yes":"no");
    #endif

  }
    

  I.ScreenNum = 0;
  I.isFlagged = false;
  I.wasFlagged = false;

  WeatherData.lat = LAT;
  WeatherData.lon = LON;
  
    
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
  tft.println("Connecting  Wifi...\n");
  if (WiFi.status() != WL_CONNECTED)
  {
    tft.print("Wifi failed after ");
    tft.print(retries);
    tft.println(" attempts. Halting.\n");
    while(true);
  } else {
    tft.printf("Wifi connected after %u attempts.\nWifi IP is",retries );
    tft.setTextColor(TFT_GREEN);    
    tft.printf("%s\n", WiFi.localIP().toString().c_str());  
    tft.setTextColor(FG_COLOR);
  }

    #ifdef _DEBUG
      Serial.println("Connected!");
      Serial.println(WiFi.localIP());
    #endif


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
    #ifdef _DEBUG
      Serial.print("Beginning OTA... ");
    #endif

  ArduinoOTA.begin();

    tft.setTextColor(TFT_GREEN);
    tft.printf(" OTA OK.\n");
    tft.setTextColor(FG_COLOR);

    #ifdef _DEBUG
      Serial.println("connected!");
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
    server.on("/RETRIEVEDATA",handleRETRIEVEDATA);

    server.onNotFound(handleNotFound);
    #ifdef _DEBUG
      Serial.print("Starting server... ");
    #endif
    tft.printf("Starting server...");

    server.begin();
    tft.setTextColor(TFT_GREEN);
    tft.printf(" SERVER OK.\n");
    tft.setTextColor(FG_COLOR);

    #ifdef _DEBUG
      Serial.println("connected!");
    #endif

    tft.println("Set up TimeClient...");

//    setSyncInterval(600); //set NTP interval for sync in sec
    timeClient.begin(); //time is in UTC
    updateTime(100,250); //check if DST and set time to EST or EDT
    
    tft.setTextColor(TFT_GREEN);
    tft.printf(" TimeClient OK.\n");
    tft.setTextColor(FG_COLOR);

    tft.println("Starting...");


    ALIVESINCE = now();

    #ifdef _DEBUG    
      Serial.println("Done");
      delay(3000);
    #endif
    tft.setTextColor(TFT_GREEN);
    tft.printf(" Done with setup.\n");
    tft.setTextColor(FG_COLOR);

    tft.println("Check weather, then start.");
    
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


void fcnPrintTxtCenter(String msg,byte FNTSZ, int x, int y) {
int X,Y;
uint32_t FH = setFont(FNTSZ);

if (x>=0 && y>=0) {
  X = x-tft.textWidth(msg)/2;
  if (X<0) X=0;
  Y = y-FH/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}

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
  uint32_t FH = setFont(FNTSZ);


  uint16_t c_heat = tft.color565(255,180,0); //TFT_ORANGE;
  uint16_t c_ac = tft.color565(0,0,(byte) 255*.7);
  uint16_t c_fan = TFT_GREEN; //tft.color565(135,206,235); //TFT_SKYBLUE;135 206 235
  uint16_t c_errorfg = TFT_BLACK; 
  uint16_t c_errorbg = tft.color565(255,0,0); 
  uint16_t c_FG=TFT_DARKGRAY, c_BG=BG_COLOR;
  
  
  int x = X;
  tft.setTextDatum(TL_DATUM);
  for (byte j=1;j<7;j++) {
    c_FG=TFT_DARKGRAY;
    c_BG=BG_COLOR;
  
    if (j==4) {
      x=X;
      Y+= FH+2; 
    }

    if (bitRead(I.isHeat,j) && bitRead(I.isAC,j)) {
      c_FG = c_errorfg;
      c_BG = c_errorbg;
    } else {
      if (bitRead(I.isFan,j)) {
          c_BG = c_fan; 
      }
      if (bitRead(I.isHeat,j)) {
        c_FG = c_heat;        
      }
      if (bitRead(I.isAC,j)) {
        c_FG = c_ac; 
        if (c_BG != c_fan) c_BG = c_errorbg;
      }
    }
    switch (j) {
      case 1:
        temp = "OF ";
        break;
      case 2:
        temp = "MB ";
        break;
      case 3:
        temp = "LR ";
        break;
      case 4:
        temp = "BR ";
        break;
      case 5:
        temp = "FR ";
        break;
      case 6:
        temp = "DN ";
        break;
    }

    tft.setTextColor(c_FG,c_BG);        
    if (FNTSZ==0)    x +=tft.drawString(temp,x,Y,&fonts::TomThumb);
    if (FNTSZ==1)    x +=tft.drawString(temp,x,Y,&fonts::Font0);
  }
  
  temp = "XX XX XX "; //placeholder to find new X,Y location
  tft.setCursor(X + tft.textWidth(temp) ,Y-FH);
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

uint32_t FH = setFont(FNTSZ);
if (x>=0 && y>=0) {
  X = x-tft.textWidth(temp)/2;
  if (X<0) X=0;
  Y = y-FH/2;
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
uint32_t FH = setFont(FNTSZ);

if (x>=0 && y>=0) {
  X = x-tft.textWidth(temp)/2;
  if (X<0) X=0;
  Y = y-FH/2;
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
  //redraw header every  [flagged change, isheat, iscool] changed  or every new hour
  if (((I.isFlagged!=I.wasFlagged || I.isAC != I.wasAC || I.isFan!=I.wasFan || I.isHeat != I.wasHeat) ) || I.lastHeader+3599<t)     I.lastHeader = t;
  else return;

  tft.fillRect(0,0,tft.width(),I.HEADER_Y,BG_COLOR); //clear the header area

  //draw header    
  String st = "";
  uint16_t x = 0,y=0;
  uint32_t FH = setFont(4);
  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  
  tft.setTextDatum(TL_DATUM);
  st = dateify(t,"dow mm/dd");
  tft.drawString(st,x,y);
  x += tft.textWidth(st)+10;
  

  y=4;
  tft.setTextDatum(TL_DATUM);
  FH = setFont(2);


  if (I.isFlagged) {
    tft.setTextFont(2);
    tft.setTextColor(tft.color565(255,0,0),BG_COLOR); //without second arg it is transparent background
    tft.drawString("FLAG",x,y);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  }
  x += tft.textWidth("FLAG")+10;

  setFont(0);
  fcnPrintTxtHeatingCooling(x,5);
  st = "XX XX XX "; //placeholder to find new X position
  x += tft.textWidth(st)+4;
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background


//add heat and AC icons to right 
  if ((I.isHeat&1)==1) {
    st="/HVAC/flame.bmp";
    drawBmp(st.c_str(),tft.width()-30,0);
  }
  if ((I.isAC&1)==1) {
    st="/HVAC/ac30.bmp";
    drawBmp(st.c_str(),tft.width()-65,0);
  }
   
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  

  FH = setFont(2);

  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, FG_COLOR);

  return;

}

void fcnDrawScreen(time_t t) {

  fcnDrawClock(t); 
  fcnDrawHeader(t);
  fncDrawCurrentCondition(t);
  fcnDrawWeather(t);
    I.wasFlagged = I.isFlagged;
    I.wasAC = I.isAC;
    I.wasFan = I.isFan;
    I.wasHeat = I.isHeat;


  return;
  
}

uint32_t setFont(uint8_t FNTSZ) {
  switch (FNTSZ) {
    case 0:
      tft.setFont(&fonts::TomThumb);      
    break;
    case 1:
      tft.setFont(&fonts::Font0);
    break;
    case 2:
      tft.setFont(&fonts::Font2);
    break;
    case 3:
      tft.setFont(&fonts::FreeSans9pt7b);
    case 4:
      tft.setFont(&fonts::Font4);
    break;
    case 5:
      tft.setFont(&fonts::FreeSans24pt7b);
    break;
    case 6:
      tft.setFont(&fonts::Font6);
    break;
    case 7:
      tft.setFont(&fonts::Font7);
    break;    
    case 8:
      tft.setFont(&fonts::Font8);
    break;
  }

  return tft.fontHeight();

}

uint16_t set_color(byte r, byte g, byte b) {
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
  
  uint16_t box_border = set_color(200,175,200);
  uint16_t box_fill = set_color(75,50,75);
  uint16_t text_color = set_color(255,225,255);

  String box_text = "";
  char tempbuf[14];

  byte isHigh = 255,isLow=255;

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
//  tft.fillRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_fill);
  //tft.drawRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_border);
  tft.fillRect(X,Y,boxsize_x-2,boxsize_y-2,box_fill);
  tft.drawRect(X,Y,boxsize_x-2,boxsize_y-2,box_border);
  tft.setTextColor(0,box_fill);
  
  Y+=  2;
  byte FNTSZ = 2;
  uint32_t FH=setFont(FNTSZ);
   
  snprintf(tempbuf,13,"%s",roomname.c_str());
  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X+boxsize_x/2,Y+FH/2);
  Y+=2+FH;

  tft.setTextColor(text_color,box_fill);
      
  //print each line to the |, then print on the next line
  FNTSZ = 1;
  FH = setFont(0);
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
      fcnPrintTxtCenter(tempstr,FNTSZ, X+boxsize_x/2,Y+FH/2);
      Y+=2+FH;
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

  byte boxsize_x=50,boxsize_y=50;
  byte gapx = 4;
  byte gapy = 2;
  String roomname;

  X = 0;
 
  byte rows = 2;
  byte cols = 6;

  for (byte r=0;r<rows;r++) {
    for (byte c=0;c<cols;c++) {
      switch ((uint16_t) r<<8 | c) { //this puts the row into the first 8 bits and column into the next 8, so can check both row and column in 1 case
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
        case 0<<8 | 4:
          roomname = "FamRm";
          break;
        case 0<<8 | 5:
          roomname = "FRBoP";
          break;
        case 1<<8 | 0:
          roomname = "Dining";
          break;
        case 1<<8 | 1:
          roomname = "LivRm";
          break;
        case 1<<8 | 2:
          roomname = "Office";
          break;
        case 1<<8 | 3:
          roomname = "Den";
          break;
        case 1<<8 | 4:
          roomname = "Garage";
          break;
        case 1<<8 | 5:
          roomname = "Bmnt";
          break;
      }


      drawBox(roomname,  X,  Y,boxsize_x,boxsize_y);
        

      X=X+boxsize_x+gapx;
    }
    Y+=boxsize_y + gapy;
    X=0;
  }
    tft.setTextColor(FG_COLOR,BG_COLOR);

}


void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg) {
  // print air pressure
  double tempval = find_sensor_name("Outside", 13); //bme pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 9); //bmp pressure
  if (tempval==255) tempval = find_sensor_name("Outside", 19); //bme680 pressure

  if (tempval !=255) {
    tempval = Sensors[(byte) tempval].snsValue;
    if (tempval>LAST_BAR+.5) {
      snprintf(tempPres,10,"%dhPa^",(int) tempval);
      *fg=tft.color565(255,0,0);
      *bg=BG_COLOR;
    } else {
      if (tempval<LAST_BAR-.5) {
        snprintf(tempPres,10,"%dhPa_",(int) tempval);
        *fg=tft.color565(0,0,255);
        *bg=BG_COLOR;
      } else {
        snprintf(tempPres,10,"%dhPa-",(int) tempval);
        *fg=FG_COLOR;
        *bg=BG_COLOR;
      }
    }

    if (tempval>=1022) {
      *fg=tft.color565(255,0,0);
      *bg=tft.color565(255,255,255);
    }

    if (tempval<=1000) {
      *fg=tft.color565(0,0,255);
      *bg=tft.color565(255,0,0);
    }

  }
  return;
}

void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg) {

  double tempval = find_sensor_name("Outside",12);
  if (tempval!=255) tempval = (int) Sensors[(byte) tempval].snsValue;
  else tempval = 0; //currently holds predicted weather

  if ((int) tempval!=0) {
    
    switch ((int) tempval) {
      case 3:
        snprintf(tempPred,10,"GALE");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,0);
        break;
      case 2:
        snprintf(tempPred,10,"WIND");
        *fg=tft.color565(255,0,0);*bg=BG_COLOR;
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
        *fg=tft.color565(0,0,255);*bg=BG_COLOR;

        break;
      case -3:
        snprintf(tempPred,10,"STRM");
        *fg=tft.color565(255,255,255);
        *bg=tft.color565(0,255,255);

        break;
      case -4:
        snprintf(tempPred,10,"STRM");
        *fg=tft.color565(255,255,0);
        *bg=tft.color565(0,255,255);
        break;
      case -5:
        snprintf(tempPred,10,"STRM");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,255,255);
        break;
      case -6:
        snprintf(tempPred,10,"GALE");
        *fg=tft.color565(255,255,0);
        *bg=tft.color565(0,0,255);
        break;
      case -7:
        snprintf(tempPred,10,"TSTRM");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,255);
        break;
      case -8:
        snprintf(tempPred,10,"BOMB");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,255);
        break;
    }
  } 

  return;
}

void fcnDrawClock(time_t t) {
  if (t == I.lastClock) return;
  bool forcedraw = false;
  int Y = tft.height() - I.CLOCK_Y;
  if (I.lastClock==0) forcedraw=true;
  I.lastClock = t;

  
  //if isflagged, then show rooms with flags
  if (I.isFlagged) {
    if (t>I.lastFlagView+I.flagViewTime) {
      I.lastFlagView = t;
      if (I.ScreenNum==1) {
        I.ScreenNum = 0; //stop showing flags
        forcedraw=true;
      }
      else {
        I.ScreenNum = 1;
        tft.fillRect(0,Y,tft.width(),I.CLOCK_Y,BG_COLOR); //clear the clock area
        fcnDrawSensors(Y);
        return;
      }
    } 
    else if (I.ScreenNum==1) return;
  }
  
  if (I.wasFlagged==true && I.isFlagged == false) {
    //force full redraw
    forcedraw=true;
  }

  //otherwise print time at bottom
  //break into three segments, will draw hour : min : sec
  int16_t X = tft.width()/3;
  uint8_t FNTSZ=6;
  uint32_t FH = setFont(FNTSZ);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  char tempbuf[20];
  

  #ifdef notdef
  //am I drawing the H, m, s or just s?
  if (second(t)==0 || forcedraw==true) { //I am redrawing everything, minute changed
    tft.fillRect(0,Y,tft.width()-X+tft.textWidth(":")/2+1,I.CLOCK_Y,BG_COLOR); //clear the "h:nn:"" clock area
    fcnPrintTxtCenter((String) ":",FNTSZ, X,Y+I.CLOCK_Y/2);      
    fcnPrintTxtCenter((String) ":",FNTSZ, 2*X,Y+I.CLOCK_Y/2);
    //print hour
    snprintf(tempbuf,5,"%s",dateify(t,"h1"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X/2,Y+I.CLOCK_Y/2);
    //print min
    snprintf(tempbuf,5,"%s",dateify(t,"nn"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X + X/2,Y+I.CLOCK_Y/2);
  }

  //now clear the second area
  tft.fillRect(2*X + tft.textWidth(":")/2+1,Y,X - tft.textWidth(":")/2-1,I.CLOCK_Y,BG_COLOR);
  //draw seconds
  snprintf(tempbuf,5,"%s",dateify(t,"ss"));
  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X+X+X/2,Y+I.CLOCK_Y/2);
  #else
    if (second(t)==0 || forcedraw) {
      FH = setFont(8);
      tft.fillRect(0,Y,tft.width(),I.CLOCK_Y,BG_COLOR); //clear the "h:nn"" clock area
      snprintf(tempbuf,19,"%s",dateify(t,"h1:nn"));
      fcnPrintTxtCenter((String) tempbuf,8, tft.width()/2,Y+I.CLOCK_Y/2);
    }
  #endif

  return;

}


void fncDrawCurrentCondition(time_t t) {
  if (I.localWeather==255)     I.localWeather=find_sensor_name("Outside", 4);  
  if (I.localWeather<255 && (int8_t) Sensors[I.localWeather].snsValue!=I.currentTemp && t-Sensors[I.localWeather].timeLogged<180) {
    I.currentTemp=(int8_t) Sensors[I.localWeather].snsValue;
  } else {
      if (t<I.lastCurrentCondition + (I.currentConditionTime)*60 && I.lastCurrentCondition>0) return; //not time to update cc
  }

  I.lastCurrentCondition = t;

  int Y = I.HEADER_Y, Z = 10,   X=180+(tft.width()-180)/2; //middle of area on side of icon
  tft.fillRect(180,Y,tft.width()-180,180,BG_COLOR); //clear the cc area
  int FNTSZ = 8;
  byte section_spacer = 3;
  bool islocal=false;
  String st = "Local";

  I.localWeather=find_sensor_name("Outside", 4); //reset periodically, in case there is a change
  I.currentTemp = WeatherData.getTemperature(t);
  
  if (I.localWeather<255) {
    if (t-Sensors[I.localWeather].timeLogged>60*30) {
      I.localWeather = 255; //localweather is only useful if <30 minutes old 
    } 
    else {
      I.currentTemp = (int8_t) Sensors[I.localWeather].snsValue;
      st+=" @" + (String) dateify(Sensors[I.localWeather].timeRead,"h1:nn");
      byte tind = find_sensor_name("Outside",61);
      if (tind<255)       st += " Bat" + (String) ((int) Sensors[tind].snsValue) + "%";
      islocal=true;
    }
  }

   
  // draw current temp
  uint32_t FH = setFont(FNTSZ);

  
  fcnPrintTxtColor(I.currentTemp,FNTSZ,X,Y+Z+FH/2);

  tft.setTextColor(FG_COLOR,BG_COLOR);
  Z+=FH+section_spacer;

  //print today max / min
    FNTSZ = 4;  
    FH = setFont(FNTSZ);
    int8_t tempMaxmin[2];
    WeatherData.getDailyTemp(0,tempMaxmin);
    if (tempMaxmin[0] == -125) tempMaxmin[0] = WeatherData.getTemperature(t);
    //does local max/min trump reported?
    if (tempMaxmin[0]<I.currentTemp) tempMaxmin[0]=I.currentTemp;
    if (tempMaxmin[1]>I.currentTemp) tempMaxmin[1]=I.currentTemp;
    I.Tmax = tempMaxmin[0];
    I.Tmin = tempMaxmin[1];
    
    fcnPrintTxtColor2(I.Tmax,I.Tmin,FNTSZ,X,Y+Z+FH/2);

    tft.setTextColor(FG_COLOR,BG_COLOR);

    Z=Z+FH+section_spacer;

  //print pressure info
    uint16_t fg,bg;
    FNTSZ = 4;
    FH = setFont(FNTSZ);
    char tempbuf[15]="";
    fcnPredictionTxt(tempbuf,&fg,&bg);
    tft.setTextColor(fg,bg);
    if (tempbuf[0]!=0) {// prediction made
      fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+FH/2);
      Z=Z+FH+section_spacer;
      FNTSZ=2;
    }

    FH = setFont(FNTSZ);
    tempbuf[0]=0;
    fcnPressureTxt(tempbuf,&fg,&bg);
    tft.setTextColor(fg,bg);
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+FH/2);
    Z=Z+FH+section_spacer;

    FNTSZ = 4;
    FH = setFont(FNTSZ);
    if (WeatherData.flag_snow) snprintf(tempbuf,14,"Snow: %u%%",WeatherData.getDailyPoP(0));
    else {
      if (WeatherData.flag_ice) snprintf(tempbuf,14,"Ice: %u%%",WeatherData.getDailyPoP(0));
      else snprintf(tempbuf,14,"PoP: %u%%",WeatherData.getDailyPoP(0));
    }

    if (WeatherData.getDailyPoP(0)>50)  tft.setTextColor(tft.color565(255,0,0),tft.color565(0,0,255));
    else tft.setTextColor(FG_COLOR,BG_COLOR);
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+FH/2);
    Z=Z+FH+section_spacer;

  //end current wthr
  
  if (islocal) {
    tft.setTextColor(FG_COLOR,BG_COLOR);
    fcnPrintTxtCenter(st,1,X,Y+6);      
  }

  return;


}

void fcnDrawWeather(time_t t) {

if ((uint32_t) (t<I.lastWeather+I.weatherTime*60) && I.lastWeather>0) return;
I.lastWeather = t;

int X=0,Y = I.HEADER_Y,Z=0; //header ends at 30

tft.fillRect(0,I.HEADER_Y,180,180,BG_COLOR); //clear the bmp area
tft.fillRect(0,I.HEADER_Y+180,tft.width(),tft.height()-(180+I.HEADER_Y+I.CLOCK_Y),BG_COLOR); //clear the weather area


int i=0,j=0;
char tempbuf[45];
uint8_t FNTSZ = 0;

byte deltaY = setFont(FNTSZ);

byte section_spacer = 3;

//draw icon for NOW
int iconID = ID2Icon(WeatherData.getWeatherID(0));
if (t > WeatherData.sunrise  && t< WeatherData.sunset) snprintf(tempbuf,44,"/BMP180dayG/%d.bmp", iconID);
else snprintf(tempbuf,44,"/BMP180nightG/%d.bmp",iconID);

drawBmp(tempbuf,0,Y);

//add info atop icon
FNTSZ=1;
deltaY = setFont(FNTSZ);
if (WeatherData.getPoP() > 50) {//greater than 50% cumulative precip prob in next 24 h
  uint32_t Next_Precip = WeatherData.nextPrecip();
  uint32_t nextrain = WeatherData.nextRain();
  uint8_t rain =  WeatherData.getRain();
  uint32_t nextsnow = WeatherData.nextSnow();
  uint8_t snow =  WeatherData.getSnow() +   WeatherData.getIce();
  if (nextsnow < t + 86400 && snow > 0) {
      tft.setTextColor(tft.color565(255,0,0),tft.color565(255,0,0));
      snprintf(tempbuf,30,"Snow@%s %.1f\"",dateify(nextsnow,"hh"),snow);
      fcnPrintTxtCenter(tempbuf,FNTSZ,60,180-7);    
  } else {
    if (rain>0 && nextrain< t + 86400 ) {
      tft.setTextColor(tft.color565(255,255,0),tft.color565(0,0,255));
      snprintf(tempbuf,30,"Rain@%s",dateify(Next_Precip,"hh:nn"));
      if (FNTSZ==0) deltaY = 8;
      else deltaY = setFont(FNTSZ);
    }
  }
  fcnPrintTxtCenter(tempbuf,FNTSZ,60,180-deltaY);
}
tft.setTextColor(FG_COLOR,BG_COLOR);


  Y = I.HEADER_Y+180+section_spacer;
  tft.setCursor(0,Y);

  FNTSZ = 4;
  deltaY = setFont(FNTSZ);
  
  tft.setTextColor(FG_COLOR,BG_COLOR);

  for (i=1;i<7;i++) { //draw 6 icons, with I.HourlyInterval hours between icons. Note that index 1 is  1 hour from now
    if (i*I.HourlyInterval>72) break; //safety check if user asked for too large an interval ... cannot display past download limit
    
    Z=0;
    X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
    iconID = ID2Icon(WeatherData.getWeatherID(t+i*I.HourlyInterval*60*60));
    if (t+i*I.HourlyInterval*3600 > WeatherData.sunrise  && t+i*I.HourlyInterval*3600< WeatherData.sunset) snprintf(tempbuf,29,"/BMP30x30day/%d.bmp",iconID);
    else snprintf(tempbuf,29,"/BMP30x30night/%d.bmp",iconID);
    
    drawBmp(tempbuf,X,Y);
    Z+=30;

     tft.setTextColor(FG_COLOR,BG_COLOR);
    X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

    FNTSZ=1;
    deltaY = setFont(FNTSZ);
    snprintf(tempbuf,6,"%s:00",dateify(t+i*I.HourlyInterval*60*60,"hh"));
    tft.setTextFont(FNTSZ); //small font
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+deltaY/2);

    Z+=deltaY+section_spacer;

    FNTSZ=4;
    deltaY = setFont(FNTSZ);
    tft.setTextFont(FNTSZ); //med font
    fcnPrintTxtColor(WeatherData.getTemperature(t + i*I.HourlyInterval*3600),FNTSZ,X,Y+Z+deltaY/2);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    Z+=deltaY+section_spacer;
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);
  Y+=Z;


//now draw daily weather
  tft.setCursor(0,Y);
  int8_t tmm[2];
  for (i=1;i<6;i++) {
    Z=0;
    X = (i-1)*(tft.width()/5) + ((tft.width()/5)-60)/2; 
    iconID = ID2Icon(WeatherData.getDailyWeatherID(i,true));
    snprintf(tempbuf,29,"/BMP60x60day/%d.bmp",iconID); //alway print day icons for future days

    drawBmp(tempbuf,X,Y);
    Z+=60;
    
    X = (i-1)*(tft.width()/5) + ((tft.width()/5))/2; 
    FNTSZ=2;
    deltaY = setFont(FNTSZ);
    snprintf(tempbuf,31,"%s",dateify(now()+i*60*60*24,"DOW"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+deltaY/2);
    
    Z+=deltaY;
    WeatherData.getDailyTemp(i,tmm);
    fcnPrintTxtColor2(tmm[0],tmm[1],FNTSZ,X,Y+Z+deltaY/2); 
    
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
    
    Z+=deltaY+section_spacer;
    
  }

  Y+=Z;

 tft.setCursor(0,Y);
 
  
}


#ifdef _DEBUG
uint16_t TESTRUN = 120;
uint32_t WTHRFAIL = 0;
#endif



void loop() {

  time_t t = now(); // store the current time in time variable t
     #ifdef _DEBUG
       if (I.flagViewTime==0) {
        Serial.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
        tft.clear();
        tft.setCursor(0,0);
        tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
        while(true);
       }
     #endif


  ArduinoOTA.handle();
  #ifdef _DEBUG
       if (I.flagViewTime==0) {
       Serial.printf("Loop OTA.handle: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
        tft.clear();
        tft.setCursor(0,0);
        tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
       while(true);
       }
     #endif
  server.handleClient();
      #ifdef _DEBUG
       if (I.flagViewTime==0) {
       Serial.printf("Loop server.handle: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
        tft.clear();
        tft.setCursor(0,0);
       tft.printf("Loop server.handle: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
       while(true);
       }
     #endif
  updateTime(1,0); //just try once
   
  
  if (OldTime[1] != minute()) {
    WeatherData.updateWeather(3600);

    #ifdef _DEBUG
       if (I.flagViewTime==0) {
       Serial.printf("Loop update weather: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
        tft.clear();
        tft.setCursor(0,0);
        tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
       while(true);
       }
     #endif
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
    
  }

 

  if (OldTime[2] != hour()) {
 
    OldTime[2] = hour(); 

    
    //expire any measurements that are older than N hours.
    initSensor(OLDESTSENSORHR*60);

      //overwrite  sensors to the sd card
    writeSensorsSD("/Data/SensorBackup.dat");

      
  }

  if (OldTime[3] != weekday()) {
    //if ((uint32_t) t-ALIVESINCE > 604800) ESP.restart(); //reset every week
    
    OldTime[3] = weekday();
  }


  //note that second events are last intentionally
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second    

    if (I.wifi>0) I.wifi--;
    fcnDrawScreen(t);
        #ifdef _DEBUG
       if (I.flagViewTime==0) {
       Serial.printf("Loop drawscreen: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
        tft.clear();
        tft.setCursor(0,0);
        tft.printf("Loop start: Time is: %s and a critical failure occurred. This was %u seconds since start.\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"),t-ALIVESINCE);
       while(true);
       }
     #endif
  }

  #ifdef _DEBUG
    if (WeatherData.getTemperature(t+3600)<0 && TESTRUN<3600) {
      Serial.printf("Loop %s: Weather data just failed\n",dateify(t));
      WTHRFAIL = t;
      TESTRUN = 3600;
    }

  #endif

}

