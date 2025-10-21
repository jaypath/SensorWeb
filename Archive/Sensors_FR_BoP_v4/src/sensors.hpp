#ifndef SENSORS_HPP
#define SENSORS_HPP
#ifndef _ISPERIPHERAL
#error "Peripheral devices require _ISPERIPHERAL to be defined in globals.hpp."
#endif


#include <Arduino.h>
#include "globals.hpp"
#include "timesetup.hpp"
#include "Devices.hpp"
#include "server.hpp"



//note that extra values here will be considered an error.
/*
  #define SENSORTYPES              {3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //list of each sensorytype in order. can have zeros for unused sensors, but must init all to the correct sensortypes
  #define MONITORINGSTATES         {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1} //each element indicates monitoring state for the ith sensor, 0 = not monitored (not sent), 1 = monitored non-critical (take no action and expiration is irrelevant), 2 = monitored moderately critical (flag if out of bounds, but not if expired), 3 = monitored critically critical (flag if out of bounds or expired)
  #define OUTSIDESTATES            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //each element indicates outside state for the ith sensor, with 1 means outside, 0 means not outside
  #define INTERVAL_POLL            {300,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30} //each ith value is the seconds between polls
  #define INTERVAL_SEND            {1200,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120} //each ith value is the seconds between sends
  #define LIMIT_MAX                {550,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //each ith value is the max  for each sensor in NVS
  #define LIMIT_MIN                {50,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //each ith value is the min values for each sensor in NVS
*/
#define SENSORTYPES              {3} //can have zeros for unused sensors, but must init all to the correct sensortypes
#define MONITORINGSTATES         {1} //each element indicates monitoring state for the ith sensor, 0 = not monitored (not sent), 1 = monitored non-critical (take no action and expiration is irrelevant), 2 = monitored moderately critical (flag if out of bounds, but not if expired), 3 = monitored critically critical (flag if out of bounds or expired)
#define OUTSIDESTATES            {0} //each element indicates outside state for the ith sensor, with 1 means outside, 0 means not outside
#define INTERVAL_POLL            {300} //each ith value is the seconds between polls
#define INTERVAL_SEND            {1200} //each ith value is the seconds between sends
#define LIMIT_MAX                {550} //each ith value is the max  for each sensor in NVS
#define LIMIT_MIN                {50} //each ith value is the min values for each sensor in NVS


  #define _USELED 12 //uncomment this to use an LED, value is the pin number

   //uncomment the devices attached!
  //note: I2c on esp32 is 22=scl and 21=sda; d1=scl and d2=sda on nodemcu
    //#define _USEDHT 1 //specify DHT pin
    //#define _USEAHT 1 //specify AHT I2C
    //#define _USEAHTADA 0x38 // with bmp combined
    //#define _USEBMP  0x77 //set to 0x76 for stand alone bmp, or 0x77 for combined aht bmp
    //#define _USEBME 1 //specify BME I2C
    //#define _USEBME680_BSEC 1 //specify BME680 I2C with BSEC
    //#define _USEBME680 1 //specify BME680 I2C
    //#define _USEPOWERPIN 12 //specify the pin being used for power
    //#define _USESOILMODULE A0 //specify the pin being read for soil moisture
    #define _USESOILRES 33//use soil resistnace, pin and power specified later
    //#define _USESOILCAP 1 //specify soil capacitance sensor pin
    //#define _USEBARPRED 1 //specify barometric pressure prediction
    //#define _USEHCSR04 1 //distance
    //#define _USETFLUNA 1 // distance
    //#define _USESSD1306  1
    //#define _USELIBATTERY  A0 //set to the pin that is analogin
    //#define _USESLABATTERY  A0 //set to the pin that is analogin
    //#define _USELOWPOWER 36e8 //microseconds must also have _USEBATTERY
    //#define _USELEAK 1 //specify the pin being used for leak detection
    //binary switches
    //#define _CHECKAIRCON 1
    //#define _CHECKHEAT 1 //check which lines are charged to provide heat
    //#define _USERELAY 6 //specify the pin being used for relay, or the mux address if using mux
    //#define _USEMUX //use analog input multiplexor to allow for >6 inputs
    //#define _USECALIBRATIONMODE 6 //testing mode

  //#define _ISHVAC 1
  /*sens types
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - 
//7 - distance, HC-SR04 or tfluna 
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
51 = any on/off switch/relay (for example, relay on at 5v)
52 = any yes/no switch
53 = total heating time (use for a multizone system)
54 = any ac on/off switch (for example, 24v driven relay)
55 - heat on/off {requires N DIO Pins}
56 - a/c  on/off {requires 2 DIO pins... compressor and fan}
57 - a/c fan on/off
58 - leak yes/no
60 -  battery power
61 - battery %
98 - clock
99 = any numerical value
100+ is a server type sensor, to which other sensors will send their data
101 - weather display server with local persistent storage (ie SD card)
102 = any weather server that has no persistent storage
103 = any server with local persistent storage (ie SD card) that uploads data cloud storage
104 = any server without local persistent storage that uploads data cloud storage
*/


    #ifdef _USEMUX
      //using CD74HC4067 mux. this mux uses 4 DIO pins to choose one of 16 lines, then outputs to 1 ESP pin
      //36 is first pin from EN, and the rest are consecutive
      const uint8_t MUXPINS[5] = {32,33,25,26,36}; //DIO pins to select from 15 channels [0 is 0 and [1111] is 15]  and 5th line is the reading (goes to an ADC pin). So 36 will be Analog and 32 will be s0...            
    #endif

    #ifdef _ISHVAC
        #ifdef _CHECKAIRCON 
        //const uint8_t DIO_INPUTS=2; //two pins assigned
        const uint8_t ACPINS[4] = {35,34,39,36}; //comp DIO in,  fan DIO in,comp DIO out, fan DIO out
        #endif

        #ifdef _CHECKHEAT
        //  const uint8_t DIO_INPUTS=6; //6 sensors
          #ifdef _USEMUX
            const uint8_t HEATPINS[5] = {0,1,2,3,4,5}; //what are the mux addresses of heat zones?
          #else
            const uint8_t HEATPINS[6] = {36, 39, 34, 35,32,33}; //what are the pins of heat zones? ADC bank 1, starting from pin next to EN
          #endif
          const String HEATZONE[6] = {"Office","MastBR","DinRm","Upstrs","FamRm","Den"};

        #endif

        #if defined(_CHECKHEAT) || defined(_CHECKAIRCON)
          #define _HVACHXPNTS 24
        #endif

      // warnings
      #if defined(_CHECKAIRCON) && defined(_CHECKHEAT)
        #error "Check air con and check heat cannot both be defined."
      #endif

    #endif

    #if defined(_USECALIBRATIONMODE) && defined(_WEBCHART)
      #error "CANNOT enable both webchart and usecalibration."
    #endif


    //for calibrating current sensor
    #ifdef _USECALIBRATIONMODE
      #define _NUMWEBCHARTPNTS 50
      const uint8_t SENSORS_TO_CHART[_USECALIBRATIONMODE] = {32,33,25,26,36}; //which pins should be stored for charting?
    #endif

    #ifdef _USELEAK
      #define _LEAKPIN 12
      #define _LEAKDIO 13
    #endif

    #ifdef _USESOILRES
      //using LM393 comparator and stainless probes. Here higher voltage is dryer, and roughly 1/2 Vcc is dry
      #define SOILR_MAX 1000 //%max resistance value (dependent on R1 choice)
      #ifdef _USE32
        const int SOILPIN = 32; // ESP32 can use any GPIO pin with certain limits - recommend to use a pin from ADC1 bank (ADC2 interferes with WiFi) - for example GPIO 36 which is VP or 32
      #endif
      #ifdef _USE8266
        const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0;
      #endif
      //const int SOILDIO = _USESOILRES;  // ESP8266 Analog Pin ADC0 = A0
    #endif


    #ifdef _USESOILRESOLD
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
      #define _OLEDTYPE &Adafruit128x64
      //#define _OLEDTYPE &Adafruit128x32
      //#define _OLEDINVERT 0
    #endif

    #ifdef _USEDHT

      #define DHTTYPE    DHT11     // DHT11 or DHT22
      
    #endif

    

#ifdef _USEBME680
  #include <Zanshin_BME680.h>
#endif

#ifdef _USEBME680_BSEC
  #include "bsec.h"  
#endif

#ifdef DHTTYPE
  #include <Adafruit_Sensor.h>
  #include <DHT.h>

#endif

#ifdef _USEAHT
  #include <AHTxx.h>
#endif
#ifdef _USEAHTADA
  #include <Adafruit_AHTX0.h>
#endif

#ifdef _USEBMP
//  #include <Adafruit_Sensor.h>
  #include <Adafruit_BMP280.h>

#endif

#ifdef _USEBME
  #include <Wire.h>
  #include <Adafruit_BME280.h>

#endif

#ifdef  _USESSD1306
  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C
#endif


#ifdef _USETFLUNA
  #include <TFLI2C.h> // TFLuna-I2C Library v.0.1.0
  extern TFLI2C tflI2C;
  extern uint8_t tfAddr; // Default I2C address for Tfluna
  struct TFLunaType {
    uint8_t MIN_DIST_CHANGE = 2; //this is how many cm must have changed to register movement
    uint8_t BASEOFFSET=68; //the "zero point" from the mounting location of the TFLUNA to where zero is (because TFLUNA may be mounted recessed, or the zero location is in front of the tfluna)
    uint8_t ZONE_SHORTRANGE = 61; //cm from BASEOFFSET that is considered short range (show measures in inches now)
    uint8_t ZONE_GOLDILOCKS = 10; //cm from BASEOFFSET that are considered to have entered the perfect distance; 3 in ~ 8 cm
    uint8_t ZONE_CRITICAL = 4; //cm from BASEOFFSET at which you are too close
    uint32_t LAST_DRAW = 0; //last time screen ws drawn, millis()!
    int32_t LAST_DISTANCE=0; //cm of last distance
    bool INVERTED = false; //sjhould  the screen be inverted now?
    uint32_t SCREENRATE = 500; //in ms
    bool ALLOWINVERT=false; //if true, do inversions
    uint16_t CHANGETOCLOCK = 30; //in seconds, time to change to clock if dist hasn't changed
    bool CLOCKMODE = false; //show clock until distance change
    uint8_t TFLUNASNS=3; //sensor number of TFLUNA
    char MSG[20] = {0}; //screen message
  };

  extern TFLunaType LocalTF;
#endif


#ifdef _USEBARPRED
  extern  double BAR_HX[];
  extern char WEATHER[];
  extern uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif



#ifdef _WEBCHART
  struct SensorChart {
    uint8_t snsType;
    uint8_t snsNum;
//    double offset; //baseline to add
 //   double multiplier; //multiply by this
  //  uint8_t values[50]; //store 50 values... 2 days at hourly. To convert FROM double, value = (orig+offset)/multiplier. to convert TO double... orig = (value*multiplier)-offset
    double values[50];
    uint16_t interval; //save every interval seconds
    uint32_t lastRead; //last read time
  };

  extern SensorChart SensorCharts[_WEBCHART];
#endif

#if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
  struct HISTORY {
    uint32_t lastRead;
    uint16_t interval;
    double values[_HVACHXPNTS];
  };
  
  extern HISTORY HVACHX[];

  #ifdef _USECALIBRATIONMODE

    void checkHVAC(void);
  #endif
  extern uint8_t HVACSNSNUM;
#endif



#ifdef _USEBME680
  extern BME680_Class BME680;  ///< Create an instance of the BME680 class
  extern int32_t temperature, humidity, pressure, gas;
  extern uint32_t last_BME680 =0;
#endif

#ifdef _USEBME680_BSEC
  extern Bsec iaqSensor;
  
#endif


#ifdef DHTTYPE
  extern DHT dht; //third parameter is for faster cpu, 8266 suggested parameter is 11
#endif

#ifdef _USEAHT
extern AHTxx aht;
#endif

#ifdef _USEAHTADA
  extern Adafruit_AHTX0 aht;
#endif

#ifdef _USEBMP
extern  Adafruit_BMP280 bmp; // I2C

#endif

#ifdef _USEBME

extern  Adafruit_BME280 bme; // I2C

#endif

#ifdef  _USESSD1306

extern   SSD1306AsciiWire oled;
#endif

int8_t ReadData(struct SnsType *P, bool forceRead=false, int16_t mydeviceindex=-1);
float readVoltageDivider(float R1, float R2, uint8_t snsPin, float Vm=3.3, byte avgN=1);
void setupSensors();
bool checkSensorValFlag(struct SnsType *P);
int peak_to_peak(int pin, int ms = 50);
void initHardwareSensors();

#ifdef _USESSD1306
void redrawOled(void);
#endif

#ifdef _USEBME680
  void read_BME680();
#endif



#endif