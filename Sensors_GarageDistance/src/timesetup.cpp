#include "timesetup.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov");
int DSTOFFSET = 0;

//Time fcn
time_t timeUpdate(void) {
  timeClient.update();
  if (month() < 3 || (month() == 3 &&  day() < 10) || month() ==12 || (month() == 11 && day() >= 3)) DSTOFFSET = -1*60*60; //2024 DST offset
  else DSTOFFSET = 0;

  setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  return now();
}


time_t setupTime(void) {
    timeClient.begin();
    timeClient.update();
    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET);

    if (month() < 3 || (month() == 3 &&  day() < 10) || month() ==12 || (month() == 11 && day() >= 3)) DSTOFFSET = -1*60*60;
    else DSTOFFSET = 0;

    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET); //set stoffregen timelib time once, to get month and day. then reset with DST

    //set the stoffregen time library with timezone and dst
    time_t t = timeUpdate();
    return t;
}


