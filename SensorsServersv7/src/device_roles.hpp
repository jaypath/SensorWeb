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
//   Hybrid:             _HAS_LOCAL_SENSORS=1  _IS_SERVER_HUB=1
//   Type 100 forces hybrid. Other servers (weatherlite 101, link hub 102) are
//   hybrid when the env sets _HAS_LOCAL_SENSORS=1.
//   Type 102 stores and pings only devices the user registered. Types 100 and 101
//   still accept every device, including when they have aggregate sensors.
//
// _MYTYPE (runtime network identity) is separate from these compile-time roles,
// except type 100 forces hybrid and type 102 forces registered-only ingest.
// Device types: 1–99 peripheral, 100–150 server. Sensor type numbers: src/sensors.hpp.

#ifndef DEV_SERVER_TYPE_MIN
#define DEV_SERVER_TYPE_MIN 100
#endif
#ifndef DEV_SERVER_TYPE_MAX
#define DEV_SERVER_TYPE_MAX 150
#endif
// Server that ingests only devices registered on the link table (max 10).
#ifndef DEV_TYPE_LINK_HUB
#define DEV_TYPE_LINK_HUB 102
#endif

// Sensor types. The table in src/sensors.hpp is the source of truth.
#define SNS_LEAK 101
#define SNS_BINARY 102
#define SNS_BINARY_INV 103
#define SNS_PRESENCE 110
#define SNS_BUTTON 111
#define SNS_HVAC_CALL 120
#define SNS_VALVE 121
#define SNS_CLOCK 151
#define SNS_NET_RSSI 152
#define SNS_NET_FIRST 153
#define SNS_NET_LAST 161
#define SNS_TIMER_ON_H 162
#define SNS_HVAC_TOTAL 163
#define SNS_ACTUATOR_MIN 170
#define SNS_ACTUATOR_MAX 219
#define SNS_SWITCH 170
#define SNS_COUNTDOWN 171
#define SNS_COUNTDOWN_INV 172
// Aggregate of linked sensors. No GPIO. Ruleset: avg, min, max, or any.
// Optional :category and :indoor or :outdoor. Broadcast only when monitored.
// A hub's own aggregate defaults to monitored and critical. A receiving hub
// sets override 0b10000011 the first time it registers that sensor, which
// forces flagged, monitored, and critical off.
// Typing stays "actuator" unless every combined sensor is one kind, in which
// case it is also that kind (temperature, humidity, pressure, distance, leak).
#define SNS_AGGREGATE 173
// Bryant Evolution, listen-only. 47 is outside the actuator range so poll
// does not own it. 174 is the hydronic recommendation and does send.
#define SNS_BRYANT_DEFROST 47
#define SNS_BRYANT_RUN_COOL 48
#define SNS_BRYANT_DAY_HEAT 49
#define SNS_BRYANT_DAY_DEFROST 66
#define SNS_BRYANT_DAY_COOL 67
#define SNS_BRYANT_MODE 164
#define SNS_BRYANT_OAT 165
#define SNS_BRYANT_SETPOINT 166
#define SNS_BRYANT_TEMP 167
#define SNS_BRYANT_RH 168
#define SNS_BRYANT_RUNTIME 169
#define SNS_HYDRONIC_ZONE 174
// min(actual - heat setpoint, 0). Stays an actuator, not a temperature.
#define SNS_TEMP_GAP 175
#define SNS_SERVER_TYPE_MIN 220
#define SNS_SERVER_TYPE_MAX 254
#define SNS_TYPE_EXTENDED 255

#define IS_SERVER_DEVICE_TYPE(t) ((unsigned)(t) >= (unsigned)DEV_SERVER_TYPE_MIN && (unsigned)(t) <= (unsigned)DEV_SERVER_TYPE_MAX)
#define IS_PERIPHERAL_DEVICE_TYPE(t) ((unsigned)(t) < (unsigned)DEV_SERVER_TYPE_MIN)
#define IS_SERVER_SENSOR_TYPE(t) ((unsigned)(t) >= (unsigned)SNS_SERVER_TYPE_MIN && (unsigned)(t) <= (unsigned)SNS_SERVER_TYPE_MAX)
#define IS_ACTUATOR_SENSOR_TYPE(t) ((unsigned)(t) >= (unsigned)SNS_ACTUATOR_MIN && (unsigned)(t) <= (unsigned)SNS_ACTUATOR_MAX)
#define IS_INTERRUPT_SENSOR_TYPE(t) ((unsigned)(t) == (unsigned)SNS_PRESENCE || (unsigned)(t) == (unsigned)SNS_BUTTON)
#define IS_NETWORK_SENSOR_TYPE(t) ((unsigned)(t) >= (unsigned)SNS_NET_RSSI && (unsigned)(t) <= (unsigned)SNS_NET_LAST)
#define IS_HVAC_RUNTIME_TYPE(t) ((unsigned)(t) == (unsigned)SNS_HVAC_CALL || (unsigned)(t) == (unsigned)SNS_HVAC_TOTAL)
// Bryant minutes: current heat (169), defrost (47), cool (48), and the three daily totals.
// Monitored and Critical stay off. The values stay on this device and on its SD history.
#define IS_BRYANT_TIME_TYPE(t) ((unsigned)(t) == (unsigned)SNS_BRYANT_RUNTIME || (unsigned)(t) == (unsigned)SNS_BRYANT_DEFROST || (unsigned)(t) == (unsigned)SNS_BRYANT_RUN_COOL || (unsigned)(t) == (unsigned)SNS_BRYANT_DAY_HEAT || (unsigned)(t) == (unsigned)SNS_BRYANT_DAY_DEFROST || (unsigned)(t) == (unsigned)SNS_BRYANT_DAY_COOL)
#define IS_EXTENDED_SENSOR_TYPE(t) ((unsigned)(t) == (unsigned)SNS_TYPE_EXTENDED)

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

// Type 100 is always a hybrid hub. A later -D of either flag does not turn that off.
#if defined(_MYTYPE) && ((_MYTYPE) == 100)
#undef _HAS_LOCAL_SENSORS
#undef _IS_SERVER_HUB
#define _HAS_LOCAL_SENSORS 1
#define _IS_SERVER_HUB 1
#endif

#ifndef _HUB_REGISTERED_ONLY
#if defined(_MYTYPE) && ((_MYTYPE) == DEV_TYPE_LINK_HUB)
#define _HUB_REGISTERED_ONLY 1
#else
#define _HUB_REGISTERED_ONLY 0
#endif
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

// Interrupt inputs (presence 110, button 111): peripherals only.
// Implementation is in src/interrupt_triggers.hpp/.cpp when _USEINTERRUPT=1.
#ifndef _USEINTERRUPT
#define _USEINTERRUPT 0
#endif
#if _USEINTERRUPT && !_HAS_LOCAL_SENSORS
#error "_USEINTERRUPT requires _HAS_LOCAL_SENSORS=1"
#endif
