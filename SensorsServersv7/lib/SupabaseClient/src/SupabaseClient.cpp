#ifdef _USESUPABASE

#include "SupabaseClient.hpp"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>

#if defined(_USE_CERT_BUNDLE)
extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_imported_bundle_bin_end[] asm("_binary_x509_crt_bundle_end");
#endif

SupabaseClient Supabase;

SupabaseClient::SupabaseClient() {
  cfg_.clear();
  accessToken_[0] = '\0';
  tokenExpiresAt_ = 0;
  lastSuccessUnix_ = 0;
  lastSuccessMs_ = 0;
  lastError_ = SupabaseError::Ok;
  lastErrorMsg_[0] = '\0';
  lastErrorCode_[0] = '\0';
}

void SupabaseClient::begin(const SupabaseConfig& cfg) {
  cfg_ = cfg;
  size_t n = strlen(cfg_.projectUrl);
  while (n > 0 && cfg_.projectUrl[n - 1] == '/') {
    cfg_.projectUrl[--n] = '\0';
  }
  normalizeMacInPlace(cfg_.deviceMac);
  accessToken_[0] = '\0';
  tokenExpiresAt_ = 0;
  setError(SupabaseError::Ok, "", "");
}

void SupabaseClient::setUtcOffset(int32_t offsetSec) {
  cfg_.utcOffsetSec = offsetSec;
}

void SupabaseClient::setDeviceIdentity(const char* deviceIp, uint8_t devType) {
  cfg_.devType = devType;
  if (deviceIp && deviceIp[0]) {
    strncpy(cfg_.deviceIp, deviceIp, sizeof(cfg_.deviceIp) - 1);
    cfg_.deviceIp[sizeof(cfg_.deviceIp) - 1] = '\0';
  }
}

void SupabaseClient::noteSuccess() {
  time_t now = time(nullptr);
  if (now > 1000000000L) lastSuccessUnix_ = (uint32_t)now;
  lastSuccessMs_ = millis();
}

void SupabaseClient::setError(SupabaseError err, const char* code, const char* msg) {
  lastError_ = err;
  strncpy(lastErrorCode_, code ? code : "", sizeof(lastErrorCode_) - 1);
  lastErrorCode_[sizeof(lastErrorCode_) - 1] = '\0';
  strncpy(lastErrorMsg_, msg ? msg : "", sizeof(lastErrorMsg_) - 1);
  lastErrorMsg_[sizeof(lastErrorMsg_) - 1] = '\0';
}

void SupabaseClient::macToString(uint64_t mac, char* out16) {
  if (!out16) return;
  snprintf(out16, 16, "%012llX", (unsigned long long)mac);
}

uint64_t SupabaseClient::macFromString(const char* macStr) {
  if (!macStr) return 0;
  char tmp[16];
  size_t j = 0;
  for (size_t i = 0; macStr[i] && j < 12; ++i) {
    if (isxdigit((unsigned char)macStr[i])) {
      tmp[j++] = (char)toupper((unsigned char)macStr[i]);
    }
  }
  tmp[j] = '\0';
  if (j != 12) return 0;
  return strtoull(tmp, nullptr, 16);
}

void SupabaseClient::normalizeMacInPlace(char* mac16) {
  if (!mac16) return;
  char tmp[16];
  size_t j = 0;
  for (size_t i = 0; mac16[i] && j < 12; ++i) {
    if (isxdigit((unsigned char)mac16[i])) {
      tmp[j++] = (char)toupper((unsigned char)mac16[i]);
    }
  }
  tmp[j] = '\0';
  strncpy(mac16, tmp, 15);
  mac16[15] = '\0';
}

static void copyStr(char* dest, size_t destLen, const char* src) {
  if (!dest || destLen == 0) return;
  if (!src) {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, src, destLen - 1);
  dest[destLen - 1] = '\0';
}

static String urlEncode(const char* s) {
  String out;
  if (!s) return out;
  for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
    unsigned char c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

static void unixToIsoZ(uint32_t unixSec, char* out, size_t outLen) {
  if (!out || outLen < 21) return;
  if (!unixSec) {
    out[0] = '\0';
    return;
  }
  time_t t = (time_t)unixSec;
  struct tm tm;
  gmtime_r(&t, &tm);
  snprintf(out, outLen, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/** UTC struct tm → unix seconds (ESP32 newlib lacks timegm). */
static time_t utcTmToUnix(struct tm* tm) {
  // days from civil calendar (Howard Hinnant) then add time-of-day
  int y = tm->tm_year + 1900;
  unsigned m = (unsigned)tm->tm_mon + 1;
  unsigned d = (unsigned)tm->tm_mday;
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
  return (time_t)(days * 86400LL + tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec);
}

static uint32_t isoOrUnixToUnix(JsonVariantConst v) {
  if (v.isNull()) return 0;
  if (v.is<long>() || v.is<unsigned long>() || v.is<int>() || v.is<long long>()) {
    long long n = v.as<long long>();
    return n > 0 ? (uint32_t)n : 0;
  }
  const char* iso = v.as<const char*>();
  if (!iso || !iso[0]) return 0;
  bool digits = true;
  for (const char* p = iso; *p; ++p) {
    if (!isdigit((unsigned char)*p)) {
      digits = false;
      break;
    }
  }
  if (digits) return (uint32_t)strtoul(iso, nullptr, 10);

  int Y = 0, M = 0, D = 0, h = 0, mi = 0, s = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &s) < 6) return 0;
  struct tm tm = {};
  tm.tm_year = Y - 1900;
  tm.tm_mon = M - 1;
  tm.tm_mday = D;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = s;
  time_t u = utcTmToUnix(&tm);
  return u > 0 ? (uint32_t)u : 0;
}

static const char* nestedSiteSlug(JsonObjectConst o) {
  JsonVariantConst sites = o["sites"];
  if (sites.isNull()) return "";
  if (sites.is<JsonArrayConst>()) {
    JsonArrayConst arr = sites.as<JsonArrayConst>();
    if (arr.size() > 0) return arr[0]["slug"] | "";
    return "";
  }
  return sites["slug"] | "";
}

static void parseSensorObj(JsonObjectConst o, SupabaseSensorDto& d) {
  memset(&d, 0, sizeof(d));
  d.limitHigh = NAN;
  d.limitLow = NAN;
  copyStr(d.deviceMac, sizeof(d.deviceMac), o["device_mac"] | "");
  d.snsType = (uint8_t)(o["sns_type"] | 0);
  d.snsId = (uint8_t)(o["sns_id"] | 0);
  copyStr(d.snsName, sizeof(d.snsName), o["sns_name"] | "");
  d.snsValue = o["sns_value"] | 0.0;
  d.timeRead = isoOrUnixToUnix(o["time_read"]);
  d.timeLogged = isoOrUnixToUnix(o["time_logged"]);
  d.sendingInt = (uint32_t)(o["sending_int"] | 0);
  d.flags = (uint8_t)(o["flags"] | 0);
  d.expired = o["expired"] | false;
  if (!o["limit_high"].isNull()) d.limitHigh = (float)(o["limit_high"] | 0.0);
  if (!o["limit_low"].isNull()) d.limitLow = (float)(o["limit_low"] | 0.0);
  d.utcOffset = (int32_t)(o["utc_offset"] | 0);
}

static void parseDeviceObj(JsonObjectConst o, SupabaseDeviceDto& d) {
  memset(&d, 0, sizeof(d));
  copyStr(d.deviceMac, sizeof(d.deviceMac), o["device_mac"] | "");
  copyStr(d.userId, sizeof(d.userId), o["user_id"] | "");
  copyStr(d.name, sizeof(d.name), o["dev_name"] | (o["name"] | ""));
  copyStr(d.deviceIp, sizeof(d.deviceIp), o["device_ip"] | "");
  const char* slug = o["site_slug"] | nestedSiteSlug(o);
  copyStr(d.siteSlug, sizeof(d.siteSlug), slug);
  copyStr(d.siteId, sizeof(d.siteId), o["site_id"] | "");
  d.devType = (uint8_t)(o["dev_type"] | 0);
  d.featureMask = (uint32_t)(o["feature_mask"] | 0);
  d.sendingInt = (uint32_t)(o["sending_int"] | 0);
  d.firmwareMajor = (uint8_t)(o["firmware_major"] | 0);
  d.firmwareMinor = (uint8_t)(o["firmware_minor"] | 0);
  d.firmwarePatch = (uint8_t)(o["firmware_patch"] | 0);
  d.expired = o["expired"] | false;
  d.flags = (uint8_t)(o["flags"] | 0);
  d.isActive = o["is_active"] | true;
  d.dataReceived = isoOrUnixToUnix(o["data_received"]);
  d.dataSent = isoOrUnixToUnix(o["data_sent"]);
  d.lastSeenAt = isoOrUnixToUnix(o["last_seen_at"]);
}

static void parseReadingObj(JsonObjectConst o, SupabaseReadingDto& d) {
  memset(&d, 0, sizeof(d));
  d.id = (int64_t)(o["id"] | 0);
  copyStr(d.deviceMac, sizeof(d.deviceMac), o["device_mac"] | "");
  copyStr(d.deviceIp, sizeof(d.deviceIp), o["device_ip"] | "");
  d.snsType = (uint8_t)(o["sns_type"] | 0);
  d.snsId = (uint8_t)(o["sns_id"] | 0);
  copyStr(d.snsName, sizeof(d.snsName), o["sns_name"] | "");
  d.snsValue = o["sns_value"] | 0.0;
  d.utcOffset = (int32_t)(o["utc_offset"] | 0);
  d.timeLogged = isoOrUnixToUnix(o["time_logged"]);
  d.timeRead = isoOrUnixToUnix(o["time_read"]);
  d.flagged = o["flagged"] | false;
  d.expired = o["expired"] | false;
  d.critical = o["critical"] | false;
  d.sendingInt = (uint32_t)(o["sending_int"] | 0);
  d.flags = (uint8_t)(o["flags"] | 0);
}

static int compareFw(uint8_t aMaj, uint8_t aMin, uint8_t aPat,
                     uint8_t bMaj, uint8_t bMin, uint8_t bPat) {
  if (aMaj != bMaj) return (int)aMaj - (int)bMaj;
  if (aMin != bMin) return (int)aMin - (int)bMin;
  return (int)aPat - (int)bPat;
}

static bool sensorFreshNow(const SupabaseSensorDto& s, uint32_t nowUnix) {
  if (s.expired || !s.timeRead || !s.sendingInt) return false;
  const uint32_t grace = s.sendingInt + (s.sendingInt / 4);
  return (s.timeRead + grace) > nowUnix;
}

SupabaseError SupabaseClient::httpJson(const char* method, const char* pathAndQuery,
                                       const char* bodyJson, bool withBearer,
                                       String& responseOut, const char* prefer) {
  responseOut = "";
  if (!cfg_.projectUrl[0] || !cfg_.anonKey[0]) {
    setError(SupabaseError::NotConfigured, "not_configured", "Supabase URL/anon incomplete");
    return lastError_;
  }
  if (withBearer && !cfg_.isReady()) {
    setError(SupabaseError::NotConfigured, "not_configured", "SupabaseConfig incomplete");
    return lastError_;
  }

  String url = String(cfg_.projectUrl) + pathAndQuery;

  WiFiClientSecure client;
#if defined(_USE_CERT_BUNDLE)
  client.setCACertBundle(x509_crt_imported_bundle_bin_start,
                         (size_t)(x509_crt_imported_bundle_bin_end - x509_crt_imported_bundle_bin_start));
#else
  client.setInsecure();
#endif

  HTTPClient http;
  http.setTimeout(cfg_.httpTimeoutMs ? cfg_.httpTimeoutMs : 20000);
  if (!http.begin(client, url)) {
    setError(SupabaseError::HttpFailed, "http_begin", "HTTP begin failed");
    return lastError_;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("apikey", cfg_.anonKey);
  if (prefer && prefer[0]) http.addHeader("Prefer", prefer);
  if (withBearer && accessToken_[0]) {
    char auth[960];
    snprintf(auth, sizeof(auth), "Bearer %s", accessToken_);
    http.addHeader("Authorization", auth);
  }

  int code = -1;
  if (!method || !strcmp(method, "POST")) {
    code = http.POST(bodyJson ? bodyJson : "{}");
  } else if (!strcmp(method, "GET")) {
    code = http.GET();
  } else if (!strcmp(method, "PATCH")) {
    code = http.sendRequest("PATCH", (uint8_t*)(bodyJson ? bodyJson : "{}"),
                            bodyJson ? strlen(bodyJson) : 2);
  } else if (!strcmp(method, "DELETE")) {
    code = http.sendRequest("DELETE");
  } else {
    http.end();
    setError(SupabaseError::InvalidArg, "bad_method", "Unsupported HTTP method");
    return lastError_;
  }

  responseOut = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    // Map common PostgREST / Auth failures
    JsonDocument errDoc;
    if (!deserializeJson(errDoc, responseOut)) {
      const char* msg = errDoc["message"] | (errDoc["error"] | "");
      const char* codeStr = errDoc["code"] | "";
      if (strstr(msg, "subscription_inactive") || strstr(codeStr, "42501")) {
        setError(SupabaseError::SubscriptionInactive, "subscription_inactive", msg);
        return lastError_;
      }
      if (code == 401 || code == 403) {
        setError(SupabaseError::ApiRejected, "http_forbidden", msg[0] ? msg : "Auth/access rejected");
        return lastError_;
      }
      if (msg[0]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HTTP %d", code);
        setError(SupabaseError::HttpFailed, "http_error", msg);
        (void)buf;
        return lastError_;
      }
    }
    if (code == 401 || code == 403) {
      setError(SupabaseError::ApiRejected, "http_forbidden", "Auth/access rejected");
    } else {
      char msg[64];
      snprintf(msg, sizeof(msg), "HTTP %d", code);
      setError(SupabaseError::HttpFailed, "http_error", msg);
    }
    return lastError_;
  }

  setError(SupabaseError::Ok, "", "");
  noteSuccess();
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::ensureAuth() {
  if (!cfg_.isReady()) {
    setError(SupabaseError::NotConfigured, "not_configured", "SupabaseConfig incomplete");
    return lastError_;
  }

  time_t now = time(nullptr);
  if (accessToken_[0] && tokenExpiresAt_ > 0 && (uint32_t)now + 60 < tokenExpiresAt_) {
    setError(SupabaseError::Ok, "", "");
    return SupabaseError::Ok;
  }

  JsonDocument body;
  body["device_mac"] = cfg_.deviceMac;
  body["api_key"] = cfg_.apiKey;
  String bodyStr;
  serializeJson(body, bodyStr);

  String resp;
  SupabaseError httpErr = httpJson("POST", "/functions/v1/mint-device-jwt", bodyStr.c_str(), false, resp);
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, resp);
  if (e) {
    setError(SupabaseError::AuthFailed, "auth_parse", e.c_str());
    return lastError_;
  }
  const char* token = doc["access_token"] | "";
  if (!token[0]) {
    const char* err = doc["error"] | "mint failed";
    setError(httpErr != SupabaseError::Ok ? httpErr : SupabaseError::AuthFailed, "auth_failed", err);
    return lastError_;
  }
  copyStr(accessToken_, sizeof(accessToken_), token);
  uint32_t expiresIn = (uint32_t)(doc["expires_in"] | 3600);
  uint32_t expiresAt = (uint32_t)(doc["expires_at"] | 0);
  tokenExpiresAt_ = expiresAt ? expiresAt : ((uint32_t)now + expiresIn);
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::claimDevice(const char* claimCode) {
  if (!claimCode || !claimCode[0]) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "claim_code required");
    return lastError_;
  }

  cfg_.applyDefaults();
  if (!cfg_.deviceMac[0]) {
    macToString(ESP.getEfuseMac(), cfg_.deviceMac);
  }

  char code[8];
  size_t j = 0;
  for (size_t i = 0; claimCode[i] && j < 4; ++i) {
    char c = claimCode[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      code[j++] = c;
    }
  }
  code[j] = '\0';
  if (j != 4) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "claim_code must be 4 alphanumeric");
    return lastError_;
  }

  JsonDocument body;
  body["device_mac"] = cfg_.deviceMac;
  body["claim_code"] = code;
  String bodyStr;
  serializeJson(body, bodyStr);

  String resp;
  SupabaseError httpErr = httpJson("POST", "/functions/v1/claim-device", bodyStr.c_str(), false, resp);

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, resp);
  if (e) {
    setError(SupabaseError::ParseFailed, "claim_parse", e.c_str());
    return lastError_;
  }
  if (!(doc["ok"] | false)) {
    const char* err = doc["error"] | "claim failed";
    setError(httpErr != SupabaseError::Ok ? httpErr : SupabaseError::ApiRejected, "claim_failed", err);
    return lastError_;
  }

  const char* projectUrl = doc["project_url"] | "";
  const char* anonKey = doc["anon_key"] | "";
  const char* apiKey = doc["api_key"] | "";
  const char* userId = doc["user_id"] | "";
  const char* siteSlug = doc["site_slug"] | "home";
  const char* deviceMac = doc["device_mac"] | cfg_.deviceMac;

  if (!projectUrl[0] || !anonKey[0] || !apiKey[0]) {
    setError(SupabaseError::ApiRejected, "claim_incomplete", "claim response missing credentials");
    return lastError_;
  }

  strncpy(cfg_.projectUrl, projectUrl, sizeof(cfg_.projectUrl) - 1);
  cfg_.projectUrl[sizeof(cfg_.projectUrl) - 1] = '\0';
  strncpy(cfg_.anonKey, anonKey, sizeof(cfg_.anonKey) - 1);
  cfg_.anonKey[sizeof(cfg_.anonKey) - 1] = '\0';
  strncpy(cfg_.apiKey, apiKey, sizeof(cfg_.apiKey) - 1);
  cfg_.apiKey[sizeof(cfg_.apiKey) - 1] = '\0';
  strncpy(cfg_.userId, userId, sizeof(cfg_.userId) - 1);
  cfg_.userId[sizeof(cfg_.userId) - 1] = '\0';
  strncpy(cfg_.siteSlug, siteSlug[0] ? siteSlug : "home", sizeof(cfg_.siteSlug) - 1);
  cfg_.siteSlug[sizeof(cfg_.siteSlug) - 1] = '\0';
  normalizeMacInPlace(cfg_.deviceMac);
  if (deviceMac[0]) {
    strncpy(cfg_.deviceMac, deviceMac, sizeof(cfg_.deviceMac) - 1);
    cfg_.deviceMac[sizeof(cfg_.deviceMac) - 1] = '\0';
    normalizeMacInPlace(cfg_.deviceMac);
  }
  size_t n = strlen(cfg_.projectUrl);
  while (n > 0 && cfg_.projectUrl[n - 1] == '/') {
    cfg_.projectUrl[--n] = '\0';
  }
  accessToken_[0] = '\0';
  tokenExpiresAt_ = 0;
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::resolveSiteId(const char* siteSlug, char* siteIdOut, size_t siteIdLen) {
  if (!siteIdOut || siteIdLen < 2) return SupabaseError::InvalidArg;
  siteIdOut[0] = '\0';
  if (!siteSlug || !siteSlug[0]) return SupabaseError::InvalidArg;

  String path = "/rest/v1/sites?slug=eq.";
  path += urlEncode(siteSlug);
  path += "&select=id&limit=1";

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "sites parse failed");
    return lastError_;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull() || arr.size() == 0) {
    setError(SupabaseError::ApiRejected, "site_not_found", "site not found");
    return lastError_;
  }
  copyStr(siteIdOut, siteIdLen, arr[0]["id"] | "");
  if (!siteIdOut[0]) {
    setError(SupabaseError::ApiRejected, "site_not_found", "site not found");
    return lastError_;
  }
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::macsForSite(const char* siteSlug, String& macCsvOut) {
  macCsvOut = "";
  char siteId[40];
  if (resolveSiteId(siteSlug, siteId, sizeof(siteId)) != SupabaseError::Ok) return lastError_;

  String path = "/rest/v1/devices?site_id=eq.";
  path += urlEncode(siteId);
  path += "&select=device_mac&limit=200";

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "devices parse failed");
    return lastError_;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  bool first = true;
  for (JsonObjectConst o : arr) {
    const char* mac = o["device_mac"] | "";
    if (!mac[0]) continue;
    if (!first) macCsvOut += ",";
    macCsvOut += mac;
    first = false;
  }
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::listSites(SupabaseSiteDto* out, uint16_t maxOut, uint16_t* countOut) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  String resp;
  if (httpJson("GET", "/rest/v1/sites?select=id,slug,name,devices(count)&order=slug",
               nullptr, true, resp) != SupabaseError::Ok) {
    return lastError_;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "sites parse failed");
    return lastError_;
  }

  JsonArrayConst arr = doc.as<JsonArrayConst>();
  uint16_t n = 0;
  for (JsonObjectConst o : arr) {
    if (n >= maxOut) break;
    memset(&out[n], 0, sizeof(out[n]));
    copyStr(out[n].id, sizeof(out[n].id), o["id"] | "");
    copyStr(out[n].slug, sizeof(out[n].slug), o["slug"] | "");
    copyStr(out[n].name, sizeof(out[n].name), o["name"] | "");
    uint16_t cnt = 0;
    JsonVariantConst dc = o["devices"];
    if (dc.is<JsonArrayConst>() && dc.as<JsonArrayConst>().size() > 0) {
      cnt = (uint16_t)(dc.as<JsonArrayConst>()[0]["count"] | 0);
    }
    out[n].deviceCount = cnt;
    ++n;
  }
  if (countOut) *countOut = n;
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::deleteSite(const char* siteSlug) {
  if (!siteSlug || !siteSlug[0]) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "site required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument body;
  body["p_slug"] = siteSlug;
  String bodyStr;
  serializeJson(body, bodyStr);

  String resp;
  return httpJson("POST", "/rest/v1/rpc/delete_my_site", bodyStr.c_str(), true, resp);
}

SupabaseError SupabaseClient::setDeviceSite(const char* siteSlug, const char* siteName) {
  if (!siteSlug || !siteSlug[0]) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "site required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument body;
  body["p_slug"] = siteSlug;
  if (siteName && siteName[0]) body["p_name"] = siteName;
  String bodyStr;
  serializeJson(body, bodyStr);

  String resp;
  if (httpJson("POST", "/rest/v1/rpc/ensure_my_site", bodyStr.c_str(), true, resp) !=
      SupabaseError::Ok) {
    return lastError_;
  }

  // RPC returns a UUID string (possibly quoted JSON string)
  char siteId[40];
  siteId[0] = '\0';
  {
    JsonDocument doc;
    if (!deserializeJson(doc, resp)) {
      if (doc.is<const char*>()) {
        copyStr(siteId, sizeof(siteId), doc.as<const char*>());
      } else {
        // raw "uuid" without object
        String trimmed = resp;
        trimmed.trim();
        if (trimmed.startsWith("\"") && trimmed.endsWith("\"") && trimmed.length() > 2) {
          trimmed = trimmed.substring(1, trimmed.length() - 1);
        }
        copyStr(siteId, sizeof(siteId), trimmed.c_str());
      }
    } else {
      String trimmed = resp;
      trimmed.trim();
      if (trimmed.startsWith("\"") && trimmed.endsWith("\"") && trimmed.length() > 2) {
        trimmed = trimmed.substring(1, trimmed.length() - 1);
      }
      copyStr(siteId, sizeof(siteId), trimmed.c_str());
    }
  }
  if (!siteId[0]) {
    setError(SupabaseError::ParseFailed, "parse_failed", "ensure_my_site returned no id");
    return lastError_;
  }

  JsonDocument patch;
  patch["site_id"] = siteId;
  if (cfg_.deviceIp[0]) patch["device_ip"] = cfg_.deviceIp;
  patch["dev_type"] = cfg_.devType;
  char nowIso[24];
  time_t now = time(nullptr);
  if (now > 1000000000L) unixToIsoZ((uint32_t)now, nowIso, sizeof(nowIso));
  else nowIso[0] = '\0';
  if (nowIso[0]) patch["last_seen_at"] = nowIso;

  String patchStr;
  serializeJson(patch, patchStr);
  String path = "/rest/v1/devices?device_mac=eq.";
  path += urlEncode(cfg_.deviceMac);

  String patchResp;
  if (httpJson("PATCH", path.c_str(), patchStr.c_str(), true, patchResp, "return=minimal") !=
      SupabaseError::Ok) {
    return lastError_;
  }

  strncpy(cfg_.siteSlug, siteSlug, sizeof(cfg_.siteSlug) - 1);
  cfg_.siteSlug[sizeof(cfg_.siteSlug) - 1] = '\0';
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::fetchOwnSite(char* siteSlugOut, size_t siteSlugOutLen) {
  if (!siteSlugOut || siteSlugOutLen < 2) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "site buffer required");
    return lastError_;
  }
  siteSlugOut[0] = '\0';

  SupabaseQueryFilter filter;
  memset(&filter, 0, sizeof(filter));
  filter.table = "devices";
  filter.deviceMac = cfg_.deviceMac;
  filter.snsType = -1;
  filter.expired = -1;
  filter.limit = 1;

  SupabaseDeviceDto row;
  uint16_t count = 0;
  SupabaseError err = queryDevices(filter, &row, 1, &count);
  if (err != SupabaseError::Ok) return lastError_;
  if (count == 0 || !row.siteSlug[0]) {
    setError(SupabaseError::NotRegistered, "no_device_site", "device site not found");
    return lastError_;
  }

  strncpy(siteSlugOut, row.siteSlug, siteSlugOutLen - 1);
  siteSlugOut[siteSlugOutLen - 1] = '\0';
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

static void appendSensorFilters(String& path, const SupabaseQueryFilter& filter,
                                const String* macInCsv) {
  path += "select=*&order=time_read.desc";
  if (filter.deviceMac && filter.deviceMac[0]) {
    path += "&device_mac=eq.";
    path += urlEncode(filter.deviceMac);
  } else if (macInCsv && macInCsv->length()) {
    path += "&device_mac=in.(";
    path += *macInCsv;
    path += ")";
  }
  if (filter.snsType >= 0) {
    path += "&sns_type=eq.";
    path += String(filter.snsType);
  }
  if (filter.expired >= 0) {
    path += "&expired=eq.";
    path += filter.expired ? "true" : "false";
  }
  if (filter.timeStartUnix) {
    char iso[24];
    unixToIsoZ(filter.timeStartUnix, iso, sizeof(iso));
    path += "&time_read=gte.";
    path += urlEncode(iso);
  }
  if (filter.timeEndUnix) {
    char iso[24];
    unixToIsoZ(filter.timeEndUnix, iso, sizeof(iso));
    path += "&time_read=lte.";
    path += urlEncode(iso);
  }
  uint16_t lim = filter.limit ? filter.limit : SUPABASE_MAX_QUERY_ROWS;
  path += "&limit=";
  path += String(lim);
}

SupabaseError SupabaseClient::listActiveSensors(SupabaseSensorDto* out, uint16_t maxOut,
                                                uint16_t* countOut, const char* scopeMac,
                                                const char* site) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }

  SupabaseQueryFilter filter;
  memset(&filter, 0, sizeof(filter));
  filter.table = "sensors";
  filter.deviceMac = (scopeMac && scopeMac[0]) ? scopeMac : nullptr;
  filter.site = (site && site[0]) ? site : nullptr;
  filter.snsType = -1;
  filter.expired = 0;
  filter.limit = maxOut;

  uint16_t n = 0;
  if (querySensors(filter, out, maxOut, &n) != SupabaseError::Ok) return lastError_;

  time_t now = time(nullptr);
  uint32_t nowUnix = (now > 1000000000L) ? (uint32_t)now : 0;
  uint16_t w = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (!nowUnix || sensorFreshNow(out[i], nowUnix)) {
      if (w != i) out[w] = out[i];
      w++;
    }
  }
  if (countOut) *countOut = w;
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::querySensors(const SupabaseQueryFilter& filter, SupabaseSensorDto* out,
                                           uint16_t maxOut, uint16_t* countOut) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  String macCsv;
  if (filter.site && filter.site[0] && !(filter.deviceMac && filter.deviceMac[0])) {
    if (macsForSite(filter.site, macCsv) != SupabaseError::Ok) return lastError_;
    if (!macCsv.length()) {
      setError(SupabaseError::Ok, "", "");
      return SupabaseError::Ok;
    }
  }

  SupabaseQueryFilter f = filter;
  if (!f.limit || f.limit > maxOut) f.limit = maxOut;

  String path = "/rest/v1/sensors?";
  appendSensorFilters(path, f, macCsv.length() ? &macCsv : nullptr);

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "sensors parse failed");
    return lastError_;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  uint16_t n = 0;
  for (JsonObjectConst o : arr) {
    if (n >= maxOut) break;
    parseSensorObj(o, out[n]);
    ++n;
  }
  if (countOut) *countOut = n;
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::queryDevices(const SupabaseQueryFilter& filter, SupabaseDeviceDto* out,
                                           uint16_t maxOut, uint16_t* countOut) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  String path =
      "/rest/v1/devices?select=id,user_id,device_mac,name,dev_name,device_ip,dev_type,"
      "feature_mask,sending_int,firmware_major,firmware_minor,firmware_patch,expired,flags,"
      "is_active,data_received,data_sent,last_seen_at,site_id,sites(slug,name)";

  if (filter.deviceMac && filter.deviceMac[0]) {
    path += "&device_mac=eq.";
    path += urlEncode(filter.deviceMac);
  }
  if (filter.deviceIp && filter.deviceIp[0]) {
    path += "&device_ip=eq.";
    path += urlEncode(filter.deviceIp);
  }
  if (filter.site && filter.site[0]) {
    char siteId[40];
    if (resolveSiteId(filter.site, siteId, sizeof(siteId)) != SupabaseError::Ok) return lastError_;
    path += "&site_id=eq.";
    path += urlEncode(siteId);
  }
  if (filter.expired >= 0) {
    path += "&expired=eq.";
    path += filter.expired ? "true" : "false";
  }
  uint16_t lim = filter.limit ? filter.limit : maxOut;
  if (lim > maxOut) lim = maxOut;
  path += "&limit=";
  path += String(lim);

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "devices parse failed");
    return lastError_;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  uint16_t n = 0;
  for (JsonObjectConst o : arr) {
    if (n >= maxOut) break;
    parseDeviceObj(o, out[n]);
    ++n;
  }
  if (countOut) *countOut = n;
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::queryReadings(const SupabaseQueryFilter& filter, SupabaseReadingDto* out,
                                            uint16_t maxOut, uint16_t* countOut) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  String macCsv;
  if (filter.site && filter.site[0] && !(filter.deviceMac && filter.deviceMac[0])) {
    if (macsForSite(filter.site, macCsv) != SupabaseError::Ok) return lastError_;
    if (!macCsv.length()) {
      setError(SupabaseError::Ok, "", "");
      return SupabaseError::Ok;
    }
  }

  String path = "/rest/v1/sensor_readings?select=*&order=time_logged.desc";
  if (filter.deviceMac && filter.deviceMac[0]) {
    path += "&device_mac=eq.";
    path += urlEncode(filter.deviceMac);
  } else if (macCsv.length()) {
    path += "&device_mac=in.(";
    path += macCsv;
    path += ")";
  }
  if (filter.snsType >= 0) {
    path += "&sns_type=eq.";
    path += String(filter.snsType);
  }
  if (filter.expired >= 0) {
    path += "&expired=eq.";
    path += filter.expired ? "true" : "false";
  }
  if (filter.timeStartUnix) {
    char iso[24];
    unixToIsoZ(filter.timeStartUnix, iso, sizeof(iso));
    path += "&time_logged=gte.";
    path += urlEncode(iso);
  }
  if (filter.timeEndUnix) {
    char iso[24];
    unixToIsoZ(filter.timeEndUnix, iso, sizeof(iso));
    path += "&time_logged=lte.";
    path += urlEncode(iso);
  }
  uint16_t lim = filter.limit ? filter.limit : maxOut;
  if (lim > maxOut) lim = maxOut;
  path += "&limit=";
  path += String(lim);

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "readings parse failed");
    return lastError_;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  uint16_t n = 0;
  for (JsonObjectConst o : arr) {
    if (n >= maxOut) break;
    parseReadingObj(o, out[n]);
    ++n;
  }
  if (countOut) *countOut = n;
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::upsertDevice(const SupabaseDeviceDto& device) {
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  const char* mac = device.deviceMac[0] ? device.deviceMac : cfg_.deviceMac;
  JsonDocument patch;

  const char* ip = device.deviceIp[0] ? device.deviceIp : cfg_.deviceIp;
  if (ip[0]) patch["device_ip"] = ip;
  if (device.name[0]) {
    patch["dev_name"] = device.name;
    patch["name"] = device.name;
  }
  patch["dev_type"] = device.devType ? device.devType : cfg_.devType;
  patch["feature_mask"] = device.featureMask;
  patch["sending_int"] = device.sendingInt;
  patch["firmware_major"] = device.firmwareMajor;
  patch["firmware_minor"] = device.firmwareMinor;
  patch["firmware_patch"] = device.firmwarePatch;
  patch["expired"] = device.expired;
  patch["flags"] = device.flags;

  char iso[24];
  if (device.dataReceived) {
    unixToIsoZ(device.dataReceived, iso, sizeof(iso));
    if (iso[0]) patch["data_received"] = iso;
  }
  if (device.dataSent) {
    unixToIsoZ(device.dataSent, iso, sizeof(iso));
    if (iso[0]) patch["data_sent"] = iso;
  }

  time_t now = time(nullptr);
  if (now > 1000000000L) {
    unixToIsoZ((uint32_t)now, iso, sizeof(iso));
    if (iso[0]) patch["last_seen_at"] = iso;
  }

  if (device.siteSlug[0]) {
    JsonDocument siteBody;
    siteBody["p_slug"] = device.siteSlug;
    String siteStr;
    serializeJson(siteBody, siteStr);
    String siteResp;
    if (httpJson("POST", "/rest/v1/rpc/ensure_my_site", siteStr.c_str(), true, siteResp) !=
        SupabaseError::Ok) {
      return lastError_;
    }
    String trimmed = siteResp;
    trimmed.trim();
    if (trimmed.startsWith("\"") && trimmed.endsWith("\"") && trimmed.length() > 2) {
      trimmed = trimmed.substring(1, trimmed.length() - 1);
    }
    if (trimmed.length()) patch["site_id"] = trimmed;
  }

  String patchStr;
  serializeJson(patch, patchStr);
  if (patchStr == "{}" || patchStr == "null") {
    setError(SupabaseError::InvalidArg, "no_fields", "no device fields to update");
    return lastError_;
  }

  String path = "/rest/v1/devices?device_mac=eq.";
  path += urlEncode(mac);
  String resp;
  return httpJson("PATCH", path.c_str(), patchStr.c_str(), true, resp, "return=minimal");
}

SupabaseError SupabaseClient::upsertSensor(const SupabaseSensorDto& sensor) {
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument row;
  row["device_mac"] = sensor.deviceMac[0] ? sensor.deviceMac : cfg_.deviceMac;
  row["sns_type"] = sensor.snsType;
  row["sns_id"] = sensor.snsId;
  row["user_id"] = cfg_.userId;
  row["sns_name"] = sensor.snsName;
  row["sns_value"] = sensor.snsValue;
  char iso[24];
  if (sensor.timeRead) {
    unixToIsoZ(sensor.timeRead, iso, sizeof(iso));
    if (iso[0]) row["time_read"] = iso;
  }
  if (sensor.timeLogged) {
    unixToIsoZ(sensor.timeLogged, iso, sizeof(iso));
    if (iso[0]) row["time_logged"] = iso;
  }
  row["sending_int"] = sensor.sendingInt ? sensor.sendingInt : 300;
  row["flags"] = sensor.flags;
  row["expired"] = sensor.expired;
  row["utc_offset"] = sensor.utcOffset ? sensor.utcOffset : cfg_.utcOffsetSec;
  if (!isnan(sensor.limitHigh)) row["limit_high"] = sensor.limitHigh;
  if (!isnan(sensor.limitLow)) row["limit_low"] = sensor.limitLow;

  time_t now = time(nullptr);
  if (now > 1000000000L) {
    unixToIsoZ((uint32_t)now, iso, sizeof(iso));
    if (iso[0]) row["updated_at"] = iso;
  }

  String body;
  serializeJson(row, body);
  String resp;
  return httpJson("POST", "/rest/v1/sensors", body.c_str(), true, resp,
                  "resolution=merge-duplicates,return=minimal");
}

SupabaseError SupabaseClient::insertReading(const SupabaseReadingDto& reading, bool refreshSensor) {
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument body;
  body["p_device_mac"] = reading.deviceMac[0] ? reading.deviceMac : cfg_.deviceMac;
  body["p_sns_type"] = reading.snsType;
  body["p_sns_id"] = reading.snsId;
  body["p_sns_value"] = reading.snsValue;
  body["p_sns_name"] = reading.snsName;
  if (reading.deviceIp[0]) body["p_device_ip"] = reading.deviceIp;
  else if (cfg_.deviceIp[0]) body["p_device_ip"] = cfg_.deviceIp;
  body["p_utc_offset"] = reading.utcOffset ? reading.utcOffset : cfg_.utcOffsetSec;
  char iso[24];
  if (reading.timeLogged) {
    unixToIsoZ(reading.timeLogged, iso, sizeof(iso));
    if (iso[0]) body["p_time_logged"] = iso;
  }
  if (reading.timeRead) {
    unixToIsoZ(reading.timeRead, iso, sizeof(iso));
    if (iso[0]) body["p_time_read"] = iso;
  }
  body["p_flagged"] = reading.flagged;
  body["p_expired"] = reading.expired;
  body["p_critical"] = reading.critical;
  if (reading.sendingInt) body["p_sending_int"] = reading.sendingInt;
  body["p_flags"] = reading.flags;
  body["p_refresh_sensor"] = refreshSensor;

  String bodyStr;
  serializeJson(body, bodyStr);
  String resp;
  return httpJson("POST", "/rest/v1/rpc/insert_my_reading", bodyStr.c_str(), true, resp);
}

SupabaseError SupabaseClient::checkFirmware(uint8_t devType, uint32_t featureMask,
                                            uint8_t maj, uint8_t min, uint8_t pat,
                                            SupabaseFirmwareOffer& offerOut) {
  memset(&offerOut, 0, sizeof(offerOut));
  offerOut.currentMajor = maj;
  offerOut.currentMinor = min;
  offerOut.currentPatch = pat;
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  String path = "/rest/v1/firmware_releases?dev_type=eq.";
  path += String(devType);
  path += "&is_active=eq.true&select=*";

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "firmware parse failed");
    return lastError_;
  }

  JsonArrayConst arr = doc.as<JsonArrayConst>();
  bool found = false;
  uint8_t bestMaj = maj, bestMin = min, bestPat = pat;
  JsonObjectConst best;

  for (JsonObjectConst r : arr) {
    uint32_t reqMask = (uint32_t)(r["feature_mask"] | 0);
    if ((featureMask & reqMask) != reqMask) continue;
    uint8_t vMaj = (uint8_t)(r["version_major"] | 0);
    uint8_t vMin = (uint8_t)(r["version_minor"] | 0);
    uint8_t vPat = (uint8_t)(r["version_patch"] | 0);
    if (compareFw(vMaj, vMin, vPat, bestMaj, bestMin, bestPat) > 0) {
      best = r;
      bestMaj = vMaj;
      bestMin = vMin;
      bestPat = vPat;
      found = true;
    }
  }

  offerOut.available = found;
  if (found) {
    copyStr(offerOut.releaseId, sizeof(offerOut.releaseId), best["id"] | "");
    offerOut.devType = (uint8_t)(best["dev_type"] | 0);
    offerOut.featureMask = (uint32_t)(best["feature_mask"] | 0);
    offerOut.versionMajor = bestMaj;
    offerOut.versionMinor = bestMin;
    offerOut.versionPatch = bestPat;
    copyStr(offerOut.storagePath, sizeof(offerOut.storagePath), best["storage_path"] | "");
    copyStr(offerOut.sha256, sizeof(offerOut.sha256), best["sha256"] | "");
    offerOut.sizeBytes = (int32_t)(best["size_bytes"] | 0);
  }
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::fetchFirmwareUrl(const char* releaseId, char* urlOut, size_t urlOutLen,
                                               uint16_t expiresInSec) {
  if (!releaseId || !urlOut || urlOutLen < 16) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "releaseId/url buffer required");
    return lastError_;
  }
  urlOut[0] = '\0';
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  String path = "/rest/v1/firmware_releases?id=eq.";
  path += urlEncode(releaseId);
  path += "&is_active=eq.true&select=storage_path&limit=1";

  String resp;
  if (httpJson("GET", path.c_str(), nullptr, true, resp) != SupabaseError::Ok) return lastError_;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "release parse failed");
    return lastError_;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull() || arr.size() == 0) {
    setError(SupabaseError::ApiRejected, "release_not_found", "release not found");
    return lastError_;
  }
  const char* storagePath = arr[0]["storage_path"] | "";
  if (!storagePath[0]) {
    setError(SupabaseError::ApiRejected, "release_not_found", "storage_path missing");
    return lastError_;
  }

  // Sign via Storage API (requires storage.objects SELECT policy for authenticated)
  String signPath = "/storage/v1/object/sign/firmware/";
  signPath += storagePath;
  JsonDocument signBody;
  signBody["expiresIn"] = expiresInSec ? expiresInSec : 300;
  String signStr;
  serializeJson(signBody, signStr);

  String signResp;
  if (httpJson("POST", signPath.c_str(), signStr.c_str(), true, signResp) != SupabaseError::Ok) {
    return lastError_;
  }

  JsonDocument signedDoc;
  if (deserializeJson(signedDoc, signResp)) {
    setError(SupabaseError::ParseFailed, "parse_failed", "signed url parse failed");
    return lastError_;
  }
  const char* signedUrl = signedDoc["signedURL"] | (signedDoc["signedUrl"] | "");
  if (!signedUrl[0]) {
    setError(SupabaseError::ApiRejected, "no_url", "No signed URL returned");
    return lastError_;
  }

  // Storage may return a relative path
  String full;
  if (signedUrl[0] == '/') {
    full = String(cfg_.projectUrl) + signedUrl;
  } else if (strncmp(signedUrl, "http", 4) == 0) {
    full = signedUrl;
  } else {
    full = String(cfg_.projectUrl) + "/storage/v1" + (signedUrl[0] == '/' ? "" : "/") + signedUrl;
  }

  if ((size_t)full.length() >= urlOutLen) {
    setError(SupabaseError::BufferTooSmall, "buffer", "URL buffer too small");
    return lastError_;
  }
  copyStr(urlOut, urlOutLen, full.c_str());
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

#endif // _USESUPABASE
