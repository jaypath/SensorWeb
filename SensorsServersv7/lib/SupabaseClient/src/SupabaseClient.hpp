#pragma once

/**
 * SensorWeb Supabase device client
 *
 * Actions (device-api v1): list_active_sensors, query, upsert_device, upsert_sensor,
 * insert_reading, firmware_check, firmware_url.
 *
 * Every call sends api_version=SUPABASE_API_VERSION. Auth via mint-device-jwt.
 * Enable with -D _USESUPABASE=1
 */

#ifdef _USESUPABASE

#include <Arduino.h>
#include <IPAddress.h>
#include <cstring>
#include <cmath>
#include "SupabaseConfig.hpp"
#include "SupabaseTypes.hpp"

#if __has_include("Devices.hpp")
#include "Devices.hpp"
#define SUPABASE_HAS_ARBORYS_TYPES 1
#endif

class SupabaseClient {
public:
  SupabaseClient();

  void begin(const SupabaseConfig& cfg);
  void setUtcOffset(int32_t offsetSec);
  const SupabaseConfig& config() const { return cfg_; }
  SupabaseError lastError() const { return lastError_; }
  const char* lastErrorMessage() const { return lastErrorMsg_; }
  const char* lastErrorCode() const { return lastErrorCode_; }

  SupabaseError ensureAuth();

  SupabaseError listActiveSensors(SupabaseSensorDto* out, uint16_t maxOut, uint16_t* countOut,
                                  const char* scopeMac = nullptr);

  SupabaseError querySensors(const SupabaseQueryFilter& filter, SupabaseSensorDto* out,
                             uint16_t maxOut, uint16_t* countOut);
  SupabaseError queryDevices(const SupabaseQueryFilter& filter, SupabaseDeviceDto* out,
                             uint16_t maxOut, uint16_t* countOut);
  SupabaseError queryReadings(const SupabaseQueryFilter& filter, SupabaseReadingDto* out,
                              uint16_t maxOut, uint16_t* countOut);

  SupabaseError upsertDevice(const SupabaseDeviceDto& device);
  SupabaseError upsertSensor(const SupabaseSensorDto& sensor);
  SupabaseError insertReading(const SupabaseReadingDto& reading, bool refreshSensor = true);

  SupabaseError checkFirmware(uint8_t devType, uint32_t featureMask,
                              uint8_t maj, uint8_t min, uint8_t pat,
                              SupabaseFirmwareOffer& offerOut);
  SupabaseError fetchFirmwareUrl(const char* releaseId, char* urlOut, size_t urlOutLen,
                                 uint16_t expiresInSec = 300);

  static void macToString(uint64_t mac, char* out16);
  static uint64_t macFromString(const char* macStr);
  static void normalizeMacInPlace(char* mac16);

#ifdef SUPABASE_HAS_ARBORYS_TYPES
  // Inline so they compile in the app TU that includes Devices.hpp + this header.
  static inline void applyToDevice(const SupabaseDeviceDto& in, ArborysDevType& out) {
    out.MAC = macFromString(in.deviceMac);
    out.IP.fromString(in.deviceIp);
    out.devType = in.devType;
    strncpy(out.devName, in.name, sizeof(out.devName) - 1);
    out.devName[sizeof(out.devName) - 1] = '\0';
    out.SendingInt = in.sendingInt;
    out.Flags = in.flags;
    out.expired = in.expired;
    out.IsSet = 1;
    out.firmware.v[0] = in.firmwareMajor;
    out.firmware.v[1] = in.firmwareMinor;
    out.firmware.v[2] = in.firmwarePatch;
    out.dataReceived = in.dataReceived;
    out.dataSent = in.dataSent;
  }

  static inline void fillFromDevice(const ArborysDevType& in, SupabaseDeviceDto& out) {
    memset(&out, 0, sizeof(out));
    macToString(in.MAC, out.deviceMac);
    strncpy(out.name, in.devName, sizeof(out.name) - 1);
    String ip = in.IP.toString();
    strncpy(out.deviceIp, ip.c_str(), sizeof(out.deviceIp) - 1);
    out.devType = in.devType;
    out.sendingInt = in.SendingInt;
    out.firmwareMajor = in.firmware.v[0];
    out.firmwareMinor = in.firmware.v[1];
    out.firmwarePatch = in.firmware.v[2];
    out.expired = in.expired;
    out.flags = in.Flags;
    out.isActive = true;
    out.dataReceived = in.dataReceived;
    out.dataSent = in.dataSent;
  }

  static inline void applyToSensor(const SupabaseSensorDto& in, ArborysSnsType& out) {
    out.snsType = in.snsType;
    out.snsID = in.snsId;
    strncpy(out.snsName, in.snsName, sizeof(out.snsName) - 1);
    out.snsName[sizeof(out.snsName) - 1] = '\0';
    out.snsValue = in.snsValue;
    out.timeRead = in.timeRead;
    out.timeLogged = in.timeLogged;
    out.SendingInt = in.sendingInt;
    out.Flags = in.flags;
    out.expired = in.expired;
    out.IsSet = 1;
    out.limitHigh = in.limitHigh;
    out.limitLow = in.limitLow;
  }

  static inline void fillFromSensor(uint64_t deviceMac, const ArborysSnsType& in,
                                    SupabaseSensorDto& out) {
    memset(&out, 0, sizeof(out));
    out.limitHigh = NAN;
    out.limitLow = NAN;
    macToString(deviceMac, out.deviceMac);
    out.snsType = in.snsType;
    out.snsId = in.snsID;
    strncpy(out.snsName, in.snsName, sizeof(out.snsName) - 1);
    out.snsValue = in.snsValue;
    out.timeRead = in.timeRead;
    out.timeLogged = in.timeLogged;
    out.sendingInt = in.SendingInt;
    out.flags = in.Flags;
    out.expired = in.expired;
    out.limitHigh = in.limitHigh;
    out.limitLow = in.limitLow;
  }

  inline SupabaseError upsertDevice(const ArborysDevType& device) {
    SupabaseDeviceDto dto;
    fillFromDevice(device, dto);
    return upsertDevice(dto);
  }

  inline SupabaseError upsertSensor(uint64_t deviceMac, const ArborysSnsType& sensor) {
    SupabaseSensorDto dto;
    fillFromSensor(deviceMac, sensor, dto);
    dto.utcOffset = cfg_.utcOffsetSec;
    return upsertSensor(dto);
  }

  inline SupabaseError insertReadingFromSensor(uint64_t deviceMac, const ArborysSnsType& sensor,
                                               const char* deviceIp = nullptr) {
    SupabaseReadingDto r;
    memset(&r, 0, sizeof(r));
    macToString(deviceMac, r.deviceMac);
    if (deviceIp) {
      strncpy(r.deviceIp, deviceIp, sizeof(r.deviceIp) - 1);
    }
    r.snsType = sensor.snsType;
    r.snsId = sensor.snsID;
    strncpy(r.snsName, sensor.snsName, sizeof(r.snsName) - 1);
    r.snsValue = sensor.snsValue;
    r.utcOffset = cfg_.utcOffsetSec;
    r.timeLogged = sensor.timeLogged;
    r.timeRead = sensor.timeRead;
    r.flagged = bitRead(sensor.Flags, 0);
    r.expired = sensor.expired;
    r.critical = bitRead(sensor.Flags, 7);
    r.sendingInt = sensor.SendingInt;
    r.flags = sensor.Flags;
    return insertReading(r, true);
  }
#endif

private:
  SupabaseConfig cfg_;
  char accessToken_[900];
  uint32_t tokenExpiresAt_;
  SupabaseError lastError_;
  char lastErrorMsg_[96];
  char lastErrorCode_[40];

  void setError(SupabaseError err, const char* code, const char* msg);
  SupabaseError httpPostJson(const char* path, const char* bodyJson, bool withBearer,
                             String& responseOut);
};

extern SupabaseClient Supabase;

#endif // _USESUPABASE
