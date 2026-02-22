#ifndef _TIMESETUPH

#define _TIMESETUPH


#include <Arduino.h>

#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Forward declarations - avoid circular includes since globals.hpp includes this file
struct STRUCT_CORE;

extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern char DATESTRING[];
extern STRUCT_CORE I;

bool getTimezoneInfo();
bool updateTime();
void DSTsetup(void);
bool setupTime(void);

String fcnDOW(time_t t, bool caps=false);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s, bool asLocalTime=true);
time_t convertStrTime(String str, bool asLocalTime=true);
time_t iso8601ToUnix(String iso, bool asLocalTime=true);

#endif

