#ifndef ACTUATORS_HPP
#define ACTUATORS_HPP

#include <Arduino.h>

struct ArborysSnsType;

// Due-interval test matches ReadData: 0 not due, >0 success, <0 error.
// Type 170 drives the pin from the linked rules. Type 171/172 counts down.
int8_t pollActuator(ArborysSnsType* sensor, bool forceRead);

// Shed edges. The countdown row names its inputs with _RULE_LINK0 (presence)
// and _RULE_LINK1 (button). Returns true when the button press should be counted.
bool Actuators_onButtonRise(ArborysSnsType* button);
void Actuators_onMotionRise(ArborysSnsType* motion);

// Re-apply a type-170 rule after a web force expires.
void Actuators_restoreSwitch(ArborysSnsType* sensor);

// Actuators whose poll interval is 0. Called from loop(), not from the
// once-per-second read. A sensor poll of 0 still means "do not poll".
// Type 173 is not a fast actuator: poll 0 means do not update.
void serviceFastActuators();

// Type 173 ruleset. op is avg, min, max, or any. category is empty when the
// rule does not name a sensor group. place: 0 all, 1 indoor, 2 outdoor.
bool Actuators_aggregateRule(int16_t prefsIndex, char* op, size_t opLen, char* category, size_t catLen, uint8_t* place);

// Build-flag ruleset string for this prefs row. Empty when the row has none.
const char* Actuators_rulesetText(int16_t prefsIndex);

// True when that row's rule expression is true. A missing input, a NAN input,
// or a bad expression is false.
bool Actuators_rulesetOn(int16_t prefsIndex);

// Local type-173 sensor whose ruleset category and place match. place 2 is outdoor.
ArborysSnsType* Actuators_findLocalAggregate(const char* category, uint8_t place);

// "" when the aggregate is generic. Otherwise temperature, humidity, pressure,
// distance, or leak, when every sensor it combines is that kind.
const char* Actuators_aggregateKind(const ArborysSnsType* sensor);

// True for an average of one class of known sensors. A NaN or expired member
// is left out of that average and does not alarm it.
bool Actuators_averageOmitsMemberGaps(const ArborysSnsType* sensor);

#endif
