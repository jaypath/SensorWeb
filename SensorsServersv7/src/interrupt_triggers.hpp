#ifndef INTERRUPT_TRIGGERS_HPP
#define INTERRUPT_TRIGGERS_HPP

#include <Arduino.h>
#include "device_roles.hpp"

struct ArborysSnsType;

// Sensor families used by /SWITCHSTATE
inline bool isSwitchStateOutputType(uint8_t snsType) {
  return snsType == SNS_SWITCH || snsType == SNS_COUNTDOWN || snsType == SNS_COUNTDOWN_INV;
}
inline bool isSwitchStateInterruptType(uint8_t snsType) {
  return IS_INTERRUPT_SENSOR_TYPE(snsType);
}

// Clock window used by type 162 Timer_On_H. True while local time is inside
// [on, off). Spec: 0-23 = hour, -1 = dawn, -2 = dusk. False if time is unknown.
bool InterruptTriggers_inClockWindow(double limitLowOn, double limitHighOff);

// Legacy clock-window pin driver. Timer_On_H does not use a pin.
void InterruptTriggers_updateClockDio(ArborysSnsType* sensor, int8_t dioGpio,
                                     double limitLowOn, double limitHighOff);

// Web SwitchState for type 170 (timed force) and 171/172 (write the countdown).
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

// Attach CHANGE IRQ for presence (110) and button (111).
void InterruptTriggers_setup(ArborysSnsType* sensor, int8_t irqGpio);

// Process pending rising edges (debounce + associated-sensor updates). Call from loop.
void serviceInterruptSensors();

// Poll-path helpers for presence and button (daily reset + .0/.1 activity decimal).
// pollIntervalSec == 0 means caller should skip (never update).
void InterruptTriggers_updateCountSensor(ArborysSnsType* sensor, uint32_t pollIntervalSec);

// Type 171/172: countdown by pollIntervalSec, drive DIO, set Flags bit0 to match DIO.
void InterruptTriggers_updateTimerOutput(ArborysSnsType* sensor, int8_t dioGpio, uint32_t pollIntervalSec);

// Simulate a rising edge (web "push button") for presence and button.
bool InterruptTriggers_simulateRisingEdge(ArborysSnsType* sensor);

void InterruptTriggers_isr(void* arg);

#endif // _USEINTERRUPT

#endif // INTERRUPT_TRIGGERS_HPP
