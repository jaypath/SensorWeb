#pragma once

#ifdef _USEWEATHERLITE

#include "Weather_Optimized.hpp"
#include "WeatherPkg.hpp"

// Lite weather consumer: no NOAA fetch. Receives/requests streamed packages from type-100 servers.
// SECURITY: uses plain HTTP GET/POST /WEATHERPKG on the LAN (see WeatherPkg.hpp).

static constexpr uint32_t WEATHER_LITE_STALE_SEC = 90u * 60u;      // request when packagedAt older than this
static constexpr uint32_t WEATHER_LITE_MIN_REQUEST_SEC = 30u * 60u; // max request rate

struct WeatherLiteState {
    uint32_t lastPackagePackagedAt = 0;   // from package header
    uint32_t lastPackageReceivedAt = 0;   // local time when unpack succeeded
    uint32_t lastRequestAttemptAt = 0;    // rate limit (includes failed attempts)
    bool lastPackageMarkedStale = false;  // WPKG_FLAG_DATA_STALE from producer
};

extern WeatherLiteState WeatherLite;

void weatherLiteApplyIFlagsFromPackage();
bool weatherLiteUnpackFile(const char* path);
bool weatherLiteRequestFromServer(IPAddress ip);
bool weatherLiteRequestFromAnyWeatherServer();
void serviceWeatherLite(bool minuteTick);

#endif
