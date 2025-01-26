#define GLOBAL_TIMEZONE_OFFSET -14400
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern int DSTOFFSET;

time_t timeUpdate();
time_t setupTime(void);