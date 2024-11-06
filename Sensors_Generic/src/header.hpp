#ifndef HEADER_HPP
#define HEADER_HPP

#include <Arduino.h>

//#define _DEBUG 1
//#define _WEBDEBUG

#define REBOOTDAILY 1 //if set, then will reboot at midnight daily 

//#define _WEBCHART 2
#ifdef _WEBCHART
  #define _NUMWEBCHARTPNTS 50
  const uint8_t SENSORS_TO_CHART[_WEBCHART] = {60,61}; //which sensors should be stored for charting?
#endif

const byte ASSIGNEDIP[4] = {0,168,68,0}; //assign here if this sensor has a dedicated IP.
#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here


#define ARDNAME "Heat" //unique name
#define SENSORNUM 7 //be sure this matches SENSORTYPES

const uint8_t SENSORTYPES[SENSORNUM] = {50,55,55,55,55,55,55};

const uint8_t MONITORED_SNS = 255; //from R to L each bit represents a sensor, 255 means all sensors are monitored
const uint8_t OUTSIDE_SNS = 0; //from R to L each bit represents a sensor, 255 means all sensors are outside

//#define _USEDHT 1
//#define _USEAHT 1
//#define _USEBMP  1
//#define _USEBME 1
//#define _USEBME680_BSEC 1
//#define _USEBME680 1
//#define _USESOILCAP 1
//#define _USESOILRES D5
//#define _USEBARPRED 1
//#define _USEHCSR04 1 //distance
//#define _USESSD1306  1
//#define _USELIBATTERY  A0 //set to the pin that is analogin
//#define _USESLABATTERY  A0 //set to the pin that is analogin
//#define _USELOWPOWER 36e8 //microseconds must also have _USEBATTERY
//#define _USELEAK 
//binary switches
//#define _CHECKAIRCON 1
#define _CHECKHEAT 1
//#define _USECALIBRATIONMODE 6 

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
21 - human present (mmwave)
50 - any binary, 1=yes/true/on
51 = any on/off switch
52 = any yes/no switch
53 = any 3 way switch
54 = 
55 - heat on/off {requires N DIO Pins}
56 - a/c  on/off {requires 2 DIO pins... compressor and fan}
57 - a/c fan on/off
58 - leak yes/no
60 -  battery power
61 - battery %
99 = any numerical value

*/



   /*
  FOR ESP32:
  //16-33 are valid pins, though not all are exposed. For example, 15 is usable but must be high at boot. 14 goes high at boot 
GPIO 0 (must be LOW to enter boot mode)
GPIO 2 (must be floating or LOW during boot)
GPIO 4
GPIO 5 (must be HIGH during boot)
GPIO 12 (must be LOW during boot)
GPIO 15 (must be HIGH during boot)

GPIO 1,3,5,6-11,14,15 are HIGH at boot
GPIO 2,4,5,12,13,14,15 support pullup and pulldown
  

GPIO21 is SDA and 22 is SCL
  GPIO2 (often labeled as "D2" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO4 (often labeled as "D4" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO5 (often labeled as "D5" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO12 (often labeled as "D12" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO13 (often labeled as "D13" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO14 (often labeled as "D14" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO15 (often labeled as "D15" on development boards) - Supports both internal pull-up and pull-down resistors.
GPIO25 - Supports internal pull-up resistor.
GPIO26 - Supports internal pull-up resistor.
GPIO27 - Supports internal pull-up resistor

ADC pins are labeled as their GPIO. NOTE: ADC bank 2 cannot be used with wifi, use bank 1 instead (which is pins 36, 39, 34, 35,32,33 numbered starting from the pin next to EN)
*/

/* for ESP8266 12e
D0 is GPIO 16, and is used for wake. Does not support interrupt. must be pulled high (along with EN) at start. connect 16 to EN for low power state
D1 is GPIO5 and is SCL
D2 is GPIO4 and is SDA
D3 is GPIO0 and is connected to flash. It is pulled high internally, so should not be used as read (ok for output). if low will not boot
D4 is GPIO2 and is connected to LED. It is pulled high internally, so should not be used as read (ok for output). if low will not boot
D5 is gpio14 and is SCLK in spi
D6 is GPIO12 and is MISO
D7 is GPIO13 and is MOSI
D8 is GPIO15 and is pulled to GND. Can be used as CS, but will not boot if pulled high


*/

//for calibrating current sensor
#ifdef _USECALIBRATIONMODE
  #define _NUMWEBCHARTPNTS 50
  const uint8_t SENSORS_TO_CHART[_USECALIBRATIONMODE] = {36, 39, 34, 35,32,33}; //which pins should be stored for charting?

#endif

#ifdef _USELEAK
  #define _LEAKPIN 12
  #define _LEAKDIO 13
#endif

#ifdef _USESOILRES
  #define SOILRESISTANCE 4700
  #define SOILR_MAX 2000
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0; use A4 or 32 for esp32 
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


#ifdef _CHECKAIRCON 
  const uint8_t DIO_INPUTS=2; //two pins assigned
  const uint8_t DIOPINS[2] = {34,35}; //comp then fan
#endif

#ifdef _CHECKHEAT
//  const uint8_t DIO_INPUTS=6; //6 sensors
  const uint8_t DIOPINS[6] = {36, 39, 34, 35,32,33}; //ADC bank 1, starting from pin next to EN
  const String HEATZONE[6] = {"Office","MastBR","DinRm","Upstrs","FamRm","Den"};
#endif

#if defined(_CHECKHEAT) || defined(_CHECKAIRCON)
  #define _HVACHXPNTS 24
#endif



#ifdef _USEDHT

  #define DHTTYPE    DHT11     // DHT11 or DHT22
  #define DHTPIN 2
#endif


//automatically detect arduino type
#if defined (ARDUINO_ARCH_ESP8266)
  #define _USE8266 1
  #define _ADCRATE 1023
#elif defined(ESP32)
  #define _USE32
  #define _ADCRATE 4095
#else
  #error Arduino architecture unrecognized by this code.
#endif

// warnings
#if defined(_CHECKAIRCON) && defined(_CHECKHEAT)
  #error "Check air con and check heat cannot both be defined."
#endif

#if defined(_USECALIBRATIONMODE) && defined(_WEBCHART)
  #error "CANNOT enable both webchart and usecalibration."
#endif


#endif
