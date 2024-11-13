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

bool storeSensorSD(struct SensorVal *S) {

    String filename = "/Data/Sensor" + (String) S->ardID + + "." + (String) S->snsType + "." + (String) S->snsID + ".dat";
    File f = SD.open(filename, FILE_APPEND);
    if (f==false) return false;
    union SensorValBytes D;
    D.sensordata=*S;

    f.write(D.bytes,sizeof(SensorVal));

    f.close();
    return true;
    
}

bool readSensorSD(byte ardID, byte snsType, byte snsID, uint32_t t[], double v[], byte *N, uint32_t starttime, uint32_t endtime,byte avgN ) {

    String filename = "/Data/Sensor" + (String) ardID + + "." + (String) snsType + "." + (String) snsID + ".dat";
    File f = SD.open(filename, FILE_READ);
    union SensorValBytes D;
    byte cnt = 0, deltacnt=0;
    double avgV = 0;
    if (f==false) return false;

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