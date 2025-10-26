#ifdef _ISPERIPHERAL
#ifdef _USETFLUNA
#include "TFLuna.hpp"
#include "Devices.hpp"

#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

//Defining variables for the LED display:
//FOR HARDWARE TYPE, only uncomment the only that fits your hardware type
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW //blue PCP with 7219 chip
//#define HARDWARE_TYPE MD_MAX72XX:GENERIC_HW
#define MAX_DEVICES 4
#define CS_PIN 5
#define CLK_PIN     18
#define DATA_PIN    23

MD_Parola screen = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); //hardware spi
//MD_Parola screen = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES); // Software spi

 TFLunaType LocalTF;
 TFLI2C tflI2C;

void checkTFLuna(int16_t snsindex) {
  uint32_t m = millis();
  if (snsindex != -1)     LocalTF.TFLUNASNS = snsindex;
  if (LocalTF.TFLUNASNS == -1)     LocalTF.TFLUNASNS = Sensors.findSnsOfType(7,true);
    //get sensor number of TFLUNA   if it is not found, return
  SnsType* P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);
  if (P == NULL || !P->IsSet) {
    LocalTF.TFLUNASNS = -1;
    snprintf(LocalTF.MSG,19,"No TF");
    MD_Draw();
    return;
  }

  int16_t tempval;
  if (tflI2C.getData(tempval, _USETFLUNA)) {
    if (tempval <= 0)           P->snsValue = -1000;
    else           P->snsValue = tempval; //in cm
  } else {
    P->snsValue = -5000; //failed
  }

}

void updateTFLunaDisplay(char* datestring) {
  uint32_t m = millis();

  //check tfluna distance if it is time
  if (m>LocalTF.LAST_DISTANCE_TIME+LocalTF.REFRESH_RATE) {
    LocalTF.LAST_DISTANCE_TIME = m;
    SnsType* P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);    
    if (P == NULL || !P->IsSet) {
    LocalTF.TFLUNASNS = -1;
    snprintf(LocalTF.MSG,19,"No TF");
    MD_Draw();
    return;
    }
    float tempdist = P->snsValue-LocalTF.BASEOFFSET;
    if (tempdist<-3000) {
      snprintf(LocalTF.MSG,19,"SLOW");
      LocalTF.ALLOWINVERT=true;
      LocalTF.SCREENRATE=250;
      LocalTF.CLOCKMODE = false; //leave clockmode
    } else {
      
      //what to draw?
      LocalTF.ALLOWINVERT=false;

      //has dist changed by more than a real amount? If yes then allow high speed screen draws
      if ((int32_t) abs((int32_t)LocalTF.LAST_DISTANCE-tempdist)> LocalTF.MIN_DIST_CHANGE) {
        LocalTF.SCREENRATE=250;
        LocalTF.CLOCKMODE = false; //leave clockmode

        //store last distance
        LocalTF.LAST_DISTANCE = tempdist;

      //is the distance unreadable (which means no car/garage door open)
      if (tempdist<-100) {
        snprintf(LocalTF.MSG,19,"SLOW");
      } else {
        //is the distance beyond the short range zone
        if (tempdist>LocalTF.ZONE_SHORTRANGE) {
          snprintf(LocalTF.MSG,19,"%.1f ft", (tempdist/2.54)/12);        
        } else {
          LocalTF.SCREENRATE=250;
          if (tempdist>LocalTF.ZONE_GOLDILOCKS) {
            snprintf(LocalTF.MSG,19,"%d in", (int) (tempdist/2.54));
          } else {
            if (tempdist>LocalTF.ZONE_CRITICAL) {
              snprintf(LocalTF.MSG,19,"GOOD");
              LocalTF.ALLOWINVERT=true;
            } else {
              snprintf(LocalTF.MSG,19,"STOP!");
              LocalTF.ALLOWINVERT=true;
            }    
          }    
        }
      }
    } else {
      //if it's been long enough, change to clock and redraw
      if (LocalTF.CLOCKMODE || m-LocalTF.LAST_DRAW>LocalTF.CHANGETOCLOCK*1000) { //changetoclock is in seconds
        snprintf(LocalTF.MSG,19,"%s",datestring);      
        LocalTF.ALLOWINVERT=false;
        LocalTF.SCREENRATE=30000;
        LocalTF.CLOCKMODE = true;
      } else {
        LocalTF.SCREENRATE=500;
        //are we in a critical zone where we're flashing?
        if (tempdist<=LocalTF.ZONE_GOLDILOCKS) {
          LocalTF.ALLOWINVERT=true;
          if (tempdist<=LocalTF.ZONE_CRITICAL)   LocalTF.SCREENRATE=250;
        }
      }
    }
  }

  MD_Draw();
}
  
}

void MD_Draw(void) {
       
uint32_t m = millis();

if (m-LocalTF.LAST_DRAW < LocalTF.SCREENRATE) return;


//should we flip the inversion?
if (LocalTF.ALLOWINVERT) LocalTF.INVERTED = !LocalTF.INVERTED;      
else       LocalTF.INVERTED = false;    



screen.displayClear();
screen.setTextAlignment(PA_CENTER);       
screen.setInvert(LocalTF.INVERTED);
//screen.print(LocalTF.MSG);
screen.printf("%s",LocalTF.MSG);
LocalTF.LAST_DRAW = m;    

  
}


    
#endif
#endif
