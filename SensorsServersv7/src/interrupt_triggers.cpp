#include "device_roles.hpp"
#include "interrupt_triggers.hpp"
#include "globals.hpp"
#include "sensors.hpp"
#include "Devices.hpp"
#include "server.hpp"

#ifndef TIMEZERO
#define TIMEZERO 1735689600
#endif

// ---------------------------------------------------------------------------
// Type 75 — clock-window DIO (Out32 12v/24v/Shed lights). No interrupts.
// ---------------------------------------------------------------------------

static uint32_t s_cachedSunriseUtc = 0;
static uint32_t s_cachedSunsetUtc = 0;
static uint32_t s_lastSunReqAt = 0;

static void clockDioEnsureSunTimes() {
  if (s_cachedSunriseUtc >= (uint32_t)TIMEZERO && s_cachedSunsetUtc >= (uint32_t)TIMEZERO) {
    return;
  }
  const uint32_t now = (uint32_t)utcNow();
  if (s_lastSunReqAt != 0 && now >= s_lastSunReqAt && (now - s_lastSunReqAt) < 900u) {
    return;
  }
  if (!wifiReadyForNetwork()) return;
  s_lastSunReqAt = now;

  uint32_t rise = 0, set = 0;
  if (requestSunTimesFromType100(rise, set) == 1) {
    s_cachedSunriseUtc = rise;
    s_cachedSunsetUtc = set;
  }
}

// Spec: 0–23 local hour; -1 = dawn; -2 = dusk. Returns false if unusable.
static bool clockDioResolveMinutes(int spec, int& minutesOut) {
  if (spec >= 0 && spec <= 23) {
    minutesOut = spec * 60;
    return true;
  }
  if (spec != -1 && spec != -2) return false;

  clockDioEnsureSunTimes();
  if (s_cachedSunriseUtc >= (uint32_t)TIMEZERO && s_cachedSunsetUtc >= (uint32_t)TIMEZERO) {
    const uint32_t utcEvent = (spec == -1) ? s_cachedSunriseUtc : s_cachedSunsetUtc;
    const time_t localEvent = unixToLocal((time_t)utcEvent);
    minutesOut = (int)hour(localEvent) * 60 + (int)minute(localEvent);
    return true;
  }
  // Until a type-100 sunAck arrives, assume 6am / 6pm local.
  minutesOut = (spec == -1) ? (6 * 60) : (18 * 60);
  return true;
}

static bool clockDioInOnWindow(int onMin, int offMin, int nowMin) {
  if (onMin == offMin) return true; // identical bounds → always on
  if (onMin < offMin) return nowMin >= onMin && nowMin < offMin;
  // Wraps midnight (e.g. dusk → dawn, or dusk → midnight).
  return nowMin >= onMin || nowMin < offMin;
}

static constexpr uint8_t kMaxWebForces = 8;

struct WebForceState {
  ArborysSnsType* sensor = nullptr;
  bool active = false;
  bool forceOn = false;
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
};

static WebForceState s_webForces[kMaxWebForces];

/** Decode snsPin encoding to GPIO (same rules as getPinType digital/analog ranges). */
static int8_t snsPinToGpio(int16_t pin) {
  if (pin == -9999 || pin == -1) return -1;
  if (pin >= 0 && pin < 100) return (int8_t)pin;
  if (pin >= -99 && pin < 0) return (int8_t)(-pin);
  if (pin >= 200 && pin < 300) return (int8_t)(pin - 200);
  if (pin >= -299 && pin <= -200) return (int8_t)(-pin - 200);
  return -1;
}

static int8_t sensorGpio(const ArborysSnsType* sensor) {
  if (!sensor) return -1;
  return snsPinToGpio(sensor->snsPin);
}

static void driveSensorDio(ArborysSnsType* sensor, bool on) {
  if (!sensor) return;
  const int8_t gpio = sensorGpio(sensor);
  if (gpio < 0) return;
  pinMode((uint8_t)gpio, OUTPUT);
  digitalWrite((uint8_t)gpio, on ? HIGH : LOW);
  sensor->snsValue = on ? 1.0 : 0.0;
  bitWrite(sensor->Flags, 0, on ? 1 : 0);
  bitWrite(sensor->Flags, 6, 1);
}

static WebForceState* findWebForce(const ArborysSnsType* sensor) {
  if (!sensor) return nullptr;
  for (uint8_t i = 0; i < kMaxWebForces; i++) {
    if (s_webForces[i].sensor == sensor) return &s_webForces[i];
  }
  return nullptr;
}

static WebForceState* allocWebForce(ArborysSnsType* sensor) {
  WebForceState* existing = findWebForce(sensor);
  if (existing) return existing;
  for (uint8_t i = 0; i < kMaxWebForces; i++) {
    if (!s_webForces[i].active && s_webForces[i].sensor == nullptr) {
      s_webForces[i].sensor = sensor;
      return &s_webForces[i];
    }
  }
  // Reuse first inactive slot
  for (uint8_t i = 0; i < kMaxWebForces; i++) {
    if (!s_webForces[i].active) {
      s_webForces[i].sensor = sensor;
      return &s_webForces[i];
    }
  }
  return nullptr;
}

static bool webForceExpired(const WebForceState* wf) {
  if (!wf || !wf->active) return true;
  return (millis() - wf->startMs) >= wf->durationMs;
}

static void restoreAutoAfterWebForce(ArborysSnsType* sensor) {
  if (!sensor) return;
  const int8_t gpio = sensorGpio(sensor);
  if (gpio < 0) return;
  if (sensor->snsType == 75) {
    InterruptTriggers_updateClockDio(sensor, gpio, sensor->limitLow, sensor->limitHigh);
  } else if (sensor->snsType == 73 || sensor->snsType == 74) {
    // Countdown types already track state in snsValue; just sync the pin.
    const bool on = sensor->snsValue > 0.0;
    pinMode((uint8_t)gpio, OUTPUT);
    digitalWrite((uint8_t)gpio, on ? HIGH : LOW);
    bitWrite(sensor->Flags, 0, on ? 1 : 0);
  } else {
    // Non-timer outputs: return low (safe idle) when web pulse ends.
    pinMode((uint8_t)gpio, OUTPUT);
    digitalWrite((uint8_t)gpio, LOW);
    sensor->snsValue = 0.0;
    bitWrite(sensor->Flags, 0, 0);
    bitWrite(sensor->Flags, 6, 1);
  }
}

static bool clockDioApplyWebForceIfActive(ArborysSnsType* sensor, int8_t dioGpio) {
  WebForceState* wf = findWebForce(sensor);
  if (!wf || !wf->active) return false;
  if (webForceExpired(wf)) {
    wf->active = false;
    return false;
  }
  pinMode((uint8_t)dioGpio, OUTPUT);
  digitalWrite((uint8_t)dioGpio, wf->forceOn ? HIGH : LOW);
  sensor->snsValue = wf->forceOn ? 1.0 : 0.0;
  bitWrite(sensor->Flags, 0, wf->forceOn ? 1 : 0);
  return true;
}

void InterruptTriggers_updateClockDio(ArborysSnsType* sensor, int8_t dioGpio,
                                      double limitLowOn, double limitHighOff) {
  if (!sensor || dioGpio < 0) return;

  if (clockDioApplyWebForceIfActive(sensor, dioGpio)) return;

  const time_t nowUtc = utcNow();
  if (nowUtc < (time_t)TIMEZERO) {
    pinMode((uint8_t)dioGpio, OUTPUT);
    digitalWrite((uint8_t)dioGpio, LOW);
    sensor->snsValue = 0.0;
    bitWrite(sensor->Flags, 0, 0);
    return;
  }

  int onMin = 0;
  int offMin = 0;
  if (!clockDioResolveMinutes((int)limitLowOn, onMin) ||
      !clockDioResolveMinutes((int)limitHighOff, offMin)) {
    return;
  }

  const time_t nowLocal = unixToLocal(nowUtc);
  const int nowMin = (int)hour(nowLocal) * 60 + (int)minute(nowLocal);
  const bool wantOn = clockDioInOnWindow(onMin, offMin, nowMin);

  pinMode((uint8_t)dioGpio, OUTPUT);
  digitalWrite((uint8_t)dioGpio, wantOn ? HIGH : LOW);
  sensor->snsValue = wantOn ? 1.0 : 0.0;
  bitWrite(sensor->Flags, 0, wantOn ? 1 : 0);
}

bool InterruptTriggers_webSetOutput(ArborysSnsType* sensor, bool on, uint8_t seconds) {
  if (!sensor || !sensor->IsSet) return false;
  if (!isSwitchStateOutputType(sensor->snsType)) return false;

  // Timer-countdown outputs: write snsValue; automatic countdown / button / motion still apply.
  if (sensor->snsType == 73 || sensor->snsType == 74) {
    if (on) {
      if (seconds == 0) return false;
      sensor->snsValue = (double)seconds;
    } else {
      sensor->snsValue = 0.0;
    }
    const int8_t gpio = sensorGpio(sensor);
    if (gpio >= 0) {
      const bool isOn = sensor->snsValue > 0.0;
      pinMode((uint8_t)gpio, OUTPUT);
      digitalWrite((uint8_t)gpio, isOn ? HIGH : LOW);
      bitWrite(sensor->Flags, 0, isOn ? 1 : 0);
    }
    bitWrite(sensor->Flags, 6, 1);
    return true;
  }

  // Clock / other outputs have no intrinsic countdown — require a pulse duration.
  if (seconds == 0) return false;

  WebForceState* wf = allocWebForce(sensor);
  if (!wf) return false;
  wf->active = true;
  wf->forceOn = on;
  wf->startMs = millis();
  wf->durationMs = (uint32_t)seconds * 1000UL;
  driveSensorDio(sensor, on);
  return true;
}

uint8_t InterruptTriggers_webForceRemainingSec(const ArborysSnsType* sensor) {
  WebForceState* wf = findWebForce(sensor);
  if (!wf || !wf->active || webForceExpired(wf)) return 0;
  const uint32_t elapsed = millis() - wf->startMs;
  if (elapsed >= wf->durationMs) return 0;
  const uint32_t leftMs = wf->durationMs - elapsed;
  uint32_t leftSec = (leftMs + 999UL) / 1000UL;
  if (leftSec > 255UL) leftSec = 255UL;
  return (uint8_t)leftSec;
}

void InterruptTriggers_serviceWebForces() {
  for (uint8_t i = 0; i < kMaxWebForces; i++) {
    WebForceState* wf = &s_webForces[i];
    if (!wf->active || !wf->sensor) continue;
    if (!webForceExpired(wf)) continue;
    wf->active = false;
    restoreAutoAfterWebForce(wf->sensor);
  }
}

#if _USEINTERRUPT

#ifndef _MOTION_EXTEND_CAP_SEC
#define _MOTION_EXTEND_CAP_SEC 120
#endif

extern Devices_Sensors Sensors;
extern STRUCT_SNSHISTORY SensorHistory;

static constexpr uint8_t kMaxIrqSensors = 8;

struct IrqSensorState {
  ArborysSnsType* sensor = nullptr;
  int8_t irqGpio = -1;
  uint8_t snsType = 0;
  volatile bool risingPending = false;
  volatile uint32_t riseMs = 0;
  uint32_t lastAcceptedRiseMs = 0;
  uint32_t lastActivityMs = 0;
  int32_t lastLocalDayKey = -1;
  bool registered = false;
};

static IrqSensorState s_irqStates[kMaxIrqSensors];
static uint8_t s_irqCount = 0;
static bool s_rcwlEnableReady = false;

static ArborysSnsType* associatedSensor(int prefsIndex) {
  if (prefsIndex < 0 || prefsIndex >= (int)_SENSORNUM) return nullptr;
  return Sensors.getSensorBySnsIndex(SensorHistory.sensorIndex[prefsIndex]);
}

static IrqSensorState* findState(ArborysSnsType* sensor) {
  if (!sensor) return nullptr;
  for (uint8_t i = 0; i < s_irqCount; i++) {
    if (s_irqStates[i].sensor == sensor) return &s_irqStates[i];
  }
  return nullptr;
}

static IrqSensorState* allocState(ArborysSnsType* sensor) {
  IrqSensorState* existing = findState(sensor);
  if (existing) return existing;
  if (s_irqCount >= kMaxIrqSensors) return nullptr;
  IrqSensorState* st = &s_irqStates[s_irqCount++];
  st->sensor = sensor;
  st->registered = true;
  return st;
}

static uint32_t sensorPollSec(const ArborysSnsType* sensor) {
  if (!sensor) return 0;
  return sensor->PollingInt;
}

static int32_t localDayKey(time_t utc) {
  if (utc < (time_t)TIMEZERO) return -1;
  const time_t local = unixToLocal(utc);
  return (int32_t)year(local) * 512 + (int32_t)month(local) * 32 + (int32_t)day(local);
}

static bool associatedIsOff(const ArborysSnsType* lights) {
  if (!lights || !lights->IsSet) return true;
  return bitRead(lights->Flags, 0) == 0 && lights->snsValue <= 0.0;
}

static void bumpCountWithRecent(ArborysSnsType* sensor, IrqSensorState* st) {
  if (!sensor || !st) return;
  const double count = floor(sensor->snsValue);
  sensor->snsValue = count + 1.0 + 0.1;
  st->lastActivityMs = millis();
  bitWrite(sensor->Flags, 6, 1);
}

static void handleButtonRising(ArborysSnsType* button, IrqSensorState* st) {
  if (!button) return;

  const uint32_t armSec = sensorPollSec(button);
  ArborysSnsType* lights = associatedSensor(_BUTTON_ASSOCIATED_SNS);
  if (lights && lights->IsSet) {
    if (bitRead(lights->Flags, 0) == 1) {
      lights->snsValue = 0.0;
      bitWrite(lights->Flags, 6, 1);
      return;
    }
    if (lights->snsValue <= 0.0) {
      lights->snsValue = (double)armSec;
      bitWrite(lights->Flags, 6, 1);
      bumpCountWithRecent(button, st);
    }
  }
}

static void handleMotionRising(ArborysSnsType* motion, IrqSensorState* st) {
  if (!motion || !st) return;

  bumpCountWithRecent(motion, st);

  ArborysSnsType* lights = associatedSensor(_RCWL_ASSOCIATED_SNS);
  if (associatedIsOff(lights)) return;
  if (lights->snsValue >= (double)_MOTION_EXTEND_CAP_SEC) return;

  const uint32_t pollSec = sensorPollSec(motion);
  lights->snsValue += (double)(pollSec + 5u);
  bitWrite(lights->Flags, 6, 1);
}

static void ensureRcwlEnablePin() {
  const int16_t en = (int16_t)_PIN_ENABLE_RCWL;
  if (en == -9999 || en == -1) return;
  const int16_t gpio = (en < 0) ? (int16_t)(-en) : en;
  if (!s_rcwlEnableReady) {
    pinMode(gpio, OUTPUT);
    s_rcwlEnableReady = true;
  }
  digitalWrite(gpio, HIGH);
}

void IRAM_ATTR InterruptTriggers_isr(void* arg) {
  IrqSensorState* st = static_cast<IrqSensorState*>(arg);
  if (!st || st->irqGpio < 0) return;
  if (digitalRead((uint8_t)st->irqGpio) != HIGH) return;
  st->riseMs = millis();
  st->risingPending = true;
}

void InterruptTriggers_setup(ArborysSnsType* sensor, int8_t irqGpio) {
  if (!sensor) return;

  if (sensor->snsType == 200) {
    ensureRcwlEnablePin();
  }

  if (irqGpio < 0) return;
  if (sensor->snsType != 200 && sensor->snsType != 220) return;

  IrqSensorState* st = allocState(sensor);
  if (!st) return;

  st->irqGpio = irqGpio;
  st->snsType = sensor->snsType;
  st->risingPending = false;
  st->lastAcceptedRiseMs = 0;
  st->lastActivityMs = 0;
  st->lastLocalDayKey = -1;

  if (sensor->snsType == 220) {
    pinMode((uint8_t)irqGpio, INPUT_PULLDOWN);
  } else {
    pinMode((uint8_t)irqGpio, INPUT);
  }

  attachInterruptArg((uint8_t)irqGpio, InterruptTriggers_isr, st, CHANGE);
}

void serviceInterruptSensors() {
  ensureRcwlEnablePin();

  for (uint8_t i = 0; i < s_irqCount; i++) {
    IrqSensorState* st = &s_irqStates[i];
    if (!st->risingPending || !st->sensor) continue;

    noInterrupts();
    const bool pending = st->risingPending;
    const uint32_t riseMs = st->riseMs;
    st->risingPending = false;
    interrupts();
    if (!pending) continue;

    if (st->snsType == 220) {
      const uint32_t elapsed = riseMs - st->lastAcceptedRiseMs;
      if (st->lastAcceptedRiseMs != 0 && elapsed < (uint32_t)_INTERRUPT_DEBOUNCE_MS) {
        continue;
      }
      st->lastAcceptedRiseMs = riseMs;
      handleButtonRising(st->sensor, st);
    } else if (st->snsType == 200) {
      const uint32_t pollSec = sensorPollSec(st->sensor);
      if (pollSec > 0 && st->lastAcceptedRiseMs != 0) {
        const uint32_t elapsed = riseMs - st->lastAcceptedRiseMs;
        if (elapsed < pollSec * 1000UL) {
          continue;
        }
      }
      st->lastAcceptedRiseMs = riseMs;
      handleMotionRising(st->sensor, st);
    }
  }
}

void InterruptTriggers_updateCountSensor(ArborysSnsType* sensor, uint32_t pollIntervalSec) {
  if (!sensor) return;
  if (pollIntervalSec == 0) return;

  IrqSensorState* st = findState(sensor);
  if (!st) {
    st = allocState(sensor);
    if (st) {
      st->snsType = sensor->snsType;
    }
  }

  const int32_t today = localDayKey(utcNow());
  if (st && today >= 0) {
    if (st->lastLocalDayKey >= 0 && st->lastLocalDayKey != today) {
      sensor->snsValue = 0.0;
      st->lastActivityMs = 0;
    }
    st->lastLocalDayKey = today;
  }

  const double count = floor(sensor->snsValue);
  bool recent = false;
  if (st && st->lastActivityMs != 0) {
    const uint32_t ageMs = millis() - st->lastActivityMs;
    recent = ageMs < (pollIntervalSec * 1000UL);
  }
  sensor->snsValue = count + (recent ? 0.1 : 0.0);
}

void InterruptTriggers_updateTimerOutput(ArborysSnsType* sensor, int8_t dioGpio, uint32_t pollIntervalSec) {
  if (!sensor || dioGpio < 0) return;
  if (pollIntervalSec == 0) return;

  if (sensor->snsValue > (double)pollIntervalSec) {
    sensor->snsValue -= (double)pollIntervalSec;
  } else {
    sensor->snsValue = 0.0;
  }

  const bool on = sensor->snsValue > 0.0;
  pinMode((uint8_t)dioGpio, OUTPUT);
  digitalWrite((uint8_t)dioGpio, on ? HIGH : LOW);
  bitWrite(sensor->Flags, 0, on ? 1 : 0);
}

bool InterruptTriggers_simulateRisingEdge(ArborysSnsType* sensor) {
  if (!sensor || !sensor->IsSet) return false;
  if (!isSwitchStateInterruptType(sensor->snsType)) return false;

  IrqSensorState* st = findState(sensor);
  if (!st) {
    st = allocState(sensor);
    if (st) {
      st->snsType = sensor->snsType;
      st->irqGpio = -1;
    }
  }
  if (!st) return false;

  if (sensor->snsType == 220) {
    handleButtonRising(sensor, st);
    return true;
  }
  if (sensor->snsType == 200) {
    handleMotionRising(sensor, st);
    return true;
  }
  return false;
}

#endif // _USEINTERRUPT
