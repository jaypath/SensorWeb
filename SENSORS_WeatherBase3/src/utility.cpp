//#define _DEBUG
#include <utility.hpp>
//#include <FS.h>

int inArray(int arr[], int N, int value) {
  //returns index to the integer array of length N holding value, or -1 if not found

for (int i = 0; i < N-1 ; i++)   if (arr[i]==value) return i;
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




//sensor class fcns
void find_limit_sensortypes(String snsname, uint8_t snsType, uint8_t* snsIndexHigh, uint8_t* snsIndexLow){
  //returns index to the highest flagged sensorval and lowest flagged sensorval with name like snsname and type like snsType. index is 255 if no lowval is flagged

  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = this is a monitored sensor, so alarm if I don't get a read on time!

  byte cnt = find_sensor_count(snsname,snsType);
  
  byte j = 255;

  double snsVal;
  //do highest
  snsVal=-99999;
  *snsIndexHigh = 255;
  for (byte i=1;i<=cnt;i++) {
    j = find_sensor_name(snsname,snsType,i);
    if (bitRead(Sensors[j].Flags,0) && bitRead(Sensors[j].Flags,5)) {
      if (Sensors[j].snsValue>snsVal) {
        *snsIndexHigh = j;
        snsVal = Sensors[j].snsValue;
      }
    }
  }

  snsVal=99999;
  *snsIndexLow = 255;
  for (byte i=1;i<=cnt;i++) {
    j = find_sensor_name(snsname,snsType,i);
    if (bitRead(Sensors[j].Flags,0) && bitRead(Sensors[j].Flags,5)==0) {
      if (Sensors[j].snsValue<snsVal) {
        *snsIndexLow = j;
        snsVal = Sensors[j].snsValue;
      }
    }
  }

}


uint8_t find_sensor_count(String snsname,uint8_t snsType) {
//find the number of sensors associated with sensor havign snsname 
String temp;
byte cnt =0;
  for (byte j=0; j<SENSORNUM;j++) {
    temp = Sensors[j].snsName; 

    if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType) cnt++;

  }

  return cnt;
}

uint8_t find_sensor_name(String snsname,uint8_t snsType,uint8_t snsID) {
  //return the first sensor that has fieldname within its name
  //set snsID to 255 to ignore this field (this is an optional field, if not specified then find first snstype for fieldname)
  //returns 255 if no such sensor found
  String temp;
  for (byte j=0; j<SENSORNUM;j++) {
    temp = Sensors[j].snsName; 

    if (snsID==255) {
      if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType) return j;
    } else {
      if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType && Sensors[j].snsID == snsID) return j;
    }
  }
  return 255;
}

//sensor fcns
int16_t findOldestDev() {
  //return 0 entry or oldest expired noncritical (but never critical sensors)
  int oldestInd = 0;
  int  i=0;

  for (i=0;i<SENSORNUM;i++) {
    //find a zero slot
    if (isSensorInit(i) == false) return i;

    //find an expired noncritical slot
    if (Sensors[i].expired == 1  && bitRead(Sensors[oldestInd].Flags,7)==0) {
      return i;
    }
  }

  
  if (oldestInd == 0) { //still no oldest ind! find oldest noncritical sensor
    for (i=0;i<SENSORNUM;i++) {
      if (bitRead(Sensors[i].Flags,7)==1) continue; //ignore critical sensors
      if (oldestInd==0 || Sensors[i].timeLogged < Sensors[oldestInd].timeLogged) oldestInd = i;
    }
    
  }

  return oldestInd;
}

void initSensor(int k) {
  //special cases... k>255 then expire any sensor that is older than k MINUTES
  //k<=-256 then init ALL sensors



  if (k<-255 || k>255) {
    if (k<-255)     for (byte i=0;i<SENSORNUM;i++) initSensor(i); //initialize everything.
    else {
      time_t t=now();

      for (byte i=0;i<SENSORNUM;i++)  {
        if (Sensors[i].snsID>0 && Sensors[i].timeLogged>0 && (uint32_t) (t-Sensors[i].timeLogged)>k*60)  initSensor(i); //convert k to seconds and  remove N hour old values 
      }
    }
    return;
  }

  if (k<0) return; //index was invalid

  Sensors[k].snsName[0]=0;
  for (byte ii=0;ii<4;ii++) Sensors[k].IP[ii]=0;
  Sensors[k].ardID=0;
  Sensors[k].snsType=0;
  Sensors[k].snsID=0;
  Sensors[k].snsValue=0;
  Sensors[k].timeRead=0;
  Sensors[k].timeLogged=0;  
  Sensors[k].Flags = 0;
  Sensors[k].expired = 0;
  
}

bool isSensorInit(int i) {
  //check if sensor is initialized

  if (i<0 || i>=SENSORNUM) return false;
  if (Sensors[i].ardID>0 && Sensors[i].snsID > 0 && Sensors[i].snsType >0) return true;
  return false;  
}

byte checkExpiration(int i, time_t t, bool onlyCritical) {
  //check for expired sensors. special case if i is <0 then cycle through all sensors.
  //returns number of  expired sensors (if onlycritical=true then return critical exired count, if false then expire sensor but do not count it [and delete it if >24h old])

  if (t==0) t=now();

  if (i<0) {
    byte countfalse = 0;
    for (byte k=0;k<SENSORNUM;k++) countfalse+=checkExpiration(k,t,onlyCritical) ;
    return countfalse;
  }

  if (isSensorInit(i)) {
    if ((t-Sensors[i].timeLogged) > (2.1*Sensors[i].SendingInt) || (t-Sensors[i].timeLogged) > 86400)  /*consider a sensor expired if more than 210% of sendingint has passed... ie at least 2 missed reads || sensor is at least 24h old*/     {
      Sensors[i].expired=1;
      
      if (bitRead(Sensors[i].Flags,7))       return 1; //critical expired
      else {
        if ((t-Sensors[i].timeLogged) > 86400) {
          initSensor(i); //delete, and do not count this sensor!
          return 0; //noncritical and deleted
        }  
        if (onlyCritical==false) return 1;
        return 0; //noncritical expired
      }

    }
    else {
      Sensors[i].expired=0;
      return 0;
    }
  } 

  return 0;
  
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID > 0) c++; 
  }
  return c;
}

int16_t findDev(byte ardID, byte snsType, byte snsID,  bool oldest) {
  //overloaded, simpler version
  //provide the desired devID and snsname, and will return the index to sensors array that represents that node
  //special cases:
  //  if snsID = 0 then just find the first blank entry
  //if no finddev identified and oldest = true, will return first empty or oldest
  //if no finddev and oldest = false, will return -1
  
  if (snsID==0) { 
    return -1;  //can't find a 0 id sensor!
  }

  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].ardID == ardID && Sensors[j].snsType == snsType && Sensors[j].snsID == snsID) {
        return j;
      }
    }
    
//if I got here, then nothing found.
  if (oldest) {
    return findOldestDev();
  } 

  return -1; //dev not found
}

int16_t findDev(struct SensorVal *S, bool findBlank) {
  //provide the desired devID and snsname, and will return the index to sensors array that represents that node
  //special cases:
  //  if snsID = 0 then just find the first blank entry
  //if no finddev identified and findBlank = true, will return first empty or oldest
  //if no finddev and findBlank = false, will return -1
  
  if (S->snsID==0) {
        

    return -1;  //can't find a 0 id sensor!
  }
  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].ardID == S->ardID && Sensors[j].snsType == S->snsType && Sensors[j].snsID == S->snsID) {
                
        return j;
      }
    }
    
//if I got here, then nothing found.
  if (findBlank) {
  
    return findOldestDev();
  } 

  return -1; //dev not found
}

int16_t findSns(byte snstype, bool newest) {
  //find the first (or newest) instance of a sensor of tpe snstype
  //returns -1 if no sensor found
  
  if (snstype==0) {
        
    return -1;  //can't find a 0 id sensor!
  }

  uint16_t newestInd = -1;
  uint32_t newestTime = 0;

  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].snsType == snstype && Sensors[j].timeLogged > newestTime) {
        if (newest==false) return j;
        newestInd = j;
        newestTime = Sensors[j].timeLogged;
      }
  }
        
  return newestInd;

}  


uint8_t countFlagged(int snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan) {
  //count sensors of type snstype [default is 0, meaning all sensortypes], flags that matter [default is 00000011 - which means that I only care about RMB1 and RMB2], what the flags should be [default is 00000011, which means I am looking for sensors that are flagged and monitored], and last logged more recently than this time [default is 0]
  //special use cases for snstype:
  /*
    -1 = check all relevant sensor types
    -2 = temperature
    -3 = hvac
    -4 = 
    -10 = expired critical sensors
    */
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = this is a monitored sensor, so alarm if I don't get a read on time!

byte count =0;

if (snsType == -10) { //count expired and critical sensors
  count = checkExpiration(-1,0,true);

  return count;
}

int snsArr[10] = {0}; //this is for special cases

if (snsType == -1) { //critical sensors, all types
snsArr[0] = 1; 
snsArr[1] = 4;
snsArr[2] = 10;
snsArr[3] = 14;
snsArr[4] = 17;
snsArr[5] = 3;
snsArr[6] = 61;
snsArr[7] = 70;
snsArr[8] = -1; //change this to 5 for AHT RH, 2 for DHT RH, etc
snsArr[9] = -1;
} 

if (snsType == -2) { //temperature sensors
snsArr[0] = 1; 
snsArr[1] = 4;
snsArr[2] = 10;
snsArr[3] = 14;
snsArr[4] = 17;
snsArr[5] = -1;
snsArr[6] = -1;
snsArr[7] = -1;
snsArr[8] = -1;
snsArr[9] = -1;
} 

if (snsType == -3) { //hvac sensors
snsArr[0] = 55; 
snsArr[1] = 56;
snsArr[2] = 57;
snsArr[3] = -1;
snsArr[4] = -1;
snsArr[5] = -1;
snsArr[6] = -1;
snsArr[7] = -1;
snsArr[8] = -1;
snsArr[9] = -1;
} 


  for (byte j = 0; j<SENSORNUM; j++) {
    if (Sensors[j].expired==false) {
      if (snsType==0 || (snsType<0 && inArray(snsArr,10,Sensors[j].snsType)>=0) || (snsType>0 && (int) Sensors[j].snsType == snsType)) {
        
        if (((Sensors[j].Flags & flagsthatmatter) ==  (flagsthatmatter & flagsettings) /*flagged*/)) {
          if (snsType==3) {
            if (bitRead(Sensors[j].Flags,5) && Sensors[j].timeLogged> MoreRecentThan) count++;          
          } else {
            if (Sensors[j].timeLogged> MoreRecentThan) count++;          
          }
        }
      }
    }
  }

  return count;
}



String breakString(String *inputstr,String token) //take a pointer to input string and break it to the token, and shorten the input string at the same time. Remove token used.
{
  String outputstr = "";
  String orig = *inputstr;

  int strOffset = orig.indexOf(token,0);
  if (strOffset == -1) { //did not find the comma, we may have cut the last one. abort.
    return outputstr; //did not find the token, just return nothing and do not touch inputstr
  } else {
    outputstr= orig.substring(0,strOffset); //end index is NOT inclusive
    orig.remove(0,strOffset+1);
  }

  *inputstr = orig;
  return outputstr;

}


// IP fcns
String IPbytes2String(byte* IP) {
//reconstruct a string from 4 bytes. If the first or second element is 0 then use 192 or 168
  String IPs = "";

  for (byte j=0;j<3;j++){
    if (IP[j] ==0) {
      if (j==0) IPs = IPs + String(192,DEC) + ".";
      else {
        if (j==1)         IPs = IPs + String(168,DEC) + ".";
        else IPs = IPs + String(0,DEC) + ".";
      }
    } 
    else     IPs = IPs + String(IP[j],DEC) + ".";
  }

  IPs = IPs + String(IP[3],DEC);
  return IPs;
}

bool IPString2ByteArray(String IPstr,byte* IP) {
        
    String temp;
    
    int strOffset; 
    IPstr = IPstr + "."; //need the string to end with .
    for(byte j=0;j<4;j++) {
      strOffset = IPstr.indexOf(".", 0);
      if (strOffset == -1) { //did not find the . , IPstr not correct. abort.
        return false;
      } else {
        temp = IPstr.substring(0, strOffset);
        IP[j] = (uint8_t) temp.toInt();
        IPstr.remove(0, strOffset + 1);
      }
    }
    
    return true;
}


bool breakLOGID(String logID,byte* ardID,byte* snsID,byte* snsNum) {
    
    String temp;
    
    int strOffset = logID.indexOf(".", 0);
    if (strOffset == -1) { //did not find the . , logID not correct. abort.
      return false;
    } else {
      temp = logID.substring(0, strOffset);
      *ardID = temp.toInt();
      logID.remove(0, strOffset + 1);

      strOffset = logID.indexOf(".", 0);
      temp = logID.substring(0, strOffset);
      *snsID = temp.toInt();
      logID.remove(0, strOffset + 1);

      *snsNum = logID.toInt();
    }
    
    return true;
}


bool storeError(const char* E) {
  snprintf(I.lastError,75,"%s",E);
 I.lastErrorTime = I.currentTime+1; 
  return storeScreenInfoSD();

}

void controlledReboot(const char* E, RESETCAUSE R,bool doreboot) {
  
  //reset info
  I.resetInfo = R;
  I.lastResetTime=I.currentTime+1; //avoid assigning zero here.

  I.rebootsSinceLast=0;

  //error message
  storeError(E);

  if (doreboot) {
    //now restart
    ESP.restart();
  }
  
}

String lastReset2String(bool addtime) {

  String st = "";
  if (addtime) st = " @" + (String) dateify(I.lastResetTime);

  st = st + " [with " + (String) I.rebootsSinceLast + " uncontrolled reboots]";
  switch (I.resetInfo) {
    case RESETCAUSE::RESET_DEFAULT:
        return "Default" + st;
    case RESETCAUSE::RESET_SD:
        return "SD Error" + st;
    case RESETCAUSE::RESET_TIME:
        return "Time error" + st;
    case RESETCAUSE::RESET_USER:
        return "User reset" + st;
    case RESETCAUSE::RESET_WEATHER:
      return "Weather error" + st;
    case RESETCAUSE::RESET_WIFI:
      return "WiFi reset" + st;
    case RESETCAUSE::RESET_OTA:
      return "OTA reset" + st;
    case RESETCAUSE::RESET_UNKNOWN:
      return "Unknown reset" + st;
    default:
      return "???" + st;
  }

  return "?";

}

uint16_t CRCCalculator(uint8_t * data, uint16_t length)
{
   uint16_t curr_crc = 0x0000;
   uint8_t sum1 = (uint8_t) curr_crc;
   uint8_t sum2 = (uint8_t) (curr_crc >> 8);
   int index;
   for(index = 0; index < length; index = index+1)
   {
      sum1 = (sum1 + data[index]) % 255;
      sum2 = (sum2 + sum1) % 255;
   }
   return (sum2 << 8) | sum1;
}