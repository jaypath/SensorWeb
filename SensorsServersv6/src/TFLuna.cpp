#ifdef _ISPERIPHERAL
#ifdef _USETFLUNA
#include "TFLuna.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "timesetup.hpp"

 TFLunaType LocalTF;
 TFLI2C tflI2C;

 extern int16_t MY_DEVICE_INDEX;

bool setupTFLuna() {

  MY_DEVICE_INDEX = Sensors.findMyDeviceIndex();
  LocalTF.TFLUNASNS = Sensors.findSensor(MY_DEVICE_INDEX,7,1);
  if (LocalTF.TFLUNASNS == -1)  {
    SerialPrint("CheckTFLuna: failed to find TFLUNASNS: " + String(LocalTF.TFLUNASNS),true);
    return TFLunaFailed();
  }

  //get sensor number of TFLUNA   if it is not found, return
  SnsType* P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);
  if (P == NULL || !P->IsSet) {
    SerialPrint("CheckTFLuna: failed to find sensor, and TFLUNASNS: " + String(LocalTF.TFLUNASNS),true);
    return TFLunaFailed();
  }


  int16_t prefs_index = Sensors.getPrefsIndex(P);
  if (prefs_index == -1) {
    SerialPrint("TFLunaUpdateMAX: failed to find sensor, and TFLUNASNS: " + String(LocalTF.TFLUNASNS),true);
    LocalTF.TFLUNASNS = -1;
    return TFLunaFailed();
  }

  LocalTF.BASEOFFSET = Prefs.SNS_LIMIT_MIN[prefs_index];
  LocalTF.goldilockszone = Prefs.SNS_LIMIT_MAX[prefs_index] - LocalTF.BASEOFFSET; //less than max, but more than min. Flash "GOOD" at this point.
  LocalTF.criticalDistance = LocalTF.goldilockszone * _TFLUNA_CRITICAL ; //flash very fast "STOP!" at this point. _TFLUNA+CRITICAL will be a percentage of the goldilockszone
  LocalTF.shortrangeDistance = LocalTF.goldilockszone * _TFLUNA_SHORTRANGE ; //getting too close to min, flash "STOP" at this point. _TFLUNA+SHORTRANGE will be a percentage of the goldilockszone

  return true;
}


bool TFLunaFailed() {
  LocalTF.TFLUNASNS = -1;
  snprintf(LocalTF.MSG,19,"No TF");
  LocalTF.LAST_DRAW = DrawNow();
  return false;
}

uint32_t checkTFLuna(int16_t snsindex) {
  if (snsindex != -1)     LocalTF.TFLUNASNS = snsindex;
  
  if (LocalTF.TFLUNASNS == -1 && setupTFLuna()==false) {
    SerialPrint("checkTFLuna: failed to setup TFLuna",true);
    return -9999;
  }

    //get sensor number of TFLUNA   if it is not found, return
  SnsType* P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);

  int16_t tempval;
  if (tflI2C.getData(tempval, _USETFLUNA)) {
    if (tempval <= 0)           P->snsValue = -3000; //negative reading, but not failed
    else           P->snsValue = tempval ; //in actual cm, apply offsets later
  } else {
    P->snsValue = -5000; //failed
  }
  uint32_t m = millis();
  LocalTF.LAST_DISTANCE_TIME = m;

  return m;
}

bool TFLunaUpdateMAX() {
  uint32_t m = millis();
//check tfluna distance if it is time

  bool isfastmode = false;
  if (m<LocalTF.FASTMODEEXPIRES) {
    isfastmode = true;
  }
  if (m>LocalTF.LAST_DISTANCE_TIME+LocalTF.REFRESH_INTERVAL) {
    double distance_change=0;
    double actualdistance=0;
    SnsType* P = NULL;
    m = checkTFLuna(-1);
    if (m == -9999) {
      LocalTF.FASTMODEEXPIRES = 0;
      isfastmode = false;
    }
    else {
      P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);

      actualdistance = P->snsValue - LocalTF.BASEOFFSET;
      distance_change = abs(actualdistance - LocalTF.LAST_DISTANCE);
      LocalTF.ALLOWINVERT=false;
      LocalTF.SCREENRATE=1000; //default screen rate for clock
    }

    //am I in fast mode?
    if (isfastmode || distance_change> LocalTF.MIN_DIST_CHANGE) {

      
      LocalTF.LAST_MINUTE = 61; //this ensures a redraw when we leave this loop
      LocalTF.ALLOWINVERT=false;
      LocalTF.SCREENRATE=250;
      
      if (distance_change > LocalTF.MIN_DIST_CHANGE) {
        LocalTF.FASTMODEEXPIRES = m + LocalTF.FASTMODE_INTERVAL;
        LocalTF.LAST_DISTANCE = actualdistance;
        LocalTF.LAST_DISTANCE_CHANGE_TIME = m;
      }
      if (P->snsValue<-1000) {
        snprintf(LocalTF.MSG,19,"OPEN");
        LocalTF.ALLOWINVERT=true;
        LocalTF.SCREENRATE=500; //slow blink open
      } else {
        if (actualdistance<0) {
          snprintf(LocalTF.MSG,19,"STOP!!");
          LocalTF.ALLOWINVERT=true;
          LocalTF.SCREENRATE=100; //very fast blink emergency          
        } else {

          if (actualdistance<LocalTF.criticalDistance) {
            snprintf(LocalTF.MSG,19,"STOP!");
            LocalTF.ALLOWINVERT=true;
            LocalTF.SCREENRATE=200; //very fast blink emergency          
          } else if (actualdistance<LocalTF.shortrangeDistance) {
            snprintf(LocalTF.MSG,19,"STOP");
            LocalTF.ALLOWINVERT=true; //blinking at normal rate of 250            
          } else if (actualdistance<LocalTF.goldilockszone) {
            snprintf(LocalTF.MSG,19,"GOOD");
            LocalTF.ALLOWINVERT=true;
          } else {
            if (actualdistance>183) { //183 is ~6ft
              snprintf(LocalTF.MSG,19,"%d ft", (int) ((float) actualdistance/2.54/12));        
            }      else {
              snprintf(LocalTF.MSG,19,"%d in", (int) ((float) actualdistance/2.54));        
            }

          }
        }
      }
      DrawNow(m); 
      return true; //true means I am in fast mode
    } else {
      //draw clock, drawnow will check if the timing is correct
      
      if (minute()!=LocalTF.LAST_MINUTE) {
        snprintf(LocalTF.MSG,19,"%s",dateify(I.currentTime,"h1:nn"));      
        if (DrawNow(m)) LocalTF.LAST_MINUTE = minute();        
      }
      return false;      
    }
  }

  return isfastmode;
}



bool DrawNow(uint32_t m) {
  if (m==0)   m = millis();

  if (m < LocalTF.LAST_DRAW+LocalTF.SCREENRATE) return false;

  //should we flip the inversion?
  if (LocalTF.ALLOWINVERT) LocalTF.INVERTED = !LocalTF.INVERTED;      
  else       LocalTF.INVERTED = false;    

  LocalTF.LAST_DRAW = Matrix_Draw(LocalTF.INVERTED, (const char*) LocalTF.MSG);  
  return true;
}

    
#endif
#endif
