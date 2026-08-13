#ifdef _USESUPABASE

#include "SupabaseClient.hpp"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#if defined(_USE_CERT_BUNDLE)
extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_imported_bundle_bin_end[] asm("_binary_x509_crt_bundle_end");
#endif

SupabaseClient Supabase;

SupabaseClient::SupabaseClient() {
  cfg_.clear();
  accessToken_[0] = '\0';
  tokenExpiresAt_ = 0;
  lastError_ = SupabaseError::Ok;
  lastErrorMsg_[0] = '\0';
  lastErrorCode_[0] = '\0';
}

void SupabaseClient::begin(const SupabaseConfig& cfg) {
  cfg_ = cfg;
  // Strip trailing slash from URL
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

SupabaseError SupabaseClient::httpPostJson(const char* path, const char* bodyJson, bool withBearer,
                                           String& responseOut) {
  responseOut = "";
  if (!cfg_.isReady()) {
    setError(SupabaseError::NotConfigured, "not_configured", "SupabaseConfig incomplete");
    return lastError_;
  }

  char url[160];
  snprintf(url, sizeof(url), "%s%s", cfg_.projectUrl, path);

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
  http.addHeader("apikey", cfg_.anonKey);
  if (withBearer && accessToken_[0]) {
    char auth[960];
    snprintf(auth, sizeof(auth), "Bearer %s", accessToken_);
    http.addHeader("Authorization", auth);
  }

  const int code = http.POST(bodyJson ? bodyJson : "{}");
  responseOut = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    // Still return body for envelope parse (device-api uses JSON errors)
    if (code == 410) {
      setError(SupabaseError::UpgradeRequired, "upgrade_required", "API version too old");
    } else if (code == 401 || code == 403) {
      setError(SupabaseError::ApiRejected, "http_forbidden", "Auth/access rejected");
    } else {
      char msg[64];
      snprintf(msg, sizeof(msg), "HTTP %d", code);
      setError(SupabaseError::HttpFailed, "http_error", msg);
    }
    // Caller may still parse envelope from responseOut
    return lastError_;
  }

  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

static SupabaseError mapApiErrorCode(const char* code) {
  if (!code) return SupabaseError::ApiRejected;
  if (!strcmp(code, "upgrade_required")) return SupabaseError::UpgradeRequired;
  if (!strcmp(code, "subscription_inactive")) return SupabaseError::SubscriptionInactive;
  if (!strcmp(code, "device_not_registered") || !strcmp(code, "device_user_mismatch")) {
    return SupabaseError::NotRegistered;
  }
  if (!strcmp(code, "invalid_auth") || !strcmp(code, "missing_auth")) {
    return SupabaseError::AuthFailed;
  }
  return SupabaseError::ApiRejected;
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

static void parseSensorObj(JsonObjectConst o, SupabaseSensorDto& d) {
  memset(&d, 0, sizeof(d));
  d.limitHigh = NAN;
  d.limitLow = NAN;
  copyStr(d.deviceMac, sizeof(d.deviceMac), o["device_mac"] | "");
  d.snsType = (uint8_t)(o["sns_type"] | 0);
  d.snsId = (uint8_t)(o["sns_id"] | 0);
  copyStr(d.snsName, sizeof(d.snsName), o["sns_name"] | "");
  d.snsValue = o["sns_value"] | 0.0;
  d.timeRead = (uint32_t)(o["time_read"] | 0);
  d.timeLogged = (uint32_t)(o["time_logged"] | 0);
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
  d.devType = (uint8_t)(o["dev_type"] | 0);
  d.featureMask = (uint32_t)(o["feature_mask"] | 0);
  d.sendingInt = (uint32_t)(o["sending_int"] | 0);
  d.firmwareMajor = (uint8_t)(o["firmware_major"] | 0);
  d.firmwareMinor = (uint8_t)(o["firmware_minor"] | 0);
  d.firmwarePatch = (uint8_t)(o["firmware_patch"] | 0);
  d.expired = o["expired"] | false;
  d.flags = (uint8_t)(o["flags"] | 0);
  d.isActive = o["is_active"] | true;
  d.dataReceived = (uint32_t)(o["data_received"] | 0);
  d.dataSent = (uint32_t)(o["data_sent"] | 0);
  d.lastSeenAt = (uint32_t)(o["last_seen_at"] | 0);
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
  d.timeLogged = (uint32_t)(o["time_logged"] | 0);
  d.timeRead = (uint32_t)(o["time_read"] | 0);
  d.flagged = o["flagged"] | false;
  d.expired = o["expired"] | false;
  d.critical = o["critical"] | false;
  d.sendingInt = (uint32_t)(o["sending_int"] | 0);
  d.flags = (uint8_t)(o["flags"] | 0);
}

/** Returns Ok if envelope ok==true; otherwise maps error_code. Fills responseDoc. */
static SupabaseError parseEnvelope(const String& raw, JsonDocument& doc, SupabaseClient* self,
                                   char* errCode, size_t errCodeLen, char* errMsg, size_t errMsgLen) {
  (void)self;
  DeserializationError e = deserializeJson(doc, raw);
  if (e) {
    strncpy(errCode, "parse_failed", errCodeLen - 1);
    strncpy(errMsg, e.c_str(), errMsgLen - 1);
    return SupabaseError::ParseFailed;
  }
  const bool ok = doc["ok"] | false;
  const char* code = doc["error_code"] | "";
  if (!ok) {
    strncpy(errCode, code, errCodeLen - 1);
    errCode[errCodeLen - 1] = '\0';
    const char* msg = doc["data"]["message"] | code;
    strncpy(errMsg, msg, errMsgLen - 1);
    errMsg[errMsgLen - 1] = '\0';
    return mapApiErrorCode(code);
  }
  errCode[0] = '\0';
  errMsg[0] = '\0';
  return SupabaseError::Ok;
}

SupabaseError SupabaseClient::ensureAuth() {
  if (!cfg_.isReady()) {
    setError(SupabaseError::NotConfigured, "not_configured", "SupabaseConfig incomplete");
    return lastError_;
  }

  // Refresh 60s before expiry
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
  SupabaseError httpErr = httpPostJson("/functions/v1/mint-device-jwt", bodyStr.c_str(), false, resp);
  // mint returns 200 with token; on failure still try parse
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

SupabaseError SupabaseClient::listActiveSensors(SupabaseSensorDto* out, uint16_t maxOut,
                                                uint16_t* countOut, const char* scopeMac) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument params;
  if (scopeMac && scopeMac[0]) params["device_mac"] = scopeMac;
  params["limit"] = maxOut;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "list_active_sensors";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String bodyStr;
  serializeJson(req, bodyStr);

  String resp;
  httpPostJson("/functions/v1/device-api", bodyStr.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  if (env != SupabaseError::Ok) {
    lastError_ = env;
    return lastError_;
  }

  JsonArrayConst arr = doc["data"]["sensors"].as<JsonArrayConst>();
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

SupabaseError SupabaseClient::querySensors(const SupabaseQueryFilter& filter, SupabaseSensorDto* out,
                                           uint16_t maxOut, uint16_t* countOut) {
  if (countOut) *countOut = 0;
  if (!out || maxOut == 0) {
    setError(SupabaseError::InvalidArg, "invalid_arg", "output buffer required");
    return lastError_;
  }
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument params;
  params["table"] = "sensors";
  params["limit"] = filter.limit ? filter.limit : maxOut;
  if (filter.deviceMac && filter.deviceMac[0]) params["device_mac"] = filter.deviceMac;
  if (filter.snsType >= 0) params["sns_type"] = filter.snsType;
  if (filter.expired >= 0) params["expired"] = (bool)filter.expired;
  if (filter.timeStartUnix) params["time_start"] = filter.timeStartUnix;
  if (filter.timeEndUnix) params["time_end"] = filter.timeEndUnix;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "query";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  if (env != SupabaseError::Ok) {
    lastError_ = env;
    return lastError_;
  }

  JsonArrayConst arr = doc["data"]["sensors"].as<JsonArrayConst>();
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

  JsonDocument params;
  params["table"] = "devices";
  params["limit"] = filter.limit ? filter.limit : maxOut;
  if (filter.deviceMac && filter.deviceMac[0]) params["device_mac"] = filter.deviceMac;
  if (filter.deviceIp && filter.deviceIp[0]) params["device_ip"] = filter.deviceIp;
  if (filter.expired >= 0) params["expired"] = (bool)filter.expired;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "query";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  if (env != SupabaseError::Ok) {
    lastError_ = env;
    return lastError_;
  }

  JsonArrayConst arr = doc["data"]["devices"].as<JsonArrayConst>();
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

  JsonDocument params;
  params["table"] = "sensor_readings";
  params["limit"] = filter.limit ? filter.limit : maxOut;
  if (filter.deviceMac && filter.deviceMac[0]) params["device_mac"] = filter.deviceMac;
  if (filter.snsType >= 0) params["sns_type"] = filter.snsType;
  if (filter.expired >= 0) params["expired"] = (bool)filter.expired;
  if (filter.timeStartUnix) params["time_start"] = filter.timeStartUnix;
  if (filter.timeEndUnix) params["time_end"] = filter.timeEndUnix;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "query";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  if (env != SupabaseError::Ok) {
    lastError_ = env;
    return lastError_;
  }

  JsonArrayConst arr = doc["data"]["readings"].as<JsonArrayConst>();
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

  JsonDocument params;
  if (device.deviceIp[0]) params["device_ip"] = device.deviceIp;
  if (device.name[0]) params["dev_name"] = device.name;
  params["dev_type"] = device.devType;
  params["feature_mask"] = device.featureMask;
  params["sending_int"] = device.sendingInt;
  params["firmware_major"] = device.firmwareMajor;
  params["firmware_minor"] = device.firmwareMinor;
  params["firmware_patch"] = device.firmwarePatch;
  params["expired"] = device.expired;
  params["flags"] = device.flags;
  if (device.dataReceived) params["data_received"] = device.dataReceived;
  if (device.dataSent) params["data_sent"] = device.dataSent;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "upsert_device";
  req["device_mac"] = device.deviceMac[0] ? device.deviceMac : cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  lastError_ = env;
  return lastError_;
}

SupabaseError SupabaseClient::upsertSensor(const SupabaseSensorDto& sensor) {
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument params;
  params["device_mac"] = sensor.deviceMac[0] ? sensor.deviceMac : cfg_.deviceMac;
  params["sns_type"] = sensor.snsType;
  params["sns_id"] = sensor.snsId;
  params["sns_name"] = sensor.snsName;
  params["sns_value"] = sensor.snsValue;
  if (sensor.timeRead) params["time_read"] = sensor.timeRead;
  if (sensor.timeLogged) params["time_logged"] = sensor.timeLogged;
  params["sending_int"] = sensor.sendingInt;
  params["flags"] = sensor.flags;
  params["expired"] = sensor.expired;
  params["utc_offset"] = sensor.utcOffset ? sensor.utcOffset : cfg_.utcOffsetSec;
  if (!isnan(sensor.limitHigh)) params["limit_high"] = sensor.limitHigh;
  if (!isnan(sensor.limitLow)) params["limit_low"] = sensor.limitLow;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "upsert_sensor";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  lastError_ = env;
  return lastError_;
}

SupabaseError SupabaseClient::insertReading(const SupabaseReadingDto& reading, bool refreshSensor) {
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument params;
  params["device_mac"] = reading.deviceMac[0] ? reading.deviceMac : cfg_.deviceMac;
  if (reading.deviceIp[0]) params["device_ip"] = reading.deviceIp;
  params["sns_type"] = reading.snsType;
  params["sns_id"] = reading.snsId;
  params["sns_name"] = reading.snsName;
  params["sns_value"] = reading.snsValue;
  params["utc_offset"] = reading.utcOffset ? reading.utcOffset : cfg_.utcOffsetSec;
  if (reading.timeLogged) params["time_logged"] = reading.timeLogged;
  if (reading.timeRead) params["time_read"] = reading.timeRead;
  params["flagged"] = reading.flagged;
  params["expired"] = reading.expired;
  params["critical"] = reading.critical;
  if (reading.sendingInt) params["sending_int"] = reading.sendingInt;
  params["flags"] = reading.flags;
  params["refresh_sensor"] = refreshSensor;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "insert_reading";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  lastError_ = env;
  return lastError_;
}

SupabaseError SupabaseClient::checkFirmware(uint8_t devType, uint32_t featureMask,
                                            uint8_t maj, uint8_t min, uint8_t pat,
                                            SupabaseFirmwareOffer& offerOut) {
  memset(&offerOut, 0, sizeof(offerOut));
  if (ensureAuth() != SupabaseError::Ok) return lastError_;

  JsonDocument params;
  params["dev_type"] = devType;
  params["feature_mask"] = featureMask;
  params["firmware_major"] = maj;
  params["firmware_minor"] = min;
  params["firmware_patch"] = pat;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "firmware_check";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  if (env != SupabaseError::Ok) {
    lastError_ = env;
    return lastError_;
  }

  JsonObjectConst data = doc["data"].as<JsonObjectConst>();
  offerOut.available = data["available"] | false;
  offerOut.currentMajor = (uint8_t)(data["current"]["major"] | maj);
  offerOut.currentMinor = (uint8_t)(data["current"]["minor"] | min);
  offerOut.currentPatch = (uint8_t)(data["current"]["patch"] | pat);
  if (offerOut.available) {
    JsonObjectConst r = data["release"].as<JsonObjectConst>();
    copyStr(offerOut.releaseId, sizeof(offerOut.releaseId), r["id"] | "");
    offerOut.devType = (uint8_t)(r["dev_type"] | 0);
    offerOut.featureMask = (uint32_t)(r["feature_mask"] | 0);
    offerOut.versionMajor = (uint8_t)(r["version_major"] | 0);
    offerOut.versionMinor = (uint8_t)(r["version_minor"] | 0);
    offerOut.versionPatch = (uint8_t)(r["version_patch"] | 0);
    copyStr(offerOut.storagePath, sizeof(offerOut.storagePath), r["storage_path"] | "");
    copyStr(offerOut.sha256, sizeof(offerOut.sha256), r["sha256"] | "");
    offerOut.sizeBytes = (int32_t)(r["size_bytes"] | 0);
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

  JsonDocument params;
  params["release_id"] = releaseId;
  params["expires_in"] = expiresInSec;

  JsonDocument req;
  req["api_version"] = SUPABASE_API_VERSION;
  req["action"] = "firmware_url";
  req["device_mac"] = cfg_.deviceMac;
  req["params"] = params;
  String body;
  serializeJson(req, body);
  String resp;
  httpPostJson("/functions/v1/device-api", body.c_str(), true, resp);

  JsonDocument doc;
  SupabaseError env = parseEnvelope(resp, doc, this, lastErrorCode_, sizeof(lastErrorCode_),
                                    lastErrorMsg_, sizeof(lastErrorMsg_));
  if (env != SupabaseError::Ok) {
    lastError_ = env;
    return lastError_;
  }

  const char* url = doc["data"]["url"] | "";
  if (!url[0]) {
    setError(SupabaseError::ApiRejected, "no_url", "No signed URL returned");
    return lastError_;
  }
  if (strlen(url) >= urlOutLen) {
    setError(SupabaseError::BufferTooSmall, "buffer", "URL buffer too small");
    return lastError_;
  }
  copyStr(urlOut, urlOutLen, url);
  setError(SupabaseError::Ok, "", "");
  return SupabaseError::Ok;
}

#endif // _USESUPABASE
