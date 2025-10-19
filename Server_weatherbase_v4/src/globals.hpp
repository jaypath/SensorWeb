#ifndef GLOBALS_HPP
#define GLOBALS_HPP
#define GLOBALS_HPP_INCLUDED

#define _USETFT
#define _USESERIAL
#define _USEWEATHER
#define _USEBATTERY
#define _USEGSHEET
#define _USESDCARD
//#define _ISPERIPHERAL 1

#define TIMEZERO 1735689600 //2025-01-01 00:00:00
#define NUMDEVICES 50 //max number of devices to track
#define NUMSENSORS 100 //number of sensors to track 
#define MYTYPE 100
#define SENSORNUM 0 //number of sensors I have

#ifdef _ISPERIPHERAL
#define SENSORHISTORYSIZE 150 //number of data points to store for each sensor
#endif
//------------------
//add libraries here

  #include <Arduino.h>
  #include <String.h>
  #include <TimeLib.h>
  #include <WiFi.h>
  #include <time.h>
  #include <SPI.h>
  #include <SD.h>
  #include <string>
  #include <LovyanGFX.hpp>

  #include <ArduinoOTA.h>
  #include <HTTPClient.h>
  #include <ArduinoJson.h>
  #include <NTPClient.h>
  #include <WiFiUdp.h>
  #include <esp_task_wdt.h>
  #include <esp_now.h>
  #include <esp_wifi.h>
  


#ifdef _USEGSHEET
#include "GsheetUpload.hpp"

#endif

#ifdef _ISPERIPHERAL
  const uint8_t SENSORTYPES[SENSORNUM] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //can have zeros for unused sensors, but must init all to the correct sensortypes
  #define MONITORINGSTATES         {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1} //for monitored, 0 = not monitored (not sent), 1 = monitored non-critical (take no action and expiration is irrelevant), 2 = monitored moderately critical (flag if out of bounds, but not if expired), 3 = monitored critically critical (flag if out of bounds or expired)
  #define OUTSIDESTATES            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //from R to L each bit represents a sensor, 1 means outside, 0 means not outside
  #define INTERVAL_POLL            {30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30} //seconds between polls
  #define INTERVAL_SEND            {120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120} //seconds between sends
  #define LIMIT_MAX                {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //store max values for each sensor in NVS
  #define LIMIT_MIN                {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} //store min values for each sensor in NVS
  //#define _ISHVAC 1
  /*sens types
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - 
//7 - distance, HC-SR04 or tfluna or tof 
//8 - motion, PIR, etc.
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
98 - clock
99 = any numerical value
100+ is a server type sensor, to which other sensors will send their data
101 - weather display server with local persistent storage (ie SD card)
102 = any weather server that has no persistent storage
103 = any server with local persistent storage (ie SD card) that uploads data cloud storage
104 = any server without local persistent storage that uploads data cloud storage
*/


  //uncomment the devices attached!
  //note: I2c on esp32 is 22=scl and 21=sda; d1=scl and d2=sda on nodemcu
    //#define _USEDHT 1 //specify DHT pin
    //#define _USEAHT 1 //specify AHT I2C
    //#define _USEAHTADA 0x38 // with bmp combined
    //#define _USEBMP  0x77 //set to 0x76 for stand alone bmp, or 0x77 for combined aht bmp
    //#define _USEBME 1 //specify BME I2C
    //#define _USEBME680_BSEC 1 //specify BME680 I2C with BSEC
    //#define _USEBME680 1 //specify BME680 I2C
    //#define _USEPOWERPIN 12 //specify the pin being used for power
    //#define _USESOILMODULE A0 //specify the pin being read for soil moisture
    //#define _USESOILRES 1 //use soil resistnace, pin and power specified later
    //#define _USESOILCAP 1 //specify soil capacitance sensor pin
    //#define _USEBARPRED 1 //specify barometric pressure prediction
    //#define _USEHCSR04 1 //distance
    //#define _USETFLUNA 1 // distance
    //#define _USESSD1306  1
    //#define _USELIBATTERY  A0 //set to the pin that is analogin
    //#define _USESLABATTERY  A0 //set to the pin that is analogin
    //#define _USELOWPOWER 36e8 //microseconds must also have _USEBATTERY
    //#define _USELEAK 1 //specify the pin being used for leak detection
    //binary switches
    //#define _CHECKAIRCON 1
    //#define _CHECKHEAT 1 //check which lines are charged to provide heat
    //#define _USEMUX //use analog input multiplexor to allow for >6 inputs
    //#define _USECALIBRATIONMODE 6 //testing mode



    #ifdef _ISHVAC
    
        #ifdef _CHECKAIRCON 
        //const uint8_t DIO_INPUTS=2; //two pins assigned
        const uint8_t DIOPINS[4] = {35,34,39,36}; //comp DIO in,  fan DIO in,comp DIO out, fan DIO out
        #endif

        #ifdef _CHECKHEAT
        //  const uint8_t DIO_INPUTS=6; //6 sensors
          #ifdef _USEMUX
            //using CD74HC4067 mux. this mux uses 4 DIO pins to choose one of 16 lines, then outputs to 1 ESP pin
            //36 is first pin from EN, and the rest are consecutive
            const uint8_t DIOPINS[5] = {32,33,25,26,36}; //first 4 lines are DIO to select from 15 channels [0 is 0 and [1111] is 15]  and 5th line is the reading (goes to an ADC pin). So 36 will be Analog and 32 will be s0...

          #else
            const uint8_t DIOPINS[6] = {36, 39, 34, 35,32,33}; //ADC bank 1, starting from pin next to EN
          #endif
          const String HEATZONE[6] = {"Office","MastBR","DinRm","Upstrs","FamRm","Den"};

        #endif

        #if defined(_CHECKHEAT) || defined(_CHECKAIRCON)
          #define _HVACHXPNTS 24
        #endif
    #endif
#endif  


#define RESET_ENUM_TO_STRING(enum_val) (#enum_val)

//potential reset issues
typedef enum {
  RESET_DEFAULT, //reset due to some unknown fault, where the reset state could not be set (probably triggered by a hang and watchdog timer kicked)
  RESET_SD, //reset because of SD card ... this can't be saved so it's actually pointless :)
  RESET_WEATHER, //reset because couldn't get weather
  RESET_USER, //user reset
  RESET_OTA, //ota reset
  RESET_WIFI, //no wifi so reset
  RESET_TIME, //time based reset 
  RESET_UNKNOWN, //unexpected reboot/crash detected
  RESET_NEWWIFI //new wifi credentials
  } RESETCAUSE;

  
  //errors in operation
  typedef enum {
    ERROR_UNDEFINED,   
    ERROR_HTTPFAIL_BOX, //failed http request for grid  coordinates
    ERROR_HTTPFAIL_GRID, //failed http request for grid
    ERROR_HTTPFAIL_HOURLY, //failed http request for hourly
    ERROR_HTTPFAIL_DAILY, //failed http request for DAILY
    ERROR_HTTPFAIL_SUNRISE, //failed http request for sunrise
    ERROR_JSON_BOX, //failed http request for grid  coordinates
    ERROR_JSON_GRID, //failed json parse request for grid
    ERROR_JSON_HOURLY, //failed json parse request for hourly
    ERROR_JSON_DAILY, //failed json parse request for DAILY
    ERROR_JSON_SUNRISE, //failed json parse request for sunrise
    ERROR_SD_LOGOPEN,
    ERROR_SD_LOGWRITE, //could not write to errorlog
    ERROR_SD_FILEWRITE,
    ERROR_SD_FILEREAD,
    ERROR_SD_DEVICESENSORSNOSNS, //device-sensor SNS not found
    ERROR_SD_DEVICESENSORSNODEV, //device-sensor DEV not found
    ERROR_SD_DEVICESENSORSSIZE, //tried to read devicesensors but size was wrong
    ERROR_SD_DEVICESENSORSOPEN, //could not open devicesensors
    ERROR_SD_DEVICESENSORSWRITE, //could not write devicesensors
    ERROR_SD_SCREENFLAGSWRITE, //could not write I
    ERROR_SD_SCREENFLAGSREAD, //could not read I
    ERROR_SD_SCREENFLAGSSIZE,  //screenflags I file was the Wrong size
    ERROR_SD_RETRIEVEDATAPARMS, //parameter error for retreiving data
    ERROR_SD_RETRIEVEDATAMISSING, //no data in time range
    ERROR_SD_OPENDIR, //could not open directory
    ERROR_SD_FILEDEL, //could not delete file
    ERROR_DEVICE_ADD, //could not add a device
    ERROR_SENSOR_ADD, //could not add a sensor
    ERROR_ESPNOW_SEND, //could not send ESPNow message
    ERROR_SD_WEATHERDATAWRITE, //could not write weather data
    ERROR_SD_WEATHERDATAREAD, //could not read weather data
    ERROR_SD_WEATHERDATASIZE, //weather data file was the wrong size
    ERROR_SD_GSHEETINFOWRITE, //could not write GsheetInfo data
    ERROR_SD_GSHEETINFOREAD, //could not read GsheetInfo data
    ERROR_JSON_GEOCODING, //failed json parse request for geocoding
    ERROR_GSHEET_CREATE, //failed to create spreadsheet
    ERROR_GSHEET_UPLOAD, //failed to upload data to spreadsheet
    ERROR_GSHEET_DELETE, //failed to delete spreadsheet
    ERROR_GSHEET_FIND, //failed to find spreadsheet
    ERROR_GSHEET_HEADERS, //failed to create headers
    ERROR_FAILED_PREFS, //failed storage or retrieval of Prefs
    ERROR_FAILED_STRUCTCORE, //faileed storage or retrieval of struct core
    ERROR_FAILED_ENCRYPTION, //failed to encrypt data
    ERROR_FAILED_DECRYPTION //failed to decrypt data
  } ERRORCODES;


  #ifdef _ISPERIPHERAL
  //create a struct type to hold sensor history
  struct STRUCT_SNSHISTORY {
    int16_t sensorIndex[SENSORNUM];
    uint8_t HistoryIndex[SENSORNUM];
    uint32_t TimeStamps[SENSORNUM][SENSORHISTORYSIZE] = {0};
    double Values[SENSORNUM][SENSORHISTORYSIZE] = {0};
    uint8_t Flags[SENSORNUM][SENSORHISTORYSIZE] = {0};
  };
  #endif

  struct STRUCT_KEYS {
    uint8_t ESPNOW_KEY[17]; // espnow PMK key, only 16 bytes are used
  };

  struct STRUCT_PrefsH {        
    bool isUpToDate; // Prefs has been saved to memory
    
    char WIFISSID[33];
    char WIFIPWD[65];
    uint32_t SSIDCRC;
    uint32_t PWDCRC;
    uint64_t PROCID; //processor ID, same as MACID for most esp32
    uint32_t LASTBOOTTIME;
    
    char DEVICENAME[30]; // Device name (moved from Screen.SERVERNAME)

    STRUCT_KEYS KEYS;

    // --- Network configuration (merged from WiFi_type) ---
    uint32_t DHCP;  // 4 bytes
    uint32_t GATEWAY; // 4 bytes
    uint32_t DNS; // 4 bytes
    uint32_t DNS2; // 4 bytes
    uint32_t SUBNET; // 4 bytes

    bool HAVECREDENTIALS = false; // Whether WiFi credentials are available

    //time zone info
    int32_t TimeZoneOffset; //offset from UTC in seconds, on standard time rather than daylight time
    uint8_t DST; //0 = no DST, 1 = DST in this locale (if DST=1, then use the DSToffset, which will be 3600 or 0)
    int16_t DSTOffset; //offset from UTC in seconds, on standard time rather than daylight time
    uint8_t DSTStartMonth; //month of DST start
    uint8_t DSTStartDay; //day of DST start
    uint8_t DSTEndMonth; //month of DST end
    uint8_t DSTEndDay; //day of DST end


    #ifdef _USEWEATHER
    double LATITUDE;
    double LONGITUDE;
    #endif

    #ifdef _ISPERIPHERAL
    double SNS_LIMIT_MAX[SENSORNUM] = LIMIT_MAX; //store max values for each sensor in NVS
    double SNS_LIMIT_MIN[SENSORNUM] = LIMIT_MIN; //store min values for each sensor in NVS
    uint8_t SNS_MONITORED[SENSORNUM] = MONITORINGSTATES;
    uint8_t SNS_OUTSIDE[SENSORNUM] = OUTSIDESTATES;
    uint16_t SNS_INTERVAL_POLL[SENSORNUM] = INTERVAL_POLL;
    uint16_t SNS_INTERVAL_SEND[SENSORNUM] = INTERVAL_SEND;
    #endif
  };
  
  
  struct STRUCT_CORE {
      uint32_t lastServerStatusUpdate;
      bool makeBroadcast=false;
      time_t lastStoreCoreDataTime;
      bool isUpToDate;  // Core has been saved to memory
      RESETCAUSE resetInfo;
      time_t lastResetTime;
      byte rebootsSinceLast=0;
      time_t ALIVESINCE;
      uint8_t wifiFailCount;
      time_t currentTime;

      
      uint8_t currentMinute; //current minute of the day, used to ensure clock is drawn correctly
      
      #ifdef _USETFT
      byte CLOCK_Y = 105;
      byte HEADER_Y = 30;
      uint32_t lastHeaderTime=0; //last time header was drawn
      uint32_t lastWeatherTime=0; //last time weather image was drawn
      uint32_t lastCurrentConditionTime; //last time current condition was drawn
      uint32_t lastClockDrawTime=0; //last time clock was updated, whether flag or not
      uint32_t lastFutureConditionTime=0; //last time future condition was drawn
      uint32_t lastFlagViewTime=0; //last time flags were drawn, instead of weather image
      uint8_t cycleHeaderMinutes = 30; //how many seconds to show header?
      uint8_t cycleCurrentConditionMinutes = 10; //how many minutes to show current condition?
      uint8_t cycleWeatherMinutes = 10; //how many minutes to show weather values?
      uint8_t cycleFutureConditionsMinutes = 10; //how many minutes to show future conditions?
      uint8_t cycleFlagSeconds = 3; //how many seconds to show flag values?
      uint8_t IntervalHourlyWeather = 2; //hours between daily weather display

      uint16_t touchX;
      uint16_t touchY;
      uint8_t alarmIndex;
      uint8_t ScreenNum;
      uint8_t oldScreenNum;
      uint8_t screenChangeTimer = 0; //seconds before screen changes back to main screen
      
      #endif
  
      #ifdef _USEWEATHER
      int8_t currentTemp;
      int8_t Tmax;
      int8_t Tmin;
      uint8_t localWeatherIndex; //index of outside sensor
      int8_t lastCurrentTemp; //last current temperature
      #endif
  
      #ifdef _USEBATTERY
      uint8_t localBatteryIndex; //index of battery
      int8_t localBatteryLevel;
      #endif
  
      //espnow info
      uint8_t TEMP_AES[32]; // [0..15]=key, [16..31]=IV
      uint32_t TEMP_AES_TIME; // unixtime of TEMP_AES creation
      uint64_t TEMP_AES_MAC; // expected server MAC for WiFi PW response
      
      
      
    //for messages received
    uint16_t ESPNOW_RECEIVES; //number of ESPNow receives since midnight
    uint8_t ESPNOW_LAST_INCOMINGMSG_STATE; //-1 if receive failure, 0 if indeterminate, 1 if send success
    uint32_t ESPNOW_LAST_INCOMINGMSG_TIME; // time of last server (type 100) broadcast. Will be 0 if no server or have registered the server
      uint64_t ESPNOW_LAST_INCOMINGMSG_FROM_MAC; // MAC of last ESPnow message sender
      IPAddress ESPNOW_LAST_INCOMINGMSG_FROM_IP;
      uint8_t ESPNOW_LAST_INCOMINGMSG_FROM_TYPE; // type of last ESPnow message sender
      uint8_t ESPNOW_LAST_INCOMINGMSG_TYPE; // type of last ESPnow message sender
      char ESPNOW_LAST_INCOMINGMSG_PAYLOAD[64]; // text portion of payload of last ESPnow message received
      
    
    //for messages sent
    uint16_t ESPNOW_SENDS; //number of ESPNow sends since midnight
    bool ESPNOW_LAST_OUTGOINGMSG_STATE; //-1 if send failure, 0 if indeterminate, 1 if send success
    uint32_t ESPNOW_LAST_OUTGOINGMSG_TIME; // time of last server (type 100) broadcast. Will be 0 if no server or have registered the server
      uint64_t ESPNOW_LAST_OUTGOINGMSG_TO_MAC; // MAC of last ESPnow message sender
      uint8_t ESPNOW_LAST_OUTGOINGMSG_TYPE; // type of last ESPnow message sender
      char  ESPNOW_LAST_OUTGOINGMSG_PAYLOAD[64]; //text portion of payload of last ESPnow message received

      uint8_t WIFI_RECOVERY_NONCE[8]; // Nonce for ESPNow WiFi recovery
      uint8_t WIFI_RECOVERY_STAGE; // 0=Prefs, 1=cycling
      uint8_t WIFI_RECOVERY_SERVER_INDEX; // index for cycling through servers
          
  
      
      uint8_t isExpired = false; //are any critical sensors expired?
      uint8_t isFlagged=false;
      uint8_t wasFlagged=false;
  
      #ifndef _ISPERIPHERAL //these are not used by peripherals
      uint8_t isHeat=false; //first bit is heat on, bits 1-6 are zones
      uint8_t isAC=false; //first bit is compressor on, bits 1-6 are zones
      uint8_t isFan=false; //first bit is fan on, bits 1-6 are zones
      uint8_t wasHeat=false; //first bit is heat on, bits 1-6 are zones
      uint8_t wasAC=false; //first bit is compressor on, bits 1-6 are zones
      uint8_t wasFan=false; //first bit is fan on, bits 1-6 are zones
  
      uint8_t isHot;
      uint8_t isCold;
      uint8_t isSoilDry;
      uint8_t isLeak;

      uint16_t showTheseFlags=(1<<3) + (1<<2) + (1<<1) + 1; //bit 0 = 1 for flagged only, bit 1 = 1 include expired, bit 2 = 1 include soil alarms, bit 3 =1 include leak, bit 4 =1 include temperature, bit 5 =1 include  RH, bit 6=1 include pressure, 7 = 1 include battery, 8 = 1 include HVAC
      #endif
  
      char lastError[76];
      time_t lastErrorTime;
      ERRORCODES lastErrorCode;
  
  
  };
  



//sensors
#define OLDESTSENSORHR 24 //hours before a sensor is removed

/*
At some point, streamline with device database and sensor database. This way a device could change name without altering sensors
struct DeviceVal {
  uint64_t MAC;
  uint32_t IP;
  uint8_t ardID;//legacy from V1 and V2 used this to define ID. Now MAC is the ID. ArdID can still be some value, but can also be zero.
};
*/





//



//automatically detect arduino type
#if defined (ARDUINO_ARCH_ESP8266)
  #define _USE8266 1
  #define _ADCRATE 1023
#elif defined(ESP32)
  #define _USE32
  #define _ADCRATE 4095
#else
  #error Arduino architecture unrecognized by this code.
#endif



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

#endif