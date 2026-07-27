#ifdef _USELOWPOWER
#ifndef LOWPOWER_HPP
#define LOWPOWER_HPP
#include "globals.hpp"

extern Devices_Sensors Sensors;
extern STRUCT_PrefsH Prefs;

void LOWPOWER_sleep(uint64_t sleepTime = _USELOWPOWER);
/** Boot entry: APSTA recovery, optional rare firmware path, or fast sensor cycle — then deep sleep. */
void LOWPOWER_readAndSend();
void LOWPOWER_Initialize();
#endif
#endif
