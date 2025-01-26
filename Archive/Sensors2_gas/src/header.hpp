#ifndef HEADER_HPP
#define HEADER_HPP
#include <Arduino.h>

//#define _DEBUG 1
//#define _USE8266 1
#define _USE32

#define ARDNAME "GasTest"
#define ARDID 200 //unique arduino ID 
#define SENSORNUM 3 //be sure this matches SENSORTYPES

const uint8_t MONITORED_SNS = 255; //from R to L each bit represents a sensor, 255 means all sensors are monitored
const uint8_t OUTSIDE_SNS = 0; //from R to L each bit represents a sensor, 255 means all sensors are outside

const uint8_t SENSORTYPES[SENSORNUM] = {3,3,3};

//#define DHTTYPE    DHT11     // DHT11 or DHT22
//#define DHTPIN 2
//#define _USEBMP  1
//#define _USEAHT 1
//#define _USEBME 1
//#define _USEBME680_BSEC 1
//#define _USEBME680 1
//#define _USESOILCAP 1
#ifdef _USESOILCAP
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
#endif

#define _USESOILRES 5
#define SOILRESISTANCE 4700
#ifdef _USESOILRES
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
  //const int SOILDIO = _USESOILRES;  // ESP8266 Analog Pin ADC0 = A0
#endif
//#define _USEBARPRED 1
//#define _USESSD1306  1
//#define _OLEDTYPE &Adafruit128x64
//#define _OLEDTYPE &Adafruit128x32
//#define _OLEDINVERT 0
//#define _USEHCSR04 1 //distance
#ifdef _USEHCSR04
  #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
  #define TRIGPIN 2
  #define ECHOPIN 3
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



#endif
