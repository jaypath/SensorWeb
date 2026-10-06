#pragma once

#include <Arduino.h>
#include "device_roles.hpp"

// Devices the user registered for aggregate sensors, and which remote sensors
// feed each local type-173 prefs slot. At most 10 devices.
// SD builds store this next to DevicesSensors.dat. Builds without SD use the
// "agglinks" NVS namespace, not the prefs boot blob.

#define AGG_MAX_DEVICES 10
#define AGG_MAX_LINKS 40

struct AggPick {
  uint64_t mac;
  uint8_t snsType;
  uint8_t snsID;
};

#if _IS_SERVER_HUB

bool AggLinks_hasDevice(uint64_t mac);
bool AggLinks_addDevice(uint64_t mac, IPAddress ip, const char* name, uint8_t devType);
void AggLinks_noteDevice(uint64_t mac, IPAddress ip, uint32_t sendingInt);
void AggLinks_load();
void AggLinks_save();
String AggLinks_groupName(uint8_t snsType);
bool AggLinks_isPicked(uint8_t prefsIndex, uint64_t mac, uint8_t snsType, uint8_t snsID);
uint8_t AggLinks_linksForPrefs(uint8_t prefsIndex, AggPick* out, uint8_t maxOut);
// err is "missing" or "full" when this returns false. A pick does not have to
// match the aggregate group. An unknown MAC is stored as a manual device.
bool AggLinks_setPicks(uint8_t prefsIndex, const AggPick* picks, uint8_t count, String& err);

#else

inline bool AggLinks_hasDevice(uint64_t) { return false; }
inline bool AggLinks_addDevice(uint64_t, IPAddress, const char*, uint8_t) { return true; }
inline void AggLinks_noteDevice(uint64_t, IPAddress, uint32_t) {}
inline void AggLinks_load() {}
inline void AggLinks_save() {}
inline String AggLinks_groupName(uint8_t) { return String(); }
inline bool AggLinks_isPicked(uint8_t, uint64_t, uint8_t, uint8_t) { return false; }
inline uint8_t AggLinks_linksForPrefs(uint8_t, AggPick*, uint8_t) { return 0; }
inline bool AggLinks_setPicks(uint8_t, const AggPick*, uint8_t, String&) { return false; }

#endif
