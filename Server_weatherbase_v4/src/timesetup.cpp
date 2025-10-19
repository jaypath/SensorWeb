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


bool setupTime(void) {
  timeClient.begin();
  
  // Initialize with base timezone offset from Prefs
  timeClient.setTimeOffset(Prefs.TimeZoneOffset +  Prefs.DSTOffset );
  setTime(timeClient.getEpochTime());

  byte i=0;
  
  while (timeClient.forceUpdate()==false && i<25) {
    i++;
  }
  if (i>=25) return false;
  checkDST(); //this also sets timelib, so no need to call settime separately

  setTime(timeClient.getEpochTime());
  I.currentTime = now();


  return true;
}



//Time fcn
bool updateTime() {

  bool isgood = false;

  if ( timeClient.update()) {  //returns false if not time to update
    setTime(timeClient.getEpochTime());
    
    // Check DST and update offset accordingly
    checkDST();

    isgood = true;
  }

  I.currentTime=now();    
  return isgood;
}

void checkDST(void) {
  

  timeClient.setTimeOffset(Prefs.TimeZoneOffset);
  setTime(timeClient.getEpochTime()); //set time without DST offset
  I.currentTime = now();  

  if (Prefs.DST==0) {
    Prefs.DSTOffset=0;
    return;
  }


  int m = month(I.currentTime);
  int d = day(I.currentTime);
  int y = year(I.currentTime);
  
  // Check if current date is within DST period  
  // Check if we're after DST start date
  if (m > Prefs.DSTStartMonth || (m == Prefs.DSTStartMonth && d >= Prefs.DSTStartDay)) {
    // Check if we're before DST end date
    if (m < Prefs.DSTEndMonth || (m == Prefs.DSTEndMonth && d < Prefs.DSTEndDay)) {
  
      Prefs.DSTOffset = 3600;
    } else {
  
      Prefs.DSTOffset = 0;
    }
  } else {
    Prefs.DSTOffset = 0;
  }
  
  timeClient.setTimeOffset(Prefs.TimeZoneOffset + Prefs.DSTOffset);
  setTime(timeClient.getEpochTime());
  I.currentTime = now();  
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



// Function to periodically check and update timezone information
void checkTimezoneUpdate() {
  // Check once per day (86400 seconds = 24 hours)
  
  // Only check if we have WiFi connection and enough time has passed
  if (Prefs.isUpToDate==false || WifiStatus()==false ) {
      return;
  }
  
  SerialPrint("Checking for timezone updates...", true);
  
  // Get current timezone information from API
  int32_t new_utc_offset = 0;
  bool new_dst_enabled = false;
  uint8_t new_dst_start_month = 3, new_dst_start_day = 9;
  uint8_t new_dst_end_month = 11, new_dst_end_day = 2;
  
  // Call the getTimezoneInfo function from server.cpp
  extern bool getTimezoneInfo(int32_t* utc_offset, bool* dst_enabled, uint8_t* dst_start_month, uint8_t* dst_start_day, uint8_t* dst_end_month, uint8_t* dst_end_day);
  
  if (getTimezoneInfo(&new_utc_offset, &new_dst_enabled, &new_dst_start_month, &new_dst_start_day, &new_dst_end_month, &new_dst_end_day)) {
      bool needsUpdate = false;
      
      // Check if any timezone settings have changed
      if (Prefs.TimeZoneOffset != new_utc_offset) {
          SerialPrint("UTC offset changed from " + String(Prefs.TimeZoneOffset) + " to " + String(new_utc_offset), true);
          Prefs.TimeZoneOffset = new_utc_offset;
          needsUpdate = true;
      }
      
      if (Prefs.DST != (new_dst_enabled ? 1 : 0)) {
          SerialPrint("DST setting changed from " + String(Prefs.DST) + " to " + String(new_dst_enabled ? 1 : 0), true);
          Prefs.DST = new_dst_enabled ? 1 : 0;
          if (Prefs.DST) {
              Prefs.DSTOffset = 3600;
          } else {
              Prefs.DSTOffset = 0;
          }
          needsUpdate = true;
      }
      
      if (Prefs.DSTStartMonth != new_dst_start_month) {
          SerialPrint("DST start month changed from " + String(Prefs.DSTStartMonth) + " to " + String(new_dst_start_month), true);
          Prefs.DSTStartMonth = new_dst_start_month;
          needsUpdate = true;
      }
      
      if (Prefs.DSTStartDay != new_dst_start_day) {
          SerialPrint("DST start day changed from " + String(Prefs.DSTStartDay) + " to " + String(new_dst_start_day), true);
          Prefs.DSTStartDay = new_dst_start_day;
          needsUpdate = true;
      }
      
      if (Prefs.DSTEndMonth != new_dst_end_month) {
          SerialPrint("DST end month changed from " + String(Prefs.DSTEndMonth) + " to " + String(new_dst_end_month), true);
          Prefs.DSTEndMonth = new_dst_end_month;
          needsUpdate = true;
      }
      
      if (Prefs.DSTEndDay != new_dst_end_day) {
          SerialPrint("DST end day changed from " + String(Prefs.DSTEndDay) + " to " + String(new_dst_end_day), true);
          Prefs.DSTEndDay = new_dst_end_day;
          needsUpdate = true;
      }
      
      if (needsUpdate) {
          // Mark prefs as needing to be saved
          Prefs.isUpToDate = false;
          
          
          SerialPrint("Timezone information updated and saved to preferences", true);
          
          // Update the time client with new offset
          checkDST(); // Recalculate DST with new settings
      } else {
          SerialPrint("Timezone information is up to date", true);
      }
  } else {
      SerialPrint("Failed to fetch timezone information from API", true);
  }
  
}


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

