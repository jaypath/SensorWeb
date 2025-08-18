#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <Arduino.h>
#include "globals.hpp"

// serial printing
bool SerialPrint(const char* S, bool newline=false );
bool SerialPrint(String S, bool newline=false);

// Errors and resets
void storeError(const char* E, ERRORCODES Z=ERROR_UNDEFINED, bool writeToSD = false);

// string helpers
String breakString(String *inputstr,String token,bool reduceOriginal=true);
uint16_t countSubstr(String orig, String token);

// IP/MAC helpers
String IPToString(uint32_t ip);
uint32_t StringToIP(String str);
uint32_t IPToUint32(byte* ip);
void uint32toIP(uint32_t ip32, byte* ip);
String IPbytes2String(byte* IP,byte len=4);
bool IPString2ByteArray(String IPstr,byte* IP);
uint64_t IPToMACID(uint32_t ip);
uint64_t IPToMACID(byte* ip);
void uint64ToMAC(uint64_t mac64, byte* macArray);
uint64_t MACToUint64(byte* macArray);
String MACToString(uint64_t mac64);
String MACToString(uint8_t* mac);

#endif

