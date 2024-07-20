//#define DEBUG_

#include "ArduinoOTA.h"

#include <Wire.h>

  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C

#include <ArduinoOTA.h>

 #include <Adafruit_BMP280.h>
  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>

  #include <MPU6500_WE.h>

#define MPU6500_ADDR 0x68
MPU6500_WE mpu = MPU6500_WE(MPU6500_ADDR);





SSD1306AsciiWire oled;

Adafruit_BMP280 bmp; // I2C


//Code to draw to the screen
  #define _OLEDTYPE &Adafruit128x64
//  #define _OLEDTYPE &Adafruit128x32
  #define _OLEDINVERT 0


typedef uint8_t u8;
typedef int16_t i16;
// typedef uint16_t u16;
typedef uint32_t u32;

  String MSG;
String HEADER2 = ""; //line 2 of header
u32 TIMESINCE = 0;


struct SensorStruct {
  byte MYID;
  bool  HASWIFI;
  bool HASMPU;
  bool HASBMP;
  u32 LASTSCREENDRAW;
  byte SCREENREFRESH;
  u32 LASTBUTTON;
  
};

#define _NUMREADINGS 6
struct MPUStruct {
  uint32_t LASTREAD;
  byte RATE_MS; //ms between readings
  uint8_t MPUREADING; //bit 0 = R, 1 = L, 2 = u, 3 = d
  //byte INDEX; //index to mpu values
  //float VALS_LR[_NUMREADINGS]; //store last 6 LR values
  //float VALS_UD[_NUMREADINGS];
  uint8_t LOCKOUT_MS; //ms to not read after a value has been registered. This is needed because a jerk has both + and - accel, just want first one
  float MPUACC_y ; //cut off for acceleration to register
  float MPUACC_x ;
  int TEMPERATURE;
};

SensorStruct INFO;
MPUStruct MPUINFO;


#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here



void printLine(String s) {
    oled.print(s.c_str());
    oled.clearToEOL();
    oled.println();
}
void printHeader() {
  oled.setFont(System5x7);
  oled.set1X();
  oled.setCursor(0,0);

  if (INFO.HASWIFI)   {
    INFO.MYID = WiFi.localIP()[3];
    oled.print("ID:");
    oled.print(INFO.MYID);
  }  else {
    INFO.MYID=0;
    oled.print("Wifi-");
  }



  if (INFO.HASMPU) {
    oled.print(" Acc+");
    
  }  else {
    oled.print(" Acc-");    
  }

  if (INFO.HASBMP) {
    oled.print(" BMP+");
    
  }  else {
    oled.print(" BMP-");    
  }

  oled.clearToEOL();
  oled.println();

  oled.print(HEADER2.c_str());
  oled.clearToEOL();
  oled.println(); //now we are at the top of the print area


}


void setup()
{

  INFO.HASBMP = false;
  INFO.HASMPU = false;
  INFO.HASWIFI = false;
  INFO.SCREENREFRESH = 100; //ms for screen refresh
  
  Wire.begin(); // Initalize Wire library
  Wire.setClock(400000L);

  #if RST_PIN >= 0
    oled.begin(&Adafruit128x64, I2C_OLED, RST_PIN);
  #else // RST_PIN >= 0
    oled.begin(&Adafruit128x64, I2C_OLED);
  #endif // RST_PIN >= 0


  oled.setFont(System5x7);
  oled.set1X();
  oled.clear();
  oled.setCursor(0,0);



  #ifdef DEBUG_
    Serial.begin(115200);
    Serial.println("start");
  #endif

  #ifdef DEBUG_
    Serial.println("try wifi...");
  #endif

  #ifdef DEBUG_
    Serial.println("try screen...");

  #endif

  oled.println("NICE! Screen works!");
  delay(500);
  MSG = "Trying to\nestablish WIFI...";
  oled.println(MSG.c_str());
  byte r = oled.row();
  
  
  WiFi.begin(ESP_SSID, ESP_PASS);
  TIMESINCE = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-TIMESINCE<5000)
    {
    delay(200);
    
    #ifdef DEBUG_
    Serial.print(".");
    #endif
    oled.setCursor(r,0);

    oled.print("No WIFI...");
    oled.clearToEOL();
    oled.println();
  }

  if(WiFi.status() == WL_CONNECTED) {
    INFO.HASWIFI = true;
    INFO.MYID = WiFi.localIP()[3];
  }

  if (!bmp.begin(0x76) ) {
    INFO.HASBMP=false;
  } else {
    INFO.HASBMP=true;
   /* Default settings from datasheet. */
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  }

  oled.clear();
  oled.setCursor(0,0);

  
  if (INFO.MYID>0) {
//    oled.print("Setting up OTA");

    ArduinoOTA.setHostname(MSG.c_str());
    ArduinoOTA.onStart([]() {
      #ifdef _DEBUG
      Serial.println("OTA started");
      #endif
      HEADER2 = "OTA started";
      printHeader();
    });
    ArduinoOTA.onEnd([]() {
      #ifdef _DEBUG
      Serial.println("OTA End");
      #endif
      HEADER2 = "OTA End";
      printHeader();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      #ifdef _DEBUG
          Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
      #endif
      HEADER2 = "Progress: " + String (100*progress / total);
      printHeader();      
    });
    ArduinoOTA.onError([](ota_error_t error) {
      #ifdef DEBUG_
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      #endif
        if (error == OTA_AUTH_ERROR) HEADER2 = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) HEADER2 = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) HEADER2 = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) HEADER2 = "Receive Failed";
        else if (error == OTA_END_ERROR) HEADER2 = "End Failed";

        printHeader();
    });
      ArduinoOTA.begin();
      oled.println("... OK");
  } else {
    oled.println(" failed");
  }


    INFO.HASMPU = false;
    if (!mpu.init()) {  // if failed no mpu
      INFO.HASMPU = false;
    } else {
      INFO.HASMPU = true;
    }

    if(INFO.HASMPU) {
      MPUINFO.LASTREAD = 0;
//      MPUINFO.INDEX=0;
      MPUINFO.LOCKOUT_MS = 175; //do not register another reading for this long
      MPUINFO.MPUACC_x= 0.0000015*16384;
      MPUINFO.MPUACC_y = 0.0000015*16384;
      MPUINFO.RATE_MS = 20;
      

      delay(1000);
      mpu.autoOffsets();

      mpu.enableGyrDLPF();
      mpu.setGyrDLPF(MPU6500_DLPF_6);
      mpu.setSampleRateDivider(5);
      mpu.setGyrRange(MPU6500_GYRO_RANGE_250);
      mpu.setAccRange(MPU6500_ACC_RANGE_2G); //16384 units/g in the +/- 2g range
      mpu.enableAccDLPF(true);
      mpu.setAccDLPF(MPU6500_DLPF_6);
      delay(200);
    }

  HEADER2 = "";
  printHeader();  

  #ifdef DEBUG_
    Serial.println("Set up TimeClient...");
  #endif

  INFO.LASTSCREENDRAW = millis();

  INFO.LASTBUTTON = 0;
  pinMode(0,INPUT_PULLUP);
}



float averageArrayF(float* t,byte n,bool init) {
  float avgF = 0;
  for (byte j=0;j<n;j++) {
    avgF+=t[j];
    if (init) t[j] = 0;
  }
  return avgF/n;

}
void loop()
{
  ArduinoOTA.handle();

  u32 m = millis();

  if (digitalRead(0) == LOW) {
    INFO.LASTBUTTON = m;    
  }
//  HEADER2 = "Button@" + (String) ((m-INFO.LASTBUTTON)/1000) + "s ago";
  

  if (INFO.HASMPU && (m-MPUINFO.LASTREAD) > MPUINFO.RATE_MS) {
    if (m-MPUINFO.LASTREAD > MPUINFO.LOCKOUT_MS) {       
      xyzFloat gValue = mpu.getGValues();
      xyzFloat gyr = mpu.getGyrValues();
      float temp = mpu.getTemperature();
      float resultantG = mpu.getResultantG(gValue);

      if ((float) abs(gValue.y) > MPUINFO.MPUACC_y || (float) abs(gValue.x) > MPUINFO.MPUACC_x) { //that equates to 0.000001*4g 
        MPUINFO.LASTREAD = m;
        MPUINFO.MPUREADING=0;
        if ((float) abs(gValue.y) >  abs(gValue.x) ) { //LR movement
          if (gValue.y>0) {
            bitWrite(MPUINFO.MPUREADING,1,1);
          }        else {
            bitWrite(MPUINFO.MPUREADING,0,1);
          }
        } else {
          if (gValue.x>0) {
            bitWrite(MPUINFO.MPUREADING,2,1);
          }        else {
            bitWrite(MPUINFO.MPUREADING,3,1);
          }
        }
      }
      
      MPUINFO.TEMPERATURE= ((int) temp*9/5+32);
  
    }
  }

  if (m > INFO.LASTSCREENDRAW + INFO.SCREENREFRESH) {
    if (INFO.HASBMP) {
      HEADER2 = (String) (bmp.readPressure()/100); //in hPa
    }
    if (INFO.HASMPU) {
      HEADER2 = "T=" + (String) (int) MPUINFO.TEMPERATURE + "F";
      
      if ((uint32_t) m < MPUINFO.LASTREAD+500) {        
        if (bitRead(MPUINFO.MPUREADING,0))       HEADER2 += " RIGHT";
        else if (bitRead(MPUINFO.MPUREADING,1)) HEADER2 += "LEFT";

        if (bitRead(MPUINFO.MPUREADING,2)) HEADER2 += "UP";
        else if (bitRead(MPUINFO.MPUREADING,3)) HEADER2 += "DOWN";
      }
    }

      for (byte j=0;j<8;j++) printLine("");

      

  }

  printHeader();
  HEADER2 = "";
}

