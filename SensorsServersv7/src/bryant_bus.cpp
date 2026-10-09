#include "bryant_bus.hpp"

#if defined(_USEBRYANT)

#include "globals.hpp"
#include "agg_links.hpp"
#include "actuators.hpp"
#include "utility.hpp"
#include "ble_provision.hpp"
#include <HardwareSerial.h>
#include <string.h>
#if defined(_USESDCARD)
#include "SDCard.hpp"
#endif
#if defined(_USESSD1306)
#include "SSD1306_graphics.hpp"
#endif

extern STRUCT_SNSHISTORY SensorHistory;

// Destination then source, big-endian. Length is the count of bytes after the
// length byte, including the CRC. Function is byte 7. Register is bytes 8-10
// (row, then table id little-endian: 01 3B 02 is row 1, table 0x023B).
// Example length constants in notes disagree with their own offsets, so the
// length byte on the wire decides the frame size and these offsets decide fields.

#ifndef BRYANT_UART_RX
#define BRYANT_UART_RX 16
#endif
#ifndef BRYANT_BAUD
#define BRYANT_BAUD 38400
#endif
#ifndef BRYANT_FRESH_SEC
#define BRYANT_FRESH_SEC 180
#endif

static const uint32_t kFreshMs = (uint32_t)BRYANT_FRESH_SEC * 1000UL;
static const uint32_t kMinOnMs = 30UL * 60UL * 1000UL;

static const uint16_t kAddrConnex = 0x2001;
static const uint16_t kAddrOutdoorA = 0x5001;
static const uint16_t kAddrOutdoorB = 0x5101;
static const uint16_t kTableMaster = 0x023B;  // 01 3B 02
static const uint16_t kTableOutdoor = 0x033B; // 01 3B 03

static uint8_t s_buf[128];
static uint8_t s_len = 0;

static uint32_t s_masterMs = 0;
static uint32_t s_outdoorMs = 0;
static bool s_haveTemp = false;
static bool s_haveHeatSp = false;
static bool s_haveRh = false;
static int16_t s_tempF = 0;
static int16_t s_heatSpF = 0;
static uint8_t s_rh = 0;
static uint8_t s_connexMode = 0;
static bool s_haveOutdoor = false;
static int16_t s_oatF = 0;
static uint8_t s_outdoorMode = 0;
static bool s_defrost = false;
static bool s_loggedMaster = false;
static bool s_loggedOutdoor = false;

struct ZoneLatch {
  bool on;
  uint32_t since;
};
static ZoneLatch s_zone2 = {false, 0};
static ZoneLatch s_zone4 = {false, 0};

static bool knownAddr(uint16_t a) {
  return a == 0x2000 || a == 0x2001 || a == 0x4000 || a == 0x4001
      || a == 0x5001 || a == 0x5101 || a == 0x6001;
}

static uint16_t crc16Modbus(const uint8_t* data, int n) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < n; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0xA001);
      else crc >>= 1;
    }
  }
  return crc;
}

static uint16_t crc16Ccitt(const uint8_t* data, int n, uint16_t init) {
  uint16_t crc = init;
  for (int i = 0; i < n; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
      else crc = (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// Accept Modbus (0x8005 reflected), CCITT-FALSE, or XMODEM, either byte order.
// A frame that matches none of those is dropped.
static bool crcOk(const uint8_t* frame, int total) {
  if (total < 4) return false;
  const int n = total - 2;
  const uint16_t le = (uint16_t)frame[n] | ((uint16_t)frame[n + 1] << 8);
  const uint16_t be = ((uint16_t)frame[n] << 8) | frame[n + 1];
  const uint16_t modbus = crc16Modbus(frame, n);
  if (modbus == le || modbus == be) return true;
  const uint16_t ccitt = crc16Ccitt(frame, n, 0xFFFF);
  if (ccitt == le || ccitt == be) return true;
  const uint16_t xmodem = crc16Ccitt(frame, n, 0);
  if (xmodem == le || xmodem == be) return true;
  return false;
}

static bool has8(int total, int index) {
  return index >= 0 && index < total - 2;
}

static bool has16(int total, int index) {
  return has8(total, index) && has8(total, index + 1);
}

static int16_t be16(const uint8_t* p) {
  return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static bool freshSince(uint32_t stamp) {
  return stamp != 0 && (uint32_t)(millis() - stamp) < kFreshMs;
}

static uint32_t stampNow() {
  const uint32_t now = millis();
  return now ? now : 1;
}

static void accountRuntime();

static void noteOutdoorRun(uint8_t mode, bool defrostFlag) {
  s_defrost = defrostFlag || mode == 0x03;
  s_outdoorMode = mode;
}

static void forgetOutdoorIfStale() {
  if (freshSince(s_outdoorMs)) return;
  s_defrost = false;
}

static bool indoorTempOk(int16_t t) {
  return t >= 32 && t <= 110;
}

static bool outdoorTempOk(int16_t t) {
  return t >= -80 && t <= 150;
}

static void onMaster(const uint8_t* frame, int total) {
  if (has16(total, 11)) {
    const int16_t t = be16(frame + 11);
    if (indoorTempOk(t)) {
      s_tempF = t;
      s_haveTemp = true;
    }
  }
  if (has16(total, 13)) {
    const int16_t t = be16(frame + 13);
    if (indoorTempOk(t)) {
      s_heatSpF = t;
      s_haveHeatSp = true;
    }
  }
  if (has8(total, 17) && isRHValid(frame[17])) {
    s_rh = frame[17];
    s_haveRh = true;
  }
  if (has8(total, 18)) s_connexMode = frame[18];
  s_masterMs = stampNow();
  if (!s_loggedMaster) {
    s_loggedMaster = true;
    SerialPrint("Bryant Connex zone frame", true);
  }
}

static void onOutdoor(const uint8_t* frame, int total) {
  accountRuntime();
  if (has16(total, 11)) {
    const int16_t t = be16(frame + 11);
    if (outdoorTempOk(t)) {
      s_oatF = t;
      s_haveOutdoor = true;
    }
  }
  if (has8(total, 13)) {
    const bool flag = has8(total, 14) && frame[14] == 0x01;
    noteOutdoorRun(frame[13], flag);
  }
  s_outdoorMs = stampNow();
  if (!s_loggedOutdoor) {
    s_loggedOutdoor = true;
    SerialPrint("Bryant outdoor status frame", true);
  }
}

static void handleFrame(const uint8_t* frame, int total) {
  if (total < 11) return;
  if (frame[7] != 0x06) return;
  if (!has8(total, 10)) return;
  const uint8_t row = frame[8];
  const uint16_t table = (uint16_t)frame[9] | ((uint16_t)frame[10] << 8);
  const uint16_t src = ((uint16_t)frame[2] << 8) | frame[3];
  if (row == 0x01 && table == kTableMaster && src == kAddrConnex) onMaster(frame, total);
  else if (row == 0x01 && table == kTableOutdoor && (src == kAddrOutdoorA || src == kAddrOutdoorB)) onOutdoor(frame, total);
}

static void dropBytes(uint8_t n) {
  if (n >= s_len) {
    s_len = 0;
    return;
  }
  memmove(s_buf, s_buf + n, s_len - n);
  s_len = (uint8_t)(s_len - n);
}

static void consume() {
  while (s_len >= 7) {
    const uint16_t dst = ((uint16_t)s_buf[0] << 8) | s_buf[1];
    const uint16_t src = ((uint16_t)s_buf[2] << 8) | s_buf[3];
    const uint8_t len = s_buf[4];
    if (!knownAddr(dst) || !knownAddr(src) || len < 8 || len > 80) {
      dropBytes(1);
      continue;
    }
    const int total = 5 + (int)len;
    if (s_len < total) return;
    if (!crcOk(s_buf, total)) {
      dropBytes(1);
      continue;
    }
    handleFrame(s_buf, total);
    dropBytes((uint8_t)total);
  }
}

void bryantBusBegin() {
  Serial1.setRxBufferSize(2048);
  Serial1.begin(BRYANT_BAUD, SERIAL_8N1, BRYANT_UART_RX, -1);
  SerialPrint("Bryant bus listen-only " + String(BRYANT_BAUD) + " RX " + String(BRYANT_UART_RX), true);
}

void bryantBusPoll() {
  while (Serial1.available() > 0) {
    const int b = Serial1.read();
    if (b < 0) break;
    if (s_len >= sizeof(s_buf)) dropBytes(1);
    s_buf[s_len++] = (uint8_t)b;
    consume();
  }
}

static void setNan(ArborysSnsType* sensor) {
  sensor->snsValue = NAN;
}

static ArborysSnsType* localByType(uint8_t snsType) {
  const int16_t me = Sensors.findMyDeviceIndex();
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex != me || s->snsType != snsType) continue;
    return s;
  }
  return nullptr;
}

static bool numberOf(ArborysSnsType* s, double& out) {
  if (!s || !s->IsSet || isnan(s->snsValue)) return false;
  out = s->snsValue;
  return true;
}

static uint32_t s_accountMs = 0;
static int32_t s_dayKey = -1;
static bool s_restored = false;
static bool s_seenOutdoor = false;

static int32_t localDayNumber(time_t utc) {
  if (!isTimeValid((uint32_t)utc)) return -1;
  const time_t local = unixToLocal(utc);
  if (local < 0) return -1;
  return (int32_t)(local / 86400);
}

static void storeRuntime(ArborysSnsType* sensor) {
#if defined(_USESDCARD)
  if (!sensor || isnan(sensor->snsValue)) return;
  const int16_t idx = Sensors.findSensor(ESP.getEfuseMac(), sensor->snsType, sensor->snsID);
  if (idx >= 0) storeSensorDataSD(idx);
#else
  (void)sensor;
#endif
}

static void finishRuntime(ArborysSnsType* sensor) {
  if (!sensor) return;
  const bool had = !isnan(sensor->snsValue) && sensor->snsValue > 0;
  if (had) storeRuntime(sensor);
  sensor->snsValue = 0;
  if (had) storeRuntime(sensor);
}

static bool lastRuntimePoint(ArborysSnsType* sensor, double& value, uint32_t& stamp) {
#if defined(_USESDCARD)
  if (!sensor) return false;
  const String filename = createSensorFilename(ESP.getEfuseMac(), sensor->snsType, sensor->snsID);
  File file = SD.open(filename, FILE_READ);
  if (!file) return false;
  const uint32_t rec = sizeof(SensorDataPoint);
  const uint32_t fileSize = file.size();
  if (fileSize < rec || (fileSize % rec) != 0) {
    file.close();
    return false;
  }
  if (!file.seek(fileSize - rec)) {
    file.close();
    return false;
  }
  SensorDataPoint point;
  const bool ok = file.read((uint8_t*)&point, rec) == rec;
  file.close();
  if (!ok || isnan(point.snsValue) || point.snsValue < 0) return false;
  value = point.snsValue;
  stamp = point.timeRead ? point.timeRead : point.fileWriteTimestamp;
  return isTimeValid(stamp);
#else
  (void)sensor;
  (void)value;
  (void)stamp;
  return false;
#endif
}

static void restoreRuntime(ArborysSnsType* sensor, bool currentRun) {
  double value = 0;
  uint32_t stamp = 0;
  if (!lastRuntimePoint(sensor, value, stamp)) return;
  const int32_t today = localDayNumber(utcNow());
  const int32_t then = localDayNumber(stamp);
  if (today < 0 || today != then) return;
  if (currentRun && (uint32_t)utcNow() - stamp > 7200UL) return;
  sensor->snsValue = value;
}

static void restoreRuntimeOnce() {
  if (s_restored) return;
  if (!localByType(SNS_BRYANT_RUNTIME)) return;
  if (localDayNumber(utcNow()) < 0) return;
  s_restored = true;
  restoreRuntime(localByType(SNS_BRYANT_RUNTIME), true);
  restoreRuntime(localByType(SNS_BRYANT_DEFROST), true);
  restoreRuntime(localByType(SNS_BRYANT_RUN_COOL), true);
  restoreRuntime(localByType(SNS_BRYANT_DAY_HEAT), false);
  restoreRuntime(localByType(SNS_BRYANT_DAY_DEFROST), false);
  restoreRuntime(localByType(SNS_BRYANT_DAY_COOL), false);
}

static void addMinutes(ArborysSnsType* sensor, double minutes) {
  if (!sensor || minutes <= 0) return;
  if (isnan(sensor->snsValue) || sensor->snsValue < 0) sensor->snsValue = 0;
  sensor->snsValue += minutes;
}

static void accountRuntime() {
  restoreRuntimeOnce();
  const uint32_t now = stampNow();
  double dtMin = 0;
  if (s_accountMs != 0) {
    const uint32_t elapsed = (uint32_t)(now - s_accountMs);
    if (elapsed > 0 && elapsed <= 30UL * 60UL * 1000UL) dtMin = (double)elapsed / 60000.0;
  }
  s_accountMs = now;

  const int32_t day = localDayNumber(utcNow());
  if (day >= 0) {
    if (s_dayKey >= 0 && day != s_dayKey) {
      finishRuntime(localByType(SNS_BRYANT_DAY_HEAT));
      finishRuntime(localByType(SNS_BRYANT_DAY_DEFROST));
      finishRuntime(localByType(SNS_BRYANT_DAY_COOL));
    }
    s_dayKey = day;
  }

  const bool outdoorOk = freshSince(s_outdoorMs);
  if (outdoorOk) s_seenOutdoor = true;
  if (!outdoorOk) {
    if (s_seenOutdoor) {
      finishRuntime(localByType(SNS_BRYANT_RUNTIME));
      finishRuntime(localByType(SNS_BRYANT_DEFROST));
      finishRuntime(localByType(SNS_BRYANT_RUN_COOL));
    }
    return;
  }

  const bool heating = s_outdoorMode == 0x02 || s_defrost;
  const bool cooling = s_outdoorMode == 0x01 && !s_defrost;
  if (heating) addMinutes(localByType(SNS_BRYANT_RUNTIME), dtMin);
  else finishRuntime(localByType(SNS_BRYANT_RUNTIME));
  if (s_defrost) addMinutes(localByType(SNS_BRYANT_DEFROST), dtMin);
  else finishRuntime(localByType(SNS_BRYANT_DEFROST));
  if (cooling) addMinutes(localByType(SNS_BRYANT_RUN_COOL), dtMin);
  else finishRuntime(localByType(SNS_BRYANT_RUN_COOL));
  if (heating) addMinutes(localByType(SNS_BRYANT_DAY_HEAT), dtMin);
  if (s_defrost) addMinutes(localByType(SNS_BRYANT_DAY_DEFROST), dtMin);
  if (cooling) addMinutes(localByType(SNS_BRYANT_DAY_COOL), dtMin);
}

struct FreshTemp {
  double value;
  uint32_t stamp;
  bool outside;
  bool ok;
};

static bool freshNumber(ArborysSnsType* s, double& out, uint32_t& stamp) {
  if (!s || !s->IsSet || isnan(s->snsValue)) return false;
  if (s->snsValue < -80.0 || s->snsValue > 150.0) return false;
  if (s->SendingInt == 0) return false;
  const uint32_t st = s->timeLogged ? s->timeLogged : s->timeRead;
  if (st == 0) return false;
  const uint32_t now = (uint32_t)utcNow();
  if (now > st + sensorExpiryGraceSec(s->SendingInt)) return false;
  out = s->snsValue;
  stamp = st;
  return true;
}

static void keepNewer(FreshTemp& best, double value, uint32_t stamp, bool outside) {
  if (best.ok && stamp <= best.stamp) return;
  best.value = value;
  best.stamp = stamp;
  best.outside = outside;
  best.ok = true;
}

static bool nonServerDevice(const ArborysDevType* d) {
  return d && d->IsSet && !IS_SERVER_DEVICE_TYPE(d->devType);
}

static bool outdoorTempAggregate(ArborysSnsType* s) {
  if (!s || s->snsType != SNS_AGGREGATE) return false;
  if (s->snsName && strstr(s->snsName, "Temp_Outdoor")) return true;
  const int16_t prefs = SensorHistory.getSensorHistoryIndex(s);
  if (prefs < 0) return false;
  char category[24];
  uint8_t place = 0;
  Actuators_aggregateRule(prefs, nullptr, 0, category, sizeof(category), &place);
  return place == 2 && strcmp(category, "temperature") == 0;
}

// 1. Fresh Connex outdoor-unit temperature.
// 2. Newest fresh temperature on a non-server device: an OAT checkbox, or an
//    outside-flagged temperature on a registered peripheral.
// 3. Newest fresh outside temperature on a registered weather server, preferring
//    that server's outdoor temperature aggregate.
static bool resolveOutsideTemp(double& out, bool& outside) {
  if (freshSince(s_outdoorMs) && s_haveOutdoor) {
    out = s_oatF;
    outside = true;
    return true;
  }

  FreshTemp best = {};
  const int16_t me = Sensors.findMyDeviceIndex();
  ArborysSnsType* oat = localByType(SNS_BRYANT_OAT);
  const int16_t oatPrefs = oat ? SensorHistory.getSensorHistoryIndex(oat) : -1;
  if (oatPrefs >= 0 && oatPrefs <= 255) {
    AggPick links[AGG_MAX_LINKS];
    const uint8_t n = AggLinks_linksForPrefs((uint8_t)oatPrefs, links, AGG_MAX_LINKS);
    for (uint8_t i = 0; i < n; i++) {
      const int16_t si = Sensors.findSensor(links[i].mac, links[i].snsType, links[i].snsID);
      ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
      if (!s || !Sensors.isSensorOfType(s, "temperature")) continue;
      if (!nonServerDevice(Sensors.getDeviceByDevIndex(s->deviceIndex))) continue;
      double v = 0;
      uint32_t stamp = 0;
      if (!freshNumber(s, v, stamp)) continue;
      keepNewer(best, v, stamp, Sensors.isOutsideSensor(si));
    }
  }
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex == me) continue;
    if (!Sensors.isSensorOfType(s, "temperature") || !Sensors.isOutsideSensor(i)) continue;
    if (!nonServerDevice(Sensors.getDeviceByDevIndex(s->deviceIndex))) continue;
    double v = 0;
    uint32_t stamp = 0;
    if (!freshNumber(s, v, stamp)) continue;
    keepNewer(best, v, stamp, true);
  }
  if (best.ok) {
    out = best.value;
    outside = best.outside;
    return true;
  }

  FreshTemp serverBest = {};
  for (int16_t di = 0; di < NUMDEVICES; di++) {
    if (di == me) continue;
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) continue;
    FreshTemp aggregate = {};
    FreshTemp raw = {};
    for (int16_t si = 0; si < NUMSENSORS; si++) {
      ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
      if (!s || !s->IsSet || s->deviceIndex != di) continue;
      double v = 0;
      uint32_t stamp = 0;
      if (!freshNumber(s, v, stamp)) continue;
      if (outdoorTempAggregate(s)) keepNewer(aggregate, v, stamp, true);
      else if (Sensors.isSensorOfType(s, "temperature") && Sensors.isOutsideSensor(si)) keepNewer(raw, v, stamp, true);
    }
    const FreshTemp pick = aggregate.ok ? aggregate : raw;
    if (pick.ok) keepNewer(serverBest, pick.value, pick.stamp, true);
  }
  if (!serverBest.ok) return false;
  out = serverBest.value;
  outside = true;
  return true;
}

static const char* gapKind(ArborysSnsType* sensor) {
  const int16_t prefs = SensorHistory.getSensorHistoryIndex(sensor);
  const char* expr = Actuators_rulesetText(prefs);
  if (!expr || strncmp(expr, "gap:", 4) != 0) return "";
  return expr + 4;
}

static void publishGap(ArborysSnsType* sensor) {
  const char* which = gapKind(sensor);
  double actual = 0;
  double setpoint = 0;
  bool haveActual = false;
  if (strcmp(which, "master") == 0) haveActual = numberOf(localByType(SNS_BRYANT_TEMP), actual);
  else if (strcmp(which, "up") == 0) haveActual = numberOf(Actuators_findLocalAggregate("temperature", 1), actual);
  else if (strcmp(which, "oat") == 0) {
    bool outside = false;
    haveActual = resolveOutsideTemp(actual, outside);
    (void)outside;
  }
  const bool haveSp = numberOf(localByType(SNS_BRYANT_SETPOINT), setpoint);
  if (!haveActual || !haveSp) {
    setNan(sensor);
    return;
  }
  const double gap = actual - setpoint;
  sensor->snsValue = gap < 0.0 ? gap : 0.0;
}

void bryantPublish(ArborysSnsType* sensor) {
  if (!sensor) return;
  bitWrite(sensor->Flags, 3, 1);
  const bool masterOk = freshSince(s_masterMs);
  forgetOutdoorIfStale();
  accountRuntime();
  const bool outdoorOk = freshSince(s_outdoorMs);

  switch (sensor->snsType) {
    case SNS_BRYANT_MODE:
      if (outdoorOk) sensor->snsValue = s_outdoorMode;
      else setNan(sensor);
      break;
    case SNS_BRYANT_OAT: {
      double outsideTemp = 0;
      bool outside = false;
      if (resolveOutsideTemp(outsideTemp, outside)) {
        sensor->snsValue = outsideTemp;
        bitWrite(sensor->Flags, 4, outside ? 1 : 0);
      } else {
        setNan(sensor);
      }
      break;
    }
    case SNS_BRYANT_SETPOINT:
      if (masterOk && s_haveHeatSp) sensor->snsValue = s_heatSpF;
      else setNan(sensor);
      break;
    case SNS_BRYANT_TEMP:
      if (masterOk && s_haveTemp) sensor->snsValue = s_tempF;
      else setNan(sensor);
      break;
    case SNS_BRYANT_RH:
      if (masterOk && s_haveRh) sensor->snsValue = s_rh;
      else setNan(sensor);
      break;
    case SNS_BRYANT_RUNTIME:
    case SNS_BRYANT_DEFROST:
    case SNS_BRYANT_RUN_COOL:
    case SNS_BRYANT_DAY_HEAT:
    case SNS_BRYANT_DAY_DEFROST:
    case SNS_BRYANT_DAY_COOL:
      if (isnan(sensor->snsValue)) sensor->snsValue = 0;
      break;
    case SNS_TEMP_GAP:
      publishGap(sensor);
      break;
    default:
      break;
  }
}

static bool connexAllowsHeat() {
  if (!freshSince(s_masterMs)) return false;
  const uint8_t mode = s_connexMode & 0x0F;
  return mode == 1 || mode == 3 || mode == 4;
}

static void refreshGaps() {
  const int16_t me = Sensors.findMyDeviceIndex();
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex != me || s->snsType != SNS_TEMP_GAP) continue;
    bryantPublish(s);
  }
}

void bryantEvaluateZone(ArborysSnsType* sensor) {
  if (!sensor) return;
  refreshGaps();
  const bool named4 = sensor->snsName && strstr(sensor->snsName, "Zone4");
  const bool named2 = sensor->snsName && strstr(sensor->snsName, "Zone2");
  const bool zone4 = named4 || (!named2 && sensor->snsID >= 2);
  ZoneLatch& latch = zone4 ? s_zone4 : s_zone2;

  const int16_t prefs = SensorHistory.getSensorHistoryIndex(sensor);
  const bool request = Actuators_rulesetOn(prefs);
  const uint32_t now = millis();
  const bool hold = latch.on && latch.since != 0 && (uint32_t)(now - latch.since) < kMinOnMs;

  bool on = hold;
  if (connexAllowsHeat() && request) on = true;
  if (on && !latch.on) {
    latch.on = true;
    latch.since = stampNow();
  } else if (!on) {
    latch.on = false;
    latch.since = 0;
  }

  sensor->snsValue = on ? 1.0 : 0.0;
  bitWrite(sensor->Flags, 0, on ? 1 : 0);
  bitWrite(sensor->Flags, 3, 1);
  bitWrite(sensor->Flags, 5, on ? 1 : 0);
}

#if defined(_USESSD1306)

// 128x64, System5x7: 8 rows of 21 characters. These six lines fit, so the
// screen does not cycle.
// Outdoor mode letter: A idle, H heat, C cool, D defrost.

static ArborysSnsType* zoneSensor(bool zone4) {
  const int16_t me = Sensors.findMyDeviceIndex();
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex != me || s->snsType != SNS_HYDRONIC_ZONE) continue;
    const bool named4 = s->snsName && strstr(s->snsName, "Zone4");
    const bool named2 = s->snsName && strstr(s->snsName, "Zone2");
    const bool is4 = named4 || (!named2 && s->snsID >= 2);
    if (is4 == zone4) return s;
  }
  return nullptr;
}

static void fmtNum(char* out, size_t n, ArborysSnsType* s) {
  if (!s || !s->IsSet || isnan(s->snsValue)) {
    snprintf(out, n, "--");
    return;
  }
  snprintf(out, n, "%d", (int)lround(s->snsValue));
}

static char modeLetter(ArborysSnsType* s) {
  if (!s || !s->IsSet || isnan(s->snsValue)) return '-';
  switch ((int)lround(s->snsValue)) {
    case 0: return 'A';
    case 1: return 'C';
    case 2: return 'H';
    case 3: return 'D';
    default: return '?';
  }
}

static const char* zoneWord(ArborysSnsType* s) {
  if (!s || !s->IsSet || isnan(s->snsValue)) return "--";
  return s->snsValue >= 0.5 ? "ON" : "OFF";
}

void bryantOledRefresh() {
  char lines[6][22];
  const bool sta = wifiReadyForNetwork();
  const bool ap = softApRunning();
  if (!sta || ap) {
    lines[0][0] = '\0';
    auto add = [&](const char* word) {
      const size_t used = strlen(lines[0]);
      const size_t room = sizeof(lines[0]) - used;
      if (room < 2) return;
      if (used > 0) {
        lines[0][used] = ' ';
        lines[0][used + 1] = '\0';
      }
      const size_t at = strlen(lines[0]);
      snprintf(lines[0] + at, sizeof(lines[0]) - at, "%s", word);
    };
    if (ap) add("AP");
    if (sta) add("STA");
    if (bleProvisionIsActive()) add("BLT");
    if (!lines[0][0]) snprintf(lines[0], sizeof(lines[0]), "NO WIFI");
  } else {
    snprintf(lines[0], sizeof(lines[0]), "%s", WiFi.localIP().toString().c_str());
  }

  char tgt[8];
  char z2t[8];
  char out[8];
  char z4t[8];
  fmtNum(tgt, sizeof(tgt), localByType(SNS_BRYANT_SETPOINT));
  fmtNum(z2t, sizeof(z2t), localByType(SNS_BRYANT_TEMP));
  fmtNum(out, sizeof(out), localByType(SNS_BRYANT_OAT));
  fmtNum(z4t, sizeof(z4t), Actuators_findLocalAggregate("temperature", 1));
  snprintf(lines[1], sizeof(lines[1]), "Mode %c   Tgt %s", modeLetter(localByType(SNS_BRYANT_MODE)), tgt);
  snprintf(lines[2], sizeof(lines[2]), "Z2T %s   Out %s", z2t, out);
  snprintf(lines[3], sizeof(lines[3]), "Z4T %s", z4t);
  snprintf(lines[4], sizeof(lines[4]), "Z2 %s", zoneWord(zoneSensor(false)));
  snprintf(lines[5], sizeof(lines[5]), "Z4 %s", zoneWord(zoneSensor(true)));

  static char shown[6][22];
  static bool haveShown = false;
  bool changed = !haveShown;
  for (int i = 0; i < 6 && !changed; i++) {
    if (strcmp(shown[i], lines[i]) != 0) changed = true;
  }
  if (!changed) return;
  oled.clear();
  oled.setCursor(0, 0);
  for (int i = 0; i < 6; i++) {
    oled.println(lines[i]);
    memcpy(shown[i], lines[i], sizeof(shown[i]));
  }
  haveShown = true;
}

#endif

#endif
