#ifdef _ISPERIPHERAL
#include "globals.hpp"
#include "sensors.hpp"

/*sens types - see hpp file
*/


extern Devices_Sensors Sensors;
#ifdef _ISPERIPHERAL
STRUCT_SNSHISTORY SensorHistory;
#endif


#if defined(_USEHVAC) 
uint8_t HVACSNSNUM = 0;
#endif


#ifdef _USEBARPRED
  double BAR_HX[_USEBARPRED];
  char WEATHER[15]; //weather string, "sunny", "cloudy", "rain", "snow", "fog", "hail", "thunderstorm", "tornado", "other"
  uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif

#ifdef _WEBCHART
  SensorChart SensorCharts[_WEBCHART];
#endif



//  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=LowPower, RMB3-derived/calculated/predictive  value, RMB4 =  Outside sensor, RMB5 = 1 - too high /  0 = too low (only matters when bit0 is 1), RMB6 = flag changed since last read, RMB7 = this sensor is critical and must be monititored (including if a reading is delayed, so I must provide sendingInt)


#ifdef _USEBME680
  BME680_Class BME680;  ///< Create an instance of the BME680 class
  int32_t temperature, humidity, pressure, gas;
  uint32_t last_BME680 =0;
#endif

#ifdef _USEBME680_BSEC
  Bsec iaqSensor;
  
#endif

#ifdef _USEADS1115
  Adafruit_ADS1115 ads;  ///< Create an instance of the ADS1115 class
  // Use the full-scale voltage range that best matches your expected input.
  // GAIN_TWOTHIRDS allows a maximum input voltage of +/- 6.144V
  //GAIN_ONE allows a maximum input voltage of +/- 4.096V
  //GAIN_TWO allows a maximum input voltage of +/- 2.048V

  #ifndef _USE_ADS_GAIN
    #define _USE_ADS_GAIN GAIN_TWO
  #endif
  #ifndef _USE_ADS_MULTIPLIER
    #define _USE_ADS_MULTIPLIER 0.0625F //gain multiplier for _USE_ADS_GAIN is 0.0625
  #endif

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
#endif




bool STRUCT_SNSHISTORY::recordSentValue(ArborysSnsType *S, int16_t hIndex) {
  if (hIndex < 0 || hIndex >= _SENSORNUM || isTimeValid(S->timeRead) == false) return false;
  HistoryIndex[hIndex]++;
  if (HistoryIndex[hIndex] >= _SENSORHISTORYSIZE) HistoryIndex[hIndex] = 0;
  TimeStamps[hIndex][HistoryIndex[hIndex]] = S->timeRead;
  Values[hIndex][HistoryIndex[hIndex]] = S->snsValue;
  Flags[hIndex][HistoryIndex[hIndex]] = S->Flags;
  return true;
}


void setupSensors() {

SerialPrint("Sensors setup started",true);
#ifdef _USE32
#ifdef _USEADCATTEN
analogSetAttenuation(_USEADCATTEN);
#endif

#ifdef _USEADCBITS
analogSetWidth(_USEADCBITS); //this sets the number of bits to 12
#endif


#endif

String myname = String(Prefs.DEVICENAME);
if (myname == "") {
  #ifdef _MYNAME
    myname = _MYNAME;
  #else
    myname = "sensor" + String(ESP.getEfuseMac(), HEX);
  #endif
  snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", myname.c_str());
  Prefs.isUpToDate = false;
}


I.MY_DEVICE_INDEX = Sensors.findMyDeviceIndex(); //update my index
SerialPrint("MY_DEVICE_INDEX: " + String(I.MY_DEVICE_INDEX),true);

uint16_t flagstates[] = _FLAGSTATES;
uint16_t interval_poll[] = _INTERVAL_POLL;
uint16_t interval_send[] = _INTERVAL_SEND;
double limit_max[] = _LIMIT_MAX;
double limit_min[] = _LIMIT_MIN;
String sensornames[] = _SENSORNAMES;
byte sensortypes[] = _SENSORTYPES;
int16_t snsPins[] = _SNSPINS;
int16_t powerPins[] = _POWERPINS;

//if Prefs has values saved, use those. Otherwise, use the defaults
// Check each sensor individually and fill in missing values

  

for (byte i=0;i<_SENSORNUM;i++) {
  bool sensorNeedsDefaults = false;
  
  // Check if this sensor's values are uninitialized (all zeros is invalid)
  if (Prefs.SNS_FLAGS[i] == 0 && Prefs.SNS_INTERVAL_POLL[i] == 0 && Prefs.SNS_INTERVAL_SEND[i] == 0) {
    sensorNeedsDefaults = true;
  }
  
  if (sensorNeedsDefaults) {
    Prefs.SNS_FLAGS[i] = flagstates[i];
    Prefs.SNS_LIMIT_MAX[i] = limit_max[i];
    Prefs.SNS_LIMIT_MIN[i] = limit_min[i];
    Prefs.SNS_INTERVAL_POLL[i] = interval_poll[i];
    Prefs.SNS_INTERVAL_SEND[i] = interval_send[i];
    Prefs.isUpToDate = false;
  }
}


#if defined(_USEMUX) 
pinMode(MUXPINS[0],OUTPUT);
pinMode(MUXPINS[1],OUTPUT);
pinMode(MUXPINS[2],OUTPUT);
pinMode(MUXPINS[3],OUTPUT);
pinMode(MUXPINS[4],INPUT);
digitalWrite(MUXPINS[0],HIGH);
digitalWrite(MUXPINS[1],HIGH);
digitalWrite(MUXPINS[2],HIGH);
digitalWrite(MUXPINS[3],HIGH); //set to last mux channel by default
#endif


#if defined(_USEHVAC)

  #if defined(_USEHEAT) && !defined(_USEMUX)
    for (byte i=0;i<_USEHEAT;i++) {
      pinMode(HEATPINS[i],INPUT);
    }
  #endif
#endif

  for (byte i=0;i<_SENSORNUM;i++) {
    byte snsID = Sensors.countSensors(sensortypes[i],I.MY_DEVICE_INDEX)+1;

    //;pin number for the sensor, if applicable. 0-99 is anolog in pin, 100-199 is MUX address, 200-299 is digital in pin, 300-399 is SPI pin, 400-599 is an I2C address. Negative values mean the same, but that there is an associated power pin. -9999 means no pin. // pin number for the sensor, if applicable. If 0-99 then it is an analoginput pin. If 100-199 then it is 100-pin number and a digital input. If >=200 then SPI and (snsPin-100) is the chip select pin. If -1 to -128 then it is -1*the I2C address of the sensor. If -200 then there is no pin, if -201 then it is a special case
    int8_t correctedPin=-1;
    uint8_t pintype = getPinType(snsPins[i], &correctedPin);
    if (pintype !=0) {
      //set up pins
      if (pintype <= 4) pinMode(correctedPin, INPUT);
      //if pintype is even, then it has a power pin, and we need to set the pin to output and low
      if (pintype % 2 == 0 && pintype <= 6) {
        togglePowerPin(powerPins[i], 0);
      } 
    }

    //note that the ith sensor index is the same as the prefs index for the sensor... though I do not guarantee that this will always be the case. 
    SensorHistory.sensorIndex[i] = Sensors.addSensor(ESP.getEfuseMac(), WiFi.localIP(), sensortypes[i], snsID, String(myname + "_" + String(sensornames[i])).c_str(), 0, 0, 0, Prefs.SNS_INTERVAL_SEND[i], Prefs.SNS_FLAGS[i], myname.c_str(), _MYTYPE, snsPins[i],powerPins[i]);
    SensorHistory.PrefsSensorIDs[i] = Sensors.makeSensorID(SensorHistory.sensorIndex[i]); 
    SensorHistory.PrefsIndex[i] = i; //this is the index to the Prefs array for the sensor, at the start it is the same as sensorhistory index
    SensorHistory.HistoryIndex[i] = 0; //start at the beginning of the history array

    switch (sensortypes[i]) {
      
      case 53: //HVAC time - this is the total time. Note that sensor pin is not used
        {
        bitWrite(Prefs.SNS_FLAGS[i],3,1);
        
        break;
        }

      case 61: //battery percent
        {
          bitWrite(Prefs.SNS_FLAGS[i],3,1);
        break;
        }
      case 90: //Sleep info
        {
          bitWrite(Prefs.SNS_FLAGS[i],7,0); bitWrite(Prefs.SNS_FLAGS[i],3,1); bitWrite(Prefs.SNS_FLAGS[i],1,0);
        break;
        }
    }
  }

  #ifdef _USETFLUNA
  Matrix_Init();
  LocalTF.TFLUNASNS = Sensors.findSensor(I.MY_DEVICE_INDEX,7,1);
  LocalTF.LAST_DRAW = Matrix_Draw(false, "INIT");
  #endif


  SerialPrint("Sensors setup complete",true);
}

double peak_to_peak(int16_t pin, int ms) {
  pinMode(pin, INPUT);
  //check n (samples) over ms milliseconds, then return the max-min value (peak to peak value) 

  if (ms==0) ms = 50; //50 ms is roughly 3 cycles of a 60 Hz sin wave
  
  double maxVal = 0;
  double minVal=6000;
  uint32_t buffer = 0;
  uint32_t t0;
  

  t0 = millis();

  while (millis()<=t0+ms) { 

    buffer = readPinValue(pin, 1);
    if (maxVal<buffer) maxVal = buffer;
    if (minVal>buffer) minVal = buffer;

  }
  

  return ((double)maxVal-minVal)/1000; //return value in volts

}


int8_t readAllSensors(bool forceRead) {
//returns the number of sensors that were read successfully
  int8_t numGood = 0;
  for (int16_t i = 0; i < _SENSORNUM; i++) {
    ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[i]);
    if (sensor && sensor->IsSet) {
      if (sensor->deviceIndex != I.MY_DEVICE_INDEX) continue;
      
      int8_t readResult = ReadData(sensor, forceRead);
      //readresult = 0 means not time to read, not an error

      if (readResult == -10) {
        #ifdef _USESERIAL
        SerialPrint((String) "Invalid sensor reading for " + (String) sensor->snsType + (String) "." + (String) sensor->snsID, true);
        #endif
        storeError((String) "Invalid sensor reading for " + (String) sensor->snsType + (String) "." + (String) sensor->snsID, ERROR_SENSOR_READ, true);
      } else if (readResult == -1) {
          #ifdef _USESERIAL
          SerialPrint((String) "Could not find index to prefs or history for " + (String) sensor->snsType + (String) "." + (String) sensor->snsID, true);
          #endif
          storeError((String) "Could not find index to prefs or history for " + (String) sensor->snsType + (String) "." + (String) sensor->snsID, ERROR_SENSOR_READ, true);
      } else if (readResult == -2) {
          #ifdef _USESERIAL
          SerialPrint((String) "Could not register" + (String) sensor->snsType + (String) "." + (String) sensor->snsID + " as a device.", true);
          #endif
          storeError((String) "Could not register" + (String) sensor->snsType + (String) "." + (String) sensor->snsID + " as a device.", ERROR_DEVICE_ADD, true);
      } else if (readResult >0) { //success
        numGood++;
      }
                      
      delay(20);
    }
  }
  return numGood;
}


int8_t ReadData(struct ArborysSnsType *P, bool forceRead) {
  //return -10 if reading is invalid, -2 if I am not registered, -1 if not my sensor, 0 if not time to read, 1 if read successful
  
  //is this my sensor?
  if (P->deviceIndex != I.MY_DEVICE_INDEX) return -1;

  // need the index to the Prefs arrays for the sensor
  int16_t prefs_index = Sensors.getPrefsIndex(P->snsType, P->snsID, I.MY_DEVICE_INDEX);
  if (prefs_index == -2) return -2;
  if (prefs_index == -1) return -1;

  //is it time to read?
  if (forceRead==false && !(P->timeRead==0 || P->timeRead>I.currentTime || P->timeRead + Prefs.SNS_INTERVAL_POLL[prefs_index] < I.currentTime || I.currentTime - P->timeRead >60*60*24 )) return 0;

  

  // Use I.currentTime instead of local time_t t variable
  byte nsamps; //only used for some sensors
  double val;
  uint8_t lastflag = P->Flags;
  //reset adjustable flags
  bitWrite(P->Flags,0,0);
  bitWrite(P->Flags,5,0);
  bitWrite(P->Flags,6,0);

  double LastsnsValue = P->snsValue;
  bool isInvalid = false;
  
  switch (P->snsType) {
    case 1: //DHT temp
      {
        #ifdef DHTTYPE
        //DHT Temp
        P->snsValue =  (dht.readTemperature()*9/5+32);
        if (isTempValid(P->snsValue,false)==false) isInvalid=true;
      #endif
      
      break;
      }
    case 2: //DHT RH
      {
        #ifdef DHTTYPE
        //DHT Temp
        P->snsValue = dht.readHumidity();
        if (isRHValid(P->snsValue)==false) isInvalid=true;
      #endif
      
      break;
      }
    case 3: //soil resistance 
    case 33: //soil capacitance
    case 34: //soil resistance (ADS1115)
    case 35: //soil capacitance (ADS1115)
      {
        //check which type of sensor reading is being done. if pin is dio then read the pin from the esp32. If pin is 100-199 then read the sensor from the MUX or ADS1115
        if (P->snsPin<100) {
          val=readPinValue(P, 10);

          delay(10);
  
          SerialPrint("val: " + String(val) + " sensor type: " + String(P->snsType),true);


          if (P->snsType==3) {
            P->snsValue = readResistanceDivider(10000, 3.3, val);
            if (isSoilResistanceValid(P->snsValue)==false) isInvalid=true;
          
          } 
          if (P->snsType==33) {
            P->snsValue = mapfloat(val, 0.15, 1.75, 0, 100); //this is the effective measurement range
            if (isSoilCapacitanceValid(P->snsValue)==false) isInvalid=true;
          } 
        }
        else {
          #ifdef _USEADS1115
            //read the sensor from the ADS1115. I have not currently implemented MUX reading and only allow 1 ADS1115 at present.
            readADS1115(10, P);

            //now convert the voltage to the sensor value
            if (P->snsType==34) {
              P->snsValue = readResistanceDivider(10000, 3.3, P->snsValue);
              if (isSoilResistanceValid(P->snsValue)==false) isInvalid=true;
            } 
            if (P->snsType==35) {
              P->snsValue = mapfloat(P->snsValue, 0.15, 1.75, 0, 100); //this is the effective measurement range
              if (isSoilCapacitanceValid(P->snsValue)==false) isInvalid=true;
            } 
          #endif
        }
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
          }
          else
          {
            SerialPrint("AHT Temperature Error",true);
          }
      #endif
      #ifdef _USEAHTADA
        //aht temperature
          sensors_event_t humidity, temperature;
          aht.getEvent(&humidity,&temperature);
          P->snsValue = (100*(temperature.temperature*9/5+32))/100; 
          if (isTempValid(P->snsValue,false)==false) isInvalid=true;

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
          }
          else
          {
            SerialPrint("AHT Humidity Error",true);
          }
          if (isRHValid(P->snsValue)==false) isInvalid=true;
          #endif
      break;
      }
    case 6: //NTC thermistor
      {
        //requires _THERMISTOR_B0, _THERMISTOR_R0 (nominal resistance at 25C), _THERMISTOR_RKNOWN (resistance of resisor in series with NTC), _THERMISTOR_TKNOWN (temperature at known resistance), _THERMISTOR_VDD (supply voltage)   
        #if defined(_USEADS1115) && defined(_THERMISTOR_B0) && defined(_THERMISTOR_R0) && defined(_THERMISTOR_RKNOWN) && defined(_THERMISTOR_TKNOWN) && defined(_THERMISTOR_VDD)
          // 1. read voltage 
          readADS1115(20, P); 

          // 2. Calculate R_thermistor using the Voltage Divider formula
          // V_in and R_known should be measured/confirmed for highest accuracy!
          float R_thermistor = _THERMISTOR_RKNOWN * (_THERMISTOR_VDD / P->snsValue - 1.0);

          // 3. Calculate Temperature using the Steinhart-Hart (B-parameter) equation (Result is in Kelvin)
          // T = 1 / [ (1/T0) + (1/B) * ln(R_thermistor / R0) ]
          float T_kelvin = 1.0 / ( (1.0/_THERMISTOR_TKNOWN) + (1.0/_THERMISTOR_B0) * log(R_thermistor / _THERMISTOR_R0) );
          
          // 4. Convert Kelvin to Fahrenheit
          // Step 4a: Convert Kelvin to Celsius
          float T_celsius = T_kelvin - 273.15;
          
          // Step 4b: Convert Celsius to Fahrenheit
          P->snsValue = (T_celsius * 9.0/5.0) + 32.0; 
          if (isTempValid(P->snsValue,false)==false) isInvalid=true;
        #else
          SerialPrint("NTC thermistor not configured",true);
          P->snsValue = -1000;
          isInvalid = true;
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
          //find the index to the TFLUNA sensor and then call checkTFLuna with the index, which will update the sensor value
            checkTFLuna(-1);          
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
        if (isPressureValid(P->snsValue)==false) isInvalid=true;

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
        if (isPressureValid(P->snsValue)==false) isInvalid=true;
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
        if (isTempValid(P->snsValue,false)==false) isInvalid=true;
      #endif
      
      break;
      }
    case 15: //bme rh
      {
        #ifdef _USEBME
      
          P->snsValue = ( bme.readHumidity() );
          if (isRHValid(P->snsValue)==false) isInvalid=true;
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
      if (isTempValid(P->snsValue,false)==false) isInvalid=true;
      break;
      }
    case 18: //bme680 humidity
      {
        read_BME680();
        P->snsValue = ((double) humidity/1000); //RH%
        if (isRHValid(P->snsValue)==false) isInvalid=true;
        break;
      }
    case 19: //bme680 air pressure
      {
        read_BME680();
      P->snsValue = ((double) pressure/100); //hPa
      if (isPressureValid(P->snsValue)==false) isInvalid=true;
      break;
      }
    case 20: //bme680 gas
      {
        read_BME680();
      P->snsValue = (gas); //milliohms
      break;
      }
    #endif

    #if defined(_USEHVAC) 

      case 50: //total HVAC time        
        {
          if (Sensors.countFlagged(55,0b00000001,0b00000001,0)>0 || Sensors.countFlagged(51,0b00000001,0b00000001,0)>0) {
            P->snsValue += Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //number of minutes HVAC  systems were on
            bitWrite(P->Flags,0,1); //currently flagged
          }
        break;
        }

    #ifdef _USEHEAT
      case 51: //heat - gas valve
        {

          
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
            //use the ESP32 pins directly
            val = peak_to_peak(snspin,50);
          #endif

          if (val > Prefs.SNS_LIMIT_MAX[prefs_index]) {
            P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the system was on
            bitWrite(P->Flags,0,1); //currently flagged
          }
        break;
        }
    
      case 52: //heat
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

        if (val > Prefs.SNS_LIMIT_MAX[prefs_index]) {
          P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the system was on
          bitWrite(P->Flags,0,1); //currently flagged

        }

            
        break;
        }
    #endif

    #if defined(_USEAC)
      case 56: //aircon compressor
      {
        //assumes you are using a fan relay to switch on
        //if the fan is off, the NC pins of relay will be connected and I can read a digital high
        //if fan is on, relay will be away (conneccting AC) andpin will be low
    
        val=readPinValue(P, 1);
        if (val == 0)           {
          bitWrite(P->Flags,0,1); //currently flagged
          P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the ac was on          
        }

        break;
      }
      case 55: //fan
      {
        //assumes you are using a fan relay to switch on
        //if the fan is off, the NC pins of relay will be connected and I can read a digital high
        //if fan is on, relay will be away (conneccting AC) andpin will be low
        val=readPinValue(P, 1);
        if (val == 0) {
          bitWrite(P->Flags,0,1); //currently flagged
          P->snsValue += (double) Prefs.SNS_INTERVAL_POLL[prefs_index]/60; //snsvalue is the number of minutes the ac was on
        }


        break;
      }
    #endif
    #endif

    #if defined(_USELIBATTERY) || defined(_USESLABATTERY)

      //use ESP ADC
      case 60: // Li battery
      case 61: //pb battery
        {
          //note that esp32 ranges 0 to ADCRATE, while 8266 is 1023. This is set in header.hpp
          P->snsValue = readVoltageDivider( _VDIVIDER_R1, _VDIVIDER_R2,  P, 20); //if R1=R2 then the divider is 50%


        break;
        }

      //use ads1115 
      case 62: //li battery voltage from an ADS1115
      case 63: //pb battery voltage from an ADS1115
        {
          //_USEBATPCNT
        #if defined(_USEADS1115) 
          readADS1115(20, P); 
          P->snsValue = P->snsValue * (((float)(_VDIVIDER_R1 + _VDIVIDER_R2))/_VDIVIDER_R2)/1000.0; //convert to total voltage, in volts
        #else
          P->snsValue = -99999; //invalid value
        #endif


        break;
        }

      #endif

      case 70: //Leak detection
      {
        #ifdef _USELEAK
        digitalWrite(_USELEAK, HIGH);
        if (digitalRead(_USELEAK)==HIGH) P->snsValue =1;
        else P->snsValue =0;
        digitalWrite(_USELEAK, LOW);

      #endif

      break;
      }

    case 90:
      {
        //don't do anything here
      //I'm set manually!
      break;
      }

    case 98:
    {
      //I am a clock sensor, return the current time in unix format
      P->snsValue = I.currentTime;
      break;
    }
    
  }

  if (isInvalid) {
    //the reading is considered invalid. Set the lastread time such that the next read will be 25% of typical interval.
    int shortDelay = (Prefs.SNS_INTERVAL_POLL[prefs_index] * 0.25);
    if (shortDelay < 60 && Prefs.SNS_INTERVAL_POLL[prefs_index] > 60) shortDelay = 60; //minimum 1 minute
    if (shortDelay > 60*10) shortDelay = 60*10; //maximum 10 minutes
    P->timeRead = I.currentTime - shortDelay;
    return -10;
  }


  #ifdef _USELOWPOWER
    bitWrite(P->Flags,2,1); //low power device
  #else
    bitWrite(P->Flags,2,0); //not low power device
  #endif

  if (P->snsType>=50 && P->snsType<60) { //HVAC is a special case. 50 = total time, 51 = gas, 55 = hydronic valve, 56 - ac 57 = fan
    if (bitRead(P->Flags,0) != bitRead(lastflag,0)) { //flags changed
      bitWrite(P->Flags,6,1); //change in flag status
      if (bitRead(P->Flags,0) == 1) bitWrite(P->Flags,5,1); //value is high
    } else {
      //no change in flag status. bit 6 is already 0.
    }
    P->timeRead = I.currentTime; //localtime

    //add to sensor history
    SensorHistory.recordSentValue(P, prefs_index);     
  }  else  {
    //set flag status

    double limitUpper = (prefs_index>=0) ? Prefs.SNS_LIMIT_MAX[prefs_index] : -9999999;
    double limitLower = (prefs_index>=0) ? Prefs.SNS_LIMIT_MIN[prefs_index] : 9999999;

    if (P->snsValue>limitUpper || P->snsValue<limitLower) {
      bitWrite(P->Flags,0,1); //flagged
      if (P->snsValue>limitUpper) bitWrite(P->Flags,5,1); //value is high
      else bitWrite(P->Flags,5,0); //value is low
    } else {
      bitWrite(P->Flags,0,0); //not flagged
    }
    if (bitRead(lastflag,0)!=bitRead(P->Flags,0)) {     
      bitWrite(P->Flags,6,1); //flag changed
    }
    P->timeRead = I.currentTime; //localtime
    //add to sensor history
    SensorHistory.recordSentValue(P, prefs_index);  
  }

  #ifdef _USELED
    //check if this is a soil sensor
    if (Sensors.isSensorOfType(P, "soil")) LEDs.LED_set_color_soil(P);
  #endif

  return 1;
}


void togglePowerPin(int16_t powerPin, bool on) {
  #ifdef _USELOWPOWER
  //low power mode does not turn on power pins, this is done at startup
  return;
  #endif
  powerPin = abs(powerPin);
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, on ? HIGH : LOW);
}

float readResistanceDivider(float R1, float Vsupply, float Vread) {
  return R1 * Vread/(Vsupply - Vread) ;
}


bool readADS1115(byte avgN, ArborysSnsType* P) {
  //returns the voltage read from the ADS1115, and stores in P->snsValue
  //note that P->snsPin is the ADS1115 channel, not the actual pin number or ADS address (for example, ADS1115 has channels 0-3).
  SerialPrint("Entering readADS1115 with: P->snsPin: " + String(P->snsPin) + " powerPin: " + String(P->powerPin), true);
  #ifdef _USEADS1115
    int16_t snsPin = P->snsPin;
    int16_t powerPin = P->powerPin;
    if (abs(P->snsPin) < 100 || abs(P->snsPin) > 199) return false; //invalid pin for ADS1115
    SerialPrint("readADS1115: snsPin: " + String(snsPin) + " powerPin: " + String(powerPin), true);
    if (P->snsPin < 0) {
      togglePowerPin(powerPin,1);      
    }
    snsPin = abs(P->snsPin)-100; //convert to positive channel number
    
    P->snsValue = 0; // Initialize to zero before accumulation
    for (byte i=0;i<avgN;i++) { //read the ADC and average
      P->snsValue += ads.readADC_SingleEnded(snsPin) ;
      SerialPrint("readADS1115: readADC_SingleEnded(" + String(snsPin) + "): " + String(ads.readADC_SingleEnded(snsPin)), true);
      delay(10);
    }

    if (P->snsPin < 0) { //if negative pin, then power pin is the same as the sensor pin
      togglePowerPin(powerPin,0);
    }

    P->snsValue /= avgN; //average the readings
    P->snsValue = P->snsValue * _USE_ADS_MULTIPLIER; //convert to voltage
    SerialPrint("readADS1115: final P->snsValue: " + String(P->snsValue), true);
    return true;
  #else
    return false;
  #endif
}

float readVoltageDivider(float R1, float R2, ArborysSnsType* P, byte avgN) {
  /*
    R1 is first resistor
    R2 is second resistor (which we are measuring voltage across)
    P is the sensor pointer
    ADCRATE is the max ADCRATE
    Vm is the ADC max voltage 
    avgN is the number of times to avg
    */
 
  return (float)  readPinValue(P, avgN) * ((float)(R1 + R2))/R2;

}


#ifdef _USEBME680
void read_BME680() {
  
  uint32_t m = millis();
  
  if (last_BME680>m || m-last_BME680>500)   BME680.getSensorData(temperature, humidity, pressure, gas);  // Get readings
  else return;
  last_BME680=m;
}
#endif


/**
 * @brief Initialize hardware sensors (BME, BMP, DHT, etc.)
 */
void initHardwareSensors() {
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("Sns setup.");
  #endif

  #ifdef _USEAHT
  if (!aht.begin()) SerialPrint("AHT not connected",true);
  #endif
  #ifdef _USEAHTADA
  if (!aht.begin()) SerialPrint("AHT not connected",true);
  #endif
  

  #ifdef _USEBME680_BSEC
    iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire);
    String output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
    Serial.println(output);
    checkIaqSensorStatus();

    bsec_virtual_sensor_t sensorList[13] = {
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_STABILIZATION_STATUS,
      BSEC_OUTPUT_RUN_IN_STATUS,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
      BSEC_OUTPUT_GAS_PERCENTAGE
    };

    iaqSensor.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);
    checkIaqSensorStatus();
  #endif

  #ifdef _USEBME680
    while (!BME680.begin(I2C_STANDARD_MODE)) {  // Start BME680 using I2C, use first device found
      #ifdef _DEBUG
        Serial.println("-  Unable to find BME680. Trying again in 5 seconds.\n");
      #endif
      delay(5000);
    }
    BME680.setOversampling(TemperatureSensor, Oversample16);
    BME680.setOversampling(HumiditySensor, Oversample16);
    BME680.setOversampling(PressureSensor, Oversample16);
    BME680.setIIRFilter(IIR4);
    BME680.setGas(320, 150);  // 320°c for 150 milliseconds
  #endif

  #ifdef DHTTYPE
    #ifdef _DEBUG
      Serial.printf("dht begin\n");
    #endif
    dht.begin();
  #endif
  #ifdef _USEBARPRED
    for (byte ii = 0; ii < 24; ii++) {
      BAR_HX[ii] = -1;
    }
    LAST_BAR_READ = 0;
  #endif
  
  byte retry = 0;
  #ifdef _USEADS1115
    retry = 0;
    while (!isI2CDeviceReady(_USEADS1115) && retry < 100)  {
      SerialPrint("ADS1115 not ready or connected. Retry number " + String(retry),true);
      delay(100);
      retry++;
    }
    retry = 0;
    while (!ads.begin() && retry < 10) {
      SerialPrint("ADS1115 not connected. Retry number " + String(retry),true);
      delay(250);
      retry++;
    }
    if (retry >= 10) SerialPrint("ADS1115 failed to connect after 10 attempts",true);
    else SerialPrint("ADS1115 connected after " + String(retry) + " attempts",true);
  
    ads.setGain(_USE_ADS_GAIN);
  #endif

  // Initialize AHT sensor
  #if defined(_USEAHT) || defined(_USEAHTADA)
    retry = 0;
    while (!isI2CDeviceReady(AHTXX_ADDRESS_X38) && retry < 100) {
      SerialPrint("AHT not ready or connected. Retry number " + String(retry),true);
      delay(100);
      retry++;
    }
    retry = 0;

    while (aht.begin() != true && retry < 10) {
      
      SerialPrint("AHT not connected. Retry number " + String(retry),true);

      #ifdef _USESSD1306  
        oled.clear();
        oled.setCursor(0,0);  
        oled.printf("No aht x%d!", retry);          
      #endif
      delay(250);
      retry++;
    }
    if (retry >= 10) SerialPrint("AHT failed to connect after 10 attempts",true);
    else SerialPrint("AHT connected after " + String(retry) + " attempts",true);
  #endif

  #ifdef _USEBMP
    retry = 0;
    //note that _USEBMP is the address of the BMP sensor, but may be wrong.
    byte BMPaddress = 0;
    bool isBMPgood = true;

    //quick check to see if an address is obviously valid
    if (isI2CDeviceReady(0x76)) BMPaddress = 0x76;
    else if (isI2CDeviceReady(0x77)) BMPaddress = 0x77;
    else BMPaddress = _USEBMP;

    while (!isI2CDeviceReady(BMPaddress) && retry < 50) {
      SerialPrint("BMP not ready at address " + String(BMPaddress) + ". Retry number " + String(retry),true);
      delay(100);
      retry++;
    }

    if (!isI2CDeviceReady(BMPaddress)) {
      SerialPrint("BMP was not detected at address " + String(BMPaddress) + ". Will check address ", false);
      if (BMPaddress == 0x76) BMPaddress = 0x77;
      else BMPaddress = 0x76;
      SerialPrint(" " + String(BMPaddress), true);
      while (!isI2CDeviceReady(BMPaddress) && retry < 50) {
        SerialPrint("BMP not ready at address " + String(BMPaddress) + ". Retry number " + String(retry),true);
        delay(100);
        retry++;
      }
      if (!isI2CDeviceReady(BMPaddress)) {
        isBMPgood = false;
        SerialPrint("BMP not ready/detected at all known addresses.",true);
      } else {
        SerialPrint("BMP ready at address " + String(BMPaddress),true);
      }
    } else {
      SerialPrint("BMP ready at address " + String(BMPaddress),true);
    }

    retry = 0;

    if (isBMPgood) {
      while (bmp.begin(BMPaddress) != true && retry < 20) {
        SerialPrint("BMP failed to connect at " + String(BMPaddress) + ".\nRetry number " + String(retry) + "\n",true);

        #ifdef _USESSD1306  
          oled.clear();
          oled.setCursor(0,0);  
          oled.printf("BMP fail at %d.\nRetry number %d\n", BMPaddress, retry);          
        #endif

        delay(100);
        retry++;
      }
      if (retry >= 20) SerialPrint("BMP failed to connect after 20 attempts",true);
      else {
        SerialPrint("BMP connected after " + String(retry) + " attempts",true);
        /* Default settings from datasheet. */
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X16,
                      Adafruit_BMP280::FILTER_X16,
                      Adafruit_BMP280::STANDBY_MS_500);
        }
    }
  #endif
  
  #ifdef _USEBME
    retry = 0;
    while (!bme.begin() && retry < 20) {
      #ifdef _USESSD1306
        oled.println("BME failed.");
        delay(500);
        oled.clear();
        oled.setCursor(0,0);
        delay(500);
      #else
        SerialPrint("BME failed. Retry number " + String(retry),true);
        digitalWrite(2, HIGH);
        delay(100);
        digitalWrite(2, LOW);
        delay(100);
      #endif
      retry++;
    }

    /* Default settings from datasheet. */
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_500);
  #endif

  // Initialize barometric prediction globals

  // Call sensor-specific setup
  setupSensors();
  
}



uint8_t getPinType(int16_t pin, int8_t* correctedPin) {
  //0 = unknown or no pin
  //1 = analog input, no power
  //2 = analog input, power
  //3 = digital input, no power
  //4 = digital input, power
  //5 = MUX input, no power
  //6 = MUX input, power
  //7 = SPI input, no power
  //8 = SPI input, power
  //9 = I2C input, no power
  //10 = I2C input, power

  //pin number for the sensor, if applicable. 0-99 is anolog in pin, 100-199 is MUX or ADS address, 200-299 is digital in pin, 300-399 is SPI pin, 400-599 is an I2C address. Negative values mean the same, but that there is an associated power pin. -9999 means no pin. 
  *correctedPin = -1;
  if (pin == -9999) return 0;
  if (pin >= 0 && pin < 100) {
    *correctedPin = pin;
    return 1;
  }
  if (pin >= -99 && pin < 0) {
    *correctedPin = -1*pin;
    return 2;
  }
  if (pin >= 100 && pin < 200) {
    *correctedPin = pin-100;
    return 5;
  }
  if (pin >= -199 && pin <= -100) {
    *correctedPin = -1*pin-100;
    return 6;
  }
  if (pin >= 200 && pin < 300) {
    *correctedPin = pin-200;
    return 3;
  }
  if (pin >= -299 && pin <= -200) {
    *correctedPin = -1*pin-200;
    return 4;
  }
  if (pin >= 300 && pin < 400) {
    *correctedPin = pin-300;
    return 7;
  }
  if (pin >= -399 && pin <= -300) {
    *correctedPin = -1*pin-300;
    return 8;
  }
  if (pin >= 400 && pin < 500) {
    *correctedPin = pin-400;
    return 9;
  }
  if (pin >= -499 && pin <= -400) {
    *correctedPin = -1*pin-400;
    return 10;
  }
  return 0;
}


float readAnalogVoltage(ArborysSnsType* P, byte nsamps) {
  return readAnalogVoltage(P->snsPin, nsamps);
}

float readAnalogVoltage(int16_t pin, byte nsamps) {
  float val = 0;
  #ifdef _USEADCATTEN
  if (pin < 0) pin = -1*pin;

  pinMode(pin, INPUT);
  for (byte ii=0;ii<nsamps;ii++) {
    val += (float) analogRead(pin); //analog pin. Note that not all ESP32 boards have the correct internal lookup for analogReadMilliVolts(), so we use analogRead() instead.
    if (ii<nsamps-1) delay(10);
  }
  val = (val/nsamps)/4095; //note that analogRead always returns 12 bit values, regardless of the _USEADCBITS setting
  //output range depends on the attenuation settings. 
  if (_USEADCATTEN == ADC_6db) {
    val= val*1.75; 
  } else if (_USEADCATTEN == ADC_2_5db) {
    val= val*1.25; 
  } else if (_USEADCATTEN == ADC_0db) {
    val= val*0.95; 
  }
  else { //assume 11db
    val= val*2.450; 
  }
  #endif
  return val;
}

float readPinValue(ArborysSnsType* P, byte nsamps) {
//wrapper function to call readPinValue with the sensor pointer and the number of samples
  return readPinValue(P->snsPin, nsamps, P->powerPin);
}


float readPinValue(int16_t pin, byte nsamps, int16_t powerPin) {
  //direct call function to read the pin value
  float val=0;


  int8_t correctedPin=-1;
  uint8_t pintype = getPinType(pin, &correctedPin);
      
  //has power pin?
  if (pintype % 2 == 0 && pintype <= 6 && powerPin != -1) { //6 is the max pintype for a power pin
    togglePowerPin(powerPin, 1);
    delay(50); //wait X ms for reading to settle
  }

  if (pintype == 1 || pintype == 2) {
    val = readAnalogVoltage(pin, nsamps);
  }

  if (pintype == 3 || pintype == 4) {
    val = digitalRead(correctedPin); //digital pin, high is dry
  }

  if ((pintype == 5 || pintype == 6)) {
    //use MUX to read the pin
    #ifdef _USEMUX
    val = readMUX(correctedPin, nsamps);
    #endif
  }

  //has power pin?
  if (pintype % 2 == 0 && pintype <= 6 && powerPin != -1) { //6 is the max pintype for a power pin
    togglePowerPin(powerPin, 0);
  }

  return val; 
}



#ifdef _USEMUX
double readMUX(int16_t pin, byte nsamps) {
  double val = 0;
  digitalWrite(MUXPINS[0],bitRead(pin,0));
  digitalWrite(MUXPINS[1],bitRead(pin,1));
  digitalWrite(MUXPINS[2],bitRead(pin,2));
  digitalWrite(MUXPINS[3],bitRead(pin,3));  
  val = readPinValue(MUXPINS[4],nsamps);
  digitalWrite(MUXPINS[0],HIGH);
  digitalWrite(MUXPINS[1],HIGH);
  digitalWrite(MUXPINS[2],HIGH);
  digitalWrite(MUXPINS[3],HIGH);
  return val;
}
#endif

#endif


