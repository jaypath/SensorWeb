#pragma once

// Independent compile-time device roles (not mutually exclusive).
//
// _HAS_LOCAL_SENSORS — local sensor subsystem: sensors.cpp, ReadData, SNS prefs, send upstream
// _IS_SERVER_HUB     — hub: collect all remote device/sensor data, ping expired sensors (5 min per sensor), broadcast presence
//                       non-hub nodes store server devices (devType>=100) only, not remote sensors
//
// Every env must set at least one to 1. Typical configs:
//   Local sensor node:  _HAS_LOCAL_SENSORS=1  _IS_SERVER_HUB=0
//   Weather/hub server: _HAS_LOCAL_SENSORS=0  _IS_SERVER_HUB=1
//   Hybrid monitor hub: _HAS_LOCAL_SENSORS=1  _IS_SERVER_HUB=1  _MYTYPE=100
//
// _MYTYPE (runtime network identity) is separate from these compile-time roles.

#ifndef _HAS_LOCAL_SENSORS
#define _HAS_LOCAL_SENSORS 0
#endif

#ifndef _IS_SERVER_HUB
#define _IS_SERVER_HUB 0
#endif

#if !_HAS_LOCAL_SENSORS && !_IS_SERVER_HUB
#error "Define _HAS_LOCAL_SENSORS=1 and/or _IS_SERVER_HUB=1 in platformio build_flags"
#endif
