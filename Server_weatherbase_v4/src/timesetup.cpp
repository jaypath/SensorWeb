//#define _DEBUG
#include "timesetup.hpp"
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <server.hpp>

#define TIMEUPDATEINT 10800000

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov",0,TIMEUPDATEINT); //3rd param is offset, 4th param is update frequency

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
    m = breakString(&str,datesplit,true).toInt();
    if (m>12) {
      yy=m;
      m = breakString(&str,datesplit,true).toInt();
      if (str.indexOf(" ")<0) //time is not included
      {
        d = str.toInt();      
      } else {
        d = breakString(&str," ",true).toInt();
      }
    } else {
      d = breakString(&str,datesplit,true).toInt();
      if (str.indexOf(" ")<0) //time is not included
      {
        yy = str.toInt();      
      } else {
        yy = breakString(&str," ",true).toInt();
      }
    }
    
    if (yy>2000) yy = yy-2000;
    else if (yy>1970) yy=yy-1900;

      SerialPrint((String) "   m=" + m + "...\n");
            SerialPrint((String) "   d=" + d + "...\n");
                  SerialPrint((String) "   yy=" + yy + "...\n");



    SerialPrint((String) "unixtime = " + makeUnixTime((byte) yy, m,d,h,n,s) + " humantime = " + m + "/" + d + "/" + yy + " " + h + ":" + n,true);

    if (str.length()==0) return makeUnixTime((byte) yy, m,d,0,0,0);
  }

  byte hoffset = 0;
  if (str.indexOf(" ")>-1) { //there is a " AM/PM" present
    if (str.indexOf("PM")>-1)  hoffset=12;
    str = breakString(&str," ",true); //remove the am/pm
  }
  h = breakString(&str,":",true).toInt() + hoffset;
  n = breakString(&str,":",true).toInt();

  if(str.length()==0) return makeUnixTime((byte) yy, m,d,h,n,s); //no sec data

  s = str.toInt();

  SerialPrint((String) "unixtime = " + makeUnixTime((byte) yy, m,d,h,n,s) + " humantime = " + m + "/" + d + "/" + yy + " " + h + ":" + n,true);

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
bool updateTime() {

  bool isgood = false;

  if ( timeClient.update()) {  //returns false if not time to update
    setTime(timeClient.getEpochTime());
    isgood = true;
  }

  I.currentTime=now();    
  return isgood;
}

void checkDST(void) {
  
 
#ifdef _DEBUG
  Serial.printf("checkDST: Starting time EST is: %s\n",dateify(I.currentTime,"mm/dd/yyyy hh:mm:ss"));
#endif

  //check if time offset is EST (-5h) or EDT (-4h)
  int m=month(I.currentTime);
  int y=year(I.currentTime);

  if (m > 3 && m < 11) {
    // Summer months (April through October) - DST is in effect
    I.DSTOFFSET = 3600;
  } else if (m == 3) {
    // March - DST starts on second Sunday at 2 AM
    // Find the second Sunday of March
    time_t march1 = makeUnixTime(y-2000, 3, 1, 2, 0, 0); // March 1st at 2 AM
    int firstSunday = 1 + (8 - weekday(march1)) % 7; // Days to add to get to first Sunday
    if (firstSunday == 8) firstSunday = 1; // If March 1st is already Sunday
    
    time_t secondSunday = makeUnixTime(y-2000, 3, firstSunday + 7, 2, 0, 0); // Second Sunday at 2 AM
    
    if (I.currentTime >= secondSunday) {
      I.DSTOFFSET = 3600; // DST is in effect
    } else {
      I.DSTOFFSET = 0; // Still in standard time
    }
    
  } else if (m == 11) {
    // November - DST ends on first Sunday at 2 AM
    // Find the first Sunday of November
    time_t nov1 = makeUnixTime(y-2000, 11, 1, 2, 0, 0); // November 1st at 2 AM
    int firstSunday = 1 + (8 - weekday(nov1)) % 7; // Days to add to get to first Sunday
    if (firstSunday == 8) firstSunday = 1; // If November 1st is already Sunday
    
    time_t firstSundayTime = makeUnixTime(y-2000, 11, firstSunday, 2, 0, 0); // First Sunday at 2 AM
    
    if (I.currentTime < firstSundayTime) {
      I.DSTOFFSET = 3600; // Still in DST
    } else {
      I.DSTOFFSET = 0; // Back to standard time
    }
  }

  timeClient.setTimeOffset(I.GLOBAL_TIMEZONE_OFFSET+I.DSTOFFSET);
  timeClient.forceUpdate();
  setTime(timeClient.getEpochTime());
   I.currentTime = now();


  #ifdef _DEBUG
    Serial.printf("checkDST: Ending time is: %s (DST offset: %ld seconds)\n\n",dateify(I.currentTime,"mm/dd/yyyy hh:mm:ss"), DSTOFFSET);
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
  if (t==0) t = I.currentTime;

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

bool setupTime(void) {
    timeClient.begin();
    timeClient.setTimeOffset(I.GLOBAL_TIMEZONE_OFFSET+I.DSTOFFSET);

    byte i=0;
    
    while (timeClient.forceUpdate()==false && i<25) {
      i++;
    }
    if (i>=25) return false;

    setTime(timeClient.getEpochTime());
    I.currentTime = now();


    checkDST(); //this also sets timelib, so no need to call settime separately


    return true;
}


