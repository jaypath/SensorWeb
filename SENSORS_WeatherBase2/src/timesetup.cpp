//#define _DEBUG
#include "timesetup.hpp"
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <server.hpp>

#define TIMEUPDATEINT 10800000

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov",(long) GLOBAL_TIMEZONE_OFFSET,10800000); //3rd param is offset, 4th param is update frequency
long DSTOFFSET = 0;

char DATESTRING[25]="";


time_t convertStrTime(String str) //take a text time, like "12:34:13 PM" and return unix time. If month, day, year are not provided then assume current m/d/y
{
  //break string... format must be one of "10/24/2024", "10/24/24", "10/24/2024 13:23:14", "10/24/2024 1:23:14 PM", "13:23:25", although the seconds and am/pm  are optional

    int m = month(), d=day(), h=0, n=0, s=0;
    int yy = year()-2000;
    String datesplit = "";

  if (str.indexOf("/")>-1) datesplit = "/"; //date is included
  if (str.indexOf("-")>-1) datesplit = "-";//date is included

  if (datesplit!="")
  {
    m = breakString(&str,datesplit).toInt();
    if (m>12) {
      yy=m;
      m = breakString(&str,datesplit).toInt();
      if (str.indexOf(" ")<0) //time is not included
      {
        d = str.toInt();      
      } else {
        d = breakString(&str," ").toInt();
      }
    } else {
      d = breakString(&str,"/").toInt();
      if (str.indexOf(" ")<0) //time is not included
      {
        yy = str.toInt();      
      } else {
        yy = breakString(&str," ").toInt();
      }
    }
    
    if (yy>2000) yy = yy-2000;
    else if (yy>1970) yy=yy-1900;

    if (str.length()==0) return makeUnixTime((byte) yy, m,d,h,n,s);
  }

  byte hoffset = 0;
  if (str.indexOf(" ")>-1) { //there is a " AM/PM" present
    if (str.indexOf("PM")>-1)  hoffset=12;
    str = breakString(&str," "); //remove the am/pm
  }
  h = breakString(&str,":").toInt() + hoffset;
  n = breakString(&str,":").toInt();



  if(str.length()==0) return makeUnixTime((byte) yy, m,d,h,n,s); //no sec data

  s = str.toInt();
  return makeUnixTime((byte) yy, m,d,h,n,s);

}

time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s) {
  //here yy is the year after 2000, so 0 = 2000 and 24 = 2024... unless yy>=70 (in which case it is year after 1900)

  int16_t y = 2000;

  if (yy>=70) y = 1900+yy;
  else y=2000+yy;

  tmElements_t unixTime;

  unixTime.Year = y-1970; //years since 1970
  unixTime.Month = m;
  unixTime.Day = d;
  unixTime.Hour = h;
  unixTime.Minute = n;
  unixTime.Second = s;

  return makeTime(unixTime);
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
   DSTOFFSET = 0; //starting default
  timeClient.setTimeOffset((long) GLOBAL_TIMEZONE_OFFSET);
  setTime(timeClient.getEpochTime());
  
  time_t n=now();

#ifdef _DEBUG
  Serial.printf("checkDST: Starting time EST is: %s\n",dateify(now(),"mm/dd/yyyy hh:mm:ss"));
#endif



//check if time offset is EST (-5h) or EDT (-4h)
  int m=month();

  if (m > 3 && m < 11) DSTOFFSET = 3600;
  else {
    if (m == 3) {
      //need to figure out if it is past the second sunday at 2 am

      time_t m1 = makeUnixTime(year(),month(),7,2,0,0); //this is the last day of the first week of March at 2 am. We want second sunday at 2 am
      if (n< m1 + ((7-weekday(m1)+1)*24*60*60)) DSTOFFSET = 0; 
      else DSTOFFSET = 3600;

      //7-weekday(m1)+1 is the next sunday 
      /*explanation:
        weekday(m1) //is the weekday of m1, the last day of the first week. If it is Sunday (1), then add 7 to get to the second sunday...
        if it is 1, then add 7 = 7-weekday(m1)+1
        if it is 2, then add 6 = 7-weekday(m1)+1
        if it is 3 then add 5 = 7-weekday(m1)+1
        if it is 4 then add 4 = 7-weekday(m1)+1
        if it is 5 then add 3 = 7-weekday(m1)+1
        if it is 6 then add 2 = 7-weekday(m1)+1
        if it is 7 then add 1 = 7-weekday(m1)+1
      */

    }

    if (m == 11) {
      //need to figure out if it is past the first sunday at 2 am
      time_t m1 = makeUnixTime(year(),month(),1,2,0,0); //this is the first day of the first week of Nov at 2 am. We want first sunday at 2 am
      if (weekday(m1)>1) m1 += (7-(weekday(m1)-1))*86400; //this is the first sunday of the month.
      if (n< m1) DSTOFFSET = 3600; //still in the summer timezone
      else DSTOFFSET = 0;

    /*explanation...
      if weekday(m1) is 1 then it is sunday, add 0
      if 2 then Monday, add 6
      if 3 then Tue, add 5
      ... +4
      +3
      +2
      +1

      so that is 7 - (weekday(m1)-1)

    */
      
    }
  }

  timeClient.setTimeOffset((long) GLOBAL_TIMEZONE_OFFSET+DSTOFFSET);
  setTime(timeClient.getEpochTime());

  #ifdef _DEBUG
    Serial.printf("checkDST: Ending time is: %s\n\n",dateify(n,"mm/dd/yyyy hh:mm:ss"));
  #endif
}


String fcnDOW(time_t t, bool caps) {
  if (caps) {
    if (weekday(t) == 1) return "SUN";
    if (weekday(t) == 2) return "MON";
    if (weekday(t) == 3) return "TUE";
    if (weekday(t) == 4) return "WED";
    if (weekday(t) == 5) return "THU";
    if (weekday(t) == 6) return "FRI";
    if (weekday(t) == 7) return "SAT";

  } else {
    if (weekday(t) == 1) return "Sun";
    if (weekday(t) == 2) return "Mon";
    if (weekday(t) == 3) return "Tue";
    if (weekday(t) == 4) return "Wed";
    if (weekday(t) == 5) return "Thu";
    if (weekday(t) == 6) return "Fri";
    if (weekday(t) == 7) return "Sat";
  }
    return "???";
}


char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  snprintf(holder,4,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  snprintf(holder,4,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  snprintf(holder,5,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  snprintf(holder,4,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  snprintf(holder,4,"%02d",hour(t));
  dateformat.replace("hh",holder);

  snprintf(holder,4,"%d",hour(t));
  dateformat.replace("h1",holder);

  snprintf(holder,4,"%d",hourFormat12(t));
  dateformat.replace("H",holder);

  snprintf(holder,4,"%02d",minute(t));
  dateformat.replace("nn",holder);

  snprintf(holder,4,"%02d",second(t));
  dateformat.replace("ss",holder);

  snprintf(holder,4,"%s",fcnDOW(t,true).c_str());
  dateformat.replace("DOW",holder);

  snprintf(holder,4,"%s",fcnDOW(t,false).c_str());
  dateformat.replace("dow",holder);

  if (dateformat.indexOf("XM")>-1) dateformat.replace("XM",(isPM(t)==true)?"PM":"AM");
  
  snprintf(DATESTRING,25,"%s",dateformat.c_str());
  
  return DATESTRING;  
}


