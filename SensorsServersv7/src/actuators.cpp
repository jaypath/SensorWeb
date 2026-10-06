#include "device_roles.hpp"
#include "actuators.hpp"
#if _HAS_LOCAL_SENSORS

#include "globals.hpp"
#include "sensors.hpp"
#include "agg_links.hpp"
#include "bryant_bus.hpp"
#include "interrupt_triggers.hpp"
#include <math.h>
#include <string.h>

extern Devices_Sensors Sensors;
extern STRUCT_SNSHISTORY SensorHistory;
extern STRUCT_PrefsH Prefs;
extern STRUCT_CORE I;

#ifndef _RULE_COUNT
#define _RULE_COUNT 0
#endif
#ifndef _RULE_LINK_COUNT
#define _RULE_LINK_COUNT 0
#endif

#if _RULE_COUNT > 0
#ifndef _RULE_OP
#define _RULE_OP {1}
#endif
#ifndef _RULE_IN0
#define _RULE_IN0 {-1}
#endif
#ifndef _RULE_TH0
#define _RULE_TH0 {0}
#endif
static const uint8_t kRuleOp[_RULE_COUNT] = _RULE_OP;
static const int16_t kRuleIn0[_RULE_COUNT] = _RULE_IN0;
static const double kRuleTh0[_RULE_COUNT] = _RULE_TH0;
#endif

#if _RULE_LINK_COUNT > 0
#ifndef _RULE_LINK0
#define _RULE_LINK0 {-1}
#endif
static const int16_t kLink0[_SENSORNUM] = _RULE_LINK0;
#endif
#if _RULE_LINK_COUNT > 1
#ifndef _RULE_LINK1
#define _RULE_LINK1 {-1}
#endif
static const int16_t kLink1[_SENSORNUM] = _RULE_LINK1;
#endif
#if _RULE_LINK_COUNT > 2
#ifndef _RULE_LINK2
#define _RULE_LINK2 {-1}
#endif
static const int16_t kLink2[_SENSORNUM] = _RULE_LINK2;
#endif
#if _RULE_LINK_COUNT > 3
#ifndef _RULE_LINK3
#define _RULE_LINK3 {-1}
#endif
static const int16_t kLink3[_SENSORNUM] = _RULE_LINK3;
#endif
#if _RULE_LINK_COUNT > 4
#ifndef _RULE_LINK4
#define _RULE_LINK4 {-1}
#endif
static const int16_t kLink4[_SENSORNUM] = _RULE_LINK4;
#endif

#if defined(_ACTUATOR_RULESET)
static const char* kRuleset[_SENSORNUM] = _ACTUATOR_RULESET;
#endif

#if defined(_ACTUATOR_AUX)
static const int16_t kActuatorAux[_SENSORNUM] = _ACTUATOR_AUX;
#endif

static int16_t prefsIndexOf(ArborysSnsType* sensor) {
  if (!sensor) return -1;
  return SensorHistory.getSensorHistoryIndex(sensor);
}

static ArborysSnsType* sensorAtPrefs(int16_t prefsIndex) {
  if (prefsIndex < 0 || prefsIndex >= (int16_t)_SENSORNUM) return nullptr;
  return Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[prefsIndex]);
}

static int16_t linkAt(int16_t prefsIndex, uint8_t slot) {
  if (prefsIndex < 0 || prefsIndex >= (int16_t)_SENSORNUM) return -1;
#if _RULE_LINK_COUNT > 0
  if (slot >= (uint8_t)_RULE_LINK_COUNT) return -1;
  switch (slot) {
    case 0: return kLink0[prefsIndex];
#if _RULE_LINK_COUNT > 1
    case 1: return kLink1[prefsIndex];
#endif
#if _RULE_LINK_COUNT > 2
    case 2: return kLink2[prefsIndex];
#endif
#if _RULE_LINK_COUNT > 3
    case 3: return kLink3[prefsIndex];
#endif
#if _RULE_LINK_COUNT > 4
    case 4: return kLink4[prefsIndex];
#endif
    default: break;
  }
#else
  (void)slot;
#endif
  return -1;
}

static int16_t findActuatorLinkedTo(uint8_t slot, int16_t inputPrefs) {
  if (inputPrefs < 0) return -1;
  for (int16_t i = 0; i < (int16_t)_SENSORNUM; i++) {
    if (linkAt(i, slot) == inputPrefs) return i;
  }
  return -1;
}

static int16_t extendCapSec(int16_t actuatorPrefs) {
#if defined(_ACTUATOR_AUX)
  if (actuatorPrefs >= 0 && actuatorPrefs < (int16_t)_SENSORNUM && kActuatorAux[actuatorPrefs] > 0) {
    return kActuatorAux[actuatorPrefs];
  }
#else
  (void)actuatorPrefs;
#endif
#ifndef _MOTION_EXTEND_CAP_SEC
#define _MOTION_EXTEND_CAP_SEC 120
#endif
  return (int16_t)_MOTION_EXTEND_CAP_SEC;
}

static bool inputAtLeast(int16_t prefsIndex, double threshold, bool atLeast) {
  ArborysSnsType* input = sensorAtPrefs(prefsIndex);
  if (!input || !input->IsSet) return false;
  if (isnan(input->snsValue) || input->snsValue <= -999) return false;
  return atLeast ? (input->snsValue >= threshold) : (input->snsValue <= threshold);
}

// Op 4: gap + local heat setpoint < threshold. Same test as outdoor < threshold
// when the gap is min(outside - setpoint, 0) and the setpoint is positive.
static bool gapPlusSetpointBelow(int16_t gapPrefs, double threshold) {
  ArborysSnsType* gap = sensorAtPrefs(gapPrefs);
  if (!gap || !gap->IsSet || isnan(gap->snsValue)) return false;
  const int16_t me = Sensors.findMyDeviceIndex();
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex != me || s->snsType != SNS_BRYANT_SETPOINT) continue;
    if (isnan(s->snsValue)) return false;
    return gap->snsValue + s->snsValue < threshold;
  }
  return false;
}

// One rule, one input, one threshold. Fail closed.
static bool ruleResult(int16_t ruleIndex) {
#if _RULE_COUNT > 0
  if (ruleIndex < 0 || ruleIndex >= (int16_t)_RULE_COUNT) return false;
  const int16_t input = kRuleIn0[ruleIndex];
  if (input < 0) return false;
  const uint8_t op = kRuleOp[ruleIndex];
  if (op == 4) return gapPlusSetpointBelow(input, kRuleTh0[ruleIndex]);
  const bool atLeast = (op != 3);
  return inputAtLeast(input, kRuleTh0[ruleIndex], atLeast);
#else
  (void)ruleIndex;
  return false;
#endif
}

struct RuleParse {
  const char* p;
  const int16_t* rules;
  int16_t ruleCount;
  int16_t used;
  bool failed;
};

static void skipSpace(RuleParse& st) {
  while (st.p && *st.p == ' ') st.p++;
}

static bool parseExpr(RuleParse& st, bool& out);

static bool parseAtom(RuleParse& st, bool& out) {
  skipSpace(st);
  if (!st.p || *st.p == 0) {
    st.failed = true;
    return false;
  }
  if (*st.p == '(') {
    st.p++;
    if (!parseExpr(st, out)) return false;
    skipSpace(st);
    if (!st.p || *st.p != ')') {
      st.failed = true;
      return false;
    }
    st.p++;
    return true;
  }
  bool invert = false;
  if (*st.p == 'n' || *st.p == 'N') {
    invert = true;
    st.p++;
    skipSpace(st);
  }
  if (!st.p || (*st.p != 'T' && *st.p != 't' && *st.p != 'F' && *st.p != 'f')) {
    st.failed = true;
    return false;
  }
  const bool positive = (*st.p == 'T' || *st.p == 't');
  st.p++;
  if (st.used >= st.ruleCount) {
    st.failed = true;
    return false;
  }
  const bool value = ruleResult(st.rules[st.used]);
  st.used++;
  out = positive ? value : !value;
  if (invert) out = !out;
  return true;
}

static bool parseExpr(RuleParse& st, bool& out) {
  if (!parseAtom(st, out)) return false;
  for (;;) {
    skipSpace(st);
    if (!st.p || *st.p == 0 || *st.p == ')') return true;
    char op = 'a';
    const char c = *st.p;
    if (c == 'a' || c == 'A' || c == 'o' || c == 'O' || c == 'x' || c == 'X') {
      op = (c == 'o' || c == 'O') ? 'o' : ((c == 'x' || c == 'X') ? 'x' : 'a');
      st.p++;
    }
    bool rhs = false;
    if (!parseAtom(st, rhs)) return false;
    if (op == 'o') out = out || rhs;
    else if (op == 'x') out = out != rhs;
    else out = out && rhs;
  }
}

const char* Actuators_rulesetText(int16_t prefsIndex) {
#if defined(_ACTUATOR_RULESET)
  if (prefsIndex >= 0 && prefsIndex < (int16_t)_SENSORNUM && kRuleset[prefsIndex]) return kRuleset[prefsIndex];
#else
  (void)prefsIndex;
#endif
  return "";
}

static bool rulesetCommand(int16_t prefsIndex) {
  int16_t rules[8];
  int16_t linked = 0;
  for (uint8_t slot = 0; slot < 8; slot++) {
    const int16_t rule = linkAt(prefsIndex, slot);
    if (rule < 0) continue;
    if (linked >= 8) return false;
    rules[linked++] = rule;
  }
  if (linked <= 0) return false;

  const char* expr = "T";
  const char* named = Actuators_rulesetText(prefsIndex);
  if (named && named[0]) expr = named;

  RuleParse st;
  st.p = expr;
  st.rules = rules;
  st.ruleCount = linked;
  st.used = 0;
  st.failed = false;
  bool value = false;
  if (!parseExpr(st, value) || st.failed) return false;
  skipSpace(st);
  if (st.p && *st.p) return false;
  if (st.used != linked) return false;
  return value;
}

bool Actuators_rulesetOn(int16_t prefsIndex) {
  return rulesetCommand(prefsIndex);
}

static void drivePin(ArborysSnsType* sensor, bool on) {
  if (!sensor) return;
  int8_t gpio = -1;
  getPinType(sensor->snsPin, &gpio);
  if (gpio < 0 || gpio > 39) return;
  // pinMode on an output that is already correct glitches the pin. Three light
  // relays share this path and would click on every poll.
  static uint64_t s_outputReady = 0;
  const uint64_t bit = 1ULL << (uint8_t)gpio;
  const bool level = on;
  bool wrote = false;
  if ((s_outputReady & bit) == 0) {
    pinMode((uint8_t)gpio, OUTPUT);
    s_outputReady |= bit;
    digitalWrite((uint8_t)gpio, level ? HIGH : LOW);
    wrote = true;
  } else if ((digitalRead((uint8_t)gpio) == HIGH) != level) {
    digitalWrite((uint8_t)gpio, level ? HIGH : LOW);
    wrote = true;
  }
  // 12 V, 24 V, and shed lights follow one rule and would otherwise pull in together.
  if (wrote) delay(150);
  sensor->snsValue = on ? 1.0 : 0.0;
  bitWrite(sensor->Flags, 0, on ? 1 : 0);
}

static uint32_t s_countdownMs[_SENSORNUM] = {0};

static void markCountdown(int16_t prefsIndex) {
  if (prefsIndex >= 0 && prefsIndex < (int16_t)_SENSORNUM) {
    s_countdownMs[prefsIndex] = millis();
  }
}

static void driveCountdownPin(ArborysSnsType* sensor, int8_t gpio) {
  if (!sensor || gpio < 0) return;
  const bool on = sensor->snsValue > 0.0;
  pinMode((uint8_t)gpio, OUTPUT);
  digitalWrite((uint8_t)gpio, on ? HIGH : LOW);
  bitWrite(sensor->Flags, 0, on ? 1 : 0);
}

static void copyToken(char* dst, size_t dstLen, const char* src, size_t n) {
  if (!dst || dstLen == 0) return;
  if (n >= dstLen) n = dstLen - 1;
  if (src && n) memcpy(dst, src, n);
  dst[n] = 0;
  for (size_t i = 0; dst[i]; i++) {
    if (dst[i] >= 'A' && dst[i] <= 'Z') dst[i] = (char)(dst[i] - 'A' + 'a');
  }
}

static bool tokenIs(const char* s, const char* lit) {
  return s && lit && strcmp(s, lit) == 0;
}

bool Actuators_aggregateRule(int16_t prefsIndex, char* op, size_t opLen, char* category, size_t catLen, uint8_t* place) {
  if (category && catLen) category[0] = 0;
  if (place) *place = 0;

  // Parse into a local buffer. Callers that only want category and place pass op as null,
  // and that must not skip the rest of the rule.
  char parsed[8];
  copyToken(parsed, sizeof(parsed), "avg", 3);

  const char* expr = nullptr;
#if defined(_ACTUATOR_RULESET)
  if (prefsIndex >= 0 && prefsIndex < (int16_t)_SENSORNUM && kRuleset[prefsIndex] && kRuleset[prefsIndex][0]) {
    expr = kRuleset[prefsIndex];
  }
#else
  (void)prefsIndex;
#endif
  if (!expr) {
    if (op && opLen) copyToken(op, opLen, parsed, strlen(parsed));
    return true;
  }

  const char* c1 = strchr(expr, ':');
  const char* opEnd = c1 ? c1 : expr + strlen(expr);
  copyToken(parsed, sizeof(parsed), expr, (size_t)(opEnd - expr));
  const bool aggregateOp = tokenIs(parsed, "avg") || tokenIs(parsed, "min")
      || tokenIs(parsed, "max") || tokenIs(parsed, "any");
  if (!aggregateOp) {
    copyToken(parsed, sizeof(parsed), "avg", 3);
    if (op && opLen) copyToken(op, opLen, parsed, strlen(parsed));
    return true;
  }
  if (op && opLen) copyToken(op, opLen, parsed, strlen(parsed));
  if (!c1 || !category || catLen == 0) return true;
  const char* cat = c1 + 1;
  const char* c2 = strchr(cat, ':');
  copyToken(category, catLen, cat, (size_t)((c2 ? c2 : cat + strlen(cat)) - cat));
  if (c2 && place) {
    char where[12];
    copyToken(where, sizeof(where), c2 + 1, strlen(c2 + 1));
    if (tokenIs(where, "indoor")) *place = 1;
    else if (tokenIs(where, "outdoor")) *place = 2;
  }
  return true;
}

ArborysSnsType* Actuators_findLocalAggregate(const char* category, uint8_t place) {
  if (!category || !category[0]) return nullptr;
  const int16_t me = Sensors.findMyDeviceIndex();
  if (me < 0) return nullptr;
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex != me || s->snsType != SNS_AGGREGATE) continue;
    const int16_t pi = SensorHistory.getSensorHistoryIndex(i);
    char cat[24];
    uint8_t where = 0;
    Actuators_aggregateRule(pi, nullptr, 0, cat, sizeof(cat), &where);
    if (where == place && strcasecmp(cat, category) == 0) return s;
  }
  return nullptr;
}

static bool aggregateSampleFresh(const ArborysSnsType* s) {
  if (!s || !s->IsSet) return false;
  if (isnan(s->snsValue)) return false;
  if (s->SendingInt == 0) return false;
  const uint32_t fresh = s->timeLogged ? s->timeLogged : s->timeRead;
  if (fresh == 0) return false;
  const uint32_t now = (uint32_t)utcNow();
  return now <= fresh + sensorExpiryGraceSec(s->SendingInt);
}

static bool aggregatePlaceOk(const ArborysSnsType* s, const char* category, uint8_t place) {
  if (!s || s->snsType == SNS_AGGREGATE) return false;
  if (category && category[0] && !Sensors.isSensorOfType(s->snsType, category)) return false;
  if (place == 1 && bitRead(s->Flags, 4)) return false;
  if (place == 2 && bitRead(s->Flags, 4) == 0) return false;
  return true;
}

// Named classes share a name. Anything else shares a sensor type, so all soil
// sensors match each other even though soil is not a named aggregate class.
static const char* measurementNameOfType(uint8_t snsType) {
  if (snsType == SNS_AGGREGATE) return "";
  if (Sensors.isSensorOfType(snsType, "temperature")) return "temperature";
  if (Sensors.isSensorOfType(snsType, "humidity")) return "humidity";
  if (Sensors.isSensorOfType(snsType, "pressure")) return "pressure";
  if (Sensors.isSensorOfType(snsType, "distance")) return "distance";
  if (Sensors.isSensorOfType(snsType, "leak")) return "leak";
  return "";
}

static bool measurementOfPick(const AggPick& link, const char** name, uint8_t* type) {
  const int16_t si = Sensors.findSensor(link.mac, link.snsType, link.snsID);
  ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
  if (s && s->snsType == SNS_AGGREGATE) {
    const char* kind = Actuators_aggregateKind(s);
    if (!kind || !kind[0]) return false;
    *name = kind;
    *type = 0;
    return true;
  }
  const uint8_t snsType = s ? s->snsType : link.snsType;
  const char* kind = measurementNameOfType(snsType);
  if (kind[0]) {
    *name = kind;
    *type = 0;
    return true;
  }
  if (snsType == 0) return false;
  *name = "";
  *type = snsType;
  return true;
}

static bool sameMeasurement(const char* aName, uint8_t aType, const char* bName, uint8_t bType) {
  if (aName[0] || bName[0]) return aName[0] && bName[0] && strcmp(aName, bName) == 0;
  return aType != 0 && aType == bType;
}

// Average of a saved list, or of one unsaved group, where every member is one class.
static bool sameClassKnownAverage(int16_t prefsIndex) {
  char op[8];
  char category[24];
  Actuators_aggregateRule(prefsIndex, op, sizeof(op), category, sizeof(category), nullptr);
  if (!tokenIs(op, "avg")) return false;
  if (prefsIndex < 0 || prefsIndex > 255) return false;

  AggPick links[AGG_MAX_LINKS];
  const uint8_t linkCount = AggLinks_linksForPrefs((uint8_t)prefsIndex, links, AGG_MAX_LINKS);
  if (linkCount == 0) return category[0] != 0;

  const char* agreedName = nullptr;
  uint8_t agreedType = 0;
  uint8_t used = 0;
  for (uint8_t i = 0; i < linkCount; i++) {
    if (links[i].mac == 0) continue;
    const char* name = "";
    uint8_t type = 0;
    if (!measurementOfPick(links[i], &name, &type)) return false;
    if (used == 0) {
      agreedName = name;
      agreedType = type;
    } else if (!sameMeasurement(agreedName, agreedType, name, type)) {
      return false;
    }
    used++;
  }
  return used > 0;
}

bool Actuators_averageOmitsMemberGaps(const ArborysSnsType* sensor) {
  if (!sensor || sensor->snsType != SNS_AGGREGATE) return false;
  if (sensor->deviceIndex != I.MY_DEVICE_INDEX) return false;
  const int16_t prefs = SensorHistory.getSensorHistoryIndex(const_cast<ArborysSnsType*>(sensor));
  if (prefs < 0) return false;
  return sameClassKnownAverage(prefs);
}

static void pollAggregate(ArborysSnsType* sensor, int16_t prefsIndex, uint8_t lastflag) {
  if (!sensor) return;
  char op[8];
  char category[24];
  uint8_t place = 0;
  Actuators_aggregateRule(prefsIndex, op, sizeof(op), category, sizeof(category), &place);

  AggPick links[AGG_MAX_LINKS];
  const uint8_t linkCount = (prefsIndex >= 0 && prefsIndex <= 255)
      ? AggLinks_linksForPrefs((uint8_t)prefsIndex, links, AGG_MAX_LINKS) : 0;

  struct AggCand { double v; uint8_t type; uint8_t flagged; };
  AggCand cand[NUMSENSORS];
  uint8_t nCand = 0;
  const bool averaging = !tokenIs(op, "min") && !tokenIs(op, "max") && !tokenIs(op, "any");
  const bool sameClass = averaging && sameClassKnownAverage(prefsIndex);

  // Explicit checks skip the group filter so a forced sensor type is included.
  // The unsaved default still requires the group.
  auto take = [&](ArborysSnsType* s, bool requireCategory) {
    if (!s || s == sensor) return;
    // The unsaved default stays inside this group. A checked sensor is used
    // even when its type is outside the group, including another aggregate.
    if (requireCategory && (s->snsType == SNS_AGGREGATE || !aggregatePlaceOk(s, category, place))) return;
    // Same-class averages skip NaN and expired members. That skip is not an alarm.
    if (sameClass && (isnan(s->snsValue) || isinf(s->snsValue) || s->expired || !aggregateSampleFresh(s))) return;
    if (!aggregateSampleFresh(s)) return;
    if (nCand >= NUMSENSORS) return;
    cand[nCand].v = s->snsValue;
    cand[nCand].type = s->snsType;
    cand[nCand].flagged = bitRead(s->Flags, 0);
    nCand++;
  };

  // A saved list, including an empty one (mac 0), is the user's choice. The
  // outside flag does not exclude a checked sensor.
  if (linkCount > 0) {
    for (uint8_t i = 0; i < linkCount; i++) {
      if (links[i].mac == 0) continue;
      const int16_t si = Sensors.findSensor(links[i].mac, links[i].snsType, links[i].snsID);
      take(Sensors.getSensorBySnsIndex(si), false);
    }
  }
#if _IS_SERVER_HUB && !_HUB_REGISTERED_ONLY
  // Nothing saved yet. :outdoor keeps sensors flagged outside. Otherwise those are left out.
  else if (category[0]) {
    for (int16_t i = 0; i < NUMSENSORS; i++) {
      ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
      if (!s || !s->IsSet || s == sensor) continue;
      const bool outside = bitRead(s->Flags, 4);
      if (place == 2) {
        if (!outside) continue;
      } else if (outside) {
        continue;
      }
      take(s, true);
    }
  }
#endif

  bool keep[NUMSENSORS];
  bool discarded = false;
  for (uint8_t i = 0; i < nCand; i++) keep[i] = true;
  if (averaging) {
    for (uint8_t i = 0; i < nCand; i++) {
      if (!sensorReadingIsPlausible(cand[i].type, cand[i].v, category)) {
        keep[i] = false;
        discarded = true;
      }
    }
    // Temperature, humidity, and pressure: drop a value more than 2 standard
    // deviations from the mean of the other plausible readings. Judged together,
    // not one removal at a time. Fewer than two others has no spread, so only
    // the range check applies.
    if (tokenIs(category, "temperature") || tokenIs(category, "humidity") || tokenIs(category, "pressure")) {
      bool spreadOut[NUMSENSORS];
      for (uint8_t i = 0; i < nCand; i++) spreadOut[i] = false;
      for (uint8_t i = 0; i < nCand; i++) {
        if (!keep[i]) continue;
        double sumOthers = 0;
        uint8_t nOthers = 0;
        for (uint8_t j = 0; j < nCand; j++) {
          if (j == i || !keep[j]) continue;
          sumOthers += cand[j].v;
          nOthers++;
        }
        if (nOthers < 2) continue;
        const double mean = sumOthers / (double)nOthers;
        double acc = 0;
        for (uint8_t j = 0; j < nCand; j++) {
          if (j == i || !keep[j]) continue;
          const double d = cand[j].v - mean;
          acc += d * d;
        }
        const double sd = sqrt(acc / (double)nOthers);
        if (fabs(cand[i].v - mean) > 2.0 * sd) spreadOut[i] = true;
      }
      for (uint8_t i = 0; i < nCand; i++) {
        if (!spreadOut[i]) continue;
        keep[i] = false;
        discarded = true;
      }
    }
  }

  double sum = 0;
  double lo = 0;
  double hi = 0;
  uint8_t n = 0;
  uint8_t nOn = 0;
  for (uint8_t i = 0; i < nCand; i++) {
    if (!keep[i]) continue;
    if (n == 0) lo = hi = cand[i].v;
    else {
      if (cand[i].v < lo) lo = cand[i].v;
      if (cand[i].v > hi) hi = cand[i].v;
    }
    sum += cand[i].v;
    if (cand[i].flagged || cand[i].v != 0.0) nOn++;
    n++;
  }

  bitWrite(sensor->Flags, 3, 1);
  if (n == 0) {
    sensor->snsValue = NAN;
    if (!sensor->expired && bitRead(sensor->Flags, 7)) bitWrite(sensor->Flags, 6, 1);
    sensor->expired = true;
    // A same-class average alarms only from its own limits. A dropped member,
    // including one outside range, is not an alarm. Other aggregates still flag
    // a discarded reading, and a NaN only when critical.
    if (sameClass) bitWrite(sensor->Flags, 0, 0);
    else bitWrite(sensor->Flags, 0, (discarded || bitRead(sensor->Flags, 7)) ? 1 : 0);
    return;
  }

  if (tokenIs(op, "min")) sensor->snsValue = lo;
  else if (tokenIs(op, "max")) sensor->snsValue = hi;
  else if (tokenIs(op, "any")) sensor->snsValue = nOn ? 1.0 : 0.0;
  else sensor->snsValue = sum / (double)n;
  if (sensor->expired && bitRead(sensor->Flags, 7)) bitWrite(sensor->Flags, 6, 1);
  sensor->expired = false;

  if (prefsIndex >= 0 && prefsIndex < (int16_t)_SENSORNUM) {
    applyAlarmFlags(sensor, Prefs.SNS_LIMIT_MAX[prefsIndex], Prefs.SNS_LIMIT_MIN[prefsIndex], lastflag);
  }
  if (tokenIs(op, "any") && nOn) bitWrite(sensor->Flags, 0, 1);
  // A mixed average flags a rejected contributor even when the result is inside the limits.
  // A same-class average does not. Its limits already decided the flag.
  if (averaging && discarded && !sameClass) bitWrite(sensor->Flags, 0, 1);
  bitWrite(sensor->Flags, 3, 1);
}

static void pollSwitch(ArborysSnsType* sensor, int16_t prefsIndex) {
  if (InterruptTriggers_webForceRemainingSec(sensor) > 0) return;
  drivePin(sensor, rulesetCommand(prefsIndex));
}

static void pollCountdown(ArborysSnsType* sensor, int16_t prefsIndex) {
  int8_t gpio = -1;
  getPinType(sensor->snsPin, &gpio);
  const uint32_t pollSec = (prefsIndex >= 0) ? Prefs.SNS_INTERVAL_POLL[prefsIndex] : sensor->PollingInt;
  if (pollSec == 0) {
    const uint32_t nowMs = millis();
    const uint32_t prevMs = (prefsIndex >= 0 && prefsIndex < (int16_t)_SENSORNUM) ? s_countdownMs[prefsIndex] : 0;
    markCountdown(prefsIndex);
    if (prevMs != 0 && sensor->snsValue > 0.0) {
      const double elapsed = (double)(nowMs - prevMs) / 1000.0;
      if (sensor->snsValue > elapsed) sensor->snsValue -= elapsed;
      else sensor->snsValue = 0.0;
    }
    driveCountdownPin(sensor, gpio);
    return;
  }
#if _USEINTERRUPT
  InterruptTriggers_updateTimerOutput(sensor, gpio, pollSec);
#else
  if (gpio < 0) return;
  if (sensor->snsValue > (double)pollSec) sensor->snsValue -= (double)pollSec;
  else sensor->snsValue = 0.0;
  driveCountdownPin(sensor, gpio);
#endif
}

bool Actuators_onButtonRise(ArborysSnsType* button) {
  const int16_t buttonPrefs = prefsIndexOf(button);
  const int16_t lightsPrefs = findActuatorLinkedTo(1, buttonPrefs);
  ArborysSnsType* lights = sensorAtPrefs(lightsPrefs);
  if (!lights || !lights->IsSet) return false;
  int8_t gpio = -1;
  getPinType(lights->snsPin, &gpio);
  if (bitRead(lights->Flags, 0) == 1 || lights->snsValue > 0.0) {
    lights->snsValue = 0.0;
    if (bitRead(lights->Flags, 1)) bitWrite(lights->Flags, 6, 1);
    driveCountdownPin(lights, gpio);
    markCountdown(lightsPrefs);
    return false;
  }
  const uint32_t armSec = button ? button->PollingInt : 0;
  lights->snsValue = (double)armSec;
  if (bitRead(lights->Flags, 1)) bitWrite(lights->Flags, 6, 1);
  driveCountdownPin(lights, gpio);
  markCountdown(lightsPrefs);
  return true;
}

void Actuators_onMotionRise(ArborysSnsType* motion) {
  const int16_t motionPrefs = prefsIndexOf(motion);
  const int16_t lightsPrefs = findActuatorLinkedTo(0, motionPrefs);
  ArborysSnsType* lights = sensorAtPrefs(lightsPrefs);
  if (!lights || !lights->IsSet) return;
  if (bitRead(lights->Flags, 0) == 0 && lights->snsValue <= 0.0) return;
  const int16_t cap = extendCapSec(lightsPrefs);
  if (lights->snsValue >= (double)cap) return;
  const uint32_t pollSec = motion ? motion->PollingInt : 0;
  lights->snsValue += (double)(pollSec + 5u);
  if (bitRead(lights->Flags, 1)) bitWrite(lights->Flags, 6, 1);
}

void Actuators_restoreSwitch(ArborysSnsType* sensor) {
  if (!sensor || sensor->snsType != SNS_SWITCH) return;
  pollSwitch(sensor, prefsIndexOf(sensor));
}

static int8_t applyActuator(ArborysSnsType* P, int16_t prefs_index, bool recordAlways) {
  const double valueBefore = P->snsValue;
  const bool pendingChange = bitRead(P->Flags, 6) == 1;
  const uint8_t lastflag = P->Flags;
  bitWrite(P->Flags, 6, 0);

  if (P->snsType == SNS_AGGREGATE) {
    pollAggregate(P, prefs_index, lastflag);
  } else if (P->snsType == SNS_SWITCH) {
    pollSwitch(P, prefs_index);
  } else if (P->snsType == SNS_COUNTDOWN || P->snsType == SNS_COUNTDOWN_INV) {
    pollCountdown(P, prefs_index);
  } else if (P->snsType == SNS_HYDRONIC_ZONE) {
    bryantEvaluateZone(P);
  } else if (P->snsType == SNS_TEMP_GAP) {
    bryantPublish(P);
    if (prefs_index >= 0 && prefs_index < (int16_t)_SENSORNUM) {
      applyAlarmFlags(P, Prefs.SNS_LIMIT_MAX[prefs_index], Prefs.SNS_LIMIT_MIN[prefs_index], lastflag);
    }
    bitWrite(P->Flags, 3, 1);
  }

  const bool flagStep = bitRead(lastflag, 0) != bitRead(P->Flags, 0);
  const bool valueStep = floor(valueBefore) != floor(P->snsValue);
  // Bounds edge sends only when Critical. A prior normal latch sends only when Monitored.
  if (pendingChange || (flagStep && bitRead(P->Flags, 7))) bitWrite(P->Flags, 6, 1);
  if (P->snsType == SNS_AGGREGATE && bitRead(P->Flags, 1) == 0 && bitRead(P->Flags, 7) == 0) bitWrite(P->Flags, 6, 0);

  P->timeRead = (uint32_t)utcNow();
  if (!(P->snsType == SNS_AGGREGATE && isnan(P->snsValue))) {
    if (P->expired && bitRead(P->Flags, 7)) bitWrite(P->Flags, 6, 1);
    P->expired = false;
  }
  if (recordAlways || flagStep || valueStep) {
    SensorHistory.recordSentValue(P);
  }
  return 1;
}

int8_t pollActuator(ArborysSnsType* P, bool forceRead) {
  if (!P) return -1;
  if (P->deviceIndex != I.MY_DEVICE_INDEX) return -1;
  if (!IS_ACTUATOR_SENSOR_TYPE(P->snsType)) return -1;

  const int16_t prefs_index = SensorHistory.getSensorHistoryIndex(P);
  if (prefs_index == -2) return -2;
  if (prefs_index < 0) return -1;

  const uint32_t nowUtc = (uint32_t)utcNow();
  const uint32_t pollSec = Prefs.SNS_INTERVAL_POLL[prefs_index];
  // Poll 0 is the every-loop path in serviceFastActuators, not this second tick.
  if (!forceRead && pollSec == 0) return 0;
  if (!forceRead && !(P->timeRead == 0 || P->timeRead > nowUtc ||
      P->timeRead + pollSec < nowUtc ||
      nowUtc - P->timeRead > 60 * 60 * 24)) {
    return 0;
  }

  return applyActuator(P, prefs_index, true);
}

static const char* dedicatedMeasurement(const char* name) {
  if (!name || !name[0]) return "";
  if (strcmp(name, "temperature") == 0) return "temperature";
  if (strcmp(name, "humidity") == 0 || strcmp(name, "rh") == 0) return "humidity";
  if (strcmp(name, "pressure") == 0) return "pressure";
  if (strcmp(name, "distance") == 0 || strcmp(name, "dist") == 0) return "distance";
  if (strcmp(name, "leak") == 0) return "leak";
  return "";
}

static const char* memberMeasurement(const ArborysSnsType* sensor) {
  if (!sensor || !sensor->IsSet) return "";
  if (sensor->snsType == SNS_AGGREGATE) return Actuators_aggregateKind(sensor);
  if (Sensors.isSensorOfType(sensor->snsType, "temperature")) return "temperature";
  if (Sensors.isSensorOfType(sensor->snsType, "humidity")) return "humidity";
  if (Sensors.isSensorOfType(sensor->snsType, "pressure")) return "pressure";
  if (Sensors.isSensorOfType(sensor->snsType, "distance")) return "distance";
  if (Sensors.isSensorOfType(sensor->snsType, "leak")) return "leak";
  return "";
}

const char* Actuators_aggregateKind(const ArborysSnsType* sensor) {
  if (!sensor || sensor->snsType != SNS_AGGREGATE) return "";
  if (sensor->deviceIndex != I.MY_DEVICE_INDEX) return "";
  static uint8_t depth = 0;
  if (depth >= 4) return "";
  const int16_t prefs = SensorHistory.getSensorHistoryIndex(const_cast<ArborysSnsType*>(sensor));
  if (prefs < 0) return "";

  depth++;
  const char* result = "";
  AggPick links[AGG_MAX_LINKS];
  const uint8_t linkCount = AggLinks_linksForPrefs((uint8_t)prefs, links, AGG_MAX_LINKS);
  if (linkCount == 0) {
    // Nothing saved yet, so the default set is the ruleset category.
    char category[24];
    Actuators_aggregateRule(prefs, nullptr, 0, category, sizeof(category), nullptr);
    result = dedicatedMeasurement(category);
  } else {
    // A saved list wins, including an empty save (mac 0), which stays generic.
    uint8_t used = 0;
    const char* agreed = nullptr;
    bool mixed = false;
    for (uint8_t i = 0; i < linkCount; i++) {
      if (links[i].mac == 0) continue;
      used++;
      const int16_t si = Sensors.findSensor(links[i].mac, links[i].snsType, links[i].snsID);
      const char* kind = memberMeasurement(Sensors.getSensorBySnsIndex(si));
      if (!kind[0]) {
        mixed = true;
        break;
      }
      if (!agreed) agreed = kind;
      else if (strcmp(agreed, kind) != 0) {
        mixed = true;
        break;
      }
    }
    if (used > 0 && !mixed && agreed) result = agreed;
  }
  depth--;
  return result;
}

void serviceFastActuators() {
  for (int16_t i = 0; i < (int16_t)_SENSORNUM; i++) {
    if (Prefs.SNS_INTERVAL_POLL[i] != 0) continue;
    ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[i]);
    if (!sensor || !sensor->IsSet) continue;
    if (sensor->deviceIndex != I.MY_DEVICE_INDEX) continue;
    if (sensor->snsType == SNS_AGGREGATE) continue;
    if (!IS_ACTUATOR_SENSOR_TYPE(sensor->snsType)) continue;
    applyActuator(sensor, i, false);
  }
}

#else
bool Actuators_onButtonRise(ArborysSnsType*) { return false; }
void Actuators_onMotionRise(ArborysSnsType*) {}
void Actuators_restoreSwitch(ArborysSnsType*) {}
int8_t pollActuator(ArborysSnsType*, bool) { return -1; }
void serviceFastActuators() {}
const char* Actuators_rulesetText(int16_t) { return ""; }
bool Actuators_rulesetOn(int16_t) { return false; }
bool Actuators_aggregateRule(int16_t, char* op, size_t opLen, char* category, size_t catLen, uint8_t* place) {
  if (op && opLen) {
    op[0] = 'a';
    if (opLen > 1) op[1] = 'v';
    if (opLen > 2) op[2] = 'g';
    if (opLen > 3) op[3] = 0;
  }
  if (category && catLen) category[0] = 0;
  if (place) *place = 0;
  return true;
}
ArborysSnsType* Actuators_findLocalAggregate(const char*, uint8_t) { return nullptr; }
const char* Actuators_aggregateKind(const ArborysSnsType*) { return ""; }
bool Actuators_averageOmitsMemberGaps(const ArborysSnsType*) { return false; }
#endif
