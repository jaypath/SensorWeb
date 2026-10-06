#pragma once

#include "device_roles.hpp"

struct ArborysSnsType;

// Listen-only Bryant Evolution bus. Never writes the UART.
#if defined(_USEBRYANT)

void bryantBusBegin();
void bryantBusPoll();
void bryantPublish(ArborysSnsType* sensor);
void bryantEvaluateZone(ArborysSnsType* sensor);
#if defined(_USESSD1306)
void bryantOledRefresh();
#endif

#else

inline void bryantBusBegin() {}
inline void bryantBusPoll() {}
inline void bryantPublish(ArborysSnsType*) {}
inline void bryantEvaluateZone(ArborysSnsType*) {}

#endif
