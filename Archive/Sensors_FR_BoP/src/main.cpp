#include <Arduino.h>

//#define _DEBUG 1
//#define _USE8266 1
#define _USE32

#define ARDNAME "FRBoP"
#define ARDID 96 //unique arduino ID 
#define SENSORNUM 1 //be sure this matches SENSORTYPES


const uint8_t SENSORTYPES[SENSORNUM] = {3};

const uint8_t MONITORED_SNS = 255; //from R to L each bit represents a sensor, 255 means all sensors are monitored
const uint8_t OUTSIDE_SNS = 0; //from R to L each bit represents a sensor, 255 means all sensors are outside

#define _USELED 12 //for esp32 //D4 //for 8266
//#define DHTTYPE    DHT11     // DHT11 or DHT22
//#define DHTPIN 2
//#define _USEBMP  1
//#define _USEAHT 1
//#define _USEBME 1
//#define _USEHCSR04 1 //distance
//#define _USESOILCAP 1
#define _USESOILRES 33 //for esp32 use any adc1 such as 33 //for esp8266 D5 ////this is the pin that turns on to test soil... not to be confused with soilpin, the Analog in pin
//#define _USEBARPRED 1
//#define _USESSD1306  1
//#define _OLEDTYPE &Adafruit128x64
//#define _OLEDTYPE &Adafruit128x32
//#define _OLEDINVERT 0


// SENSORTYPES is an array that defines the sensors. 
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
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


#ifdef _USESOILCAP
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
#endif

#ifdef _USESOILRES
  const int SOILPIN = 32; //A4 is the same as GPIO 32 on the esp32 //  A0;  // use A0 on ESP8266 Analog Pin ADC0 = A0
  const int SOILDIO = _USESOILRES;  // ESP8266 Analog Pin ADC0 = A0
  //#define SOILRESISTANCE 4700 //ohms
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
    #define _ADCRATE 1023

#endif
//8266... I think they're the same. If not, then use nodemcu or wemos
#ifdef _USE32
  #define _ADCRATE 4095
/*  static const uint8_t TX = 1;
  static const uint8_t RX = 3;
  static const uint8_t SDA = 21;
  static const uint8_t SCL = 22;

  static const uint8_t SS    = 5;
  static const uint8_t MOSI  = 23;
  static const uint8_t MISO  = 19;
  static const uint8_t SCK   = 18;
  //need to define pin labeled outputs...
  static const uint8_t LED   = 2; //this is true for dev1 board... may be wrong for others
  */
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

//#include <Wire.h>

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
  uint32_t LastsnsValue;
  uint8_t Flags; 
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = Not used by server
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
uint8_t findSensor(byte snsType, byte snsID);

byte CURRENTSENSOR_WEB = 1;

#ifdef _USELED
  #include <FastLED.h>

  #ifdef _USE8266
    #define PI 3.14159265
  #endif
  
  #define LEDCOUNT 57 //how many LEDs did you install?

  CRGB LEDARRAY[LEDCOUNT];

  //LED Drawing
  typedef struct  {
    //cos(kx +(DIR)* wt) + 0.5; where k - is 2*pi/L [L, lambda, is wavelength]; and w is 2*pi/T [T, period, is time it takes to cycle through one wavelength)so speed is v = w/k; here I am using cos because you can set the inputs to 0 to obtain the static val
    //DIR is -1 for right rotation, +1 for left rotation
    // k = 2*pi/L, where L is in units of number of LEDs
    //w = 2*pi/T where T is seconds to cycle through a wavelenghth [L]
    //y(x,t) = (MaxBrightness-MinBrightness) * (cos(kx - wt) + 1)/2 + MinBrightness; here x is LED number, t is time
    uint8_t  animation_style; //1 = counter clockwise rotation (sin) // 2 = clockwise rotation (sin) // 3 = non-moving pulse // 4 = static
    uint16_t sin_T; //in other words, T, ms to move through one wavelength
    uint8_t sin_L; //wavelength, in number of LEDs
    uint8_t MaxBrightness; //sin amp
    uint8_t MinBrightness; //sin amp
    uint32_t last_LED_update;
    byte LEDredrawRefreshMS;
    CRGB color1; //current color start
    CRGB color2; //current color end //color will be a random uniformly distributed value between color 1 and 2
    uint32_t  LED_TIMING[LEDCOUNT]; //keeps track of the time of each LED in animations where each LED is in a different cycle (like sparkling gaussians, type 5)
    uint32_t  LED_BASE[LEDCOUNT]; //keeps track of the base color of each LED (generated as a random between color1 and color2), necessary where LEDs are cycling independently. In other cases, LED[0] will be the base color. Note this resets when LED completes a cycle
    

    void LED_update(void) {
      uint32_t m = millis();

      if (m-this->last_LED_update<this->LEDredrawRefreshMS) return;

      this->last_LED_update = m;

      double L1,T1;
      int8_t DIR = 0;

      for (byte j=0;j<LEDCOUNT;j++) {

        if (this->animation_style==1) {
          DIR=-1;
          L1 = (double) 1/this->sin_L;
          T1 = (double) 1/this->sin_T;
        }
        if (this->animation_style==2) {
          DIR=1;
          L1 = (double) 1/this->sin_L;
          T1 = (double) 1/this->sin_T;
        }
        if (this->animation_style==3) {
          DIR=1;
          L1=0;
          T1 = (double) 1/this->sin_T;
        }
        if (this->animation_style==4) {
          DIR=0;
          L1=0;
          T1 = 0;
        }
        if (this->animation_style==5) {
          DIR=0;
          L1=LEDCOUNT*300; //use L1 for limit of random chance
          T1=750; //use T1 for standard deviaiton of guassian, msec          
        }


        LEDARRAY[j] = this->LED_setLED(j,L1,T1,DIR,m);
      
        
      }


      #ifdef _DEBUG
        Serial.printf("global color is %d to %d. LED0 color is %d, 10 %d, 20 %d, 30 %d, 40 %d\n",this->color1,this->color2,LEDARRAY[0],LEDARRAY[10],LEDARRAY[20],LEDARRAY[30],LEDARRAY[40]);
      #endif

      FastLED.show();

    }

    uint32_t LED_setLED(byte j, double L1, double T1, int8_t DIR, uint32_t m) {
      //given an LED position, info on timing such as cosine cycle, animation direction, etc - set the color and brightness of that LED 
      //L1 T1 can have differing values, but if L1==0 then it is just static color
      uint32_t temp;
      byte brightness;

      if (L1==0 && j>0) return this->LED_BASE[0]; //all LEDs are the same for this animation style, just return the first base value

      if (this->animation_style==3 || this->animation_style==4) { //got here because j=0 so we completed a draw cycle. Choose new colors
        temp = LED_choose_color(this->MaxBrightness); //note that this will be the same color if color1 = color2
        this->LED_BASE[0] = temp;
        return temp;
      }

      if (this->animation_style==5) {
        //guassian shimmering... LED_TIMING holds the times of the peak display
        if (this->LED_TIMING[j]==0) {
          //this LED position has completed a cycle. Reset the next peak time and choose a new color
          this->LED_TIMING[j]=m + random(25,25+L1);
          this->LED_BASE[j]=LED_choose_color(this->MaxBrightness);
          return 0;
        }

        //determine the brightness as a guassian distribution:
        brightness = this->MaxBrightness * ((double) pow(2.71828,-1*((pow((double) m-this->LED_TIMING[j],2)/(2*pow(T1,2)))))); //m is current time, led_array is time of peak, T1 is standrad dev
        if (brightness < this->MinBrightness && this->LED_TIMING[j]<m) { //the peak is in the past and the brightness has tapered to below threshold, set the timing to 0 to reset on next cycke
          this->LED_TIMING[j]=0;
          return 0;
        }
        else {
          return LED_scale_brightness(this->LED_BASE[j],brightness);
        }
      }

      //if we got here then it is a cosine wave

      brightness = (uint8_t) ((double) (this->MaxBrightness-this->MinBrightness) * (cos((double) 2*PI*(j*L1 +(DIR)* m*T1)) + 1)/2 + this->MinBrightness);
      if (brightness == this->MinBrightness) this->LED_BASE[0] = LED_choose_color(this->MaxBrightness); //completed a cycle of the cosine wave, choose new color. Only the first element needs to change.

      temp =  LED_scale_brightness(this->LED_BASE[0],brightness); //only need the first value of LED_VASE, all the colors are the same
  
  /*
      #ifdef _DEBUG
        Serial.printf("LED_setLED: color = %d %d %d\n",temp.red,temp.green,temp.blue);
      #endif 
*/
      
      return temp;
    }

    uint32_t LED_scale_brightness(CRGB colorin, byte BRIGHTNESS_SCALE) {
      //scales brightness of the provided color
      double r = (double) colorin.red*BRIGHTNESS_SCALE/100;
      double g = (double) colorin.green*BRIGHTNESS_SCALE/100;
      double b = (double) colorin.blue*BRIGHTNESS_SCALE/100;

/*
      #ifdef _DEBUG
        Serial.printf("scale brightness: r_in = %d, g_in = %d, b_in = %d, scalereq = %d, r=%d, g=%d, b=%d, and u32 is %d\n",colorin.red, colorin.green, colorin.blue, BRIGHTNESS_SCALE,(byte) r,(byte) g,(byte) b,(uint32_t) ((byte) r << 16 | ((byte) g << 8 |  ((byte) b) )));
      #endif 
  */     
      return  (uint32_t) ((byte) r << 16 | (byte) g << 8 |  ((byte) b));
    }

    uint32_t LED_choose_color(byte BRIGHTNESS_SCALE=100) {      
      //chooses a color between 1 and 2 (and sets brightness)
      byte r1 = (byte) ((double) this->color1.red *BRIGHTNESS_SCALE/100);
      byte g1 = (byte) ((double) this->color1.green *BRIGHTNESS_SCALE/100);
      byte b1 = (byte) ((double) this->color1.blue *BRIGHTNESS_SCALE/100);

      byte r2 = (byte) ((double) this->color2.red *BRIGHTNESS_SCALE/100);
      byte g2 = (byte) ((double) this->color2.green *BRIGHTNESS_SCALE/100);
      byte b2 = (byte) ((double) this->color2.blue *BRIGHTNESS_SCALE/100);


      return (uint32_t) (this->LED_scale_color(r1,r2)<<16) | (this->LED_scale_color(g1,g2)<<8) | (this->LED_scale_color(b1,b2));
    }

    byte LED_scale_color(byte c1, byte c2) {
      //random uniformly choose a color that is between c1 and c2
      if (c1==c2) return c1;
      randomSeed(micros()); 
      if (c1>c2) return random(c2,c1+1);
      return random(c1,c2+1);
    }


    void LED_set_color(byte r1, byte g1, byte b1, byte r2, byte g2, byte b2) {
      this->color1 =   (uint32_t) ((byte) r1 << 16 | (byte) (g1) << 8 | (byte) (b1));
      this->color2 =   (uint32_t) ((byte) r2 << 16 | (byte) (g2) << 8 | (byte) (b2));

      #ifdef _DEBUG
        Serial.printf("LED_set_color R color1 is now %d.\n",this->color1.red);
      #endif 

    }

    void LED_set_color_soil(struct SensorVal *sns) {
      //while log(resistivity) is linearly correlated with moiusture, it is roughly linear in our range
      //let's scale from blue to green to yellow to red

      byte r1=0,g1=0,b1=0,r2=0,g2=0,b2=0;      
      byte animStyle = 0;

      byte  snspct = (byte) (100*((double)sns->snsValue / sns->limitUpper)); 
      byte snslim = 0;
      byte animStylestep = 0;

      do {
        animStylestep++;
        snslim += 10;
        if (snspct <= snslim) animStyle=animStylestep;        
        if (snspct>100) animStyle=11;
      } while (animStyle ==0);

      animStyle--; //added one extra

      switch (animStyle) {
        case 11: //very high... pulse red.
          LED_animation_defaults(3);
          this->MaxBrightness = 100;
          this->MinBrightness = 10;
          this->sin_T = 1000;
          r1=255;
          g1=0;
          b1=0;
          r2=255;
          g2=0;
          b2=0;
          
          break;
        case 10: //high sparkle red
          LED_animation_defaults(5);
          this->MaxBrightness = 30;
          this->MinBrightness = 5;
          this->sin_T = 1000;
          r1=255;
          g1=0;
          b1=0;
          r2=255;
          g2=0;
          b2=0;
          break;
        case 9: 
          LED_animation_defaults(5);
          this->MaxBrightness = 30;
          this->MinBrightness = 5;
          r1=255;
          g1=0;
          b1=0;
          r2=255;
          g2=50;
          b2=0;
          break;
        case 8:
          LED_animation_defaults(5);
          this->MaxBrightness = 25;
          this->MinBrightness = 5;
          r1=255;
          g1=50;
          b1=0;
          r2=255;
          g2=100;
          b2=0;
          break;
        case 7:
          LED_animation_defaults(5);
          this->MaxBrightness = 25;
          this->MinBrightness = 5;
          r1=255;
          g1=255;
          b1=0;
          r2=150;
          g2=255;
          b2=0;
          break;
        case 6:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=150;
          g1=255;
          b1=0;
          r2=50;
          g2=255;
          b2=0;
          break;
        case 5:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=50;
          g1=255;
          b1=0;
          r2=0;
          g2=255;
          b2=0;
          break;
        case 4:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=0;
          g1=255;
          b1=0;
          r2=0;
          g2=255;
          b2=100;
          break;
        case 3:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=0;
          g1=255;
          b1=50;
          r2=0;
          g2=255;
          b2=200;
          break;
        case 2:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=0;
          g1=255;
          b1=100;
          r2=0;
          g2=150;
          b2=255;
          break;
        case 1:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=0;
          g1=255;
          b1=100;
          r2=0;
          g2=200;
          b2=255;
          break;
        case 0:
          LED_animation_defaults(5);
          this->MaxBrightness = 15;
          this->MinBrightness = 5;
          r1=0;
          g1=0;
          b1=0;
          r2=0;
          g2=150;
          b2=255;
          break;

      }
      this->LED_set_color(r1,g1,b1,r2,g2,b2);
    }

    void LED_animation_defaults(byte anim) {
      this->animation_style = anim;
      switch (anim) {
        case 1: //wave clockwise
          sin_T = 1500; //in other words, T, ms to move through one wavelength
          sin_L = LEDCOUNT/2; //wavelength, in number of LEDs
          MaxBrightness = 15; //sin amp
          MinBrightness=5; 
          break;
        case 2: //wave counter-clockwise
          sin_T = 1500; //in other words, T, ms to move through one wavelength
          sin_L = LEDCOUNT/2; //wavelength, in number of LEDs
          MaxBrightness = 10; //sin amp
          MinBrightness=1; 
          break;
        case 3: //pulse
          sin_T = 30000; //in other words, T ms to move through one wavelength
          sin_L = LEDCOUNT/2; //wavelength, in number of LEDs
          MaxBrightness = 10; //sin amp
          MinBrightness=5; 
          break;
        case 4: //const
          MaxBrightness = 5;
          MinBrightness = 5;
          break;
        case 5: //random gaussian
          MaxBrightness = 50;
          MinBrightness = 5;
            
          break;
      }
    }


  } Animation_type;



Animation_type LEDs;

  
#endif



void setup()
{
byte i;

  SERVERIP[0].IP = {192,168,68,93};
  SERVERIP[1].IP = {192,168,68,106};
  SERVERIP[2].IP = {192,168,68,100};

#ifdef _USELED
  FastLED.addLeds<WS2813,_USELED,GRB>( LEDARRAY, LEDCOUNT).setCorrection(TypicalLEDStrip);

  LEDs.LED_animation_defaults(1);
  LEDs.LEDredrawRefreshMS=20;
  LEDs.LED_set_color(255,255,255,255,255,255); //default is white
#endif 
  
  #ifdef _USESOILRES
    pinMode(SOILDIO,OUTPUT);
    digitalWrite(SOILDIO, LOW);  
  #endif

//  Wire.begin(); 
//  Wire.setClock(400000L);
  
    #ifdef _DEBUG
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


  
  #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Begin Setup");
  #endif




      #ifdef _USESSD1306
        oled.clear();
        oled.setCursor(0,0);
        oled.println("WiFi Starting.");
      #endif

  WiFi.begin(ESP_SSID, ESP_PASS);
    #ifdef _DEBUG
        Serial.println("wifi begin");
    #endif

  #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
  #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    #ifdef _DEBUG
    Serial.print(".");
    #endif
    #ifdef _USESSD1306
      oled.print(".");
    #endif
  }

//  MYIP.IP = WiFi.localIP();

    #ifdef _DEBUG
    Serial.println("");
    Serial.print("Wifi OK. IP is ");
    Serial.println(WiFi.localIP().toString());
    Serial.println("Connecting ArduinoOTA...");
    #endif

  #ifdef _DEBUG
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
    #ifdef _DEBUG
    Serial.println("OTA started");
    #endif
    #ifdef _USELED
      for (byte j=0;j<LEDCOUNT;j++) {
        LEDARRAY[j] = (uint32_t) 0<<16 | 255<<8|0; //green
         
      }
      FastLED.show();
    #endif
  });
  ArduinoOTA.onEnd([]() {
    #ifdef _DEBUG
    Serial.println("OTA End");
    #endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef _USELED
      for (byte j=0;j<LEDCOUNT;j++) {
        LEDARRAY[LEDCOUNT-j-1] = 0;
        if (j<=(double) LEDCOUNT*progress/total) LEDARRAY[LEDCOUNT-j-1] = (uint32_t) 64 <<16 | 64 << 8 | 64;
         
      }
      FastLED.show();
    #endif
    #ifdef _DEBUG
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
    #ifdef _DEBUG
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

    #ifdef _DEBUG
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
    #ifdef _DEBUG
        Serial.println("dht begin");
    #endif
    dht.begin();
  #endif


  #ifdef _USEBMP
    #ifdef _DEBUG
        Serial.println("bmp begin");
    #endif
    uint32_t t = millis();
    uint32_t deltaT = 0;
    while (!bmp.begin(0x76) and deltaT<15000) { //default address is 0x77, but amazon review says this is 0x76
      deltaT = millis()-t;
      #ifdef _USESSD1306
        oled.println("BMP failed.");
        #ifdef _DEBUG
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
      #ifdef _DEBUG
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
    #ifdef _DEBUG
      Serial.println("An Error has occurred while mounting LittleFS");
    #endif
    while (true) {
    }
  } else {
    #ifdef _DEBUG
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
          snprintf(Sensors[i].snsName,31,"%s_T", ARDNAME);
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
          snprintf(Sensors[i].snsName,31,"%s_RH",ARDNAME);
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
        #ifdef _USESOILCAP
          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soil",ARDNAME);
          Sensors[i].limitUpper = 150;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=600;
        #endif
        #ifdef _USESOILRES
          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soilR",ARDNAME);
          Sensors[i].limitUpper = 750;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=300;
        #endif

        break;
      case 4: //AHT temp
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_T",ARDNAME);
          Sensors[i].limitUpper = 115;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
      case 5:
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_RH",ARDNAME);
          Sensors[i].limitUpper = 85;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
  

      case 7: //dist
        Sensors[i].snsPin=0; //not used
        snprintf(Sensors[i].snsName,31,"%s_Dist",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = 10;
        Sensors[i].PollingInt=100;
        Sensors[i].SendingInt=100;
        break;
      case 9: //BMP pres
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 10: //BMP temp
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMP_t",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 11: //BMP alt
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60000;
        Sensors[i].SendingInt=60000;
        break;
      case 12: //Bar prediction
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_Pred",ARDNAME);
        Sensors[i].limitUpper = 0;
        Sensors[i].limitLower = 0; //anything over 0 is an alarm
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        bitWrite(Sensors[i].Flags,3,1); //calculated
        bitWrite(Sensors[i].Flags,4,1); //predictive
        break;
      case 13: //BME pres
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 14: //BMEtemp
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMEt",ARDNAME);
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
        snprintf(Sensors[i].snsName,31,"%s_BMErh",ARDNAME);
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
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
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
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, flag status changed and I have not sent data yet
  
  bool lastflag = false;
  bool thisflag = false;

  if (P->snsType==50 || P->snsType==55 || P->snsType==56 || P->snsType==57) { //HVAC is a special case
    lastflag = bitRead(P->Flags,0); //this is the last flag status
    if (P->LastsnsValue <  P->snsValue) { //currently flagged
      bitWrite(P->Flags,0,1); //currently flagged
      bitWrite(P->Flags,5,1); //value is high
      if (lastflag) {
        bitWrite(P->Flags,6,0); //no change in flag

      } else {
        bitWrite(P->Flags,6,1); //changed to high

      }
      return true; //flagged
    } else { //currently NOT flagged
      bitWrite(P->Flags,0,0); //currently not flagged
      bitWrite(P->Flags,5,0); //irrelevant
      if (lastflag) {
        bitWrite(P->Flags,6,1); // changed from flagged to NOT flagged

      } else {
        bitWrite(P->Flags,6,0); //no change (was not flagged, still is not flagged)

      }
        return false; //not flagged
    }
  }

  if (P->LastsnsValue>P->limitUpper || P->LastsnsValue<P->limitLower) lastflag = true;

  if (P->snsValue>P->limitUpper || P->snsValue<P->limitLower) {
    thisflag = true;
    bitWrite(P->Flags,0,1);

    //is it too high? write bit 5
    if (P->snsValue>P->limitUpper) bitWrite(P->Flags,5,1);
    else bitWrite(P->Flags,5,0);
  } 

  //now check for changes...  
  if (lastflag!=thisflag) {
    bitWrite(P->Flags,6,1); //change detected
    
  } else {
    bitWrite(P->Flags,6,0);
    
  }
  
  return bitRead(P->Flags,0);

}



uint8_t findSensor(byte snsType, byte snsID) {
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID == snsID && Sensors[j].snsType == snsType) return j; 
  }
  return 255;  
}

uint16_t findOldestDev() {
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
  snprintf(Sensors[k].snsName,20,"");
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
  time_t t = now(); // store the current time in time variable t
  ArduinoOTA.handle();

  server.handleClient();
  
  
  #ifdef _USELED
    LEDs.LED_update();
  #endif 

  if (OldTime[0] != second()) {
  
    OldTime[0] = second();
    //do stuff every second
    bool flagstatus=false;
    
    #ifdef _DEBUG
      Serial.printf("Time is %s. Sensor #1 is %s.   ",dateify(t,"hh:nn:ss"),Sensors[0].snsName);

    #endif

    for (byte k=0;k<SENSORNUM;k++) {
      bool goodread = false;

      if (Sensors[k].LastReadTime==0 || Sensors[k].LastReadTime>t || Sensors[k].LastReadTime + Sensors[k].PollingInt < t || t- Sensors[k].LastReadTime >60*60*24 ) goodread = ReadData(&Sensors[k]); //read value if it's time or if the read time is more than 24 hours from now in either direction
      
      if (goodread == true) {
        if (Sensors[k].LastSendTime ==0 || Sensors[k].LastSendTime>t || Sensors[k].LastSendTime + Sensors[k].SendingInt < t || bitRead(Sensors[k].Flags,6) /* isflagged changed since last read and this value was not sent*/ || t - Sensors[k].LastSendTime >60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
      }
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
    #ifdef _DEBUG
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

  snprintf(temp,29,"%s","Is Critical");
  strPad(temp,padder,25);
  currentLine += "<label for=\"Crit\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"Crit\" name=\"Critical\" value=\"" + String(bitRead(Sensors[j].Flags,7),DEC) + "\"><br>";  

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

  currentLine += "     ";
  currentLine +=  "Flags: ";
  temp[8] = 0;//make sure no characters after the first 8 shown
  Byte2Bin(Sensors[j].Flags,temp);
  currentLine +=  (String) temp + "<br>";



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

bool SendData(struct SensorVal *snsreading) {
  if (bitRead(snsreading->Flags,1) == 0) return false;
  
#ifdef _DEBUG
  Serial.printf("SENDDATA: Sending data from %s. \n", snsreading->snsName);
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


bool ReadData(struct SensorVal *P) {
      double val;

  time_t t=now();
  bitWrite(P->Flags,0,0);
  
    P->LastsnsValue = P->snsValue;

  switch (P->snsType) {
    case 1: //DHT temp
    {
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  (dht.readTemperature()*9/5+32);
      #endif
      break;
    }
    case 2: //DHT RH
    {
      #ifdef DHTTYPE
        //DHT Temp
        P->snsValue = dht.readHumidity();
      #endif
      break;
    }
    case 3: //soil
    {
        #ifdef _USESOILCAP
        //soil moisture v1.2
        val = analogRead(P->snsPin);
        //based on experimentation... this eq provides a scaled soil value where 0 to 100 corresponds to 450 to 800 range for soil saturation (higher is dryer. Note that water and air may be above 100 or below 0, respec
        val =  (int) ((-0.28571*val+228.5714)*100); //round value
        P->snsValue =  val/100;
      #endif

      #ifdef _USESOILRES
        //soil moisture by stainless steel probe unit (Resistance is characterized by a voltage signal, that varies between 0 (wet) and ~500 (dry) on a 1024 resolution scale 0 to 3.3v)
        
        val=0;
        uint8_t nsamp=100;
          digitalWrite(SOILDIO, HIGH);
          delay(100);
        
        for (uint8_t ii=0;ii<nsamp;ii++) {        
          val += analogRead(P->snsPin);
          
          delay(1); //wait X ms for reading to settle
        }
        digitalWrite(SOILDIO, LOW);
        val=val/nsamp;

        //convert val to voltage
        val = 3.3 * (val / _ADCRATE);

        //the chip I am using is a voltage divider with a 10K r1. 
        //equation for R2 is R2 = R1 * (V2/(V-v2))

        P->snsValue = (double) 10000 * (val/(3.3-val));



        #ifdef _USELED
          LEDs.LED_set_color_soil(P);
        #endif 

      #endif

      break;
    }
    case 4: //AHT Temp
    {
      #ifdef _USEAHT
        //AHT21 temperature
          val = aht21.readTemperature();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (100*(val*9/5+32))/100; 
            #ifdef _DEBUG
              Serial.print(F("AHT Temperature...: "));
              Serial.print(P->snsValue);
              Serial.println(F("F"));
            #endif
          }
          else
          {
            #ifdef _DEBUG
              Serial.print(F("AHT Temperature Error"));
            #endif
          }
      #endif
      break;
    }
    case 5: //AHT RH
    {
      //AHT21 humidity
        #ifdef _USEAHT
          val = aht21.readHumidity();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (val*100)/100; 
            #ifdef _DEBUG
              Serial.print(F("AHT HUmidity...: "));
              Serial.print(P->snsValue);
              Serial.println(F("%"));
            #endif
          }
          else
          {
            #ifdef _DEBUG
              Serial.print(F("AHT Humidity Error"));
            #endif
          }
      #endif
      break;
    }
    case 7: //dist
    {
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
    }
    case 9: //BMP pres
    {
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

          if (LAST_BAR_READ+60*60 < t) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = t;          
          }
        #endif

      #endif
      break;
    }
    case 10: //BMP temp
    {
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
            snprintf(WEATHER,22,"Poor Weather");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 6) {
            P->snsValue = 2;
            snprintf(WEATHER,22,"Strong Winds");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 10) {
            P->snsValue = 3;        
            snprintf(WEATHER,22,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 1.1 && BAR_HX[0] <= 1009) {
            P->snsValue = -1;
            snprintf(WEATHER,22,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1023) {
            P->snsValue = -2;
            snprintf(WEATHER,22,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1009) {
            P->snsValue = -3;
            snprintf(WEATHER,22,"Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 6 && BAR_HX[0] <= 1009) {
            P->snsValue = -4;
            snprintf(WEATHER,22,"Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 7 && BAR_HX[0] <= 990) {
            P->snsValue = -5;
            snprintf(WEATHER,22,"Very Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 10 && BAR_HX[0] <= 1009) {
            P->snsValue = -6;
            snprintf(WEATHER,22,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[11]-BAR_HX[0] >= 8 && BAR_HX[0] <= 1005) {
            P->snsValue = -7;
            snprintf(WEATHER,22,"TStorm");
          }
          if (BAR_HX[23]-BAR_HX[0] >= 24) {
            P->snsValue = -8;
            snprintf(WEATHER,22,"BOMB");
          }
        }
      #endif
      break;
    }
    case 13: //BME pres
    {
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

          if (LAST_BAR_READ+60*60 < t) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = t;          
          }
        #endif
      #endif
      break;
    }
    case 14: //BMEtemp
    {
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
  }

  checkSensorValFlag(P); //sets isFlagged
  P->LastReadTime = t; //localtime
  

#ifdef _DEBUG
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

char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  snprintf(holder,4,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  snprintf(holder,4,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  snprintf(holder,4,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  snprintf(holder,4,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  snprintf(holder,4,"%02d",hour(t));
  dateformat.replace("hh",holder);

  snprintf(holder,4,"%02d",minute(t));
  dateformat.replace("nn",holder);

  snprintf(holder,4,"%02d",second(t));
  dateformat.replace("ss",holder);
  
  snprintf(DATESTRING,19,"%s",dateformat.c_str());
  
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
