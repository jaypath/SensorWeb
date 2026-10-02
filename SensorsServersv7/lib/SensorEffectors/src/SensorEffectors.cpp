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

  switch (snsType) {
    case 73: // timer countdown DIO
    case 75: // clock-window DIO
    case 200: // RCWL daily count / recent activity
    case 220: // button daily count / recent activity
      break;
    default:
      break;
  }
}
