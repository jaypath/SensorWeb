#ifndef SENSOR_EFFECTORS_HPP
#define SENSOR_EFFECTORS_HPP

#include <Arduino.h>

// Called once from setupSensors(). Wire pins / restore last effector state here.
void SensorEffectors_init();

// Called when a local sensor's alarm flag (Flags bit 0) changes.
// previousFlags is the Flags byte from before this read/limit update.
void SensorEffectors_onAlarmChange(uint8_t snsType, uint8_t snsID, double snsValue,
                                   uint8_t flags, uint8_t previousFlags);

#endif
