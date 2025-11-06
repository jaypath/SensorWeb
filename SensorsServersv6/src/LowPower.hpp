#ifdef _USELOWPOWER
#ifndef LOWPOWER_HPP
#define LOWPOWER_HPP
#include "Devices.hpp"
#include "globals.hpp"


extern Devices_Sensors Sensors;
extern STRUCT_PrefsH Prefs;

void LOWPOWER_sleep(uint64_t sleepTime = _USELOWPOWER);
void LOWPOWER_readAndSend();
void LOWPOWER_Initialize();
#endif
#endif
