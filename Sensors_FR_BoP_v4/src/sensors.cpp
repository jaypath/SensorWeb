#include "sensors.hpp"

/*sens types
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - 
//7 - distance, HC-SR04
//8 - human presence (mm wave)
//9 - BMP pressure
//10 - BMP temp
//11 - BMP altitude
//12 - Pressure derived prediction (uses an array called BAR_HX containing hourly air pressure for past 24 hours). REquires _USEBARPRED be defined
//13 - BMe pressure
//14 - BMe temp
//15 - BMe humidity
//16 - BMe altitude
//17 - BME680 temp
18 - BME680 rh
19 - BME680 air press
20  - BME680 gas sensor
21 - human present (mmwave)
50 - any binary, 1=yes/true/on
51 = any on/off switch
52 = any yes/no switch
53 = any 3 way switch
54 = 
55 - heat on/off {requires N DIO Pins}
56 - a/c  on/off {requires 2 DIO pins... compressor and fan}
57 - a/c fan on/off
58 - 
60 -  battery power
61 - battery %
70 - leak sensor 
98 - clock
99 = any numerical value
100+ is a server type sensor, to which other sensors will send their data
100 = any server (receives data), disregarding subtype
101 - weather display server with local persistent storage (ie SD card)
102 = any weather server that has no persistent storage
103 = any server with local persistent storage (ie SD card) that uploads data cloud storage
104 = any server without local persistent storage that uploads data cloud storage
 

*/

extern Devices_Sensors Sensors;

#if defined(_CHECKAIRCON) || defined(_CHECKHEAT) 
uint8_t HVACSNSNUM = 0;
#endif


// No local Sensors[] array; use Sensors exclusively

#ifdef _USETFLUNA
TFLunaType LocalTF;
uint8_t tfAddr = _USETFLUNA;
TFLI2C tflI2C;
#endif

#ifdef _WEBCHART
  SensorChart SensorCharts[_WEBCHART];
#endif

#if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 
  HISTORY HVACHX[SENSORNUM];

  #ifdef _USECALIBRATIONMODE
    void checkHVAC(void) {
      //push peak to peak values into HVACHX array
      for (byte j=0;j<SENSORNUM;j++) {
        pushDoubleArray(HVACHX[j].values,_HVACHXPNTS,peak_to_peak(SENSORS_TO_CHART[j],100));
      }
    
    }
  #endif

  
#endif


//  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified)


#ifdef _USEBME680
  BME680_Class BME680;  ///< Create an instance of the BME680 class
  int32_t temperature, humidity, pressure, gas;
  uint32_t last_BME680 =0;
#endif

#ifdef _USEBME680_BSEC
  Bsec iaqSensor;
  
#endif


#ifdef DHTTYPE
  DHT dht(_USEDHT,DHTTYPE,11); //third parameter is for faster cpu, 8266 suggested parameter is 11
#endif

#ifdef _USEAHT
  AHTxx aht(AHTXX_ADDRESS_X38, AHT2x_SENSOR);  
#endif


#ifdef _USEAHTADA
  Adafruit_AHTX0 aht;  
#endif



#ifdef _USEBMP

  Adafruit_BMP280 bmp; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef _USEBME

  Adafruit_BME280 bme; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef  _USESSD1306

  SSD1306AsciiWire oled;
#endif






void setupSensors() {
/* legacy template removed; Sensors is now the source of truth */

int16_t myDevID = Sensors.findMe();

int16_t monitored_sns[] = MONITORINGSTATES;
int16_t outside_sns[] = OUTSIDESTATES;
int16_t interval_poll[] = INTERVAL_POLL;
int16_t interval_send[] = INTERVAL_SEND;
int16_t limit_max[] = LIMIT_MAX;
int16_t limit_min[] = LIMIT_MIN;
byte sensortypes[] = SENSORTYPES;


#if defined(_CHECKHEAT) || defined(_CHECKAIRCON)
//init hvachx 
  for (byte j=0;j<SENSORNUM;j++) {
    HVACHX[j].interval = 60*60; //seconds
    HVACHX[j].lastRead = 0;
    for (byte jj=0;jj<_HVACHXPNTS;jj++) HVACHX[j].values[jj]=0;
  }

  #if defined(_USEMUX) && defined(_CHECKHEAT)
    pinMode(DIOPINS[0],OUTPUT);
    pinMode(DIOPINS[1],OUTPUT);
    pinMode(DIOPINS[2],OUTPUT);
    pinMode(DIOPINS[3],OUTPUT);
    pinMode(DIOPINS[4],INPUT);
    digitalWrite(DIOPINS[0],LOW);
    digitalWrite(DIOPINS[1],LOW);
    digitalWrite(DIOPINS[2],LOW);
    digitalWrite(DIOPINS[3],LOW);
    #ifdef _DEBUG
      Serial.println("dio configured");
    #endif

  #endif

  #if defined(_CHECKAIRCON)
    pinMode(DIOPINS[0],OUTPUT);
    pinMode(DIOPINS[1],OUTPUT);
    pinMode(DIOPINS[2],INPUT);
    pinMode(DIOPINS[3],INPUT);
    digitalWrite(DIOPINS[0],LOW);
    digitalWrite(DIOPINS[1],LOW);    
  #endif



#endif

//double sc_multiplier = 0;
//int sc_offset;


  for (byte i=0;i<SENSORNUM;i++) {
    uint8_t stype = sensortypes[i];
    if (stype == 0) continue;
    uint8_t flags = 0;
    bitWrite(flags,7,1);
    if (monitored_sns[i]) bitWrite(flags,1,1);
    else bitWrite(flags,1,0);

    
    if (outside_sns[i]) bitWrite(flags,2,1);
    else bitWrite(flags,2,0);

    if (Prefs.SNS_LIMIT_MAX[i]==0) {
      Prefs.SNS_LIMIT_MAX[i]=limit_max[i]; Prefs.SNS_LIMIT_MIN[i]=limit_min[i];
    }
    if (Prefs.SNS_INTERVAL_POLL[i]==0) {
      Prefs.SNS_INTERVAL_POLL[i]=interval_poll[i];
    }
    if (Prefs.SNS_INTERVAL_SEND[i]==0) {
      Prefs.SNS_INTERVAL_SEND[i]=interval_send[i];
    }


    switch (stype) {
      case 1: //DHT temp
      {
        #ifdef DHTTYPE
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), stype, 0, String(MYNAME+String("_T")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        #endif
        break;
      }
      case 2: //DHT RH
       {

        #ifdef DHTTYPE
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), stype, 0, String(MYNAME+String("_RH")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        #endif
        break;
       }
      case 3: //soil
        {
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        #ifdef _USESOILCAP
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), stype, 0, String(MYNAME+String("_soil")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        #endif
        #ifdef _USESOILRES
          pinMode(_USESOILRES,OUTPUT);  
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), stype, 0, String(MYNAME+String("_soilR")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        #endif

        break;
        }
      case 4: //AHT temp
        {
        #if defined(_USEAHT) || defined(_USEAHTADA)
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          int16_t si = Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 4, 0, String(MYNAME+String("_AHT_T")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        #endif
        break;
        }
      case 5: //aht rh
        {
        #if defined(_USEAHT) || defined(_USEAHTADA)
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 5, 0, String(MYNAME+String("_AHT_RH")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        #endif
        break;
        }

      case 7: //dist
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        bitWrite(flags,7,0);
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 7, 0, String(MYNAME+String("_Dist")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 9: //BMP pres
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 9, 0, String(MYNAME+String("_hPa")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 10: //BMP temp
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 10, 0, String(MYNAME+String("_BMP_t")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 11: //BMP alt
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 11, 0, String(MYNAME+String("_alt")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 12: //Bar prediction
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        bitWrite(flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        bitWrite(flags,3,1); bitWrite(flags,4,1);
        
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 12, 0, String(MYNAME+String("_Pred")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 13: //BME pres
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 13, 0, String(MYNAME+String("_hPa")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 14: //BMEtemp
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 14, 0, String(MYNAME+String("_BMEt")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 15: //bme rh
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        bitWrite(flags,7,0);
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 15, 0, String(MYNAME+String("_BMErh")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 16: //bme alt
        {
        Prefs.SENSORIDS[i] = stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        bitWrite(flags,7,0);
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 16, 0, String(MYNAME+String("_alt")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 17: //bme680
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 17, 0, String(MYNAME+String("_T")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 18: //bme680
        {
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 18, 0, String(MYNAME+String("_RH")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 19: //bme680
        {
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 19, 0, String(MYNAME+String("_hPa")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        break;
        }
      case 20: //bme680
        {
        Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 20, 0, String(MYNAME+String("_VOC")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        break;
        }

      #if defined(_CHECKAIRCON) 
        case 53: //HVAC time - this is the total time. Note that sensor pin is not used
         {
          bitWrite(flags,3,1);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 50, 0, String(MYNAME+String("_Total")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          
          break;
         }
      #endif

      #if defined(_CHECKHEAT) 
        case 53: //HVAC time - this is the total time. Note that sensor pin is not used
         {


          bitWrite(flags,3,1);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 50, 0, String(MYNAME+String("_Total")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          
          break;
         }

        case 54: //ac, gas valve
        {
          pinMode(_USERELAY, INPUT);
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 51, 0, String(MYNAME+String("_GAS")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          
          break;
        }
        case 55: //heat
        {
          //need to count how many heat zones have been added so far
          int16_t HZindex = Sensors.countSensors(55,myDevID); //returns a 1-based index. this will be the snsID

          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

          String heatName = String(MYNAME) + "_" + String(HEATZONE[HZindex]); //no HZindex-1 because we are using a 1-based index
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 55, HZindex, heatName.c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          #ifdef _USEMUX
          #else
            pinMode(HEATPINS[HZindex], INPUT);
          #endif
          break;
        }
      #endif


      #ifdef _CHECKAIRCON
        case 56: //aircon compressor
          {
            Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
            Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 56, 0, String(MYNAME+String("_comp")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          break;
          }
        case 57: //aircon fan
          {
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 57, 0, String(MYNAME+String("_fan")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

          break;
          }
          
      #endif
      case 70: //leak
        {
          #ifdef _USELEAK
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 70, 0, String(MYNAME+String("_leak")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          #endif
          break;
        }

      case 60: //battery
        {
          bitWrite(flags,1,0);
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 60, 0, String(MYNAME+String("_bat")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1

        break;
        }
      case 61: //battery percent
        {
          
          bitWrite(flags,3,1);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 61, 0, String(MYNAME+String("_bpct")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        break;
        }
      case 90: //Sleep info
        {
          bitWrite(flags,7,0); bitWrite(flags,3,1); bitWrite(flags,1,0);
          Prefs.SENSORIDS[i] = myDevID<<16 + stype<<8 + Sensors.countSensors(stype); //Sensors.countSensors(stype) returns a 1-based index, so no need to subtract 1
          Sensors.addSensor(ESP.getEfuseMac(), (uint32_t)WiFi.localIP(), 90, 0, String(MYNAME+String("_sleep")).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], flags, MYNAME);
        
        break;
        }

    }


  }




  #if defined(_CHECKHEAT) || defined(_CHECKAIRCON)
  //init hvachx 

  //count hvac sensors
  HVACSNSNUM=0;
  for (byte j=0;j<SENSORNUM;j++) if (Sensors[j].snsType>=50 && Sensors[j].snsType<60) HVACSNSNUM++;
  
  for (byte j=0;j<HVACSNSNUM;j++) {
    HVACHX[j].interval = 60*60; //seconds
    HVACHX[j].lastRead = 0;
    for (byte jj=0;jj<_HVACHXPNTS;jj++) HVACHX[j].values[jj]=0;
  }

  #if defined(_USEMUX) && defined(_CHECKHEAT)
    pinMode(HEATPINS[0],OUTPUT);
    pinMode(HEATPINS[1],OUTPUT);
    pinMode(HEATPINS[2],OUTPUT);
    pinMode(HEATPINS[3],OUTPUT);
    pinMode(HEATPINS[4],INPUT);
    digitalWrite(HEATPINS[0],LOW);
    digitalWrite(HEATPINS[1],LOW);
    digitalWrite(HEATPINS[2],LOW);
    digitalWrite(HEATPINS[3],LOW);

  #endif

  #if defined(_CHECKAIRCON)
    pinMode(ACPINS[0],OUTPUT);
    pinMode(ACPINS[1],OUTPUT);
    pinMode(ACPINS[2],INPUT);
    pinMode(ACPINS[3],INPUT);
    digitalWrite(ACPINS[2],LOW);
    digitalWrite(ACPINS[3],LOW);    
  #endif



#endif


}

int peak_to_peak(int pin, int ms) {
  
  //check n (samples) over ms milliseconds, then return the max-min value (peak to peak value) 

  if (ms==0) ms = 50; //50 ms is roughly 3 cycles of a 60 Hz sin wave
  
  int maxVal = 0;
  int minVal=6000;
  uint16_t buffer = 0;
  uint32_t t0;
  

  t0 = millis();

  while (millis()<=t0+ms) { 
    buffer = analogRead(pin);
    if (maxVal<buffer) maxVal = buffer;
    if (minVal>buffer) minVal = buffer;

  }
  

  return maxVal-minVal;

}



int8_t ReadData(struct SnsType *P, bool forceRead) {
  //return -1 if not my sensor, 0 if not time to read, 1 if read successful

  //is this my sensor?
  if (P->deviceIndex != Sensors.findMe()) return -1;

  //may need the index to the Prefs sensor
  int16_t prefs_index = Sensors.getPrefsIndex(P->snsType, P->snsID);


  //is it time to read?
  if (forceRead==false && !(P->timeRead==0 || P->timeRead>I.currentTime || P->timeRead + Prefs.SNS_INTERVAL_POLL[prefs_index] < I.currentTime || I.currentTime - P->timeRead >60*60*24 )) return 0;


  // Use I.currentTime instead of local time_t t variable
  byte nsamps; //only used for some sensors
  double val;
  bitWrite(P->Flags,0,0);

  double LastsnsValue = P->snsValue;

  
  switch (P->snsType) {
    case 1: //DHT temp
      {
        #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  (dht.readTemperature()*9/5+32);
      #endif
      
      break;
      }
    case 2: //DHT RH
      {
        #ifdef DHTTYPE
        //DHT Temp
        P->snsValue = dht.readHumidity();
      #endif
      
      break;
      }
    case 3: //soil
      {
        #ifdef _USESOILCAP
        //soil moisture v1.2
        val = analogRead(SOILPIN);
        //based on experimentation... this eq provides a scaled soil value where 0 to 100 corresponds to 450 to 800 range for soil saturation (higher is dryer. Note that water and air may be above 100 or below 0, respec
        val =  (int) ((-0.28571*val+228.5714)*100); //round value
        P->snsValue =  val/100;
      
        #endif

        #ifdef _USESOILRES
        //soil moisture by stainless steel probe (voltage out = 0 to Vcc)
        val=0;
        nsamps=100;

        digitalWrite(_USESOILRES, HIGH);
        delay(100); //wait X ms for reading to settle
        for (byte ii=0;ii<nsamps;ii++) {                  
          val += analogRead(SOILPIN);
          delay(1);
        }
          digitalWrite(_USESOILRES, LOW);
        val=val/nsamps;


        //convert val to voltage
        val = 3.3 * (val / _ADCRATE);

        //the chip I am using is a voltage divider with a 10K r1. 
        //equation for R2 is R2 = R1 * (V2/(V-v2))

        P->snsValue = (double) 10000 * (val/(3.3-val));
               
        
        #endif


        #ifdef _USESOILRESOLD
        //soil moisture by stainless steel wire (Resistance)        
        digitalWrite(_USESOILRES, HIGH);
        val = analogRead(SOILPIN);
        digitalWrite(_USESOILRES, LOW);
        //voltage divider, calculate soil resistance: Vsoil = 3.3 *r_soil / ( r_soil + r_fixed)
        //so R_soil = R_fixed * (3.3/Vsoil -1)
      

        #ifdef _USE32
          val = val * (3.3 / 4095); //12 bit
          P->snsValue =  (int) ((double) SOILRESISTANCE * (3.3/val -1)); //round value. 
        #endif
        #ifdef _USE8266
          val = val * (3.3 / _ADCRATE); //it's _ADCRATE because the value 1024 is overflow
          P->snsValue =  (int) ((double) SOILRESISTANCE * (3.3/val -1)); //round value. 
        #endif

        
        #endif
      
      break;
      }
    case 4: //AHT Temp
      {
        #ifdef _USEAHT
        //aht temperature
          val = aht.readTemperature();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (100*(val*9/5+32))/100; 
            #ifdef _DEBUG
              Serial.print(F("AHT Temperature...: "));
              Serial.print(P->snsValue);
              Serial.println(F("F"));
            #endif
          }
          else
          {
            #ifdef _DEBUG
              Serial.print(F("AHT Temperature Error"));
            #endif
          }
      #endif
      #ifdef _USEAHTADA
        //aht temperature
          sensors_event_t humidity, temperature;
          aht.getEvent(&humidity,&temperature);
          P->snsValue = (100*(temperature.temperature*9/5+32))/100; 
          

      #endif


      break;
      }
    case 5: //AHT RH
      {
      //aht humidity
        #ifdef _USEAHTADA
          //AHT
            sensors_event_t humidity, temperature;
            aht.getEvent(&humidity,&temperature);
            P->snsValue = (100*(humidity.relative_humidity))/100;            
        #endif
        #ifdef _USEAHT
          val = aht.readHumidity();
          if (val != AHTXX_ERROR) //AHTXX_ERROR = 255, library returns 255 if error occurs
          {
            P->snsValue = (val*100)/100; 
            #ifdef _DEBUG
              Serial.print(F("AHT HUmidity...: "));
              Serial.print(P->snsValue);
              Serial.println(F("%"));
            #endif
          }
          else
          {
            #ifdef _DEBUG
              Serial.print(F("AHT Humidity Error"));
            #endif
          }
      #endif
      
      break;
      }
    case 7: //dist
      {
        #ifdef _USEHCSR04
          #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
          digitalWrite(TRIGPIN, LOW);
          delayMicroseconds(2);
          //Now we'll activate the ultrasonic ability
          digitalWrite(TRIGPIN, HIGH);
          delayMicroseconds(10);
          digitalWrite(TRIGPIN, LOW);

          //Now we'll get the time it took, IN MICROSECONDS, for the beam to bounce back
          long duration = pulseIn(ECHOPIN, HIGH);

          //Now use duration to get distance in units specd b USONIC_DIV.
          //We divide by 2 because it took half the time to get there, and the other half to bounce back.
          P->snsValue = (duration / USONIC_DIV); 
        #endif
        #ifdef _USETFLUNA
          int16_t tempval;
          tflI2C.getData(tempval, tfAddr);
          if (tempval <= 0)           P->snsValue = -1000;
          else           P->snsValue = tempval; //in cm


        #endif
   
      break;
      }
    case 9: //BMP pres
      {
        #ifdef _USEBMP
         P->snsValue = bmp.readPressure()/100; //in hPa
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1000;
          } else {
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1009;
          }

          if (LAST_BAR_READ+60*60 < I.currentTime) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = I.currentTime;          
          }
        #endif

      #endif
      
      break;
      }
    case 10: //BMP temp
      {
        #ifdef _USEBMP
        P->snsValue = ( bmp.readTemperature()*9/5+32);
      #endif
      
      break;
      }
    case 11: //BMP alt
      {
        #ifdef _USEBMP
         P->snsValue = (bmp.readAltitude(1013.25)); //meters
      #endif
      
      break;
      }
    case 12: //make a prediction about weather
      {
        #ifdef _USEBARPRED
        /*rules: 
        3 rise of 10 in 3 hrs = gale
        2 rise of 6 in 3 hrs = strong winds
        1 rise of 1.1 and >1015 = poor weather
        -1 fall of 1.1 and <1009 = rain
        -2 fall of 4 and <1023 = rain
        -3 fall of 4 and <1009 = storm
        -4 fall of 6 and <1009 = strong storm
        -5 fall of 7 and <990 = very strong storm
        -6 fall of 10 and <1009 = gale
        -7 fall of 4 and fall of 8 past 12 hours and <1005 = severe tstorm
        -8 fall of 24 in past 24 hours = weather bomb
        //https://www.worldstormcentral.co/law%20of%20storms/secret%20law%20of%20storms.html
        */        
        //fall of >3 hPa in 3 hours and P<1009 = storm
        P->snsValue = 0;
        if (BAR_HX[2]>0) {
          if (BAR_HX[0]-BAR_HX[2] >= 1.1 && BAR_HX[0] >= 1015) {
            P->snsValue = 1;
            snprintf(WEATHER,22,"Poor Weather");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 6) {
            P->snsValue = 2;
            snprintf(WEATHER,22,"Strong Winds");
          }
          if (BAR_HX[0]-BAR_HX[2] >= 10) {
            P->snsValue = 3;        
            snprintf(WEATHER,22,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 1.1 && BAR_HX[0] <= 1009) {
            P->snsValue = -1;
            snprintf(WEATHER,22,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1023) {
            P->snsValue = -2;
            snprintf(WEATHER,22,"Rain");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[0] <= 1009) {
            P->snsValue = -3;
            snprintf(WEATHER,22,"Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 6 && BAR_HX[0] <= 1009) {
            P->snsValue = -4;
            snprintf(WEATHER,22,"Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 7 && BAR_HX[0] <= 990) {
            P->snsValue = -5;
            snprintf(WEATHER,22,"Very Strong Storm");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 10 && BAR_HX[0] <= 1009) {
            P->snsValue = -6;
            snprintf(WEATHER,22,"Gale");
          }
          if (BAR_HX[2]-BAR_HX[0] >= 4 && BAR_HX[11]-BAR_HX[0] >= 8 && BAR_HX[0] <= 1005) {
            P->snsValue = -7;
            snprintf(WEATHER,22,"TStorm");
          }
          if (BAR_HX[23]-BAR_HX[0] >= 24) {
            P->snsValue = -8;
            snprintf(WEATHER,22,"BOMB");
          }
        }
      #endif
      
      break;
      }
    case 13: //BME pres
      {
        #ifdef _USEBME
         P->snsValue = bme.readPressure(); //in Pa
        #ifdef _USEBARPRED
          //adjust critical values based on history, if available
          if (P->snsValue<1009 && BAR_HX[0] < P->snsValue  && BAR_HX[0] > BAR_HX[2] ) {
            //pressure is low, but rising
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1000;
          } else {
            Prefs.SNS_LIMIT_MIN[prefs_index] = 1009;
          }

          if (LAST_BAR_READ+60*60 < I.currentTime) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = I.currentTime;          
          }
        #endif
      #endif
      
      break;
      }
    case 14: //BMEtemp
      {
        #ifdef _USEBME
        P->snsValue = (( bme.readTemperature()*9/5+32) );
      #endif
      
      break;
      }
    case 15: //bme rh
      {
        #ifdef _USEBME
      
        P->snsValue = ( bme.readHumidity() );
      #endif
      
      break;
      }
    case 16: //BMEalt
      {
        #ifdef _USEBME
         P->snsValue = (bme.readAltitude(1013.25)); //meters

      #endif
      break;
      }
    #ifdef _USEBME680
    case 17: //bme680 temp
      {
        read_BME680();
      P->snsValue = (double) (( ((double) temperature/100) *9/5)+32); //degrees F
      break;
      }
    case 18: //bme680 humidity
      {
        read_BME680();
        P->snsValue = ((double) humidity/1000); //RH%
        break;
      }
    case 19: //bme680 air pressure
      {
        read_BME680();
      P->snsValue = ((double) pressure/100); //hPa
      
      break;
      }
    case 20: //bme680 gas
      {
        read_BME680();
      P->snsValue = (gas); //milliohms
      break;
      }
    #endif

    #if defined(_CHECKHEAT) 

      case 53: //total HVAC time        
        {
          if (P.countFlagged(-3,0b00000001,0b00000001,0)>0) P->snsValue += Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //number of minutes HVAC multizone systems were on

        #ifndef _USECALIBRATIONMODE
          //note that for heat and ac, the total time (case 53) accounts for slot 0 and the heat or cool elements take the following slots
          if (HVACHX[0].lastRead+HVACHX[0].interval <= P->LastReadTime) {
            pushDoubleArray(HVACHX[0].values,_HVACHXPNTS,P->snsValue);
            HVACHX[0].lastRead = P->LastReadTime;
          }
        #endif


        break;
        }

      case 54: //heat - gas valve
        {
          byte snspin = _USERELAY;

          //can use the mux, as long as _USERELAY is set correctly.
          #ifndef _USEMUX
            pinMode(snspin, INPUT);
          #endif


          //take n measurements, and average
          val=0;
          nsamps=1; //number of samples to average

          #ifdef _USEMUX
            //set the MUX channel to snspin
            digitalWrite(MUXPINS[0],bitRead(snspin,0));
            digitalWrite(MUXPINS[1],bitRead(snspin,1));
            digitalWrite(MUXPINS[2],bitRead(snspin,2));
            digitalWrite(MUXPINS[3],bitRead(snspin,3));

            wait_ms(50); //provide time for channel switch and charge capacitors

            //read values from the mux and find p2p
            val = peak_to_peak(MUXPINS[4],50); //50 ms is 3 clock cycles at 60 Hz
          #else
            //use the DIO pins directly
            val = peak_to_peak(snspin,50);
          #endif

          if (val > Prefs.SNS_LIMIT_MAX[prefs_index])           P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the system was on

          #ifndef _USECALIBRATIONMODE
            //note that for heat, the total time (case 50) accounts for slot 0 , gas  takes 1, zones take id + 1
            if (HVACHX[1].lastRead+HVACHX[1].interval <= P->LastReadTime) {
              pushDoubleArray(HVACHX[1].values,_HVACHXPNTS,P->snsValue);
              HVACHX[1].lastRead = P->LastReadTime;
            }
          #endif  
        break;
        }
    
      case 55: //heat
        {
        //take n measurements, and average
        val=0;
        nsamps=1; //number of samples to average
        byte snspin = HEATPINS[P->snsID];

        #ifdef _USEMUX
          //set the MUX channel to snspin
          digitalWrite(MUXPINS[0],bitRead(snspin,0));
          digitalWrite(MUXPINS[1],bitRead(snspin,1));
          digitalWrite(MUXPINS[2],bitRead(snspin,2));
          digitalWrite(MUXPINS[3],bitRead(snspin,3));

          wait_ms(50); //provide time for channel switch and charge capacitors

          //read values from the mux and find p2p
          val = peak_to_peak(MUXPINS[4],50); //50 ms is 3 clock cycles at 60 Hz
        #else
          //use the DIO pins directly
          val = peak_to_peak(snspin,50);
        #endif

        if (val > Prefs.SNS_LIMIT_MAX[prefs_index])           P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the system was on

            
        #ifndef _USECALIBRATIONMODE
          //note that for heat, the total time (case 50) accounts for slot 0 , gas  takes 1, so these take id + 1
          if (HVACHX[P->snsID+1].lastRead+HVACHX[P->snsID+1].interval <= P->LastReadTime) {
            pushDoubleArray(HVACHX[P->snsID+1].values,_HVACHXPNTS,P->snsValue);
            HVACHX[P->snsID+1].lastRead = P->LastReadTime;
          }
        #endif
        break;
        }
    #endif

    #if defined(_CHECKAIRCON)
      case 56: //aircon compressor
      {
        //assumes you are using a fan relay to switch on
        //if the fan is off, the NC pins of relay will be connected and I can read a digital high
        //if fan is on, pin will be low
        //turn on the voltage DIO to the compressor
        pinMode(ACPINS[2],OUTPUT);
        pinMode(ACPINS[0],INPUT);
        digitalWrite(ACPINS[2],HIGH);
        delay(10);
        if (digitalRead(ACPINS[0]) == LOW)           P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the ac was on
        digitalWrite(ACPINS[2],LOW);


        if (HVACHX[1].lastRead+HVACHX[1].interval <= P->LastReadTime) {
          pushDoubleArray(HVACHX[1].values,_HVACHXPNTS,P->snsValue);
          HVACHX[1].lastRead = P->LastReadTime;
        }

        break;
      }
      case 57: //aircon fan
      {
        //assumes you are using a fan relay to switch on
        //if the fan is off, the NC pins of relay will be connected and I can read a digital high
        //turn on the voltage DIO to the fan
        pinMode(ACPINS[3],OUTPUT);
        pinMode(ACPINS[1],INPUT);
        digitalWrite(ACPINS[3],HIGH);
        delay(10);
        if (digitalRead(ACPINS[1]) == LOW)           P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the ac was on
        digitalWrite(ACPINS[3],LOW);



        if (HVACHX[2].lastRead+HVACHX[2].interval <= P->LastReadTime) {
          pushDoubleArray(HVACHX[2].values,_HVACHXPNTS,P->snsValue);
          HVACHX[2].lastRead = P->LastReadTime;
        }

        break;
      }
    #endif

    case 70: //Leak detection
      {
        #ifdef _USELEAK
        digitalWrite(_LEAKDIO, HIGH);
        if (digitalRead(_LEAKPIN)==HIGH) P->snsValue =1;
        else P->snsValue =0;
        digitalWrite(_LEAKDIO, LOW);

      #endif

      break;
      }
    #if defined(_USELIBATTERY) || defined(_USESLABATTERY)

      case 60: // battery
        {
          #ifdef _USELIBATTERY
          //note that esp32 ranges 0 to 4095, while 8266 is 1023. This is set in header.hpp
          P->snsValue = readVoltageDivider( 1,1,  _USELIBATTERY, 3.3, 3); //if R1=R2 then the divider is 50%
          
        #endif
        #ifdef _USESLABATTERY
          P->snsValue = readVoltageDivider( 100,  6,  _USESLABATTERY, 1, 3); //esp12e ADC maxes at 1 volt, and can sub the lowest common denominator of R1 and R2 rather than full values
          
        #endif


        break;
        }
      case 61:
        {
          //_USEBATPCNT
        #ifdef _USELIBATTERY
          P->snsValue = readVoltageDivider( 1,1,  _USELIBATTERY, 3.3, 3); //if R1=R2 then the divider is 50%

          #define VOLTAGETABLE 21
          static float BAT_VOLT[VOLTAGETABLE] = {4.2,4.15,4.11,4.08,4.02,3.98,3.95,3.91,3.87,3.85,3.84,3.82,3.8,3.79,3.77,3.75,3.73,3.71,3.69,3.61,3.27};
          static byte BAT_PCNT[VOLTAGETABLE] = {100,95,90,85,80,75,70,65,60,55,50,45,40,35,30,25,20,15,10,5,1};

          for (byte jj=0;jj<VOLTAGETABLE;jj++) {
            if (P->snsValue> BAT_VOLT[jj]) {
              P->snsValue = BAT_PCNT[jj];
              break;
            } 
          }


        #endif

        #ifdef _USESLABATTERY
          P->snsValue = readVoltageDivider( 100,  6,  _USESLABATTERY, 1, 3); //esp12e ADC maxes at 1 volt, and can sub the lowest common denominator of R1 and R2 rather than full values

          #define VOLTAGETABLE 11
          static float BAT_VOLT[VOLTAGETABLE] = {12.89,12.78,12.65,12.51,12.41,12.23,12.11,11.96,11.81,11.7,11.63};
          static byte BAT_PCNT[VOLTAGETABLE] = {100,90,80,70,60,50,40,30,20,10,0};
          for (byte jj=0;jj<VOLTAGETABLE;jj++) {
            if (P->snsValue>= BAT_VOLT[jj]) {
              P->snsValue = BAT_PCNT[jj];
              break;
            } 
          }

        #endif

        break;
        }
    #endif
    case 90:
      {
        //don't do anything here
      //I'm set manually!
      break;
      }
    
  }
  checkSensorValFlag(P); //sets isFlagged
  P->timeRead = I.currentTime; //localtime

  #ifdef _WEBCHART
    for (byte k=0;k<_WEBCHART;k++) {
      if (SensorCharts[k].snsType == P->snsType) {
        if (SensorCharts[k].lastRead+SensorCharts[k].interval <= P->timeRead) {
          SensorCharts[k].lastRead = P->timeRead;
          //byte tmpval = (byte) ((double) (P->snsValue + SensorCharts[k].offset) / SensorCharts[k].multiplier );

          pushDoubleArray(SensorCharts[k].values,_NUMWEBCHARTPNTS,P->snsValue);
        }
      }

    }
  #endif

  


return 1;
}



bool checkSensorValFlag(struct SnsType *P) {
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, flag status changed and I have not sent data yet
  
  bool lastflag = bitRead(P->Flags,0);
  bool thisflag = false;

  int16_t prefs_index = Sensors.getPrefsIndex(P->snsType, P->snsID);

  double limitUpper = (prefs_index>=0) ? Prefs.SNS_LIMIT_MAX[prefs_index] : -9999999;
  double limitLower = (prefs_index>=0) ? Prefs.SNS_LIMIT_MIN[prefs_index] : 9999999;

  if (P->snsType>=50 && P->snsType<60) { //HVAC is a special case. 50 = total time, 51 = gas, 55 = hydronic valve, 56 - ac 57 = fan
    lastflag = bitRead(P->Flags,0); //this is the last flag status
    // Note: LastsnsValue field removed in new SnsType structure
    // HVAC logic simplified - just check current value against limits
    

    
    if (P->snsValue > limitUpper || P->snsValue < limitLower) { //currently flagged
      bitWrite(P->Flags,0,1); //currently flagged
      bitWrite(P->Flags,5,1); //value is high
      if (lastflag) {
        bitWrite(P->Flags,6,0); //no change in flag        
      } else {
        bitWrite(P->Flags,6,1); //change in flag status
      }
      return true; //flagged
    } else { //currently NOT flagged
      bitWrite(P->Flags,0,0); //currently not flagged
      bitWrite(P->Flags,5,0); //irrelevant
      if (lastflag) {
        bitWrite(P->Flags,6,1); // changed from flagged to NOT flagged
      } else {
        bitWrite(P->Flags,6,0); //no change (was not flagged, still is not flagged)
      }
        return false; //not flagged
    }
  }


  if (P->snsValue>limitUpper || P->snsValue<limitLower) {
    thisflag = true;
    bitWrite(P->Flags,0,1);

    //is it too high? write bit 5
    if (P->snsValue>limitUpper) bitWrite(P->Flags,5,1);
    else bitWrite(P->Flags,5,0);
  } 

  //now check for changes...  
  if (lastflag!=thisflag) {
    bitWrite(P->Flags,6,1); //change detected
    
  } else {
    bitWrite(P->Flags,6,0);
  }
  
  return bitRead(P->Flags,0);

}


float readVoltageDivider(float R1, float R2, uint8_t snsPin, float Vm, byte avgN) {
  /*
    R1 is first resistor
    R2 is second resistor (which we are measuring voltage across)
    snsPin is the pin to measure the voltage, Vo
    ADCRATE is the max ADCRATE
    Vm is the ADC max voltage (1 for esp12e, 3.3 for NodeMCU, 3.3 for ESP)
    avgN is the number of times to avg
    */

  float Vo = 0;

  for (byte i=0;i<avgN;i++) {
    Vo += (float) Vm * ((R2+R1)/R2) * analogRead(snsPin)/_ADCRATE;
  }

  return  Vo/avgN;
}


#ifdef _USEBME680
void read_BME680() {
  
  uint32_t m = millis();
  
  if (last_BME680>m || m-last_BME680>500)   BME680.getSensorData(temperature, humidity, pressure, gas);  // Get readings
  else return;
  last_BME680=m;
}
#endif

#ifdef _USESSD1306

void redrawOled() {


  oled.clear();
  oled.setCursor(0,0);
  oled.printf("[%u] %d:%02d\n",WiFi.localIP()[3],hour(),minute());

  byte j=0;
  while (j<Sensors.getNumSensors()) {
    for (byte jj=0;jj<2;jj++) {
      SnsType* s = Sensors.getSensorBySnsIndex(j);
      if (s) {
        oled.printf("%d.%d=%d%s",s->snsType,s->snsID,(int) s->snsValue, (bitRead(s->Flags,0)==1)?"! ":" ");
      }
      j++;
    }
    oled.println("");    
  }

  return;    
}

#endif


