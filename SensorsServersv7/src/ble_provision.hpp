#ifndef BLE_PROVISION_HPP
#define BLE_PROVISION_HPP

#include <Arduino.h>

// Espressif BLE Wi-Fi provisioning (WiFiProv / network_prov_scheme_ble).
// Coexists with the SoftAP HTTP wizard: BLE only injects the same Prefs credentials.
// Lifetime: starts at boot when credentials are missing; torn down on success or at
// BLE_PROV_MAX_MS after boot (whichever first). BT stack memory is released on stop.

#ifndef _USE_BLE_PROV
#define _USE_BLE_PROV 0
#endif

#ifndef BLE_PROV_MAX_MS
#define BLE_PROV_MAX_MS (30UL * 60UL * 1000UL)  // 30 minutes from boot
#endif

// Gate on ESP32 (framework define), not _USE32 — this header may be included before globals.hpp.
#if defined(ESP32) && _USE_BLE_PROV

/** Start BLE portal if unprovisioned and still inside the boot window. One-shot per boot. */
void bleProvisionBeginIfNeeded();

/** Periodic: apply pending Prefs, enforce 30-minute teardown. */
void bleProvisionService();

/** Tear down BLE immediately (safe to call when not running). Releases BTDM/BLE RAM. */
void bleProvisionStop();

bool bleProvisionIsActive();

/** Proof-of-possession string for the Espressif phone app (stable per device). */
const char* bleProvisionPop();

/** BLE service name (PROV_XXXXXX). Empty if not started. */
const char* bleProvisionServiceName();

#else

inline void bleProvisionBeginIfNeeded() {}
inline void bleProvisionService() {}
inline void bleProvisionStop() {}
inline bool bleProvisionIsActive() { return false; }
inline const char* bleProvisionPop() { return ""; }
inline const char* bleProvisionServiceName() { return ""; }

#endif

#endif
