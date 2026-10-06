#include "device_roles.hpp"
#if _SUPABASE_RUNTIME

#include "supabase_prefs.hpp"
#include "globals.hpp"
#include "BootSecure.hpp"
#include "server.hpp"
#include "utility.hpp"
#include "Devices.hpp"
#include <SupabaseClient.hpp>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#ifdef _USESDCARD
#include "SDCard.hpp"
#endif
#ifdef _USETFT
#include "graphics.hpp"
#endif
#include <string.h>

#ifndef SUPABASE_SITE_SYNC_INTERVAL_MS
#define SUPABASE_SITE_SYNC_INTERVAL_MS (3UL * 60UL * 60UL * 1000UL) // retained for deferred Query wiring
#endif
#ifndef SUPABASE_READING_UPLOAD_INTERVAL_SEC
#define SUPABASE_READING_UPLOAD_INTERVAL_SEC (10UL * 60UL) // never faster than every 10 minutes
#endif
#ifndef SUPABASE_KEEPALIVE_INTERVAL_SEC
#define SUPABASE_KEEPALIVE_INTERVAL_SEC (12UL * 3600UL) // 12h since last successful cloud contact
#endif
#ifndef SUPABASE_ORPHAN_SERVER_SEC
#define SUPABASE_ORPHAN_SERVER_SEC (6UL * 3600UL) // peripheral fallback if no server heard
#endif
#ifndef SUPABASE_BOOT_TLS_START_MS
#define SUPABASE_BOOT_TLS_START_MS 30000UL
#endif
#ifndef SUPABASE_BOOT_STEP_GAP_MS
#define SUPABASE_BOOT_STEP_GAP_MS 8000UL
#endif
#ifndef ARB_TLS_WORKER_STACK
// mbedtls + cert bundle needs headroom beyond the Arduino loop; keep all ArbNet TLS here.
#define ARB_TLS_WORKER_STACK 32768
#endif

/** Boot cloud stages complete (Auth→Ping). Cloud sync waits for this. */
static bool s_arbBootCloudReady = false;
static volatile int8_t s_arbWorkerResult = 0; // 0=busy, 1=ok, -1=fail
static TaskHandle_t s_arbWorkerTask = nullptr;
static char s_arbQuerySite[33] = {0}; // retained for deferred Query worker

/** Periodic keepalive / reading upload — NEVER run TLS on the Arduino loop task. */
enum : uint8_t { CLOUD_JOB_NONE = 0, CLOUD_JOB_KEEPALIVE = 1, CLOUD_JOB_READINGS = 2 };
static TaskHandle_t s_arbCloudTask = nullptr;
static volatile uint8_t s_cloudJob = CLOUD_JOB_NONE;
static volatile int8_t s_cloudKeepaliveUi = 0; // 0=idle, 1=ok flash, -1=fail flash, 2=in progress (yellow already shown)

#ifndef CLOUD_ACK_MAX
#define CLOUD_ACK_MAX 32
#endif
struct CloudAckEntry {
  uint64_t mac;
  uint8_t snsType;
  uint8_t snsId;
  uint32_t timeCloudUpload;
  char ip[16];
};
static CloudAckEntry s_cloudAckBuf[CLOUD_ACK_MAX];
static volatile uint8_t s_cloudAckCount = 0;
static volatile uint8_t s_cloudAckReady = 0; // 1 = main loop should broadcast
static uint32_t s_lastReadingUploadUnix = 0;
static uint32_t s_lastReadingUploadMs = 0;

#if _IS_SERVER_HUB && !defined(_USELOWPOWER)
static void supabaseHubExpiredPollApplyPending();
static SemaphoreHandle_t s_hubCloudGate = nullptr;

static void hubCloudGateEnsure() {
  if (!s_hubCloudGate) s_hubCloudGate = xSemaphoreCreateMutex();
}

static bool hubCloudTryLock() {
  hubCloudGateEnsure();
  return s_hubCloudGate && xSemaphoreTake(s_hubCloudGate, 0) == pdTRUE;
}

static void hubCloudUnlock() {
  if (s_hubCloudGate) xSemaphoreGive(s_hubCloudGate);
}

struct HubCloudTryLock {
  bool held;
  HubCloudTryLock() : held(hubCloudTryLock()) {}
  ~HubCloudTryLock() { if (held) hubCloudUnlock(); }
  explicit operator bool() const { return held; }
};
#endif

void arborysNetStoreError(const char* message, ERRORCODES code) {
  char buf[100];
  snprintf(buf, sizeof(buf), "ArborysNet: %s", message ? message : "error");
  storeError(buf, code, true);
  SerialPrint(buf, true);
}

void arborysNetLogEvent(const char* message, SYSTEMEVENTS code) {
  char buf[80];
  snprintf(buf, sizeof(buf), "ArborysNet: %s", message ? message : "event");
  logSystemEvent(buf, code);
  SerialPrint(buf, true);
}

void arborysNetLogClientError(const char* context, ERRORCODES fallbackCode) {
  const char* msg = Supabase.lastErrorMessage();
  const char* codeStr = Supabase.lastErrorCode();
  if (!msg || !msg[0]) msg = codeStr;
  if (!msg || !msg[0]) msg = "unknown error";

  auto containsI = [](const char* hay, const char* needle) -> bool {
    if (!hay || !needle || !needle[0]) return false;
    for (const char* p = hay; *p; ++p) {
      const char* h = p;
      const char* n = needle;
      while (*h && *n) {
        char a = *h, b = *n;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) break;
        ++h;
        ++n;
      }
      if (!*n) return true;
    }
    return false;
  };

  ERRORCODES code = fallbackCode;
  if (containsI(msg, "invalid api key") || containsI(msg, "invalid_api_key") ||
      containsI(msg, "jwt") || containsI(codeStr, "auth") ||
      containsI(msg, "http_forbidden") || containsI(msg, "not_configured")) {
    code = ERROR_ARBORYSNET_AUTH;
  } else if (containsI(msg, "claim") || fallbackCode == ERROR_ARBORYSNET_CLAIM) {
    code = ERROR_ARBORYSNET_CLAIM;
  }

  char buf[100];
  if (context && context[0]) {
    snprintf(buf, sizeof(buf), "%s: %s", context, msg);
  } else {
    snprintf(buf, sizeof(buf), "%s", msg);
  }
  arborysNetStoreError(buf, code);
}

void supabaseBeginFromPrefs() {
  // Claimed without API key: still load bootstrap so mint can try IP recovery.
  // Do not auto-unclaim here — reclaim_required / invalid_device clear NVS explicitly.
  if (!Prefs.SUPABASE_CLAIMED) return;
  if (!Prefs.SUPABASE_PROJECT_URL[0]) {
    return;
  }

  SupabaseConfig cfg;
  cfg.clear();
  strncpy(cfg.projectUrl, Prefs.SUPABASE_PROJECT_URL, sizeof(cfg.projectUrl) - 1);
  // Prefer stored anon when complete; older Prefs truncated JWT to 199 chars →
  // gateway "Invalid API key". Fall back to compile-time default in that case.
  if (Prefs.SUPABASE_ANON_KEY[0] && strlen(Prefs.SUPABASE_ANON_KEY) >= 200) {
    strncpy(cfg.anonKey, Prefs.SUPABASE_ANON_KEY, sizeof(cfg.anonKey) - 1);
  }
  if (Prefs.SUPABASE_API_KEY[0]) {
    strncpy(cfg.apiKey, Prefs.SUPABASE_API_KEY, sizeof(cfg.apiKey) - 1);
  }
  strncpy(cfg.userId, Prefs.SUPABASE_USER_ID, sizeof(cfg.userId) - 1);
  strncpy(cfg.siteSlug, supabaseSiteSlug(), sizeof(cfg.siteSlug) - 1);
  SupabaseClient::macToString(ESP.getEfuseMac(), cfg.deviceMac);
  cfg.utcOffsetSec = Prefs.TimeZoneOffset;
  cfg.devType = (uint8_t)_MYTYPE;
  if (wifiReadyForNetwork()) {
    String ip = WiFi.localIP().toString();
    strncpy(cfg.deviceIp, ip.c_str(), sizeof(cfg.deviceIp) - 1);
  }
  cfg.applyDefaults(); // fills anonKey from SUPABASE_DEFAULT_ANON_KEY if still empty
  Supabase.begin(cfg);
}

bool supabasePersistClaimedPrefs() {
  const SupabaseConfig& cfg = Supabase.config();
  strncpy(Prefs.SUPABASE_PROJECT_URL, cfg.projectUrl, sizeof(Prefs.SUPABASE_PROJECT_URL) - 1);
  Prefs.SUPABASE_PROJECT_URL[sizeof(Prefs.SUPABASE_PROJECT_URL) - 1] = '\0';
  strncpy(Prefs.SUPABASE_ANON_KEY, cfg.anonKey, sizeof(Prefs.SUPABASE_ANON_KEY) - 1);
  Prefs.SUPABASE_ANON_KEY[sizeof(Prefs.SUPABASE_ANON_KEY) - 1] = '\0';
  strncpy(Prefs.SUPABASE_API_KEY, cfg.apiKey, sizeof(Prefs.SUPABASE_API_KEY) - 1);
  Prefs.SUPABASE_API_KEY[sizeof(Prefs.SUPABASE_API_KEY) - 1] = '\0';
  strncpy(Prefs.SUPABASE_USER_ID, cfg.userId, sizeof(Prefs.SUPABASE_USER_ID) - 1);
  Prefs.SUPABASE_USER_ID[sizeof(Prefs.SUPABASE_USER_ID) - 1] = '\0';
  strncpy(Prefs.SITE_SLUG, cfg.siteSlug[0] ? cfg.siteSlug : "home", sizeof(Prefs.SITE_SLUG) - 1);
  Prefs.SITE_SLUG[sizeof(Prefs.SITE_SLUG) - 1] = '\0';
  Prefs.SUPABASE_CLAIMED = true;
  Prefs.isUpToDate = false;

  BootSecure boot;
  return boot.setPrefs(true) > 0;
}

const char* supabaseSiteSlug() {
  if (Prefs.SITE_SLUG[0]) return Prefs.SITE_SLUG;
  return "home";
}

bool supabaseHasStoredCredentials() {
  if (!Prefs.SUPABASE_CLAIMED) return false;
  // API key optional: mint may recover via public IP match when key is missing/wrong.
  if (!Prefs.SUPABASE_PROJECT_URL[0]) return false;
  return true;
}

bool supabaseIsConnected() {
  if (!supabaseHasStoredCredentials()) return false;
  // Any successful HTTPS to Supabase this boot (mint, ping, sites, etc.).
  return Supabase.lastSuccessMs() != 0;
}

static void arborysNetHeaderMsg(const char* msg, uint16_t fg) {
#ifdef _USE_HEADER_INFO_ALERT
  HeaderInfoAlert(msg, fg, TFT_BLACK, 45);
#else
  (void)msg;
  (void)fg;
#endif
}

/** Wipe local ArborysNet claim fields and reset the client to defaults (no cloud call). */
static void supabaseClearClaimPrefsCore() {
  Prefs.SUPABASE_CLAIMED = false;
  Prefs.SUPABASE_API_KEY[0] = '\0';
  Prefs.SUPABASE_ANON_KEY[0] = '\0';
  Prefs.SUPABASE_USER_ID[0] = '\0';
  Prefs.SUPABASE_PROJECT_URL[0] = '\0';
  strncpy(Prefs.SITE_SLUG, "home", sizeof(Prefs.SITE_SLUG) - 1);
  Prefs.SITE_SLUG[sizeof(Prefs.SITE_SLUG) - 1] = '\0';
  Prefs.isUpToDate = false;
  s_arbBootCloudReady = false;

  SupabaseConfig cfg;
  cfg.clear();
  cfg.applyDefaults();
  SupabaseClient::macToString(ESP.getEfuseMac(), cfg.deviceMac);
  cfg.utcOffsetSec = Prefs.TimeZoneOffset;
  cfg.devType = (uint8_t)_MYTYPE;
  Supabase.begin(cfg);

  BootSecure boot;
  boot.setPrefs(true);
}

/** Treat device as locally unclaimed so ArborysNet TLS stops until user reclaims. */
static void supabaseMarkUnclaimedLocal(const char* reason) {
  supabaseClearClaimPrefsCore();
  arborysNetStoreError(reason ? reason : "auth failed — marked unclaimed", ERROR_ARBORYSNET_CLAIM);
  arborysNetHeaderMsg("ArbNet Auth", TFT_RED);
  SerialPrint("ArborysNet: marked unclaimed after auth failure", true);
}

bool supabaseClearClaimIfInvalidDevice() {
  if (!Supabase.isInvalidDeviceCredentials()) return false;
  supabaseMarkUnclaimedLocal("invalid device credentials");
  return true;
}

void supabaseQuitArborysNet() {
  supabaseClearClaimPrefsCore();
  arborysNetLogEvent("quit ArborysNet (local claim cleared)", EVENT_ARBORYSNET);
  arborysNetHeaderMsg("ArbNet Off", TFT_YELLOW);
  SerialPrint("ArborysNet: Quit. Local claim prefs cleared", true);
}

static void supabaseRefreshIdentity() {
  String ip = wifiReadyForNetwork() ? WiFi.localIP().toString() : String("0.0.0.0");
  Supabase.setDeviceIdentity(ip.c_str(), (uint8_t)_MYTYPE);
  Supabase.setUtcOffset(Prefs.TimeZoneOffset);
}

/** Fill own-device DTO (MAC + Prefs.DEVICENAME + local IP + type/site) and PATCH. */
static SupabaseError supabaseUpsertOwnDevice(bool fireAndForget = true) {
  supabaseRefreshIdentity();

  SupabaseDeviceDto d;
  memset(&d, 0, sizeof(d));

  const SupabaseConfig& cfg = Supabase.config();
  if (cfg.deviceMac[0]) {
    strncpy(d.deviceMac, cfg.deviceMac, sizeof(d.deviceMac) - 1);
  } else {
    SupabaseClient::macToString(ESP.getEfuseMac(), d.deviceMac);
  }

  if (Prefs.DEVICENAME[0]) {
    strncpy(d.name, Prefs.DEVICENAME, sizeof(d.name) - 1);
  }

  if (cfg.deviceIp[0] && strcmp(cfg.deviceIp, "0.0.0.0") != 0) {
    strncpy(d.deviceIp, cfg.deviceIp, sizeof(d.deviceIp) - 1);
  } else if (wifiReadyForNetwork()) {
    String ip = WiFi.localIP().toString();
    strncpy(d.deviceIp, ip.c_str(), sizeof(d.deviceIp) - 1);
  }

  d.devType = cfg.devType ? cfg.devType : (uint8_t)_MYTYPE;
  d.isActive = true;

  const char* site = supabaseSiteSlug();
  if (site && site[0]) {
    strncpy(d.siteSlug, site, sizeof(d.siteSlug) - 1);
  }

  // After Auth: fire-and-forget PATCH (full upsert fields; site_id not re-resolved).
  return Supabase.upsertDevice(d, fireAndForget);
}

static bool supabaseApplyCloudSite(const char* cloudSite) {
  if (!cloudSite || !cloudSite[0]) return false;
  const char* local = supabaseSiteSlug();
  if (strcmp(local, cloudSite) == 0) return false;

  strncpy(Prefs.SITE_SLUG, cloudSite, sizeof(Prefs.SITE_SLUG) - 1);
  Prefs.SITE_SLUG[sizeof(Prefs.SITE_SLUG) - 1] = '\0';
  Prefs.isUpToDate = false;

  SupabaseConfig cfg = Supabase.config();
  strncpy(cfg.siteSlug, cloudSite, sizeof(cfg.siteSlug) - 1);
  cfg.siteSlug[sizeof(cfg.siteSlug) - 1] = '\0';
  Supabase.begin(cfg);
  supabaseRefreshIdentity();

  BootSecure boot;
  boot.setPrefs(true);
  arborysNetLogEvent(("location updated to " + String(cloudSite)).c_str(), EVENT_ARBORYSNET_LOCATION);
  return true;
}

static void arbBootAuthWorker(void* /*arg*/) {
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  s_arbWorkerResult = (Supabase.ensureAuth() == SupabaseError::Ok) ? 1 : -1;
  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);
  s_arbWorkerTask = nullptr;
  vTaskDelete(nullptr);
}

static void arbBootPingWorker(void* /*arg*/) {
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  // upsertDevice F&F: name + IP + MAC (+ type) and last_seen (no response wait).
  s_arbWorkerResult = (supabaseUpsertOwnDevice(true) == SupabaseError::Ok) ? 1 : -1;
  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);
  s_arbWorkerTask = nullptr;
  vTaskDelete(nullptr);
}

static void arbBootQueryWorker(void* /*arg*/) {
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  s_arbQuerySite[0] = '\0';
  if (Supabase.fetchOwnSite(s_arbQuerySite, sizeof(s_arbQuerySite)) != SupabaseError::Ok) {
    s_arbWorkerResult = -1;
  } else {
    s_arbWorkerResult = 1;
  }
  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);
  s_arbWorkerTask = nullptr;
  vTaskDelete(nullptr);
}

/**
 * One TLS-heavy step per call, each run on a worker (not the loop stack):
 *   Auth (mint JWT) → Ping (upsertDevice fire-and-forget: name/IP/MAC). Query retained but unwired.
 * Auth failure clears local claim only on invalid_device.
 */
void supabaseServiceStartupSiteSync() {
#ifdef _USELOWPOWER
  return;
#else
  enum : uint8_t {
    ST_WAIT = 0,
    ST_AUTH = 1,
    ST_PING = 2,
    ST_QUERY = 3, // retained for later wiring; not entered from boot
    ST_DONE = 4,
    ST_DEAD = 5,
    ST_BUSY = 6
  };
  static uint8_t st = ST_WAIT;
  static uint8_t stAfterBusy = ST_WAIT;
  static uint32_t lastStepMs = 0;

  if (!Prefs.SUPABASE_CLAIMED) {
    st = ST_WAIT;
    s_arbBootCloudReady = false;
    return;
  }
  if (st == ST_DEAD) return;

  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) return;
  }
  if (!wifiReadyForNetwork()) return;

  const uint32_t nowMs = millis();

  if (st == ST_BUSY) {
    if (s_arbWorkerResult == 0) return;
    const int8_t res = s_arbWorkerResult;
    const uint8_t finished = stAfterBusy;
    lastStepMs = nowMs;

    if (finished == ST_AUTH) {
      if (res < 0) {
        arborysNetLogClientError("boot auth", ERROR_ARBORYSNET_CLAIM);
        arborysNetHeaderMsg("ArbNet Auth", TFT_RED);
        if (supabaseClearClaimIfInvalidDevice()) {
          st = ST_DEAD;
          return;
        }
        SerialPrint("ArborysNet: boot auth failed (keeping claim; will retry)", true);
        st = ST_AUTH;
        return;
      }
      arborysNetHeaderMsg("ArbNet Auth", TFT_GREEN);
      arborysNetLogEvent("boot auth ok", EVENT_ARBORYSNET);
      st = ST_PING;
      return;
    }

    if (finished == ST_PING) {
      if (res < 0) {
        arborysNetLogClientError("boot ping", ERROR_ARBORYSNET_SYNC);
        arborysNetHeaderMsg("ArbNet Png", TFT_RED);
        if (supabaseClearClaimIfInvalidDevice()) {
          st = ST_DEAD;
          return;
        }
        SerialPrint("ArborysNet: boot ping failed (keeping claim; cloud ready with soft fail)", true);
        st = ST_DONE;
        s_arbBootCloudReady = true;
        return;
      }
      arborysNetHeaderMsg("ArbNet Png", TFT_GREEN);
      arborysNetLogEvent("boot ping ok", EVENT_ARBORYSNET_UPLOAD);
      st = ST_DONE;
      s_arbBootCloudReady = true;
      return;
    }

    // ST_QUERY retained for later; if somehow finished, mark ready.
    if (finished == ST_QUERY) {
      if (res >= 0 && s_arbQuerySite[0]) {
        supabaseApplyCloudSite(s_arbQuerySite);
      }
      st = ST_DONE;
      s_arbBootCloudReady = true;
      return;
    }

    st = ST_DONE;
    return;
  }

  if (st == ST_WAIT) {
    if (nowMs < SUPABASE_BOOT_TLS_START_MS) return;
    st = ST_AUTH;
  }

  if (st == ST_DONE) {
    s_arbBootCloudReady = true;
    return; // Query / periodic site sync deferred
  }

  if (lastStepMs != 0 && (nowMs - lastStepMs) < SUPABASE_BOOT_STEP_GAP_MS) return;
  if (s_arbWorkerTask) return;

  auto launch = [&](uint8_t step, TaskFunction_t fn, const char* name) {
    s_arbWorkerResult = 0;
    stAfterBusy = step;
    st = ST_BUSY;
    if (xTaskCreatePinnedToCore(fn, name, ARB_TLS_WORKER_STACK, nullptr, 1, &s_arbWorkerTask, 1) != pdPASS) {
      s_arbWorkerTask = nullptr;
      s_arbWorkerResult = -1;
    }
  };

  if (st == ST_AUTH) {
    arborysNetHeaderMsg("ArbNet Auth", TFT_YELLOW);
    SerialPrint("ArborysNet: Auth (mint JWT if needed)", true);
    supabaseRefreshIdentity();
    launch(ST_AUTH, arbBootAuthWorker, "arbAuth");
    return;
  }

  if (st == ST_PING) {
    arborysNetHeaderMsg("ArbNet Png", TFT_YELLOW);
    SerialPrint("ArborysNet: Ping (upsertDevice F&F name/IP/MAC)", true);
    launch(ST_PING, arbBootPingWorker, "arbPing");
    return;
  }

  // ST_QUERY not launched from boot (kept for later wiring).
  (void)arbBootQueryWorker;
#endif
}

#ifndef _USELOWPOWER
static bool supabaseCloudQuietTooLong() {
  const uint32_t nowUnix = (uint32_t)utcNow();
  const uint32_t lastUnix = Supabase.lastSuccessUnix();
  if (nowUnix && lastUnix) {
    return (nowUnix - lastUnix) >= SUPABASE_KEEPALIVE_INTERVAL_SEC;
  }
  const uint32_t lastMs = Supabase.lastSuccessMs();
  if (lastMs == 0) return true;
  return (millis() - lastMs) >= (SUPABASE_KEEPALIVE_INTERVAL_SEC * 1000UL);
}

static bool supabaseSendKeepalive() {
  SerialPrint("ArborysNet: keepalive upsertDevice F&F (name/IP/MAC)", true);
  if (supabaseUpsertOwnDevice() != SupabaseError::Ok) {
    arborysNetLogClientError("keepalive", ERROR_ARBORYSNET_SYNC);
    return false;
  }
  arborysNetLogEvent("keepalive uploaded", EVENT_ARBORYSNET_UPLOAD);
  return true;
}

static bool supabasePeripheralOrphaned(uint32_t nowUnix) {
#if _IS_SERVER_HUB
  (void)nowUnix;
  return false;
#else
  if (!nowUnix) return false;
  if (I.lastServerHeardTime == 0) return true;
  return (nowUnix - (uint32_t)I.lastServerHeardTime) >= SUPABASE_ORPHAN_SERVER_SEC;
#endif
}

/** Hub with toggle on, or peripheral with no server contact for 6h. */
static bool supabaseMayUploadReadings(uint32_t nowUnix) {
#if _IS_SERVER_HUB
  (void)nowUnix;
  return Prefs.UPLOAD_TO_SUPABASE;
#else
  return supabasePeripheralOrphaned(nowUnix);
#endif
}

static bool supabaseReadingBatchIntervalElapsed(uint32_t nowUnix, uint32_t nowMs) {
  if (nowUnix && s_lastReadingUploadUnix) {
    return (nowUnix - s_lastReadingUploadUnix) >= SUPABASE_READING_UPLOAD_INTERVAL_SEC;
  }
  if (s_lastReadingUploadMs == 0) return true;
  return (nowMs - s_lastReadingUploadMs) >= (SUPABASE_READING_UPLOAD_INTERVAL_SEC * 1000UL);
}

static bool supabaseSensorNeedsCloudUpload(const ArborysSnsType* s) {
  if (!s || !s->IsSet) return false;
  const int16_t idx = Sensors.findSensorByPointer(const_cast<ArborysSnsType*>(s));
  const uint8_t flags = (idx >= 0) ? Sensors.effectiveSensorFlags(idx, true) : s->Flags;
  if (!bitRead(flags, 1)) return false;
  if (!s->timeRead) return false;
#if _HAS_LOCAL_SENSORS
  // Same gate as the hub uplink: do not upload a placeholder into cloud averages.
  if (s->deviceIndex == I.MY_DEVICE_INDEX && !localSensorReadyToSend(s)) return false;
#endif
  return s->timeRead > s->timeCloudUpload;
}

static bool supabaseAnyReadingDueForUpload() {
#if _IS_SERVER_HUB
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (supabaseSensorNeedsCloudUpload(s)) return true;
  }
  return false;
#else
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s || !s->IsSet) continue;
    if (s->deviceIndex != I.MY_DEVICE_INDEX) continue;
    if (supabaseSensorNeedsCloudUpload(s)) return true;
  }
  return false;
#endif
}

static void cloudAckPush(uint64_t mac, uint8_t snsType, uint8_t snsId, uint32_t tcu, const char* ip) {
  if (s_cloudAckCount >= CLOUD_ACK_MAX) return;
  CloudAckEntry& e = s_cloudAckBuf[s_cloudAckCount++];
  e.mac = mac;
  e.snsType = snsType;
  e.snsId = snsId;
  e.timeCloudUpload = tcu;
  e.ip[0] = '\0';
  if (ip && ip[0]) {
    strncpy(e.ip, ip, sizeof(e.ip) - 1);
    e.ip[sizeof(e.ip) - 1] = '\0';
  }
}

static void supabaseUploadReadingsDue(uint32_t nowUnix, uint32_t nowMs) {
  supabaseRefreshIdentity();
  s_cloudAckCount = 0;

  uint16_t sent = 0;
  uint16_t fail = 0;

  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!supabaseSensorNeedsCloudUpload(s)) continue;
#if !_IS_SERVER_HUB
    if (s->deviceIndex != I.MY_DEVICE_INDEX) continue;
#endif
    ArborysDevType* d = Sensors.getDeviceBySnsIndex(si);
    if (!d || !d->IsSet) continue;

    char ipBuf[16] = {0};
    if (d->IP != IPAddress(0, 0, 0, 0)) {
      snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", d->IP[0], d->IP[1], d->IP[2], d->IP[3]);
    }

    if (Supabase.insertReadingFromSensor(d->MAC, *s, ipBuf[0] ? ipBuf : nullptr) != SupabaseError::Ok) {
      fail++;
      arborysNetLogClientError("reading upload", ERROR_ARBORYSNET_UPLOAD);
      continue;
    }

    const uint32_t tcu = nowUnix ? nowUnix : (uint32_t)utcNow();
    s->timeCloudUpload = tcu;
    cloudAckPush(d->MAC, s->snsType, s->snsID, tcu, ipBuf);
    sent++;
  }

  s_lastReadingUploadUnix = nowUnix ? nowUnix : s_lastReadingUploadUnix;
  s_lastReadingUploadMs = nowMs;

  if (sent) {
    char ev[64];
    snprintf(ev, sizeof(ev), "uploaded %u reading%s", (unsigned)sent, sent == 1 ? "" : "s");
    arborysNetLogEvent(ev, EVENT_ARBORYSNET_UPLOAD);
#if _IS_SERVER_HUB
    if (s_cloudAckCount > 0) s_cloudAckReady = 1;
#endif
  }
  if (fail) {
    char err[64];
    snprintf(err, sizeof(err), "upload failed for %u reading%s", (unsigned)fail,
             fail == 1 ? "" : "s");
    arborysNetStoreError(err, ERROR_ARBORYSNET_UPLOAD);
  }
}

static void arbCloudWorker(void* /*arg*/) {
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  const uint8_t job = s_cloudJob;
  if (job == CLOUD_JOB_KEEPALIVE) {
    const bool ok = supabaseSendKeepalive();
    s_cloudKeepaliveUi = ok ? 1 : -1;
  } else if (job == CLOUD_JOB_READINGS) {
    const uint32_t nowMs = millis();
    const uint32_t nowUnix = (uint32_t)utcNow();
    supabaseUploadReadingsDue(nowUnix, nowMs);
  }
  s_cloudJob = CLOUD_JOB_NONE;
  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);
  s_arbCloudTask = nullptr;
  vTaskDelete(nullptr);
}

static bool launchArbCloudJob(uint8_t job, const char* name) {
  if (s_arbCloudTask || s_arbWorkerTask) return false;
  if (SupabaseClient::isTlsBusy()) return false;
  s_cloudJob = job;
  if (xTaskCreatePinnedToCore(arbCloudWorker, name, ARB_TLS_WORKER_STACK, nullptr, 1,
                              &s_arbCloudTask, 1) != pdPASS) {
    s_arbCloudTask = nullptr;
    s_cloudJob = CLOUD_JOB_NONE;
    return false;
  }
  return true;
}

#if _IS_SERVER_HUB
static void supabaseBroadcastCloudAckPending() {
  if (!s_cloudAckReady) return;
  const uint8_t n = s_cloudAckCount;
  s_cloudAckReady = 0;
  if (n == 0) return;

  // Compact: {"msgType":"cloudAck","s":[{"m":"...","t":1,"i":2,"u":123,"p":"1.2.3.4"},...]}
  String json = "{\"msgType\":\"cloudAck\",\"s\":[";
  for (uint8_t i = 0; i < n; i++) {
    if (i) json += ',';
    json += "{\"m\":\"";
    json += MACToString(s_cloudAckBuf[i].mac, '\0', true);
    json += "\",\"t\":";
    json += String(s_cloudAckBuf[i].snsType);
    json += ",\"i\":";
    json += String(s_cloudAckBuf[i].snsId);
    json += ",\"u\":";
    json += String(s_cloudAckBuf[i].timeCloudUpload);
    if (s_cloudAckBuf[i].ip[0]) {
      json += ",\"p\":\"";
      json += s_cloudAckBuf[i].ip;
      json += '"';
    }
    json += '}';
  }
  json += "]}";

#ifdef _USEUDP
  SerialPrint("ArborysNet: cloudAck UDP broadcast (" + String(n) + ")", true);
  sendUDPMessage((const uint8_t*)json.c_str(), IPAddress(0, 0, 0, 0), (uint16_t)json.length(), "cloudAck");
#endif

  for (int16_t di = 0; di < NUMDEVICES; di++) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) continue;
    if (di == I.MY_DEVICE_INDEX) continue;
    if (d->IP == IPAddress(0, 0, 0, 0)) continue;
    if (deviceUdpPingRateAbove50(d)) continue; // UDP broadcast sufficient
    SerialPrint("ArborysNet: cloudAck HTTP to " + String(d->devName), true);
    sendHTTPJSON(d->IP, json.c_str(), "cloudAck");
  }

  s_cloudAckCount = 0;
}
#endif
#endif // !_USELOWPOWER

void supabaseServiceCloudSync(bool force) {
  (void)force;
#ifdef _USELOWPOWER
  return; // low-power sensors do not talk to Supabase
#else
  static uint32_t lastKeepaliveAttemptMs = 0;

  if (!Prefs.SUPABASE_CLAIMED) return;
  if (!s_arbBootCloudReady) return; // wait for Auth→Ping pipeline
  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) return;
  }
  if (!wifiReadyForNetwork()) return;

#if _IS_SERVER_HUB
  HubCloudTryLock cloudLock;
  if (!cloudLock) return;
  if (s_cloudAckReady) {
    supabaseBroadcastCloudAckPending();
  }
#endif

  const uint32_t nowMs = millis();
  const uint32_t nowUnix = (uint32_t)utcNow();

  if (s_cloudKeepaliveUi == 1 || s_cloudKeepaliveUi == -1) {
#ifdef _USE_HEADER_INFO_ALERT
    HeaderInfoAlert("ArbNet Png", s_cloudKeepaliveUi > 0 ? TFT_GREEN : TFT_RED, TFT_BLACK, 45);
#endif
    s_cloudKeepaliveUi = 0;
  }

  if (s_arbCloudTask || s_arbWorkerTask) return;

  bool wantReadings = false;
  if (supabaseMayUploadReadings(nowUnix) && supabaseAnyReadingDueForUpload()) {
#if _IS_SERVER_HUB
    if (I.makeCloudUpload) {
      I.makeCloudUpload = false;
      wantReadings = true;
    } else if (force) {
      wantReadings = true;
    }
#else
    // Peripheral orphan path: max every 10 minutes
    if (force || supabaseReadingBatchIntervalElapsed(nowUnix, nowMs)) {
      wantReadings = true;
    }
#endif
  }

  if (wantReadings) {
    (void)launchArbCloudJob(CLOUD_JOB_READINGS, "arbRead");
    return;
  }

  if (supabaseCloudQuietTooLong()) {
    if (lastKeepaliveAttemptMs != 0 && (nowMs - lastKeepaliveAttemptMs) < 60000UL) {
      return;
    }
    lastKeepaliveAttemptMs = nowMs;
#ifdef _USE_HEADER_INFO_ALERT
    HeaderInfoAlert("ArbNet Png", TFT_YELLOW, TFT_BLACK, 45);
#endif
    s_cloudKeepaliveUi = 2;
    if (!launchArbCloudJob(CLOUD_JOB_KEEPALIVE, "arbKeep")) {
#ifdef _USE_HEADER_INFO_ALERT
      HeaderInfoAlert("ArbNet Png", TFT_RED, TFT_BLACK, 45);
#endif
      s_cloudKeepaliveUi = 0;
      SerialPrint("ArborysNet: keepalive worker launch failed", true);
    }
  }
#endif
}

#if _IS_SERVER_HUB
#ifndef SUPABASE_HUB_INVENTORY_INTERVAL_MS
#define SUPABASE_HUB_INVENTORY_INTERVAL_MS (12UL * 3600UL * 1000UL)
#endif
#ifndef SUPABASE_HUB_INVENTORY_LOOKBACK_SEC
#define SUPABASE_HUB_INVENTORY_LOOKBACK_SEC (24UL * 3600UL)
#endif

bool supabaseHubInventorySync(SupabaseHubInventoryResult* out) {
  SupabaseHubInventoryResult local = {};
  local.ok = false;
  if (out) *out = local;

#ifdef _USELOWPOWER
  if (out) strncpy(out->error, "low_power", sizeof(out->error) - 1);
  return false;
#else
  if (!supabaseHasStoredCredentials()) {
    if (out) strncpy(out->error, "not_claimed", sizeof(out->error) - 1);
    return false;
  }
  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) {
      if (out) strncpy(out->error, "not_configured", sizeof(out->error) - 1);
      return false;
    }
  }
  if (!wifiReadyForNetwork()) {
    if (out) strncpy(out->error, "wifi_required", sizeof(out->error) - 1);
    return false;
  }

  const uint32_t nowUnix = (uint32_t)utcNow();
  if (nowUnix < 1000000000UL) {
    if (out) strncpy(out->error, "time_invalid", sizeof(out->error) - 1);
    return false;
  }

  supabaseRefreshIdentity();
  const char* site = supabaseSiteSlug();
  const uint64_t myMac = ESP.getEfuseMac();

  // Page devices for this site only (never dump all user devices).
  uint16_t totalDevices = 0;
  if (Supabase.countDevicesForSite(site, &totalDevices) != SupabaseError::Ok) {
    if (out) {
      strncpy(out->error, Supabase.lastErrorCode(), sizeof(out->error) - 1);
    }
    arborysNetLogClientError("inventory device count", ERROR_ARBORYSNET_SYNC);
    return false;
  }

  static SupabaseDeviceDto s_devBuf[ARBORYSNET_HUB_DEVICE_PAGE];
  static SupabaseSensorDto s_snsBuf[32];

  uint16_t sensorsAdded = 0;
  uint16_t devicesAdded = 0;
  uint16_t sensorsQueried = 0;
  char macCsv[ARBORYSNET_HUB_DEVICE_PAGE * 13]; // AABBCCDDEEFF + comma

  for (uint16_t offset = 0; offset < totalDevices || (totalDevices == 0 && offset == 0); ) {
    esp_task_wdt_reset();
    uint16_t pageCount = 0;
    SupabaseQueryFilter df;
    memset(&df, 0, sizeof(df));
    df.table = "devices";
    df.site = site;
    df.snsType = -1;
    df.expired = -1;
    df.limit = ARBORYSNET_HUB_DEVICE_PAGE;
    df.offset = offset;
    if (Supabase.queryDevices(df, s_devBuf, ARBORYSNET_HUB_DEVICE_PAGE, &pageCount) !=
        SupabaseError::Ok) {
      if (out) {
        strncpy(out->error, Supabase.lastErrorCode(), sizeof(out->error) - 1);
      }
      arborysNetLogClientError("inventory devices", ERROR_ARBORYSNET_SYNC);
      return false;
    }
    if (pageCount == 0) break;

    macCsv[0] = '\0';
    size_t macLen = 0;
    for (uint16_t i = 0; i < pageCount; i++) {
      if (!s_devBuf[i].deviceMac[0]) continue;
      if (macLen + 13 >= sizeof(macCsv)) break;
      if (macLen) macCsv[macLen++] = ',';
      size_t m = strnlen(s_devBuf[i].deviceMac, 12);
      memcpy(macCsv + macLen, s_devBuf[i].deviceMac, m);
      macLen += m;
      macCsv[macLen] = '\0';
    }

    uint16_t snsCount = 0;
    if (macLen > 0) {
      SupabaseQueryFilter sf;
      memset(&sf, 0, sizeof(sf));
      sf.table = "sensors";
      sf.deviceMacIn = macCsv;
      sf.snsType = -1;
      sf.expired = 0;
      sf.timeStartUnix = nowUnix - SUPABASE_HUB_INVENTORY_LOOKBACK_SEC;
      sf.limit = 32;
      if (Supabase.querySensors(sf, s_snsBuf, 32, &snsCount) != SupabaseError::Ok) {
        if (out) {
          strncpy(out->error, Supabase.lastErrorCode(), sizeof(out->error) - 1);
        }
        arborysNetLogClientError("inventory sensors", ERROR_ARBORYSNET_SYNC);
        return false;
      }
    }
    sensorsQueried = (uint16_t)(sensorsQueried + snsCount);

    auto findDevMeta = [&](const char* macStr) -> const SupabaseDeviceDto* {
      for (uint16_t i = 0; i < pageCount; i++) {
        if (strcasecmp(s_devBuf[i].deviceMac, macStr) == 0) return &s_devBuf[i];
      }
      return nullptr;
    };

    for (uint16_t i = 0; i < snsCount; i++) {
      const SupabaseSensorDto& s = s_snsBuf[i];
      uint64_t mac = SupabaseClient::macFromString(s.deviceMac);
      if (mac == 0 || mac == myMac) continue;
      if (Sensors.findSensor(mac, s.snsType, s.snsId) >= 0) continue;

      const bool wasKnownDevice = (Sensors.findDevice(mac) >= 0);
      IPAddress ip(0, 0, 0, 0);
      const char* devName = "";
      uint8_t devType = 0;
      const SupabaseDeviceDto* meta = findDevMeta(s.deviceMac);
      if (meta) {
        if (meta->deviceIp[0]) ip.fromString(meta->deviceIp);
        if (meta->name[0]) devName = meta->name;
        devType = meta->devType;
      }

      int16_t idx = Sensors.addSensor(
          mac, ip, s.snsType, s.snsId, s.snsName, s.snsValue,
          s.timeRead, s.timeLogged, s.sendingInt ? s.sendingInt : 300, s.flags,
          devName, devType, -9999, -9999,
          s.limitHigh, s.limitLow, !isnan(s.limitHigh), !isnan(s.limitLow));
      if (idx >= 0) {
        sensorsAdded++;
        if (!wasKnownDevice) devicesAdded++;
      }
    }

    offset = (uint16_t)(offset + pageCount);
    if (pageCount < ARBORYSNET_HUB_DEVICE_PAGE) break;
    // Soft cap: at most 5 pages (~100 devices) per inventory pass.
    if (offset >= ARBORYSNET_HUB_DEVICE_PAGE * 5) break;
  }

  if (sensorsAdded > 0) {
#ifdef _USESDCARD
    storeDevicesSensorsSD();
#endif
  }

  {
    char ev[72];
    snprintf(ev, sizeof(ev), "inventory ok nDev=%u q=%u +sns=%u +dev=%u",
             (unsigned)totalDevices, (unsigned)sensorsQueried,
             (unsigned)sensorsAdded, (unsigned)devicesAdded);
    arborysNetLogEvent(ev, EVENT_ARBORYSNET_LOCATION);
  }

  if (out) {
    out->ok = true;
    out->sensorsQueried = sensorsQueried;
    out->sensorsAdded = sensorsAdded;
    out->devicesAdded = devicesAdded;
    out->error[0] = '\0';
  }
  return true;
#endif
}

void supabaseHubPollTick() {
#ifdef _USELOWPOWER
  return;
#else
  supabaseHubExpiredPollApplyPending();
  if (!Prefs.SUPABASE_CLAIMED || !Supabase.config().isReady()) return;
  if (!wifiReadyForNetwork()) return;

  static uint32_t lastPollMs = 0;
  const uint32_t nowMs = millis();
  if (lastPollMs != 0 && (nowMs - lastPollMs) < SUPABASE_HUB_INVENTORY_INTERVAL_MS) return;

  HubCloudTryLock cloudLock;
  if (!cloudLock) return; // expired-poll worker has TLS; retry next tick
  lastPollMs = nowMs;

  supabaseHubInventorySync(nullptr);
#endif
}

#ifndef _USELOWPOWER
static constexpr UBaseType_t EXPIRED_POLL_QUEUE_DEPTH = 8;

struct ExpiredPollJob {
  uint64_t mac;
};

enum : uint8_t { EXP_POLL_IDLE = 0, EXP_POLL_READY = 1 };

struct ExpiredPollResult {
  volatile uint8_t state;
  uint8_t queryOk;
  uint64_t mac;
  char deviceIp[16];
  uint16_t snsCount;
  SupabaseSensorDto sns[32];
};

static QueueHandle_t s_expiredPollQueue = nullptr;
static TaskHandle_t s_expiredPollTask = nullptr;
static ExpiredPollResult s_expiredPollResult = {};
static bool s_expiredPollQueued[NUMDEVICES] = {};
// lastN[si] = last SendingInt multiple (N≥2) already queried for sensor index si
static uint16_t s_expiredLastN[NUMSENSORS] = {0};

static bool expiredSensorsDueForCloud(int16_t devIndex, uint32_t nowUnix, uint16_t* dueNOut) {
  bool anyDue = false;
  if (dueNOut) memset(dueNOut, 0, sizeof(uint16_t) * NUMSENSORS);

  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s || !s->IsSet || s->deviceIndex != devIndex) continue;
    if (!s->expired) {
      s_expiredLastN[si] = 0; // recovered → allow future cycles
      continue;
    }
    const uint32_t sendInt = s->SendingInt ? s->SendingInt : 300;
    const uint32_t freshness = s->timeLogged ? s->timeLogged : s->timeRead;
    if (!freshness || nowUnix <= freshness) continue;

    const uint32_t age = nowUnix - freshness;
    const uint32_t n = age / sendInt;
    if (n < 2) continue; // only at 2×, 3×, …
    if (n <= s_expiredLastN[si]) continue;

    if (dueNOut) dueNOut[si] = (uint16_t)((n > 65535UL) ? 65535UL : n);
    anyDue = true;
  }
  return anyDue;
}

static void supabaseHubExpiredPollApplyPending() {
  if (s_expiredPollResult.state != EXP_POLL_READY) return;

  const uint64_t mac = s_expiredPollResult.mac;
  const uint8_t queryOk = s_expiredPollResult.queryOk;
  const uint16_t snsCount = s_expiredPollResult.snsCount;
  char deviceIp[16];
  strncpy(deviceIp, s_expiredPollResult.deviceIp, sizeof(deviceIp) - 1);
  deviceIp[sizeof(deviceIp) - 1] = '\0';

  static SupabaseSensorDto snsCopy[32];
  uint16_t copyCount = (snsCount > 32) ? 32 : snsCount;
  if (queryOk && copyCount > 0) {
    memcpy(snsCopy, s_expiredPollResult.sns, sizeof(SupabaseSensorDto) * copyCount);
  }

  s_expiredPollResult.state = EXP_POLL_IDLE;
  if (!queryOk) return;

  ArborysDevType* device = Sensors.getDeviceByMAC(mac);
  if (!device || !device->IsSet) return;

  const int16_t devIndex = Sensors.findDevice(mac);
  if (devIndex < 0) return;

  const uint32_t nowUnix = (uint32_t)utcNow();
  uint16_t dueN[NUMSENSORS];
  if (!nowUnix || !expiredSensorsDueForCloud(devIndex, nowUnix, dueN)) {
    return; // recovered or no longer in an N× window; lastN unchanged so we can retry
  }

  if (deviceIp[0]) {
    IPAddress ip;
    if (ip.fromString(deviceIp) && ip != device->IP) {
      device->IP = ip;
    }
  }

  uint16_t applied = 0;
  uint16_t marked = 0;
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    if (!dueN[si]) continue;
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s || !s->IsSet) continue;

    const SupabaseSensorDto* cloud = nullptr;
    for (uint16_t i = 0; i < copyCount; i++) {
      if (snsCopy[i].snsType == s->snsType && snsCopy[i].snsId == s->snsID) {
        cloud = &snsCopy[i];
        break;
      }
    }

    // Advance multiple even if cloud has nothing newer — one attempt per N× window
    s_expiredLastN[si] = dueN[si];
    marked++;

    if (!cloud) continue;
    if (cloud->timeLogged && s->timeLogged && cloud->timeLogged <= s->timeLogged) continue;

    int16_t idx = Sensors.addSensor(
        device->MAC, device->IP, cloud->snsType, cloud->snsId, cloud->snsName, cloud->snsValue,
        cloud->timeRead, cloud->timeLogged ? cloud->timeLogged : cloud->timeRead,
        cloud->sendingInt ? cloud->sendingInt : s->SendingInt, cloud->flags,
        device->devName, device->devType, -9999, -9999,
        cloud->limitHigh, cloud->limitLow, !isnan(cloud->limitHigh), !isnan(cloud->limitLow));
    if (idx >= 0) applied++;
  }

  if (applied > 0) {
#ifdef _USESDCARD
    storeDevicesSensorsSD();
#endif
  }

  if (applied > 0 || marked > 0) {
    char ev[72];
    snprintf(ev, sizeof(ev), "expired poll %s applied=%u", device->devName, (unsigned)applied);
    arborysNetLogEvent(ev, EVENT_ARBORYSNET);
  }
}

static void expiredPollWorkerTask(void* /*arg*/) {
  ExpiredPollJob job;
  for (;;) {
    if (xQueueReceive(s_expiredPollQueue, &job, portMAX_DELAY) != pdTRUE) continue;

    const int16_t di = Sensors.findDevice(job.mac);
    if (di >= 0 && di < NUMDEVICES) s_expiredPollQueued[di] = false;

    while (s_expiredPollResult.state != EXP_POLL_IDLE) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    hubCloudGateEnsure();
    if (s_hubCloudGate) xSemaphoreTake(s_hubCloudGate, portMAX_DELAY);

    s_expiredPollResult.queryOk = 0;
    s_expiredPollResult.mac = job.mac;
    s_expiredPollResult.deviceIp[0] = '\0';
    s_expiredPollResult.snsCount = 0;

    bool ok = false;
    if (SupabaseClient::isTlsBusy()) {
      // Do not block on TLS; leave lastN unchanged (queryOk=0) so this sensor retries later.
      SerialPrint("expired poll: TLS busy, skipping device for now", true);
    } else if (wifiReadyForNetwork() && Prefs.SUPABASE_CLAIMED && Supabase.config().isReady()) {
      char macStr[16];
      SupabaseClient::macToString(job.mac, macStr);

      supabaseRefreshIdentity();
      uint16_t snsCount = 0;
      SupabaseQueryFilter sf;
      memset(&sf, 0, sizeof(sf));
      sf.table = "sensors";
      sf.deviceMac = macStr;
      sf.snsType = -1;
      sf.expired = -1;
      sf.limit = 32;

      if (Supabase.querySensors(sf, s_expiredPollResult.sns, 32, &snsCount) == SupabaseError::Ok) {
        s_expiredPollResult.snsCount = snsCount;
        ok = true;

        SupabaseDeviceDto drow;
        uint16_t dcount = 0;
        SupabaseQueryFilter df;
        memset(&df, 0, sizeof(df));
        df.table = "devices";
        df.deviceMac = macStr;
        df.snsType = -1;
        df.expired = -1;
        df.limit = 1;
        if (Supabase.queryDevices(df, &drow, 1, &dcount) == SupabaseError::Ok && dcount > 0 &&
            drow.deviceIp[0]) {
          strncpy(s_expiredPollResult.deviceIp, drow.deviceIp, sizeof(s_expiredPollResult.deviceIp) - 1);
          s_expiredPollResult.deviceIp[sizeof(s_expiredPollResult.deviceIp) - 1] = '\0';
        }
      } else if (strcmp(Supabase.lastErrorCode(), "tls_busy") == 0) {
        SerialPrint("expired poll: TLS became busy, skip", true);
      } else {
        arborysNetLogClientError("expired poll", ERROR_ARBORYSNET_SYNC);
      }
    }

    s_expiredPollResult.queryOk = ok ? 1 : 0;
    s_expiredPollResult.state = EXP_POLL_READY;
    if (s_hubCloudGate) xSemaphoreGive(s_hubCloudGate);
  }
}

static bool ensureExpiredPollWorker() {
  if (s_expiredPollQueue) return true;
  s_expiredPollQueue = xQueueCreate(EXPIRED_POLL_QUEUE_DEPTH, sizeof(ExpiredPollJob));
  if (!s_expiredPollQueue) return false;
  BaseType_t created = xTaskCreatePinnedToCore(
      expiredPollWorkerTask, "expPoll", ARB_TLS_WORKER_STACK, nullptr, 1, &s_expiredPollTask, 1);
  if (created != pdPASS) {
    vQueueDelete(s_expiredPollQueue);
    s_expiredPollQueue = nullptr;
    s_expiredPollTask = nullptr;
    return false;
  }
  return true;
}

static bool queueExpiredPoll(uint64_t mac) {
  const int16_t di = Sensors.findDevice(mac);
  if (di < 0 || di >= NUMDEVICES) return false;
  if (s_expiredPollQueued[di]) return true;
  if (!ensureExpiredPollWorker()) return false;

  ExpiredPollJob job = {};
  job.mac = mac;
  if (xQueueSend(s_expiredPollQueue, &job, 0) != pdTRUE) {
    SerialPrint("expired poll: queue full", true);
    return false;
  }
  s_expiredPollQueued[di] = true;
  return true;
}
#endif

void supabaseHubPollExpiredAfterLan(ArborysDevType* device) {
#ifdef _USELOWPOWER
  (void)device;
  return;
#else
  supabaseHubExpiredPollApplyPending();

  if (!device || !device->IsSet || !device->expired) return;
  if (!Prefs.SUPABASE_CLAIMED || !s_arbBootCloudReady) return;
  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) return;
  }
  if (!wifiReadyForNetwork()) return;
  // Shared TLS held by boot/sites/etc. — skip this device for now; LAN cycle will retry.
  if (SupabaseClient::isTlsBusy()) return;

  const uint32_t nowUnix = (uint32_t)utcNow();
  if (!nowUnix) return;

  const int16_t devIndex = Sensors.findDevice(device->MAC);
  if (devIndex < 0) return;

  if (!expiredSensorsDueForCloud(devIndex, nowUnix, nullptr)) return;

  if (!queueExpiredPoll(device->MAC)) {
    SerialPrint("expired poll: could not queue " + String(device->devName), true);
  }
#endif
}
#endif

#endif
