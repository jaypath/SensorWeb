#ifndef SENSORS_HPP
#define SENSORS_HPP
#include <Arduino.h>
#include <header.hpp>
#include <timesetup.hpp>

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
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value
};

extern SensorVal Sensors[SENSORNUM];

bool ReadData(struct SensorVal *P);
byte find_limit_sensortypes(String snsname, byte snsType,bool highest);
byte find_sensor_count(String snsname,byte snsType);
byte find_sensor_name(String snsname,byte snsType,byte snsID);
uint8_t countDev();
void initSensor(byte k);
uint16_t findOldestDev();
uint8_t findSensor(byte snsType, byte snsID);
bool checkSensorValFlag(struct SensorVal *P);
#ifdef _USEBME680
  void read_BME680();
#endif

#endif