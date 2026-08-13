#pragma once

#ifdef _USESUPABASE

#include <Arduino.h>

/**
 * Runtime config for SupabaseClient.
 * Fill from NVS/Prefs/serial later; keep Prefs struct size unchanged for v1.
 */
struct SupabaseConfig {
  char projectUrl[96];   // https://xxxx.supabase.co  (no trailing slash)
  char anonKey[200];
  char deviceMac[16];    // AABBCCDDEEFF
  char apiKey[96];       // plaintext device API key from enroll-device
  int32_t utcOffsetSec;  // Prefs.TimeZoneOffset
  uint16_t httpTimeoutMs;

  void clear() {
    projectUrl[0] = '\0';
    anonKey[0] = '\0';
    deviceMac[0] = '\0';
    apiKey[0] = '\0';
    utcOffsetSec = 0;
    httpTimeoutMs = 20000;
  }

  bool isReady() const {
    return projectUrl[0] && anonKey[0] && deviceMac[0] && apiKey[0];
  }
};

#endif
