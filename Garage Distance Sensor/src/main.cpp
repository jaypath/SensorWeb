//#define DEBUG_

#include <Wire.h>
#include <Arduino.h> // Every sketch needs this
#include <TFLI2C.h> // TFLuna-I2C Library v.0.1.0
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here


TFLI2C tflI2C;

int16_t tfAddr = 0x10; // Default I2C address


#define GOLDILOCKS_ZONE 3 //inches from 0 that are considered perfect

//Code to draw to the screen
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#include <ESP8266HTTPClient.h>


#include "timesetup.h"

#include <ESP8266WebServer.h>



//Defining variables for the LED display:
//FOR HARDWARE TYPE, only uncomment the only that fits your hardware type
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW //blue PCP with 7219 chip
//#define HARDWARE_TYPE MD_MAX72XX:GENERIC_HW
#define MAX_DEVICES 4
#define CS_PIN D8
#define CLK_PIN     D5
#define DATA_PIN    D7

#define SCREEN_DRAW 400 //in ms, wait between each screen draw

#define INVERT
#define INVERT_TIME 333 //in ms

//#define DEBUG

MD_Parola screen = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); //hardware spi
//MD_Parola screen = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES); // Software spi

typedef uint8_t u8;
typedef int16_t i16;
// typedef uint16_t u16;
typedef uint32_t u32;


//globals
//measurements In inches
u32  STABLETIME;
time_t LASTTIMEUPDATE;
u8 LASTMINUTEDRAWN = 0;
i16 OFFSET = 30;
bool INVERTED = false;
u32 LASTINVERT = 0;
u32 LASTDRAW = 0;
i16 OLDDIST= 0;
i16 DIST = 0;
bool GARAGEOPEN = false;
char DATESTRING[24]=""; //holds up to hh:nn:ss day mm/dd/yyy
ESP8266WebServer server(80);

void handlePost();
void handleRoot();              // function prototypes for HTTP handlers
void handleNotFound();
String fcnDOW(time_t);
char* dateify(time_t, String);

void handleNotFound(){
  server.send(404, "text/plain", "GarageDistance says: 404 Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

String fcnDOW(time_t t) {
    if (weekday(t) == 1) return "Sun";
    if (weekday(t) == 2) return "Mon";
    if (weekday(t) == 3) return "Tue";
    if (weekday(t) == 4) return "Wed";
    if (weekday(t) == 5) return "Thu";
    if (weekday(t) == 6) return "Fri";
    if (weekday(t) == 7) return "Sat";

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



inline void getDistance(void) {
  OLDDIST = DIST;
  tflI2C.getData( DIST, tfAddr);
  if (DIST<=0) {
    DIST=-1000;
    return;
  }

  DIST=DIST/2.54 - OFFSET;

  if (abs(DIST-OLDDIST) > 1)   STABLETIME = millis();

  return; //return inches
}



void setup()
{

  Wire.begin(); // Initalize Wire library

  #ifdef DEBUG_
    Serial.begin(115200);
    Serial.println("start");


  #endif

  #ifdef DEBUG_
    Serial.println("try wifi...");

  #endif

    WiFi.begin(ESP_SSID, ESP_PASS);


  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    
    #ifdef DEBUG_
    Serial.print(".");
    #endif
  }

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

  #ifdef DEBUG_
    Serial.println("try screen...");

  #endif

  //Setting up the LED display
  screen.begin();
  screen.setIntensity(1);
  screen.displayClear();
  screen.setTextAlignment(PA_CENTER);
  screen.setInvert(false);


  #ifdef DEBUG_
    Serial.println("Set up TimeClient...");
  #endif

  LASTTIMEUPDATE=setupTime();

  #ifdef DEBUG_
    Serial.println("Set up webserver...");
  #endif

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.on("/POST", handlePost);   
  
  server.begin();



  #ifdef DEBUG_
    Serial.println("Setup end");
  #endif
}

void distStr(char* msg,  bool showNegatives) {
  if (abs(DIST) < GOLDILOCKS_ZONE) {
    sprintf(msg, "STOP");
    return;
  }

  GARAGEOPEN = false;
  if (DIST<-500) {
    sprintf(msg, "OPEN");
    GARAGEOPEN = true;
    return;
  }


  if (DIST < 0) {
    if (showNegatives) sprintf(msg, "%i in", DIST);
    else sprintf(msg, "STOP");
    return;
  }

  //If distance is less than 12 inches, dislay as X inches
  //Otherwise display as X feet
  if (DIST < 12) sprintf(msg, "%i in", DIST);
  else sprintf(msg, "%i ft", (int)(DIST / 12));
}



void loop()
{
  ArduinoOTA.handle();
  server.handleClient();
  timeClient.update();


  getDistance();
  char msg[10];

  if (millis() - STABLETIME > 15000) {
    if (now() - LASTTIMEUPDATE >3600)       LASTTIMEUPDATE= timeUpdate();
    
    //distance has been stable for 15 seconds

    if (LASTMINUTEDRAWN == minute()) return ; //don't redraw if I've already drawn this minute
    LASTMINUTEDRAWN = minute();
    screen.displayClear();
    screen.setTextAlignment(PA_CENTER);        
    sprintf(msg,"%d:%02d",hour(),minute());
    screen.print(msg);
    return;

  }  
 
  
  if (!(millis() - LASTDRAW >= SCREEN_DRAW || (OLDDIST > GOLDILOCKS_ZONE && DIST <= GOLDILOCKS_ZONE ))) return; //don't redraw if it's not time

  #ifdef DEBUG_
    Serial.print("Dist: ");
    Serial.println(DIST);
  #endif



  #ifdef INVERT
    if ((int) DIST < GOLDILOCKS_ZONE && millis() - LASTINVERT >= INVERT_TIME) {
      INVERTED = !INVERTED;
      screen.setInvert(INVERTED);
      LASTINVERT = millis();
    } else if (DIST >= GOLDILOCKS_ZONE) screen.setInvert(INVERTED = false);
  #endif

  distStr(msg,  true);
  screen.print(msg);
  LASTDRAW = millis();

}

void handlePost() {

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"OFFSET") OFFSET = server.arg(k).toInt();
  }


  server.sendHeader("Location", "/");
  handleRoot();
  //server.send(302, "text/plain", "Updated-- Press Back Button");  //This Line Keeps It on Same Page

}

void handleRoot() {

  String currentLine = "<!DOCTYPE html><html><head><title>Garage Distance Sensor</title></head><body>";

  currentLine = currentLine + "<h2>" + dateify(now(),"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br><br>";

  if (GARAGEOPEN)     currentLine = currentLine + "   GarageDoor: OPEN<br>";
  else currentLine = currentLine + "   GarageDoor: CLOSED<br>";
  currentLine = currentLine + "   CurrentDistance: " + (String) DIST + " inches<br>";


  currentLine += "<FORM action=\"/POST\" method=\"get\">";
  currentLine += "<p style=\"font-family:monospace\">";

  currentLine += "<label for=\"ZeroOffset\">Zero Offset: </label>";
  currentLine += "<input type=\"text\" id=\"ZeroOffset\" name=\"OFFSET\" value=\"" + (String) OFFSET  + "\"><br>";  

  currentLine += "<button type=\"submit\" formaction=\"/POST\">Change Offset</button><br><br>";

  currentLine += "</p>";
  currentLine += "</form>";


    
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}


