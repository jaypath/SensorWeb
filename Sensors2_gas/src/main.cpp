#include <sensors.hpp>
#include <server.hpp>
#include <timesetup.hpp>


#define GLOBAL_TIMEZONE_OFFSET  -14400


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

#include "ArduinoOTA.h"

#include <WiFiClient.h>

#include <Wire.h>

#ifdef _USEBME680
  #include <Zanshin_BME680.h>
  BME680_Class BME680;  ///< Create an instance of the BME680 class
  int32_t temperature, humidity, pressure, gas;
  uint32_t last_BME680 =0;
#endif

#ifdef _USEBME680_BSEC
  #include "bsec.h"
  Bsec iaqSensor;
  
#endif


#ifdef DHTTYPE
  #include <Adafruit_Sensor.h>
  #include <DHT.h>

  DHT dht(DHTPIN,DHTTYPE,11); //third parameter is for faster cpu, 8266 suggested parameter is 11
#endif

#ifdef _USEAHT
  #include <AHTxx.h>
  

  AHTxx aht21(AHTXX_ADDRESS_X38, AHT2x_SENSOR);
#endif



#ifdef _USEBMP
//  #include <Adafruit_Sensor.h>
  #include <Adafruit_BMP280.h>
  Adafruit_BMP280 bmp; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef _USEBME
  #include <Wire.h>
  //#include <Adafruit_Sensor.h>
  #include <Adafruit_BME280.h>
  Adafruit_BME280 bme; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//  #define BMP_SCK  (13)
//  #define BMP_MISO (12)
//  #define BMP_MOSI (11)
// #define BMP_CS   (10)
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK); //software SPI
#endif

#ifdef  _USESSD1306
  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C
  SSD1306AsciiWire oled;
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


//function declarations
void pushDoubleArray(double arr[], byte, double);
void redrawOled(void);

#ifdef _USEBME680
  void read_BME680();
#endif



void setup()
{
  byte i;

  #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Begin Setup");
  #endif



  SERVERIP[0].IP = {192,168,68,93};
  SERVERIP[1].IP = {192,168,68,106};
  SERVERIP[2].IP = {192,168,68,100};

  #ifdef _DEBUG
    Serial.println("servers set");
  #endif

  #ifdef _USESOILRES
    pinMode(_USESOILRES,OUTPUT);  
  #endif


  Wire.begin(); 
  Wire.setClock(400000L);
  
  #ifdef _DEBUG
      Serial.println("oled begin");
  #endif

#ifdef _USESSD1306
  
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
        Serial.print(F("-  Unable to find BME680. Trying again in 5 seconds.\n"));
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
  oled.println("WiFi Starting.");
#endif
  WiFi.mode(WIFI_STA);
  WiFi.begin(ESP_SSID, ESP_PASS);
    #ifdef _DEBUG
        Serial.println("wifi begin");
    #endif

  #ifdef _DEBUG
    Serial.println();
    Serial.print("Connecting");
  #endif
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    #ifdef _DEBUG
    Serial.print(".");
    #endif
    #ifdef _USESSD1306
      oled.print(".");
    #endif
  }


    #ifdef _DEBUG
    Serial.println("");
    Serial.print("Wifi OK. IP is ");
    Serial.println(WiFi.localIP().toString());
    Serial.println("Connecting ArduinoOTA...");
    #endif


    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("WiFi OK.");
      oled.println("timesetup next.");      
    #endif

  setupTime();
  #ifdef _DEBUG
    Serial.println("setuptime done. OTA next.");
  #endif


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

    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("start time.");
    #endif



    //set the stoffregen time library with timezone and dst
    timeUpdate();



    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("start Server.");
    #endif

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/UPDATEALLSENSORREADS", handleUPDATEALLSENSORREADS);               
    server.on("/UPDATESENSORREAD",handleUPDATESENSORREAD);
    server.on("/SETTHRESH", handleSETTHRESH);               
    server.on("/LIST", handleLIST);
    server.on("/UPDATESENSORPARAMS", handleUPDATESENSORPARAMS);
    server.on("/NEXTSNS", handleNEXT);
    server.on("/LASTSNS", handleLAST);
    server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call
    server.begin();

    #ifdef _DEBUG
      Serial.println("HTML server started!");
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
    for ( i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }
    #ifdef _USEBARPRED
      for (byte ii=0;ii<24;ii++) {
        BAR_HX[ii] = -1;
      }
      LAST_BAR_READ=0; 
    #endif
  
  
  #ifdef _USEAHT
    while (aht21.begin() != true)
    {
      Serial.println(F("AHT2x not connected or fail to load calibration coefficient")); //(F()) save string to flash & keeps dynamic memory free
    }

  #endif

  #ifdef DHTTYPE
    #ifdef _DEBUG
        Serial.println("dht begin");
    #endif
    dht.begin();
  #endif


  #ifdef _USEBMP
    #ifdef _DEBUG
        Serial.println("bmp begin");
    #endif
    uint32_t t = millis();
    uint32_t deltaT = 0;
    while (!bmp.begin(0x76) and deltaT<15000) { //default address is 0x77, but amazon review says this is 0x76
      deltaT = millis()-t;
      #ifdef _USESSD1306
        oled.println("BMP failed.");
        #ifdef _DEBUG
            Serial.println("bmp failed.");
        #endif
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
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */


  #endif
  #ifdef _USEBME
      #ifdef _DEBUG
        Serial.println("bme begin");
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


  /*
  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  
  if(!LittleFS.begin()){
    #ifdef _DEBUG
      Serial.println("An Error has occurred while mounting LittleFS");
    #endif
    while (true) {
    }
  } else {
    #ifdef _DEBUG
      Serial.println("FileSys OK. Config Wifi");
    #endif

  }

*/




  for (i=0;i<SENSORNUM;i++) {
    Sensors[i].snsType=SENSORTYPES[i];

    Sensors[i].Flags = 0;
    if (bitRead(MONITORED_SNS,i)) bitWrite(Sensors[i].Flags,1,1);
    else bitWrite(Sensors[i].Flags,1,0);
    
    if (bitRead(OUTSIDE_SNS,i)) bitWrite(Sensors[i].Flags,2,1);
    else bitWrite(Sensors[i].Flags,2,0);

    switch (SENSORTYPES[i]) {
      case 1: //DHT temp
        #ifdef DHTTYPE
          Sensors[i].snsPin=DHTPIN;
          snprintf(Sensors[i].snsName,31,"%s_T", ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=2*60;          
        #endif
        break;
      case 2: //DHT RH
        #ifdef DHTTYPE
          Sensors[i].snsPin=DHTPIN;
          snprintf(Sensors[i].snsName,31,"%s_RH",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 25;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
          }
          Sensors[i].PollingInt=2*60;
          Sensors[i].SendingInt=5*60;
        #endif
        break;
      case 3: //soil
        #ifdef _USESOILCAP
          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soil",ARDNAME);
          Sensors[i].limitUpper = 290;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=120;
          Sensors[i].SendingInt=600;
        #endif
        #ifdef _USESOILRES
          Sensors[i].snsPin=SOILPIN;
          snprintf(Sensors[i].snsName,31,"%s_soilR",ARDNAME);
          Sensors[i].limitUpper = 2500;
          Sensors[i].limitLower = 0;
          Sensors[i].PollingInt=60*60;
          Sensors[i].SendingInt=60*60;
        #endif

        break;
      case 4: //AHT temp
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_T",ARDNAME);
          Sensors[i].limitUpper = 115;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
      case 5:
        #ifdef _USEAHT
          Sensors[i].snsPin=0;
          snprintf(Sensors[i].snsName,31,"%s_AHT_RH",ARDNAME);
          Sensors[i].limitUpper = 85;
          Sensors[i].limitLower = 25;
          Sensors[i].PollingInt=10*60;
          Sensors[i].SendingInt=10*60;
        #endif
        break;
  

      case 7: //dist
        Sensors[i].snsPin=0; //not used
        snprintf(Sensors[i].snsName,31,"%s_Dist",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = 10;
        Sensors[i].PollingInt=100;
        Sensors[i].SendingInt=100;
        break;
      case 9: //BMP pres
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 10: //BMP temp
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMP_t",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 11: //BMP alt
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=60000;
        Sensors[i].SendingInt=60000;
        break;
      case 12: //Bar prediction
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_Pred",ARDNAME);
        Sensors[i].limitUpper = 0;
        Sensors[i].limitLower = 0; //anything over 0 is an alarm
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        bitWrite(Sensors[i].Flags,3,1); //calculated
        bitWrite(Sensors[i].Flags,4,1); //predictive
        break;
      case 13: //BME pres
        Sensors[i].snsPin=0; //i2c
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1022; //normal is 1013
        Sensors[i].limitLower = 1009;
        Sensors[i].PollingInt=30*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 14: //BMEtemp
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMEt",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 35;
          }
          else {
            Sensors[i].limitUpper = 80;
            Sensors[i].limitLower = 65;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case 15: //bme rh
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_BMErh",ARDNAME);
          if (bitRead(OUTSIDE_SNS,i)) {
            Sensors[i].limitUpper = 85;
            Sensors[i].limitLower = 15;
          }
          else {
            Sensors[i].limitUpper = 65;
            Sensors[i].limitLower = 25;
          }
        Sensors[i].PollingInt=120;
        Sensors[i].SendingInt=5*60;
        break;
      case 16: //bme alt
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_alt",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = -5;
        Sensors[i].PollingInt=15*60*60;
        Sensors[i].SendingInt=15*60*60;
        break;
      case 17: //bme680
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_T",ARDNAME);
        Sensors[i].limitUpper = 90;
        Sensors[i].limitLower = 50;
        Sensors[i].PollingInt=15*60;
        Sensors[i].SendingInt=15*60;
        break;
      case 18: //bme680
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_RH",ARDNAME);
        Sensors[i].limitUpper = 100;
        Sensors[i].limitLower = 0;
        Sensors[i].PollingInt=15*60;
        Sensors[i].SendingInt=15*60;
        break;
      case 19: //bme680
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_hPa",ARDNAME);
        Sensors[i].limitUpper = 1020;
        Sensors[i].limitLower = 1012;
        Sensors[i].PollingInt=60*60;
        Sensors[i].SendingInt=60*60;
        break;
      case 20: //bme680
        Sensors[i].snsPin=0;
        snprintf(Sensors[i].snsName,31,"%s_gas",ARDNAME);
        Sensors[i].limitUpper = 1000;
        Sensors[i].limitLower = 50;
        Sensors[i].PollingInt=1*60;
        Sensors[i].SendingInt=1*60;
        break;

    }

    Sensors[i].snsID=find_sensor_count((String) ARDNAME,Sensors[i].snsType); //increment this if there are others of the same type

    Sensors[i].snsValue=0;
    Sensors[i].LastReadTime=0;
    Sensors[i].LastSendTime=0;  

  }


   //**************************************




  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println(WiFi.localIP().toString());
    oled.print(hour());
    oled.print(":");
    oled.println(minute());
  #endif
}




void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  timeClient.update();

    
  time_t t = now(); // store the current time in time variable t
  
  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second
    bool flagstatus=false;
    
    #ifdef _DEBUG
      Serial.printf("Time is %s. Sensor #1 is %s.   ",dateify(t,"hh:nn:ss"),Sensors[0].snsName);

    #endif

    for (byte k=0;k<SENSORNUM;k++) {
      flagstatus = bitRead(Sensors[k].Flags,0); //flag before reading value

      if (Sensors[k].LastReadTime + Sensors[k].PollingInt < t || abs((int) ((uint32_t) Sensors[k].LastReadTime - now()))>60*60*24 || Sensors[k].LastReadTime ==0) ReadData(&Sensors[k]); //read value if it's time or if the read time is more than 24 hours from now in either direction
      
      if ((Sensors[k].LastSendTime ==0 || Sensors[k].LastSendTime + Sensors[k].SendingInt < t || flagstatus != bitRead(Sensors[k].Flags,0)) || abs((int) ((uint32_t)Sensors[k].LastSendTime - now()))>60*60*24) SendData(&Sensors[k]); //note that I also send result if flagged status changed or if it's been 24 hours
    }
  }
  
  if (OldTime[1] != minute()) {
    OldTime[1] = minute();

    #ifdef _USESSD1306
      redrawOled();
    #endif

  }
  
  if (OldTime[2] != hour()) {
    OldTime[2] = hour();
    timeUpdate();

  }
  
  if (OldTime[3] != weekday()) {
    OldTime[3] = weekday();
  
  //sync time
    #ifdef _DEBUG
      t=now();
      Serial.print("Current time is ");
      Serial.print(hour(t));
      Serial.print(":");
      Serial.print(minute(t));
      Serial.print(":");
      Serial.println(second(t));
    #endif

  }
  
}




void redrawOled() {
  #if _USESSD1306

  byte j;

  oled.clear();
  oled.setCursor(0,0);
  oled.println(WiFi.localIP().toString());
  oled.print(hour());
  oled.print(":");
  oled.println(minute());

  if (_OLEDTYPE == &Adafruit128x64) {       
    for (byte j=0;j<3;j++) {
      oled.print(SERVERIP[j].IP.toString());
      oled.print(":");
      oled.print(SERVERIP[j].server_status);
      if (j!=2) oled.println(" ");    
    }    
  }

  for (j=0;j<SENSORNUM;j++) {
    if (bitRead(Sensors[j].Flags,0))   oled.print("!");
    else oled.print("O");
    oled.println(" ");
    #ifdef _USEBARPRED
      if (Sensors[j].snsType==12) {
        oled.println(WEATHER);
      } 
    #endif
  }
  #endif
  return;    
}

void pushDoubleArray(double arr[], byte N, double value) {
  for (byte i = N-1; i > 0 ; i--) {
    arr[i] = arr[i-1];
  }
  arr[0] = value;

  return ;

/*
  double arr2[N];
  arr2[0] = value;

  for (byte i = 0 ; i < N-1 ; i++)
    {
        arr2[i+1] = *(arr+i);
        *(arr+i) = arr2[i];
    }
    *(arr+N-1) = arr2[N-1];
*/
}


