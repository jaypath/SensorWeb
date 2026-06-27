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

static constexpr uint16_t TIMEZONE_BOOT_HTTP_TIMEOUT_MS = 5000;
static constexpr uint16_t TIMEZONE_REFRESH_HTTP_TIMEOUT_MS = 10000;
static constexpr uint32_t TIMEZONE_REFRESH_INTERVAL_SEC = 3600;

bool timezonePrefsValid();
bool getTimezoneInfo(uint16_t timeoutMs = TIMEZONE_REFRESH_HTTP_TIMEOUT_MS);
bool refreshTimezoneFromNetwork(uint16_t timeoutMs = TIMEZONE_REFRESH_HTTP_TIMEOUT_MS);
bool updateTime();
void DSTsetup(void);
bool setupTime(void);
bool syncNtpAndApplyDST(); // NTP sync + DSTsetup using Prefs rules (no TimeAPI call)

String fcnDOW(time_t t, bool caps=false);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s, bool asLocalTime=true);
time_t convertStrTime(String str, bool asLocalTime=true);
time_t iso8601ToUnix(String iso);
time_t unixToLocal(time_t unixTime);

#endif

