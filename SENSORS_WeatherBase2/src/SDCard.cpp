#include <SDCard.hpp>
#include <globals.hpp>
#include <SD.h>


//this allows a sensorval struct to be written to file as a series of bytes
union SensorValBytes {
    struct SensorVal sensordata;
    uint8_t bytes[sizeof(SensorVal)];
//    SensorValBytes(){};
//    ~SensorValBytes(){};
};

bool writeSensorsSD(String filename)
{
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

    String filename = "/Data/Sensor" + (String) S->ardID + + "." + (String) S->snsType + "." + (String) S->snsID + "_v2.dat";
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


    File f = SD.open(filename, FILE_READ);
    if (f==false) return false;

    byte cnt = 0;
    while (f.available()) {
        f.read(D.bytes,sizeof(SensorVal));
        Sensors[cnt++] = D.sensordata;
    }

    f.close();

    return true;

}

//overloaded version to read multiple values and average them together
bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N,uint32_t *samples, uint32_t starttime, uint32_t endtime,byte avgN ) {

    String filename = "/Data/Sensor" + (String) ardID + + "." + (String) snsType + "." + (String) snsID + "_v2.dat";
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
    if (starttime==0) starttime = I.currentTime;
    if (avgN==0) avgN=1;

    String filename = "/Data/Sensor" + (String) ardID + + "." + (String) snsType + "." + (String) snsID + "_v2.dat";
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
            if (D.sensordata.timeLogged < I.currentTime && D.sensordata.timeLogged > 1577854800) { //possible that a value was registered at an incorrect time - for example if time clock failed and we started at 0. Disregard such values if older than 1/1/2020
                avgV += D.sensordata.snsValue;
                avgT += I.currentTime-D.sensordata.timeLogged; //shift from now
                goodcount++;
            }
        }
        if (goodcount>0) {
            t[ind] = I.currentTime-(avgT/goodcount);
            v[ind++] = avgV/goodcount;
        }
    }
    *N=ind;
    f.close();

    return true;
}

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


