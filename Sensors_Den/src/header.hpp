#ifndef HEADER_HPP
#define HEADER_HPP
#include <Arduino.h>

//#define _DEBUG 1

#define ARDNAME "Den" //unique name
#define SENSORNUM 3 //be sure this matches SENSORTYPES
//#define ARDID 94 //unique arduino ID //deprecated - now ardid is last 3 of wifi IP. if this is defined it will override wifi id

const uint8_t MONITORED_SNS = 255; //from R to L each bit represents a sensor, 255 means all sensors are monitored
const uint8_t OUTSIDE_SNS = 0; //from R to L each bit represents a sensor, 255 means all sensors are outside

const uint8_t SENSORTYPES[SENSORNUM] = {3,4,5};

//#define DHTTYPE    DHT11     // DHT11 or DHT22
//#define DHTPIN 2
#define _USEAHT 1
//#define _USEBMP  1
//#define _USEBME 1
//#define _USEBME680_BSEC 1
//#define _USEBME680 1
//#define _USESOILCAP 1
#define _USESOILRES D5
//#define _USEBARPRED 1
//#define _USEHCSR04 1 //distance
//#define _USESSD1306  1

#ifdef _USESOILRES
  #define SOILRESISTANCE 4700
  #define SOILR_MAX 1000
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
  //const int SOILDIO = _USESOILRES;  // ESP8266 Analog Pin ADC0 = A0
#endif

#ifdef _USESOILCAP
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
#endif

#ifdef _USEHCSR04
  #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
  #define TRIGPIN 2
  #define ECHOPIN 3
#endif

#ifdef _USESSD1306
  //#define _OLEDTYPE &Adafruit128x64
  //#define _OLEDTYPE &Adafruit128x32
  //#define _OLEDINVERT 0
#endif


/*sens types
//0 - not defined
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
99 - any binary sensor

*/



#if defined (ARDUINO_ARCH_ESP8266)
  #define _USE8266 1
#elif defined(ESP32)
  #define _USE32
#else
  #error Arduino architecture unrecognized by this code.
#endif


#endif
