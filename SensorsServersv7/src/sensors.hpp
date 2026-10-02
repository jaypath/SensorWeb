#include "device_roles.hpp"
#if _HAS_LOCAL_SENSORS
#ifndef SENSORS_HPP
#define SENSORS_HPP



#include <Arduino.h>

// Forward declarations - avoid circular includes since globals.hpp includes this file
struct STRUCT_CORE;
struct STRUCT_PrefsH;
struct ArborysDevType;
struct ArborysSnsType;
class Devices_Sensors;

//  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=LowPower, RMB3-derived/calculated  value, RMB4 =  Outside sensor, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is critical and monitored - alert if it expires after time limit specified)
// Prefs.SNS_FLAGS is uint16_t: bits 0-7 mirror runtime Flags; bit 8 = auto-zero for scaled sensors (SNS_FLAG_BIT_AUTOZERO)
#define SNS_FLAG_BIT_AUTOZERO 8

  /*sens types
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - - ADS1115 reading NTC thermistor , requires _THERMISTOR_B0, _THERMISTOR_R0 (nominal resistance at 25C), _THERMISTOR_RKNOWN (resistance of resisor in series with NTC), _THERMISTOR_TKNOWN (temperature at known resistance), _THERMISTOR_VDD (supply voltage)   
//7 - distance, HC-SR04 or tfluna 
//8 - 
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
21 - 
30 -

50 - HVAC, total heating time (use for a multizone system) (ie heat on)
51 - HVAC, Heat zone 
52 - HVAC, Heat fan  
53 - HVAC, Heat pump on
55 = HVAC, total cooling time
56 = HVAC, AC/heatpump Compressor on
57 = HVAC, AC/heatpump fan on
58 = HVAC, dehumidifer on
59 = HVAC, humidifier on

60 -  battery power
61 - battery %
62 - battery voltage, ads1115
70 - leak yes/no (DIO; same pin/pull encoding as 71). Value HIGH=1 LOW=0. Alarms use Prefs.SNS_LIMIT_MAX / SNS_LIMIT_MIN: value>MAX or value<MIN. MAX=0 MIN=0 → HIGH alarms; MAX=1 MIN=1 → LOW alarms; MAX=1 MIN=0 → never.
71 - any binary DIO, 1=high/on, 0=low/off. snsPin is the GPIO (0-99 analog encoding or 200-299 digital). powerPin is pull config, not a rail: -9999/-1 ignore (INPUT, idle LOW); -100 INPUT_PULLDOWN idle LOW; -99 INPUT_PULLUP idle HIGH. Same Prefs.SNS_LIMIT_MAX / SNS_LIMIT_MIN alarm rules as type 70.
SendingInt 0 (any sensor) = transmit only on alarm-status change (Flags bit 6) or hub/user request; one send after first read so hubs can register the sensor.
72 - any binary DIO, 0=high/on, 1=low/off. snsPin is the GPIO (0-99 analog encoding or 200-299 digital). powerPin is pull config, not a rail: -9999/-1 ignore (INPUT, idle LOW); -100 INPUT_PULLDOWN idle LOW; -99 INPUT_PULLUP idle HIGH. Same Prefs.SNS_LIMIT_MAX / SNS_LIMIT_MIN alarm rules as type 70.
73 - timer countdown DIO OUTPUT. snsValue is remaining seconds (>0 → DIO HIGH, else LOW). Each poll subtracts poll_interval seconds (min 0). Flags bit0 mirrors DIO state. Poll 0 = never update. Default poll 1s.
74 - inverted timer countdown DIO OUTPUT (same as 73 but opposite DIO polarity when implemented).
75 - clock-window DIO OUTPUT. limitMin = on time, limitMax = off time (local hour 0–23; -1=dawn, -2=dusk via type-100 sunAck).
     DIO HIGH while now is in [on, off) (wraps midnight if on>off). snsValue 0=LOW / 1=HIGH; Flags bit0 mirrors DIO. No IRQ.
80-89 network monitor sensors (sns/power pins ignored)
80 WiFi RSSI (dBm) from STRUCT_CORE I; snsID 1=current, 2=low, 3=high — universal, no _USENETWORKMONITOR
81-89 network monitor tests (_USENETWORKMONITOR): 81 AP switch count, 82 local IP change count,
83 DNS resolution (ms), 84 HTTP Tx failures, 85 gateway ping avg RTT (ms), 86 gateway ping jitter (ms),
87 external ping avg RTT (ms), 88 external ping jitter (ms), 89 download speed (Mbps)
98 - clock
99 = any numerical value
100-150 - server type sensors, to which other sensors will send their data
100 - weather display server with local persistent storage (ie SD card)
200-255 - interrupt-driven DIO sensors (_USEINTERRUPT=1). Implementation: src/interrupt_triggers.hpp/.cpp.
     Poll interval: activity decimal refresh / daily reset; 0 = never run sensor update.
     snsValue = daily integer count + .1 if triggered within last poll_interval, else .0.
     Limits: fractional recent activity is HIGH. MAX≠0 MIN=0 → alarm while recent (default);
     MAX≠0 MIN≠0 → alarm when idle; MAX=0 MIN=0 → never; MAX=0 MIN≠0 is swapped.
200 - human presence (RCWL-0516). Rising edge IRQ. _PIN_ENABLE_RCWL (GPIO, driven HIGH at setup),
     _RCWL_ASSOCIATED_SNS (prefs index of type 73 timer). Edges within poll_interval are ignored (no count/timer).
     When Lights are on and remaining snsValue < 120, adds poll_interval+5 to Lights.
220 - momentary button. Rising edge IRQ with debounce; falling edge ignored. _BUTTON_ASSOCIATED_SNS (prefs index).
     If associated Flags bit0 is on → set associated snsValue=0 (not counted). If associated snsValue<=0 → arm to this
     button's poll_interval (counted).
*/

// Type 71 powerPin values (not a GPIO rail)
#define SNS_DIO_PULLDOWN (-100)
#define SNS_DIO_PULLUP (-99)

    #ifdef _USEMUX
      //using CD74HC4067 mux. this mux uses 4 DIO pins to choose one of 16 lines, then outputs to 1 ESP pin
      //36 is first pin from EN, and the rest are consecutive
      const uint8_t MUXPINS[5] = {32,33,25,26,36}; //DIO pins to select from 15 channels [0 is 0 and [1111] is 15]  and 5th line is the reading (goes to an ADC pin). So 36 will be Analog and 32 will be s0...            
    #endif

    #ifdef _USEHVAC //this is any number

      #ifdef _USEHEAT
        //  const uint8_t DIO_INPUTS=6; //6 sensors
          #ifdef _USEMUX
            const uint8_t HEATPINS[4] = _HEATPINS; //if using MUX, then the pin count is always 4, regardless of the number of zones
          #else
            const uint8_t HEATPINS[_USEHEAT] = _HEATPINS; //what are the pins of heat zones? ADC bank 1, starting from pin next to EN
          #endif
          const String HEATZONE[_USEHEAT] = _HEATZONES;

      #endif

      

    #endif


    
    #ifdef _USEHCSR04
      #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
      #define TRIGPIN 2
      #define ECHOPIN 3
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
  #include <Wire.h>
  #include <AHTxx.h>
#endif

#ifdef _USEADS1115
  #include <Adafruit_ADS1X15.h>
#endif

#ifdef _USEAHTADA
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#endif

#ifdef _USEBMP
//  #include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

#endif

#ifdef _USEBME
  #include <Wire.h>
  #include <Adafruit_Sensor.h>
  #include <Adafruit_BME280.h>

#endif


#ifdef _USETFLUNA
  #include "TFLuna.hpp"
#endif

#ifdef _ISCLOCK480X480
  #include "Clock480X480.hpp"
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


int8_t ReadData(struct ArborysSnsType *P, bool forceRead=false, bool uncalibrated=false);
// Type 200: nonzero limit = HIGH, zero = LOW. Swaps invalid MAX=0/MIN≠0. Returns true if swapped.
bool normalizeHumanPresenceLimits(double& limitHigh, double& limitLow);
void applyAlarmFlags(ArborysSnsType* P, double limitHigh, double limitLow, uint8_t lastflag);
bool sensorUsesScaling(uint8_t snsType);
float readResistanceDivider(float R1, float Vsupply, float Vread);
float readVoltageDivider(float R1, float R2, ArborysSnsType* P, byte avgN=1);
void setupSensors();
#if _USEINTERRUPT
void serviceInterruptSensors();
#endif
double peak_to_peak(int16_t pin, int ms = 50);
void initHardwareSensors();
uint8_t getPinType(int16_t pin, int8_t* correctedPin);
int8_t readAllSensors(bool forceRead=false);
float readAnalogVoltage(ArborysSnsType* P, byte nsamps);
float readAnalogVoltage(int16_t pin, byte nsamps=1);
float readPinValue(ArborysSnsType* P, byte nsamps);
float readPinValue(int16_t pin, byte nsamps, int16_t powerPin=-1);
void togglePowerPin(int16_t powerPin, bool on);
bool readADS1115(byte avgN, ArborysSnsType* P);
int8_t findSnsHistoryIndex(ArborysSnsType* P);

#ifdef _USEMUX
double readMUX(int16_t pin, byte nsamps);
#endif
#ifdef _USEBME680
  void read_BME680();
#endif


  //create a struct type to hold sensor history
  struct STRUCT_SNSHISTORY {
    int16_t sensorIndex[_SENSORNUM]; //index to the sensor array
    //uint32_t PrefsSensorIDs[_SENSORNUM]; //Prefs based sensor ID, which is devID<<16 + snsType<<8 + snsID
    uint8_t HistoryIndex[_SENSORNUM] = {0}; //point in array that we are at for each sensor's history
    uint8_t PrefsIndex[_SENSORNUM]; //index to the Prefs array for the sensor
    uint32_t TimeStamps[_SENSORNUM][_SENSORHISTORYSIZE] = {0};
    double Values[_SENSORNUM][_SENSORHISTORYSIZE] = {0};
    uint8_t Flags[_SENSORNUM][_SENSORHISTORYSIZE] = {0};

    bool recordSentValue(ArborysSnsType *S);
    int16_t getSensorHistoryIndex(ArborysSnsType *S);
    int16_t getSensorHistoryIndex(int16_t index);
  };


  #ifdef _USELED
  #include "LEDGraphics.hpp"
  #endif

#endif
#endif