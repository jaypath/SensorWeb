#include "timesetup.hpp"
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <server.hpp>


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov");
int DSTOFFSET = 0;

char DATESTRING[20]="";



bool checkTime(void) {

  uint32_t td = now(); 
  
  if ( WifiStatus()  && (td>2208992400 || td<1704070800)) return false;
  return true;

  
}

//Time fcn
time_t timeUpdate(void) {

  
  timeClient.update();

        
  if (month() < 3 || (month() == 3 &&  day() < 10) || month() ==12 || (month() == 11 && day() >= 3)) DSTOFFSET = -1*60*60; //2024 DST offset
  else DSTOFFSET = 0;

  setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);

  if (checkTime()==false) return 0; //not a possible time

  return now();
}


time_t setupTime(void) {
    timeClient.begin();
    timeClient.update();


    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET);

    if (month() < 3 || (month() == 3 &&  day() < 12) || month() ==12 || (month() == 11 && day() >= 5)) DSTOFFSET = -1*60*60;
    else DSTOFFSET = 0;

    setTime(timeClient.getEpochTime()+GLOBAL_TIMEZONE_OFFSET+DSTOFFSET); //set stoffregen timelib time once, to get month and day. then reset with DST

    if (checkTime()==false) return 0;

    return now();
}

String fcnDOW(time_t t) {
    if (weekday(t) == 1) return "Sun";
    if (weekday(t) == 2) return "Mon";
    if (weekday(t) == 3) return "Tue";
    if (weekday(t) == 4) return "Wed";
    if (weekday(t) == 5) return "Thu";
    if (weekday(t) == 6) return "Fri";
    if (weekday(t) == 7) return "Sat";

    return "???";
}

char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  snprintf(holder,4,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  snprintf(holder,4,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  snprintf(holder,4,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  snprintf(holder,4,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  snprintf(holder,4,"%02d",hour(t));
  dateformat.replace("hh",holder);

  snprintf(holder,4,"%02d",minute(t));
  dateformat.replace("nn",holder);

  snprintf(holder,4,"%02d",second(t));
  dateformat.replace("ss",holder);
  
  snprintf(DATESTRING,19,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

