#pragma once

#ifdef _USESUPABASE

#include <Arduino.h>
#include "SupabaseTypes.hpp"

/**
 * Runtime config for SupabaseClient.
 * Fill from claim-device response or NVS; keep Prefs struct size unchanged for v1.
 */
struct SupabaseConfig {
  char projectUrl[96];   // https://xxxx.supabase.co  (no trailing slash)
  char anonKey[200];
  char deviceMac[16];    // AABBCCDDEEFF
  char apiKey[96];       // plaintext device API key from enroll/claim
  char userId[40];       // auth user uuid (from claim); optional for mint
  int32_t utcOffsetSec;  // Prefs.TimeZoneOffset
  uint16_t httpTimeoutMs;
  uint8_t apiVersion;    // device-api envelope version

  void clear() {
    projectUrl[0] = '\0';
    anonKey[0] = '\0';
    deviceMac[0] = '\0';
    apiKey[0] = '\0';
    userId[0] = '\0';
    utcOffsetSec = 0;
    httpTimeoutMs = 20000;
    apiVersion = SUPABASE_API_VERSION;
  }

  bool isReady() const {
    return projectUrl[0] && anonKey[0] && deviceMac[0] && apiKey[0];
  }
};

#endif
