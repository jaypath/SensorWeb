#pragma once

#ifdef _USESUPABASE

#include <Arduino.h>
#include "SupabaseTypes.hpp"

/** Bootstrap endpoint used before claim (override via -D in platformio.ini). */
#ifndef SUPABASE_DEFAULT_PROJECT_URL
#define SUPABASE_DEFAULT_PROJECT_URL "https://mfrvhbypqypczjzhwqrf.supabase.co"
#endif
#ifndef SUPABASE_DEFAULT_ANON_KEY
#define SUPABASE_DEFAULT_ANON_KEY \
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im1mcnZoYnlwcXlwY3pqemh3cXJmIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTY5NDU1MTQsImV4cCI6MjA3MjUyMTUxNH0.vzNZQju1ZK5fWE3HchX47Ab4gvVgB2wJ1e28sYCbzZw"
#endif

/**
 * Runtime config for SupabaseClient.
 * Fill from claim-device response or Prefs NVS after claim.
 */
struct SupabaseConfig {
  char projectUrl[96];   // https://xxxx.supabase.co  (no trailing slash)
  char anonKey[200];
  char deviceMac[16];    // AABBCCDDEEFF
  char apiKey[96];       // plaintext device API key from enroll/claim
  char userId[40];       // auth user uuid (from claim); optional for mint
  char siteSlug[33];     // logical site (default "home")
  char deviceIp[16];     // last known STA IP for identity on every call
  uint8_t devType;       // _MYTYPE
  int32_t utcOffsetSec;  // Prefs.TimeZoneOffset
  uint16_t httpTimeoutMs;
  uint8_t apiVersion;    // device-api envelope version

  void clear() {
    projectUrl[0] = '\0';
    anonKey[0] = '\0';
    deviceMac[0] = '\0';
    apiKey[0] = '\0';
    userId[0] = '\0';
    strncpy(siteSlug, "home", sizeof(siteSlug) - 1);
    siteSlug[sizeof(siteSlug) - 1] = '\0';
    deviceIp[0] = '\0';
    devType = 0;
    utcOffsetSec = 0;
    httpTimeoutMs = 20000;
    apiVersion = SUPABASE_API_VERSION;
  }

  /** Enough to call claim-device (no device api_key yet). */
  bool hasBootstrap() const {
    return projectUrl[0] && anonKey[0] && deviceMac[0];
  }

  bool isReady() const {
    return hasBootstrap() && apiKey[0];
  }

  void applyDefaults() {
    if (!projectUrl[0]) {
      strncpy(projectUrl, SUPABASE_DEFAULT_PROJECT_URL, sizeof(projectUrl) - 1);
      projectUrl[sizeof(projectUrl) - 1] = '\0';
    }
    if (!anonKey[0]) {
      strncpy(anonKey, SUPABASE_DEFAULT_ANON_KEY, sizeof(anonKey) - 1);
      anonKey[sizeof(anonKey) - 1] = '\0';
    }
    if (!siteSlug[0]) {
      strncpy(siteSlug, "home", sizeof(siteSlug) - 1);
      siteSlug[sizeof(siteSlug) - 1] = '\0';
    }
  }
};

#endif
