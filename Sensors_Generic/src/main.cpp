#define _DEBUG

#include <Arduino.h>
#include <Wire.h>

#include <header.hpp>
#include <sensors.hpp>
#include <server.hpp>
#include <timesetup.hpp>


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

  

#ifdef _USE8266
  #include "LittleFS.h"

  #define FileSys LittleFS

#endif
#ifdef _USE32
  #include "SPIFFS.h" 
  #include "FS.h"
  #define FILESYS SPIFFS
  #define FORMAT_FS_IF_FAILED false
#endif


#define FileSys LittleFS
#define BG_COLOR 0xD69A


//wellesley, MA
#define LAT 42.307614
#define LON -71.299288

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
  connectWiFi(); //this is done async, so can continue processing


  Wire.begin(); 
  Wire.setClock(400000L);
  #ifdef _DEBUG
    Serial.println("wire ok");
  #endif

  #ifdef _USEMUX
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


  //by now wifi should have connected, but wait for it if not
  if (WIFI_INFO.status==0)  do {
    #ifdef _USESSD1306
    oled.print(".");
    #endif
    #ifdef _DEBUG
      Serial.print(".");
      #endif
      delay(200);

  } while (WIFI_INFO.status==0);

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
OldTime[1] = minute();
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
    Serial.println("OTA started");
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
      oled.clear();
      oled.setCursor(0,0);
      oled.println("Receiving OTA:");
      String strbuff = "Progress: " + (100*progress / total);
      oled.println("OTA start.");   
      oled.println(strbuff);
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
    server.on("/NEXTSNS", handleNEXT);
    server.on("/LASTSNS", handleLAST);
    server.on("/REBOOT", handleREBOOT);
    server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call
    server.begin();
    
    #ifdef _DEBUG
            Serial.println( "HTML server started!\n");
    #endif
  #endif



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
  
  
  #ifdef _USEAHT
    if (aht21.begin() != true)
    {
      for (byte retry = 0;retry<10;retry++) {
        #ifdef _DEBUG
        Serial.printf( "AHT2x not connected or failed to load calibration coefficient. Retry number " + (String) retry + "\n"); //(F()) save string to flash & keeps dynamic memory free
        #endif
  
        if (aht21.begin() == true) continue;
      }
    }

  #endif

  #ifdef DHTTYPE
    #ifdef _DEBUG
      Serial.printf( "dht begin\n");
      #endif
 
      dht.begin();
  #endif


  #ifdef _USEBMP
    #ifdef _DEBUG
        Serial.println("bmp begin");
    #endif
    uint32_t t = millis();
    uint32_t deltaT = 0;
    while (!bmp.begin(0x76) && deltaT<5000) { //default address is 0x77, but amazon review says this is 0x76
      deltaT = millis()-t;
      #ifdef _USESSD1306
        oled.println("BMP failed.");
        #ifdef _DEBUG
            Serial.println("bmp failed.");
        #endif
        delay(100);
        oled.clear();
        oled.setCursor(0,0);
      #else
        delay(100);
      #endif
    }
 
    /* Default settings from datasheet. */
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */


  #endif
  #ifdef _USEBME
         #ifdef _DEBUG
    Serial.printf( "bme begin\n");
       #endif
    while (!bme.begin()) {
      #ifdef _USESSD1306
        oled.println("BME failed.");
        delay(500);
        oled.clear();
        oled.setCursor(0,0);
        delay(500);
      #else
        digitalWrite(LED,HIGH);
        delay(500);
        digitalWrite(LED,LOW);
        delay(500);
      #endif
    }
 
    /* Default settings from datasheet. */
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BME280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BME280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BME280::FILTER_X16,      /* Filtering. */
                  Adafruit_BME280::STANDBY_MS_500); /* Standby time. */


  #endif

  setupSensors();


  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println(WIFI_INFO.MYIP.toString());
    oled.print(hour());
    oled.print(":");
    oled.println(minute());
  #endif


}


void loop() {

  updateTime(1,0);
  
  ArduinoOTA.handle();
  server.handleClient();
  
  
  time_t t = now(); // store the current time in time variable t

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
        if (Sensors[k].LastSendTime ==0 || Sensors[k].LastSendTime>t || Sensors[k].LastSendTime + Sensors[k].SendingInt < t || bitRead(Sensors[k].Flags,7) /* isflagged changed since last read and this value was not sent*/ || t - Sensors[k].LastSendTime >60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
      }


    }

  }
  
  if (OldTime[1] != minute()) {
    
    if (WIFI_INFO.status==0) WiFi.reconnect(); //try restarting wifi
    if (WIFI_INFO.MYIP[4] != WiFi.localIP()[4]) assignIP(WIFI_INFO.MYIP,WiFi.localIP()); //update if wifi changed

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

    //expire 24 hour old readings
    initSensor(24*60);

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
