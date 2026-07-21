#pragma once

// Shared weather package format for full (_USEWEATHER) producers and lite (_USEWEATHERLITE) consumers.
//
// SECURITY: Package transfer uses plain HTTP on the LAN (GET/POST /WEATHERPKG).
// Anyone who can reach the device can read or inject weather packages. Do not expose
// these endpoints beyond a trusted local network; prefer firewall/VLAN isolation.

#include <stdint.h>

#if defined(_USEWEATHER) || defined(_USEWEATHERLITE)

static constexpr uint8_t WEATHER_PKG_VER_MAJOR = 1;
static constexpr uint8_t WEATHER_PKG_VER_MINOR = 0;

// Section types
static constexpr uint8_t WPKG_SEC_WEATHERDATA = 1;
static constexpr uint8_t WPKG_SEC_EVENT = 2;
// 3+ reserved (e.g. daily detail bins — not packaged today)

static constexpr uint8_t WPKG_FLAG_DATA_STALE = 0x01; // producer marks NOAA/data stale at pack time

static constexpr uint16_t WEATHER_PKG_MAX_SECTIONS = 64;
static constexpr uint32_t WEATHER_PKG_MAX_BYTES = 50u * 1024u;

// Packed section entry: absolute file offset + type
#pragma pack(push, 1)
struct WeatherPkgSectionEnt {
  uint32_t start;
  uint8_t type;
};
#pragma pack(pop)

// Header layout (bytes 0..3 = version; changing this layout requires a major version bump):
//   [0] major [1] minor [2..3] headerBytes
//   [4..7] packagedAt [8..11] totalSize
//   [12..13] weatherStoreVersion [14..15] sizeof(WeatherInfoOptimized)
//   [16] sectionCount [17] flags [18..19] reserved
//   [20..] section table (MAX_SECTIONS * 5), then padding to HEADER_BYTES
// Today: CORE=20 + 64*5 = 340 used; HEADER_BYTES = 2× used = 680.
static constexpr uint16_t WEATHER_PKG_HEADER_CORE =
    4u + 4u + 4u + 2u + 2u + 1u + 1u + 2u;
static constexpr uint16_t WEATHER_PKG_HEADER_USED =
    (uint16_t)(WEATHER_PKG_HEADER_CORE + WEATHER_PKG_MAX_SECTIONS * sizeof(WeatherPkgSectionEnt));
static constexpr uint16_t WEATHER_PKG_HEADER_BYTES =
    (uint16_t)(WEATHER_PKG_HEADER_USED * 2u);

static constexpr const char* WEATHER_PKG_PATH = "/Data/weatherdata.pkg";
static constexpr const char* WEATHER_PKG_TMP_PATH = "/Data/weatherdata.pkg.tmp";
static constexpr const char* WEATHER_PKG_RECV_TMP_PATH = "/Data/weatherdata.recv.tmp";

#endif
