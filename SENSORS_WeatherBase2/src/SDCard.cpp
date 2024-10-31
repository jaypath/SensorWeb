#include <SDCard.hpp>
#include <globals.hpp>
#include <SD.h>





//this allows a sensorval struct to be written to file as a series of bytes
union SensorValBytes {
    struct SensorVal sensordata;
    uint8_t bytes[sizeof(SensorVal)];
    SensorValBytes(){};
    ~SensorValBytes(){};
};

bool writeSensorSD(struct SensorVal &S, String filename)
{
    union SensorValBytes D;

    D.sensordata=S;


    File f = SD.open(filename, FILE_APPEND);

    f.write(D.bytes, sizeof(SensorVal));
    f.close();



    return true;

}

