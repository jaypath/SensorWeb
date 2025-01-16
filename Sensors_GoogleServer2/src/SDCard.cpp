#include <SDCard.hpp>
//#define _DEBUG
extern String lastError;

//this allows a sensorval struct to be written to file as a series of bytes
union SensorValBytes {
    struct SensorVal sensordata;
    uint8_t bytes[sizeof(SensorVal)];
//    SensorValBytes(){};
//    ~SensorValBytes(){};
};



union ScreenInfoBytes {
    struct ScreenFlags screendata;
    uint8_t bytes[sizeof(ScreenFlags)];

    ScreenInfoBytes() {};
};


//storing variables
bool storeScreenInfoSD() {

    String filename = "/Data/ScreenFlags.dat";
    File f = SD.open(filename, FILE_APPEND);
    if (f==false) {
        lastError = "Failed to write to SD";
        return false;
    }
    union ScreenInfoBytes D; 
    D.screendata=ScreenInfo;

    f.write(D.bytes,sizeof(ScreenFlags));

    f.close();
    return true;
    
}

bool readScreenInfoSD() //read last screenInfo
{
    union ScreenInfoBytes D; 
    String filename = "/Data/ScreenFlags.dat";

    File f = SD.open(filename, FILE_READ);

    if (f==false)  {
        #ifdef _DEBUG
        Serial.printf("Failed to read screenInfo from SD\n");
        #endif
        lastError = "Failed to read screenInfo from SD";
        ScreenInfo.lastErrorTime = now();
        return false;
    }
    if (f.size() != sizeof(ScreenFlags)) {
              #ifdef _DEBUG
        Serial.printf("file on SD was not the size of screenInfo!\n");
        #endif

        lastError = "Screeninfo was the wrong size on SD card!";
        ScreenInfo.lastErrorTime = now();
        f.close();

        deleteFiles("ScreenFlags.dat","/Data");

        return false;
    }



    while (f.available()) {
        f.read(D.bytes,sizeof(ScreenFlags));
        ScreenInfo = D.screendata;
    }

    f.close();

    return true;

}


bool writeSensorsSD(String filename)
{ // write all the sensors to file
    union SensorValBytes D;



    File f = SD.open(filename, FILE_WRITE);
    if (f==false) return false;

    for (byte j=0;j<SENSORNUM;j++) {
        D.sensordata=Sensors[j];
        f.write(D.bytes, sizeof(SensorVal));
    }

    f.close();

    return true;

}



bool storeSensorSD(struct SensorVal *S) {
    //write a specific sensor to file
    String filename = "/Data/Sensor" + (String) S->ardID + + "." + (String) S->snsType + "." + (String) S->snsID + ".dat";
    File f = SD.open(filename, FILE_APPEND);
    if (f==false) return false;
    union SensorValBytes D; 
    D.sensordata=*S;

    f.write(D.bytes,sizeof(SensorVal));

    f.close();
    return true;
    
}

bool readSensorsSD(String filename) //read last available sensorvals back from disk
{
    union SensorValBytes D;

    //if I am reading in sensor vals, reset the sensor index.
    initSensor(-256); //reset them all!
    


    File f = SD.open(filename, FILE_READ);
    if (f==false) {
        lastError = "Failed to open Sensor file";
        ScreenInfo.lastErrorTime = now();

        return false;
    }

    byte cnt = 0;
    while (f.available()) {
        f.read(D.bytes,sizeof(SensorVal));
        if (cnt<SENSORNUM)             Sensors[cnt++] = D.sensordata;
        
    }
    #ifdef _DEBUG
        Serial.printf("readsensor cnt: %u\n",cnt);
    #endif
    if (cnt==0) {
        lastError = "Read ZERO sensors from SD";
        ScreenInfo.lastErrorTime = now();
    }
    f.close();

    return true;
}

bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N,uint32_t *samples, uint32_t starttime, uint32_t endtime,byte avgN ) {

    String filename = "/Data/Sensor" + (String) ardID + + "." + (String) snsType + "." + (String) snsID + ".dat";
    File f = SD.open(filename, FILE_READ);
    union SensorValBytes D;
    byte cnt = 0, deltacnt=0;
    double avgV = 0;
    if (f==false) return false;

    *samples = f.size()/sizeof(SensorVal);
    while (f.available()) {
        f.read(D.bytes,sizeof(SensorVal));

        if (D.sensordata.timeLogged < starttime) continue; //ignore values that aren't in the time range

        if (D.sensordata.timeLogged>endtime) break;

        if (cnt<*N) {
            avgV += D.sensordata.snsValue;
            deltacnt++;

            if (deltacnt>=avgN) {
                  t[cnt] = D.sensordata.timeLogged;
                v[cnt] = avgV/deltacnt;
                deltacnt=0;
                avgV=0;
                cnt++;
            } 
        } 
    }

    *N=cnt;
    f.close();

    return true;
}

bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, byte avgN) { //read the last N sensor readings, stating from starttime. return new N if there were less than N reads
    uint32_t tn=now();
    if (starttime==0) starttime = tn;
    if (avgN==0) avgN=1;

    String filename = "/Data/Sensor" + (String) ardID + + "." + (String) snsType + "." + (String) snsID + ".dat";
    File f = SD.open(filename, FILE_READ);
    union SensorValBytes D;
    double avgV = 0;
    byte svsz = sizeof(SensorVal);
    uint32_t avgT=0;
    byte goodcount=0;

    if (f==false) return false;

    uint32_t sz = f.size(); //file size 
    *samples = sz/svsz; //total number of stored samples


    
    //find the entry that is older than/equal to starttime

    uint16_t n=1;

    while (sz >= n*svsz) {
        f.seek(sz-n*svsz); //go to nth  entry from the end
        f.read(D.bytes,svsz); //read this block
        if ((uint32_t) D.sensordata.timeLogged <= starttime) {            
            break;
        } 
        else  n++;
    } 


    //the current position is the start of the block after sz-n, so current position contains all the blocks we can have. how many N can we return?
    if (f.position()<((*N) * avgN*svsz))  *N = (byte) ((double) f.position()/(avgN*svsz)); //This is how many N we can return with averaging.
    
   

    byte ind = 0;
    //now fill the vectors
    for (byte j=1;j<=*N;j++) {
        goodcount = 0;
        avgT = 0;
        avgV = 0;
        f.seek(sz-(n + j*avgN)*svsz); //go to jth set of entries relative to n. Note n so we are at the start of the nth block
        for (byte jj=0;jj<avgN;jj++) {
            f.read(D.bytes,svsz);
            if (D.sensordata.timeLogged < tn && D.sensordata.timeLogged > 1577854800) { //possible that a value was registered at an incorrect time - for example if time clock failed and we started at 0. Disregard such values if older than 1/1/2020
                avgV += D.sensordata.snsValue;
                avgT += tn-D.sensordata.timeLogged; //shift from now
                goodcount++;
            }
        }
        if (goodcount>0) {
            t[ind] = tn-(avgT/goodcount);
            v[ind++] = avgV/goodcount;
        }
    }
    *N=ind;
    f.close();

    return true;
}


//screen
uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}


void deleteFiles(const char* pattern,const char* directory) {
  File dir = SD.open(directory); // Open the root directory

    String filename ;;

  if (!dir) {
    #ifdef _DEBUG 
    Serial.println("Error opening root directory.");
    #endif
    return;
  }


  while (true) {
    File entry = dir.openNextFile();
    filename  = (String) directory + "/" + entry.name();
    #ifdef _DEBUG
        Serial.printf("Current file: %s\n",filename);
    #endif
    if (!entry) {
      // no more files
      break;
    }

    if (matchPattern(entry.name(), pattern)) {
        #ifdef _DEBUG
        Serial.print("Deleting: ");
        Serial.println(filename);
        #endif
      if (!SD.remove(filename)) {
        #ifdef _DEBUG
        Serial.print("Error deleting: ");
        Serial.println(filename);
        #endif
        
      }
    }
    entry.close();
  }
  dir.close();
}


// Simple wildcard matching function (supports * only)
bool matchPattern(const char* filename, const char* pattern) {
  const char* fnPtr = filename;
  const char* patPtr = pattern;

  while (*fnPtr != '\0' || *patPtr != '\0') {
    if (*patPtr == '*') {
      patPtr++; // Skip the '*'
      if (*patPtr == '\0') {
        return true; // '*' at the end matches everything remaining
      }
      while (*fnPtr != '\0') {
        if (matchPattern(fnPtr, patPtr)) {
          return true;
        }
        fnPtr++;
      }
      return false; // No match found after '*'
    } else if (*fnPtr == *patPtr) {
      fnPtr++;
      patPtr++;
    } else {
      return false; // Mismatch
    }
  }

  return (*fnPtr == '\0' && *patPtr == '\0'); // Both strings must be at the end
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
