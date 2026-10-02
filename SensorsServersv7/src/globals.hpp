#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include "device_roles.hpp"

//edit these!


// RGB565 color definitions. LovyanGFX may #undef these when its headers are included after globals; values below match LGFX.
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_YELLOW 0xFFE0
#define TFT_WHITE 0xFFFF
#define TFT_BLACK 0x0000
#define TFT_SKYBLUE 0x867D

//automatically detect arduino type
#if defined (ARDUINO_ARCH_ESP8266)
  #define _USE8266 1
  #define _ADCRATE 1023
#elif defined(ESP32)
  #define _USE32
//note that ADCrate is now automatically detected based on the ADC_BITS and ADC_ATTEN settings
  #else
  #error Arduino architecture unrecognized by this code.
#endif

#define SNSDATA_JSON_BUFFER_SIZE 1024

// Forward declarations
class LGFX;
class Devices_Sensors;

// Include necessary Arduino libraries for basic types
#include <Arduino.h>
#include <WiFi.h>
#include <IPAddress.h>

#ifdef _USEUDP
#include <WiFiUdp.h>
#endif
#ifdef _USEI2C
#include <Wire.h>
#endif

#define RESET_ENUM_TO_STRING(enum_val) (#enum_val)



typedef enum {
  EVENT_DEFAULT, //any event, no specific type specified
  EVENT_WIFI_CONNECTED, //wifi connected
  EVENT_WIFI_DISCONNECTED, //wifi disconnected
  EVENT_WIFI_AP_STARTED, //wifi AP started
  EVENT_WIFI_AP_STOPPED, //wifi AP stopped
  EVENT_WIFI_AP_CLIENT_CONNECTED, //wifi AP client connected
  EVENT_WIFI_AP_CLIENT_DISCONNECTED, //wifi AP client disconnected
  EVENT_REBOOT_TRIGGERED, //reboot triggered
  EVENT_BOOT_WARNING, //boot warning - any non error occured
  EVENT_BOOT_COMPLETE, //boot complete
  EVENT_FIRMWARE_UPDATED, //firmware updated
  EVENT_ARBORYSNET, // generic ArborysNet / cloud event
  EVENT_ARBORYSNET_CLAIMED, // device claim succeeded
  EVENT_ARBORYSNET_LOCATION, // locations listed / assigned / changed
  EVENT_ARBORYSNET_UPLOAD // reading or keepalive uploaded
  } SYSTEMEVENTS;

void logSystemEvent(const char* description, SYSTEMEVENTS code = EVENT_DEFAULT);
void logSystemEvent(String description, SYSTEMEVENTS code = EVENT_DEFAULT);
/** Most recent logSystemEvent text (RAM only; for STATUS page). */
const char* getLastSystemLogMessage();
time_t getLastSystemLogTime();

//potential reset issues
typedef enum {
  RESET_DEFAULT, // cause was never recorded
  RESET_SD, // SD card failure
  RESET_WEATHER, // weather fetch failed
  RESET_USER, // manual reboot
  RESET_OTA, // firmware update
  RESET_WIFI, // Wi-Fi recovery gave up
  RESET_TIME, // time-based reset
  RESET_UNKNOWN, // unexpected reboot, cause not identified
  RESET_NEWWIFI, // new Wi-Fi credentials or setup finished
  RESET_MEMORY_LOW, // free heap under 1 KB
  RESET_MEMORY_FRAGMENTED, // heap too fragmented to continue
  RESET_WATCHDOG, // task, interrupt, or other watchdog (chip does not need three enums)
  RESET_PANIC, // abort or other panic (failed allocation, assert)
  RESET_BROWNOUT, // supply dip
  RESET_POWERON, // chip powered on; RTC may have been cleared
  RESET_DEEPSLEEP, // woke from deep sleep
  RESET_SDIO, // reset over SDIO
  RESET_EXTERNAL, // external reset pin
  } RESETCAUSE;

  
  //errors in operation
  typedef enum {
    ERROR_UNDEFINED, 
    ERROR_REBOOT_TRIGGERED, //system rebooted, which may or may not be an error
    ERROR_HTTP_GENERAL, //HTTP general error
    ERROR_HTTP_REQUEST, //HTTP request error
    ERROR_HTTP_RESPONSE, //HTTP response error
    ERROR_HTTP_TIMEOUT, //HTTP timeout
    ERROR_HTTP_NOT_FOUND, //HTTP not found
    ERROR_HTTP_POST, //HTTP post error
    ERROR_HTTP_GET, //HTTP get error
    ERROR_HTTP_PUT, //HTTP put error
    ERROR_HTTP_DELETE, //HTTP delete error
    ERROR_HTTPFAIL_BOX, //failed http request for grid  coordinates
    ERROR_HTTPFAIL_GRID, //failed http request for grid
    ERROR_HTTPFAIL_HOURLY, //failed http request for hourly
    ERROR_HTTPFAIL_DAILY, //failed http request for DAILY
    ERROR_HTTPFAIL_SUNRISE, //failed http request for sunrise
    ERROR_JSON_PARSE, //failed json parse request
    ERROR_JSON_GEOCODING, //failed json parse request for geocoding
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
    ERROR_SD_WEATHERDATAWRITE, //could not write weather data
    ERROR_SD_WEATHERDATAREAD, //could not read weather data
    ERROR_SD_WEATHERDATASIZE, //weather data file was the wrong size
    ERROR_SD_GSHEETINFOWRITE, //could not write GsheetInfo data
    ERROR_SD_GSHEETINFOREAD, //could not read GsheetInfo data
    ERROR_SD_ERRORWRITE, //could not write error data
    ERROR_SD_ERRORREAD, //could not read error data
    ERROR_SD_ERRORFILESIZE, //error data file was the wrong size
    ERROR_DEVICE_ADD, //could not add a device
    ERROR_SENSOR_ADD, //could not add a sensor
    ERROR_MESH_GENERAL, // ArborysMesh general error
    ERROR_MESH_SEND, // could not send ArborysMesh message
    ERROR_MESH_SEND_UDP, // could not send ArborysMesh via UDP
    ERROR_MESH_SEND_ESPNOW, // could not send ArborysMesh via ESP-NOW radio
    // Legacy names
    ERROR_ESPNOW_GENERAL = ERROR_MESH_GENERAL,
    ERROR_ESPNOW_SEND = ERROR_MESH_SEND,
    ERROR_ESPNOW_SEND_UDP = ERROR_MESH_SEND_UDP,
    ERROR_ESPNOW_SEND_ESPNOW = ERROR_MESH_SEND_ESPNOW,
    ERROR_GSHEET_CREATE, //failed to create spreadsheet
    ERROR_GSHEET_UPLOAD, //failed to upload data to spreadsheet
    ERROR_GSHEET_DELETE, //failed to delete spreadsheet
    ERROR_GSHEET_FIND, //failed to find spreadsheet
    ERROR_GSHEET_HEADERS, //failed to create headers
    ERROR_FAILED_PREFS, //failed storage or retrieval of Prefs
    ERROR_FAILED_STRUCTCORE, //faileed storage or retrieval of struct core
    ERROR_FAILED_ENCRYPTION, //failed to encrypt data
    ERROR_FAILED_DECRYPTION, //failed to decrypt data
    ERROR_HARDWARE_MEMORY, //hardware memory error
    ERROR_TIME, 
    ERROR_SENSOR_READ, //could not read a sensor
    ERROR_SENSOR_SEND, //could not write a sensor
    ERROR_SENSOR_NOT_FOUND, //sensor not found
    ERROR_SENSOR_INVALID, //sensor invalid
    ERROR_SENSOR_1, //placeholder
    ERROR_SENSOR_2, //placeholder
    ERROR_SENSOR_3, //placeholder
    ERROR_SENSOR_4, //placeholder
    ERROR_SENSOR_5, //placeholder
    ERROR_DEVICE_NOTFOUND, //device not found
    ERROR_DEVICE_MDEVICE_NOTFOUND, //my device not found
    ERROR_DEVICE_WIFIINVALID, //device wifi invalid
    ERROR_DEVICE_NAME_INVALID, //device name invalid
    ERROR_DEVICE_NAME_1, //placeholder
    ERROR_DEVICE_NAME_2, //placeholder
    ERROR_DEVICE_NAME_3, //placeholder
    ERROR_DEVICE_NAME_4, //placeholder
    ERROR_DEVICE_NAME_5, //placeholder
    ERROR_UDP_MULTICAST_JOIN, //failed to join multicast group
    ERROR_UDP_1, //UDP message error placeholder
    ERROR_UDP_2, //UDP message error
    ERROR_UDP_3, //UDP message error
    ERROR_UDP_4, //UDP message error
    ERROR_UDP_5, //UDP message error
    ERROR_BMP_PLANES, //BMP error with planes
    ERROR_BMP_BITDEPTH, //BMP error with bitDepth
    ERROR_BMP_COMPRESSION, //BMP error with compression
    ERROR_WEATHER_TIMEOUT, //weather error
    ERROR_ARBORYSNET, // ArborysNet / cloud general failure
    ERROR_ARBORYSNET_AUTH, // invalid API key, mint JWT, auth rejected
    ERROR_ARBORYSNET_CLAIM, // claim-device failed
    ERROR_ARBORYSNET_SYNC, // location sync / inventory / keepalive failed
    ERROR_ARBORYSNET_UPLOAD // reading upload failed
  } ERRORCODES;



#include "FirmwareVersion.hpp"

  struct STRUCT_KEYS {
    uint8_t ESPNOW_KEY[17]; // LMK for ArborysMesh AES (16 bytes used; name kept for Prefs layout)
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
    bool AUTOSWITCHNEWERFIRMWARE=false; //use new version when present

    STRUCT_KEYS KEYS;

    // --- Network configuration (merged from WiFi_type) ---
    uint32_t DHCP;  // 4 bytes
    uint32_t GATEWAY; // 4 bytes
    uint32_t DNS; // 4 bytes
    uint32_t DNS2; // 4 bytes
    uint32_t SUBNET; // 4 bytes

    bool HAVECREDENTIALS = false; // Whether WiFi credentials are available

    //time zone info
    int32_t TimeZoneOffset= 90000; //offset from UTC in seconds, including DST offset. 90000 is a sentinel value for no offset set
    uint8_t DST=0; //0 = no DST used here, 1 = DST used in this locale, but not active. 2 = DST is active
    int16_t DSTOffset=0; //offset from UTC in seconds, 0 if not in DST
    time_t DSTStartUnixTime=0; // DST start, stored in STANDARD local time (UTC + TimeZoneOffset, no DST). Compare with standardLocalNow().
    time_t DSTEndUnixTime=0;   // DST end, stored in STANDARD local time (UTC + TimeZoneOffset, no DST). Compare with standardLocalNow().
    
    double LATITUDE;
    double LONGITUDE;
    FirmwareVersion FIRMWARE; // running firmware version (0.0.0 default)

    #if _HAS_LOCAL_SENSORS
    double SNS_LIMIT_MAX[_SENSORNUM] = {0}; //store max values for each sensor in NVS
    double SNS_LIMIT_MIN[_SENSORNUM] = {0}; //store min values for each sensor in NVS
    uint16_t SNS_FLAGS[_SENSORNUM] = {0}; //store this, as user may have changed some settings here
    uint16_t SNS_INTERVAL_POLL[_SENSORNUM] = {0};
    uint16_t SNS_INTERVAL_SEND[_SENSORNUM] = {0};
    double SNS_CALIB_MIN[_SENSORNUM] = {0}; //store min values for each sensor in NVS
    double SNS_CALIB_MAX[_SENSORNUM] = {0}; //store max values for each sensor in NVS
    #endif

    #ifdef _USESUPABASE
    char SUPABASE_PROJECT_URL[96] = {0};
    char SUPABASE_ANON_KEY[256] = {0}; // JWT anon keys are ~208+ chars; 200 truncated → "Invalid API key"
    char SUPABASE_API_KEY[96] = {0};
    char SUPABASE_USER_ID[40] = {0};
    char SITE_SLUG[33] = {'h','o','m','e','\0'};
    bool SUPABASE_CLAIMED = false;
    // Hub default true / peripheral default false; set in BootSecure when Prefs wiped or first boot.
    bool UPLOAD_TO_SUPABASE = false;
    #endif
  };
  
  struct ERROR_STRUCT {
    char errorMessage[100];
    ERRORCODES errorCode;
    time_t errorTime; // UTC unix
  };
  


  struct STRUCT_CORE {

    int16_t MY_DEVICE_INDEX; // local stored index of this device in Sensors
      bool makeBroadcast=false;
      // ArborysMesh info
      bool espNowFailed=false; // ArborysMesh: no hub ACK — also UDP-broadcast telemetry
      bool makeCloudUpload=false; // hub: upload readings this 10-minute slot (random second)
      time_t lastStoreCoreDataTime;
      bool isUpToDate;  // Core has been saved to memory
      RESETCAUSE resetInfo;
      time_t lastResetTime;
      byte rebootsToday=0;
      time_t ALIVESINCE;
      time_t lastServerHeardTime; // last message from any server (devType 100–150), incl. broadcasts
      uint8_t wifiFailCount; // consecutive associated-without-IP recovery attempts (reset on GOT_IP)
      time_t wifiDownSince;
      bool initialSetupFinalized; // initial wizard submitted (or already configured at boot)
      bool initialSetupExitPending; // wait for provisioning client to leave AP before stopping soft-AP
      time_t apLastClientActivity; // last HTTP request while in AP mode (0 = none)
      time_t apLastReconnectCheckTime; // last minute tick for AP-mode STA reconnect attempt
      time_t apModeEnteredTime; // when soft-AP provisioning started
      time_t apLastChannelScanTime; // last AP-mode ESP-NOW channel scan (0 = none this session)
      time_t lastTimezoneRefresh; // last successful TimeAPI timezone fetch (0 = never)


      //timezone offset is in prefs
      
      time_t UTCTime; //UTC time
      time_t currentTime;
      uint8_t currentSecond;

      int8_t WiFiStatus; //2= connected but wifi.status() doesn't list this correctly, 1 = connected, 0=unknown, -1 - no valid IP address, -2 - no valid RSSI range, -3 - no valid SSID, -4 - no valid gateway, -5 - no connection
      WiFiEvent_t WiFiLastEvent; //last WiFi event
      uint8_t WifiChannel; // current WiFi RF channel (1-14); 0 if unknown
      int16_t RSSIcurrent = -999; // current WiFi RSSI (dBm); -999 = invalid
      int16_t RSSIlow = -999;     // worst (most negative) RSSI seen since boot
      int16_t RSSIhigh = -999;    // best (least negative) RSSI seen since boot
      time_t lastRSSItime = 0;    // last RSSI sample time (updated every 5 sec)
      uint8_t currentMinute; //current minute of the day, used to ensure clock is drawn correctly

  
      #if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
      bool haveOutsideTemperatureSensor;
      int8_t currentOutsideTemp;
      int8_t currentOutsideHumidity;
      int8_t currentOutsidePressure;
      int8_t Tmax;
      int8_t Tmin;
      int8_t lastCurrentOutsideTemp; //last current outside temperature
      uint8_t WeatherEventFlags; 
      //bit 0 = current flag status, 1 = flagged, 0 = not flagged
      //bit 1 = last flag status, 1 = flagged, 0 = not flagged
      //bit 2 = Severe flag
      //bit 3 = Imminent flag
      //bit 4 = 
      //bit 5 = 
      //bit 6 = 
      //bit 7 = 
      #endif
  
      #ifdef _MONITOROUTDOORBATTERYSENSORS
      uint8_t localBatteryIndex; // index of outside battery_li sensor (255 = none)
      #endif
  
      // ArborysMesh radio stats (ESP-NOW transport)
      uint8_t TEMP_AES[32]; // [0..15]=key, [16..31]=IV
      uint32_t TEMP_AES_TIME; // unixtime of TEMP_AES creation
      uint64_t TEMP_AES_MAC; // expected server MAC for WiFi PW response
      
      
      //for UDP messages
      uint16_t UDP_RECEIVES; //number of UDP receives since midnight
      uint16_t UDP_SENDS; //number of UDP sends since midnight
      uint32_t UDP_LAST_INCOMINGMSG_TIME; // time of last UDP message received
      char UDP_LAST_INCOMINGMSG_TYPE[10]; // message of last UDP status check - [Sensor, System, etc]      
      IPAddress UDP_LAST_INCOMINGMSG_FROM_IP; // IP address of last UDP message sender
      uint8_t UDP_INCOMING_ERRORS; //number of UDP incoming errors since midnight
      uint32_t UDP_LAST_OUTGOINGMSG_TIME; // time of last UDP message sent
      char UDP_LAST_OUTGOINGMSG_TYPE[10]; // message of last UDP status check - [Sensor, System, etc]      
      IPAddress UDP_LAST_OUTGOINGMSG_TO_IP; // IP address of last UDP message target
      uint8_t UDP_OUTGOING_ERRORS; //number of UDP outgoing errors since midnight
      

      //for HTTP messages
      uint16_t HTTP_RECEIVES; //number of HTTP receives since midnight
      uint16_t HTTP_SENDS; //number of HTTP sends since midnight
      uint32_t HTTP_LAST_INCOMINGMSG_TIME; // time of last HTTP message received
      char HTTP_LAST_INCOMINGMSG_TYPE[10]; // message of last HTTP status check - [Sensor, System, etc]      
      IPAddress HTTP_LAST_INCOMINGMSG_FROM_IP; // IP address of last HTTP message sender
      uint8_t HTTP_INCOMING_ERRORS; //number of HTTP incoming errors since midnight
      uint32_t HTTP_LAST_OUTGOINGMSG_TIME; // time of last HTTP message sent
      char HTTP_LAST_OUTGOINGMSG_TYPE[10]; // message of last HTTP status check - [Sensor, System, etc]      
      IPAddress HTTP_LAST_OUTGOINGMSG_TO_IP; // IP address of last HTTP message target
      uint8_t HTTP_OUTGOING_ERRORS; //number of HTTP outgoing errors since midnight
      
    // ArborysMesh stats (ESP-NOW transport only; UDP mesh twin uses UDP_* counters)
    uint16_t MESH_RECEIVES;
    uint32_t MESH_LAST_INCOMINGMSG_TIME;
      uint64_t MESH_LAST_INCOMINGMSG_FROM_MAC;
      IPAddress MESH_LAST_INCOMINGMSG_FROM_IP;
      uint8_t MESH_LAST_INCOMINGMSG_FROM_TYPE;
      uint8_t MESH_LAST_INCOMINGMSG_TYPE;
      char MESH_LAST_INCOMINGMSG_PAYLOAD[64];
      uint8_t MESH_INCOMING_ERRORS;
    
    uint16_t MESH_SENDS;
    uint32_t MESH_LAST_OUTGOINGMSG_TIME;
      uint64_t MESH_LAST_OUTGOINGMSG_TO_MAC;
      uint8_t MESH_LAST_OUTGOINGMSG_TYPE;
      char  MESH_LAST_OUTGOINGMSG_PAYLOAD[64];
      uint8_t MESH_OUTGOING_ERRORS;

      uint8_t WIFI_RECOVERY_NONCE[8]; // Nonce for WiFi recovery (legacy)
      uint8_t WIFI_RECOVERY_STAGE; // 0=Prefs, 1=cycling
      uint8_t WIFI_RECOVERY_SERVER_INDEX; // index for cycling through servers
          
      uint8_t isExpired = false; //are any critical sensors expired?
      uint8_t isFlagged=false;
      uint8_t wasFlagged=false;
  
      #if _IS_SERVER_HUB // HVAC aggregate state (hub displays)
      uint8_t isHeat=false; //first bit is heat on, bits 1-6 are zones
      uint8_t isAC=false; //first bit is compressor on, bits 1-6 are zones
      uint8_t isFan=false; //first bit is fan on, bits 1-6 are zones
      uint8_t wasHeat=false; //first bit is heat on, bits 1-6 are zones
      uint8_t wasAC=false; //first bit is compressor on, bits 1-6 are zones
      uint8_t wasFan=false; //first bit is fan on, bits 1-6 are zones
      #endif
      uint8_t MyRandomSecond=0; //random second at which to send data
      
      uint8_t isHot;
      uint8_t isCold;
      uint8_t isSoilDry;
      uint8_t isLeak;

      uint16_t showTheseFlags=(1<<3) + (1<<2) + (1<<1) + 1; //bit 0 = 1 for flagged only, bit 1 = 1 include expired, bit 2 = 1 include soil alarms, bit 3 =1 include leak, bit 4 =1 include temperature, bit 5 =1 include  RH, bit 6=1 include pressure, 7 = 1 include battery, 8 = 1 include HVAC
      
      char lastError[76];
      time_t lastErrorTime;
      ERRORCODES lastErrorCode;

      int8_t SerialPrintLevel=0; //negative values... print only that level, 0=print everything, 1=print outputs and worse, 2=print info and worse, 3=print errors and worse, 4=print serious faults, 5=print critical only
  
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


/*sens types
//0 - not defined
//1 - temp, DHT
//2 - RH, DHT
//3 - soil moisture, capacitative or Resistive
//4 -  temp, AHT21
//5 - RH, AHT21
//6 - 
//7 - distance, HC-SR04
//8 - 
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
21 - 
30 -
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
100-150 - server type sensors, to which other sensors will send their data
100 = any server (receives data), disregarding subtype
101 - weather display server with local persistent storage (ie SD card)
102 = any weather server that has no persistent storage
103 = any server with local persistent storage (ie SD card) that uploads data cloud storage
104 = any server without local persistent storage that uploads data cloud storage
200-255 - interrupt-driven sensors
200 - human presence (mm-wave RCWL-0516); snsValue = +light sec remaining, or -motion-ignore sec remaining
220 - momentary lights switch; snsValue same shared timer (+on / -ignore / 0 idle)
 

*/

//------------------
// Now include headers that depend on the above type definitions
#include "Devices.hpp"

// Forward declare classes to avoid circular dependencies
class WebServer;
class LGFX;

#include "utility.hpp"
#include "server.hpp"
#include "timesetup.hpp"

#ifdef _USESDCARD
#include "SDCard.hpp"
#endif

#if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
#include "Weather_Optimized.hpp"
#endif
#ifdef _USEWEATHERLITE
#include "Weather_Optimized_lite.hpp"
#endif

#ifdef _USETFT
  #ifdef _ISCLOCK480X480
    #include "Clock480X480.hpp"
  #else
    #include "graphics.hpp"
  #endif
#endif


#include "ArborysMesh.hpp"
#include "BootSecure.hpp"

#ifdef _HAS_LOCAL_SENSORS
#include "sensors.hpp"
#endif

#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
#endif

#if _SUPABASE_RUNTIME
#include <SupabaseClient.hpp>
#include "supabase_prefs.hpp"
#endif

#ifdef _USEFIREBASE
#include "FirebaseUpload.hpp"
#endif

#ifdef _USETFLUNA
#include "TFLuna.hpp"
#endif


//general libraries
#include <Arduino.h>
#include <String.h>
#include <TimeLib.h>
#include <time.h>
#include <SPI.h>
#ifdef _USESDCARD
#include <SD.h>
#endif
#include <string>
#if defined(_USETFT) && _IS_SERVER_HUB
#include <LovyanGFX.hpp>
#endif

#ifdef _USESSD1306
#include "SSD1306_graphics.hpp"
#endif

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <esp_task_wdt.h>
#include <esp_now.h>
#include <esp_wifi.h>


#endif