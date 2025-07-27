#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <Arduino.h>
#include "globals.hpp"
#include "SDCard.hpp"
#include "Devices.hpp"
#include "timesetup.hpp"

//serial printing
bool SerialPrint(const char* S, bool newline=false );
bool SerialPrint(String S, bool newline=false);

// Legacy compatibility functions - these are now methods of the Devices_Sensors class
// but we keep them as standalone functions for backward compatibility

void storeError(const char* E, ERRORCODES Z=ERROR_UNDEFINED, bool writeToSD = true);
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
String breakString(String *inputstr,String token,bool reduceOriginal=true);
uint16_t countSubstr(String orig, String token);
String enumErrorToName(ERRORCODES E);
bool cycleIndex(uint16_t* start, uint16_t arraysize, uint16_t origin);
bool cycleByteIndex(byte* start, byte arraysize, byte origin);

// Legacy sensor functions - these now delegate to the Devices_Sensors class
bool isSensorInit(int i);
bool isMACSet(byte *m, bool doReset=false);
bool compareMAC(byte *MAC1,byte *MAC2);

int16_t findDev(byte* macID, byte ardID, byte snsType, byte snsID,  bool oldest);
int16_t findSnsOfType(byte snstype, bool newest = false);
uint8_t countFlagged(int snsType=0, uint8_t flagsthatmatter = 0b00000011, uint8_t flagsettings= 0b00000011, uint32_t MoreRecentThan=0);
uint8_t countDev();
void checkHeat(void);
uint8_t findSensorByName(String snsname, uint8_t snsType=0, uint8_t snsID = 0);
uint8_t find_sensor_count(String snsname,uint8_t snsType);
void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow);
void initSensor(int k); //k is the index to sensor to init. use -256 [anything <-255] to clear all, and any number over 255 to clear expired (in which case the value of k is the max age in minutes)
byte checkExpiration(int i, time_t t=0,bool onlycritical=true);


// --- IP address conversion utilities ---
String ArrayToString(byte* Arr, byte len, char separator= '.', bool asHex = false);
String IPToString(uint32_t ip);
String IPbytes2String(byte* IP,byte len=4);
uint32_t StringToIP(String str);
bool IPString2ByteArray(String IPstr,byte* IP);
uint32_t IPToUint32(byte* ip);
void uint32toIP(uint32_t ip32, byte* ip);
uint64_t IPToMACID(uint32_t ip);
uint64_t IPToMACID(byte* ip);

// --- MAC address conversion utilities ---
void uint64ToMAC(uint64_t mac64, byte* macArray);
uint64_t MACToUint64(byte* macArray);
String MACToString(uint64_t mac64);
String MACToString(uint8_t* mac); //wrapper for ip2string

// --- PROCID byte access utility ---
uint8_t getPROCIDByte(uint64_t procid, uint8_t byteIndex);


#endif