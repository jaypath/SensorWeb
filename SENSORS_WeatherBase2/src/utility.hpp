#ifndef UTILITY_HPP
#define UTILITY_HPP

#include "Arduino.h"
#include <globals.hpp>
#include <timesetup.hpp>
#include "FS.h"
#include <SD.h>

extern SensorVal Sensors[SENSORNUM];


int inArray(int arrind[], int N, int value);
bool inIndex(byte lookfor,byte used[],byte arraysize);
void pushDoubleArray(double arr[], byte N, double value);
void Byte2Bin(uint8_t value, char* output, bool invert = false);
bool inIndex(byte lookfor,byte used[],byte arraysize);
char* strPad(char* str, char* pad, byte L);
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
bool stringToLong(String s, uint32_t* val);
int16_t cumsum(int16_t * arr, int16_t ind1, int16_t ind2);
String breakString(String *inputstr,String token);


int16_t findDev(struct SensorVal *S, bool oldest = false);
int16_t findSns(byte snstype, bool newest = false);
uint8_t countFlagged(int snsType=0, uint8_t flagsthatmatter = B00000011, uint8_t flagsettings= B00000011, uint32_t MoreRecentThan=0);
uint8_t countDev();
void checkHeat(void);
uint8_t find_sensor_name(String snsname, uint8_t snsType, uint8_t snsID = 255);
uint8_t find_sensor_count(String snsname,uint8_t snsType);
void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
void initSensor(int k); //k is the index to sensor to init. use -256 [anything <-255] to clear all, and any number over 255 to clear expired (in which case the value of k is the max age in minutes)



#endif