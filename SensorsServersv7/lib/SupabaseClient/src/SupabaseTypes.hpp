#pragma once

#ifdef _USESUPABASE

#include <Arduino.h>
#include <IPAddress.h>

/** Retained for Prefs / claim bootstrap compatibility (PostgREST path does not send this). */
#ifndef SUPABASE_API_VERSION
#define SUPABASE_API_VERSION 1
#endif

/** Max items returned into caller buffers by list/query helpers. */
#ifndef SUPABASE_MAX_QUERY_ROWS
#define SUPABASE_MAX_QUERY_ROWS 64
#endif

enum class SupabaseError : int8_t {
  Ok = 0,
  NotConfigured = -1,
  AuthFailed = -2,
  HttpFailed = -3,
  ParseFailed = -4,
  ApiRejected = -5,
  UpgradeRequired = -6,
  SubscriptionInactive = -7,
  NotRegistered = -8,
  BufferTooSmall = -9,
  InvalidArg = -10,
};

struct SupabaseDeviceDto {
  char deviceMac[16];   // AABBCCDDEEFF + NUL
  char userId[40];
  char name[30];
  char deviceIp[16];
  char siteSlug[33];    // e.g. home, work
  char siteId[40];      // uuid or empty
  uint8_t devType;
  uint32_t featureMask;
  uint32_t sendingInt;
  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  uint8_t firmwarePatch;
  bool expired;
  uint8_t flags;
  bool isActive;
  uint32_t dataReceived;  // UTC unix
  uint32_t dataSent;      // UTC unix
  uint32_t lastSeenAt;    // UTC unix
};

struct SupabaseSensorDto {
  char deviceMac[16];
  uint8_t snsType;
  uint8_t snsId;
  char snsName[30];
  double snsValue;
  uint32_t timeRead;    // UTC unix
  uint32_t timeLogged;  // UTC unix
  uint32_t sendingInt;
  uint8_t flags;
  bool expired;
  float limitHigh;  // NAN if unset
  float limitLow;
  int32_t utcOffset;
};

struct SupabaseReadingDto {
  int64_t id;
  char deviceMac[16];
  char deviceIp[16];
  uint8_t snsType;
  uint8_t snsId;
  char snsName[30];
  double snsValue;
  int32_t utcOffset;
  uint32_t timeLogged;
  uint32_t timeRead;
  bool flagged;
  bool expired;
  bool critical;
  uint32_t sendingInt;
  uint8_t flags;
};

struct SupabaseFirmwareOffer {
  bool available;
  char releaseId[40];
  uint8_t versionMajor;
  uint8_t versionMinor;
  uint8_t versionPatch;
  uint8_t currentMajor;
  uint8_t currentMinor;
  uint8_t currentPatch;
  uint8_t devType;
  uint32_t featureMask;
  char storagePath[96];
  char sha256[72];
  int32_t sizeBytes;
};

struct SupabaseSiteDto {
  char id[40];
  char slug[33];
  char name[64];
  uint16_t deviceCount;
};

struct SupabaseQueryFilter {
  const char* table;       // "sensors" | "devices" | "sensor_readings" | "sites"
  const char* deviceMac;   // optional
  const char* deviceIp;    // optional
  const char* site;        // optional site slug (null/empty = all sites for user)
  int16_t snsType;         // -1 = any
  int8_t expired;          // -1 = any, 0/1 = filter
  uint32_t timeStartUnix;  // 0 = none
  uint32_t timeEndUnix;    // 0 = none
  uint16_t limit;
};

#endif // _USESUPABASE
