#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include <Arduino.h>
#include <TimeLib.h>

// Architecture detection
#if defined(ARDUINO_ARCH_ESP8266)
#define _USE8266 1
#define _ADCRATE 1023
#elif defined(ESP32)
#define _USE32 1
#define _ADCRATE 4095
#else
#error Unsupported architecture
#endif

// Project-wide sizes and types (tunable per project)
#ifndef NUMDEVICES
#define NUMDEVICES 50
#endif
#ifndef NUMSENSORS
#define NUMSENSORS 100
#endif

// Device type for this node (sensors will typically be < 100)
#ifndef MYTYPE
#define MYTYPE 1
#endif

// Core persistent preferences (subset for generic sensors)
struct STRUCT_PrefsH {
  bool isUpToDate;
  uint32_t lastESPNOW;
  byte WIFISSID[33];
  byte WIFIPWD[65];
  uint32_t SSIDCRC;
  uint32_t PWDCRC;
  uint64_t PROCID;
  uint32_t LASTBOOTTIME;
  uint8_t MyType;
  char DEVICENAME[30];

  // Network config
  uint32_t DHCP;
  uint32_t GATEWAY;
  uint32_t DNS;
  uint32_t DNS2;
  uint32_t SUBNET;
  uint32_t MYIP;
  uint8_t status;
  bool HAVECREDENTIALS = false;
};

// Reset causes
typedef enum {
  RESET_DEFAULT,
  RESET_SD,
  RESET_WEATHER,
  RESET_USER,
  RESET_OTA,
  RESET_WIFI,
  RESET_TIME,
  RESET_UNKNOWN,
  RESET_NEWWIFI
} RESETCAUSE;

// Error codes (keep minimal)
typedef enum {
  ERROR_UNDEFINED,
  ERROR_ESPNOW_SEND,
  ERROR_DEVICE_ADD,
  ERROR_SENSOR_ADD,
} ERRORCODES;

// Core runtime state (subset for generic sensors)
struct STRUCT_CORE {
  bool isUpToDate;
  RESETCAUSE resetInfo;
  time_t lastResetTime;
  byte rebootsSinceLast = 0;
  time_t ALIVESINCE = 0;
  uint8_t wifiFailCount = 0;
  time_t currentTime = 0;
  uint8_t WiFiMode = 0;
  time_t lastESPNOW = 0;

  // ESPNOW tracking (ESP32-only usage)
  uint64_t LAST_ESPNOW_SERVER_MAC = 0;
  uint32_t LAST_ESPNOW_SERVER_IP = 0;
  uint32_t LAST_ESPNOW_SERVER_TIME = 0;

  char lastError[76] = {0};
  time_t lastErrorTime = 0;
  ERRORCODES lastErrorCode = ERROR_UNDEFINED;

  int16_t GLOBAL_TIMEZONE_OFFSET = -18000; // default EST
  int16_t DSTOFFSET = 0;
};

extern STRUCT_CORE I;
extern STRUCT_PrefsH Prefs;

#endif

