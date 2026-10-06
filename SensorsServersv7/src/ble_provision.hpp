#ifndef BLE_PROVISION_HPP
#define BLE_PROVISION_HPP

#include <Arduino.h>

// Espressif BLE Wi-Fi provisioning (network_prov_scheme_ble).
// Runs only while the soft AP is up (STA lost). Stops when STA is usable again.
// The controller is not released, so a later outage can advertise again.
// Phone app: Espressif BLE Provisioning. Device name arborysnet-XXXXXX
// (last six hex digits of the MAC). Proof of possession is the soft-AP password.

#ifndef _USE_BLE_PROV
#define _USE_BLE_PROV 0
#endif

#if defined(ESP32) && _USE_BLE_PROV

/** Start BLE if the soft AP is up and advertising is not already running. */
void bleProvisionBeginIfNeeded();

/** Apply credentials received over BLE, and match advertising to soft-AP state. */
void bleProvisionService();

/** Stop advertising. Safe when not running. Does not release the controller. */
void bleProvisionStop();

bool bleProvisionIsActive();

/** Proof-of-possession string (same as the soft-AP password). */
const char* bleProvisionPop();

/** BLE device name (arborysnet-XXXXXX). */
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
