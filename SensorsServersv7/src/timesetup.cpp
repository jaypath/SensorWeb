//#define _DEBUG
#include "globals.hpp"
#include "timesetup.hpp"
#include "AddESPNOW.hpp"
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <server.hpp>
#include <esp_task_wdt.h>
#ifdef ESP32
#include <sys/time.h>
#endif

#ifdef _USETFT
extern LGFX tft;
#endif

#define TIMEUPDATEINT 10800000

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov",0,TIMEUPDATEINT); //3rd param is offset, 4th param is update frequency

char DATESTRING[25]="";

// Standard (non-DST) local time as a pseudo-unix value. This matches how the DST
// boundaries are stored in Prefs (UTC + standard timezone offset, with NO DST applied).
// ALWAYS compare against this when testing Prefs.DSTStartUnixTime / Prefs.DSTEndUnixTime.
// Do NOT use now()/I.UTCTime (those are UTC) or I.currentTime (that includes the DST offset).
static time_t standardLocalNow() {
  return now() + Prefs.TimeZoneOffset;
}

// FAT/VFS file stamps use POSIX gettimeofday(), not TimeLib — keep them in sync after every UTC set.
static void syncPosixClockUtc(time_t utc) {
#ifdef ESP32
  if (utc < TIMEZERO) return;
  timeval tv = {};
  tv.tv_sec = utc;
  settimeofday(&tv, nullptr);
#endif
}

static void setTimeAndSyncPosix(time_t utc) {
  setTime(utc);
  syncPosixClockUtc(utc);
}

// Apply Prefs timezone + DST state to I.currentTime (requires valid TimeLib UTC via now()).
static void applyLocalTimeFromPrefs() {
  I.UTCTime = now();
  DSTsetup();
  I.currentTime = I.UTCTime + Prefs.TimeZoneOffset + (Prefs.DST > 0 ? (Prefs.DST - 1) * Prefs.DSTOffset : 0);
  I.currentSecond = second();
}

// NTP sync + DSTsetup using cached Prefs rules (no TimeAPI/getTimezoneInfo network call).
// Used at boot from initSystem when WiFi is up; setupTime() may call this again if UTC is still invalid.
bool syncNtpAndApplyDST() {
  if (!wifiReadyForNetwork()) return false;

  timeClient.begin();
  timeClient.setTimeOffset(0);

  byte i = 0;
  while (timeClient.forceUpdate() == false && i < 250) {
    i++;
    delay(50);
  }
  if (i >= 250) return false;

  setTimeAndSyncPosix(timeClient.getEpochTime());
  applyLocalTimeFromPrefs();

  SerialPrint("DST at boot: " + String(Prefs.DST == 0 ? "Not used" : (Prefs.DST == 1 ? "Inactive" : "Active")) +
              ", local=" + String(dateify(I.currentTime, "yyyy-mm-dd hh:nn:ss")), true, 5);
  return (I.UTCTime >= TIMEZERO);
}

static void applyTimeFromServerPing(uint32_t serverLocalTime) {
  time_t utc = serverLocalTime;
  if (timezonePrefsValid()) {
    utc -= Prefs.TimeZoneOffset;
    if (Prefs.DST > 0) utc -= (Prefs.DST - 1) * Prefs.DSTOffset;
  }
  setTimeAndSyncPosix(utc);
  I.UTCTime = now();
  I.currentTime = serverLocalTime;
  I.currentSecond = second();
  if (Prefs.DST > 0) DSTsetup();
}

static constexpr uint32_t ESPNOW_TIME_SYNC_INTERVAL_MS = 600000; // 10 minutes between ESP-NOW sync sessions
static uint32_t s_lastEspNowTimeSyncAttemptMs = 0;

static bool espNowTimeSyncSessionAllowed() {
  if (s_lastEspNowTimeSyncAttemptMs == 0) return true;
  const uint32_t elapsed = millis() - s_lastEspNowTimeSyncAttemptMs;
  return elapsed >= ESPNOW_TIME_SYNC_INTERVAL_MS;
}

static void markEspNowTimeSyncSessionStarted() {
  s_lastEspNowTimeSyncAttemptMs = millis();
}

static bool setupTimeFromEspNowServerPing() {
  constexpr uint8_t kMaxAttempts = 3;
  constexpr uint32_t kWaitPerAttemptMs = 500; // 3 attempts x 0.5s = 1.5s max blocking

  if (!ensureESPNOW()) {
    SerialPrint("setupTime: ESP-NOW not available for time sync", true, 5);
    return false;
  }

  beginServerPingTimeSync();

  for (uint8_t attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    if (!broadcastServerPing(1)) {
      delay(100);
      esp_task_wdt_reset();
      continue;
    }

    const uint32_t startMs = millis();
    while (millis() - startMs < kWaitPerAttemptMs) {
      delay(50);
      esp_task_wdt_reset();

      uint32_t serverLocalTime = 0;
      if (takeServerPingTimeSync(serverLocalTime)) {
        endServerPingTimeSync();
        applyTimeFromServerPing(serverLocalTime);
        SerialPrint("setupTime: time set from ESP-NOW server ping: " + String(serverLocalTime), true);
        return (I.currentTime >= TIMEZERO);
      }
    }
  }

  endServerPingTimeSync();
  SerialPrint("setupTime: ESP-NOW server ping time sync failed after " + String(kMaxAttempts) + " attempts", true, 5);
  return false;
}

bool timezonePrefsValid() {
  if (Prefs.TimeZoneOffset > 50400) return false; // includes sentinel 90000 = unset
  if (Prefs.DST == 0) return true;
  return (Prefs.DSTStartUnixTime != 0 && Prefs.DSTEndUnixTime != 0 && Prefs.DSTOffset != 0);
}

bool refreshTimezoneFromNetwork(uint16_t timeoutMs) {
  if (!wifiReadyForNetwork()) return false;
  if (!getTimezoneInfo(timeoutMs)) return false;
  applyLocalTimeFromPrefs();
  I.lastTimezoneRefresh = I.currentTime;
  return true;
}

bool setupTime(void) {
  if (!wifiReadyForNetwork()) {
    if (!espNowTimeSyncSessionAllowed()) {
      return false;
    }
    markEspNowTimeSyncSessionStarted();

    if (!setupTimeFromEspNowServerPing()) {
      return false;
    }
    if (timezonePrefsValid()) {
      DSTsetup();
    }
    if (I.lastTimezoneRefresh == 0 && I.currentTime >= TIMEZERO) {
      I.lastTimezoneRefresh = I.currentTime;
    }
    I.isUpToDate = false;
    return (I.currentTime >= TIMEZERO);
  }

  bool ntpOk = true;

  #ifdef _USETFT
  if (I.UTCTime >= TIMEZERO) {
    tftPrint("NTP: already synced.", true, TFT_WHITE, 2, 1, false, -1, -1);
  } else {
    tftPrint("NTP sync... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  }
  #endif

  if (I.UTCTime < TIMEZERO) {
    ntpOk = syncNtpAndApplyDST();
    #ifdef _USETFT
    tftPrint(ntpOk ? " OK." : " FAIL.", true, ntpOk ? TFT_GREEN : TFT_RED);
    #endif
  }

  #ifdef _USETFT
  if (timezonePrefsValid()) {
    tftPrint("Timezone: using cached prefs.", true, TFT_WHITE, 2, 1, false, -1, -1);
  } else {
    tftPrint("Timezone lookup... ", false, TFT_WHITE, 2, 1, false, -1, -1);
  }
  #endif

  if (timezonePrefsValid()) {
    DSTsetup();
    applyLocalTimeFromPrefs();
    if (I.lastTimezoneRefresh == 0) I.lastTimezoneRefresh = I.currentTime;
  } else {
    bool tzOk = getTimezoneInfo(TIMEZONE_BOOT_HTTP_TIMEOUT_MS);
    applyLocalTimeFromPrefs();
    if (tzOk) I.lastTimezoneRefresh = I.currentTime;
    #ifdef _USETFT
    tftPrint(tzOk ? " OK." : " FAIL.", true, tzOk ? TFT_GREEN : TFT_RED);
    #endif
  }

  I.isUpToDate = false;
  return ntpOk && (I.UTCTime >= TIMEZERO);
}



//Time fcn
bool updateTime() {
  I.UTCTime = now();

  //run this every second
  if (I.currentSecond == second()) return false;

  bool isgood = false;

  if ( timeClient.update()) {  //returns false if not time to update
    setTimeAndSyncPosix(timeClient.getEpochTime());
    I.UTCTime = now();
    isgood = true;
  }

  //re-evaluate DST every tick when DST is in use. DSTsetup() is cheap and only triggers a
  //network refresh on an actual transition, so this can't "miss" a boundary second.
  if (Prefs.DST>0) {
      DSTsetup(); //this sets Prefs.DST
  }

  I.currentTime = I.UTCTime + Prefs.TimeZoneOffset + (Prefs.DST>0?(Prefs.DST-1)*Prefs.DSTOffset:0);
  I.currentSecond = second();
  I.isUpToDate = false;
  return isgood;
}

bool getTimezoneInfo(uint16_t timeoutMs) {
  //this needs to run at startup and at DST changes
  char tzURL[150];
// Query TimeAPI.io (Secure/HTTPS using Bundle)
if (Prefs.LATITUDE==0 || Prefs.LONGITUDE==0) { //use IP address to get timezone
  String myIP = getPublicIP(timeoutMs);
  if (myIP == "") return false;
  snprintf(tzURL, 99, "https://timeapi.io/api/timezone/ip?ipAddress=%s", myIP.c_str());
} else {
  //use latitude and longitude to get timezone
  snprintf(tzURL, 99, "https://timeapi.io/api/timezone/coordinate?latitude=%f&longitude=%f", Prefs.LATITUDE, Prefs.LONGITUDE);
}


JsonDocument doc;

HTTPMessage M;
M.setUrl(tzURL);
M.setMethod("GET");
M.setContentType("application/json");
M.setCacert("bundle");
M.timeout = timeoutMs;
M.usePSRAM = false;
M.responseDoc = &doc;

if (SendHTTPMessage(M)) {
  if (doc["standardUtcOffset"]["seconds"].is<int32_t>()) {
    time_t tzOffset = doc["standardUtcOffset"]["seconds"].as<int32_t>();
    if (tzOffset != Prefs.TimeZoneOffset) {
      SerialPrint("TimeZoneOffset changed from " + String(Prefs.TimeZoneOffset) + " to " + String(tzOffset), true);
      Prefs.TimeZoneOffset = tzOffset;
      
    }
    // Check if DST exists
    Prefs.DST = doc["dstInterval"].isNull() ? 0 : 1; //DST is set to 0 if no dst here, or 1 if DST is used in this area. If 1, will be adjusted to 1 or 2 by DSTsetup()
    

    if (Prefs.DST>0) {
      Prefs.DSTStartUnixTime = unixToLocal(iso8601ToUnix(doc["dstInterval"]["dstStart"].as<String>())); //this is in local time
      Prefs.DSTEndUnixTime = unixToLocal(iso8601ToUnix(doc["dstInterval"]["dstEnd"].as<String>()));
      Prefs.DSTOffset = doc["dstInterval"]["dstOffsetToStandardTime"]["seconds"].as<int32_t>(); //this is the offset from local time. offset to UTC is also available, but incorporates TZ offset as well
      time_t localNow = standardLocalNow();
      if (localNow > Prefs.DSTStartUnixTime && localNow < Prefs.DSTEndUnixTime) {
        Prefs.DST = 2;
      } else {
        Prefs.DST = 1;
      }
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


void DSTsetup(void) {
//this function needs only be called  once a day to check if DST is active - or at known DST change times

  if (Prefs.DST==0) { // we don't use DST here
    Prefs.DSTOffset=0;        
    Prefs.isUpToDate = false;
    return;
  } 

  
  int8_t DST_old = Prefs.DST;

  // Boundaries are stored in standard local time, so compare against standard local "now".
  time_t localNow = standardLocalNow();

  if (Prefs.DSTStartUnixTime < Prefs.DSTEndUnixTime) {
    if (localNow >= Prefs.DSTStartUnixTime && localNow < Prefs.DSTEndUnixTime) {
      Prefs.DST = 2; //DST is active
    } else {
      Prefs.DST = 1; //DST is inactive
    }
  } else {
    if (localNow < Prefs.DSTEndUnixTime) { //this check is only in case DSTStartUnixTime was updated to the NEXT start, while end time has not been updated yet
      Prefs.DST = 2; //DST is active
    } else {
      Prefs.DST = 1; //DST is inactive
    }
  }

  if (DST_old != Prefs.DST) {
    SerialPrint("DST changed from " + String(DST_old) + " to " + String(Prefs.DST), true);
    if (getTimezoneInfo()) {
      I.lastTimezoneRefresh = I.currentTime;
    }
    Prefs.isUpToDate = false;
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



time_t convertStrTime(String str, bool asLocalTime) //take a text time, like "12:34:13 PM" and return unix time. If month, day, year are not provided then assume current m/d/y
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



    SerialPrint((String) "unixtime = " + makeUnixTime((byte) yy, m,d,h,n,s,asLocalTime) + " humantime = " + m + "/" + d + "/" + yy + " " + h + ":" + n,true);

    if (str.length()==0) return makeUnixTime((byte) yy, m,d,0,0,0,asLocalTime);
  }

  byte hoffset = 0;
  if (str.indexOf(" ")>-1) { //there is a " AM/PM" present
    if (str.indexOf("PM")>-1)  hoffset=12;
    str = breakString(&str," ",true); //remove the am/pm
  }
  h = breakString(&str,":",true).toInt() + hoffset;
  n = breakString(&str,":",true).toInt();

  if(str.length()==0) return makeUnixTime((byte) yy, m,d,h,n,s,asLocalTime); //no sec data

  s = str.toInt();

  SerialPrint((String) "unixtime = " + makeUnixTime((byte) yy, m,d,h,n,s) + " humantime = " + m + "/" + d + "/" + yy + " " + h + ":" + n,true);

  return makeUnixTime((byte) yy, m,d,h,n,s,asLocalTime);

}

time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s, bool asLocalTime) {
  //asLocalTime means you are providing a time in your local timezone. It will be converted back to UTC and RETURNED AS UTC
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
    return makeTime(unixTime) - (Prefs.DST>0?(Prefs.DST-1)*Prefs.DSTOffset:0) - Prefs.TimeZoneOffset;
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

  if (tz.length()>=1) {    
    if (tz.startsWith("Z")) {
      //this is in UTC
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