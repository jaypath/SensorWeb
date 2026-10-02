#ifndef INTERRUPT_TRIGGERS_HPP
#define INTERRUPT_TRIGGERS_HPP

#include <Arduino.h>
#include "device_roles.hpp"

struct ArborysSnsType;

// Sensor families used by /SWITCHSTATE
inline bool isSwitchStateOutputType(uint8_t snsType) {
  return snsType >= 71 && snsType <= 79;
}
inline bool isSwitchStateInterruptType(uint8_t snsType) {
  return snsType >= 200;
}

// --- Type 75: clock-window DIO OUTPUT (no IRQ; used by Out32 etc.) ---
// limitLow = on time, limitHigh = off time.
// Spec: 0–23 = local hour, -1 = dawn, -2 = dusk. snsValue = 0 (LOW) or 1 (HIGH).
void InterruptTriggers_updateClockDio(ArborysSnsType* sensor, int8_t dioGpio,
                                     double limitLowOn, double limitHighOff);

// Web SwitchState: set output for types 71–79.
// Type 73/74: on → snsValue=seconds (countdown); off → snsValue=0. Auto logic still runs.
// Type 75 (and other non-countdown outputs): timed force for `seconds` (1–255), then auto resumes.
// Returns false if rejected (bad args / unsupported).
bool InterruptTriggers_webSetOutput(ArborysSnsType* sensor, bool on, uint8_t seconds);

// Remaining web-force seconds for non-countdown outputs (0 if none).
uint8_t InterruptTriggers_webForceRemainingSec(const ArborysSnsType* sensor);

// Expire timed web forces and restore auto state. Call from loop when local sensors exist.
void InterruptTriggers_serviceWebForces();

#if _USEINTERRUPT

#ifndef _INTERRUPT_DEBOUNCE_MS
#define _INTERRUPT_DEBOUNCE_MS 50
#endif

#ifndef _PIN_ENABLE_RCWL
#define _PIN_ENABLE_RCWL -9999
#endif
#ifndef _RCWL_ASSOCIATED_SNS
#define _RCWL_ASSOCIATED_SNS -1
#endif
#ifndef _BUTTON_ASSOCIATED_SNS
#define _BUTTON_ASSOCIATED_SNS -1
#endif

// Attach CHANGE IRQ for type 200 (RCWL) / 220 (button). Type 73 is polled output only.
void InterruptTriggers_setup(ArborysSnsType* sensor, int8_t irqGpio);

// Process pending rising edges (debounce + associated-sensor updates). Call from loop.
void serviceInterruptSensors();

// Poll-path helpers for types 200/220 (daily reset + .0/.1 activity decimal).
// pollIntervalSec == 0 means caller should skip (never update).
void InterruptTriggers_updateCountSensor(ArborysSnsType* sensor, uint32_t pollIntervalSec);

// Type 73: countdown by pollIntervalSec, drive DIO, set Flags bit0 to match DIO.
void InterruptTriggers_updateTimerOutput(ArborysSnsType* sensor, int8_t dioGpio, uint32_t pollIntervalSec);

// Simulate a rising-edge interrupt action (e.g. web "push button") for types 200–255.
bool InterruptTriggers_simulateRisingEdge(ArborysSnsType* sensor);

void InterruptTriggers_isr(void* arg);

#endif // _USEINTERRUPT

#endif // INTERRUPT_TRIGGERS_HPP
