#include "device_roles.hpp"
#if _HAS_LOCAL_SENSORS
#include "globals.hpp"
#include "hardware_fault.hpp"
#include "sensors.hpp"
#include <SensorEffectors.hpp>
#include "interrupt_triggers.hpp"
#include "actuators.hpp"
#include "bryant_bus.hpp"
#include <math.h>
#include <esp_task_wdt.h>

#if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
#include "NetworkMonitor.hpp"
#endif
#ifdef _USEUDP
#include "server.hpp"
#endif

/*sens types - see hpp file
*/


extern Devices_Sensors Sensors;
#if _HAS_LOCAL_SENSORS
STRUCT_SNSHISTORY SensorHistory;
#endif


#if defined(_USEHVAC) 
uint8_t HVACSNSNUM = 0;
#endif


#ifdef _USEBARPRED
  double BAR_HX[_USEBARPRED];
  char WEATHER[15]; //weather string, "sunny", "cloudy", "rain", "snow", "fog", "hail", "thunderstorm", "tornado", "other"
  uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif

#ifdef _WEBCHART
  SensorChart SensorCharts[_WEBCHART];
#endif



//  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=LowPower, RMB3-derived/calculated/predictive  value, RMB4 =  Outside sensor, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is critical and must be monititored (including if a reading is delayed, so I must provide sendingInt)


#ifdef _USEBME680
  BME680_Class BME680;  ///< Create an instance of the BME680 class
  int32_t temperature, humidity, pressure, gas;
  uint32_t last_BME680 =0;
#endif

#ifdef _USEBME680_BSEC
  Bsec iaqSensor;
  
#endif

#ifdef _USEADS1115
  Adafruit_ADS1115 ads;  ///< Create an instance of the ADS1115 class
  // Use the full-scale voltage range that best matches your expected input.
  // GAIN_TWOTHIRDS allows a maximum input voltage of +/- 6.144V
  //GAIN_ONE allows a maximum input voltage of +/- 4.096V
  //GAIN_TWO allows a maximum input voltage of +/- 2.048V

  #ifndef _USE_ADS_GAIN
    #define _USE_ADS_GAIN GAIN_TWO
  #endif
  #ifndef _USE_ADS_MULTIPLIER
    #define _USE_ADS_MULTIPLIER 0.0625F //gain multiplier for _USE_ADS_GAIN is 0.0625
  #endif

#endif


#ifdef DHTTYPE
  DHT dht(_USEDHT,DHTTYPE,11); //third parameter is for faster cpu, 8266 suggested parameter is 11
#endif

#ifdef _USEAHT
  AHTxx aht(AHTXX_ADDRESS_X38, AHT2x_SENSOR);  
#endif


#ifdef _USEAHTADA
  Adafruit_AHTX0 aht;  
#endif



#ifdef _USEBMP

  Adafruit_BMP280 bmp; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef _USEBME

  Adafruit_BME280 bme; // I2C
#endif



int16_t STRUCT_SNSHISTORY::getSensorHistoryIndex(ArborysSnsType *S) {
  if (S == NULL) return -1;
  if (S->IsSet == false) return -1;

  int16_t index = Sensors.findSensorByPointer(S);
  if (index < 0) return -1;
  return SensorHistory.getSensorHistoryIndex(index);
}

int16_t STRUCT_SNSHISTORY::getSensorHistoryIndex(int16_t index) {
  if (index < 0) return -1;


  for (int16_t i = 0; i < _SENSORNUM; i++) {
    if (SensorHistory.sensorIndex[i] == index) {
      return i;
    }
  }
  return -1;
}

// Type 200/70/71 reuse powerPin as pull config (-100 pulldown, -99 pullup), not a GPIO rail.
static bool isDioPullConfig(int16_t powerPin) {
  return powerPin == SNS_DIO_PULLDOWN || powerPin == SNS_DIO_PULLUP;
}

static bool isRealPowerPin(int16_t powerPin) {
  return powerPin != -9999 && powerPin != -1 && !isDioPullConfig(powerPin);
}

static bool isDioBinaryType(uint8_t snsType) {
  return snsType == SNS_LEAK || snsType == SNS_BINARY || snsType == SNS_BINARY_INV || snsType == SNS_VALVE;
}

static bool isInterruptDioType(uint8_t snsType) {
  return IS_INTERRUPT_SENSOR_TYPE(snsType);
}

static bool isTimerOutputType(uint8_t snsType) {
  return snsType == SNS_COUNTDOWN || snsType == SNS_COUNTDOWN_INV;
}

static bool isSwitchOutputType(uint8_t snsType) {
  return snsType == SNS_SWITCH;
}

static bool isDrivenDioOutputType(uint8_t snsType) {
  return isTimerOutputType(snsType) || isSwitchOutputType(snsType);
}

static bool humanPresenceLimitIsHigh(double v) {
  return !isnan(v) && v != 0.0;
}

bool normalizeHumanPresenceLimits(double& limitHigh, double& limitLow) {
  // Type 200: nonzero = HIGH, zero/NaN = LOW.
  // high=0 and low≠0 is invalid (would be the 70/71 polarity); swap.
  const bool highNz = humanPresenceLimitIsHigh(limitHigh);
  const bool lowNz = humanPresenceLimitIsHigh(limitLow);
  if (!highNz && lowNz) {
    const double tmp = isnan(limitHigh) ? 0.0 : limitHigh;
    limitHigh = limitLow;
    limitLow = tmp;
    return true;
  }
  return false;
}

bool sensorReadingIsPlausible(uint8_t snsType, double value, const char* category) {
  if (isnan(value) || isinf(value)) return false;
  auto inRange = [](double v, double lo, double hi) { return v >= lo && v <= hi; };

  // Household sensors are °F. -200 is a failed read. 0 °F can be real and is left for the spread check.
  if (category && strcasecmp(category, "temperature") == 0) return inRange(value, -80.0, 170.0);
  if (category && strcasecmp(category, "humidity") == 0) return inRange(value, 0.0, 100.0);
  if (category && strcasecmp(category, "pressure") == 0) return inRange(value, 800.0, 1100.0);
  if (category && strcasecmp(category, "soil") == 0) return inRange(value, 0.0, 100.0);
  if (category && (strcasecmp(category, "distance") == 0 || strcasecmp(category, "dist") == 0)) return inRange(value, 0.0, 40.0);
  if (category && strcasecmp(category, "altitude") == 0) return inRange(value, -500.0, 9000.0);

  if (Sensors.isSensorOfType(snsType, "temperature")) return inRange(value, -80.0, 170.0);
  if (Sensors.isSensorOfType(snsType, "humidity")) return inRange(value, 0.0, 100.0);
  if (Sensors.isSensorOfType(snsType, "pressure")) return inRange(value, 800.0, 1100.0);
  if (Sensors.isSensorOfType(snsType, "soil")) return inRange(value, 0.0, 100.0);
  if (Sensors.isSensorOfType(snsType, "distance")) return inRange(value, 0.0, 40.0);
  if (Sensors.isSensorOfType(snsType, "altitude")) return inRange(value, -500.0, 9000.0);
  if (snsType == 60 || snsType == 62) return inRange(value, 2.0, 5.5);   // Li-ion volts
  if (snsType == 61 || snsType == 63) return inRange(value, 6.0, 18.0);  // lead-acid volts
  if (snsType == 20) return value > 0.0 && value < 5000000.0;            // BME680 gas ohms
  if (snsType == SNS_NET_RSSI) return inRange(value, -120.0, 0.0);
  if (snsType == 12) return inRange(value, -10.0, 10.0);                 // weather code
  if (snsType == SNS_BINARY || snsType == SNS_BINARY_INV || snsType == SNS_LEAK || snsType == SNS_VALVE || snsType == SNS_SWITCH) {
    return value == 0.0 || value == 1.0;
  }
  return true;
}

static uint8_t s_limitCrossBits[(NUMSENSORS + 7) / 8];
static uint8_t s_critLimitBits[(NUMSENSORS + 7) / 8];
static bool s_critLimitSendNow = false;

static int16_t indexOfLocalSensor(const ArborysSnsType* S) {
  if (!S) return -1;
  for (int16_t i = 0; i < NUMSENSORS; ++i) {
    if (Sensors.snsIndexToPointer(i) == S) return i;
  }
  return -1;
}

void markMonitoredLimitCross(const ArborysSnsType* S) {
  int16_t i = indexOfLocalSensor(S);
  if (i < 0) return;
  s_limitCrossBits[i / 8] |= (uint8_t)(1u << (i % 8));
}

bool monitoredLimitCrossPending(int16_t snsIndex) {
  if (snsIndex < 0 || snsIndex >= NUMSENSORS) return false;
  return (s_limitCrossBits[snsIndex / 8] & (uint8_t)(1u << (snsIndex % 8))) != 0;
}

void clearMonitoredLimitCross(int16_t snsIndex) {
  if (snsIndex < 0 || snsIndex >= NUMSENSORS) return;
  s_limitCrossBits[snsIndex / 8] &= (uint8_t)~(1u << (snsIndex % 8));
}

void markCriticalLimitCross(const ArborysSnsType* S) {
  int16_t i = indexOfLocalSensor(S);
  if (i < 0) return;
  s_critLimitBits[i / 8] |= (uint8_t)(1u << (i % 8));
  s_critLimitSendNow = true;
}

bool criticalLimitCrossPending(const ArborysSnsType* S) {
  int16_t i = indexOfLocalSensor(S);
  if (i < 0) return false;
  return (s_critLimitBits[i / 8] & (uint8_t)(1u << (i % 8))) != 0;
}

void clearCriticalLimitCross(int16_t snsIndex) {
  if (snsIndex < 0 || snsIndex >= NUMSENSORS) return;
  s_critLimitBits[snsIndex / 8] &= (uint8_t)~(1u << (snsIndex % 8));
}

bool takeCriticalLimitSendNow() {
  bool due = s_critLimitSendNow;
  s_critLimitSendNow = false;
  return due;
}

void applyAlarmFlags(ArborysSnsType* P, double limitHigh, double limitLow, uint8_t lastflag) {
  if (!P) return;

  // Type 170/171/172: Flags bit0 is the pin state, not an alarm-from-limits bit.
  // Type 162 limits are the clock window, not alarm thresholds.
  if (isDrivenDioOutputType(P->snsType) || P->snsType == SNS_TIMER_ON_H) {
    if (bitRead(lastflag, 0) != bitRead(P->Flags, 0)) {
      // Pin or clock-window state. That is a normal send, so only Monitored queues it.
      if (bitRead(P->Flags, 1)) bitWrite(P->Flags, 6, 1);
      SensorEffectors_onAlarmChange(P->snsType, P->snsID, P->snsValue, P->Flags, lastflag);
    }
    return;
  }

  if (P->snsType == SNS_PRESENCE || P->snsType == SNS_BUTTON) {
    normalizeHumanPresenceLimits(limitHigh, limitLow);
    P->limitHigh = (float)limitHigh;
    P->limitLow = (float)limitLow;
    // Integer = daily count; fractional .1 = activity within last poll_interval.
    const double frac = P->snsValue - floor(P->snsValue);
    const bool recent = frac >= 0.05;
    const bool highNz = humanPresenceLimitIsHigh(limitHigh);
    const bool lowNz = humanPresenceLimitIsHigh(limitLow);
    bool alarm = false;
    if (highNz && !lowNz) {
      alarm = recent;       // default: trigger on recent activity
    } else if (highNz && lowNz) {
      alarm = !recent;      // both nonzero: trigger when idle
    }
    // both zero: never alarm
    bitWrite(P->Flags, 0, alarm ? 1 : 0);
    bitWrite(P->Flags, 5, recent ? 1 : 0);
  } else {
    // Alarm if value > SNS_LIMIT_MAX or value < SNS_LIMIT_MIN (strict).
    // DIO HIGH=1 LOW=0: MAX=0 MIN=0 → HIGH alarms; MAX=1 MIN=1 → LOW alarms; MAX=1 MIN=0 → never.
    if (P->snsValue > limitHigh || P->snsValue < limitLow) {
      bitWrite(P->Flags, 0, 1);
      bitWrite(P->Flags, 5, (P->snsValue > limitHigh) ? 1 : 0);
    } else {
      bitWrite(P->Flags, 0, 0);
    }
  }

  if (bitRead(lastflag, 0) != bitRead(P->Flags, 0)) {
    // In range ↔ out of range. Critical (bit 7) sends on this read, not at SendingInt.
    // Monitored waits for its normal interval, then the send must be acknowledged.
    if (bitRead(P->Flags, 7)) {
      bitWrite(P->Flags, 6, 1);
      markCriticalLimitCross(P);
    }
    if (bitRead(P->Flags, 1)) markMonitoredLimitCross(P);
    SensorEffectors_onAlarmChange(P->snsType, P->snsID, P->snsValue, P->Flags, lastflag);
  }
}

static void setupDioSwitchPin(int16_t snsPin, int16_t powerPin) {
  int8_t gpio = -1;
  const uint8_t pintype = getPinType(snsPin, &gpio);
  if (pintype == 0 || gpio < 0) return;

  if (powerPin == SNS_DIO_PULLDOWN) {
    pinMode(gpio, INPUT_PULLDOWN);
    digitalWrite(gpio, LOW);
  } else if (powerPin == SNS_DIO_PULLUP) {
    pinMode(gpio, INPUT_PULLUP);
    digitalWrite(gpio, HIGH);
  } else {
    pinMode(gpio, INPUT);
    digitalWrite(gpio, LOW);
  }
}

bool STRUCT_SNSHISTORY::recordSentValue(ArborysSnsType *S) {
  //get the hIndex, index to sensor history
  int16_t hIndex = SensorHistory.getSensorHistoryIndex(S);
  
  if (hIndex < 0 || hIndex >= _SENSORNUM || isTimeValid(S->timeRead) == false) return false;
  HistoryIndex[hIndex]++;
  if (HistoryIndex[hIndex] >= _SENSORHISTORYSIZE) HistoryIndex[hIndex] = 0;
  TimeStamps[hIndex][HistoryIndex[hIndex]] = S->timeRead;
  Values[hIndex][HistoryIndex[hIndex]] = S->snsValue;
  Flags[hIndex][HistoryIndex[hIndex]] = S->Flags;
  return true;
}


void setupSensors() {

SerialPrint("Sensors setup started",true);
#ifdef _USE32
#ifdef _USEADCATTEN
analogSetAttenuation(_USEADCATTEN);
#endif

#ifdef _USEADCBITS
  // Use analogReadResolution on every target: it guarantees analogRead() returns 0..(2^_USEADCBITS - 1),
  // which is what readAnalogVoltage() divides by. (analogSetWidth is a no-op on S2/S3/C3, which would
  // leave analogRead() at 12-bit and break the voltage scaling.)
  analogReadResolution(_USEADCBITS);  // e.g. 10, 11, or 12
#endif
#endif

String myname = String(Prefs.DEVICENAME);
if (myname == "") {
  #ifdef _MYNAME
    myname = _MYNAME;
  #else
    myname = "sensor" + String(ESP.getEfuseMac(), HEX);
  #endif
  snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", myname.c_str());
  Prefs.isUpToDate = false;
}


I.MY_DEVICE_INDEX = Sensors.findMyDeviceIndex(); //update my index
SerialPrint("MY_DEVICE_INDEX: " + String(I.MY_DEVICE_INDEX),true);

uint16_t flagstates[] = _FLAGSTATES;
uint16_t interval_poll[] = _INTERVAL_POLL;
uint16_t interval_send[] = _INTERVAL_SEND;
double limit_max[] = _LIMIT_MAX;
double limit_min[] = _LIMIT_MIN;
String sensornames[] = _SENSORNAMES;
byte sensortypes[] = _SENSORTYPES;
int16_t snsPins[] = _SNSPINS;
int16_t powerPins[] = _POWERPINS;

//if Prefs has values saved, use those. Otherwise, use the defaults
// Check each sensor individually and fill in missing values

  

for (byte i=0;i<_SENSORNUM;i++) {
  byte snsID = Sensors.countSensors(sensortypes[i],I.MY_DEVICE_INDEX)+1;

  #if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
       bool isVirtualSensor = (sensortypes[i] == SNS_NET_RSSI) || (sensortypes[i] == SNS_AGGREGATE)
           || (sensortypes[i] == SNS_TEMP_GAP) || NetworkMonitor.isSensorType(sensortypes[i]);
  #else
       bool isVirtualSensor = (sensortypes[i] == SNS_NET_RSSI) || (sensortypes[i] == SNS_AGGREGATE)
           || (sensortypes[i] == SNS_TEMP_GAP);
  #endif


  bool sensorNeedsDefaults = false;
  const int16_t sensorPin = isVirtualSensor ? (int16_t)-9999 : snsPins[i];
  const int16_t sensorPowerPin = isVirtualSensor ? (int16_t)-9999 : powerPins[i];

  //note that the ith sensor index is the same as the prefs index for the sensor... though I do not guarantee that this will always be the case.
  // Seed timeRead/timeLogged so local sensors are not immediately expired before the first read/send cycle.
  const uint32_t seedTime = (uint32_t)utcNow();
  // Check if this sensor's values are uninitialized (all zeros is invalid)
  if (Prefs.SNS_FLAGS[i] == 0 && Prefs.SNS_INTERVAL_POLL[i] == 0 && Prefs.SNS_INTERVAL_SEND[i] == 0) {
    sensorNeedsDefaults = true;
  }
  
  if (sensorNeedsDefaults) {
    Prefs.SNS_FLAGS[i] = flagstates[i];
    // Soil / scaled moisture sensors default auto-zero ON so scaled readings never go negative
    if (sensorUsesScaling(sensortypes[i])) {
      bitWrite(Prefs.SNS_FLAGS[i], SNS_FLAG_BIT_AUTOZERO, 1);
    }
    Prefs.SNS_LIMIT_MAX[i] = limit_max[i];
    Prefs.SNS_LIMIT_MIN[i] = limit_min[i];
    Prefs.SNS_INTERVAL_POLL[i] = interval_poll[i];
    Prefs.SNS_INTERVAL_SEND[i] = interval_send[i];
    Prefs.SNS_CALIB_MIN[i] = NAN;
    Prefs.SNS_CALIB_MAX[i] = NAN;
    Prefs.isUpToDate = false;
  }

  switch (sensortypes[i]) {
    
    case SNS_HVAC_TOTAL:
      {
      bitWrite(Prefs.SNS_FLAGS[i],3,1);
      break;
      }

#if defined(_USEBRYANT)
    case SNS_BRYANT_MODE:
    case SNS_BRYANT_OAT:
    case SNS_BRYANT_SETPOINT:
    case SNS_BRYANT_TEMP:
    case SNS_BRYANT_RH:
    case SNS_BRYANT_RUNTIME:
    case SNS_BRYANT_DEFROST:
    case SNS_BRYANT_RUN_COOL:
    case SNS_BRYANT_DAY_HEAT:
    case SNS_BRYANT_DAY_DEFROST:
    case SNS_BRYANT_DAY_COOL:
    case SNS_HYDRONIC_ZONE:
    case SNS_TEMP_GAP:
      bitWrite(Prefs.SNS_FLAGS[i], 3, 1);
      break;
#endif

    case 61: //battery percent
      {
        bitWrite(Prefs.SNS_FLAGS[i],3,1);
      break;
      }

    case SNS_NET_RSSI:
      break;

    case SNS_NET_FIRST:
    case 154:
    case 155:
    case 156:
    case 157:
    case 158:
    case 159:
    case 160:
    case SNS_NET_LAST:
      break;
    case SNS_PRESENCE:
    case SNS_BUTTON:
      if (normalizeHumanPresenceLimits(Prefs.SNS_LIMIT_MAX[i], Prefs.SNS_LIMIT_MIN[i])) {
        Prefs.isUpToDate = false;
      }
      break;

    case 90: //Sleep info
      {
        bitWrite(Prefs.SNS_FLAGS[i],7,0); bitWrite(Prefs.SNS_FLAGS[i],3,1); bitWrite(Prefs.SNS_FLAGS[i],1,0);
      break;
      }
  }

  // Send-rate 0: leave timeLogged unset so the first successful read is sent (hub registration).
  const uint32_t seedLogged = (Prefs.SNS_INTERVAL_SEND[i] == 0
#if _USEINTERRUPT
      || IS_INTERRUPT_SENSOR_TYPE(sensortypes[i])
#endif
      ) ? 0 : seedTime;
  SensorHistory.sensorIndex[i] = Sensors.addSensor(
      ESP.getEfuseMac(), WiFi.localIP(), sensortypes[i], snsID,
      String(myname + "_" + String(sensornames[i])).c_str(), 0, seedTime, seedLogged,
      Prefs.SNS_INTERVAL_SEND[i], Prefs.SNS_FLAGS[i], myname.c_str(), _MYTYPE,
      sensorPin, sensorPowerPin,
      (float)Prefs.SNS_LIMIT_MAX[i], (float)Prefs.SNS_LIMIT_MIN[i], true, true);
  //SensorHistory.SensorID[i] = Sensors.makeSensorID(SensorHistory.sensorIndex[i]); 
  SensorHistory.PrefsIndex[i] = i; //this is the index to the Prefs array for the sensor, at the start it is the same as sensorhistory index. In theory it might shift if a sensor were to be removed and then re-added. But since this is not currently implemented, it is not a problem.
  SensorHistory.HistoryIndex[i] = 0; //start at the beginning of the history array
  ArborysSnsType* added = Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[i]);
  if (added) {
    added->PollingInt = Prefs.SNS_INTERVAL_POLL[i];
  }

  int8_t correctedPin = -1;
  uint8_t pintype = 0;
  if (!isVirtualSensor) {
    //;pin number for the sensor, if applicable. 0-99 is anolog in pin, 100-199 is MUX address, 200-299 is digital in pin, 300-399 is SPI pin, 400-599 is an I2C address. Negative values mean the same, but that there is an associated power pin. -9999 means no pin.
    pintype = getPinType(snsPins[i], &correctedPin);
    if (isDrivenDioOutputType(sensortypes[i])) {
      if (correctedPin >= 0) {
        pinMode((uint8_t)correctedPin, OUTPUT);
        digitalWrite((uint8_t)correctedPin, LOW);
        if (added) bitWrite(added->Flags, 0, 0);
      }
    } else if (isDioBinaryType(sensortypes[i]) || isInterruptDioType(sensortypes[i])) {
      setupDioSwitchPin(snsPins[i], powerPins[i]);
    } else if (pintype != 0 && pintype <= 4) {
      pinMode(correctedPin, INPUT);
    }
  }

#if _USEINTERRUPT
  if (added && IS_INTERRUPT_SENSOR_TYPE(sensortypes[i])) {
    InterruptTriggers_setup(added, correctedPin);
  }
#endif
}


#if defined(_USEMUX) 
pinMode(MUXPINS[0],OUTPUT);
pinMode(MUXPINS[1],OUTPUT);
pinMode(MUXPINS[2],OUTPUT);
pinMode(MUXPINS[3],OUTPUT);
pinMode(MUXPINS[4],INPUT);
digitalWrite(MUXPINS[0],HIGH);
digitalWrite(MUXPINS[1],HIGH);
digitalWrite(MUXPINS[2],HIGH);
digitalWrite(MUXPINS[3],HIGH); //set to last mux channel by default
#endif


#if defined(_USEHVAC)

  #if defined(_USEHEAT) && !defined(_USEMUX)
    for (byte i=0;i<_USEHEAT;i++) {
      pinMode(HEATPINS[i],INPUT);
    }
  #endif
#endif

  #if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
    NetworkMonitor.init();
  #endif


  #ifdef _USETFLUNA
  Matrix_Init();
  LocalTF.TFLUNASNS = Sensors.findSensor(I.MY_DEVICE_INDEX,7,1);
  LocalTF.LAST_DRAW = Matrix_Draw(false, "INIT");
  #endif


#if defined(_USEBRYANT)
  bryantBusBegin();
#endif

  SensorEffectors_init();
  SerialPrint("Sensors setup complete",true);
}

double peak_to_peak(int16_t pin, int ms) {
  pinMode(pin, INPUT);
  //check n (samples) over ms milliseconds, then return the max-min value (peak to peak value) 

  if (ms==0) ms = 50; //50 ms is roughly 3 cycles of a 60 Hz sin wave
  
  double maxVal = 0;
  double minVal=6000;
  uint32_t buffer = 0;
  uint32_t t0;
  

  t0 = millis();

  while (millis()<=t0+ms) { 

    buffer = readPinValue(pin, 1);
    if (maxVal<buffer) maxVal = buffer;
    if (minVal>buffer) minVal = buffer;

  }
  

  return ((double)maxVal-minVal)/1000; //return value in volts

}

// Active-low DIO, including a 120 Hz ripple through an optocoupler.
// True if LOW is seen at any sample in the window.
static bool sampleActiveLow(int8_t gpio, uint16_t windowMs) {
  if (gpio < 0) return false;
  pinMode((uint8_t)gpio, INPUT);
  const uint32_t start = millis();
  do {
    if (digitalRead((uint8_t)gpio) == LOW) return true;
    delay(1);
  } while ((uint32_t)(millis() - start) < windowMs);
  return false;
}


static bool bootReadChangesOutput(uint8_t snsType) {
  return snsType == SNS_SWITCH || snsType == SNS_COUNTDOWN || snsType == SNS_COUNTDOWN_INV
      || snsType == SNS_HYDRONIC_ZONE;
}

static bool bootSensorReadIsFeasible(const ArborysSnsType* sensor) {
  if (!sensor) return false;
  const uint8_t t = sensor->snsType;
  if (t == 90) return false; // sleep info is set manually
  if (bootReadChangesOutput(t)) return false;
  // Ping, DNS, and download tests block setup and have no sample until they run on their own interval.
  if (IS_NETWORK_SENSOR_TYPE(t) && t != SNS_NET_RSSI) return false;
  return true;
}

static bool bootSampleIsUsable(const ArborysSnsType* sensor) {
  if (!sensor) return false;
  const double v = sensor->snsValue;
  if (isnan(v) || isinf(v)) return false;
  // Climate / I2C failure sentinel. Hubs store this as NaN.
  if (v <= -999.0 && v > -10000.0) return false;
  return true;
}

static bool bootReadIsDerived(uint8_t snsType) {
  return snsType == SNS_AGGREGATE || snsType == SNS_TEMP_GAP;
}

// Drop the history slot just written for a sample we are not keeping.
static void dropBootHistorySample(ArborysSnsType* S) {
  const int16_t h = SensorHistory.getSensorHistoryIndex(S);
  if (h < 0 || h >= _SENSORNUM) return;
  const uint8_t idx = SensorHistory.HistoryIndex[h];
  if (SensorHistory.TimeStamps[h][idx] == 0 || SensorHistory.TimeStamps[h][idx] != S->timeRead) return;
  SensorHistory.TimeStamps[h][idx] = 0;
  SensorHistory.Values[h][idx] = 0;
  SensorHistory.Flags[h][idx] = 0;
  SensorHistory.HistoryIndex[h] = (idx == 0) ? (uint8_t)(_SENSORHISTORYSIZE - 1) : (uint8_t)(idx - 1);
}

static bool readOneLocalSensorAtBoot(ArborysSnsType* sensor) {
  const uint32_t prevLogged = sensor->timeLogged;
  const uint8_t prevFlags = sensor->Flags;
  const bool prevExpired = sensor->expired;

  int8_t readResult = 0;
  if (IS_ACTUATOR_SENSOR_TYPE(sensor->snsType)) {
    readResult = pollActuator(sensor, true);
  } else {
    readResult = ReadData(sensor, true);
  }

  if (readResult > 0 && bootSampleIsUsable(sensor)) {
    delay(20);
    return true;
  }

  dropBootHistorySample(sensor);
  sensor->snsValue = NAN;
  sensor->Flags = prevFlags;
  sensor->expired = prevExpired;
  sensor->timeLogged = prevLogged;
  // -10 and a committed read already scheduled the next try. Anything else stays unread.
  if (readResult != -10 && readResult <= 0) sensor->timeRead = 0;
  if (readResult != 0) delay(20);
  return false;
}

// Called once setup has finished configuring pins. A usable reading replaces the
// registration value before any minute-boundary send. Unusable results become NaN
// with no history entry, so hubs and ArborysNet never average a placeholder.
int8_t readLocalSensorsAtBoot() {
  if (hardwareFaultBlocksLocalSensors()) return 0;
  SerialPrint("Boot: reading local sensors", true);
  esp_task_wdt_reset();
  updateRSSI(true);
  bryantBusPoll();

  int8_t numGood = 0;
  // Measurements first, then averages and gaps that depend on them.
  for (int pass = 0; pass < 2; pass++) {
    for (int16_t i = 0; i < _SENSORNUM; i++) {
      ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[i]);
      if (!sensor || !sensor->IsSet || sensor->deviceIndex != I.MY_DEVICE_INDEX) continue;
      // Leave outputs for the loop, after measurements exist. timeRead 0 keeps the placeholder off the uplink.
      if (bootReadChangesOutput(sensor->snsType)) {
        sensor->timeRead = 0;
        continue;
      }
      if (!bootSensorReadIsFeasible(sensor)) continue;
      const bool derived = bootReadIsDerived(sensor->snsType);
      if ((pass == 0 && derived) || (pass == 1 && !derived)) continue;
      if (readOneLocalSensorAtBoot(sensor)) numGood++;
      esp_task_wdt_reset();
    }
  }

  SerialPrint("Boot sensor read: " + String(numGood) + " ok", true);
  esp_task_wdt_reset();
  return numGood;
}

// Gate for hub uplinks and cloud uploads. Remote sensors are already someone else's sample.
bool localSensorReadyToSend(const ArborysSnsType* S) {
  if (!S) return false;
  if (S->deviceIndex != I.MY_DEVICE_INDEX) return true;
  if (S->timeRead == 0) return false;
  if (isnan(S->snsValue) || isinf(S->snsValue)) return false;
#if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
  if (IS_NETWORK_SENSOR_TYPE(S->snsType) && S->snsType != SNS_NET_RSSI) {
    if (NetworkMonitor.readSensorTime(S->snsType) == 0) return false;
  }
#endif
  return true;
}

int8_t readAllSensors(bool forceRead) {
//returns the number of sensors that were read successfully
  if (hardwareFaultBlocksLocalSensors()) return 0;
  int8_t numGood = 0;
  for (int16_t i = 0; i < _SENSORNUM; i++) {
    ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[i]);
    if (sensor && sensor->IsSet) {
      if (sensor->deviceIndex != I.MY_DEVICE_INDEX) continue;
      
      int8_t readResult = 0;
      if (IS_ACTUATOR_SENSOR_TYPE(sensor->snsType)) {
        readResult = pollActuator(sensor, forceRead);
      } else {
        readResult = ReadData(sensor, forceRead);
      }
      //readresult = 0 means not time to read, not an error

      if (readResult == -10) {
        #ifdef _USESERIAL
        SerialPrint((String) "Invalid sensor reading for " + (String) sensor->snsType + (String) "." + (String) sensor->snsID, true);
        #endif
      } else if (readResult == -1) {
          #ifdef _USESERIAL
          SerialPrint((String) "Could not find index to prefs or history for " + (String) sensor->snsType + (String) "." + (String) sensor->snsID, true);
          #endif
      } else if (readResult == -2) {
          #ifdef _USESERIAL
          SerialPrint((String) "Could not register" + (String) sensor->snsType + (String) "." + (String) sensor->snsID + " as a device.", true);
          #endif
      } else if (readResult >0) { //success
        numGood++;
      }

      // Only settle the bus after an actual hardware read (skip when poll interval not due).
      if (readResult != 0) {
        delay(20);
      }
    }
  }
  return numGood;
}


bool sensorUsesScaling(uint8_t snsType) {
  // Sensors that map a raw reading through SNS_CALIB_MIN/MAX to a 0-100 scale
  return snsType == 3 || snsType == 33 || snsType == 34 || snsType == 35;
}

// DHT / AHT / BMP / BME / BME680 climate channels: on bus or read failure report -999 (still send).
static bool isClimateEnvSensor(uint8_t snsType) {
  switch (snsType) {
    case 1: case 2:           // DHT
    case 4: case 5:           // AHT
    case 9: case 10: case 11: // BMP
    case 13: case 14: case 15: case 16: // BME
    case 17: case 18: case 19: case 20: // BME680
      return true;
    default:
      return false;
  }
}

// Map raw → 0..100 via calib endpoints. If auto-zero is enabled and the scaled value
// would be negative, move SNS_CALIB_MIN to the current raw reading so it becomes zero.
static double applyScaledReading(int16_t prefs_index, double raw, double calibMin, double calibMax, bool calibWasUnset) {
  double scaled = mapfloat(raw, calibMin, calibMax, 0, 100);
  if (bitRead(Prefs.SNS_FLAGS[prefs_index], SNS_FLAG_BIT_AUTOZERO) && scaled < 0) {
    Prefs.SNS_CALIB_MIN[prefs_index] = raw;
    if (calibWasUnset) {
      Prefs.SNS_CALIB_MAX[prefs_index] = calibMax;
    }
    Prefs.isUpToDate = false;
    scaled = 0;
  }
  return scaled;
}

// Sticky per-address I2C begin() failure from initHardwareSensors (7-bit addr 0..127).
static bool s_i2cInitFailed[128] = {false};

static void setI2cInitFailed(uint8_t addr, bool failed) {
  s_i2cInitFailed[addr & 0x7F] = failed;
}

static bool isI2cInitFailed(uint8_t addr) {
  return s_i2cInitFailed[addr & 0x7F];
}

bool hardwareInitFailedForI2cAddr(uint8_t addr) {
  return isI2cInitFailed(addr);
}

#ifdef _USETFLUNA
static SemaphoreHandle_t s_i2cMux = nullptr;
#endif

void i2cBusPrepare() {
#ifdef _USETFLUNA
  if (!s_i2cMux) s_i2cMux = xSemaphoreCreateMutex();
#endif
}

void i2cBusLock() {
#ifdef _USETFLUNA
  if (s_i2cMux) xSemaphoreTake(s_i2cMux, portMAX_DELAY);
#endif
}

void i2cBusUnlock() {
#ifdef _USETFLUNA
  if (s_i2cMux) xSemaphoreGive(s_i2cMux);
#endif
}

struct I2cBusGuard {
  I2cBusGuard() { i2cBusLock(); }
  ~I2cBusGuard() { i2cBusUnlock(); }
};

static bool isI2cSensorPin(int16_t snsPin, uint8_t* outAddr) {
  int8_t corrected = -1;
  const uint8_t pt = getPinType(snsPin, &corrected);
  if ((pt == 9 || pt == 10) && corrected >= 0) {
    if (outAddr) *outAddr = (uint8_t)corrected;
    return true;
  }
  return false;
}

int8_t ReadData(struct ArborysSnsType *P, bool forceRead, bool uncalibrated) {
  //return -10 if reading is invalid, -2 if I am not registered, -1 if not my sensor, 0 if not time to read, 1 if read successful
  
  //is this my sensor?
  if (P->deviceIndex != I.MY_DEVICE_INDEX) return -1;

  // need the index to the Prefs arrays for the sensor
  int16_t prefs_index = SensorHistory.getSensorHistoryIndex(P);
  if (prefs_index == -2) {
    storeError((String) "Could not register " + (String) P->snsType + "." + (String) P->snsID + " as a device.", ERROR_DEVICE_ADD, true);
    return -2;
  }
  if (prefs_index == -1) {
    storeError((String) "Could not find index to prefs or history for " + (String) P->snsType + "." + (String) P->snsID, ERROR_SENSOR_READ, true);
    return -1;
  }

  //is it time to read?
  const uint32_t nowUtc = (uint32_t)utcNow();
#ifdef _USETFLUNA
  // The focus task refreshes timeRead on every sample. Gate the heavier history
  // and alarm path on the prefs poll interval so that stamp does not suppress it.
  if (P->snsType == 7) {
    if (!forceRead) {
      static uint32_t s_tflunaPollAt = 0;
      const uint32_t poll = Prefs.SNS_INTERVAL_POLL[prefs_index];
      const bool due = (poll > 0) && (s_tflunaPollAt == 0 || nowUtc < s_tflunaPollAt || nowUtc >= s_tflunaPollAt + poll);
      if (!due) return 0;
      s_tflunaPollAt = nowUtc;
    }
  } else
#endif
  {
  // Poll interval 0 = never auto-update (forceRead still allowed).
  if (forceRead == false && Prefs.SNS_INTERVAL_POLL[prefs_index] == 0) return 0;
  if (forceRead==false && !(P->timeRead==0 || P->timeRead>nowUtc || P->timeRead + Prefs.SNS_INTERVAL_POLL[prefs_index] < nowUtc || nowUtc - P->timeRead >60*60*24 )) return 0;
  }

  bool turnOffPinAtEnd = false;
  int8_t correctedPin=-1;
  uint8_t pintype = getPinType(P->snsPin, &correctedPin);

  double LastsnsValue = P->snsValue;
  bool isInvalid = false;
  bool i2cInitFailShortCircuit = false;
  time_t measuredReadTime = 0;

  // I2C sensors (pin encoding 400–599): if begin() failed at init, report -999 and skip power/HW read.
  {
    uint8_t i2cAddr = 0;
    if (isI2cSensorPin(P->snsPin, &i2cAddr) && isI2cInitFailed(i2cAddr)) {
      P->snsValue = -999;
      isInvalid = true;
      i2cInitFailShortCircuit = true;
    }
  }

  // Even pintype = powered sensor; use P->powerPin (GPIO), not correctedPin (addr/channel).
  // DIO types store pull config in powerPin (-99/-100); never treat that as a rail.
  if (!i2cInitFailShortCircuit &&
      !isDioBinaryType(P->snsType) &&
      pintype != 0 && (pintype % 2 == 0) && isRealPowerPin(P->powerPin)) {
    togglePowerPin(P->powerPin, 1);
    turnOffPinAtEnd = true;
    //delay appropriately so that sensor stabilizes
    if (pintype == 10) {
      delay(100); //wait 100 ms for I2c devices reading to settle
    } else     delay(50); //wait X ms for reading to settle
  }

  // Use utcNow() for stamps (I.currentTime is local wall for display only)
  byte nsamps; //only used for some sensors
  double val;
  uint8_t lastflag = P->Flags;
  //reset adjustable flags (driven DIO outputs keep bit0 as DIO state until update sets it)
  if (!isDrivenDioOutputType(P->snsType)) {
    bitWrite(P->Flags,0,0);
  }
  bitWrite(P->Flags,5,0);
  bitWrite(P->Flags,6,0);

  if (!i2cInitFailShortCircuit) switch (P->snsType) {  
    case 1: //DHT temp
      {
        #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  (dht.readTemperature()*9/5+32);
        if (isTempValid(P->snsValue,false)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }
      #endif
      
      break;
      }
    case 2: //DHT RH
      {
        #ifdef DHTTYPE
        //DHT RH
        P->snsValue = dht.readHumidity();
        if (isRHValid(P->snsValue)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }
      #endif
      
      break;
      }
    case 3: //soil resistance 
    case 33: //soil capacitance
    case 34: //soil resistance (ADS1115)
    case 35: //soil capacitance (ADS1115)
      {
        //check which type of sensor reading is being done. if pin is dio then read the pin from the esp32. If pin is 100-199 then read the sensor from the MUX or ADS1115
        if (P->snsPin<100) {
          val=readPinValue(P, 10);

          delay(10);
  
          SerialPrint("val: " + String(val) + " sensor type: " + String(P->snsType),true);


          if (P->snsType==3) {
            double resistance = readResistanceDivider(10000, 3.3, val);
            if (isSoilResistanceValid(resistance)==false) isInvalid=true;
            if (uncalibrated) {
              //live calibration readout: show the raw resistance the user enters as min/max
              P->snsValue = resistance;
            } else {
              //calibration is sourced from Prefs (the persistent source of truth). If it was
              //never set (NAN) or is degenerate (min==max), fall back to the raw resistance.
              double calibMin = Prefs.SNS_CALIB_MIN[prefs_index];
              double calibMax = Prefs.SNS_CALIB_MAX[prefs_index];
              if (isnan(calibMin) || isnan(calibMax) || calibMin==calibMax) {
                P->snsValue = resistance;
              } else {
                P->snsValue = applyScaledReading(prefs_index, resistance, calibMin, calibMax, false);
              }
            }
          } 
          if (P->snsType==33) {
            if (uncalibrated) {
              P->snsValue = val;
            } else {
              //calibration is sourced from Prefs (the persistent source of truth). Fall back to
              //sensible defaults if it was never set (NAN) or is degenerate (min==max).
              double calibMin = Prefs.SNS_CALIB_MIN[prefs_index];
              double calibMax = Prefs.SNS_CALIB_MAX[prefs_index];
              bool calibWasUnset = (isnan(calibMin) || isnan(calibMax) || calibMin==calibMax);
              if (calibWasUnset) {
                calibMin = 0.15;
                calibMax = 1.75;
              }
              P->snsValue = applyScaledReading(prefs_index, val, calibMin, calibMax, calibWasUnset);
              if (isSoilCapacitanceValid(P->snsValue)==false) isInvalid=true;
            }
          } 
        }
        else {
          #ifdef _USEADS1115
            //read the sensor from the ADS1115. I have not currently implemented MUX reading and only allow 1 ADS1115 at present.
            readADS1115(10, P);

            //now convert the voltage to the sensor value
            if (P->snsType==34) {
              double resistance = readResistanceDivider(10000, 3.3, P->snsValue);
              if (isSoilResistanceValid(resistance)==false) isInvalid=true;
              if (uncalibrated) {
                //live calibration readout: show the raw resistance the user enters as min/max
                P->snsValue = resistance;
              } else {
                //calibration is sourced from Prefs (the persistent source of truth). If it was
                //never set (NAN) or is degenerate (min==max), fall back to the raw resistance.
                double calibMin = Prefs.SNS_CALIB_MIN[prefs_index];
                double calibMax = Prefs.SNS_CALIB_MAX[prefs_index];
                if (isnan(calibMin) || isnan(calibMax) || calibMin==calibMax) {
                  P->snsValue = resistance;
                } else {
                  P->snsValue = applyScaledReading(prefs_index, resistance, calibMin, calibMax, false);
                }
              }
            } 
            if (P->snsType==35) {
              if (uncalibrated) {
                //live calibration readout: leave the raw reading the user enters as min/max
                P->snsValue = P->snsValue;
              } else {
                //calibration is sourced from Prefs (the persistent source of truth). Fall back to
                //sensible defaults if it was never set (NAN) or is degenerate (min==max).
                double calibMin = Prefs.SNS_CALIB_MIN[prefs_index];
                double calibMax = Prefs.SNS_CALIB_MAX[prefs_index];
                bool calibWasUnset = (isnan(calibMin) || isnan(calibMax) || calibMin==calibMax);
                if (calibWasUnset) {
                  calibMin = 0.15;
                  calibMax = 1.75;
                }
                P->snsValue = applyScaledReading(prefs_index, P->snsValue, calibMin, calibMax, calibWasUnset);
                if (isSoilCapacitanceValid(P->snsValue)==false) isInvalid=true;
              }
            } 
          #endif
        }
      break;
      }

    case 4: //AHT Temp
      {
        I2cBusGuard i2cGuard;
        #ifdef _USEAHT
        //aht temperature
          val = aht.readTemperature();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (100*(val*9/5+32))/100;
            // A high limit above 125°F (an attic) accepts hotter air. Otherwise the usual range applies.
            const bool allowHot = prefs_index >= 0 && Prefs.SNS_LIMIT_MAX[prefs_index] > 125.0;
            if (isTempValid(P->snsValue, allowHot)==false) {
              P->snsValue = -999;
              isInvalid=true;
            }
          }
          else
          {
            SerialPrint("AHT Temperature Error",true);
            P->snsValue = -999;
            isInvalid=true;
          }
      #endif
      #ifdef _USEAHTADA
        //aht temperature
          sensors_event_t humidity, temperature;
          if (!aht.getEvent(&humidity,&temperature)) {
            SerialPrint("AHT Temperature Error",true);
            P->snsValue = -999;
            isInvalid=true;
          } else {
            P->snsValue = (100*(temperature.temperature*9/5+32))/100;
            const bool allowHot = prefs_index >= 0 && Prefs.SNS_LIMIT_MAX[prefs_index] > 125.0;
            if (isTempValid(P->snsValue, allowHot)==false) {
              P->snsValue = -999;
              isInvalid=true;
            }
          }

      #endif


      break;
      }
    case 5: //AHT RH
      {
      I2cBusGuard i2cGuard;
      //aht humidity
        #ifdef _USEAHTADA
          //AHT
            sensors_event_t humidity, temperature;
            if (!aht.getEvent(&humidity,&temperature)) {
              SerialPrint("AHT Humidity Error",true);
              P->snsValue = -999;
              isInvalid=true;
            } else {
              P->snsValue = (100*(humidity.relative_humidity))/100;
              if (isRHValid(P->snsValue)==false) {
                P->snsValue = -999;
                isInvalid=true;
              }
            }
        #endif
        #ifdef _USEAHT
          val = aht.readHumidity();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (val*100)/100;
            if (isRHValid(P->snsValue)==false) {
              P->snsValue = -999;
              isInvalid=true;
            }
          }
          else
          {
            SerialPrint("AHT Humidity Error",true);
            P->snsValue = -999;
            isInvalid=true;
          }
          #endif
      break;
      }
    case 6: //NTC thermistor
      {
        //requires _THERMISTOR_B0, _THERMISTOR_R0 (nominal resistance at 25C), _THERMISTOR_RKNOWN (resistance of resisor in series with NTC), _THERMISTOR_TKNOWN (temperature at known resistance), _THERMISTOR_VDD (supply voltage)   
        #if defined(_USEADS1115) && defined(_THERMISTOR_B0) && defined(_THERMISTOR_R0) && defined(_THERMISTOR_RKNOWN) && defined(_THERMISTOR_TKNOWN) && defined(_THERMISTOR_VDD)
          // 1. read voltage 
          readADS1115(20, P); 
          if (!uncalibrated) {

            // 2. Calculate R_thermistor using the Voltage Divider formula
            // V_in and R_known should be measured/confirmed for highest accuracy!
            float R_thermistor = _THERMISTOR_RKNOWN * (_THERMISTOR_VDD / P->snsValue - 1.0);

            // 3. Calculate Temperature using the Steinhart-Hart (B-parameter) equation (Result is in Kelvin)
            // T = 1 / [ (1/T0) + (1/B) * ln(R_thermistor / R0) ]
            float T_kelvin = 1.0 / ( (1.0/_THERMISTOR_TKNOWN) + (1.0/_THERMISTOR_B0) * log(R_thermistor / _THERMISTOR_R0) );
            
            // 4. Convert Kelvin to Fahrenheit
            // Step 4a: Convert Kelvin to Celsius
            float T_celsius = T_kelvin - 273.15;
            
            // Step 4b: Convert Celsius to Fahrenheit
            P->snsValue = (T_celsius * 9.0/5.0) + 32.0; 
            if (isTempValid(P->snsValue,false)==false) isInvalid=true;
          }
        #else
          SerialPrint("NTC thermistor not configured",true);
          P->snsValue = -1000;
          isInvalid = true;
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
        #ifdef _USETFLUNA
          //find the index to the TFLUNA sensor and then call checkTFLuna with the index, which will update the sensor value
            checkTFLuna(-1);          
        #endif
   
      break;
      }
    case 9: //BMP pres
      {
        I2cBusGuard i2cGuard;
        #ifdef _USEBMP
         P->snsValue = bmp.readPressure()/100; //in hPa
         
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (isPressureValid(P->snsValue) && P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1000;
          } else if (isPressureValid(P->snsValue)) {
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1009;
          }

          if (isPressureValid(P->snsValue) && LAST_BAR_READ+60*60 < (uint32_t)utcNow()) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = (uint32_t)utcNow();          
          }
        #endif
        if (isPressureValid(P->snsValue)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }

      #endif
          
      break;
      }
    case 10: //BMP temp
      {
        I2cBusGuard i2cGuard;
        #ifdef _USEBMP
        P->snsValue = ( bmp.readTemperature()*9/5+32);
        const bool allowHot = prefs_index >= 0 && Prefs.SNS_LIMIT_MAX[prefs_index] > 125.0;
        if (isTempValid(P->snsValue, allowHot)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }
      #endif
      
      break;
      }
    case 11: //BMP alt
      {
        I2cBusGuard i2cGuard;
        #ifdef _USEBMP
         P->snsValue = (bmp.readAltitude(1013.25)); //meters
         if (isnan(P->snsValue)) {
           P->snsValue = -999;
           isInvalid=true;
         }
      #endif
      
      break;
      }
    case 12: //make a prediction about weather
      {
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
         P->snsValue = bme.readPressure()/100.0; //in hPa
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (isPressureValid(P->snsValue) && P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1000;
          } else if (isPressureValid(P->snsValue)) {
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1009;
          }

          if (isPressureValid(P->snsValue) && LAST_BAR_READ+60*60 < (uint32_t)utcNow()) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = (uint32_t)utcNow();          
          }
        #endif
        if (isPressureValid(P->snsValue)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }
      #endif
      
      break;
      }
    case 14: //BMEtemp
      {
        #ifdef _USEBME
        P->snsValue = (( bme.readTemperature()*9/5+32) );
        if (isTempValid(P->snsValue,false)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }
      #endif
      
      break;
      }
    case 15: //bme rh
      {
        #ifdef _USEBME
      
          P->snsValue = ( bme.readHumidity() );
          if (isRHValid(P->snsValue)==false) {
            P->snsValue = -999;
            isInvalid=true;
          }
        #endif
      
      break;
      }
    case 16: //BMEalt
      {
        #ifdef _USEBME
         P->snsValue = (bme.readAltitude(1013.25)); //meters
         if (isnan(P->snsValue)) {
           P->snsValue = -999;
           isInvalid=true;
         }

      #endif
      break;
      }
    #ifdef _USEBME680
    case 17: //bme680 temp
      {
        read_BME680();
      P->snsValue = (double) (( ((double) temperature/100) *9/5)+32); //degrees F
      if (isTempValid(P->snsValue,false)==false) {
        P->snsValue = -999;
        isInvalid=true;
      }
      break;
      }
    case 18: //bme680 humidity
      {
        read_BME680();
        P->snsValue = ((double) humidity/1000); //RH%
        if (isRHValid(P->snsValue)==false) {
          P->snsValue = -999;
          isInvalid=true;
        }
        break;
      }
    case 19: //bme680 air pressure
      {
        read_BME680();
      P->snsValue = ((double) pressure/100); //hPa
      if (isPressureValid(P->snsValue)==false) {
        P->snsValue = -999;
        isInvalid=true;
      }
      break;
      }
    case 20: //bme680 gas
      {
        read_BME680();
      P->snsValue = (gas); //milliohms
      if (P->snsValue <= 0) {
        P->snsValue = -999;
        isInvalid=true;
      }
      break;
      }
    #endif

    #if defined(_USEHVAC)

      case SNS_HVAC_CALL:
      {
        const bool on = sampleActiveLow(correctedPin, 50);
        if (on) {
          bitWrite(P->Flags, 0, 1);
          P->snsValue += (double)Prefs.SNS_INTERVAL_POLL[prefs_index] / 60.0;
        } else {
          bitWrite(P->Flags, 0, 0);
        }
        break;
      }

      case SNS_HVAC_TOTAL:
      {
        if (Sensors.countFlagged(SNS_HVAC_CALL, 0b00000001, 0b00000001, 0, false, false, 0) > 0) {
          bitWrite(P->Flags, 0, 1);
          P->snsValue += (double)Prefs.SNS_INTERVAL_POLL[prefs_index] / 60.0;
        } else {
          bitWrite(P->Flags, 0, 0);
        }
        break;
      }

    #endif

    #if defined(_USELIBATTERY) || defined(_USESLABATTERY)

      //use ESP ADC
      case 60: // Li battery
      case 61: //pb battery
        {
          //note that esp32 ranges 0 to ADCRATE, while 8266 is 1023. This is set in header.hpp
          P->snsValue = readVoltageDivider( _VDIVIDER_R1, _VDIVIDER_R2,  P, 20); //if R1=R2 then the divider is 50%


        break;
        }

      //use ads1115 
      case 62: //li battery voltage from an ADS1115
      case 63: //pb battery voltage from an ADS1115
        {
          //_USEBATPCNT
        #if defined(_USEADS1115) 
          readADS1115(20, P); 
          P->snsValue = P->snsValue * (((float)(_VDIVIDER_R1 + _VDIVIDER_R2))/_VDIVIDER_R2)/1000.0; //convert to total voltage, in volts
        #else
          P->snsValue = -99999; //invalid value
        #endif


        break;
        }

      #endif

      case SNS_TIMER_ON_H:
      {
        const double onSpec = (prefs_index >= 0) ? Prefs.SNS_LIMIT_MIN[prefs_index] : P->limitLow;
        const double offSpec = (prefs_index >= 0) ? Prefs.SNS_LIMIT_MAX[prefs_index] : P->limitHigh;
        const bool inside = InterruptTriggers_inClockWindow(onSpec, offSpec);
        P->snsValue = inside ? 1.0 : 0.0;
        bitWrite(P->Flags, 0, inside ? 1 : 0);
        break;
      }

      case SNS_PRESENCE:
      case SNS_BUTTON:
      {
#if _USEINTERRUPT
        const uint32_t pollSec = (prefs_index >= 0) ? Prefs.SNS_INTERVAL_POLL[prefs_index] : P->PollingInt;
        InterruptTriggers_updateCountSensor(P, pollSec);
#else
        P->snsValue = floor(P->snsValue);
#endif
        break;
      }
      case SNS_LEAK:
      case SNS_BINARY:
      case SNS_BINARY_INV:
      case SNS_VALVE:
      {
        int8_t dioGpio = correctedPin;
        #ifdef _USELEAK
        if (P->snsType == SNS_LEAK && dioGpio < 0) {
          dioGpio = (int8_t)_USELEAK;
        }
        #endif
        if (dioGpio < 0) {
          isInvalid = true;
          break;
        }
        if (P->snsType == SNS_VALVE) {
          P->snsValue = sampleActiveLow(dioGpio, 50) ? 1.0 : 0.0;
        } else if (P->snsType == SNS_BINARY_INV) {
          P->snsValue = (digitalRead(dioGpio) == LOW) ? 1.0 : 0.0;
        } else {
          P->snsValue = (digitalRead(dioGpio) == HIGH) ? 1.0 : 0.0;
        }
        break;
      }
    case SNS_NET_RSSI:
      {
        switch (P->snsID) {
          case 2:
            P->snsValue = I.RSSIlow;
            break;
          case 3:
            P->snsValue = I.RSSIhigh;
            break;
          default:
            P->snsValue = I.RSSIcurrent;
            break;
        }
        if (P->snsValue <= -999) {
          isInvalid = true;
        } else if (I.lastRSSItime > 0) {
          measuredReadTime = I.lastRSSItime;
        }
        break;
      }
#if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
    case SNS_NET_FIRST:
    case 154:
    case 155:
    case 156:
    case 157:
    case 158:
    case 159:
    case 160:
    case SNS_NET_LAST:
      {
        int8_t nmTest = NetworkMonitor.runTestIndexFromSensorType(P->snsType);
        if (nmTest >= 0) {
          NetworkMonitor.runTest((uint8_t)nmTest);
          measuredReadTime = NetworkMonitor.readSensorTime(P->snsType);
        }
        if (nmTest < 0
            || !NetworkMonitor.readSensorValue(P->snsType, P->snsValue)
            || NetworkMonitor.isSensorValueInvalid(P->snsType, P->snsValue)) {
          isInvalid = true;
        }
        break;
      }
#endif
    case 90:
      {
        //don't do anything here
      //I'm set manually!
      break;
      }

#if defined(_USEBRYANT)
    case SNS_BRYANT_MODE:
    case SNS_BRYANT_OAT:
    case SNS_BRYANT_SETPOINT:
    case SNS_BRYANT_TEMP:
    case SNS_BRYANT_RH:
    case SNS_BRYANT_RUNTIME:
    case SNS_BRYANT_DEFROST:
    case SNS_BRYANT_RUN_COOL:
    case SNS_BRYANT_DAY_HEAT:
    case SNS_BRYANT_DAY_DEFROST:
    case SNS_BRYANT_DAY_COOL:
      bryantPublish(P);
      break;
#endif

    case SNS_CLOCK:
    {
      P->snsValue = I.currentTime;
      break;
    }
    
  }

  if (isInvalid) {
    if (isClimateEnvSensor(P->snsType) || i2cInitFailShortCircuit) {
      // Bus / read / init failure: still stamp, history, and send so hubs see -999.
      P->snsValue = -999;
      if (i2cInitFailShortCircuit) {
        storeError((String) "I2C init failed (reporting -999) for " + (String) P->snsType + (String) "." + (String) P->snsID, ERROR_SENSOR_READ, true);
      } else {
        storeError((String) "Climate sensor read failed (reporting -999) for " + (String) P->snsType + (String) "." + (String) P->snsID, ERROR_SENSOR_READ, true);
      }
    } else {
      //the reading is considered invalid. Set the lastread time such that the next read will be 25% of typical interval.
      int shortDelay = (Prefs.SNS_INTERVAL_POLL[prefs_index] * 0.25);
      if (shortDelay < 60 && Prefs.SNS_INTERVAL_POLL[prefs_index] > 60) shortDelay = 60; //minimum 1 minute
      if (shortDelay > 60*10) shortDelay = 60*10; //maximum 10 minutes
      P->timeRead = (uint32_t)utcNow() - shortDelay;
      if (turnOffPinAtEnd) {
        togglePowerPin(P->powerPin, 0);
      }
      storeError((String) "Invalid sensor reading for " + (String) P->snsName + " " + (String) P->snsType + "." + (String) P->snsID, ERROR_SENSOR_READ, true);
      return -10;
    }
  }


  #ifdef _USELOWPOWER
    bitWrite(P->Flags,2,1); //low power device
  #else
    bitWrite(P->Flags,2,0); //not low power device
  #endif

  if (IS_HVAC_RUNTIME_TYPE(P->snsType)) {
    if (bitRead(P->Flags,0) != bitRead(lastflag,0)) { //flags changed
      if (bitRead(P->Flags, 7)) bitWrite(P->Flags,6,1); //critical: bounds edge
      if (bitRead(P->Flags,0) == 1) bitWrite(P->Flags,5,1); //value is high
      SensorEffectors_onAlarmChange(P->snsType, P->snsID, P->snsValue, P->Flags, lastflag);
    } else {
      //no change in flag status. bit 6 is already 0.
    }
    P->timeRead = (measuredReadTime > 0) ? measuredReadTime : (uint32_t)utcNow();

    //add to sensor history
    SensorHistory.recordSentValue(P);     
  }  else  {
    //set flag status

    double limitHigh = (prefs_index >= 0) ? Prefs.SNS_LIMIT_MAX[prefs_index] : P->limitHigh;
    double limitLow = (prefs_index >= 0) ? Prefs.SNS_LIMIT_MIN[prefs_index] : P->limitLow;

    if (isDioBinaryType(P->snsType)) {
      P->snsValue = (P->snsValue >= 0.5) ? 1.0 : 0.0;
    }

    if (IS_INTERRUPT_SENSOR_TYPE(P->snsType) && prefs_index >= 0 &&
        normalizeHumanPresenceLimits(Prefs.SNS_LIMIT_MAX[prefs_index], Prefs.SNS_LIMIT_MIN[prefs_index])) {
      limitHigh = Prefs.SNS_LIMIT_MAX[prefs_index];
      limitLow = Prefs.SNS_LIMIT_MIN[prefs_index];
      Prefs.isUpToDate = false;
      storeError("Presence/button limits were inverted; swapped high/low", ERROR_SENSOR_INVALID, true);
    }

    if (prefs_index >= 0) {
      P->limitHigh = (float)limitHigh;
      P->limitLow = (float)limitLow;
      applyAlarmFlags(P, limitHigh, limitLow, lastflag);
    } else {
      bitWrite(P->Flags, 0, 0);
    }
    P->timeRead = (measuredReadTime > 0) ? measuredReadTime : (uint32_t)utcNow();
    //add to sensor history
    SensorHistory.recordSentValue(P);  
  }

  #ifdef _USELED
    //check if this is a soil sensor
    if (Sensors.isSensorOfType(P, "soil")) LEDs.LED_set_color_soil(P);
  #endif

  // Successful local read refreshes the expiry clock. Critical sends the
  // expired → fresh edge; Monitored does not send for that change alone.
  if (P->expired && bitRead(P->Flags, 7)) bitWrite(P->Flags, 6, 1);
  P->expired = false;

  if (turnOffPinAtEnd) {
    togglePowerPin(P->powerPin, 0);
  }

  return 1;
}


void togglePowerPin(int16_t powerPin, bool on) {
  #ifdef _USELOWPOWER
  //low power mode does not turn on power pins, this is done at startup
  return;
  #endif
  if (!isRealPowerPin(powerPin)) return;
  powerPin = abs(powerPin);
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, on ? HIGH : LOW);
}

float readResistanceDivider(float R1, float Vsupply, float Vread) {
  return R1 * Vread/(Vsupply - Vread) ;
}


bool readADS1115(byte avgN, ArborysSnsType* P) {
  //returns the voltage read from the ADS1115, and stores in P->snsValue
  //note that P->snsPin is the ADS1115 channel, not the actual pin number or ADS address (for example, ADS1115 has channels 0-3).
  SerialPrint("Entering readADS1115 with: P->snsPin: " + String(P->snsPin) + " powerPin: " + String(P->powerPin), true);
  #ifdef _USEADS1115
    int16_t snsPin = P->snsPin;
    int16_t powerPin = P->powerPin;
    if (abs(P->snsPin) < 100 || abs(P->snsPin) > 199) return false; //invalid pin for ADS1115
    SerialPrint("readADS1115: snsPin: " + String(snsPin) + " powerPin: " + String(powerPin), true);
    if (P->snsPin < 0) {
      togglePowerPin(powerPin,1);      
    }
    snsPin = abs(P->snsPin)-100; //convert to positive channel number
    
    P->snsValue = 0; // Initialize to zero before accumulation
    for (byte i=0;i<avgN;i++) { //read the ADC and average
      P->snsValue += ads.readADC_SingleEnded(snsPin) ;
      SerialPrint("readADS1115: readADC_SingleEnded(" + String(snsPin) + "): " + String(ads.readADC_SingleEnded(snsPin)), true);
      delay(10);
    }

    if (P->snsPin < 0) { //if negative pin, then power pin is the same as the sensor pin
      togglePowerPin(powerPin,0);
    }

    P->snsValue /= avgN; //average the readings
    P->snsValue = P->snsValue * _USE_ADS_MULTIPLIER; //convert to voltage
    SerialPrint("readADS1115: final P->snsValue: " + String(P->snsValue), true);
    return true;
  #else
    return false;
  #endif
}

float readVoltageDivider(float R1, float R2, ArborysSnsType* P, byte avgN) {
  /*
    R1 is first resistor
    R2 is second resistor (which we are measuring voltage across)
    P is the sensor pointer
    ADCRATE is the max ADCRATE
    Vm is the ADC max voltage 
    avgN is the number of times to avg
    */
 
  return (float)  readPinValue(P, avgN) * ((float)(R1 + R2))/R2;

}


#ifdef _USEBME680
void read_BME680() {
  
  uint32_t m = millis();
  
  if (last_BME680>m || m-last_BME680>500)   BME680.getSensorData(temperature, humidity, pressure, gas);  // Get readings
  else return;
  last_BME680=m;
}
#endif


/**
 * @brief Initialize hardware sensors (BME, BMP, DHT, etc.)
 */
void initHardwareSensors() {
#ifdef _USEI2C
  // Bound a missing device before the probe loops. AHTxx::begin() later sets this to 1 ms.
  Wire.setTimeout(50);
  Wire.setTimeOut(50);
#endif
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("Sns setup.");
  #endif

  // Energize all configured sensor power rails so I2C/SPI begin() can see devices.
  {
    const int16_t powerPinsInit[] = _POWERPINS;
    for (byte i = 0; i < _SENSORNUM; i++) {
      if (isRealPowerPin(powerPinsInit[i])) {
        togglePowerPin(powerPinsInit[i], 1);
      }
    }
    delay(200);
  }

  #ifdef _USEBME680_BSEC
    iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire);
    String output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
    Serial.println(output);
    checkIaqSensorStatus();

    bsec_virtual_sensor_t sensorList[13] = {
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_STABILIZATION_STATUS,
      BSEC_OUTPUT_RUN_IN_STATUS,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
      BSEC_OUTPUT_GAS_PERCENTAGE
    };

    iaqSensor.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);
    checkIaqSensorStatus();
  #endif

  #ifdef _USEBME680
    {
      byte retry = 0;
      while (!BME680.begin(I2C_STANDARD_MODE) && retry < 20) {  // Start BME680 using I2C, use first device found
        #ifdef _DEBUG
          Serial.println("-  Unable to find BME680. Retrying without delay.\n");
        #endif
        retry++;
      }
      if (retry >= 20) {
        SerialPrint("BME680 failed to connect after 20 attempts (will report -999 on read)", true);
        storeError("BME680 failed to connect after 20 attempts", ERROR_SENSOR_READ, true);
        setI2cInitFailed(0x76, true);
        setI2cInitFailed(0x77, true);
      } else {
        setI2cInitFailed(0x76, false);
        setI2cInitFailed(0x77, false);
        BME680.setOversampling(TemperatureSensor, Oversample16);
        BME680.setOversampling(HumiditySensor, Oversample16);
        BME680.setOversampling(PressureSensor, Oversample16);
        BME680.setIIRFilter(IIR4);
        BME680.setGas(320, 150);  // 320°c for 150 milliseconds
      }
    }
  #endif

  #ifdef DHTTYPE
    #ifdef _DEBUG
      Serial.printf("dht begin\n");
    #endif
    dht.begin();
  #endif
  #ifdef _USEBARPRED
    for (byte ii = 0; ii < 24; ii++) {
      BAR_HX[ii] = -1;
    }
    LAST_BAR_READ = 0;
  #endif
  
  byte retry = 0;
  #ifdef _USEADS1115
    retry = 0;
    while (!isI2CDeviceReady(_USEADS1115) && retry < 5)  {
      SerialPrint("ADS1115 not ready or connected. Retry number " + String(retry),true);
      esp_task_wdt_reset();
      delay(20);
      retry++;
    }
    retry = 0;
    while (!ads.begin() && retry < 10) {
      SerialPrint("ADS1115 not connected. Retry number " + String(retry),true);
      delay(250);
      retry++;
    }
    if (retry >= 10) {
      SerialPrint("ADS1115 failed to connect after 10 attempts",true);
      storeError("ADS1115 failed to connect after 10 attempts", ERROR_SENSOR_READ, true);
      setI2cInitFailed((uint8_t)_USEADS1115, true);
    } else {
      SerialPrint("ADS1115 connected after " + String(retry) + " attempts",true);
      setI2cInitFailed((uint8_t)_USEADS1115, false);
    }
  
    ads.setGain(_USE_ADS_GAIN);
  #endif

  // Initialize AHT sensor (screen/retry without blocking delays on failure)
  #if defined(_USEAHT) || defined(_USEAHTADA)
    retry = 0;
    #if defined(AHTXX_ADDRESS_X38)
      const byte ahtAddr = AHTXX_ADDRESS_X38;
    #else
      const byte ahtAddr = 0x38;
    #endif
    while (!isI2CDeviceReady(ahtAddr) && retry < 5) {
      SerialPrint("AHT not ready or connected. Retry number " + String(retry),true);
      esp_task_wdt_reset();
      retry++;
    }
    if (!isI2CDeviceReady(ahtAddr)) {
      SerialPrint("AHT not detected (will report hardware fault)", true);
      storeError("AHT not detected", ERROR_SENSOR_READ, true);
      setI2cInitFailed(ahtAddr, true);
    } else {
    retry = 0;

    while (aht.begin() != true && retry < 3) {
      SerialPrint("AHT not connected. Retry number " + String(retry),true);
      esp_task_wdt_reset();

      #ifdef _USESSD1306  
        oled.clear();
        oled.setCursor(0,0);  
        oled.printf("No aht x%d!", retry);          
      #endif
      retry++;
    }
    if (retry >= 3) {
      SerialPrint("AHT failed to connect after 3 attempts (will report -999 on read)",true);
      storeError("AHT failed to connect after 3 attempts", ERROR_SENSOR_READ, true);
      setI2cInitFailed(ahtAddr, true);
    } else {
      SerialPrint("AHT connected after " + String(retry) + " attempts",true);
      setI2cInitFailed(ahtAddr, false);
    }
    }
  #endif

  #ifdef _USEBMP
    retry = 0;
    //note that _USEBMP is the address of the BMP sensor, but may be wrong.
    byte BMPaddress = 0;
    bool isBMPgood = true;

    //quick check to see if an address is obviously valid
    if (isI2CDeviceReady(0x76)) BMPaddress = 0x76;
    else if (isI2CDeviceReady(0x77)) BMPaddress = 0x77;
    else BMPaddress = _USEBMP;

    while (!isI2CDeviceReady(BMPaddress) && retry < 5) {
      SerialPrint("BMP not ready at address " + String(BMPaddress) + ". Retry number " + String(retry),true);
      esp_task_wdt_reset();
      retry++;
    }

    if (!isI2CDeviceReady(BMPaddress)) {
      SerialPrint("BMP was not detected at address " + String(BMPaddress) + ". Will check address ", false);
      if (BMPaddress == 0x76) BMPaddress = 0x77;
      else BMPaddress = 0x76;
      SerialPrint(" " + String(BMPaddress), true);
      retry = 0;
      while (!isI2CDeviceReady(BMPaddress) && retry < 5) {
        SerialPrint("BMP not ready at address " + String(BMPaddress) + ". Retry number " + String(retry),true);
        esp_task_wdt_reset();
        retry++;
      }
      if (!isI2CDeviceReady(BMPaddress)) {
        isBMPgood = false;
        SerialPrint("BMP not ready/detected at all known addresses.",true);
        storeError("BMP not detected at known addresses", ERROR_SENSOR_READ, true);
      } else {
        SerialPrint("BMP ready at address " + String(BMPaddress),true);
      }
    } else {
      SerialPrint("BMP ready at address " + String(BMPaddress),true);
    }

    retry = 0;

    if (isBMPgood) {
      while (bmp.begin(BMPaddress) != true && retry < 3) {
        esp_task_wdt_reset();
        SerialPrint("BMP failed to connect at " + String(BMPaddress) + ".\nRetry number " + String(retry) + "\n",true);

        #ifdef _USESSD1306  
          oled.clear();
          oled.setCursor(0,0);  
          oled.printf("BMP fail at %d.\nRetry number %d\n", BMPaddress, retry);          
        #endif

        retry++;
      }
      if (retry >= 3) {
        SerialPrint("BMP failed to connect after 3 attempts (will report -999 on read)",true);
        storeError("BMP failed to connect after 3 attempts", ERROR_SENSOR_READ, true);
        setI2cInitFailed(BMPaddress, true);
        setI2cInitFailed(0x76, true);
        setI2cInitFailed(0x77, true);
      } else {
        SerialPrint("BMP connected after " + String(retry) + " attempts",true);
        setI2cInitFailed(BMPaddress, false);
        /* Default settings from datasheet. */
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X16,
                      Adafruit_BMP280::FILTER_X16,
                      Adafruit_BMP280::STANDBY_MS_500);
        }
    } else {
      setI2cInitFailed(0x76, true);
      setI2cInitFailed(0x77, true);
      setI2cInitFailed((uint8_t)_USEBMP, true);
    }
  #endif
  
  #ifdef _USEBME
    retry = 0;
    // Screen common BME280 addresses (0x76 / 0x77) without blocking delays.
    byte BMEaddress = 0;
    if (isI2CDeviceReady(0x76)) BMEaddress = 0x76;
    else if (isI2CDeviceReady(0x77)) BMEaddress = 0x77;
    else BMEaddress = 0x76;

    while (!isI2CDeviceReady(BMEaddress) && retry < 5) {
      SerialPrint("BME not ready at address " + String(BMEaddress) + ". Retry number " + String(retry),true);
      esp_task_wdt_reset();
      retry++;
    }
    if (!isI2CDeviceReady(BMEaddress)) {
      byte alt = (BMEaddress == 0x76) ? 0x77 : 0x76;
      SerialPrint("BME not at " + String(BMEaddress) + ", trying " + String(alt), true);
      BMEaddress = alt;
      retry = 0;
      while (!isI2CDeviceReady(BMEaddress) && retry < 5) {
        SerialPrint("BME not ready at address " + String(BMEaddress) + ". Retry number " + String(retry),true);
        esp_task_wdt_reset();
        retry++;
      }
    }

    retry = 0;
    if (!isI2CDeviceReady(BMEaddress)) {
      SerialPrint("BME not detected at known addresses", true);
      storeError("BME not detected at known addresses", ERROR_SENSOR_READ, true);
      setI2cInitFailed(0x76, true);
      setI2cInitFailed(0x77, true);
      retry = 3;
    }
    while (!bme.begin(BMEaddress) && retry < 3) {
      esp_task_wdt_reset();
      #ifdef _USESSD1306
        oled.clear();
        oled.setCursor(0,0);
        oled.printf("BME fail %d x%d", BMEaddress, retry);
      #else
        SerialPrint("BME failed at " + String(BMEaddress) + ". Retry number " + String(retry),true);
      #endif
      retry++;
    }
    if (retry >= 3) {
      SerialPrint("BME failed to connect after 3 attempts (will report -999 on read)", true);
      storeError("BME failed to connect after 3 attempts", ERROR_SENSOR_READ, true);
      setI2cInitFailed(BMEaddress, true);
      setI2cInitFailed(0x76, true);
      setI2cInitFailed(0x77, true);
    } else {
      setI2cInitFailed(BMEaddress, false);
      /* Default settings from datasheet. */
      bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                      Adafruit_BME280::SAMPLING_X2,
                      Adafruit_BME280::SAMPLING_X16,
                      Adafruit_BME280::FILTER_X16,
                      Adafruit_BME280::STANDBY_MS_500);
    }
  #endif

  #ifdef _USETFLUNA
    {
      const uint8_t tfAddr = (uint8_t)_USETFLUNA;
      // No dedicated begin() here; mark failed if the device does not ACK on the bus.
      if (!isI2CDeviceReady(tfAddr)) {
        SerialPrint("TFLuna not ready at I2C " + String(tfAddr) + " (will report -999 on read)", true);
        setI2cInitFailed(tfAddr, true);
      } else {
        setI2cInitFailed(tfAddr, false);
      }
    }
  #endif

  // Initialize barometric prediction globals

  // Call sensor-specific setup
  setupSensors();

#ifdef _USEI2C
  // AHTxx::begin() calls Stream::setTimeout(1). That is the readBytes wait, not
  // the I2C engine. A 1 ms wait drops bytes when Wi-Fi interrupts land mid-read.
  // OutdoorLighting then posts a hub error on every failed BMP or AHT sample.
  Wire.setClock(100000L);
  Wire.setTimeout(50);
  Wire.setTimeOut(50);
#endif

  // Powered sensors use negative snsPin encoding: turn their rails back OFF for low power.
  // ReadData will pulse them on around each poll.
  {
     int16_t snsPinsInit[] = _SNSPINS;
     int16_t powerPinsInit[] = _POWERPINS;
    for (byte i = 0; i < _SENSORNUM; i++) {
      if (snsPinsInit[i] < 0 && snsPinsInit[i] != -9999 &&
          isRealPowerPin(powerPinsInit[i])) {
        togglePowerPin(powerPinsInit[i], 0);
      }
    }
  }

  auditLocalHardwareInit();
}



uint8_t getPinType(int16_t pin, int8_t* correctedPin) {
  //0 = unknown or no pin
  //1 = analog input, no power
  //2 = analog input, power
  //3 = digital input, no power
  //4 = digital input, power
  //5 = MUX input, no power
  //6 = MUX input, power
  //7 = SPI input, no power
  //8 = SPI input, power
  //9 = I2C input, no power
  //10 = I2C input, power

  //pin number for the sensor, if applicable. 0-99 is anolog in pin, 100-199 is MUX or ADS address, 200-299 is digital in pin, 300-399 is SPI pin, 400-599 is an I2C address. Negative values mean the same, but that there is an associated power pin. -9999 means no pin. 
  *correctedPin = -1;
  if (pin == -9999) return 0;
  if (pin >= 0 && pin < 100) {
    *correctedPin = pin;
    return 1;
  }
  if (pin >= -99 && pin < 0) {
    *correctedPin = -1*pin;
    return 2;
  }
  if (pin >= 100 && pin < 200) {
    *correctedPin = pin-100;
    return 5;
  }
  if (pin >= -199 && pin <= -100) {
    *correctedPin = -1*pin-100;
    return 6;
  }
  if (pin >= 200 && pin < 300) {
    *correctedPin = pin-200;
    return 3;
  }
  if (pin >= -299 && pin <= -200) {
    *correctedPin = -1*pin-200;
    return 4;
  }
  if (pin >= 300 && pin < 400) {
    *correctedPin = pin-300;
    return 7;
  }
  if (pin >= -399 && pin <= -300) {
    *correctedPin = -1*pin-300;
    return 8;
  }
  if (pin >= 400 && pin < 600) {
    *correctedPin = pin-400;
    return 9;
  }
  if (pin >= -599 && pin <= -400) {
    *correctedPin = -1*pin-400;
    return 10;
  }
  return 0;
}


float readAnalogVoltage(ArborysSnsType* P, byte nsamps) {
  return readAnalogVoltage(P->snsPin, nsamps);
}

float readAnalogVoltage(int16_t pin, byte nsamps) {
  float val = 0;
  #ifdef _USEADCATTEN
  if (pin < 0) pin = -1*pin;

  pinMode(pin, INPUT);
  for (byte ii=0;ii<nsamps;ii++) {
    val += (float) analogRead(pin); //analog pin. Note that not all ESP32 boards have the correct internal lookup for analogReadMilliVolts(), so we use analogRead() instead.
    if (ii<nsamps-1) delay(10);
  }
  //normalize by the actual full-scale count for the configured resolution. analogReadResolution(_USEADCBITS)
  //makes analogRead() return 0..(2^_USEADCBITS - 1), so dividing by 4095 (12-bit) would compress the scale.
  #ifdef _USEADCBITS
  val = (val/nsamps) / (float)((1UL << _USEADCBITS) - 1);
  #else
  val = (val/nsamps)/4095.0;
  #endif
  //output range depends on the attenuation settings. 
  if (_USEADCATTEN == ADC_6db) {
    val= val*1.75; 
  } else if (_USEADCATTEN == ADC_2_5db) {
    val= val*1.25; 
  } else if (_USEADCATTEN == ADC_0db) {
    val= val*0.95; 
  }
  else { //assume 11db
    val= val*2.450; 
  }
  #endif
  return val;
}

float readPinValue(ArborysSnsType* P, byte nsamps) {
//wrapper function to call readPinValue with the sensor pointer and the number of samples
  return readPinValue(P->snsPin, nsamps, P->powerPin);
}


float readPinValue(int16_t pin, byte nsamps, int16_t powerPin) {
  //direct call function to read the pin value
  float val=0;


  int8_t correctedPin=-1;
  uint8_t pintype = getPinType(pin, &correctedPin);
      
  if (pintype == 1 || pintype == 2) {
    val = readAnalogVoltage(pin, nsamps);
  }

  if (pintype == 3 || pintype == 4) {
    val = digitalRead(correctedPin); //digital pin, high is dry
  }

  if ((pintype == 5 || pintype == 6)) {
    //use MUX to read the pin
    #ifdef _USEMUX
    val = readMUX(correctedPin, nsamps);
    #endif
  }


  return val; 
}
  

#ifdef _USEMUX
double readMUX(int16_t pin, byte nsamps) {
  double val = 0;
  digitalWrite(MUXPINS[0],bitRead(pin,0));
  digitalWrite(MUXPINS[1],bitRead(pin,1));
  digitalWrite(MUXPINS[2],bitRead(pin,2));
  digitalWrite(MUXPINS[3],bitRead(pin,3));  
  val = readPinValue(MUXPINS[4],nsamps);
  digitalWrite(MUXPINS[0],HIGH);
  digitalWrite(MUXPINS[1],HIGH);
  digitalWrite(MUXPINS[2],HIGH);
  digitalWrite(MUXPINS[3],HIGH);
  return val;
}
#endif

#endif


