#include "timesetup.hpp"

#define TIMEUPDATEINT 10800000

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.nist.gov", 0, TIMEUPDATEINT);

char DATESTRING[25] = "";

static String breakStringLocal(String *inputstr, String token, bool reduceOriginal=true) {
  String output = "";
  int pos = inputstr->indexOf(token);
  if (pos>-1) {
    output = inputstr->substring(0,pos);
    if (reduceOriginal) *inputstr = inputstr->substring(pos+token.length());
  } else {
    output = *inputstr;
    if (reduceOriginal) *inputstr = "";
  }
  return output;
}

time_t makeUnixTime(byte yy, byte m, byte d, byte h, byte n, byte s) {
  int16_t y = (yy>=70) ? (1900+yy) : (2000+yy);
  tmElements_t unixTime;
  unixTime.Year = y-1970;
  unixTime.Month = m;
  unixTime.Day = d;
  unixTime.Hour = h;
  unixTime.Minute = n;
  unixTime.Second = s;
  return makeTime(unixTime);
}

static String fcnDOWLocal(time_t t, bool caps) {
  const char* capsNames[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  const char* normNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  uint8_t idx = weekday(t);
  if (idx < 1 || idx > 7) return "???";
  return caps ? String(capsNames[idx-1]) : String(normNames[idx-1]);
}

char* dateify(time_t t, String dateformat) {
  if (t==0) t = I.currentTime;
  char holder[5] = "";
  snprintf(holder,4,"%02d",month(t)); dateformat.replace("mm",holder);
  snprintf(holder,4,"%02d",day(t)); dateformat.replace("dd",holder);
  snprintf(holder,5,"%02d",year(t)); dateformat.replace("yyyy",holder);
  snprintf(holder,4,"%02d",year(t)-2000); dateformat.replace("yy",holder);
  snprintf(holder,4,"%02d",hour(t)); dateformat.replace("hh",holder);
  snprintf(holder,4,"%d",hour(t)); dateformat.replace("h1",holder);
  snprintf(holder,4,"%d",hourFormat12(t)); dateformat.replace("H",holder);
  snprintf(holder,4,"%02d",minute(t)); dateformat.replace("nn",holder);
  snprintf(holder,4,"%02d",second(t)); dateformat.replace("ss",holder);
  snprintf(holder,4,"%s",fcnDOWLocal(t,true).c_str()); dateformat.replace("DOW",holder);
  snprintf(holder,4,"%s",fcnDOWLocal(t,false).c_str()); dateformat.replace("dow",holder);
  if (dateformat.indexOf("XM")>-1) dateformat.replace("XM",(isPM(t)==true)?"PM":"AM");
  snprintf(DATESTRING,25,"%s",dateformat.c_str());
  return DATESTRING;
}

String fcnDOW(time_t t, bool caps) {
  return fcnDOWLocal(t,caps);
}

bool updateTime() {
  bool isgood = false;
  if (timeClient.update()) { setTime(timeClient.getEpochTime()); isgood = true; }
  I.currentTime=now();
  return isgood;
}

void checkDST(void) {
  int m=month(I.currentTime);
  int y=year(I.currentTime);
  if (m > 3 && m < 11) {
    I.DSTOFFSET = 3600;
  } else if (m == 3) {
    time_t march1 = makeUnixTime(y-2000, 3, 1, 2, 0, 0);
    int firstSunday = 1 + (8 - weekday(march1)) % 7; if (firstSunday == 8) firstSunday = 1;
    time_t secondSunday = makeUnixTime(y-2000, 3, firstSunday + 7, 2, 0, 0);
    I.DSTOFFSET = (I.currentTime >= secondSunday) ? 3600 : 0;
  } else if (m == 11) {
    time_t nov1 = makeUnixTime(y-2000, 11, 1, 2, 0, 0);
    int firstSunday = 1 + (8 - weekday(nov1)) % 7; if (firstSunday == 8) firstSunday = 1;
    time_t firstSundayTime = makeUnixTime(y-2000, 11, firstSunday, 2, 0, 0);
    I.DSTOFFSET = (I.currentTime < firstSundayTime) ? 3600 : 0;
  }
  timeClient.setTimeOffset(I.GLOBAL_TIMEZONE_OFFSET+I.DSTOFFSET);
  timeClient.forceUpdate();
  setTime(timeClient.getEpochTime());
  I.currentTime = now();
}

bool setupTime(void) {
  timeClient.begin();
  timeClient.setTimeOffset(I.GLOBAL_TIMEZONE_OFFSET+I.DSTOFFSET);
  byte i=0;
  while (timeClient.forceUpdate()==false && i<25) { i++; }
  if (i>=25) return false;
  setTime(timeClient.getEpochTime());
  I.currentTime = now();
  checkDST();
  return true;
}