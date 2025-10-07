#include "utility.hpp"
#include "BootSecure.hpp"


//flags for sensors:
////  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)

void initScreenFlags(bool completeInit) {
  if (completeInit) {

      I.rebootsSinceLast=0;
      I.wifiFailCount=0;
      I.currentTime=0;
      I.CLOCK_Y = 105;
      I.HEADER_Y = 30;
      
      I.cycleHeaderMinutes = 30; //how many seconds to show header?
      I.cycleCurrentConditionMinutes = 10; //how many minutes to show current condition?
      I.cycleWeatherMinutes = 10; //how many minutes to show weather values?
      I.cycleFutureConditionsMinutes = 10; //how many minutes to show future conditions?
      I.cycleFlagSeconds = 3; //how many seconds to show flag values?
      I.IntervalHourlyWeather = 2; //hours between daily weather display
      I.screenChangeTimer = 30; //how many seconds before screen changes back to main screen

      I.isExpired = false; //are any critical sensors expired?
      I.wasFlagged=false;
      I.isHeat=false; //first bit is heat on, bits 1-6 are zones
      I.isAC=false; //first bit is compressor on, bits 1-6 are zones
      I.isFan=false; //first bit is fan on, bits 1-6 are zones
      I.wasHeat=false; //first bit is heat on, bits 1-6 are zones
      I.wasAC=false; //first bit is compressor on, bits 1-6 are zones
      I.wasFan=false; //first bit is fan on, bits 1-6 are zones

      I.isHot=0;
      I.isCold=0;
      I.isSoilDry=0;
      I.isLeak=0;
      I.localWeatherIndex=255; //index of outside sensor
      I.localBatteryIndex=255;

      I.showTheseFlags=(1<<3) + (1<<2) + (1<<1) + 1; //bit 0 = 1 for flagged only, bit 1 = 1 include expired, bit 2 = 1 include soil alarms, bit 3 =1 include leak, bit 4 =1 include temperature, bit 5 =1 include  RH, bit 6=1 include pressure, 7 = 1 include battery, 8 = 1 include HVAC

      I.currentTemp-127;
      I.Tmax=-127;
      I.Tmin=127;
      I.lastErrorTime=0;
  }
  I.ScreenNum = 0;
  I.oldScreenNum = 0;
  I.lastStoreCoreDataTime = 0;

  I.lastHeaderTime=0; //last time header was drawn
  I.lastWeatherTime=0; //last time weather was drawn
  I.lastCurrentConditionTime=0; //last time current condition was drawn
  I.lastClockDrawTime=0; //last time clock was updated, whether flag or not
  I.lastFutureConditionTime=0; //last time future condition was drawn
  I.lastFlagViewTime=0; //last time clock was updated, whether flag or not
  
  // Timezone is now managed entirely through Prefs
  
  I.localBatteryLevel=0;

  I.ESPNOW_SENDS = 0;
  I.ESPNOW_RECEIVES = 0;

  I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC=0;
  I.ESPNOW_LAST_INCOMINGMSG_FROM_IP=IPAddress(0,0,0,0);
  I.ESPNOW_LAST_INCOMINGMSG_TYPE=0;
  I.ESPNOW_LAST_INCOMINGMSG_FROM_TYPE=MYTYPE;
  memset(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD,0,80);
  I.ESPNOW_LAST_INCOMINGMSG_TIME=0;
  I.ESPNOW_LAST_INCOMINGMSG_STATE=0;

  I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC=0;
  I.ESPNOW_LAST_OUTGOINGMSG_TYPE=0;
  memset(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD,0,80);
  I.ESPNOW_LAST_OUTGOINGMSG_TIME=0;
  I.ESPNOW_LAST_OUTGOINGMSG_STATE=0;

  I.lastResetTime=I.currentTime;
  I.ALIVESINCE=I.currentTime;
  I.wifiFailCount=0;
  I.ScreenNum = 0;
  I.isFlagged = false;

  I.isUpToDate = false;

  if (completeInit) {
      storeScreenInfoSD();
  }
}


bool SerialPrint(String S,bool newline) {
  return SerialPrint(S.c_str(),newline);
}

bool SerialPrint(const char* S,bool newline) {
  bool printed =false;

  
  #ifdef _USESERIAL
    Serial.printf("%s",S);
    if (newline) Serial.println();
    printed=true;
  #endif

  return printed;


}

int inArray(int arr[], int N, int value,bool returncount) {
  //returns index to the integer array of length N holding value, or -1 if not found
  //if returncount == true then returns number of occurrences

byte cnt=0;

for (int i = 0; i < N-1 ; i++)   {
  if (arr[i]==value) {
    if (returncount) cnt++;
    else return i;
  }
}

return -1;

}

int inArrayBytes(byte arr[], int N, byte value,bool returncount) {
  //returns index to the integer array of length N holding value, or -1 if not found
  //if returncount == true then returns number of occurrences

byte cnt=0;

for (int i = 0; i < N-1 ; i++)   {
  if (arr[i]==value) {
    if (returncount) cnt++;
    else return i;
  }
}

return -1;

}


void pushDoubleArray(double arr[], byte N, double value) { //array variable, size of array, value to push
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;

}

bool inIndex(byte lookfor,byte used[],byte arraysize) {
  for (byte i=0;i<arraysize;i++) {
    if (used[i] == lookfor) return true;
  }

  return false;
}

void Byte2Bin(uint8_t value, char* output, bool invert) {
  snprintf(output,9,"00000000");
  for (byte i = 0; i < 8; i++) {
    if (invert) {
      if (value & (1 << i)) output[i] = '1';
      else output[i] = '0';
    } else {
      if (value & (1 << i)) output[7-i] = '1';
      else output[7-i] = '0';
    }
  }

  return;
}


char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}




bool stringToLong(String s, uint32_t* val) {
 
  char* e;
  uint32_t myint = strtol(s.c_str(), &e, 0);

  if (e != s.c_str()) { // check if strtol actually found some digits
    *val = myint;
    return true;
  } else {
    return false;
  }
  
}

int16_t cumsum(int16_t * arr, int16_t ind1, int16_t ind2) {
  int16_t output = 0;
  for (int x=ind1;x<=ind2;x++) {
    output+=arr[x];
  }
  return output;
}




// Legacy compatibility functions that delegate to the Devices_Sensors class

void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow){
  Sensors.find_limit_sensortypes(snsname, snsType, snsIndexHigh, snsIndexLow);
}

uint8_t find_sensor_count(String snsname,uint8_t snsType) {
  return Sensors.find_sensor_count(snsname, snsType);
}

uint8_t findSensorByName(String snsname,uint8_t snsType,uint8_t snsID) {
  return Sensors.findSensorByName(snsname, snsType, snsID);
}

uint8_t findSensorByName(String snsname,uint8_t snsType) {
  return Sensors.findSensorByName(snsname, snsType, 0);
}

//sensor fcns
int16_t findOldestDev() {
  //return 0 entry or oldest expired noncritical (but never critical sensors)
  int oldestInd = 0;
  int  i=0;

  for (i=0;i<Sensors.getNumSensors();i++) {
    //find a zero slot
    if (!Sensors.isSensorInit(i)) return i;

    //find an expired noncritical slot
    SnsType* sensor = Sensors.getSensorBySnsIndex(i);
    if (sensor && sensor->expired == 1 && !bitRead(sensor->Flags,7)) {
      return i;
    }
  }

  return oldestInd;
}

void initSensor(int k) {
  Sensors.initSensor(k);
}


bool isSensorInit(int i) {
  return Sensors.isSensorInit(i);
}

byte checkExpiration(int i, time_t t, bool onlyCritical) {
  //i is -1 for all sensors, or the specific sensor index. Checks expiration of sensors, not devices.
  return Sensors.checkExpiration(i, t, onlyCritical);
}

uint8_t countDev() {
  return Sensors.getNumDevices();
}

int16_t findDev(byte* macID, byte ardID, byte snsType, byte snsID,  bool oldest) {
  // Legacy function - convert to new format
  uint64_t mac=0;
  memcpy(&mac, macID, 6);
  return Sensors.findDevice(mac);
}

int16_t findSnsOfType(byte snstype, bool newest) {
  return Sensors.findSnsOfType(snstype, newest);
}

uint8_t countFlagged(int snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan) {
  return Sensors.countFlagged(snsType, flagsthatmatter, flagsettings, MoreRecentThan);
}

void checkHeat() {
  // Check if any HVAC sensors are active
  I.isHeat = 0;
  I.isAC = 0;
  I.isFan = 0;
  
  // Iterate through all devices and sensors with bounds checking
  for (int16_t deviceIndex = 0; deviceIndex < NUMDEVICES && deviceIndex < Sensors.getNumDevices(); deviceIndex++) {
    DevType* device = Sensors.getDeviceByDevIndex(deviceIndex);
    if (!device || !device->IsSet) continue;
    
    for (int16_t sensorIndex = 0; sensorIndex < NUMSENSORS && sensorIndex < Sensors.getNumSensors(); sensorIndex++) {
      SnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);
      if (!sensor || !sensor->IsSet) continue;
      
      // Check HVAC sensors (types 50-59)
      if (sensor->snsType >= 50 && sensor->snsType < 60) {
        if (sensor->snsValue > 0) {
          switch (sensor->snsType) {
            case 50: // Heat
              I.isHeat = 1;
              break;
            case 51: // AC
              I.isAC = 1;
              break;
            case 52: // Fan
              I.isFan = 1;
              break;
          }
        }
      }
    }
  }
}

String breakString(String *inputstr,String token,bool reduceOriginal) 
//take a pointer to input string and break it to the token.
//if reduceOriginal=true, then will decimate string - otherwise will leave original string as is

{
  String output = "";
  int pos = inputstr->indexOf(token);
  if (pos>-1) {
    output = inputstr->substring(0,pos);
    if (reduceOriginal) {
      *inputstr = inputstr->substring(pos+token.length());
    }
  } else {
    output = *inputstr;
    if (reduceOriginal) {
      *inputstr = "";
    }
  }
  return output;
}


uint16_t countSubstr(String orig, String token) {
  uint16_t count = 0;
  int pos = 0;
  while ((pos = orig.indexOf(token, pos)) != -1) {
    count++;
    pos += token.length();
  }
  return count;
}

String enumErrorToName(ERRORCODES E) {
  switch (E) {
    case ERROR_HTTPFAIL_BOX: return "HTTP failed for NOAA grid location";
    case ERROR_HTTPFAIL_DAILY: return "HTTP failed for NOAA daily weather";
    case ERROR_HTTPFAIL_GRID: return "HTTP failed for NOAA weather grid";
    case ERROR_HTTPFAIL_HOURLY: return "HTTP failed for NOAA hourly weather";
    case ERROR_HTTPFAIL_SUNRISE: return "HTTP failed for sunrise.io";
    case ERROR_JSON_BOX: return "Json parsing failed for NOAA grid location";
    case ERROR_JSON_DAILY: return "Json parsing failed for NOAA daily weather";
    case ERROR_JSON_GRID: return "Json parsing failed for NOAA weather grid";
    case ERROR_JSON_HOURLY: return "Json parsing failed for NOAA hourly weather";
    case ERROR_JSON_SUNRISE: return "Json parsing failed for sunrise.io";
    case ERROR_SD_LOGOPEN: return "Could not open log on SD";
    

    default: return "Error code is indeterminate";
  }

}

void storeError(const char* E, ERRORCODES CODE, bool writeToSD) {
    
    if (E && strlen(E) < 75) {
        strncpy(I.lastError, E, 74);
        I.lastError[74] = '\0';
    } else if (E) {
        strncpy(I.lastError, E, 74);
        I.lastError[74] = '\0';
    } else {
        I.lastError[0] = '\0';
    }

    I.lastErrorCode = CODE;
    I.lastErrorTime = I.currentTime;


    if (writeToSD) writeErrorToSD();
}


void storeCoreData() {
  //force core data to be stored to SD
  I.isUpToDate = true;
  storeScreenInfoSD();

  if (Prefs.isUpToDate==false) {
  Prefs.isUpToDate = true;
  BootSecure bootSecure;
  bootSecure.setPrefs();
  }

}


void controlledReboot(const char* E, RESETCAUSE R,bool doreboot) {
  storeError(E);
  I.resetInfo = R;
  I.lastResetTime = I.currentTime;
  I.rebootsSinceLast++;


  storeCoreData();
  
  if (doreboot) {
    SerialPrint("Controlled reboot in 1 second", true);
    delay(1000);
    ESP.restart();
  }
}

String lastReset2String(bool addtime) {
  String output = "";
  switch (I.resetInfo) {
    case RESET_DEFAULT: output = "Default"; break;
    case RESET_SD: output = "SD Card"; break;
    case RESET_WEATHER: output = "Weather"; break;
    case RESET_USER: output = "User"; break;
    case RESET_OTA: output = "OTA"; break;
    case RESET_WIFI: output = "WiFi"; break;
    case RESET_TIME: output = "Time"; break;
    case RESET_UNKNOWN: output = "Unexpected Error"; break;
    default: output = "Unknown"; break;
  }
  
  if (addtime) {
    output += " at " + String(I.lastResetTime ? dateify(I.lastResetTime) : "???");
  }
  
  return output;
}

// Helper function to get detailed reboot information for debugging
String getRebootDebugInfo() {
  String info = "Reboot Debug Info:\n";
  info += "Reset Cause: " + lastReset2String(false) + "\n";
  info += "Last Reset Time: " + String(I.lastResetTime ? dateify(I.lastResetTime) : "Never") + "\n";
  info += "ALIVESINCE: " + String(I.ALIVESINCE ? dateify(I.ALIVESINCE) : "Never") + "\n";
  info += "Current Time: " + String(I.currentTime ? dateify(I.currentTime) : "Not Set") + "\n";
  if (I.lastResetTime != 0 && I.ALIVESINCE != 0 && I.currentTime != 0) {
    time_t timeDiff = I.currentTime - I.ALIVESINCE;
    info += "Time Difference: " + String(timeDiff) + " seconds\n";
  }
  return info;
}

// --- IP address conversion utilities ---

uint64_t IPToMACID(IPAddress ip) {
  //convert IP address to MAC ID
  //replace with 0x00000000FF000000 prefix if desired
  uint32_t ip32 = IPToUint32(ip);
  return (uint64_t)ip32;

}

uint64_t IPToMACID(byte* ip) {
  //wrapper for IPToMACID when ip is a byte array

  return IPToMACID(IPAddress(ip));
}

// Convert uint64_t MAC to 6-byte array
void uint64ToMAC(uint64_t mac64, byte* macArray) {
    memcpy(macArray, &mac64, 6); //note that arduino is little endian, so this is correct
}

// Convert 6-byte array to uint64_t MAC
uint64_t MACToUint64(const uint8_t* macArray) {
    uint64_t mac64 = 0;
    memcpy(&mac64, macArray, 6);
    return mac64;
}

String MACToString(const uint64_t mac64) {
  byte macArray[6];
  uint64ToMAC(mac64, macArray);
  return ArrayToString(macArray, 6,':',true);
}

String MACToString(const uint8_t* mac) {
  
  return ArrayToString(mac, 6,':',true);
}


bool compareMAC(byte *MAC1,byte *MAC2) {
  for (byte i=0;i<6;i++) {
    if (MAC1[i]!=MAC2[i]) return false;
  }
  return true;
}

bool isMACSet(byte *m, bool doReset) {
  for (byte i=0;i<6;i++) {
    if (m[i]!=0) return true;
  }
  if (doReset) {
    memset(m,0,6);
  }
  return false;
}


String ArrayToString(const uint8_t* Arr, byte len, char separator, bool asHex) {

  String output = "";
  char holder[10] = "";
  for (int i=len-1; i>=0; i--) {
    if (i!=len-1) output += (String) separator;
    if (asHex)    snprintf(holder,9,"%x",Arr[i]);
    else    snprintf(holder,9,"%d",Arr[i]);
    output += (String) holder;
  }
  return output;

}


// Get a specific byte (zero indexed) from PROCID (MAC address).
uint8_t getPROCIDByte(uint64_t procid, uint8_t byteIndex) {
    if (byteIndex >= 6) return 0;
    return (procid >> (8 * (byteIndex))) & 0xFF;
}

bool cycleByteIndex(byte* start, byte arraysize, byte origin) {
  //cycles through a byte vector of arraysize, from start and looping around back to 0 until it wraps around back to origin
  //usage: cycle through a vector of size 10, starting from 6... repeatedly call y = cycleIndex(x,10,6) where x is the last y returned
  //returns true if cycling, false if complete, and start is incremented appropriately.

  if (arraysize==0) return false;
  if (origin>=arraysize) return false;
  if (*start>=arraysize) return false; 


  if (*start == arraysize-1) *start=0;
  else *start = *start + 1;

  if (*start==origin)  {
    //cycle index has already been updated, no changes
    return false;
  } else {
    return true;
  }
}

bool cycleIndex(uint16_t* start, uint16_t arraysize, uint16_t origin) {
  //cycles through a vector of arraysize, from start and looping around back to 0 until it wraps around back to origin
  //usage: cycle through a vector of size 10, starting from 6... repeatedly call y = cycleIndex(x,10,6) where x is the last y returned
  //returns true if cycling, false if complete

  if (arraysize==0) return false;
  if (origin>=arraysize) return false;
  if (*start>=arraysize) return false; 



  if (*start == arraysize-1) *start=0;
  else *start = *start + 1;

  if (*start==origin)  {
    //cycle index has already been updated, no changes
    return false;
  } else {
    return true;
  }
  
}

uint32_t IPToUint32(IPAddress ip) {
  return (ip[0]<<24) + (ip[1]<<16) + (ip[2]<<8) + ip[3];
} 


