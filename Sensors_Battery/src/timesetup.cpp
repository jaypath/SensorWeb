#include "timesetup.hpp"
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <server.hpp>


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov",(long) GLOBAL_TIMEZONE_OFFSET,10800000); //3rd param is offset, 4th param is update frequency
long DSTOFFSET = 0;

char DATESTRING[25]="";



bool checkTime(void) {

  uint32_t td = now(); 
  
  if ( WifiStatus()  && (td>2208992400 || td<1704070800)) return false;
  return true;

}


//Time fcn
bool updateTime(byte retries,uint16_t waittime) {
  bool isgood = timeClient.update();
  byte i=1;


  while (i<retries && isgood==false) {
    i++; 
    isgood = timeClient.update();
    if (isgood==false) {
      delay(waittime);

      #ifdef _DEBUG
        Serial.printf("timeupdate: Attempt %u... Time is: %s and timeclient failed to update.\n",i,dateify(now(),"mm/dd/yyyy hh:mm:ss"));
      #endif
    }
  } 

  if (isgood) checkDST();
  return isgood;
}

void checkDST(void) {
  timeClient.setTimeOffset((long) GLOBAL_TIMEZONE_OFFSET);
  setTime(timeClient.getEpochTime());
  
  time_t n=now();

#ifdef _DEBUG
  Serial.printf("checkDST: Starting time EST is: %s\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"));
#endif


//check if time offset is EST (-5h) or EDT (-4h)
int m = month(n);
int d = day(n);
int dow = weekday(n); //1 is sunday

  if (m > 3 && m < 11) DSTOFFSET = 3600;
  else {
    if (m == 3) {
      //need to figure out if it is past the second sunday at 2 am
      if (d<8) DSTOFFSET = 0;
      else {
        if (d>13)  DSTOFFSET = 3600; //must be past second sunday... though technically could be the second sunday and before 2 am... not a big error though
        else {
          if (d-dow+1>7) DSTOFFSET = 3600; //d-dow+1 is the date of the most recently passed sunday. if it is >7 then it is  the second sunday or more
          else DSTOFFSET = 0;
        }
      }
    }

    if (m == 11) {
      //need to figure out if it is past the first sunday at 2 am
      if (d>7)  DSTOFFSET = 0; //must be past first sunday... though technically could be the second sunday and before 2 am... not a big error though
      else {
        if ((int) d-dow+1>1) DSTOFFSET = 0; //d-dow+1 is the date of the most recently passed sunday. if it is >1 then it is past the first sunday
        else DSTOFFSET = 3600;
      }
    }
  }

  timeClient.setTimeOffset((long) GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  setTime(timeClient.getEpochTime());

  #ifdef _DEBUG
    Serial.printf("checkDST: Ending time is: %s\n\n",dateify(n,"mm/dd/yyyy hh:mm:ss"));
  #endif
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

  snprintf(holder,5,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  snprintf(holder,5,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  snprintf(holder,5,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  snprintf(holder,5,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  snprintf(holder,5,"%02d",hour(t));
  dateformat.replace("hh",holder);

  snprintf(holder,5,"%02d",minute(t));
  dateformat.replace("nn",holder);

  snprintf(holder,5,"%02d",second(t));
  dateformat.replace("ss",holder);
  
  snprintf(DATESTRING,25,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

