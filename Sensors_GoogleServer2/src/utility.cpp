//#define _DEBUG
#include <utility.hpp>


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


void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  
  File root = fs.open(dirname);
  if(!root){
    return;
  }
  if(!root.isDirectory()){
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
  //    Serial.print("  DIR : ");
    //  Serial.println(file.name());
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      // Serial.print("  FILE: ");
      // Serial.print(file.name());
      // Serial.print("  SIZE: ");
      // Serial.println(file.size());
    }
    file = root.openNextFile();
  }
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
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = not used by the server

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
  //return 0 entry or oldest
  int oldestInd = 0;
  int  i=0;
  for (i=0;i<SENSORNUM;i++) {
    if (Sensors[i].ardID == 0 && Sensors[i].snsType == 0 && Sensors[i].snsID ==0) return i;
    if (Sensors[oldestInd].timeLogged == 0) {
      oldestInd = i;
    }    else {
      if (Sensors[i].timeLogged< Sensors[oldestInd].timeLogged && Sensors[i].timeLogged >0) oldestInd = i;
    }
  }
  
//  if (Sensors[oldestInd].timeLogged == 0) oldestInd = -1;

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
        if (Sensors[i].snsID>0 && Sensors[i].timeLogged>0 && (uint32_t) (ScreenInfo.t-Sensors[i].timeLogged)>k*60)  initSensor(i); //convert k to seconds and  remove N hour old values 
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
  
  Sensors[k].IP[0] = 0;
  Sensors[k].IP[1] = 0;
  Sensors[k].IP[2] = 0;
  Sensors[k].IP[3] = 0;

  Sensors[k].snsValue_MAX=-10000;
  Sensors[k].snsValue_MIN=10000;

  Sensors[k].localFlags=0;  

}

uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (isSensorInit(j)) c++; 
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

int16_t findDev(struct SensorVal *S, bool oldest) {
  //provide the desired devID and snsname, and will return the index to sensors array that represents that node
  //special cases:
  //  if snsID = 0 then just find the first blank entry
  //if no finddev identified and oldest = true, will return first empty or oldest
  //if no finddev and oldest = false, will return -1
  
  if (S->snsID==0) {
        

    return -1;  //can't find a 0 id sensor!
  }
  for (int j=0;j<SENSORNUM;j++)  {
      if (Sensors[j].ardID == S->ardID && Sensors[j].snsType == S->snsType && Sensors[j].snsID == S->snsID) {
                
        return j;
      }
    }
    
//if I got here, then nothing found.
  if (oldest) {
  
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
    if (bitRead(Sensors[j].localFlags,1)==1) {
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


byte checkExpiration(int i, time_t t_ago, bool onlyCritical) {
  //check for expired sensors. special case if i is <0 then cycle through all sensors.
  //returns number of  expired sensors (if onlycritical=true then return critical exired count, if false then expire sensor but do not count it [and delete it if >24h old])

  if (t==0) t_ago=now();

  if (i<0) {
    byte countfalse = 0;
    for (byte k=0;k<SENSORNUM;k++) countfalse+=checkExpiration(k,t_ago,onlyCritical) ;
    return countfalse;
  }

  if (isSensorInit(i)) {
    bitWrite(Sensors[i].localFlags,1,0);
    if ((t_ago-Sensors[i].timeLogged) > (3*Sensors[i].SendingInt) || (t_ago-Sensors[i].timeLogged) > 86400)  /*consider a sensor expired if more than 300% of sendingint has passed... ie at least 3 missed reads || sensor is at least 24h old*/     {
      bitWrite(Sensors[i].localFlags,1,1);
      
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
      bitWrite(Sensors[i].localFlags,1,0);
      return 0;
    }
  } 

  return 0;
  
}

void checkHeat() {
  //heat byte, from RIGHT to LEFT bits are: heat is on, zone 1, zone 2, zone 3... zone 6, unused
  //cool byte from RIGHT to LEFT bits are: AC compressor is on, zone 1, zone 2, zone 3... zone 6, unused
  //cool byte from RIGHT to LEFT bits are: AC compressor is on, zone 1, zone 2, zone 3... zone 6, unused
  //fan byte from RIGHT TO LEFT bits are: fan is on,  zone 1, zone 2, zone 3... zone 6, unused
  //initialize heat and cool variaiables, now all bits are 0

  /*
  ZONES (in system):
  1=office
  2=mast br
  3=lr/dr
  4=bed
  5=kitch/fam
  6=den

  ZONES (per me, this is hardwired into receiver arduino):
  1=FamRm
  2=LR/DR
  3=office
  4=Bedrms
  5=MastBR
  6=Den

  */



ScreenInfo.isHeat=0;
ScreenInfo.isAC=0;
ScreenInfo.isFan=0;


  for (byte j=0;j<SENSORNUM;j++) {
    if (Sensors[j].snsType == 55) { //heat
      if ( (Sensors[j].Flags&1) == 1 && (uint32_t) ScreenInfo.t-Sensors[j].timeLogged < 1800 ) bitWrite(ScreenInfo.isHeat,Sensors[j].snsID,1); //Sensors[j].Flags&1 == 1 means that 0th bit is 1, ie flagged. note that 0th bit is that any heater is on or off!. If a reading is >30 minutes it is too old to be believed
      else bitWrite(ScreenInfo.isHeat,Sensors[j].snsID,0);
    } else {
      if (Sensors[j].snsType == 56 && (Sensors[j].Flags&1)==1) { //ac comp        
        //which ac?
        String S = Sensors[j].snsName;         
        if (S.indexOf("ACDown")>-1) {
          bitWrite(ScreenInfo.isAC,1,1);
          bitWrite(ScreenInfo.isAC,2,1);
          bitWrite(ScreenInfo.isAC,3,1);
          bitWrite(ScreenInfo.isAC,6,1);
        } else {
          bitWrite(ScreenInfo.isAC,4,1);
          bitWrite(ScreenInfo.isAC,5,1);
        }
      }
      if (Sensors[j].snsType == 57 && (Sensors[j].Flags&1)==1) { //ac fan
        String S = Sensors[j].snsName;         
        if (S.indexOf("ACDown")>-1) {
          bitWrite(ScreenInfo.isFan,1,1);
          bitWrite(ScreenInfo.isFan,2,1);
          bitWrite(ScreenInfo.isFan,3,1);
          bitWrite(ScreenInfo.isFan,6,1);
        } else {
          bitWrite(ScreenInfo.isFan,4,1);
          bitWrite(ScreenInfo.isFan,5,1);
        }
      }
    }
  }

  if (ScreenInfo.isHeat>0)     bitWrite(ScreenInfo.isHeat,0,1); //any heater is on    

  if (ScreenInfo.isAC>0)     bitWrite(ScreenInfo.isAC,0,1); //any ac is on
    
  if (ScreenInfo.isFan>0) bitWrite(ScreenInfo.isFan,0,1); //any fan is on
  
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

void tallyFlags() {
  if (ScreenInfo.t - ScreenInfo.lastFlagTally < ScreenInfo.intervalFlagTallySEC) return; //not time. intervalflagtally in seconds

  //expire old sensors and characterize summary stats
    ScreenInfo.isFlagged = 0;
    ScreenInfo.isExpired=0;
    ScreenInfo.isSoilDry=0;
    

  
    ScreenInfo.isFlagged =countFlagged(-1,B00000111,B00000011,0); //-1 means flag for all common sensors. Note that it is possible for count to be zero for expired sensors in some cases (when morerecentthan is >0)
    
    checkHeat(); //this updates ScreenInfo.isheat and ScreenInfo.isac

    ScreenInfo.isSoilDry =countFlagged(3,B00000111,B00000011,(ScreenInfo.t>3600)?ScreenInfo.t-3600:0);

    ScreenInfo.isHot =countFlagged(-2,B00100111,B00100011,(ScreenInfo.t>3600)?ScreenInfo.t-3600:0);

    ScreenInfo.isCold =countFlagged(-2,B00100111,B00000011,(ScreenInfo.t>3600)?ScreenInfo.t-3600:0);

    ScreenInfo.isLeak = countFlagged(70,B00000001,B00000001,(ScreenInfo.t>3600)?ScreenInfo.t-3600:0);


    ScreenInfo.isExpired = checkExpiration(-1,ScreenInfo.t,true); //this counts critical  expired sensors
    ScreenInfo.isFlagged+=ScreenInfo.isExpired; //add expired count, because expired sensors don't otherwiseget included
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

String IP2String(byte* IP) {
//reconstruct a string from 4 bytes. If the first or second element is 0 then use 192 or 168
  String IPs = "";

    if (IP[0] ==0)       IPs = IPs + String(192,DEC) + ".";
    else IPs = IPs + String(IP[0],DEC) + ".";

    if (IP[1] ==0)       IPs = IPs + String(168,DEC) + ".";
    else IPs = IPs + String(IP[1],DEC) + ".";

    IPs = IPs + String(IP[2],DEC) + ".";

    IPs = IPs + String(IP[3],DEC);
  return IPs;
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

byte getButton(int32_t X, int32_t Y,  byte ScreenNum) {
  //returns 0 if no button was pushed, otherwise the number of the button based on screenNum
  if (ScreenNum == 0 || (X == 0 && Y == 0) ) return 0;

  if (ScreenNum >= 1) {
    if (Y < TFT_HEIGHT*0.75) return 0; //button press was not in button zone

    //6 buttons at bottom, arranged on bottom 25% of screen [3 columns of 2 rows]
    byte deltaX = TFT_WIDTH * 0.33; //width of each button row
    byte deltaY = TFT_HEIGHT * 0.125; //height of each button row

    if (Y<TFT_HEIGHT*0.75 + deltaY) { //row 1
      if (X<deltaX) return 1;
      if (X<2*deltaX) return 2;
      return 3;
    } else {
      if (X<deltaX) return 4;
      if (X<2*deltaX) return 5;
      return 6;
    }
  }
return 0;
}




bool isSensorInit(int i) {
  //check if sensor is initialized

  if (i<0 || i>=SENSORNUM) return false;
  
  if (Sensors[i].ardID>0 && Sensors[i].snsID > 0 && Sensors[i].snsType >0) return true;
  return false;  
}

double minmax(double value, double minval, double maxval) {
  if (value<minval) value = minval;
  if (value>maxval) value = maxval;

  return value;

}
