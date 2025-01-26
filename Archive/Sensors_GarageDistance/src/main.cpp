//#define DEBUG_

#include <Wire.h>
#include <Arduino.h> // Every sketch needs this
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

//Code to draw to the screen
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#include <ESP8266HTTPClient.h>

#include "timesetup.h"
#define ARDNAME  "Garage"

#define ARDID 95
#define NUMSERVERS 3
#define SENSORNUM 3
#define INVERT //if defined, do inversion when in stop zone


#define MAX_NEGLIGIBLE_DIST_CHANGE 2 //Max # of cm the distance can change before we consider it a distance change

#define DHT_temp 1
#define DHT_rh 2
#define ultrasonic_dist 7
#define BMP_pressure 9
#define BMP_temp 10
#define BMP_alt 11
#define bar_prediction 12
#define BME_pressure 13
#define BME_temp 14
#define BME_rh 15
#define BME_alt 16

#define nullpin 0

#define DHTTYPE    DHT22     // DHT11 or DHT22
#define DHTPIN D6
#define _USETFLUNA


typedef uint8_t u8;
typedef int16_t i16;
// typedef uint16_t u16;
typedef uint32_t u32;

const u8 SENSORTYPES[SENSORNUM] = {ultrasonic_dist, DHT_temp, DHT_rh};
const u8 MONITORED_SNS = 255;
const u8 OUTSIDE_SNS = 0;

#ifdef _USETFLUNA
  #include <TFLI2C.h> // TFLuna-I2C Library v.0.1.0
  TFLI2C tflI2C;
#endif

#ifdef DHTTYPE
 //  #include <Adafruit_Sensor.h>
  #include <DHT.h>

  DHT dht(DHTPIN, DHTTYPE); //third parameter is for faster cpu, 8266 suggested parameter is 11
#endif


#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here

i16 tfAddr = 0x10; // Default I2C address for Tfluna

//Defining variables for the LED display:
//FOR HARDWARE TYPE, only uncomment the only that fits your hardware type
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW //blue PCP with 7219 chip
//#define HARDWARE_TYPE MD_MAX72XX:GENERIC_HW
#define MAX_DEVICES 4
#define CS_PIN D8
#define CLK_PIN     D5
#define DATA_PIN    D7



MD_Parola screen = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); //hardware spi
//MD_Parola screen = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES); // Software spi

struct SensorVal {
  u8  snsType ;
  u8 snsID;
  u8 snsPin;
  char snsName[20];
  double snsValue;
  double limitUpper;
  double limitLower;
  uint16_t PollingInt;
  uint16_t SendingInt;
  u32 LastReadTime;
  u32 LastSendTime;  
  u8 Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value
};


//globals
int8_t GOLDILOCKS_ZONE = 8; //cm from 0 that are considered perfect; 3 in ~ 8 cm
int8_t CRITICAL_ZONE = -12; //cm from 0 that are considered perfect; 3 in ~ 8 cm
uint16_t INVERT_TIME = 500; //in ms
uint16_t CRITICAL_INVERT_TIME = 250;
uint16_t SCREEN_DRAW = 250; //in ms, wait between each screen draw
u32 LAST_DIST_CHANGE = 0;
u32 LASTINVERT = 0;
u32 LASTDRAW = 0;
u32 CHANGETOCLOCK = 30000; //in milliseconds, time to change to clock if dist hasn't changed
u8 NOWSHOWINCHES = 24; //this is in INCHES (all other units in cm, until display)

//measurements In cm, except display in inches
IPAddress arduino_IP; //my IP
IPAddress SERVERIP[3];
SensorVal Sensors[SENSORNUM]; //up to 2 sensors will be monitored
time_t LASTTIMEUPDATE;
u8 LASTMINUTEDRAWN = 0;
i16 OFFSET = 71;  //in cm
bool INVERTED = false;
int16_t OLDDIST= 10000; //in cm
int16_t DIST = -10000; //in cm

bool GARAGEOPEN = false;
char DATESTRING[24] = ""; //holds up to hh:nn:ss day mm/dd/yyy
ESP8266WebServer server(80);

bool ReadData(struct SensorVal*);
bool SendData(struct SensorVal*);
void handlePost(void);
void handleRoot(void);          // function prototypes for HTTP handlers
void handleNotFound(void);
String fcnDOW(time_t);
char* dateify(time_t, String);

void handleNotFound(void) {
  server.send(404, "text/plain", "GarageDistance says: 404 Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

String DOWs[7] = {
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat"
};
String fcnDOW(time_t t) {
  int day_of_week = weekday(t);
  if (day_of_week >= 1 && day_of_week <= 7) return DOWs[day_of_week - 1];

  return "???";
}

char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  sprintf(holder,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  sprintf(holder,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  sprintf(holder,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  sprintf(holder,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  sprintf(holder,"%02d",hour(t));
  dateformat.replace("hh",holder);

  sprintf(holder,"%02d",minute(t));
  dateformat.replace("nn",holder);

  sprintf(holder,"%02d",second(t));
  dateformat.replace("ss",holder);

  sprintf(holder,"%s",fcnDOW(t));
  dateformat.replace("DOW",holder);
  
  sprintf(DATESTRING,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

bool showDistance(uint32_t cm) {  //returns true if show distance, false if show clock
  bool showdist=true;

  i16 temporary_dist = 0;
  
  tflI2C.getData(temporary_dist, tfAddr);
  if (temporary_dist <= 0) {
    DIST=-1000;
    if (OLDDIST != DIST) {
      OLDDIST = DIST;
      if (cm-LAST_DIST_CHANGE > CHANGETOCLOCK) showdist= false;
      else showdist=true;
      LAST_DIST_CHANGE = cm;
      return showdist;
    } else {
      if (cm-LAST_DIST_CHANGE > CHANGETOCLOCK) return false;
      else return true;
    }
  } 
  
  temporary_dist = temporary_dist - OFFSET; //in cm!
  //If the distance has changed a negligble amount,
  //don't update the last distance change time nor the distance.

  if (abs(temporary_dist - OLDDIST) < MAX_NEGLIGIBLE_DIST_CHANGE)  {
    //just determine if clock... don't update distance
    if (cm-LAST_DIST_CHANGE > CHANGETOCLOCK) return false;
    else return true;
  }
  
  //the distance changed!
  OLDDIST = DIST;
  DIST = temporary_dist;
  LAST_DIST_CHANGE = cm;
  return true;
}

void initSensor(byte k) {
  sprintf(Sensors[k].snsName,"");
  Sensors[k].snsID = 0;
  Sensors[k].snsType = 0;
  Sensors[k].snsValue = 0;
  Sensors[k].PollingInt = 0;
  Sensors[k].SendingInt = 0;
  Sensors[k].LastReadTime = 0;
  Sensors[k].LastSendTime = 0;  
  Sensors[k].Flags = 0; 
}

u8 findSensor(byte snsType, byte snsID) {
  for (byte j = 0; j < SENSORNUM; j++)  {
    if (Sensors[j].snsID == snsID && Sensors[j].snsType == snsType) return j; 
  }
  return 255;  
}

bool checkSensorValFlag(struct SensorVal *P) {
  if (P->snsValue>P->limitUpper || P->snsValue<P->limitLower) bitWrite(P->Flags,0,1);
  else bitWrite(P->Flags,0,0);

return bitRead(P->Flags,0);

}
void initSensorsBasedOnType(void) {
  for (byte i = 0;i < SENSORNUM; i++) {
    Sensors[i].snsType=SENSORTYPES[i];
    Sensors[i].snsID=1; //increment this if there are others of the same type, should not occur
    Sensors[i].Flags = 0;
    if (bitRead(MONITORED_SNS,i)) bitWrite(Sensors[i].Flags,1,1);
    else bitWrite(Sensors[i].Flags,1,0);
    
    if (bitRead(OUTSIDE_SNS,i)) bitWrite(Sensors[i].Flags,2,1);
    else bitWrite(Sensors[i].Flags,2,0);

    switch (SENSORTYPES[i]) {
      #ifdef DHTTYPE
      case DHT_temp:
          Sensors[i].snsPin=DHTPIN;
          sprintf(Sensors[i].snsName,"%s_T",ARDNAME);
          Sensors[i].limitUpper = 90;
          Sensors[i].limitLower = 45;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=5*60;
        break;
      case DHT_rh:
          Sensors[i].snsPin=DHTPIN;
          sprintf(Sensors[i].snsName,"%s_RH",ARDNAME);
          Sensors[i].limitUpper = 75;
          Sensors[i].limitLower = 20;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=5*60;
        break;
      #endif
      case ultrasonic_dist:
        Sensors[i].snsPin = nullpin; //not used
        sprintf(Sensors[i].snsName,"%s_Dist",ARDNAME);
        Sensors[i].limitUpper = 250;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60;
        Sensors[i].SendingInt=5*60;
        break;
      case BMP_pressure:
        Sensors[i].snsPin = 0; //i2c
        sprintf(Sensors[i].snsName,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case BMP_temp:
        Sensors[i].snsPin = nullpin;
        sprintf(Sensors[i].snsName,"%s_BMP_t",ARDNAME);
        Sensors[i].limitUpper = 85;
        Sensors[i].limitLower = 65;
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=120;
        break;
      case BMP_alt:
        Sensors[i].snsPin = nullpin;
        sprintf(Sensors[i].snsName,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60000;
        Sensors[i].SendingInt=60000;
        break;
      case bar_prediction:
        Sensors[i].snsPin = nullpin;
        sprintf(Sensors[i].snsName,"%s_Pred",ARDNAME);
        Sensors[i].limitUpper = 0;
        Sensors[i].limitLower = 0; //anything over 0 is an alarm
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        bitWrite(Sensors[i].Flags,3,1); //calculated
        bitWrite(Sensors[i].Flags,4,1); //predictive
        break;
      case BME_pressure:
        Sensors[i].snsPin = nullpin; //i2c
        sprintf(Sensors[i].snsName,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case BME_temp:
        Sensors[i].snsPin = nullpin;
        sprintf(Sensors[i].snsName,"%s_BMEt",ARDNAME);
        Sensors[i].limitUpper = 85;
        Sensors[i].limitLower = 65;
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case BME_rh:
        Sensors[i].snsPin = nullpin;
        sprintf(Sensors[i].snsName,"%s_BMErh",ARDNAME);
        Sensors[i].limitUpper = 65;
        Sensors[i].limitLower = 25;
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case BME_alt:
        Sensors[i].snsPin = nullpin;
        sprintf(Sensors[i].snsName,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=15*60*60;
        Sensors[i].SendingInt=15*60*60;
        break;
    }

    Sensors[i].snsValue=0;
    Sensors[i].LastReadTime=0;
    Sensors[i].LastSendTime=0;
  }
}

void setup()
{
  SERVERIP[0] = {192,168,68,93};
  SERVERIP[1] = {192,168,68,106};
  SERVERIP[2] = {192,168,68,100};

  char msg[10];



  Wire.begin(); // Initalize Wire library

  #ifdef DEBUG_
    Serial.begin(115200);
    Serial.println("start");
  #endif

  #ifdef DEBUG_
    Serial.println("try wifi...");
  #endif

  #ifdef DEBUG_
    Serial.println("try screen...");

  #endif

  //Setting up the LED display
  screen.begin();
  screen.setIntensity(1);
  screen.displayClear();
  screen.setTextAlignment(PA_CENTER);
  screen.setInvert(false);
  screen.displayClear();
  screen.print("Start");



  WiFi.begin(ESP_SSID, ESP_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    
    #ifdef DEBUG_
    Serial.print(".");
    #endif
  }

  arduino_IP = WiFi.localIP();
  screen.displayClear();
  sprintf(msg,"IP:%i",arduino_IP[3]);
  screen.print(msg);

  delay(3000);

  screen.displayClear();
  screen.print("OTA");


   ArduinoOTA.setHostname("GarageDistance");
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG_
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
  });
    ArduinoOTA.begin();

  #ifdef DEBUG_
    Serial.println("init done...");

  #endif

  screen.displayClear();
  screen.print("TIME");

  #ifdef DEBUG_
    Serial.println("Set up TimeClient...");
  #endif

  LASTTIMEUPDATE=setupTime();


 /*
   * 
   * DEVICE SPECIFIC SETUP HERE!
   * 
   */
   //**************************************
  #ifdef DEBUG_
    Serial.println("DHT start...");
  #endif
  screen.displayClear();
  screen.print("DHT");

  //pinMode(DHTPIN, INPUT);
  dht.begin();
  
  
  #ifdef DHTTYPE
    #ifdef DEBUG_
        Serial.println("dht begin");
    #endif
    dht.begin();
  #endif

  for (byte i=0;i<SENSORNUM;i++) {
    initSensor(i);
  }
  initSensorsBasedOnType();

  screen.displayClear();
  screen.print("SRVR");
  server.begin();

  #ifdef DEBUG_
    Serial.println("Set up webserver...");
  #endif

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.on("/POST", handlePost);   
  



  #ifdef DEBUG_
    Serial.println("Setup end");
  #endif
    screen.displayClear();
  screen.print("Done");
  delay(1000);

}

void distStr(char* msg,  bool showNegatives, uint32_t cm) {


if ( (cm - LASTDRAW) <= SCREEN_DRAW ) return; //not time to redraw

double tempDIST = DIST/2.54;
double tempOLDDIST = OLDDIST/2.54;



//if ((int) tempDIST == (int) tempOLDDIST) return; //no effective change in measurement, return 

  if (abs(DIST) < GOLDILOCKS_ZONE) {
    sprintf(msg, "STOP");
  } else {

    GARAGEOPEN = false;
    if (DIST == -1000) {
      GARAGEOPEN =true; 
      sprintf(msg, "OPEN");
    } else {

      if (DIST < 0) {
        if (showNegatives) sprintf(msg, "%i in", (int) tempDIST);
        else sprintf(msg, "STOP");
      } else {

        //If distance is less than NOWSHOWINCHES inches, dislay as X inches
        //Otherwise display as X feet
        if (tempDIST < NOWSHOWINCHES) sprintf(msg, "%i in", (int) tempDIST);
        else sprintf(msg, "%i ft", (int)tempDIST/12);
      }
    }
  }

  screen.displayClear();
  screen.setTextAlignment(PA_CENTER);       
  screen.setInvert(INVERTED);
  screen.print(msg);
  LASTDRAW = cm;

}



void loop()
{
  u32 current_millis = millis();
  u32 n = now();
  char msg[10];

  if (current_millis < LAST_DIST_CHANGE) LAST_DIST_CHANGE=0; //last dist rolled over
  if (current_millis < LASTINVERT) LASTINVERT=0; //last  rolled over
  if (current_millis < LASTDRAW) LASTDRAW=0; //last  rolled over



  //1. get distance, this function also compares to prior dist (OLDDIST) and sets the last distance change time to ms if DIST changed

  if ( showDistance(current_millis)) { // distance has changed (or it isn't clocktime)

    current_millis = millis(); //update, since showdistance could take a while
    //  Decide if dist should be inverting or not.
    #ifdef DEBUG_
      Serial.print("Dist (cm): ");
      Serial.println(DIST);
    #endif

    #ifdef INVERT
      if (DIST == -1000) {
        INVERTED = true;
        LASTINVERT = current_millis;     
      } else {

        if ( DIST > GOLDILOCKS_ZONE) {
          INVERTED = false;
          LASTINVERT = current_millis;
        } else {
          if ((DIST <= CRITICAL_ZONE && (current_millis - LASTINVERT) >= CRITICAL_INVERT_TIME) || ( DIST <= GOLDILOCKS_ZONE &&  (current_millis - LASTINVERT) >= INVERT_TIME)) {
            INVERTED = !INVERTED;
            LASTINVERT = current_millis;
          }
        }
      }
      

    #endif

    distStr(msg,  true, current_millis);


  } else {
    //ok to handle tasks

    ArduinoOTA.handle();
    server.handleClient();
    timeClient.update();
    if (n - LASTTIMEUPDATE > 3600) LASTTIMEUPDATE = timeUpdate();
    
    //perform maintenance ... send to server... do stuff that shouldn't be done when in measurement mode
    for (byte k=0;k<SENSORNUM;k++) {
      bool flagstatus = bitRead(Sensors[k].Flags,0); //flag before reading value

      if (Sensors[k].LastReadTime + Sensors[k].PollingInt < n || n - Sensors[k].LastReadTime >60*60*24) ReadData(&Sensors[k]); //read value if it's time or if the read time is more than 24 hours from now in either direction
      
      if ((Sensors[k].LastSendTime + Sensors[k].SendingInt < n || flagstatus != bitRead(Sensors[k].Flags,0)) || n-Sensors[k].LastSendTime>60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
    }

    if (LASTMINUTEDRAWN == minute()) return ; //don't redraw if I've already drawn this minute
    LASTMINUTEDRAWN = minute();
    
    screen.displayClear();
    screen.setTextAlignment(PA_CENTER);       
    screen.setInvert(INVERTED = false); 
    LASTINVERT = current_millis;
    sprintf(msg,"%d:%02d",hour(),minute());
    screen.print(msg);
  }
 

}

void handlePost(void) {

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"OFFSET") OFFSET = (int) ((double) 2.54*server.arg(k).toDouble());
    if ((String)server.argName(k) == (String)"NOWSHOWINCHES") NOWSHOWINCHES = server.arg(k).toInt();
    if ((String)server.argName(k) == (String)"GOLDILOCKS_ZONE")  GOLDILOCKS_ZONE = (int16_t) ((double) 2.54*server.arg(k).toDouble());
    if ((String)server.argName(k) == (String)"CRITICAL_ZONE")  CRITICAL_ZONE = (int16_t) ((double) 2.54*server.arg(k).toDouble());
    if ((String)server.argName(k) == (String)"INVERT_TIME")  INVERT_TIME = server.arg(k).toInt();
    if ((String)server.argName(k) == (String)"CRITICAL_INVERT_TIME")  CRITICAL_INVERT_TIME = server.arg(k).toInt();
    if ((String)server.argName(k) == (String)"SCREEN_DRAW")  SCREEN_DRAW = server.arg(k).toInt();
    if ((String)server.argName(k) == (String)"CHANGETOCLOCK") CHANGETOCLOCK = server.arg(k).toInt();


  }


  server.sendHeader("Location", "/");
  handleRoot();
  //server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleRoot(void) {

  String currentLine = "<!DOCTYPE html><html><head><title>Garage Distance Sensor</title></head><body>";

  char* now_date = dateify(now(), "DOW mm/dd/yyyy hh:nn:ss");
  currentLine = currentLine + "<h1>Garage Distance Sensor</h1><br><br><h2>" + now_date + "</h2><br>";

  if (GARAGEOPEN)     currentLine = currentLine + "   GarageDoor: OPEN @" + now_date + "<br>";
  else currentLine = currentLine + "   GarageDoor: CLOSED @" + now_date + "<br>";

  currentLine = currentLine + "   CurrentDistance: " + (String) ((int) DIST/2.54) + " inches<br>";
  currentLine = currentLine + "   Garage Temperature: " + (String) Sensors[1].snsValue + "F @" + dateify(Sensors[1].LastReadTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";
  currentLine = currentLine + "   Garage RH: " + (String) Sensors[2].snsValue + "% @" + dateify(Sensors[2].LastReadTime,"DOW mm/dd/yyyy hh:nn:ss") + "<br>";

  currentLine += "<FORM action=\"/POST\" method=\"get\">";
  currentLine += "<p style=\"font-family:monospace\">";

  currentLine += "<label for=\"ZeroOffset\">Zero Offset (in): </label>";
  currentLine += "<input type=\"text\" id=\"ZeroOffset\" name=\"OFFSET\" value=\"" + (String) ((int) OFFSET/2.54)  + "\"><br>";  
  currentLine += "<label for=\"DistToShowInches\">Change to inches at this distance (in): </label>";
  currentLine += "<input type=\"text\" id=\"DistToShowInches\" name=\"NOWSHOWINCHES\" value=\"" + (String) NOWSHOWINCHES  + "\"><br>";  
  currentLine += "<label for=\"SHOWSTOP\">Show stop at this distance (in): </label>";
  currentLine += "<input type=\"text\" id=\"SHOWSTOP\" name=\"GOLDILOCKS_ZONE\" value=\"" + (String) ((int) GOLDILOCKS_ZONE/2.54)  + "\"><br>";  
  currentLine += "<label for=\"Criticalclose\">Critical close distance (in): </label>";
  currentLine += "<input type=\"text\" id=\"Criticalclose\" name=\"CRITICAL_ZONE\" value=\"" + (String) ((int) CRITICAL_ZONE/2.54)  + "\"><br>";

  currentLine += "<label for=\"InvertTime\">Goldilocks Flash Time (ms): </label>";
  currentLine += "<input type=\"text\" id=\"InvertTime\" name=\"INVERT_TIME\" value=\"" + (String) INVERT_TIME  + "\"><br>";  
  currentLine += "<label for=\"CritTime\">CRITICAL Flash Time (ms): </label>";
  currentLine += "<input type=\"text\" id=\"CritTime\" name=\"CRITICAL_INVERT_TIME\" value=\"" + (String) CRITICAL_INVERT_TIME  + "\"><br>";  
  
  currentLine += "<label for=\"SCREEN_DRAW\">Screen redraw Time (ms): </label>";
  currentLine += "<input type=\"text\" id=\"SCREEN_DRAW\" name=\"SCREEN_DRAW\" value=\"" + (String) SCREEN_DRAW  + "\"><br>";  

  currentLine += "<label for=\"CHANGETOCLOCK\">If no distance change, show clock after (ms): </label>";
  currentLine += "<input type=\"text\" id=\"CHANGETOCLOCK\" name=\"CHANGETOCLOCK\" value=\"" + (String) CHANGETOCLOCK  + "\"><br>";  


  currentLine += "<button type=\"submit\" formaction=\"/POST\">Update settings</button><br><br>";

  currentLine += "</p>";
  currentLine += "</form>";
    
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}


bool SendData(struct SensorVal *snsreading) {
  if (bitRead(snsreading->Flags,1) == 0) return false;
  
  WiFiClient wfclient;
  HTTPClient http;
    
    byte ipindex=0;
    bool isGood = false;

  
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String URL;
    String tempstring;
    int httpCode=404;
    tempstring = "/POST?IP=" + arduino_IP.toString() + ",&varName=" + String(snsreading->snsName);
    tempstring = tempstring + "&varValue=";
    tempstring = tempstring + String(snsreading->snsValue, DEC);
    tempstring = tempstring + "&Flags=";
    tempstring = tempstring + String(snsreading->Flags, DEC);
    tempstring = tempstring + "&logID=";
    tempstring = tempstring + String(ARDID, DEC);
    tempstring = tempstring + "." + String(snsreading->snsType, DEC) + "." + String(snsreading->snsID, DEC) + "&timeLogged=" + String(snsreading->LastReadTime, DEC) + "&isFlagged=" + String(bitRead(snsreading->Flags,0), DEC);

    do {
      URL = "http://" + SERVERIP[ipindex].toString();
      URL = URL + tempstring;
    
      http.useHTTP10(true);
    //note that I'm coverting lastreadtime to GMT
  
      snsreading->LastSendTime = now();
        #ifdef DEBUG_
            Serial.print("sending this message: ");
            Serial.println(URL.c_str());
        #endif
    
      http.begin(wfclient,URL.c_str());
      httpCode = http.GET();
      payload = http.getString();
      http.end();
        #ifdef DEBUG_
          Serial.print("Received: ");
          Serial.println(payload);
          Serial.print("Code: ");
          Serial.println(httpCode);
          Serial.println("*****************");
          Serial.println(" ");
        #endif

        ipindex++;

        if (httpCode == 200) isGood = true;
    } while(ipindex<NUMSERVERS);
  
    
  }
     return isGood;
}


bool ReadData(struct SensorVal *P) {
 
  bitWrite(P->Flags,0,0);
  
  switch (P->snsType) {
    case 1: //DHT temp
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  dht.readTemperature(true);
      #endif
      break;
    case 2: //DHT RH
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue = dht.readHumidity();
      #endif
      break;
    case 7: //dist
      #ifdef _USEHCSR04
        #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
        digitalWrite(TRIGPIN, LOW);
        delayMicroseconds(2);
        //Now we'll activate the ultrasonic ability
        digitalWrite(TRIGPIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIGPIN, LOW);

        //Now we'll get the time it took, IN MICROSECONDS, for the beam to bounce back
        long duration = pulseIn(ECHOPIN, HIGH);

        //Now use duration to get distance in units specd b USONIC_DIV.
        //We divide by 2 because it took half the time to get there, and the other half to bounce back.
        P->snsValue = (duration / USONIC_DIV); 
      #endif

      #ifdef _USETFLUNA
        
        P->snsValue=DIST;

      #endif

      break;
    case 9: //BMP pres
      #ifdef _USEBMP
         P->snsValue = bmp.readPressure()/100; //in hPa
        #ifdef _USEBARPRED
          if (LAST_BAR_READ+60*60 < now()) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
          }
          LAST_BAR_READ = now();
        #endif

      #endif
      break;
    case 10: //BMP temp
      #ifdef _USEBMP
        P->snsValue = ( bmp.readTemperature()*9/5+32);
      #endif
      break;
    case 11: //BMP alt
      #ifdef _USEBMP
         P->snsValue = (bmp.readAltitude(1013.25)); //meters
      #endif
      break;
    case 12: //make a prediction about weather
      #ifdef _USEBARPRED
        /*rules: 
        3 rise of 10 in 3 hrs = gale
        2 rise of 6 in 3 hrs = strong winds
        1 rise of 1.1 and >1015 = poor weather
        -1 fall of 1.1 and <1009 = rain
        -2 fall of 4 and <1023 = rain
        -3 fall of 4 and <1009 = storm
        -4 fall of 6 and <1009 = strong storm
        -5 fall of 7 and <990 = very strong storm
        -6 fall of 10 and <1009 = gale
        -7 fall of 4 and fall of 8 past 12 hours and <1005 = severe tstorm
        -8 fall of 24 in past 24 hours = weather bomb
        //https://www.worldstormcentral.co/law%20of%20storms/secret%20law%20of%20storms.html
        */        
        //fall of >3 hPa in 3 hours and P<1009 = storm
        P->snsValue = 0;
        if (BAR_HX[2]>0) {
          if (BAR_HX[0]-BAR_HX[2] >= 1.1 && BAR_HX[0] >= 1015) {
            P->snsValue = 1;
            sprintf(WEATHER,"Poor Weather");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 6) {
            P->snsValue = 2;
            sprintf(WEATHER,"Strong Winds");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 10) {
            P->snsValue = 3;        
            sprintf(WEATHER,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 1.1 && BAR_HX[0] <= 1009) {
            P->snsValue = -1;
            sprintf(WEATHER,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1023) {
            P->snsValue = -2;
            sprintf(WEATHER,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1009) {
            P->snsValue = -3;
            sprintf(WEATHER,"Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 6 && BAR_HX[0] <= 1009) {
            P->snsValue = -4;
            sprintf(WEATHER,"Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 7 && BAR_HX[0] <= 990) {
            P->snsValue = -5;
            sprintf(WEATHER,"Very Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 10 && BAR_HX[0] <= 1009) {
            P->snsValue = -6;
            sprintf(WEATHER,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[11]-BAR_HX[0] >= 8 && BAR_HX[0] <= 1005) {
            P->snsValue = -7;
            sprintf(WEATHER,"TStorm");
          }
          if (BAR_HX[23]-BAR_HX[0] >= 24) {
            P->snsValue = -8;
            sprintf(WEATHER,"BOMB");
          }
        }
      #endif
      break;
    case 13: //BME pres
      #ifdef _USEBME
         P->snsValue = bme.readPressure(); //in Pa
        #ifdef _USEBARPRED
          if (LAST_BAR_READ+60*60 < now()) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
          }
          LAST_BAR_READ = now();
        #endif
      #endif
      break;
    case 14: //BMEtemp
      #ifdef _USEBME
        P->snsValue = (( bme.readTemperature()*9/5+32) );
      #endif
      break;
    case 15: //bme rh
      #ifdef _USEBME
      
        P->snsValue = ( bme.readHumidity() );
      #endif
      break;
    case 16: //BMEalt
      #ifdef _USEBME
         P->snsValue = (bme.readAltitude(1013.25)); //meters

      #endif
      break;
  }

  checkSensorValFlag(P); //sets isFlagged
  P->LastReadTime = now(); //localtime
  
  return true;
}
