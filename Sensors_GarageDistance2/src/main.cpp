#define _DEBUG

#include <Arduino.h>
#include <Wire.h>

#include <header.hpp>
#include <sensors.hpp>
#include <server.hpp>
#include <timesetup.hpp>


//local code
//Code to draw to the screen
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


extern TFLunaType LocalTF;


void MD_Draw() {

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



//_________________________________________________

#ifdef _USE8266 
  //static const uint8_t A0   = A0;
  // static const uint8_t D0   = 16;
  // static const uint8_t D1   = 5;
  // static const uint8_t D2   = 4;
  // static const uint8_t D3   = 0;
  // static const uint8_t D4   = 2;
  // static const uint8_t D5   = 14;
  // static const uint8_t D6   = 12;
  // static const uint8_t D7   = 13;
  // static const uint8_t D8   = 15;
  // static const uint8_t SDA   = 4;
  // static const uint8_t SCL  = 5;
  static const uint8_t LED   = 2;
#endif
//8266... I think they're the same. If not, then use nodemcu or wemos
#ifdef _USE32
/*
  static const uint8_t TX = 1;
  static const uint8_t RX = 3;
  static const uint8_t SDA = 21;
  static const uint8_t SCL = 22;

  static const uint8_t SS    = 5;
  static const uint8_t MOSI  = 23;
  static const uint8_t MISO  = 19;
  static const uint8_t SCK   = 18;
  //need to define pin labeled outputs...
  static const uint8_t LED   = 2; //this is true for dev1 board... may be wrong for others
  */
#endif



//#define NODEMCU
  /*
  SDA -> D2 -> 4
  SCL -> D1 -> 5
  SPI SCLK -> D5 ->14
  SPI MISO -> D6 -> 12
  SPI MOSI -> D7 -> 13
  SPI CS -> D8 -> 15
  */
//#define WEMOS
  /*
  SDA -> D2 -> 4
  SCL -> D1 -> 5
  SPI SCLK -> D5
  SPI MISO -> D6
  SPI MOSI -> D7
  SPI CS -> D8
  */

  

//gen global types


//gen unions
union convertULONG {
  char str[4];
  unsigned long val;
};
union convertINT {
  char str[2];
  int16_t val;
};



//globals
#ifdef _USEBARPRED
  double BAR_HX[24];
  char WEATHER[24] = "Steady";
  uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif


byte OldTime[5] = {0,0,0,0,0};

time_t ALIVESINCE = 0;
uint32_t LAST_SERVER_STATUS_UPDATE = 0;

//function declarations


#ifdef _USELOWPOWER

uint16_t SleepCounter = 0;
// Write 16 bit int to RTC memory with checksum, return true if verified OK
// Slot 0-127
// (C) Turo Heikkinen 2019
bool writeRtcMem(uint16_t *inVal, uint8_t slot = 0) {
  uint32_t valToStore = *inVal | ((*inVal ^ 0xffff) << 16); //Add checksum
  uint32_t valFromMemory;
  if (ESP.rtcUserMemoryWrite(slot, &valToStore, sizeof(valToStore)) &&
      ESP.rtcUserMemoryRead(slot, &valFromMemory, sizeof(valFromMemory)) &&
      valToStore == valFromMemory) {
        return true;
  }
  return false;
}

// Read 16 bit int from RTC memory, return true if checksum OK
// Only overwrite variable, if checksum OK
// Slot 0-127
// (C) Turo Heikkinen 2019
bool readRtcMem(uint16_t *inVal, uint8_t slot = 0) {
  uint32_t valFromMemory;
  if (ESP.rtcUserMemoryRead(slot, &valFromMemory, sizeof(valFromMemory)) &&
      ((valFromMemory >> 16) ^ (valFromMemory & 0xffff)) == 0xffff) {
        *inVal = valFromMemory;
        return true;
  }
  return false;
}

#endif

void setup()
{
  #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Begin Setup");
  #endif

  WIFI_INFO.status=0;
  WIFI_INFO.MYIP[0]=0; //set my ip to zero to setup wifi automatically, or to assigned IP if desired.

  #ifdef _DEBUG
    Serial.println("Wifi instantiating in background");
  #endif


  connectWiFi(); //this is done async if 32, so can continue processing


  Wire.begin(); 
  Wire.setClock(400000L);
  #ifdef _DEBUG
    Serial.println("wire ok");
  #endif
 
  //LCOAL CODE
  screen.begin();
  screen.setIntensity(15);
  screen.displayClear();
  screen.setTextAlignment(PA_CENTER);
  screen.setInvert(false);
  screen.displayClear();
  screen.print("Start");
  //end local


  assignIP(SERVERIP[0].IP,192,168,68,93);
  assignIP(SERVERIP[1].IP ,192,168,68,100);
  if (KiyaanServer)  assignIP(SERVERIP[2].IP,192,168,68,106);
  else assignIP(SERVERIP[2].IP,0,0,0,0);

#ifdef _DEBUG
    Serial.println("servers set");
  #endif

  

#ifdef _USESSD1306
  #ifdef _DEBUG
    Serial.println("oled begin");
  #endif

  
  #if INCLUDE_SCROLLING == 0
  #error INCLUDE_SCROLLING must be non-zero. Edit SSD1306Ascii.h
  #elif INCLUDE_SCROLLING == 1
    // Scrolling is not enable by default for INCLUDE_SCROLLING set to one.
    oled.setScrollMode(SCROLL_MODE_AUTO);
  #else  // INCLUDE_SCROLLING
    // Scrolling is enable by default for INCLUDE_SCROLLING greater than one.
  #endif

  #if RST_PIN >= 0
    oled.begin(_OLEDTYPE, I2C_OLED, RST_PIN);
  #else // RST_PIN >= 0
    oled.begin(_OLEDTYPE, I2C_OLED);
  #endif // RST_PIN >= 0
  #ifdef _OLEDINVERT
    oled.ssd1306WriteCmd(SSD1306_SEGREMAP); // colAddr 0 mapped to SEG0 (RESET)
    oled.ssd1306WriteCmd(SSD1306_COMSCANINC); // Scan from COM0 to COM[N –1], normal (RESET)
  #endif

  oled.setFont(System5x7);
  oled.set1X();
  oled.clear();
  oled.setCursor(0,0);
#endif


#ifdef _USEBME680_BSEC
  iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire);
  output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
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
    Serial.println("-  Unable to find BME680. Trying again in 5 seconds.\n"));
    #endif

      delay(5000);
    }  // of loop until device is located
    //Serial.print(F("- Setting 16x oversampling for all sensors\n"));
    BME680.setOversampling(TemperatureSensor, Oversample16);  // Use enumerated type values
    BME680.setOversampling(HumiditySensor, Oversample16);     // Use enumerated type values
    BME680.setOversampling(PressureSensor, Oversample16);     // Use enumerated type values
    //Serial.print(F("- Setting IIR filter to a value of 4 samples\n"));
    BME680.setIIRFilter(IIR4);  // Use enumerated type values
    //Serial.print(F("- Setting gas measurement to 320\xC2\xB0\x43 for 150ms\n"));  // "�C" symbols
    BME680.setGas(320, 150);  // 320�c for 150 milliseconds
    // of method setup()
#endif

#ifdef _USESSD1306
  oled.clear();
  oled.setCursor(0,0);
  oled.print("WiFi Starting.");
#endif


  #ifdef _DEBUG
  Serial.print("Is WiFi connected");
  #endif

//by now wifi should have connected, but wait for it if not
if (WiFi.status() != WL_CONNECTED)  do {
  #ifdef _USESSD1306
  oled.print(".");
  #endif
  #ifdef _DEBUG
  Serial.print(".");
  #endif
  delay(200);

} while (WiFi.status() != WL_CONNECTED);
WIFI_INFO.status=1;

  #ifdef _DEBUG
  Serial.print(" YES, address: ");
  Serial.println(WiFi.localIP().toString().c_str());
  #endif


#ifdef _USESSD1306
  oled.clear();
  oled.setCursor(0,0);
  oled.println("set up time.");
#endif

#ifdef _DEBUG
  Serial.println("setuptime done. OTA next.\n");
#endif
 
#ifdef _USESSD1306
  oled.clear();
  oled.setCursor(0,0);
  oled.println("OTA Starting.");
#endif



#ifdef _USESSD1306
  oled.clear();
  oled.setCursor(0,0);
  oled.println("start time.");
#endif


//set time
timeClient.begin(); //time is in UTC
updateTime(10,250); //check if DST and set time to EST

ALIVESINCE = now();
OldTime[0] = 100;//some arbitrary seconds that will trigger a second update
OldTime[1] = 61; //some arbitrary seconds that will trigger a second update
OldTime[2] = hour();
OldTime[3] = day();


#ifdef _USESSD1306
  oled.clear();
  oled.setCursor(0,0);
#endif


#ifndef _USELOWPOWER
  //OTA
    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(ARDNAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    #ifdef _DEBUG
    Serial.println("OTA started...");
    #endif
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.printf("OTA data incoming...\n");
    #endif
    
  });
  ArduinoOTA.onEnd([]() {
    #ifdef _DEBUG
    Serial.println("OTA End");
    #endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef _DEBUG
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
    #endif
    #ifdef _USESSD1306
      if ((int) (progress )%10==0) oled.print(".");   
    #endif

  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef _DEBUG
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      String strbuff;
      strbuff = "Error[%u]: " + (String) error + " ";
      oled.print(strbuff);
      if (error == OTA_AUTH_ERROR) oled.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) oled.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) oled.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) oled.println("Receive Failed");
      else if (error == OTA_END_ERROR) oled.println("End Failed");
    #endif
  });
  ArduinoOTA.begin();
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("OTA OK.");      
  #endif


  #ifdef _DEBUG
  Serial.println( "set up HTML server... ");
  #endif
  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/UPDATEALLSENSORREADS", handleUPDATEALLSENSORREADS);               
  server.on("/UPDATESENSORREAD",handleUPDATESENSORREAD);
  server.on("/SETTHRESH", handleSETTHRESH);               
  server.on("/UPDATESENSORPARAMS", handleUPDATESENSORPARAMS);
  server.on("/UPDATELOCALTF", HTTP_POST, handleUpdateLocalTF);  // Handle TFLuna configuration updates
  server.on("/NEXTSNS", handleNEXT);
  server.on("/LASTSNS", handleLAST);
  server.on("/REBOOT", handleREBOOT);
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call
  server.begin();
  
  #ifdef _DEBUG
          Serial.println( "HTML server started!\n");
  #endif
#endif


  byte retry = 0;

  /*
   * 
   * DEVICE SPECIFIC SETUP HERE!
   * 
   */
   //**************************************
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("Sns setup.");
    #endif


    //init globals
      initSensor(-256);

    #ifdef _USEBARPRED
      for (byte ii=0;ii<24;ii++) {
        BAR_HX[ii] = -1;
      }
      LAST_BAR_READ=0; 
    #endif
  
  
  #if defined(_USEAHT) || defined(_USEAHTADA)
    retry  = 0;
    while (aht.begin() != true && retry<10) {
      #ifdef _DEBUG
      Serial.printf( "AHT not connected. Retry number %d\n" , retry ); //(F()) save string to flash & keeps dynamic memory free
      #endif

      #ifdef _USESSD1306  
        oled.clear();
        oled.setCursor(0,0);  
        oled.printf("No aht x%d!",retry);          
      #endif
      delay(250);
      retry++;
    }


  #endif

  #ifdef DHTTYPE
    #ifdef _DEBUG
      Serial.printf( "dht begin\n");
      #endif
 
      dht.begin();
  #endif


  byte trydev;
  #ifdef _USEBMP
    retry = 0;
    trydev= _USEBMP;
    #ifdef _DEBUG
        Serial.println("bmp begin");
    #endif

    while (bmp.begin() != true && retry<20) {
      #ifdef _DEBUG
      Serial.printf( "BMP fail at 0x%d.\nRetry number %d\n" , trydev, retry ); //(F()) save string to flash & keeps dynamic memory free
      #endif

      #ifdef _USESSD1306  
        oled.clear();
        oled.setCursor(0,0);  
        oled.printf("BMP fail at 0x%d.\nRetry number %d\n" , trydev, retry);          
      #endif

      if (retry==10) { //try swapping address
        if (trydev == 0x76) trydev = 0x77;
        else trydev = 0x76;
      }

      delay(100);
      retry++;
    }
  
 
    /* Default settings from datasheet. 
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     // Operating Mode. 
                  Adafruit_BMP280::SAMPLING_X2,     // Temp. oversampling
                  Adafruit_BMP280::SAMPLING_X16,    // Pressure oversampling 
                  Adafruit_BMP280::FILTER_X16,      // Filtering. 
                  Adafruit_BMP280::STANDBY_MS_500); // Standby time. 
                  */


  #endif
  #ifdef _USEBME
    #ifdef _DEBUG
      Serial.printf( "bme begin\n");
    #endif
    retry=0;
    trydev= _USEBME;
    #ifdef _DEBUG
        Serial.println("bme begin");
    #endif

    while (bme.begin() != true && retry<20) {
      #ifdef _DEBUG
      Serial.printf( "BME fail at 0x%d.\nRetry number %d\n" , trydev, retry ); //(F()) save string to flash & keeps dynamic memory free
      #endif

      #ifdef _USESSD1306  
        oled.clear();
        oled.setCursor(0,0);  
        oled.printf("BME fail at 0x%d.\nRetry number %d\n" , trydev, retry);          
      #endif

      if (retry==10) { //try swapping address
        if (trydev == 0x76) trydev = 0x77;
        else trydev = 0x76;
      }

      delay(250);
      retry++;
    }
  
    
 
    /* 
    Default settings from datasheet. 
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,     // Operating Mode. 
                  Adafruit_BME280::SAMPLING_X2,     // Temp. oversampling 
                  Adafruit_BME280::SAMPLING_X2,     // hum. oversampling 
                  Adafruit_BME280::SAMPLING_X16,    // Pressure oversampling 
                  Adafruit_BME280::FILTER_X16,      // Filtering. 
                  Adafruit_BME280::STANDBY_MS_500); // Standby time. 
  */

  #endif

  setupSensors();


  #ifdef _USESSD1306
    oled.clear();
    oled.printf("SETUP COMPLETE\n");
  #endif


//LOCAL CODE
  LocalTF.TFLUNASNS = findSensor(7,1);
  snprintf(LocalTF.MSG,19,"Hi!");
  MD_Draw();
//END LOCAL

}


void loop() {

  updateTime(1,0);
  
  ArduinoOTA.handle();
  server.handleClient();
  
  
  time_t t = now(); // store the current time in time variable t
  uint32_t m = millis();

//LOCAL CODE ----------------------------------------------------------
  //check tfluna distance if it is time
  if (m>LocalTF.LAST_DISTANCE_TIME+LocalTF.REFRESH_RATE) {
    LocalTF.LAST_DISTANCE_TIME = m;
    ReadData(&Sensors[LocalTF.TFLUNASNS]); //recheck TF luna distance at apr rate...    
    float tempdist = Sensors[LocalTF.TFLUNASNS].snsValue-LocalTF.BASEOFFSET;
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
        snprintf(LocalTF.MSG,19,"%s",dateify(t,"hh:nn"));      
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
  
//END LOCAL CODE ----------------------------------------------------------

  #ifdef _USELOWPOWER

         #ifdef _DEBUG
    Serial.printf("Checking for low power state.\n");
       #endif



    /*
    uint16_t rtc_temp; 

    for (byte rtcind=0;rtcind<4;rtcind++) {
      if (!readRtcMem(&rtc_temp,rtcind+1)) {
        rtc_temp = OldTime[rtcind];
        writeRtcMem(&rtc_temp,rtcind+1);
      } else {
        OldTime[rtcind] = rtc_temp;
      }
    }


    if (!readRtcMem(&SleepCounter,0))     writeRtcMem(&SleepCounter,0);
    else {
      SleepCounter++;
      writeRtcMem(&SleepCounter,0);
    }
    */

      //read and send everything now if sleep was long enough
        for (byte k=0;k<SENSORNUM;k++) {
          
                   #ifdef _DEBUG
Serial.printf( "Going to attempt read and write sensor %u\n", k );
       #endif
        
          ReadData(&Sensors[k]); //read value 
          SendData(&Sensors[k]); //send value
        }

                   #ifdef _DEBUG
        Serial.printf( "Entering sleep.\n");
       #endif
      //  Serial.flush();
      
      ESP.deepSleep(_USELOWPOWER, WAKE_RF_DEFAULT);
//will awaken with a soft reset, no need to do anything else

  #endif

  #ifdef _USECALIBRATIONMODE
    if (millis()%10000==0) checkHVAC();
  #endif


  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second
    
    #ifdef _DEBUG
      Serial.printf( ".");
    #endif

    for (byte k=0;k<SENSORNUM;k++) {
      bool goodread = false;

      if (Sensors[k].LastReadTime==0 || Sensors[k].LastReadTime>t || Sensors[k].LastReadTime + Sensors[k].PollingInt < t || t- Sensors[k].LastReadTime >60*60*24 ) goodread = ReadData(&Sensors[k]); //read value if it's time or if the read time is more than 24 hours from now in either direction
      
      if (goodread == true) {
        if (Sensors[k].LastSendTime ==0 || Sensors[k].LastSendTime>t || Sensors[k].LastSendTime + Sensors[k].SendingInt < t || bitRead(Sensors[k].Flags,6) /* isflagged changed since last read*/ || t - Sensors[k].LastSendTime >60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
      }


    }

  }
  
  if (OldTime[1] != minute()) {

    //local
      LocalTF.LAST_DRAW=0; //a new minute, force a redraw        


    if (WIFI_INFO.status==0) WiFi.reconnect(); //try restarting wifi
    if (WIFI_INFO.MYIP[3] != WiFi.localIP()[3]) assignIP(WIFI_INFO.MYIP,WiFi.localIP()); //update if wifi changed

    OldTime[1] = minute();

    #ifdef _DEBUG
    Serial.printf("\nTime is %s\n", dateify(t,"hh:nn:ss"));
    #endif

    
    #ifdef _USESSD1306
      redrawOled();
    #endif

  }


  if (OldTime[2] != hour()) {
      
  
    #ifdef _DEBUG
    Serial.printf( "\nNew hour... TimeUpdate running. \n");
    #endif
  
    checkDST();

    //don't do this!! the sensors here are permanent!!
    //expire 24 hour old readings
//    initSensor(24*60);

    OldTime[2] = hour();

  }
  
  if (OldTime[3] != day()) {
    //once per day
    #ifdef REBOOTDAILY
      ESP.restart();
    #endif

    OldTime[3] = day();

    checkDST();

    //reset heat and ac values
    #ifdef _DEBUG
      Serial.printf("\nNew day... ");
    #endif



    #if defined(_CHECKHEAT) || defined(_CHECKAIRCON) 

      #ifdef _DEBUG
        Serial.printf( "Reset HVACs...\n");
      #endif
      initHVAC();

    #endif


    #ifdef _DEBUG
      Serial.printf("\n");

    #endif

  } 
}
