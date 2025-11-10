#include <sensors.hpp>

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
58 - leak yes/no
60 -  battery power
61 - battery %
99 = any numerical value

*/

#if defined(_CHECKAIRCON) || defined(_CHECKHEAT) 
uint8_t HVACSNSNUM = 0;
#endif


SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored

#ifdef _USETFLUNA
TFLunaType LocalTF;
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




//FUNCTIONS
uint8_t countFlagged(int snsType, uint8_t flagsthatmatter, uint8_t flagsettings, uint32_t MoreRecentThan) {
  //count sensors of type snstype [default is 0, meaning all sensortypes], flags that matter [default is 00000011 - which means that I only care about RMB1 and RMB2], what the flags should be [default is 00000011, which means I am looking for sensors that are flagged and monitored], and last logged more recently than this time [default is 0]
  //special use case... is snsType == -1 then this is a special case where we will look for types 1, 4, 10, 14, 17, 3, 61 [temperatures from various sensors, battery%]
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified

byte count =0;
int snsArr[10] = {0}; //this is for special cases

if (snsType == -1) { //critical sensors, all types of sensors that raise a critical alert when flagged (so not heater on, ac on, etc)
snsArr[0] = 1; 
snsArr[1] = 4;
snsArr[2] = 10;
snsArr[3] = 14;
snsArr[4] = 17;
snsArr[5] = 3;
snsArr[6] = 61;
snsArr[7] = 70;
snsArr[8] = -1;
snsArr[9] = -1;
} 

if (snsType == -2) { //temperature sensors
snsArr[0] = 1; 
snsArr[1] = 4;
snsArr[2] = 10;
snsArr[3] = 14;
snsArr[4] = 17;
snsArr[5] = -1;
snsArr[6] = -1;
snsArr[7] = -1;
snsArr[8] = -1;
snsArr[9] = -1;
} 

if (snsType == -3) { //hvac sensors (these are non critical sensors)
snsArr[0] = 55; 
snsArr[1] = 56;
snsArr[2] = 57;
snsArr[3] = -1; 
snsArr[4] = -1;
snsArr[5] = -1;
snsArr[6] = -1;
snsArr[7] = -1;
snsArr[8] = -1;
snsArr[9] = -1;
} 



  for (byte j = 0; j<SENSORNUM; j++) {
    if (snsType==0 || (snsType<0 && inArray(snsArr,10,Sensors[j].snsType)>=0) || Sensors[j].snsType == snsType) 
      if ((Sensors[j].Flags & flagsthatmatter ) == (flagsthatmatter & flagsettings)) {
        if (Sensors[j].LastReadTime> MoreRecentThan) count++;
      }
  }

  return count;
}



void setupSensors() {
/*
          Sensors[i].snsPin=_USEDHT; //this is the pin to read/write from - not always used
          snprintf(Sensors[i].snsName,31,"%s_T", ARDNAME); //sensor name
            Sensors[i].limitUpper = 88; //upper limit of normal
            Sensors[i].limitLower = 20; //lower limit of normal
          Sensors[i].PollingInt=120; //how often to check this sensor, in seconds
          Sensors[i].SendingInt=2*60; //how often to send data from this sensor. nte that value will be sent regardless if flag status changes, so can set to arbitrarily high value if you only want to send when flag status changes
*/

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
uint16_t  sc_interval; 



  for (byte i=0;i<SENSORNUM;i++) {
    Sensors[i].snsType=SENSORTYPES[i];
    Sensors[i].snsID=find_sensor_count(Sensors[i].snsType); 


    Sensors[i].Flags = 0;
    //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed status since prior read, RMB7 = this sensor is monitored - alert if no updates received within time limit specified
    bitWrite(Sensors[i].Flags,7,1); //default sensors to monitored level
    if (bitRead(MONITORED_SNS,i)) bitWrite(Sensors[i].Flags,1,1);
    else bitWrite(Sensors[i].Flags,1,0);
    
    if (bitRead(OUTSIDE_SNS,i)) bitWrite(Sensors[i].Flags,2,1);
    else bitWrite(Sensors[i].Flags,2,0);

    switch (Sensors[i].snsType) {
      case 1: //DHT temp
      {
        //sc_multiplier = 1;
        //sc_offset=100;
        sc_interval=60*30;//seconds 

        #ifdef DHTTYPE
          Sensors[i].snsPin=_USEDHT;
          snprintf(Sensors[i].snsName,31,"%s_T", ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 88;
            Sensors[i].limitLower = 20;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 60;
          }
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=2*60;          
        #endif
        break;
      }
      case 2: //DHT RH
       {

        //sc_multiplier = 1;
        //sc_offset=0;
        sc_interval=60*30;//seconds 
//        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        #ifdef DHTTYPE
          Sensors[i].snsPin=_USEDHT;
          snprintf(Sensors[i].snsName,31,"%s_RH",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 95;
            Sensors[i].limitLower = 10;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
            bitWrite(Sensors[i].Flags,1,0); //not monitored
          }
          Sensors[i].PollingInt=2*60;
          Sensors[i].SendingInt=5*60;
        #endif
        break;
       }
      case 3: //soil
        {
          //sc_multiplier = 100; //divide by 100
        //sc_offset=0;
        sc_interval=60*30;//seconds 
        #ifdef _USESOILCAP
          Sensors[i].snsPin=SOILPIN; //input, usually A0
          snprintf(Sensors[i].snsName,31,"%s_soil",ARDNAME);
          Sensors[i].limitUpper = 290;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=600;
        #endif
        #ifdef _USESOILRES
          pinMode(_USESOILRES,OUTPUT);  

          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soilR",ARDNAME);
          Sensors[i].limitUpper = SOILR_MAX;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=60*60;
        #endif

        break;
        }
      case 4: //AHT temp
        {
          //sc_multiplier = 1;
        //sc_offset=100;
        sc_interval=60*30;//seconds 

        #if defined(_USEAHT) || defined(_USEAHTADA)
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_T",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 88;
            Sensors[i].limitLower = 20;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 60;
          }
          Sensors[i].PollingInt=5*60;
          Sensors[i].SendingInt=5*60;
        #endif
        break;
        }
      case 5: //aht rh
        {
          //sc_multiplier = 1;
        //sc_offset=0;
        sc_interval=60*30;//seconds 
       // bitWrite(Sensors[i].Flags,7,0); //not a "critically monitored" sensor, ie do not alarm if sensor expires

        #if defined(_USEAHT) || defined(_USEAHTADA)
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_RH",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 95;
            Sensors[i].limitLower = 10;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
            // bitWrite(Sensors[i].Flags,1,0); //not monitored
          }
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
        }

      case 7: //dist
        {
          //sc_multiplier = 1;
        //sc_offset=50;
        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        sc_interval=60*30;//seconds 
        Sensors[i].snsPin=0; //not used
        snprintf(Sensors[i].snsName,31,"%s_Dist",ARDNAME);
        Sensors[i].limitUpper = 2000;
        Sensors[i].limitLower = -1001;
        Sensors[i].PollingInt=60;
        Sensors[i].SendingInt=120;
        break;
        }
      case 9: //BMP pres
        {
          //sc_multiplier = .5; //[multiply by 2]
        //sc_offset=-950; //now range is <100, so multiplier of .5 is ok
        //bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        sc_interval=60*60;//seconds 
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
        }
      case 10: //BMP temp
        {
          //sc_multiplier = 1;
        //sc_offset=50;
        sc_interval=60*30;//seconds 
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMP_t",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 88;
            Sensors[i].limitLower = 25;
            bitWrite(Sensors[i].Flags,1,0); //not monitored
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 60;
            bitWrite(Sensors[i].Flags,1,0); //not monitored
          }
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
        }
      case 11: //BMP alt
        {
          //sc_multiplier = 1;
        //sc_offset=0;
        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        sc_interval=60*30;//seconds 
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60000;
        Sensors[i].SendingInt=60000;
        break;
        }
      case 12: //Bar prediction
        {
          //sc_multiplier = 1;
        //sc_offset=10; //to eliminate neg numbs
        sc_interval=60*30;//seconds 
        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report

        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_Pred",ARDNAME);
        Sensors[i].limitUpper = 0;
        Sensors[i].limitLower = 0; //anything over 0 is an alarm
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        bitWrite(Sensors[i].Flags,3,1); //calculated
        bitWrite(Sensors[i].Flags,4,1); //predictive
        break;
        }
      case 13: //BME pres
        {
          //sc_multiplier = .5;
        //sc_offset=-950; //now range is <100, so multiply by 2
       // bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        sc_interval=60*60;//seconds 
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
        }
      case 14: //BMEtemp
        {
          //sc_multiplier = 1;
        //sc_offset=100;
        sc_interval=60*30;//seconds 
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMEt",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 88;
            Sensors[i].limitLower = 25;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 60;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
        }
      case 15: //bme rh
        {
          //sc_multiplier = 1;
        //sc_offset=0;
        sc_interval=60*30;//seconds 
        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report

        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMErh",ARDNAME);
        if (bitRead(OUTSIDE_SNS,i)) {
          Sensors[i].limitUpper = 95;
          Sensors[i].limitLower = 10;
        }
        else {
          Sensors[i].limitUpper = 65;
          Sensors[i].limitLower = 25;
          bitWrite(Sensors[i].Flags,1,0); //not monitored
        }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
        }
      case 16: //bme alt
        {
          //sc_multiplier = 1;
        //sc_offset=0;
        sc_interval=60*30;//seconds 
        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report

        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=15*60*60;
        Sensors[i].SendingInt=15*60*60;
        break;
        }
      case 17: //bme680
        {
          //sc_multiplier = 1;
        //sc_offset=50;
        sc_interval=60*30;//seconds 
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_T",ARDNAME);
        if (bitRead(OUTSIDE_SNS,i)) {
          Sensors[i].limitUpper = 88;
          Sensors[i].limitLower = 25;
        }
        else {
          Sensors[i].limitUpper = 80;
          Sensors[i].limitLower = 60;
        }
        Sensors[i].PollingInt=15*60;
        Sensors[i].SendingInt=15*60;
        break;
        }
      case 18: //bme680
        {

             //sc_multiplier = 1;
        //sc_offset=0;
        sc_interval=60*30;//seconds 
        bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report

        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_RH",ARDNAME);
        if (bitRead(OUTSIDE_SNS,i)) {
          Sensors[i].limitUpper = 95;
          Sensors[i].limitLower = 10;
        }
        else {
          Sensors[i].limitUpper = 65;
          Sensors[i].limitLower = 25;
          bitWrite(Sensors[i].Flags,1,0); //not monitored
        }
        Sensors[i].PollingInt=15*60;
        Sensors[i].SendingInt=15*60;
        break;
        }
      case 19: //bme680
        {
          //sc_multiplier = .5;
        //sc_offset=-950; //now range is <100, so multiply by 2
        sc_interval=60*60;//seconds 
     //   bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1020;
        Sensors[i].limitLower = 1012;
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        break;
        }
      case 20: //bme680
        {
          //sc_multiplier = 1;
        //sc_offset=00;
        sc_interval=60*30;//seconds 

        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_VOC",ARDNAME);
        Sensors[i].limitUpper = 1000;
        Sensors[i].limitLower = 50;
        Sensors[i].PollingInt=1*60;
        Sensors[i].SendingInt=1*60;
        break;
        }

      #if defined(_CHECKAIRCON) 
        case 50: //HVAC time - this is the total time. Note that sensor pin is not used
         {

          sc_interval=60*30;//seconds 

          snprintf(Sensors[i].snsName,31,"%s_Total",ARDNAME);
      
          Sensors[i].limitUpper = 1440; //maximum is 24*60 minutes, which is one day (essentially upper and lower limit are not used here)
          Sensors[i].limitLower = -1;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=300; 
          bitWrite(Sensors[i].Flags,3,1); //calculated
          
          break;
         }
      #endif

      #if defined(_CHECKHEAT) 
        case 50: //HVAC time - this is the total time. Note that sensor pin is not used
         {

          sc_interval=60*30;//seconds 

          snprintf(Sensors[i].snsName,31,"%s_Total",ARDNAME);
          /*
          #ifdef _USEMUX
            
          #else
            Sensors[i].snsPin=DIOPINS[Sensors[i].snsID-1];
            pinMode(Sensors[i].snsPin, INPUT);
          #endif
          */
          Sensors[i].limitUpper = 1440; //maximum is 24*60 minutes, which is one day (essentially upper and lower limit are not used here)
          Sensors[i].limitLower = -1;
          Sensors[i].PollingInt=60;
          Sensors[i].SendingInt=300; 
          bitWrite(Sensors[i].Flags,3,1); //calculated
          
          break;
         }

        case 51: //heat, gas valve
        {
          //sc_multiplier = 4096/256;
        //sc_offset=0;
          sc_interval=60*30;//seconds 

          snprintf(Sensors[i].snsName,31,"%s_GAS",ARDNAME);
          #ifdef _USEMUX
            Sensors[i].snsPin=0; //the DIO configuration to select this channel            
          #else
            //undefined. gas is not measured if notusing mux
          #endif
          Sensors[i].limitUpper = 100; //this is the difference needed in the analog read of the induction sensor to decide if device is powered. Here the units are in adc units
          Sensors[i].limitLower = -1;
          Sensors[i].PollingInt=30; //short poll int given that gas may not be on often 
          Sensors[i].SendingInt=600; 
          break;
        }
        case 55: //heat
        {
          //sc_multiplier = 4096/256;
        //sc_offset=0;
        sc_interval=60*30;//seconds 

          snprintf(Sensors[i].snsName,31,"%s_%s",ARDNAME,HEATZONE[Sensors[i].snsID-1]);
          #ifdef _USEMUX
            Sensors[i].snsPin=Sensors[i].snsID; //the DIO configuration to select this channel. For heat zones, will be 1 - zone number                        
          #else
            Sensors[i].snsPin=DIOPINS[Sensors[i].snsID-1];
            pinMode(Sensors[i].snsPin, INPUT);
          #endif
          Sensors[i].limitUpper = 100; //this is the difference needed in the analog read of the induction sensor to decide if device is powered. Here the units are in adc units
          Sensors[i].limitLower = -1;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=600; 
          break;
        }
      #endif


      #ifdef _CHECKAIRCON
        case 56: //aircon compressor
          {
            //sc_multiplier = 4096/256;
          //sc_offset=0;
          sc_interval=60*30;//seconds 
          Sensors[i].snsPin=DIOPINS[2];
          pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_comp",ARDNAME);
          Sensors[i].limitUpper = 700;
          Sensors[i].limitLower = -1;
          Sensors[i].PollingInt=30;
          Sensors[i].SendingInt=300;
          break;
          }
        case 57: //aircon fan
          {
            //sc_multiplier = 4096/256;
          //sc_offset=0;
          sc_interval=60*30;//seconds 
          Sensors[i].snsPin=DIOPINS[3];
          pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_fan",ARDNAME);
          Sensors[i].limitUpper = 700;
          Sensors[i].limitLower = -1;
          Sensors[i].PollingInt=30;
          Sensors[i].SendingInt=300;
          break;
          }
          
      #endif
      case 70: //leak
        {
          #ifdef _USELEAK
          sc_interval=60*60;//seconds 
          Sensors[i].snsPin=_LEAKPIN;
          pinMode(Sensors[i].snsPin,INPUT);
          pinMode(_LEAKDIO,OUTPUT);
          digitalWrite(_LEAKDIO, LOW);
          snprintf(Sensors[i].snsName,31,"%s_leak",ARDNAME);
          Sensors[i].limitUpper = 0.5;
          Sensors[i].limitLower = -0.5;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
          #endif
          break;
        }

      case 60: //battery
        {
          #ifdef _USELIBATTERY
          //sc_multiplier = .01;
          //sc_offset=-3.1;
          sc_interval=60*30;//seconds 
          Sensors[i].snsPin=_USELIBATTERY;
          pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_bat",ARDNAME);
          Sensors[i].limitUpper = 4.3;
          Sensors[i].limitLower = 3.7;
          Sensors[i].PollingInt=1*60;
          Sensors[i].SendingInt=5*60;
          bitWrite(Sensors[i].Flags,1,0); //not monitored

          
        #endif
        #ifdef _USESLABATTERY
          //sc_multiplier = .01;
          //sc_offset=-3.1;
          sc_interval=60*30;//seconds 
          Sensors[i].snsPin=_USESLABATTERY;
          pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_bat",ARDNAME);
          Sensors[i].limitUpper = 12.89;
          Sensors[i].limitLower = 12.23;
          Sensors[i].PollingInt=60*60;
          Sensors[i].SendingInt=60*60;
          bitWrite(Sensors[i].Flags,1,0); //not monitored
        #endif

        break;
        }
      case 61: //battery percent
        {
          #ifdef _USELIBATTERY
          //sc_multiplier = 1;
          //sc_offset=0;
          sc_interval=60*30;//seconds 

          Sensors[i].snsPin=_USELIBATTERY;
          pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_bpct",ARDNAME);
          Sensors[i].limitUpper = 105;
          Sensors[i].limitLower = 10;
          Sensors[i].PollingInt=1*60;
          Sensors[i].SendingInt=5*60;
          bitWrite(Sensors[i].Flags,3,1); //calculated

        #endif
        #ifdef _USESLABATTERY
          //sc_multiplier = 1;
          //sc_offset=0;
          sc_interval=60*60;//seconds 

          Sensors[i].snsPin=_USESLABATTERY;
          pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_bpct",ARDNAME);
          Sensors[i].limitUpper = 100;
          Sensors[i].limitLower = 50;
          Sensors[i].PollingInt=60*60;
          Sensors[i].SendingInt=60*60;
          bitWrite(Sensors[i].Flags,3,1); //calculated

        #endif
        break;
        }
      case 90: //Sleep info
        {
          sc_interval=60*30;//seconds 
          bitWrite(Sensors[i].Flags,7,0); //not a "monitored" sensor, ie do not alarm if sensor fails to report

          Sensors[i].snsPin=0;
          //pinMode(Sensors[i].snsPin, INPUT);
          snprintf(Sensors[i].snsName,31,"%s_sleep",ARDNAME);
          Sensors[i].limitUpper = 14400;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=10*60; //these don't matter
          Sensors[i].SendingInt=10*60; //these don't matter
          bitWrite(Sensors[i].Flags,3,1); //calculated
          bitWrite(Sensors[i].Flags,1,0); //not monitored
        
        break;
        }

    }

        
    Sensors[i].snsValue=0;
    Sensors[i].LastReadTime=0;
    Sensors[i].LastSendTime=0;  
    #ifdef _WEBCHART
      for (byte j=0;j<_WEBCHART;j++) {
        if (Sensors[i].snsType==SENSORS_TO_CHART[j]) {
          SensorCharts[j].snsType = Sensors[i].snsType;
          SensorCharts[j].snsNum = i;
          //SensorCharts[j].offset = sc_offset;
          //SensorCharts[j].multiplier = sc_multiplier;
          for (byte k=0; k<_NUMWEBCHARTPNTS; k++)     SensorCharts[j].values[k] = 0;
          SensorCharts[j].interval = sc_interval;
          SensorCharts[j].lastRead = 0;
        }
      }
    #endif

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
    pinMode(DIOPINS[2],OUTPUT);
    pinMode(DIOPINS[3],OUTPUT);
    pinMode(DIOPINS[0],INPUT);
    pinMode(DIOPINS[1],INPUT);
    digitalWrite(DIOPINS[2],LOW);
    digitalWrite(DIOPINS[3],LOW);    
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



bool ReadData(struct SensorVal *P) {
  
  time_t t=now();
  byte nsamps; //only used for some sensors
  double val;
  bitWrite(P->Flags,0,0);

  P->LastsnsValue = P->snsValue;
  
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
        val = analogRead(P->snsPin);
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
          val += analogRead(P->snsPin);
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
        val = analogRead(P->snsPin);
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
          if (tflI2C.getData(tempval, _USETFLUNA)) {
            if (tempval <= 0)           P->snsValue = -1000;
            else           P->snsValue = tempval; //in cm
          } else {
            P->snsValue = -5000; //failed
          }


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
            P->limitLower = 1000;
          } else {
            P->limitLower = 1009;
          }

          if (LAST_BAR_READ+60*60 < t) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = t;          
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
            P->limitLower = 1000;
          } else {
            P->limitLower = 1009;
          }

          if (LAST_BAR_READ+60*60 < t) {
            pushDoubleArray(BAR_HX,24,P->snsValue);
            LAST_BAR_READ = t;          
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

      case 50: //total HVAC time        
        {
          if (countFlagged(-3,B00000001,B00000001,0)>0) P->snsValue += P->PollingInt/60; //number of minutes HVAC multizone systems were on

        #ifndef _USECALIBRATIONMODE
          //note that for heat and ac, the total time (case 50) accounts for slot 0 and the heat or cool elements take the following slots
          if (HVACHX[0].lastRead+HVACHX[0].interval <= P->LastReadTime) {
            pushDoubleArray(HVACHX[0].values,_HVACHXPNTS,P->snsValue);
            HVACHX[0].lastRead = P->LastReadTime;
          }
        #endif


        break;
        }

    #ifdef _USEMUX
      case 51: //heat - gas valve
        {
          //gas only measured if using mux
          /*
          DIOPINS were initialized in setup...
          DIOPINS[0] - mux DIO selector 0
          DIOPINS[1] - mux DIO selector 1
          DIOPINS[2] - mux DIO selector 2
          DIOPINS[3] - mux DIO selector 3
          DIOPINS[4] - MUX read out
          P->snsPin //this is the DIO settings for the mux
          */

         //select MUX


          //take n measurements, and average
          val=0;
          nsamps=1; //number of samples to average

          //set the MUX channel
          byte bitval = 0;
          #ifdef _DEBUG
            Serial.print ("gas byte array: ");
            #endif
          for (byte j=0;j<4;j++) {
            bitval = bitRead(P->snsPin,j);
            #ifdef _DEBUG
            Serial.print (bitval);
            #endif
            if (bitval==0) digitalWrite(DIOPINS[j],LOW);
            else digitalWrite(DIOPINS[j],HIGH);
          }
          #ifdef _DEBUG
            Serial.println (" was the bit array of chans 1-4.");
            #endif

          wait_ms(50); //provide time for channel switch and charge capacitors

            val = peak_to_peak(DIOPINS[4],50); 

//          for (byte j=0;j<nsamps;j++) {
  //          val += peak_to_peak(DIOPINS[4],50);
    //      }
      //    val = val/nsamps; //average
           #ifdef _DEBUG
            Serial.printf ("%s reading: %d\n", P->snsName, (int) val);
            #endif

          if (val > P->limitUpper)           P->snsValue += (double) P->PollingInt/60; //snsvalue is the number of minutes the system was on

          #ifndef _USECALIBRATIONMODE
            //note that for heat, the total time (case 50) accounts for slot 0 
            if (HVACHX[1].lastRead+HVACHX[1].interval <= P->LastReadTime) {
              pushDoubleArray(HVACHX[1].values,_HVACHXPNTS,P->snsValue);
              HVACHX[1].lastRead = P->LastReadTime;
            }
          #endif  
        break;
        }
      #endif
    
      case 55: //heat
        {
        //take n measurements, and average
        val=0;
        nsamps=1; //number of samples to average

        #ifdef _USEMUX
          //set the MUX channel
          byte bitval = 0;

          #ifdef _DEBUG
            Serial.printf ("%s byte array: ", P->snsName);
            #endif
          for (byte j=0;j<4;j++) {
            bitval = bitRead(P->snsPin,j);
            #ifdef _DEBUG
            Serial.print (bitval);
            #endif
            if (bitval==0) digitalWrite(DIOPINS[j],LOW);
            else digitalWrite(DIOPINS[j],HIGH);
          }
          #ifdef _DEBUG
            Serial.println (" was the bit array of chans 1-4.");
            #endif

          wait_ms(50); //provide time for channel switch and charge capacitors

        val = peak_to_peak(DIOPINS[4],50); //50 ms is 3 clock cycles. 
        
//          for (byte j=0;j<nsamps;j++) {
  //          val += peak_to_peak(DIOPINS[4],50); //50 ms is 3 clock cycles
    //      }
      //    val = val/nsamps; //average

           #ifdef _DEBUG
            Serial.printf ("%s reading: %d\n", P->snsName, (int) val);
            #endif
        

          if (val > P->limitUpper)           P->snsValue += P->PollingInt/60; //snsvalue is the number of minutes the system was on
          
          #ifndef _USECALIBRATIONMODE
            //note that for heat, the total time (case 50) accounts for slot 0 , gas (case 51) takes 1, so these take id + 1
            if (HVACHX[P->snsID+1].lastRead+HVACHX[P->snsID+1].interval <= P->LastReadTime) {
              pushDoubleArray(HVACHX[P->snsID+1].values,_HVACHXPNTS,P->snsValue);
              HVACHX[P->snsID+1].lastRead = P->LastReadTime;
            }
          #endif
        #else

          for (byte j=0;j<nsamps;j++) {
            val += peak_to_peak(P->snsPin,50);
          }
          val = val/nsamps; //average
          
          if (val > P->limitUpper)           P->snsValue += P->PollingInt/60; //snsvalue is the number of minutes the system was on

          #ifndef _USECALIBRATIONMODE
            //note that for heat, the total time (case 50) accounts for slot 0 and the heat elements take the following slots
            if (HVACHX[P->snsID].lastRead+HVACHX[P->snsID].interval <= P->LastReadTime) {
              pushDoubleArray(HVACHX[P->snsID].values,_HVACHXPNTS,P->snsValue);
              HVACHX[P->snsID].lastRead = P->LastReadTime;
            }
          #endif
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
        pinMode(DIOPINS[2],OUTPUT);
        pinMode(DIOPINS[0],INPUT);
        digitalWrite(DIOPINS[2],HIGH);
        delay(10);
        if (digitalRead(DIOPINS[0]) == LOW)           P->snsValue += (double) P->PollingInt/60; //snsvalue is the number of minutes the ac was on
        digitalWrite(DIOPINS[2],LOW);


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
        pinMode(DIOPINS[3],OUTPUT);
        pinMode(DIOPINS[1],INPUT);
        digitalWrite(DIOPINS[3],HIGH);
        delay(10);
        if (digitalRead(DIOPINS[1]) == LOW)           P->snsValue += (double) P->PollingInt/60; //snsvalue is the number of minutes the ac was on
        digitalWrite(DIOPINS[3],LOW);



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
          P->snsValue = readVoltageDivider( 1,1,  P->snsPin, 3.3, 3); //if R1=R2 then the divider is 50%
          
        #endif
        #ifdef _USESLABATTERY
          P->snsValue = readVoltageDivider( 100,  6,  P->snsPin, 1, 3); //esp12e ADC maxes at 1 volt, and can sub the lowest common denominator of R1 and R2 rather than full values
          
        #endif


        break;
        }
      case 61:
        {
          //_USEBATPCNT
        #ifdef _USELIBATTERY
          P->snsValue = readVoltageDivider( 1,1,  P->snsPin, 3.3, 3); //if R1=R2 then the divider is 50%

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
          P->snsValue = readVoltageDivider( 100,  6,  P->snsPin, 1, 3); //esp12e ADC maxes at 1 volt, and can sub the lowest common denominator of R1 and R2 rather than full values

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
  P->LastReadTime = t; //localtime

  #ifdef _WEBCHART
    for (byte k=0;k<_WEBCHART;k++) {
      if (SensorCharts[k].snsType == P->snsType) {
        if (SensorCharts[k].lastRead+SensorCharts[k].interval <= P->LastReadTime) {
          SensorCharts[k].lastRead = P->LastReadTime;
          //byte tmpval = (byte) ((double) (P->snsValue + SensorCharts[k].offset) / SensorCharts[k].multiplier );

          pushDoubleArray(SensorCharts[k].values,_NUMWEBCHARTPNTS,P->snsValue);
        }
      }

    }
  #endif

  


return true;
}

byte find_limit_sensortypes(String snsname, byte snsType,bool highest){
  //returns nidex to sensorval for the entry that is flagged and highest for sensor with name like snsname and type like snsType
byte cnt = find_sensor_count(snsType);
byte j = 255;
byte ind = 255;
double snsVal;
if (highest) snsVal=-99999;
else snsVal = 99999;
bool newind = false;
  for (byte i=1;i<=cnt;i++) {
    newind = false;
    j = find_sensor_name(snsname,snsType,i);
    if (j!=255) {
      if (highest) { 
        if (snsVal<Sensors[j].snsValue) newind = true;
      } else {
        if (snsVal>Sensors[j].snsValue) newind = true;
      }

      if (newind) {
        snsVal = Sensors[j].snsValue;
        ind=j;
      }
    }
  }

  return ind;

}


byte find_sensor_count(byte snsType) {
//find the number of sensors associated with sensor havign snsname 
byte cnt =0;
  for (byte j=0; j<SENSORNUM;j++) {

    if (Sensors[j].snsType == snsType) cnt++;

  }

  return cnt;
}

byte find_sensor_type(byte snsType,byte snsID) {
  //return the first sensor that is of specified type
  //set snsID to 255 to ignore this field (this is an optional field, if not specified then find first snstype for fieldname)
  //returns 255 if no such sensor found
  String temp;
  for (byte j=0; j<SENSORNUM;j++) {
    if (snsID==255) {
      if (Sensors[j].snsType == snsType) return j;
    } else {
      if (Sensors[j].snsType == snsType && Sensors[j].snsID == snsID) return j;
    }
  }
  return 255;
}


byte find_sensor_name(String snsname,byte snsType,byte snsID) {
  //return the first sensor that has fieldname within its name
  //set snsID to 255 to ignore this field (this is an optional field, if not specified then find first snstype for fieldname)
  //returns 255 if no such sensor found
  String temp;

  if (snsname=="") snsname = ARDNAME;
  for (byte j=0; j<SENSORNUM;j++) {
    temp = Sensors[j].snsName; 

    if (snsID==255) {
      if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType) return j;
    } else {
      if (temp.indexOf(snsname)>-1 && Sensors[j].snsType == snsType && Sensors[j].snsID == snsID) return j;
    }
  }
  return 255;
}

bool checkSensorValFlag(struct SensorVal *P) {
  //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value, 
  //RMB5 is only relevant if bit 0 is 1 [flagged] and then this is 1 if the value is too high and 0 if too low, RMB6 = flag changed since last read, flag status changed and I have not sent data yet
  
  bool lastflag = false;
  bool thisflag = false;

  if (P->snsType>=50 && P->snsType<60) { //HVAC is a special case. 50 = total time, 51 = gas, 55 = hydronic valve, 56 - ac 57 = fan
    lastflag = bitRead(P->Flags,0); //this is the last flag status
    if (P->LastsnsValue <  P->snsValue) { //currently flagged
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

  if (P->LastsnsValue>P->limitUpper || P->LastsnsValue<P->limitLower) lastflag = true;

  if (P->snsValue>P->limitUpper || P->snsValue<P->limitLower) {
    thisflag = true;
    bitWrite(P->Flags,0,1);

    //is it too high? write bit 5
    if (P->snsValue>P->limitUpper) bitWrite(P->Flags,5,1);
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

uint8_t findSensor(byte snsType, byte snsID) {
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsID == snsID && Sensors[j].snsType == snsType) return j; 
  }
  return 255;  
}

uint16_t findOldestDev() {
  int oldestInd = 0;
  int  i=0;
  for (i=0;i<SENSORNUM;i++) {
    if (Sensors[oldestInd].LastReadTime == 0) oldestInd = i;
    else if (Sensors[i].LastReadTime< Sensors[oldestInd].LastReadTime && Sensors[i].LastReadTime >0) oldestInd = i;
  }
  if (Sensors[oldestInd].LastReadTime == 0) oldestInd = 255; //some arbitrarily large number that will never be seen...

  return oldestInd;
}

void initSensor(int k) {
  //special cases... k>255 then expire any sensor that is older than k mimnutes
  //k<-255 then init ALL sensors
  time_t t=now();
  if (k<-255)  {
    for (byte i=0;i<SENSORNUM;i++) initSensor(i); //init all sensors
  }
    
  if (k>255) { //init all sensors that are this old in unixtime minutes
    for (byte i=0;i<SENSORNUM;i++)  {
      if (Sensors[i].snsID>0 && Sensors[i].LastSendTime>0 && (uint32_t) (t-Sensors[i].LastSendTime)>(uint32_t) k*60)  {//convert to seconds
        //remove N hour old values 
        initSensor(i);
      }
    }
  }
  if (k<0) return; //cannot deciper this
  if (k>=SENSORNUM) return; //cannot deciper this

  sprintf(Sensors[k].snsName,"");
  Sensors[k].snsID = 255; //this is an impossible value for ID, as 0 is valid
  Sensors[k].snsType = 0;
  Sensors[k].snsValue = 0;
  Sensors[k].PollingInt = 0;
  Sensors[k].SendingInt = 0;
  Sensors[k].LastReadTime = 0;
  Sensors[k].LastSendTime = 0;  
  Sensors[k].Flags =0; 
}


uint8_t countDev() {
  uint8_t c = 0;
  for (byte j=0;j<SENSORNUM;j++)  {
    if (Sensors[j].snsType > 0) c++; 
  }
  return c;
}

void pushByteArray(byte arr[], byte N, byte value) {
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;
}

void pushDoubleArray(double arr[], byte N, double value) { //array variable, size of array, value to push
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;

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
  while (j<SENSORNUM) {
    for (byte jj=0;jj<2;jj++) {
      oled.printf("%d.%d=%d%s",Sensors[j].snsType,Sensors[j].snsID,(int) Sensors[j].snsValue, (bitRead(Sensors[j].Flags,0)==1)?"! ":" ");
      j++;
    }
    oled.println("");    
  }

  return;    
}

#endif


int inArray(int arr[], int N, int value) {
  //returns index to the integer array of length N holding value, or -1 if not found

for (int i = 0; i < N-1 ; i++)   if (arr[i]==value) return i;
return -1;


}


