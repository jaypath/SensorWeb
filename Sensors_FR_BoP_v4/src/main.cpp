//#define _DEBUG

#include "globals.hpp"
#include <ArduinoOTA.h>
#include "sensors.hpp"
#include "server.hpp"
#include "Devices.hpp"
#include "utility.hpp"
#include "timesetup.hpp"
#include "AddESPNOW.hpp"
#include "BootSecure.hpp"

#ifdef _USELED
#include "LEDGraphics.hpp"
#endif


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
//--------------------------------

#ifdef _USEBARPRED
  double BAR_HX[24];
  char WEATHER[24] = "Steady";
  uint32_t LAST_BAR_READ; //last pressure reading in BAR_HX
#endif



uint16_t mydeviceindex = 0;
byte OldTime[5] = {0,0,0,0,0};
extern Devices_Sensors Sensors;


#ifdef _USELED
  extern Animation_type LEDs;
#endif

//function declarations
void initSystem();
bool initConnectivity();
void initDisplay();
bool initTime();
void initOTA();
void initServer();
void handleESPNOWPeriodicBroadcast(uint8_t interval);
void handleStoreCoreData();

// ==================== SETUP HELPER FUNCTIONS ====================

/**
 * @brief Initialize system - boot secure, serial, and copy sensor configs
 */

 
void initSystem() {
  #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Begin Setup");
  #endif

  #ifdef _USE32
    // Load preferences securely from NVS (ESP32 only)
    BootSecure bs;
    int8_t boot_status = bs.setup();
    #ifdef _DEBUG
      Serial.printf("BootSecure setup status: %d\n", boot_status);
    #endif

    SerialPrint("Prefs.MYIP: " + String(Prefs.MYIP),true);

    if (Prefs.DEVICENAME[0] == 0) {
        snprintf(Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), MYNAME);
        Prefs.isUpToDate = false;        
    }


    // Copy persisted per-sensor config into Sensors  (intervals are used at runtime)
    for (int16_t si = 0; si < Sensors.getNumSensors(); ++si) {
      if (Sensors.isMySensor(si) == false) continue; //not valid/not mine

      SnsType* s = Sensors.getSensorBySnsIndex(si);

      int16_t prefs_index = Sensors.getPrefsIndex(s->snsType, s->snsID);

      s->SendingInt = Prefs.SNS_INTERVAL_SEND[prefs_index];
      // Polling and limits are used by ReadData/checks; keep in Prefs and reference via indices
    }
  #endif
  
  #ifdef _DEBUG
    Serial.println("System initialized");
  #endif
}

/**
 * @brief Initialize WiFi, I2C, and ESPNOW
 */
bool initConnectivity() {
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.print("WiFi Starting.");
  #endif
  
  Prefs.status = 0;
  connectWiFi(); //this is done async if 32, so can continue processing

  #ifdef _USEWIRE
  Wire.begin(); 
  Wire.setClock(400000L);
  #ifdef _DEBUG
    Serial.println("wire ok");
  #endif
  #endif

  #if defined(_USE32)
    initESPNOW();
  #endif

  #ifdef _USE32
    // By now wifi should have connected, but wait for it if not
    if (Prefs.status == 0) {
      do {
        #ifdef _USESSD1306
          oled.print(".");
        #endif
        #ifdef _DEBUG
          Serial.print(".");
        #endif
        delay(200);
      } while (Prefs.status == 0);
    }
  #endif
  
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("WiFi OK");
  #endif
  
  #ifdef _DEBUG
    Serial.println("Connectivity initialized");
    Serial.printf("WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    Serial.printf("Prefs.status: %d\n", Prefs.status);
    Serial.printf("HAVECREDENTIALS: %s\n", Prefs.HAVECREDENTIALS ? "true" : "false");
  #endif
  
  return true;
}

/**
 * @brief Initialize display (OLED)
 */
void initDisplay() {
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
    
    #ifdef _DEBUG
      Serial.println("Display initialized");
    #endif
  #endif
}


/**
 * @brief Initialize time using NTP
 */
bool initTime() {
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("start time.");
  #endif

  // Set time (setupTime calls timeClient.begin() internally)
  setupTime(); // Initialize time and DST (shared)

  I.ALIVESINCE = now();
  OldTime[0] = 100; // Arbitrary seconds that will trigger a second update
  OldTime[1] = 61;  // Arbitrary seconds that will trigger a second update
  OldTime[2] = hour();
  OldTime[3] = day();
  
  I.currentTime = now();
  
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("Time OK");
  #endif
  
  #ifdef _DEBUG
    Serial.println("Time initialized");
  #endif
  
  return true;
}

/**
 * @brief Initialize OTA (Over-The-Air updates)
 */
void initOTA() {
  #ifndef _USELOWPOWER
    #ifdef _USESSD1306
      oled.clear();
      oled.setCursor(0,0);
      oled.println("OTA Starting.");
    #endif
    
    #ifdef _DEBUG
      Serial.println("setuptime done. OTA next.");
    #endif

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(MYNAME);

    ArduinoOTA.onStart([]() {
      #ifdef _DEBUG
        Serial.println("OTA started...");
      #endif
      #ifdef _USESSD1306
        oled.clear();
        oled.setCursor(0,0);
        oled.printf("OTA data incoming...\n");
      #endif
      #ifdef _USELED
        // Set all LEDs to green to indicate OTA start
        for (byte j = 0; j < LEDCOUNT; j++) {
          LEDARRAY[j] = (uint32_t) 0 << 16 | 26 << 8 | 0; // green at 10% brightness
        }
        FastLED.show();
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
        if ((int)(progress) % 10 == 0) oled.print(".");   
      #endif
      #ifdef _USELED
        // Show OTA progress on LEDs as a filling bar
        for (byte j = 0; j < LEDCOUNT; j++) {
          LEDARRAY[LEDCOUNT - j - 1] = 0;
          if (j <= (double) LEDCOUNT * progress / total) {
            LEDARRAY[LEDCOUNT - j - 1] = (uint32_t) 64 << 16 | 64 << 8 | 64; // dim white
          }
        }
        FastLED.show();
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
        String strbuff = "Error[%u]: " + String(error) + " ";
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
      Serial.println("OTA initialized");
    #endif
  #endif
}

/**
 * @brief Initialize web server and routes
 */
void initServer() {
  #ifdef _DEBUG
    Serial.println("set up HTML server...");
  #endif

  setupServerRoutes(); // Set up all server routes
  server.begin();
  
  #ifdef _DEBUG
    Serial.println("HTML server started!");
    Serial.printf("Server IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Server port: 80\n");
  #endif
  
  #ifdef _USESSD1306
    oled.clear();
    oled.setCursor(0,0);
    oled.println("Server OK");
  #endif
}

// ==================== ESPNOW HELPER FUNCTIONS ====================

/**
 * @brief Handle periodic ESPNow presence broadcast (every N minutes).
 * @param interval Broadcast interval in minutes (e.g., 5 for every 5 minutes)
 */
void handleESPNOWPeriodicBroadcast(uint8_t interval) {    
  if (minute() % interval == 0 && I.ESPNOW_LAST_OUTGOINGMSG_TIME!=I.currentTime) {        
      // ESPNow does not require WiFi connection; always broadcast
      broadcastServerPresence();
  }
}

/**
 * @brief Store core data (Prefs and Screen Info) to NVS/SD when needed
 */
void handleStoreCoreData() {
  if (!Prefs.isUpToDate) { 
    Prefs.isUpToDate = true;
    BootSecure::setPrefs();        
  }

  #ifdef _USESDCARD
  if (!I.isUpToDate && I.lastStoreCoreDataTime + 300 < I.currentTime) { //store if more than 5 minutes since last store
    I.isUpToDate = true;
    storeScreenInfoSD();
  }
  #endif
}

// ==================== MAIN SETUP FUNCTION ====================

void setup()
{
  #ifdef _USESERIAL
    Serial.begin(115200);
  #endif
  
  SerialPrint("Begin Setup",true);
  
  #ifdef _USE32
  // Initialize watchdog timer for ESP32
  esp_task_wdt_init(30, true); // 30 second timeout, panic on timeout
  esp_task_wdt_add(NULL); // Add current task to watchdog
  #endif
  
  // Initialize system (boot secure, serial, prefs)
  initSystem();
  
  // Initialize display (OLED)
  initDisplay();
  
  SerialPrint("Init Connectivity",true);
    // Initialize connectivity (WiFi, I2C, ESPNOW) - do this after so server routes are known to softap
    initConnectivity();
  
  // Initialize time
  initTime();
  
  // Initialize OTA
  initOTA();
  
  // Initialize server and routes
  initServer();


  // Initialize hardware sensors (BME, BMP, DHT, etc.)
  initHardwareSensors();

  // Initialize LED strip (if enabled)
  #ifdef _USELED
    FastLED.addLeds<WS2813, _USELED, GRB>(LEDARRAY, LEDCOUNT).setCorrection(TypicalLEDStrip);
    LEDs.LED_animation_defaults(1);
    LEDs.LEDredrawRefreshMS = 20;
    LEDs.LED_set_color(255, 255, 255, 255, 255, 255); // default is white
    #ifdef _DEBUG
      Serial.println("LED strip initialized");
    #endif
  #endif

  #ifdef _USESSD1306
    oled.clear();
    oled.printf("SETUP COMPLETE\n");
  #endif
  
  #ifdef _DEBUG
    Serial.println("Setup complete!");
  #endif
}


void loop() {
  esp_task_wdt_reset();

  updateTime();
  
  ArduinoOTA.handle();
  server.handleClient();
  
  // Update LED animations (if enabled)
  #ifdef _USELED
    LEDs.LED_update();
  #endif
  
  // Note: I.currentTime is updated by updateTime()

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
        for (int16_t si = 0; si < Sensors.getNumSensors(); ++si) {
          if (Sensors.isMySensor(si) == false) continue; //not valid/not mine
          SnsType* temp = Sensors.getSensorBySnsIndex(si);
          ReadData(&temp);
            
          

          SendData(&temp);
          
        }

      #ifdef _DEBUG
        Serial.printf( "Returning to sleep.\n");
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


    for (int16_t si = 0; si < Sensors.getNumSensors(); ++si) {
      
      if (Sensors.isMySensor(si) == false) continue; //not valid/not mine

      SnsType* temp = Sensors.getSensorBySnsIndex(si);
      if (!temp) {
        SerialPrint("ERROR: Invalid sensor pointer at index " + String(si), true);
        continue;
      }

      // Reset watchdog before sensor operations
      esp_task_wdt_reset();
      
      try {
        int readResult = ReadData(temp);
        if (readResult > 0) {
          //SerialPrint("ReadData: temp->snsType: " + (String) temp->snsType,true);
          #ifdef _USELED
          if (temp->snsType == 3) { // soil sensor
            SerialPrint("LED_set_color_soil: temp->snsType: " + (String) temp->snsType,true);
            LEDs.LED_set_color_soil(temp);
          }
          #endif
        }
        
        // Reset watchdog before sending data
        esp_task_wdt_reset();
        
        bool sendResult = SendData(temp);
        if (!sendResult && readResult > 0) {
          // Only log if we had new data but failed to send
          SerialPrint("WARNING: Failed to send data for sensor " + String(temp->snsName), true);
        }
      } catch (...) {
        SerialPrint("ERROR: Exception in sensor processing for index " + String(si), true);
        // Continue with next sensor instead of crashing
      }
    }

  }
  
  if (OldTime[1] != minute()) {

    if (I.makeBroadcast) {
      broadcastServerPresence(true);
      I.makeBroadcast = false;
    }

    if (Prefs.status==0) {
      WiFi.reconnect(); //try restarting wifi
      
      #ifdef _USEESPNOWFORWIFI
      // If WiFi is still down and we have a known server, request WiFi password via ESPNOW
      if (WiFi.status() != WL_CONNECTED && I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC != 0) {
        static uint32_t lastPwdRequestMinute = 0;
        if (minute() != lastPwdRequestMinute) { // Only request once per minute
          lastPwdRequestMinute = minute();
          uint8_t serverMAC[6];
          uint64ToMAC(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC, serverMAC);
          requestWiFiPassword(serverMAC, nullptr);
          #ifdef _DEBUG
          Serial.printf("WiFi down - requesting password via ESPNOW from server MAC: %s\n", MACToString(serverMAC).c_str());
          #endif
        }
      }
      #endif
    }

    OldTime[1] = minute();

    if (OldTime[1]%10==0) {
    
      #ifdef _USE32
      size_t freeHeap = ESP.getFreeHeap();
      size_t minFreeHeap = ESP.getMinFreeHeap();
        
      // If memory is critically low, restart
      if (freeHeap < 10000) { // Less than 10KB free
        SerialPrint("CRITICAL: Low memory detected, restarting system", true);
        delay(1000);
        ESP.restart();
      }
      
      // If minimum free heap is very low, log warning
      if (minFreeHeap < 5000) { // Less than 5KB minimum
        SerialPrint("WARNING: Memory fragmentation detected", true);
        ESP.restart();
      }
      #endif
    
    }

    #ifdef _DEBUG
    Serial.printf("\nTime is %s\n", dateify(I.currentTime,"hh:nn:ss"));
    #endif

    // Handle periodic ESPNOW broadcasts (every 10 minutes for sensors)
    handleESPNOWPeriodicBroadcast(10);
    
    // Store core data to NVS/SD when needed
    handleStoreCoreData();
    
    #ifdef _USESSD1306
      redrawOled();
    #endif

  }


  if (OldTime[2] != hour()) {
      
  
    #ifdef _DEBUG
    Serial.printf( "\nNew hour... TimeUpdate running. \n");
    #endif
  
    checkDST();

    //expire 24 hour old readings handled via DeviceStore timestamps

    OldTime[2] = hour();

  }
  
  if (OldTime[3] != day()) {
    //once per day
    #ifdef REBOOTDAILY
      ESP.restart();
    #endif

    // Check for timezone updates once per day
    checkTimezoneUpdate();

    OldTime[3] = day();

    checkDST();
    //check timezone update
    checkTimezoneUpdate();
    
    I.ESPNOW_SENDS = 0;
    I.ESPNOW_RECEIVES = 0;

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
