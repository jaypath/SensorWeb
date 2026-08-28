#ifdef _USESUPABASE

#include "supabase_prefs.hpp"
#include "globals.hpp"
#include "BootSecure.hpp"
#include "server.hpp"
#include "utility.hpp"
#include "Devices.hpp"
#include <SupabaseClient.hpp>
#ifdef _USESDCARD
#include "SDCard.hpp"
#endif
#include <strings.h>
#include <string.h>

#ifndef SUPABASE_SITE_SYNC_INTERVAL_MS
#define SUPABASE_SITE_SYNC_INTERVAL_MS (3UL * 60UL * 60UL * 1000UL) // 3 hours
#endif
#ifndef SUPABASE_READING_UPLOAD_INTERVAL_SEC
#define SUPABASE_READING_UPLOAD_INTERVAL_SEC 3600UL // never faster than hourly
#endif
#ifndef SUPABASE_KEEPALIVE_INTERVAL_SEC
#define SUPABASE_KEEPALIVE_INTERVAL_SEC (3UL * 3600UL)
#endif

void supabaseBeginFromPrefs() {
  if (!Prefs.SUPABASE_CLAIMED) return;
  if (!Prefs.SUPABASE_PROJECT_URL[0] || !Prefs.SUPABASE_ANON_KEY[0] || !Prefs.SUPABASE_API_KEY[0]) {
    return;
  }

  SupabaseConfig cfg;
  cfg.clear();
  strncpy(cfg.projectUrl, Prefs.SUPABASE_PROJECT_URL, sizeof(cfg.projectUrl) - 1);
  strncpy(cfg.anonKey, Prefs.SUPABASE_ANON_KEY, sizeof(cfg.anonKey) - 1);
  strncpy(cfg.apiKey, Prefs.SUPABASE_API_KEY, sizeof(cfg.apiKey) - 1);
  strncpy(cfg.userId, Prefs.SUPABASE_USER_ID, sizeof(cfg.userId) - 1);
  strncpy(cfg.siteSlug, supabaseSiteSlug(), sizeof(cfg.siteSlug) - 1);
  SupabaseClient::macToString(ESP.getEfuseMac(), cfg.deviceMac);
  cfg.utcOffsetSec = Prefs.TimeZoneOffset;
  cfg.devType = (uint8_t)_MYTYPE;
  if (wifiReadyForNetwork()) {
    String ip = WiFi.localIP().toString();
    strncpy(cfg.deviceIp, ip.c_str(), sizeof(cfg.deviceIp) - 1);
  }
  cfg.applyDefaults();
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

static void supabaseRefreshIdentity() {
  String ip = wifiReadyForNetwork() ? WiFi.localIP().toString() : String("0.0.0.0");
  Supabase.setDeviceIdentity(ip.c_str(), (uint8_t)_MYTYPE);
  Supabase.setUtcOffset(Prefs.TimeZoneOffset);
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
  SerialPrint("Supabase site sync: updated " + String(local) + " -> " + String(cloudSite), true);
  return true;
}

void supabaseServiceStartupSiteSync() {
#ifdef _USELOWPOWER
  return; // low-power sensors do not talk to Supabase
#else
  static bool bootSynced = false;
  static uint8_t bootAttempts = 0;
  static uint32_t lastAttemptMs = 0;
  static uint32_t lastPeriodicMs = 0;

  if (!Prefs.SUPABASE_CLAIMED) return;
  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) return;
  }
  if (!wifiReadyForNetwork()) return;

  const uint32_t nowMs = millis();
  const bool duePeriodic = bootSynced &&
      (lastPeriodicMs == 0 || (nowMs - lastPeriodicMs) >= SUPABASE_SITE_SYNC_INTERVAL_MS);
  const bool dueBoot = !bootSynced;

  if (!dueBoot && !duePeriodic) return;

  if (dueBoot) {
    if (bootAttempts > 0 && (nowMs - lastAttemptMs) < 15000UL) return;
    if (bootAttempts >= 5) {
      bootSynced = true;
      lastPeriodicMs = nowMs;
      SerialPrint("Supabase site sync: giving up boot retries; will retry every 3h", true);
      return;
    }
    bootAttempts++;
  }

  lastAttemptMs = nowMs;
  supabaseRefreshIdentity();
  char cloudSite[33];
  if (Supabase.fetchOwnSite(cloudSite, sizeof(cloudSite)) != SupabaseError::Ok) {
    SerialPrint("Supabase site sync failed: " + String(Supabase.lastErrorCode()) + " " +
                    String(Supabase.lastErrorMessage()),
                true);
    if (duePeriodic) lastPeriodicMs = nowMs;
    return;
  }

  if (!supabaseApplyCloudSite(cloudSite)) {
    SerialPrint("Supabase site sync: unchanged (" + String(cloudSite) + ")", true);
  }

  bootSynced = true;
  lastPeriodicMs = nowMs;
#endif
}

#ifndef _USELOWPOWER
static bool supabaseCloudQuietTooLong() {
  const uint32_t nowUnix = (I.currentTime > 1000000000UL) ? I.currentTime : 0;
  const uint32_t lastUnix = Supabase.lastSuccessUnix();
  if (nowUnix && lastUnix) {
    return (nowUnix - lastUnix) >= SUPABASE_KEEPALIVE_INTERVAL_SEC;
  }
  const uint32_t lastMs = Supabase.lastSuccessMs();
  if (lastMs == 0) return true;
  return (millis() - lastMs) >= (SUPABASE_KEEPALIVE_INTERVAL_SEC * 1000UL);
}

static bool supabaseSendKeepalive() {
  supabaseRefreshIdentity();
  SupabaseDeviceDto d;
  memset(&d, 0, sizeof(d));
  strncpy(d.deviceMac, Supabase.config().deviceMac, sizeof(d.deviceMac) - 1);
  strncpy(d.deviceIp, Supabase.config().deviceIp, sizeof(d.deviceIp) - 1);
  d.devType = Supabase.config().devType;
  strncpy(d.name, Prefs.DEVICENAME, sizeof(d.name) - 1);
  strncpy(d.siteSlug, supabaseSiteSlug(), sizeof(d.siteSlug) - 1);
  d.isActive = true;
  d.firmwareMajor = Prefs.FIRMWARE.v[0];
  d.firmwareMinor = Prefs.FIRMWARE.v[1];
  d.firmwarePatch = Prefs.FIRMWARE.v[2];

  if (Supabase.upsertDevice(d) != SupabaseError::Ok) {
    SerialPrint("Supabase keepalive failed: " + String(Supabase.lastErrorCode()), true);
    return false;
  }
  SerialPrint("Supabase keepalive ok IP=" + String(d.deviceIp) + " type=" + String(d.devType), true);
  return true;
}

#if _HAS_LOCAL_SENSORS
/** Never faster than 1h; if sensor SendingInt is longer, use that. */
static uint32_t supabaseReadingIntervalSec(const ArborysSnsType* s) {
  uint32_t sendInt = (s && s->SendingInt) ? s->SendingInt : SUPABASE_READING_UPLOAD_INTERVAL_SEC;
  if (sendInt < SUPABASE_READING_UPLOAD_INTERVAL_SEC) {
    sendInt = SUPABASE_READING_UPLOAD_INTERVAL_SEC;
  }
  return sendInt;
}

static bool supabaseSensorReadingDue(int16_t si, const ArborysSnsType* s, uint32_t nowUnix,
                                     uint32_t nowMs, const uint32_t* lastUnix, const uint32_t* lastMs) {
  const uint32_t interval = supabaseReadingIntervalSec(s);
  if (nowUnix && lastUnix[si]) {
    return (nowUnix - lastUnix[si]) >= interval;
  }
  if (lastMs[si] == 0) return true;
  return (nowMs - lastMs[si]) >= (interval * 1000UL);
}

static void supabaseUploadLocalReadingsDue(uint32_t nowUnix, uint32_t nowMs,
                                           uint32_t* lastUnix, uint32_t* lastMs) {
  supabaseRefreshIdentity();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(I.MY_DEVICE_INDEX);
  if (!me || !me->IsSet) return;

  const char* ip = Supabase.config().deviceIp;
  uint16_t sent = 0;
  uint16_t fail = 0;
  uint16_t skipped = 0;

  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s || !s->IsSet) continue;
    if (s->deviceIndex != I.MY_DEVICE_INDEX) continue;

    if (!supabaseSensorReadingDue(si, s, nowUnix, nowMs, lastUnix, lastMs)) {
      skipped++;
      continue;
    }

    if (Supabase.insertReadingFromSensor(me->MAC, *s, ip) != SupabaseError::Ok) {
      fail++;
      const uint32_t interval = supabaseReadingIntervalSec(s);
      lastMs[si] = nowMs - (interval * 1000UL) + 300000UL;
      if (nowUnix) lastUnix[si] = nowUnix - interval + 300;
    } else {
      sent++;
      lastMs[si] = nowMs;
      lastUnix[si] = nowUnix ? nowUnix : 0;
    }
  }

  if (sent || fail) {
    SerialPrint("Supabase readings upload sent=" + String(sent) + " fail=" + String(fail) +
                    " skipped=" + String(skipped),
                true);
  }
}
#endif
#endif // !_USELOWPOWER

void supabaseServiceCloudSync(bool force) {
  (void)force;
#ifdef _USELOWPOWER
  return; // low-power sensors do not talk to Supabase
#else
  static uint32_t lastKeepaliveAttemptMs = 0;
#if _HAS_LOCAL_SENSORS
  static uint32_t lastReadingUnix[NUMSENSORS] = {0};
  static uint32_t lastReadingMs[NUMSENSORS] = {0};
#endif

  if (!Prefs.SUPABASE_CLAIMED) return;
  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) return;
  }
  if (!wifiReadyForNetwork()) return;

  const uint32_t nowMs = millis();
  const uint32_t nowUnix = (I.currentTime > 1000000000UL) ? I.currentTime : 0;

#if _HAS_LOCAL_SENSORS
  supabaseUploadLocalReadingsDue(nowUnix, nowMs, lastReadingUnix, lastReadingMs);
#endif

  if (supabaseCloudQuietTooLong()) {
    if (lastKeepaliveAttemptMs != 0 && (nowMs - lastKeepaliveAttemptMs) < 60000UL) {
      return;
    }
    lastKeepaliveAttemptMs = nowMs;
    supabaseSendKeepalive();
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
  if (!Prefs.SUPABASE_CLAIMED) {
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

  const uint32_t nowUnix = (I.currentTime > 1000000000UL) ? I.currentTime : (uint32_t)time(nullptr);
  if (nowUnix < 1000000000UL) {
    if (out) strncpy(out->error, "time_invalid", sizeof(out->error) - 1);
    return false;
  }

  supabaseRefreshIdentity();
  const char* site = supabaseSiteSlug();
  const uint64_t myMac = ESP.getEfuseMac();

  // Device metadata for the site (IP / name / type)
  SupabaseDeviceDto devBuf[48];
  uint16_t devCount = 0;
  {
    SupabaseQueryFilter df;
    memset(&df, 0, sizeof(df));
    df.table = "devices";
    df.site = site;
    df.snsType = -1;
    df.expired = -1;
    df.limit = 48;
    if (Supabase.queryDevices(df, devBuf, 48, &devCount) != SupabaseError::Ok) {
      if (out) {
        strncpy(out->error, Supabase.lastErrorCode(), sizeof(out->error) - 1);
      }
      SerialPrint("Supabase inventory devices query failed: " + String(Supabase.lastErrorCode()), true);
      return false;
    }
  }

  auto findDevMeta = [&](const char* macStr) -> const SupabaseDeviceDto* {
    for (uint16_t i = 0; i < devCount; i++) {
      if (strcasecmp(devBuf[i].deviceMac, macStr) == 0) return &devBuf[i];
    }
    return nullptr;
  };

  // Sensors with time_read in the last 24h for this site
  SupabaseSensorDto snsBuf[96];
  uint16_t snsCount = 0;
  {
    SupabaseQueryFilter sf;
    memset(&sf, 0, sizeof(sf));
    sf.table = "sensors";
    sf.site = site;
    sf.snsType = -1;
    sf.expired = 0;
    sf.timeStartUnix = nowUnix - SUPABASE_HUB_INVENTORY_LOOKBACK_SEC;
    sf.limit = 96;
    if (Supabase.querySensors(sf, snsBuf, 96, &snsCount) != SupabaseError::Ok) {
      if (out) {
        strncpy(out->error, Supabase.lastErrorCode(), sizeof(out->error) - 1);
      }
      SerialPrint("Supabase inventory sensors query failed: " + String(Supabase.lastErrorCode()), true);
      return false;
    }
  }

  uint16_t sensorsAdded = 0;
  uint16_t devicesAdded = 0;

  for (uint16_t i = 0; i < snsCount; i++) {
    const SupabaseSensorDto& s = snsBuf[i];
    uint64_t mac = SupabaseClient::macFromString(s.deviceMac);
    if (mac == 0 || mac == myMac) continue;

    if (Sensors.findSensor(mac, s.snsType, s.snsId) >= 0) continue; // known

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

  if (sensorsAdded > 0) {
#ifdef _USESDCARD
    storeDevicesSensorsSD();
#endif
  }

  SerialPrint("Supabase inventory site=" + String(site) +
                  " queried=" + String(snsCount) +
                  " addedSensors=" + String(sensorsAdded) +
                  " addedDevices=" + String(devicesAdded),
              true);

  if (out) {
    out->ok = true;
    out->sensorsQueried = snsCount;
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
  if (!Prefs.SUPABASE_CLAIMED || !Supabase.config().isReady()) return;
  if (!wifiReadyForNetwork()) return;

  static uint32_t lastPollMs = 0;
  const uint32_t nowMs = millis();
  if (lastPollMs != 0 && (nowMs - lastPollMs) < SUPABASE_HUB_INVENTORY_INTERVAL_MS) return;
  lastPollMs = nowMs;

  supabaseHubInventorySync(nullptr);
#endif
}

void supabaseHubPollExpiredAfterLan(ArborysDevType* device) {
#ifdef _USELOWPOWER
  (void)device;
  return;
#else
  if (!device || !device->IsSet || !device->expired) return;
  if (!Prefs.SUPABASE_CLAIMED) return;
  if (!Supabase.config().isReady()) {
    supabaseBeginFromPrefs();
    if (!Supabase.config().isReady()) return;
  }
  if (!wifiReadyForNetwork()) return;

  const uint32_t nowUnix = (I.currentTime > 1000000000UL) ? I.currentTime : 0;
  if (!nowUnix) return;

  const int16_t devIndex = Sensors.findDevice(device->MAC);
  if (devIndex < 0) return;

  // lastN[si] = last SendingInt multiple (N≥2) we already queried for sensor index si
  static uint16_t lastN[NUMSENSORS] = {0};

  // Which expired sensors on this device are due for a new N× poll?
  bool anyDue = false;
  uint16_t dueN[NUMSENSORS];
  memset(dueN, 0, sizeof(dueN));

  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s || !s->IsSet || s->deviceIndex != devIndex) continue;
    if (!s->expired) {
      lastN[si] = 0; // recovered → allow future cycles
      continue;
    }
    const uint32_t sendInt = s->SendingInt ? s->SendingInt : 300;
    const uint32_t freshness = s->timeLogged ? s->timeLogged : s->timeRead;
    if (!freshness || nowUnix <= freshness) continue;

    const uint32_t age = nowUnix - freshness;
    const uint32_t n = age / sendInt;
    if (n < 2) continue; // only at 2×, 3×, …
    if (n <= lastN[si]) continue;

    dueN[si] = (uint16_t)((n > 65535UL) ? 65535UL : n);
    anyDue = true;
  }

  if (!anyDue) return;

  char macStr[16];
  SupabaseClient::macToString(device->MAC, macStr);

  SupabaseSensorDto snsBuf[32];
  uint16_t snsCount = 0;
  SupabaseQueryFilter sf;
  memset(&sf, 0, sizeof(sf));
  sf.table = "sensors";
  sf.deviceMac = macStr;
  sf.snsType = -1;
  sf.expired = -1;
  sf.limit = 32;

  supabaseRefreshIdentity();
  if (Supabase.querySensors(sf, snsBuf, 32, &snsCount) != SupabaseError::Ok) {
    SerialPrint("Supabase expired poll failed for " + String(device->devName) + ": " +
                    String(Supabase.lastErrorCode()),
                true);
    return; // do not advance lastN — retry next LAN cycle
  }

  // Optional: refresh device IP from cloud for next LAN attempt
  {
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
      IPAddress ip;
      if (ip.fromString(drow.deviceIp) && ip != device->IP) {
        device->IP = ip;
      }
    }
  }

  uint16_t applied = 0;
  uint16_t marked = 0;
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    if (!dueN[si]) continue;
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
    if (!s || !s->IsSet) continue;

    const SupabaseSensorDto* cloud = nullptr;
    for (uint16_t i = 0; i < snsCount; i++) {
      if (snsBuf[i].snsType == s->snsType && snsBuf[i].snsId == s->snsID) {
        cloud = &snsBuf[i];
        break;
      }
    }

    // Advance multiple even if cloud has nothing newer — one attempt per N× window
    lastN[si] = dueN[si];
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

  SerialPrint("Supabase expired poll " + String(device->devName) +
                  " mac=" + String(macStr) +
                  " cloudRows=" + String(snsCount) +
                  " multiples=" + String(marked) +
                  " applied=" + String(applied),
              true);
#endif
}
#endif

#endif
