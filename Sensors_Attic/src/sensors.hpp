#ifndef SENSORS_HPP
#define SENSORS_HPP

#include <Arduino.h>
#include <header.hpp>
#include <timesetup.hpp>

#include <server.hpp>


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
  #include <Adafruit_BME280.h>

#endif

#ifdef  _USESSD1306
  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C
#endif

#ifdef _USEBARPRED
  extern  double BAR_HX[];
  extern char WEATHER[];
  extern uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif


struct SensorVal {
  uint8_t  snsType ;
  uint8_t snsID; //unique ID numnber for type. Example, if there are two type 55 sensors, first will have ID 0 and second will have ID 1... this their unique index is 55.0 and 55.1
  uint8_t snsPin;
  char snsName[32];
  double snsValue;
  double LastsnsValue;
  double limitUpper;
  double limitLower;
  uint16_t PollingInt;
  uint16_t SendingInt;
  uint32_t LastReadTime;
  uint32_t LastSendTime;  
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed status, RMB7 = was not flagged, but now is flagged
};

extern SensorVal Sensors[SENSORNUM];

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

int inArray(int arrind[], int N, int value);
bool ReadData(struct SensorVal *P);
byte find_limit_sensortypes(String snsname, byte snsType,bool highest);
byte find_sensor_count(byte snsType);
byte find_sensor_name(String snsname,byte snsType,byte snsID);
byte find_sensor_type(byte snsType,byte snsID=255);
float readVoltageDivider(float R1, float R2, uint8_t snsPin, float Vm=3.3, byte avgN=1);
uint8_t countFlagged(int snsType=0, uint8_t flagsthatmatter = B00000011, uint8_t flagsettings= B00000011, uint32_t MoreRecentThan=0);
uint8_t countDev();
void setupSensors();
void initSensor(int k);
uint16_t findOldestDev();
uint8_t findSensor(byte snsType, byte snsID);
bool checkSensorValFlag(struct SensorVal *P);
void pushDoubleArray(double arr[], byte, double);
void pushByteArray(byte arr[], byte, byte);
int peak_to_peak(int pin, int ms = 50);

#ifdef _USESSD1306
void redrawOled(void);
#endif

#ifdef _USEBME680
  void read_BME680();
#endif



#endif