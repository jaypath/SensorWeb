#define DEBUG_ 1
#define _USE8266 1
//#define _USE32

#define ARDNAME "ATTIC"
#define ARDID 111 //unique arduino ID 

// SENSORTYPES is an array that defines the sensors. 
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative
//4 -  temp, AHT21
//5 - RH, AHT21
//6
//7 - distance, HC-SR04
//8
//9 - BMP pressure
//10 - BMP temp
//11 - BMP altitude
//12 - Pressure derived prediction (uses an array called BAR_HX containing hourly air pressure for past 24 hours). REquires _USEBARPRED be defined
//13 - BMe pressure
//14 - BMe temp
//15 - BMe humidity
//16 - BMe altitude

#define SENSORNUM 4 //be sure this matches SENSORTYPES

#include <Arduino.h>
const uint8_t SENSORTYPES[SENSORNUM] = {4,5,9,12};

const uint8_t MONITORED_SNS = 255; //from R to L each bit represents a sensor, 255 means all sensors are monitored
const uint8_t OUTSIDE_SNS = 0; //from R to L each bit represents a sensor, 255 means all sensors are outside

//#define DHTTYPE    DHT11     // DHT11 or DHT22
//#define DHTPIN 2
#define _USEBMP  1
#define _USEAHT 1
//#define _USEBME 1
//#define _USEHCSR04 1 //distance
//#define _USESOIL 1
#define _USEBARPRED 1
//#define _USESSD1306  1
//#define _OLEDTYPE &Adafruit128x64
//#define _OLEDTYPE &Adafruit128x32
//#define _OLEDINVERT 0

#ifdef _USESOIL
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
#endif

#ifdef _USEHCSR04
  #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
  #define TRIGPIN 2
  #define ECHOPIN 3
#endif

// SENSORTYPES is an array that defines the sensors. 
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative
//4
//5
//6
//7 - distance, HC-SR04
//8
//9 - BMP pressure
//10 - BMP temp
//11 - BMP altitude
//12 - Pressure derived prediction (uses an array called BAR_HX containing hourly air pressure for past 24 hours). REquires _USEBARPRED be defined
//13 - BMe pressure
//14 - BMe temp
//15 - BMe humidity
//16 - BMe altitude

#define GLOBAL_TIMEZONE_OFFSET  -14400

#define NUMSERVERS 3


#ifdef _USE8266 
  //static const uint8_t A0   = A0;
  // static const uint8_t D0   = 16;
  // static const uint8_t D1   = 5;
  // static const uint8_t D2   = 4;
  // static const uint8_t D3   = 0;
  // static const uint8_t D4   = 2;
  // static const uint8_t D5   = 14;
  // static const uint8_t D6   = 12;
  // static const uint8_t D7   = 13;
  // static const uint8_t D8   = 15;
  // static const uint8_t SDA   = 4;
  // static const uint8_t SCL  = 5;
  static const uint8_t LED   = 2;
#endif
//8266... I think they're the same. If not, then use nodemcu or wemos
#ifdef _USE32
  static const uint8_t TX = 1;
  static const uint8_t RX = 3;
  static const uint8_t SDA = 21;
  static const uint8_t SCL = 22;

  static const uint8_t SS    = 5;
  static const uint8_t MOSI  = 23;
  static const uint8_t MISO  = 19;
  static const uint8_t SCK   = 18;
  //need to define pin labeled outputs...
  static const uint8_t LED   = 2; //this is true for dev1 board... may be wrong for others
  
#endif



//#define NODEMCU
  /*
  SDA -> D2 -> 4
  SCL -> D1 -> 5
  SPI SCLK -> D5 ->14
  SPI MISO -> D6 -> 12
  SPI MOSI -> D7 -> 13
  SPI CS -> D8 -> 15
  */
//#define WEMOS
  /*
  SDA -> D2 -> 4
  SCL -> D1 -> 5
  SPI SCLK -> D5
  SPI MISO -> D6
  SPI MOSI -> D7
  SPI CS -> D8
  */

  

/*
 * Rev history
 * no number (v11) - data sends to server. polling this IP address will result in return of available data. Some settings can be changed/sent over HTTP.
 * 11.2 - data is sent to:
 * (first) arduino server, if available
 * (otherwise) directly to main server
 */

/*
 * 10's = Den
 * 20's = Basement/garage
 * 30's = Living Room
 * 40's = Office
 * 50's = Family Room
 * 60's = Kithcen
 * 70's = dining room
 * 80's = other 1st floor
 * 90's = other 1st floor
 * 100's = upstairs
 * 110's = attic
 * 150's = outside
 */


/*
 *Kiyaan server commands: 
 *192.168.68.147
 *  /WEATHERREQUEST?weatherType=xxx
 *   where xxx = temp, feels_like, pressure, humidity, dew_point, uvi, clouds, visibility, wind_speed, wind_deg, wind_gust, pop, last_rain, next_rain, weather [requires additional info specified by weatherObjType]
 *     if last_ or next_rain then you will receive unixtime of next event (or zero)
 *     if /WEATHERREQUEST?weatherType=weather&weatherObjType=yyy
 *        where yyy can be id, main, description, icon
 *        
 *  /GET?varName=zzz
 *    where zzz is a varName [sensor/arduino value] that I assign. varName can be, at most, 32 char. Example 23.12.2 [which could be arduino 23, sensor type 12, number 2]
 *  
 *  /POST?varName=zzz&varValue=NUMERIC&logID=FLOAT&timeLogged=UNX&isFlagged=bool
 *    where varName is a 32 char value representing arduino and sensor
 *    varValue is a numeric value (stored on server as float)
 *    logID = int representing the arduino sensor ID
 *    timeLogged is the unixtime (sec), but optional
 *    isFlagged is bool true or false (note lowercase), and is optional
 *    
 *    Queries:
*
*Using sql to query table directly, do the following (need selectGiven=true because SQL lite uses select or run [any other command] as parameter, and this specifies to use select [note that %22 is the equivalent of "]
*http://192.168.68.147/QUERYSTRING?selectGiven=true&queryString=select+*+from+logs+where+logID+=+%2214.15%22
*or
*http://192.168.68.147/QUERYSTRING?selectGiven=true&queryString=select+*+from+logs+where+logID+*equals*+%221.1.1%22+order+by+timeLogged+desc+LIMIT+1
*which returns top 1 value from 1.1.1 ordered by timelogged (desc) top 1 [so last value!]
*or
*http://192.168.68.147/QUERYSTRING?selectGiven=true&queryString=SELECT+logID,max(timeLogged),timeReceived,varName,varValue,isFlagged+FROM+logs+group+by+logid
*uses group by and max to get most recent val
*
*http://192.168.68.147/QUERYSTRING?selectGiven=false&queryString=UPDATE+logs+set+isFlagged+*equals*+0
*Resets isflagged for all entries (be careful with this!!!)
*
*NOTE: for = in sql use *equals*, > use *greter*, < use *lesser*
*
*
*fields:
*
*timeLogged
*varName
*varValue
*logID
*timeReceived
*
*
*codes: 
*700 invalid sql statement
*705 sql statement accepted
*
 
 */





#include <TimeLib.h>
#ifdef _USE8266
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>
  #include "LittleFS.h"

  #define FileSys LittleFS

#endif
#ifdef _USE32
  #include "SPIFFS.h" 
  #include "FS.h"
  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>
  #define FILESYS SPIFFS
  #define FORMAT_FS_IF_FAILED false
#endif

#include "ArduinoOTA.h"

#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <Wire.h>

#ifdef DHTTYPE
  #include <Adafruit_Sensor.h>
  #include <DHT.h>

  DHT dht(DHTPIN,DHTTYPE,11); //third parameter is for faster cpu, 8266 suggested parameter is 11
#endif

#ifdef _USEAHT
  #include <AHTxx.h>
  

  AHTxx aht21(AHTXX_ADDRESS_X38, AHT2x_SENSOR);
#endif



#ifdef _USEBMP
//  #include <Adafruit_Sensor.h>
  #include <Adafruit_BMP280.h>
  Adafruit_BMP280 bmp; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef _USEBME
  #include <Wire.h>
  //#include <Adafruit_Sensor.h>
  #include <Adafruit_BME280.h>
  Adafruit_BME280 bme; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef  _USESSD1306
  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C
  SSD1306AsciiWire oled;
#endif

#define FileSys LittleFS
#define BG_COLOR 0xD69A


//wifi
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov");
// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
// NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
//#define ESP_SSID "kandy-hispeed" // Your network name here
//#define ESP_PASS "manath77" // Your network password here


//this server
#ifdef _USE8266
  ESP8266WebServer server(80);
#endif
#ifdef _USE32
  WebServer server(80);
#endif

/*
const char FORM_page[] PROGMEM = R"=====(
  <FORM action="/UPDATESENSOR" method="get">
  <input type="hidden" name="SensorNum" value="@@SNSNUM@@"><br>  
  <label for="Mon">Is Monitored: </label>
  <input type="text" id="Mon" name="Monitored" value="@@FLAG1@@"><br>  
  <label for="Out">Is Outside: </label>
  <input type="text" id="Out" name="Outside" value="@@FLAG2@@"><br>  
  <label for="UL">Upper Limit:</label>
  <input type="text" id="UL" name="UpperLim" value="@@UPPERLIM@@"><br>
  <label for="LL">Lower Limit:</label>
  <input type="text" id="LL" name="LowerLim" value="@@LOWERLIM@@"><br>
  <label for="POLL">Poll Interval (sec):</label>
  <input type="text" id="POLL" name="PollInt" value="@@POLL@@"><br>
  <label for="SEND">Send Interval (sec):</label>
  <input type="text" id="SEND" name="SendInt" value="@@SEND@@"><br>
  <label for="recheck" class="switch">
  <input id="recheck" type="checkbox" name="recheckSensor"> Recheck Sensor <span class="slider round"></span>
  </label>
  <button type="submit">Submit</button>
  <button type="submit" formaction="/NEXTSNS">Next Sensor</button>
  <button type="submit" formaction="/LASTSNS">Prior Sensor</button>
</form>
)=====";
*/


bool KiyaanServer = false;

//wellesley, MA
#define LAT 42.307614
#define LON -71.299288

//gen global types

struct SensorVal {
  uint8_t  snsType ;
  uint8_t snsID;
  uint8_t snsPin;
  char snsName[32];
  double snsValue;
  double limitUpper;
  double limitLower;
  uint16_t PollingInt;
  uint16_t SendingInt;
  uint32_t LastReadTime;
  uint32_t LastSendTime;  
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value
};

struct IP_TYPE {
  IPAddress IP;
  int server_status;
};

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
  uint8_t b[51];
};

//globals
#ifdef _USEBARPRED
  double BAR_HX[24];
  char WEATHER[24] = "Steady";
  uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif

SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored
IP_TYPE SERVERIP[3];

IP_TYPE MYIP; //my IP
char DATESTRING[20]="";
byte OldTime[5] = {0,0,0,0,0};

int DSTOFFSET = 0;

//function declarations
char* strPad(char*, char*, byte);     // Simple C string function
bool SendData(struct SensorVal*);
bool ReadData(struct SensorVal*);
void timeUpdate(void);
void handleRoot(void);              // function prototypes for HTTP handlers
void initSensor(byte);
void handleNotFound(void);
void handleSETTHRESH(void);
void handleLIST(void);
void handleUPDATESENSORPARAMS(void);
void handleUPDATEALLSENSORREADS(void);
void handleUPDATESENSORREAD(void);
void handleNEXT(void);
void handleLAST(void);
void pushDoubleArray(double arr[], byte, double);
void redrawOled(void);
void Byte2Bin(uint8_t value, char* output, bool invert = false);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");

byte CURRENTSENSOR_WEB = 1;

void setup()
{
byte i;

  SERVERIP[0].IP = {192,168,68,93};
  SERVERIP[1].IP = {192,168,68,106};
  SERVERIP[2].IP = {192,168,68,104};

  Wire.begin(); 
  Wire.setClock(400000L);
  
    #ifdef DEBUG_
        Serial.println("oled begin");
    #endif

#ifdef _USESSD1306
  
  #if INCLUDE_SCROLLING == 0
  #error INCLUDE_SCROLLING must be non-zero. Edit SSD1306Ascii.h
  #elif INCLUDE_SCROLLING == 1
    // Scrolling is not enable by default for INCLUDE_SCROLLING set to one.
    oled.setScrollMode(SCROLL_MODE_AUTO);
  #else  // INCLUDE_SCROLLING
    // Scrolling is enable by default for INCLUDE_SCROLLING greater than one.
  #endif

  #if RST_PIN >= 0
    oled.begin(_OLEDTYPE, I2C_OLED, RST_PIN);
  #else // RST_PIN >= 0
    oled.begin(_OLEDTYPE, I2C_OLED);
  #endif // RST_PIN >= 0
  #ifdef _OLEDINVERT
    oled.ssd1306WriteCmd(SSD1306_SEGREMAP); // colAddr 0 mapped to SEG0 (RESET)
    oled.ssd1306WriteCmd(SSD1306_COMSCANINC); // Scan from COM0 to COM[N –1], normal (RESET)
  #endif

  oled.setFont(System5x7);
  oled.set1X();
  oled.clear();
  oled.setCursor(0,0);
#endif


  
  #ifdef DEBUG_
    Serial.begin(115200);
    Serial.println("Begin Setup");
  #endif




      #ifdef _USESSD1306
        oled.clear();
        oled.setCursor(0,0);
        oled.println("WiFi Starting.");
      #endif

  WiFi.begin(ESP_SSID, ESP_PASS);
    #ifdef DEBUG_
        Serial.println("wifi begin");
    #endif

  #ifdef DEBUG_
    Serial.println();
    Serial.print("Connecting");
  #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    #ifdef DEBUG_
    Serial.print(".");
    #endif
    #ifdef _USESSD1306
      oled.print(".");
    #endif
  }

  MYIP.IP = WiFi.localIP();

    #ifdef DEBUG_
    Serial.println("");
    Serial.print("Wifi OK. IP is ");
    Serial.println(MYIP.IP.toString());
    Serial.println("Connecting ArduinoOTA...");
    #endif

  #ifdef DEBUG_
    Serial.println("Connected!");
    Serial.println(WiFi.localIP());
  #endif

    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("WiFi OK.");
      oled.println("OTA start.");      
    #endif


    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname(ARDNAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    #ifdef DEBUG_
    Serial.println("OTA started");
    #endif
  });
  ArduinoOTA.onEnd([]() {
    #ifdef DEBUG_
    Serial.println("OTA End");
    #endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef DEBUG_
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
    #endif
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("Receiving OTA:");
      String strbuff = "Progress: " + (100*progress / total);
      oled.println("OTA start.");   
      oled.println(strbuff);
    #endif

  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG_
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      String strbuff;
      strbuff = "Error[%u]: " + (String) error + " ";
      oled.print(strbuff);
      if (error == OTA_AUTH_ERROR) oled.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) oled.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) oled.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) oled.println("Receive Failed");
      else if (error == OTA_END_ERROR) oled.println("End Failed");
    #endif
  });
  ArduinoOTA.begin();
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("OTA OK.");      
    #endif

    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("start time.");
    #endif

    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET);

    if (month() < 3 || (month() == 3 &&  day() < 12) || month() ==12 || (month() == 11 && day() > 5)) DSTOFFSET = -1*60*60;
    else DSTOFFSET = 0;

    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET); //set stoffregen timelib time once, to get month and day. then reset with DST

    //set the stoffregen time library with timezone and dst
    timeUpdate();



    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("start Server.");
    #endif

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/UPDATEALLSENSORREADS", handleUPDATEALLSENSORREADS);               
    server.on("/UPDATESENSORREAD",handleUPDATESENSORREAD);
    server.on("/SETTHRESH", handleSETTHRESH);               
    server.on("/LIST", handleLIST);
    server.on("/UPDATESENSORPARAMS", handleUPDATESENSORPARAMS);
    server.on("/NEXTSNS", handleNEXT);
    server.on("/LASTSNS", handleLAST);
    server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call
    server.begin();

    #ifdef DEBUG_
      Serial.println("HTML server started!");
    #endif


  /*
   * 
   * DEVICE SPECIFIC SETUP HERE!
   * 
   */
   //**************************************
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("Sns setup.");
    #endif


    //init globals
    for ( i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }
    #ifdef _USEBARPRED
      for (byte ii=0;ii<24;ii++) {
        BAR_HX[ii] = -1;
      }
      LAST_BAR_READ=0; 
    #endif
  
  
  #ifdef _USEAHT
    while (aht21.begin() != true)
    {
      Serial.println(F("AHT2x not connected or fail to load calibration coefficient")); //(F()) save string to flash & keeps dynamic memory free
    }

  #endif

  #ifdef DHTTYPE
    #ifdef DEBUG_
        Serial.println("dht begin");
    #endif
    dht.begin();
  #endif


  #ifdef _USEBMP
    #ifdef DEBUG_
        Serial.println("bmp begin");
    #endif
    while (!bmp.begin(0x76)) { //default address is 0x77, but amazon review says this is 0x76

      #ifdef _USESSD1306
        oled.println("BMP failed.");
        #ifdef DEBUG_
            Serial.println("bmp failed.");
        #endif
        delay(500);
        oled.clear();
        oled.setCursor(0,0);
        delay(500);
      #else
        digitalWrite(LED,HIGH);
        delay(500);
        digitalWrite(LED,LOW);
        delay(500);
      #endif
    }
 
    /* Default settings from datasheet. */
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */


  #endif
  #ifdef _USEBME
      #ifdef DEBUG_
        Serial.println("bme begin");
    #endif

    while (!bme.begin()) {
      #ifdef _USESSD1306
        oled.println("BME failed.");
        delay(500);
        oled.clear();
        oled.setCursor(0,0);
        delay(500);
      #else
        digitalWrite(LED,HIGH);
        delay(500);
        digitalWrite(LED,LOW);
        delay(500);
      #endif
    }
 
    /* Default settings from datasheet. */
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BME280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BME280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BME280::FILTER_X16,      /* Filtering. */
                  Adafruit_BME280::STANDBY_MS_500); /* Standby time. */


  #endif


  /*
  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  
  if(!LittleFS.begin()){
    #ifdef DEBUG_
      Serial.println("An Error has occurred while mounting LittleFS");
    #endif
    while (true) {
    }
  } else {
    #ifdef DEBUG_
      Serial.println("FileSys OK. Config Wifi");
    #endif

  }

*/




  for (i=0;i<SENSORNUM;i++) {
    Sensors[i].snsType=SENSORTYPES[i];
    Sensors[i].snsID=1; //increment this if there are others of the same type, should not occur
    Sensors[i].Flags = 0;
    if (bitRead(MONITORED_SNS,i)) bitWrite(Sensors[i].Flags,1,1);
    else bitWrite(Sensors[i].Flags,1,0);
    
    if (bitRead(OUTSIDE_SNS,i)) bitWrite(Sensors[i].Flags,2,1);
    else bitWrite(Sensors[i].Flags,2,0);


    switch (SENSORTYPES[i]) {
      case 1: //DHT temp
        #ifdef DHTTYPE
          Sensors[i].snsPin=DHTPIN;
          sprintf(Sensors[i].snsName,"%s_T",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=2*60;          
        #endif
        break;
      case 2: //DHT RH
        #ifdef DHTTYPE
          Sensors[i].snsPin=DHTPIN;
          sprintf(Sensors[i].snsName,"%s_RH",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 25;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
          }
          Sensors[i].PollingInt=2*60;
          Sensors[i].SendingInt=5*60;
        #endif
        break;
      case 3: //soil
        #ifdef _USESOIL
          Sensors[i].snsPin=SOILPIN;
          sprintf(Sensors[i].snsName,"%s_soil",ARDNAME);
          Sensors[i].limitUpper = 290;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=600;
        #endif
        break;
      case 4: //AHT temp
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          sprintf(Sensors[i].snsName,"%s_AHT_T",ARDNAME);
          Sensors[i].limitUpper = 115;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=1*60;
          Sensors[i].SendingInt=5*60;
        #endif
      case 5:
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          sprintf(Sensors[i].snsName,"%s_AHT_RH",ARDNAME);
          Sensors[i].limitUpper = 85;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=300;
        #endif
  

      case 7: //dist
        Sensors[i].snsPin=0; //not used
        sprintf(Sensors[i].snsName,"%s_Dist",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = 10;
        Sensors[i].PollingInt=100;
        Sensors[i].SendingInt=100;
        break;
      case 9: //BMP pres
        Sensors[i].snsPin=0; //i2c
        sprintf(Sensors[i].snsName,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 10: //BMP temp
        Sensors[i].snsPin=0;
        sprintf(Sensors[i].snsName,"%s_BMP_t",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=120;
        break;
      case 11: //BMP alt
        Sensors[i].snsPin=0;
        sprintf(Sensors[i].snsName,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60000;
        Sensors[i].SendingInt=60000;
        break;
      case 12: //Bar prediction
        Sensors[i].snsPin=0;
        sprintf(Sensors[i].snsName,"%s_Pred",ARDNAME);
        Sensors[i].limitUpper = 0;
        Sensors[i].limitLower = 0; //anything over 0 is an alarm
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        bitWrite(Sensors[i].Flags,3,1); //calculated
        bitWrite(Sensors[i].Flags,4,1); //predictive
        break;
      case 13: //BME pres
        Sensors[i].snsPin=0; //i2c
        sprintf(Sensors[i].snsName,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 14: //BMEtemp
        Sensors[i].snsPin=0;
        sprintf(Sensors[i].snsName,"%s_BMEt",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case 15: //bme rh
        Sensors[i].snsPin=0;
        sprintf(Sensors[i].snsName,"%s_BMErh",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 15;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case 16: //bme alt
        Sensors[i].snsPin=0;
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


   //**************************************




  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println(WiFi.localIP().toString());
    oled.print(hour());
    oled.print(":");
    oled.println(minute());
  #endif
}



//sensor fcns
bool checkSensorValFlag(struct SensorVal *P) {
  if (P->snsValue>P->limitUpper || P->snsValue<P->limitLower)       bitWrite(P->Flags,0,1);
      
  else bitWrite(P->Flags,0,0);

return bitRead(P->Flags,0);

}

uint8_t findSensor(byte snsType, byte snsID) {
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID == snsID && Sensors[j].snsType == snsType) return j; 
  }
  return 255;  
}

uint16_t findOldestDev() {
  int thisInd = 0;
  int oldestInd = 0;
  int  i=0;
  for (i=0;i<SENSORNUM;i++) {
    if (Sensors[oldestInd].LastReadTime == 0) oldestInd = i;
    else if (Sensors[i].LastReadTime< Sensors[oldestInd].LastReadTime && Sensors[i].LastReadTime >0) oldestInd = i;
  }
  if (Sensors[oldestInd].LastReadTime == 0) oldestInd = 30000;

  return oldestInd;
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
  Sensors[k].Flags =0; 
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID > 0) c++; 
  }
  return c;
}


//Time fcn
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

void timeUpdate() {
  timeClient.update();
  if (month() < 3 || (month() == 3 &&  day() < 12) || month() ==12 || (month() == 11 && day() > 5)) DSTOFFSET = -1*60*60;
  else DSTOFFSET = 0;

  setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  return;
}


void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  timeClient.update();

  time_t t = now(); // store the current time in time variable t
  time_t t2;

  convertULONG Ltype;
  
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second
    bool flagstatus=false;
    

    for (byte k=0;k<SENSORNUM;k++) {
      flagstatus = bitRead(Sensors[k].Flags,0); //flag before reading value

      if (Sensors[k].LastReadTime + Sensors[k].PollingInt < t || abs(Sensors[k].LastReadTime - now())>60*60*24) ReadData(&Sensors[k]); //read value if it's time or if the read time is more than 24 hours from now in either direction
      
      if ((Sensors[k].LastSendTime + Sensors[k].SendingInt < t || flagstatus != bitRead(Sensors[k].Flags,0)) || abs(Sensors[k].LastSendTime - now())>60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
    }
  }
  
  if (OldTime[1] != minute()) {
    OldTime[1] = minute();

    #ifdef _USESSD1306
      redrawOled();
    #endif

  }
  
  if (OldTime[2] != hour()) {
    OldTime[2] = hour();
    timeUpdate();

  }
  
  if (OldTime[3] != weekday()) {
    OldTime[3] = weekday();
  
  //sync time
    #ifdef DEBUG_
      t=now();
      Serial.print("Current time is ");
      Serial.print(hour(t));
      Serial.print(":");
      Serial.print(minute(t));
      Serial.print(":");
      Serial.println(second(t));
    #endif

  }
  
 
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
    if (server.argName(i)=="SensorName") strcpy(Sensors[j].snsName, server.arg(i).c_str());

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

    currentLine = currentLine + "ARDID:" + String(ARDID, DEC) + "; snsType:"+(String) Sensors[j].snsType+"; snsID:"+ (String) Sensors[j].snsID + "; SnsName:"+Sensors[j].snsName + "; LastRead:"+(String) Sensors[j].LastReadTime+"; LastSend:"+(String) Sensors[j].LastSendTime + "; snsVal:"+(String) Sensors[j].snsValue + "; UpperLim:"+(String) Sensors[j].limitUpper + "; LowerLim:"+(String) Sensors[j].limitLower + "; Flag:"+(String) bitRead(Sensors[j].Flags,0) + "; Monitored: " + (String) bitRead(Sensors[j].Flags,1) + "\n";
    server.send(400, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
  } else {
    server.send(400, "text/plain", "That sensor was not found");   // Send HTTP status 400 as error
  }
}

void handleLIST() {
String currentLine = "";
currentLine =  "IP:" + MYIP.IP.toString() + "\nARDID:" + String( ARDID, DEC) + "\n";
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
    currentLine +=  Sensors[j].snsName;
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

    #ifdef DEBUG_
        Serial.print("SpecType after byte2bin: ");
        Serial.println(cbuff);
    #endif

    currentLine +=  cbuff;
    currentLine += "\n\n";
  }
   #ifdef DEBUG_
      Serial.println(currentLine);
    #endif
  server.send(200, "text/plain", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleRoot() {
char temp[30] = "";

String currentLine = "<!DOCTYPE html><html><head><title>Arduino Server Page</title></head><body>";

//currentLine += "<h1></h1>";

currentLine =  "<h2>Arduino: " + (String) ARDNAME + "<br>IP:" + MYIP.IP.toString() + "<br>ARDID:" + String( ARDID, DEC) + "<br></h2><p>";
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
  sprintf(temp,"%s","Sensor Name");
  strPad(temp,padder,25);
  currentLine += "<label for=\"MyName\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"MyName\" name=\"SensorName\" value=\"" + String(Sensors[j].snsName) + "\" maxlength=\"30\"><br>";  

  sprintf(temp,"%s","Sensor Value");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Val\">" + (String) temp + " " + String(Sensors[j].snsValue,DEC) + "</label>";
  currentLine +=  "<br>";
  
  sprintf(temp,"%s","Is Monitored");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Mon\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Mon\" name=\"Monitored\" value=\"" + String(bitRead(Sensors[j].Flags,1),DEC) + "\"><br>";  
  
sprintf(temp,"%s","Is Outside");
strPad(temp,padder,25);
  currentLine += "<label for=\"Out\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Out\" name=\"Outside\" value=\"" + String(bitRead(Sensors[j].Flags,2),DEC) + "\"><br>";  

sprintf(temp,"%s","Upper Limit");
strPad(temp,padder,25);
  currentLine += "<label for=\"UL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"UL\" name=\"UpperLim\" value=\"" + String(Sensors[j].limitUpper,DEC) + "\"><br>";

sprintf(temp,"%s","Lower Limit");
strPad(temp,padder,25);
  currentLine += "<label for=\"LL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"LL\" name=\"LowerLim\" value=\"" + String(Sensors[j].limitLower,DEC) + "\"><br>";

sprintf(temp,"%s","Poll Int (sec)");
strPad(temp,padder,25);
  currentLine += "<label for=\"POLL\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"POLL\" name=\"PollInt\" value=\"" + String(Sensors[j].PollingInt,DEC) + "\"><br>";

sprintf(temp,"%s","Send Int (Sec)");
strPad(temp,padder,25);
  currentLine += "<label for=\"SEND\">" + (String) temp + "</label>";
    currentLine += "<input type=\"text\" id=\"SEND\" name=\"SendInt\" value=\"" + String(Sensors[j].SendingInt,DEC) + "\"><br>";

/*
  sprintf(temp,"%s","Recheck Sensor");
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

   #ifdef DEBUG_
      Serial.println(currentLine);
    #endif

    //IF USING PROGMEM: use send_p   !!
  server.send(200, "text/html", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleNotFound(){
  server.send(404, "text/plain", "Arduino says 404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

bool SendData(struct SensorVal *snsreading) {
  if (bitRead(snsreading->Flags,1) == 0) return false;
  
  WiFiClient wfclient;
  HTTPClient http;
    
    byte ipindex=0;
    bool isGood = false;

      #ifdef DEBUG_
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
    tempstring = "/POST?IP=" + MYIP.IP.toString() + "," + "&varName=" + String(snsreading->snsName);
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

        if (httpCode == 200) {
          isGood = true;
          SERVERIP[ipindex].server_status = httpCode;
        } 
    } while(ipindex<NUMSERVERS);
  
    
  }
     return isGood;
}


bool ReadData(struct SensorVal *P) {
      double val;

  bitWrite(P->Flags,0,0);
  
  switch (P->snsType) {
    case 1: //DHT temp
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  (dht.readTemperature()*9/5+32);
      #endif
      break;
    case 2: //DHT RH
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue = dht.readHumidity();
      #endif
      break;
    case 3: //soil
      #ifdef _USESOIL
        //soil moisture v1.2
        val = analogRead(P->snsPin);
        //based on experimentation... this eq provides a scaled soil value where 0 to 100 corresponds to 450 to 800 range for soil saturation (higher is dryer. Note that water and air may be above 100 or below 0, respec
        val =  (int) ((-0.28571*val+228.5714)*100); //round value
        P->snsValue =  val/100;
      #endif
      break;
    case 4: //AHT Temp
      #ifdef _USEAHT
        //AHT21 temperature
          val = aht21.readTemperature();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (100*(val*9/5+32))/100; 
            #ifdef DEBUG_
              Serial.print(F("AHT Temperature...: "));
              Serial.print(P->snsValue);
              Serial.println(F("F"));
            #endif
          }
          else
          {
            #ifdef DEBUG_
              Serial.print(F("AHT Temperature Error"));
            #endif
          }
      #endif
      break;
    case 5: //AHT RH
      //AHT21 humidity
        #ifdef _USEAHT
          val = aht21.readHumidity();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (val*100)/100; 
            #ifdef DEBUG_
              Serial.print(F("AHT HUmidity...: "));
              Serial.print(P->snsValue);
              Serial.println(F("%"));
            #endif
          }
          else
          {
            #ifdef DEBUG_
              Serial.print(F("AHT Humidity Error"));
            #endif
          }
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

      break;
    case 9: //BMP pres
      #ifdef _USEBMP
         P->snsValue = bmp.readPressure()/100; //in hPa
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            P->limitLower = 1000;
          } else {
            P->limitLower = 1009;
          }

          if (LAST_BAR_READ+60*60 < now()) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = now();          
          }
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
          //adjust critical values based on history, if available
          if (P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            P->limitLower = 1000;
          } else {
            P->limitLower = 1009;
          }

          if (LAST_BAR_READ+60*60 < now()) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = now();          
          }
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
  

#ifdef DEBUG_
      Serial.println(" ");
      Serial.println("*****************");
      Serial.println("Reading Sensor");
      Serial.print("Device: ");
          Serial.println(P->snsName);
      Serial.print("SnsType: ");
          Serial.println(P->snsType);
      Serial.print("Value: ");
          Serial.println(P->snsValue);
      Serial.print("LastLogged: ");
          Serial.println(P->LastReadTime);
      Serial.print("isFlagged: ");
          Serial.println(bitRead(P->Flags,0));          
      Serial.println("*****************");
      Serial.println(" ");

      #endif


return true;
}

void redrawOled() {
  #if _USESSD1306

  byte j;

  oled.clear();
  oled.setCursor(0,0);
  oled.println(WiFi.localIP().toString());
  oled.print(hour());
  oled.print(":");
  oled.println(minute());

  if (_OLEDTYPE == &Adafruit128x64) {       
    for (byte j=0;j<3;j++) {
      oled.print(SERVERIP[j].IP.toString());
      oled.print(":");
      oled.print(SERVERIP[j].server_status);
      if (j!=2) oled.println(" ");    
    }    
  }

  for (j=0;j<SENSORNUM;j++) {
    if (bitRead(Sensors[j].Flags,0))   oled.print("!");
    else oled.print("O");
    oled.println(" ");
    #ifdef _USEBARPRED
      if (Sensors[j].snsType==12) {
        oled.println(WEATHER);
      } 
    #endif
  }
  #endif
  return;    
}

void pushDoubleArray(double arr[], byte N, double value) {
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;

/*
  double arr2[N];
  arr2[0] = value;

  for (byte i = 0 ; i < N-1 ; i++)
    {
        arr2[i+1] = *(arr+i);
        *(arr+i) = arr2[i];
    }
    *(arr+N-1) = arr2[N-1];
*/
}

void Byte2Bin(uint8_t value, char* output, bool invert) {
  sprintf(output,"00000000");
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
  
  sprintf(DATESTRING,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}
