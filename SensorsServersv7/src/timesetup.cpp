//#define _DEBUG
#include "globals.hpp"
#include "timesetup.hpp"
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <server.hpp>

#define TIMEUPDATEINT 10800000

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov",0,TIMEUPDATEINT); //3rd param is offset, 4th param is update frequency

char DATESTRING[25]="";

// Apply Prefs timezone + DST state to I.currentTime (requires valid TimeLib UTC via now()).
static void applyLocalTimeFromPrefs() {
  I.UTCTime = now();
  DSTsetup();
  I.currentTime = I.UTCTime + Prefs.TimeZoneOffset + (Prefs.DST > 0 ? (Prefs.DST - 1) * Prefs.DSTOffset : 0);
  I.currentSecond = second();
}

bool syncNtpAndApplyDST() {
  if (CheckWifiStatus(false) != 1) return false;

  timeClient.begin();
  timeClient.setTimeOffset(0);

  byte i = 0;
  while (timeClient.forceUpdate() == false && i < 250) {
    i++;
    delay(50);
  }
  if (i >= 250) return false;

  setTime(timeClient.getEpochTime());
  applyLocalTimeFromPrefs();

  SerialPrint("DST at boot: " + String(Prefs.DST == 0 ? "Not used" : (Prefs.DST == 1 ? "Inactive" : "Active")) +
              ", local=" + String(dateify(I.currentTime, "yyyy-mm-dd hh:nn:ss")), true, 5);
  return (I.UTCTime >= TIMEZERO);
}

bool setupTime(void) {
  //call this at startup
  if (!syncNtpAndApplyDST()) return false;

  getTimezoneInfo(); //this sets timezone and dstoffsets from TimeAPI
  applyLocalTimeFromPrefs();

  SerialPrint("DST settings are DST=" + String(Prefs.DST==0?"Not used":(Prefs.DST==1?"Inactive":"Active")) + ", DSTStartUnixTime=" + String(Prefs.DSTStartUnixTime) + ", DSTEndUnixTime=" + String(Prefs.DSTEndUnixTime) + ", DSTOffset=" + String(Prefs.DSTOffset),true);

  return true;
}



//Time fcn
bool updateTime() {

  //run this every second
  if (I.currentSecond == second()) return false;

  bool isgood = false;

  if ( timeClient.update()) {  //returns false if not time to update
    setTime(timeClient.getEpochTime());    
    isgood = true;
  }


  
  // Re-evaluate DST on transition instants (boundaries stored as UTC)
  if (Prefs.DST > 0 && Prefs.DSTStartUnixTime != 0) {
    time_t utc = now();
    if (utc == Prefs.DSTStartUnixTime || utc == Prefs.DSTEndUnixTime) {
      DSTsetup();
    }
  }


  I.UTCTime = now();
  I.currentTime = I.UTCTime + Prefs.TimeZoneOffset + (Prefs.DST>0?(Prefs.DST-1)*Prefs.DSTOffset:0);
  I.currentSecond = second();
  return isgood;
}

bool getTimezoneInfo(double latitude, double longitude, bool updateUtcOffset) {
  //this needs to run at startup and at DST changes
  //String myIP = getPublicIP();
  //if (myIP == "") return false;

  char tzURL[150];
// Query TimeAPI.io (Secure/HTTPS using Bundle)
SerialPrint("getTimezoneInfo: latitude=" + String(latitude) + ", longitude=" + String(longitude), true,5);

if (latitude==0 && longitude==0) {
  latitude = Prefs.LATITUDE;
  longitude = Prefs.LONGITUDE;
}

if (latitude==0 && longitude==0) { //use IP address to get timezone
  String myIP = getPublicIP();
  if (myIP == "") return false;
  snprintf(tzURL, 99, "https://timeapi.io/api/timezone/ip?ipAddress=%s", myIP.c_str());
} else {
  //use latitude and longitude to get timezone
  snprintf(tzURL, 99, "https://timeapi.io/api/timezone/coordinate?latitude=%f&longitude=%f", latitude, longitude);
}


JsonDocument doc;

HTTPMessage M;
M.setUrl(tzURL);
M.setMethod("GET");
M.setContentType("application/json");
M.setCacert("bundle");
M.timeout = 10000; // 5 second timeout for geocoding
M.usePSRAM = false;
M.responseDoc = &doc;

if (SendHTTPMessage(M)) {
  SerialPrint("getTimezoneInfo: HTTP request successful", true,5);
  if (doc["standardUtcOffset"]["seconds"].is<int32_t>()) {
    if (updateUtcOffset) {
      time_t tzOffset = doc["standardUtcOffset"]["seconds"].as<int32_t>();
      if (tzOffset != Prefs.TimeZoneOffset) {
        SerialPrint("TimeZoneOffset changed from " + String(Prefs.TimeZoneOffset) + " to " + String(tzOffset), true,5);
        Prefs.TimeZoneOffset = tzOffset;
        Prefs.isUpToDate = false;
      }
      SerialPrint("getTimezoneInfo: TimeZoneOffset set to " + String(Prefs.TimeZoneOffset), true,5);
    }
    // Check if DST exists
    Prefs.DST = doc["dstInterval"].isNull() ? 0 : 1; //DST is set to 0 if no dst here, or 1 if DST is used in this area. If 1, will be adjusted to 1 or 2 by DSTsetup()

    if (Prefs.DST>0) {
      Prefs.DSTStartUnixTime = iso8601ToUnix(doc["dstInterval"]["dstStart"].as<String>());
      Prefs.DSTEndUnixTime = iso8601ToUnix(doc["dstInterval"]["dstEnd"].as<String>());
      Prefs.DSTOffset = doc["dstInterval"]["dstOffsetToStandardTime"]["seconds"].as<int16_t>();
      time_t utc = (I.UTCTime != 0) ? I.UTCTime : now();
      Prefs.DST = isInDstInterval(utc) ? 2 : 1;
    } else {
      Prefs.DSTStartUnixTime = 0;
      Prefs.DSTEndUnixTime = 0;
      Prefs.DSTOffset = 0;
    }
    Prefs.isUpToDate = false;
    return true;
  } else {
    storeError("getTimezoneInfo: JSON had incorrect format", ERROR_JSON_PARSE,true);
    return false;
  }
} else {
  storeError("getTimezoneInfo: HTTP request failed with code: " + String(M.httpCode), ERROR_HTTP_REQUEST,true);
  return false;
}
return false;


}


bool isInDstInterval(time_t utc) {
  if (Prefs.DSTStartUnixTime == 0 || Prefs.DSTEndUnixTime == 0) {
    return false;
  }
  if (Prefs.DSTStartUnixTime < Prefs.DSTEndUnixTime) {
  // Northern hemisphere: DST window is [start, end)
    return utc >= Prefs.DSTStartUnixTime && utc < Prefs.DSTEndUnixTime;
  }
  // Southern hemisphere: DST window wraps across year end
  return utc >= Prefs.DSTStartUnixTime || utc < Prefs.DSTEndUnixTime;
}

time_t dstBoundaryLocalDisplay(time_t utcBoundary) {
  if (utcBoundary == 0) {
    return 0;
  }
  return utcBoundary + Prefs.TimeZoneOffset;
}

char* dateifyDstBoundary(time_t utcBoundary, String dateformat) {
  return dateify(dstBoundaryLocalDisplay(utcBoundary), dateformat);
}

void DSTsetup(void) {
//this function needs only be called  once a day to check if DST is active - or at known DST change times

  if (Prefs.DST==0) { // we don't use DST here
    Prefs.DSTOffset=0;        
    return;
  } 

  if (Prefs.DSTStartUnixTime == 0 || Prefs.DSTEndUnixTime == 0) {
    return;
  }

  int8_t DST_old = Prefs.DST;
  time_t utc = (I.UTCTime != 0) ? I.UTCTime : now();
  Prefs.DST = isInDstInterval(utc) ? 2 : 1;

  if (DST_old != Prefs.DST) {
    BootSecure bootSecure;
    bootSecure.setPrefs(true);

    SerialPrint("DST changed from " + String(DST_old) + " to " + String(Prefs.DST), true,5);
    //update dst
    getTimezoneInfo();
  }
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



time_t convertDstBoundaryStrTime(String str) {
  return convertStrTime(str, true, true);
}

time_t convertStrTime(String str, bool asLocalTime, bool standardLocalOnly) //take a text time, like "12:34:13 PM" and return unix time. If month, day, year are not provided then assume current m/d/y
{
  //if time is provided in local time, set asLocalTime to true

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



    SerialPrint((String) "unixtime = " + makeUnixTime((byte) yy, m,d,h,n,s,asLocalTime, standardLocalOnly) + " humantime = " + m + "/" + d + "/" + yy + " " + h + ":" + n,true);

    if (str.length()==0) return makeUnixTime((byte) yy, m,d,0,0,0,asLocalTime, standardLocalOnly);
  }

  byte hoffset = 0;
  if (str.indexOf(" ")>-1) { //there is a " AM/PM" present
    if (str.indexOf("PM")>-1)  hoffset=12;
    str = breakString(&str," ",true); //remove the am/pm
  }
  h = breakString(&str,":",true).toInt() + hoffset;
  n = breakString(&str,":",true).toInt();

  if(str.length()==0) return makeUnixTime((byte) yy, m,d,h,n,s,asLocalTime, standardLocalOnly); //no sec data

  s = str.toInt();

  SerialPrint((String) "unixtime = " + makeUnixTime((byte) yy, m,d,h,n,s) + " humantime = " + m + "/" + d + "/" + yy + " " + h + ":" + n,true);

  return makeUnixTime((byte) yy, m,d,h,n,s,asLocalTime, standardLocalOnly);

}

time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s, bool asLocalTime, bool standardLocalOnly) {
  //asLocalTime means you are providing a time in your local timezone. It will be converted back to UTC and RETURNED AS UTC
  //standardLocalOnly: for DST boundary strings, do not subtract the active DST offset
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

  if (asLocalTime) {
    int32_t dstAdj = standardLocalOnly ? 0 : (Prefs.DST>0?(Prefs.DST-1)*Prefs.DSTOffset:0);
    return makeTime(unixTime) - dstAdj - Prefs.TimeZoneOffset;
  } else {
    return makeTime(unixTime);
  }

}



time_t iso8601ToUnix(String iso) {
  // ISO 8601: 2026-11-01T06:00:00Z or 2026-11-01T06:00:00-05:00 where the first is UTC and the second is local time. Z is at 19th index.
  // Indices:  0123-56-89T11:13:15
  
  
  int32_t UTCmodifier = 0;
  if (iso.length() < 19) return 0; // Basic safety check

  tmElements_t tm;
  
  // Note: Year in TimeLib is offset from 1970
  tm.Year   = (byte) ((uint16_t) iso.substring(0, 4).toInt()-1970);
  tm.Month  = iso.substring(5, 7).toInt();
  tm.Day    = iso.substring(8, 10).toInt();
  tm.Hour   = iso.substring(11, 13).toInt();
  tm.Minute = iso.substring(14, 16).toInt();
  tm.Second = iso.substring(17, 19).toInt();


  String tz = iso.substring(19);
  bool asLocalTime = true; //default to local time

  if (tz.length()>=1) {    
    if (tz.startsWith("Z")) {
      //this is in UTC
      asLocalTime = false;
      UTCmodifier = 0;
    } else if (tz.startsWith("+") || tz.startsWith("-")) {
      //this is a local time string, determine the offset time and make it a UTC
      UTCmodifier = (tz.substring(1,3).toInt()*3600 + tz.substring(4,6).toInt()*60);
      if (tz.startsWith("-"))         UTCmodifier = -UTCmodifier;      
    }
  }
    
  
  time_t returnTime = makeTime(tm); //this is now the UNIXTIME of the time specified in tm (not the local time). For example, if it were 1/1/2026 at 12:00 - 05:00, this would return  1/1/2026 at 12:00 UTC time! it is actually the unixtime in seconds for 1/1/2026 at midnight UTC (ignoring the UTC offset).
  //to get this to UTC, SUBTRACT the UTC modifier from the return time
  returnTime = returnTime - UTCmodifier; //now this is a valid UTC time.  
  
  return returnTime;

}

time_t unixToLocal(time_t unixTime) {
  //convert a UTC time to local time
  return unixTime + (Prefs.DST>0?(Prefs.DST-1)*Prefs.DSTOffset:0) + Prefs.TimeZoneOffset;
}