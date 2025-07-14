//#define _DEBUG
#include "utility.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "timesetup.hpp"
#include "SDCard.hpp"

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
    for (byte i=0;i<6;i++) {
      m[i]=0;
    }
  }
  return false;
}

bool isSensorInit(int i) {
  return Sensors.isSensorInit(i);
}

byte checkExpiration(int i, time_t t, bool onlyCritical) {
  return Sensors.checkExpiration(i, t, onlyCritical);
}

uint8_t countDev() {
  return Sensors.countDev();
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

String IPbytes2String(byte* IP,byte len) {
  String output = "";
  for (byte i=0;i<len;i++) {
    if (i>0) output += ".";
    output += (String) IP[i];
  }
  return output;
}

bool IPString2ByteArray(String IPstr,byte* IP) {
  //parse IP string like "192.168.1.1" into byte array
  String temp = IPstr;
  String token;
  byte i=0;
  
  while (temp.length()>0 && i<4) {
    token = breakString(&temp,".",true);
    if (token.length()>0) {
      IP[i] = token.toInt();
      i++;
    }
  }
  
  return (i==4);
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

void storeError(const char* E) {
    if (E && strlen(E) < 75) {
        strncpy(I.lastError, E, 74);
        I.lastError[74] = '\0';
    } else if (E) {
        strncpy(I.lastError, E, 74);
        I.lastError[74] = '\0';
    } else {
        I.lastError[0] = '\0';
    }
}

void controlledReboot(const char* E, RESETCAUSE R,bool doreboot) {
  storeError(E);
  I.resetInfo = R;
  I.lastResetTime = I.currentTime;
  I.rebootsSinceLast++;
  
  if (doreboot) {
    #ifdef HAS_TFT
    tft.clear();
    tft.setCursor(0,0);
    tft.setTextColor(TFT_RED);
    tft.println("Controlled reboot in 1 second");
    #endif 
    
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
    case RESET_UNKNOWN: output = "Unknown"; break;
    default: output = "Unknown"; break;
  }
  
  if (addtime) {
    output += " at " + String(I.lastResetTime);
  }
  
  return output;
}

// --- IP address conversion utilities ---
String IPToString(uint32_t ip) {
    return String((ip >> 24) & 0xFF) + "." +
           String((ip >> 16) & 0xFF) + "." +
           String((ip >> 8) & 0xFF) + "." +
           String(ip & 0xFF);
}

bool StringToIP(const String& str, uint32_t& ip) {
    int parts[4] = {0};
    int lastPos = 0, pos = 0;
    for (int i = 0; i < 4; ++i) {
        pos = str.indexOf('.', lastPos);
        if (pos == -1 && i < 3) return false;
        String part = (i < 3) ? str.substring(lastPos, pos) : str.substring(lastPos);
        parts[i] = part.toInt();
        if (parts[i] < 0 || parts[i] > 255) return false;
        lastPos = pos + 1;
    }
    ip = ((uint32_t)parts[0] << 24) | ((uint32_t)parts[1] << 16) | ((uint32_t)parts[2] << 8) | (uint32_t)parts[3];
    return true;
}

// Convert uint64_t MAC to 6-byte array
void uint64ToMAC(uint64_t mac64, byte* macArray) {
    memcpy(macArray, &mac64, 6); //note that arduino is little endian, so this is correct
}

// Convert 6-byte array to uint64_t MAC
uint64_t MACToUint64(byte* macArray) {
    uint64_t mac64 = 0;
    memcpy(&mac64, macArray, 6);
    return mac64;
}

String MACToString(uint64_t mac64) {
  byte macArray[6];
  uint64ToMAC(mac64, macArray);
  return IPbytes2String(macArray, 6);
}

// Get a specific byte (zero indexed) from PROCID (MAC address).
uint8_t getPROCIDByte(uint64_t procid, uint8_t byteIndex) {
    if (byteIndex >= 6) return 0;
    return (procid >> (8 * (byteIndex))) & 0xFF;
}
