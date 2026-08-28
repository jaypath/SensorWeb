#pragma once

#ifdef _USESUPABASE

#include <Arduino.h>

struct ArborysDevType;

/** Load Prefs cloud fields into Supabase client when claimed. Call after BootSecure::setup. */
void supabaseBeginFromPrefs();

/** Persist current Supabase.config() + claimed flag into Prefs and encrypt-save. */
bool supabasePersistClaimedPrefs();

/** Prefs.SITE_SLUG or "home" when empty. */
const char* supabaseSiteSlug();

/**
 * Sync this device's site_slug from cloud: once after boot (with retries), then every 3 hours.
 * Hub and peripheral. Safe to call every loop.
 */
void supabaseServiceStartupSiteSync();

/**
 * Cloud sync for claimed non-low-power devices:
 * - Local sensors: upload each reading at max(1h, sensor SendingInt)
 * - If no successful cloud message in 3 hours: keepalive upsert (MAC, IP, dev_type)
 * No-ops on _USELOWPOWER builds (those devices do not talk to Supabase).
 */
void supabaseServiceCloudSync(bool force = false);

#if _IS_SERVER_HUB
struct SupabaseHubInventoryResult {
  bool ok;
  uint16_t sensorsQueried;
  uint16_t sensorsAdded;
  uint16_t devicesAdded;
  char error[80];
};

/**
 * Query site sensors with time_read within last 24h; add unknown peripherals locally.
 * force ignored for rate (caller controls). Returns summary in *out if non-null.
 */
bool supabaseHubInventorySync(SupabaseHubInventoryResult* out = nullptr);

/** Every 12 hours run inventory sync (no-op if not claimed / no wifi). */
void supabaseHubPollTick();

/**
 * After a LAN data-request to an expired peripheral: for each expired sensor on that
 * device, query Supabase once at each multiple N≥2 of SendingInt since last freshness
 * (2×, 3×, 4×, …). Applies newer cloud state locally when found.
 */
void supabaseHubPollExpiredAfterLan(ArborysDevType* device);
#endif

#endif
