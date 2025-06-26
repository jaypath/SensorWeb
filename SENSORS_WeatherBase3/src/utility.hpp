#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <Arduino.h>
#include "globals.hpp"

// Legacy compatibility functions - these are now methods of the Devices_Sensors class
// but we keep them as standalone functions for backward compatibility

void storeError(const char* E);
String lastReset2String(bool addtime=true);
void controlledReboot(const char* E, RESETCAUSE R,bool doreboot=true);
int inArrayBytes(byte arr[], int N, byte value,bool returncount=false);
int inArray(int arr[], int N, int value,bool returncount=false);
bool inIndex(byte lookfor,byte used[],byte arraysize);
void pushDoubleArray(double arr[], byte N, double value);
void Byte2Bin(uint8_t value, char* output, bool invert = false);
char* strPad(char* str, char* pad, byte L);
bool stringToLong(String s, uint32_t* val);
int16_t cumsum(int16_t * arr, int16_t ind1, int16_t ind2);
String breakString(String *inputstr,String token,bool reduceOriginal=false);
uint16_t countSubstr(String orig, String token);

// Legacy sensor functions - these now delegate to the Devices_Sensors class
bool isSensorInit(int i);
bool isMACSet(byte *m, bool doReset=false);
bool compareMAC(byte *MAC1,byte *MAC2);

int16_t findDev(byte* macID, byte ardID, byte snsType, byte snsID,  bool oldest);
int16_t findSns(byte snstype, bool newest = false);
uint8_t countFlagged(int snsType=0, uint8_t flagsthatmatter = 0b00000011, uint8_t flagsettings= 0b00000011, uint32_t MoreRecentThan=0);
uint8_t countDev();
void checkHeat(void);
uint8_t find_sensor_name(String snsname, uint8_t snsType, uint8_t snsID = 255);
uint8_t find_sensor_count(String snsname,uint8_t snsType);
void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
void initSensor(int k); //k is the index to sensor to init. use -256 [anything <-255] to clear all, and any number over 255 to clear expired (in which case the value of k is the max age in minutes)
byte checkExpiration(int i, time_t t=0,bool onlycritical=true);
String IPbytes2String(byte* IP,byte len=4);
bool IPString2ByteArray(String IPstr,byte* IP);

// --- IP address conversion utilities ---
String IPToString(uint32_t ip);
bool StringToIP(const String& str, uint32_t& ip);

#endif