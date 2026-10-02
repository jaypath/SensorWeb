#pragma once

#include "device_roles.hpp"
#if _SUPABASE_RUNTIME

#include <Arduino.h>
#include "globals.hpp"

struct ArborysDevType;

/** Load Prefs cloud fields into Supabase client when claimed. Call after BootSecure::setup. */
void supabaseBeginFromPrefs();

/** Persist current Supabase.config() + claimed flag into Prefs and encrypt-save. */
bool supabasePersistClaimedPrefs();

/** Prefs.SITE_SLUG or "home" when empty. */
const char* supabaseSiteSlug();

/**
 * True when NVS has a usable ArborysNet claim (claimed flag + project URL + API key).
 * False if the flag is set but the key was wiped (inconsistent state).
 */
bool supabaseHasStoredCredentials();

/**
 * True when stored credentials exist and this boot has had at least one successful
 * Supabase HTTPS call. Shown as "Connected" in the web UI (status only — not a gate).
 */
bool supabaseIsConnected();

/**
 * If the last Supabase client error is confirmed invalid_device, wipe local claim/NVS.
 * Returns true when credentials were cleared. Transient TLS/HTTP failures must not call this.
 */
bool supabaseClearClaimIfInvalidDevice();

/**
 * User-initiated Quit ArborysNet: clear local claim credentials from Prefs/NVS and
 * stop cloud TLS until the device is claimed again. Does not call Supabase.
 */
void supabaseQuitArborysNet();

/** Store major ArborysNet error (SD error log when available + lastError). */
void arborysNetStoreError(const char* message, ERRORCODES code = ERROR_ARBORYSNET);

/** Append ArborysNet system event (SD systemlog when available). */
void arborysNetLogEvent(const char* message, SYSTEMEVENTS code = EVENT_ARBORYSNET);

/** Map last Supabase client error into storeError. */
void arborysNetLogClientError(const char* context, ERRORCODES fallbackCode = ERROR_ARBORYSNET);

/**
 * Sync this device's site from cloud in isolated TLS steps (one per call):
 *   Auth (mint JWT) → Ping (upsertDevice fire-and-forget: name/IP/MAC). Query retained but unwired.
 * Starts ~30s after boot with ~8s gaps. Only clears local claim on invalid_device.
 * Safe to call every loop.
 */
void supabaseServiceStartupSiteSync();

/**
 * Cloud sync for claimed non-low-power devices:
 * - Hub (UPLOAD_TO_SUPABASE): upload monitored readings every 10 min (timeRead > timeCloudUpload)
 * - Peripheral: same upload only when no server contact for 6h
 * - Keepalive if no successful cloud contact in 12h
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
 * After a LAN data-request to an expired peripheral: queue a non-blocking cloud poll
 * (TLS on a worker). For each expired sensor, query once at each N≥2 of SendingInt
 * (2×, 3×, …). Results are applied on the main loop when newer cloud state is found.
 */
void supabaseHubPollExpiredAfterLan(ArborysDevType* device);
#endif

#endif
