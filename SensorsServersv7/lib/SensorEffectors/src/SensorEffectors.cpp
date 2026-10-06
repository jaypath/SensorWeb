#include "SensorEffectors.hpp"

void SensorEffectors_init() {
  // No hardware yet. Add pinMode / default-off for relays, LEDs, etc. here.
}

void SensorEffectors_onAlarmChange(uint8_t snsType, uint8_t snsID, double snsValue,
                                   uint8_t flags, uint8_t previousFlags) {
  const bool nowAlarmed = bitRead(flags, 0) == 1;
  const bool wasAlarmed = bitRead(previousFlags, 0) == 1;
  if (nowAlarmed == wasAlarmed) return;

  (void)snsID;
  (void)snsValue;

  (void)snsType;
}
