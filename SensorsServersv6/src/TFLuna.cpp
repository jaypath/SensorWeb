#ifdef _ISPERIPHERAL
#ifdef _USETFLUNA
#include "TFLuna.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "timesetup.hpp"

 TFLunaType LocalTF;
 TFLI2C tflI2C;

 extern int16_t MY_DEVICE_INDEX;

uint32_t checkTFLuna(int16_t snsindex) {
  if (snsindex != -1)     LocalTF.TFLUNASNS = snsindex;
  
  if (LocalTF.TFLUNASNS == -1)     LocalTF.TFLUNASNS = Sensors.findSensor(MY_DEVICE_INDEX,7,1);

    //get sensor number of TFLUNA   if it is not found, return
  SnsType* P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);
  if (P == NULL || !P->IsSet) {
    SerialPrint("CheckTFLuna: failed to find sensor, and TFLUNASNS: " + String(LocalTF.TFLUNASNS),true);
    LocalTF.TFLUNASNS = -1;
    snprintf(LocalTF.MSG,19,"No TF");
    LocalTF.LAST_DRAW = DrawNow();
    return 0;
  }


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

void TFLunaUpdateMAX() {
  uint32_t m = millis();
//check tfluna distance if it is time
if (m>LocalTF.LAST_DISTANCE_TIME+LocalTF.REFRESH_INTERVAL) {

  m = checkTFLuna(-1);
  SnsType* P = Sensors.getSensorBySnsIndex(LocalTF.TFLUNASNS);
  if (P == NULL || !P->IsSet) {
    SerialPrint("TFLunaUpdateMAX: failed to find sensor, and TFLUNASNS: " + String(LocalTF.TFLUNASNS),true);
    LocalTF.TFLUNASNS = -1;
    snprintf(LocalTF.MSG,19,"No TF");
    LocalTF.LAST_DRAW = DrawNow(m);
    return;
  }

  int16_t prefs_index = Sensors.getPrefsIndex(P);
  if (prefs_index == -1) {
    SerialPrint("TFLunaUpdateMAX: failed to find sensor, and TFLUNASNS: " + String(LocalTF.TFLUNASNS),true);
    LocalTF.TFLUNASNS = -1;
    snprintf(LocalTF.MSG,19,"No TF");
    LocalTF.LAST_DRAW = DrawNow();
    return;
  }


  double actualdistance = P->snsValue - Prefs.SNS_LIMIT_MIN[prefs_index]; //Prefs.SNS_LIMIT_MIN[prefs_index] is zero for our calculations
  double distance_change = abs(actualdistance - LocalTF.LAST_DISTANCE);
  LocalTF.ALLOWINVERT=false;
  LocalTF.SCREENRATE=1000; //default screen rate for clock


  //has dist changed by more than a real amount? If yes then allow high speed screen draws
  if ((distance_change)> 2) {

    uint16_t goldilockszone = Prefs.SNS_LIMIT_MAX[prefs_index] - Prefs.SNS_LIMIT_MIN[prefs_index]; //less than max, but more than min. Flash "GOOD" at this point.
    uint16_t criticalDistance = goldilockszone * _TFLUNA_CRITICAL ; //flash very fast "STOP!" at this point. _TFLUNA+CRITICAL will be a percentage of the goldilockszone
    uint16_t shortrangeDistance = goldilockszone * _TFLUNA_SHORTRANGE ; //getting too close to min, flash "STOP" at this point. _TFLUNA+SHORTRANGE will be a percentage of the goldilockszone
    
    WiFi.disconnect(true); //disconnect from wifi to avoid distractions
    do {  
      LocalTF.LAST_MINUTE = 61; //this ensures a redraw when we leave this loop
      LocalTF.ALLOWINVERT=false;
      LocalTF.SCREENRATE=250;
      
      if (abs(actualdistance - LocalTF.LAST_DISTANCE) > 2) {
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

          if (actualdistance<criticalDistance) {
            snprintf(LocalTF.MSG,19,"STOP!");
            LocalTF.ALLOWINVERT=true;
            LocalTF.SCREENRATE=200; //very fast blink emergency          
          } else if (actualdistance<shortrangeDistance) {
            snprintf(LocalTF.MSG,19,"STOP");
            LocalTF.ALLOWINVERT=true; //blinking at normal rate of 250            
          } else if (actualdistance<goldilockszone) {
            snprintf(LocalTF.MSG,19,"GOOD");
            LocalTF.ALLOWINVERT=true;
          } else {
            if (actualdistance>61) { //61 is 2 ft
              snprintf(LocalTF.MSG,19,"%.1f ft", (float) actualdistance/2.54/12);        
            }      else {
              snprintf(LocalTF.MSG,19,"%f in", (float) actualdistance/2.54);        
            }

          }
        }
      }
      if (DrawNow()) {
        m=checkTFLuna(-1);
        actualdistance = P->snsValue - Prefs.SNS_LIMIT_MIN[prefs_index];
        distance_change = abs(actualdistance - LocalTF.LAST_DISTANCE);  
      } else {
        m=millis();
      }
    } while((distance_change)> 2 || m-LocalTF.LAST_DISTANCE_CHANGE_TIME <LocalTF.CHANGETOCLOCK*1000); //keep repeating until distance has not changed and at least clocktime has passed  
    WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD);
  } else {
    //draw clock, drawnow will check if the timing is correct
    
    if (minute()!=LocalTF.LAST_MINUTE) {
      snprintf(LocalTF.MSG,19,"%s",dateify(I.currentTime,"h1:nn"));      
      if (DrawNow(m)) LocalTF.LAST_MINUTE = minute();
      if (WiFi.status() != WL_CONNECTED ) { //retry wifi every 1 minute
        WiFi.begin((char *) Prefs.WIFISSID, (char *) Prefs.WIFIPWD); //retry wifi every 60 seconds
      }
      
    }
  }

}

return;
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
