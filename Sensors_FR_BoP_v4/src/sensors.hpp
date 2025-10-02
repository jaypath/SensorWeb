#ifndef SENSORS_HPP
#define SENSORS_HPP

#include <Arduino.h>
#include "globals.hpp"
#include "timesetup.hpp"
#include "Devices.hpp"
#include "server.hpp"


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

int8_t ReadData(struct SnsType *P, bool forceRead=false);
float readVoltageDivider(float R1, float R2, uint8_t snsPin, float Vm=3.3, byte avgN=1);
void setupSensors();
bool checkSensorValFlag(struct SnsType *P);
int peak_to_peak(int pin, int ms = 50);


#ifdef _USESSD1306
void redrawOled(void);
#endif

#ifdef _USEBME680
  void read_BME680();
#endif



#endif