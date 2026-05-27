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

bool getTimezoneInfo(double latitude=0, double longitude=0, bool updateUtcOffset=true);
bool updateTime();
void DSTsetup(void);
bool setupTime(void);
bool syncNtpAndApplyDST(); // NTP sync + DSTsetup using Prefs rules (no TimeAPI call)

String fcnDOW(time_t t, bool caps=false);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s, bool asLocalTime=true, bool standardLocalOnly=false);
time_t convertStrTime(String str, bool asLocalTime=true, bool standardLocalOnly=false);
time_t convertDstBoundaryStrTime(String str); // local standard wall time -> UTC
time_t iso8601ToUnix(String iso);
time_t unixToLocal(time_t unixTime);
time_t dstBoundaryLocalDisplay(time_t utcBoundary); // UTC boundary -> scalar for dateify()
char* dateifyDstBoundary(time_t utcBoundary, String format = "mm/dd/yyyy hh:nn");
bool isInDstInterval(time_t utc); // true when utc is inside [DSTStart, DSTEnd)

#endif

