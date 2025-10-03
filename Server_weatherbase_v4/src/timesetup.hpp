#ifndef _TIMESETUPH

#define _TIMESETUPH


#include <Arduino.h>

#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "globals.hpp"


extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern char DATESTRING[];
extern STRUCT_CORE I;

void checkDST(void);
bool updateTime();
bool checkTime(void);
bool setupTime(void);
void checkTimezoneUpdate();
String fcnDOW(time_t t, bool caps=false);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s) ;
time_t convertStrTime(String str);
#endif

