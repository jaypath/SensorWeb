#pragma once

// Independent compile-time device roles (not mutually exclusive).
//
// _HAS_LOCAL_SENSORS — local sensor subsystem: sensors.cpp, ReadData, SNS prefs, send upstream
// _IS_SERVER_HUB     — hub: collect all remote device/sensor data, ping expired peripherals, broadcast presence
//                       non-hub nodes store server devices (devType 100–150) only, not remote sensors
//
// Every env must set at least one to 1. Typical configs:
//   Local sensor node:  _HAS_LOCAL_SENSORS=1  _IS_SERVER_HUB=0
//   Weather/hub server: _HAS_LOCAL_SENSORS=0  _IS_SERVER_HUB=1
//   Hybrid monitor hub: _HAS_LOCAL_SENSORS=1  _IS_SERVER_HUB=1  _MYTYPE=100
//
// _MYTYPE (runtime network identity) is separate from these compile-time roles.
// Device types: 1–99 peripheral, 100–150 server. Sensor types 100–150 are server slots;
// 200–255 are interrupt-driven sensors.

#ifndef DEV_SERVER_TYPE_MIN
#define DEV_SERVER_TYPE_MIN 100
#endif
#ifndef DEV_SERVER_TYPE_MAX
#define DEV_SERVER_TYPE_MAX 150
#endif
#ifndef SNS_SERVER_TYPE_MIN
#define SNS_SERVER_TYPE_MIN 100
#endif
#ifndef SNS_SERVER_TYPE_MAX
#define SNS_SERVER_TYPE_MAX 150
#endif
#ifndef SNS_INTERRUPT_TYPE_MIN
#define SNS_INTERRUPT_TYPE_MIN 200
#endif

#define IS_SERVER_DEVICE_TYPE(t) ((unsigned)(t) >= (unsigned)DEV_SERVER_TYPE_MIN && (unsigned)(t) <= (unsigned)DEV_SERVER_TYPE_MAX)
#define IS_PERIPHERAL_DEVICE_TYPE(t) ((unsigned)(t) < (unsigned)DEV_SERVER_TYPE_MIN)
#define IS_SERVER_SENSOR_TYPE(t) ((unsigned)(t) >= (unsigned)SNS_SERVER_TYPE_MIN && (unsigned)(t) <= (unsigned)SNS_SERVER_TYPE_MAX)
#define IS_INTERRUPT_SENSOR_TYPE(t) ((unsigned)(t) >= (unsigned)SNS_INTERRUPT_TYPE_MIN)

#ifdef _MYTYPE
// Integer-only: these are used in #if. C casts are not legal in the preprocessor.
#define _I_AM_SERVER ((_MYTYPE) >= (DEV_SERVER_TYPE_MIN) && (_MYTYPE) <= (DEV_SERVER_TYPE_MAX))
#define _I_AM_PERIPHERAL ((_MYTYPE) < (DEV_SERVER_TYPE_MIN))
#else
#define _I_AM_SERVER 0
#define _I_AM_PERIPHERAL 0
#endif

#ifndef _HAS_LOCAL_SENSORS
#define _HAS_LOCAL_SENSORS 0
#endif

#ifndef _IS_SERVER_HUB
#define _IS_SERVER_HUB 0
#endif

#if !_HAS_LOCAL_SENSORS && !_IS_SERVER_HUB
#error "Define _HAS_LOCAL_SENSORS=1 and/or _IS_SERVER_HUB=1 in platformio build_flags"
#endif

// Ring-buffer depth for local sensor history (current + prior samples).
// Peripherals default to 3; hubs default to 24. Override per-env with -D _SENSORHISTORYSIZE=N.
#ifndef _SENSORHISTORYSIZE
#if _IS_SERVER_HUB
#define _SENSORHISTORYSIZE 24
#else
#define _SENSORHISTORYSIZE 3
#endif
#endif

// Cloud Supabase query/upload/claim runtime. Default OFF.
// Enable per-env with -D _SUPABASE_RUNTIME=1 (hubs or selected peripherals).
// Requires _USESUPABASE=1 for Prefs cloud fields (set in base [env]).
#ifndef _SUPABASE_RUNTIME
#define _SUPABASE_RUNTIME 0
#endif
#if _SUPABASE_RUNTIME && !(defined(_USESUPABASE) && (_USESUPABASE))
#error "_SUPABASE_RUNTIME=1 requires _USESUPABASE=1"
#endif

// Weather roles (mutually exclusive). Full NOAA fetch vs package consumer.
#if defined(_USEWEATHER) && defined(_USEWEATHERLITE)
#error "Define only one of _USEWEATHER or _USEWEATHERLITE"
#endif
#if defined(_USEWEATHER) && !defined(_USESDCARD)
#error "_USEWEATHER requires _USESDCARD (weather package + Events on SD)"
#endif
#if defined(_USEWEATHERLITE) && !defined(_USESDCARD)
#error "_USEWEATHERLITE requires _USESDCARD (receive/unpack weather package on SD)"
#endif

// Interrupt sensors (snsType 200–255): peripherals only.
// Implementation is in src/interrupt_triggers.hpp/.cpp when _USEINTERRUPT=1.
#ifndef _USEINTERRUPT
#define _USEINTERRUPT 0
#endif
#if _USEINTERRUPT && !_HAS_LOCAL_SENSORS
#error "_USEINTERRUPT requires _HAS_LOCAL_SENSORS=1"
#endif
