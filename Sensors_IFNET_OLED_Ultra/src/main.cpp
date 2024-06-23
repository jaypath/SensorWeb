//#define DEBUG_

#include "ArduinoOTA.h"

#include <Wire.h>

  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C

#include <ArduinoOTA.h>


  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>

SSD1306AsciiWire oled;


//Code to draw to the screen
  //#define _OLEDTYPE &Adafruit128x64
  #define _OLEDTYPE &Adafruit128x32
  #define _OLEDINVERT 0


typedef uint8_t u8;
typedef int16_t i16;
// typedef uint16_t u16;
typedef uint32_t u32;

const u8 SENSORTYPES[SENSORNUM] = {ultrasonic_dist, DHT_temp, DHT_rh};
const u8 MONITORED_SNS = 255;
const u8 OUTSIDE_SNS = 0;


#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here




void setup()
{

  
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


  char msg[10];



  Wire.begin(); // Initalize Wire library

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

  WiFi.begin(ESP_SSID, ESP_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    
    #ifdef DEBUG_
    Serial.print(".");
    #endif
  }

  arduino_IP = WiFi.localIP();
  sprintf(msg,"IP:%i",arduino_IP[3]);

  delay(3000);



   ArduinoOTA.setHostname("thisisatemp");
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG_
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
  });
    ArduinoOTA.begin();

  #ifdef DEBUG_
    Serial.println("init done...");

  #endif


  #ifdef DEBUG_
    Serial.println("Set up TimeClient...");
  #endif



  #ifdef DEBUG_
    Serial.println("Setup end");
  #endif
  delay(1000);

}



void loop()
{
  u32 current_millis = millis();
  u32 n = now();
  char msg[10];

if (millis()%1000==0) {
  oled.clear();

  oled.printf("Hello %d",millis());

}

}
