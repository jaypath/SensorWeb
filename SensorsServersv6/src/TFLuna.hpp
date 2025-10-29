#ifdef _ISPERIPHERAL
#ifdef _USETFLUNA
#ifndef TFLUNA_HPP
#define TFLUNA_HPP
#include "Devices.hpp"
#include "globals.hpp"
#include "LEDMatrix.hpp"

  //use this library to read the tfluna
  #include <TFLI2C.h> // TFLuna-I2C Library v.0.1.0
  extern TFLI2C tflI2C;
  extern uint8_t tfAddr; // Default I2C address for Tfluna
  struct TFLunaType {
    uint16_t REFRESH_RATE = 20; //ms between refreshes. I believe TFluna in i2c is max 100hz, so lowest is 10
    //removed - now using prefs values -- int8_t BASEOFFSET=_TFLUNA_BASEOFFSET; //the "zero point" from the mounting location of the TFLUNA to where zero is (because TFLUNA may be mounted recessed, or the zero location is in front of the tfluna), in cm
    uint8_t ZONE_SHORTRANGE = _TFLUNA_SHORTRANGE; //cm from BASEOFFSET that is considered short range (show measures in inches now)
    uint8_t ZONE_CRITICAL = _TFLUNA_CRITICAL; //cm from BASEOFFSET at which you are too close
    uint32_t LAST_DRAW = 0; //last time screen ws drawn, millis()!
    int32_t LAST_DISTANCE=-5000; //cm of last distance
    int32_t LAST_DISTANCE_TIME=0; //time of last distance measure in ms
    bool INVERTED = false; //sjhould  the screen be inverted now?
    uint32_t SCREENRATE = 500; //in ms
    bool ALLOWINVERT=false; //if true, do inversions
    uint16_t CHANGETOCLOCK = 30; //in seconds, time to change to clock if dist hasn't changed
    bool CLOCKMODE = false; //show clock until distance change
    int16_t TFLUNASNS=-1; //sensor number of TFLUNA
    char MSG[20] = {0}; //screen message
  };

   extern TFLunaType LocalTF;
   extern TFLI2C tflI2C;
  

void checkTFLuna(int16_t snsindex=-1);
bool TFLunaUpdateMAX();
uint32_t DrawNow(uint32_t m);

extern Devices_Sensors Sensors;
extern STRUCT_CORE I;



#endif
#endif
#endif
