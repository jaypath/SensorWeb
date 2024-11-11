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

bool writeSensorSD(String filename)
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


bool readSensorSD(String filename) //read last available sensorvals back from disk
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
