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

//  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored (send on interval), RMB2=LowPower, RMB3-derived/calculated  value, RMB4 =  Outside sensor, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = send now, RMB7 = Critical: send on bounds or expiry change, either direction
// Prefs.SNS_FLAGS is uint16_t: bits 0-7 mirror runtime Flags; bit 8 = auto-zero for scaled sensors (SNS_FLAG_BIT_AUTOZERO)
#define SNS_FLAG_BIT_AUTOZERO 8

  /* Sensor types (uint8_t). This is the only type table. Device types are separate:
     1-99 peripheral, 100-150 server (_MYTYPE / devType). Do not widen snsType.
     makeSensorID packs type in 8 bits: (deviceIndex << 16) + (snsType << 8) + snsID.

     0      undefined / off
     1-20   hardware sensors. Do not renumber.
            1 DHT temp, 2 DHT RH, 3 soil, 4 AHT temp, 5 AHT RH,
            6 ADS1115 NTC, 7 distance, 9 BMP pressure, 10 BMP temp, 11 BMP altitude,
            12 pressure prediction, 13-16 BME, 17-20 BME680 (20 gas, kept even if unused)
     21-59  more hardware-specific sensors (33-35 soil variants live here).
            47 Bryant current defrost minutes. 48 current cool minutes.
                49 daily heat minutes. These stay on this device.
     60-98  generic voltage surrogates, not tied to one chip. 60-63 battery stay.
            66 Bryant daily defrost minutes. 67 daily cool minutes. Not batteries.
     99     voltage surrogate, unspecified
     100-149 DIO stand-ins for a physical thing. 100 is the unspecified DIO.
            101 leak (was 70). 102 binary, high=on (was 71). 103 binary, low=on (was 72).
            110 human presence, IRQ (was 200). 111 momentary button, IRQ (was 220).
            120 HVAC call. Name is AC, heat, or fan. Active-low (or a short sample of a
                rippling optocoupler). snsValue is minutes on. Flag bit0 is on now.
            121 solenoid / valve. Not an HVAC call, so HVAC total ignores it.
                A voltage that means open/closed still uses this type.
     150-169 calculated or remote. The name is the meaning. Permanent assignments:
            151 clock, unix time (was 98)
            152 WiFi RSSI (was 80). 153-161 network tests (were 81-89)
            162 Timer_On_H. 1 while local time is inside [limitMin, limitMax), else 0.
                0-23 = hour, -1 = dawn, -2 = dusk. Wraps midnight when on is after off.
            163 HVAC total. Adds minutes while any type-120 call row is flagged.
            164 Bryant outdoor operating mode. 165 outside °F. A fresh Bryant
                outdoor frame, else the newest fresh temperature on a non-server
                device (OAT checkboxes, or an outside-flagged temperature on a
                registered peripheral), else the newest fresh outside temperature
                on a registered weather server, preferring that server's outdoor
                temperature aggregate. 166 heat setpoint °F. 167 Connex master °F.
                168 Connex master RH. 169 current heat-pump run minutes, kept
                through defrost. 47, 48, 49, 66, 67, and 169 are local only
                (Monitored and Critical off). Daily totals reset at local midnight.
                A reboot restores today's totals from the SD history.
     170-219 actuators. 170 power switch (rule true → pin high). 171 countdown (was 73).
            172 inverted countdown (was 74).
            173 aggregate, no GPIO. Ruleset avg, min, max, or any. Optional
            :category and :indoor or :outdoor (flag bit 4). Checked sensors are
            the user's choice, including a type outside the group. With nothing
            saved yet, a full hub averages that group. :outdoor keeps only
            sensors flagged outside. Without it, those are left out. A saved
            empty list stays NAN. An average drops a reading outside a physical
            range, and for temperature, humidity, and pressure a reading more
            than 2 standard deviations from the mean of the other plausible
            values. Min, max, and any do not. A mixed average flags a discarded
            reading. An average of one class leaves a bad member out, including
            one outside range or far from the others, and alarms only from its
            limits. Poll uses stored readings (send interval +
            25%). All stale → NAN. Interval broadcast only when monitored (flag bit 1).
            A request that names the sensor still returns it. Critical (bit 7)
            sends when the value crosses a limit or the expired state changes,
            in either direction, even if the sensor is not monitored.
            Weather hubs use the
            outdoor temperature, humidity, and pressure aggregates for the
            outside conditions on the display.
            174 hydronic recommendation, no GPIO. snsValue 1 recommends on.
                It is sent upstream. It does not drive a zone valve.
            175 temperature gap, no GPIO. min(actual - heat setpoint, 0).
                At or above the setpoint the value is 0. Below it, the value is
                negative degrees. A missing input is NAN. Ruleset gap:master,
                gap:up, or gap:oat names the actual temperature. Not a temperature,
                so an average does not fold the gap back in.
     220-254 server sensor slots. Device type 100 (weather hub) is not a sensor type.
     255    extension sentinel. snsType2 is not in the struct yet. Do not send 255.
            When a wide type is added, the mesh packet and findSensor must carry it.
            uint16_t is enough for that field.

     Presence 110: daily count + 0.1 if active within the poll interval. RCWL enable pin
     _PIN_ENABLE_RCWL. Button 111: rising edge, debounce _INTERRUPT_DEBOUNCE_MS.
     Shed lights are actuator 171. Link 0 is the presence prefs index, link 1 is the
     button prefs index, and the actuator threshold is the motion-extend cap (120s).
     Button rising: if the countdown is on, clear it (not counted). If it is off, arm it
     to the button poll interval (counted). Motion while on and remaining under the cap
     adds poll_interval+5 seconds.
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
// Physical range for a reading. category (temperature, humidity, pressure, ...) wins when set.
// Types with no feasible range return true. NaN and infinity are never plausible.
bool sensorReadingIsPlausible(uint8_t snsType, double value, const char* category = nullptr);
bool sensorUsesScaling(uint8_t snsType);
float readResistanceDivider(float R1, float Vsupply, float Vread);
float readVoltageDivider(float R1, float R2, ArborysSnsType* P, byte avgN=1);
void setupSensors();
#if _USEINTERRUPT
void serviceInterruptSensors();
#endif
double peak_to_peak(int16_t pin, int ms = 50);
void initHardwareSensors();
// Force one local-sensor pass at the end of setup. Registration stamps timeRead,
// so the first loop would otherwise skip the poll and uplink the placeholder 0.
// Returns how many sensors produced a usable sample.
int8_t readLocalSensorsAtBoot();
// False when this device has not yet produced a sample worth averaging:
// never read, NaN, infinity, or a network test that has not run.
bool localSensorReadyToSend(const ArborysSnsType* S);
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