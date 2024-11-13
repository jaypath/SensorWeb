#ifndef _TIMESETUPH

#define _TIMESETUPH


#define GLOBAL_TIMEZONE_OFFSET -18000
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <header.hpp>


extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern long DSTOFFSET;
extern char DATESTRING[];

void checkDST(void);
bool updateTime(byte retries=10,uint16_t waittime=250);
bool checkTime(void);
String fcnDOW(time_t t, bool caps=false);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s);
#endif