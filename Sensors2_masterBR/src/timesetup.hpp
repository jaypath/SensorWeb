#ifndef _TIMESETUPH

#define _TIMESETUPH


#define GLOBAL_TIMEZONE_OFFSET -14400
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern int DSTOFFSET;
extern char DATESTRING[];

bool checkTime();
bool updateTime(byte retries,uint16_t waittime);
void checkDST(void);
String fcnDOW(time_t t);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
#endif