#include "globals.hpp"

#ifdef _USEWEATHERLITE

#include "Weather_Optimized_lite.hpp"
#include "Devices.hpp"
#include "utility.hpp"
#include "server.hpp"
#include <HTTPClient.h>

WeatherLiteState WeatherLite;

static char s_weatherLiteFail[80];

const char* weatherLiteLastRequestError() {
    return s_weatherLiteFail;
}

static void weatherLiteFail(const char* why) {
    snprintf(s_weatherLiteFail, sizeof(s_weatherLiteFail), "%s", why ? why : "request failed");
    SerialPrint(String("weatherLite: ") + s_weatherLiteFail, true);
}

void weatherLiteApplyIFlagsFromPackage() {
    // Derive alert UI flags from packaged object (not transferred via I).
    if (WeatherData.NumWeatherEvents > 0) {
        bitSet(I.WeatherEventFlags, 0); // alerts present
        bitClear(I.WeatherEventFlags, 1);
    } else {
        bitClear(I.WeatherEventFlags, 0);
        bitClear(I.WeatherEventFlags, 1);
    }
    // Load first alert summary into alertInfo if events exist on SD
    if (WeatherData.NumWeatherEvents > 0) {
        WeatherData.alertInfo.eventnumber = 0;
        WeatherData.loadNextWeatherAlert();
    } else {
        WeatherData.alertInfo.initAlertInfo();
    }
}

static bool readExact(File& f, void* dst, size_t n) {
    uint8_t* p = (uint8_t*)dst;
    size_t got = 0;
    while (got < n) {
        int r = f.read(p + got, n - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

bool weatherLiteUnpackFile(const char* path) {
    if (!path) path = WEATHER_PKG_PATH;
    File f = SD.open(path, FILE_READ);
    if (!f) {
        weatherLiteFail("could not open package");
        storeError("weatherLiteUnpack: open failed", ERROR_SD_WEATHERDATAREAD, true);
        return false;
    }

    uint8_t verMajor = 0, verMinor = 0;
    uint16_t headerBytes = 0;
    if (!readExact(f, &verMajor, 1) || !readExact(f, &verMinor, 1) || !readExact(f, &headerBytes, 2)) {
        f.close();
        weatherLiteFail("package header short");
        return false;
    }
    if (verMajor > WEATHER_PKG_VER_MAJOR) {
        f.close();
        weatherLiteFail("package version unsupported");
        storeError("weatherLiteUnpack: incompatible major version", ERROR_SD_WEATHERDATAREAD, true);
        return false;
    }
    if (headerBytes < WEATHER_PKG_HEADER_CORE || headerBytes > 4096) {
        f.close();
        weatherLiteFail("package header invalid");
        return false;
    }

    // Header only (reserved size is 2× today's used layout). Never load package body into RAM.
    uint8_t header[WEATHER_PKG_HEADER_BYTES];
    if (headerBytes > sizeof(header) || headerBytes < WEATHER_PKG_HEADER_CORE) {
        f.close();
        weatherLiteFail("package header invalid");
        return false;
    }
    header[0] = verMajor;
    header[1] = verMinor;
    memcpy(header + 2, &headerBytes, 2);
    if (headerBytes > 4) {
        if (!readExact(f, header + 4, headerBytes - 4)) {
            f.close();
            weatherLiteFail("package header short");
            return false;
        }
    }

    uint32_t packagedAt = 0, totalSize = 0;
    uint16_t storeVer = 0, objSize = 0;
    memcpy(&packagedAt, header + 4, 4);
    memcpy(&totalSize, header + 8, 4);
    memcpy(&storeVer, header + 12, 2);
    memcpy(&objSize, header + 14, 2);
    uint8_t sectionCount = header[16];
    uint8_t flags = header[17];

    if (storeVer != WEATHER_STORE_VERSION || objSize != (uint16_t)sizeof(WeatherInfoOptimized)) {
        f.close();
        weatherLiteFail("weather data size mismatch");
        storeError("weatherLiteUnpack: WeatherData ABI mismatch", ERROR_SD_WEATHERDATAREAD, true);
        return false;
    }
    if (sectionCount == 0 || sectionCount > WEATHER_PKG_MAX_SECTIONS) {
        f.close();
        weatherLiteFail("package sections invalid");
        return false;
    }
    if (totalSize > WEATHER_PKG_MAX_BYTES || totalSize < headerBytes) {
        f.close();
        weatherLiteFail("package size invalid");
        return false;
    }

    WeatherPkgSectionEnt sections[WEATHER_PKG_MAX_SECTIONS];
    memcpy(sections, header + WEATHER_PKG_HEADER_CORE, sectionCount * sizeof(WeatherPkgSectionEnt));

    // Find weather data section
    int weatherIdx = -1;
    uint8_t eventIdxs[WEATHER_PKG_MAX_SECTIONS];
    uint8_t eventCount = 0;
    for (uint8_t i = 0; i < sectionCount; i++) {
        if (sections[i].type == WPKG_SEC_WEATHERDATA) weatherIdx = (int)i;
        else if (sections[i].type == WPKG_SEC_EVENT && eventCount < WEATHER_PKG_MAX_SECTIONS) {
            eventIdxs[eventCount++] = i;
        }
    }
    if (weatherIdx < 0) {
        f.close();
        weatherLiteFail("package missing weather");
        return false;
    }

    // Stream WeatherData object into memory
    if (!f.seek(sections[weatherIdx].start)) {
        f.close();
        weatherLiteFail("package seek failed");
        return false;
    }
    if (!readExact(f, &WeatherData, sizeof(WeatherInfoOptimized))) {
        f.close();
        weatherLiteFail("weather object short");
        return false;
    }

    const bool timesUtc = (flags & WPKG_FLAG_TIMES_UTC) != 0;
    WeatherData.normalizePackagedTimestampsToUtc(timesUtc);

    // SD.open does not create parent directories. A package with alerts fails
    // here when /Data/Events was never created on this device.
    if (!SD.exists("/Data")) SD.mkdir("/Data");
    if (!SD.exists("/Data/Events")) SD.mkdir("/Data/Events");
    deleteFiles("*", "/Data/Events");
    for (uint8_t e = 0; e < eventCount; e++) {
        uint8_t si = eventIdxs[e];
        uint32_t start = sections[si].start;
        uint32_t end = (si + 1 < sectionCount) ? sections[si + 1].start : totalSize;
        // If next section is not contiguous, find next higher start
        for (uint8_t j = 0; j < sectionCount; j++) {
            if (sections[j].start > start && sections[j].start < end) end = sections[j].start;
        }
        if (end <= start) continue;
        uint32_t len = end - start;
        if (!f.seek(start)) {
            f.close();
            weatherLiteFail("alert seek failed");
            return false;
        }
        char evPath[32];
        snprintf(evPath, sizeof(evPath), "/Data/Events/%u.txt", (unsigned)(e + 1));
        File ef = SD.open(evPath, FILE_WRITE);
        if (!ef) {
            f.close();
            weatherLiteFail("could not save alert");
            return false;
        }
        uint8_t buf[256];
        uint32_t left = len;
        while (left > 0) {
            uint32_t n = left > sizeof(buf) ? sizeof(buf) : left;
            if (!readExact(f, buf, n)) {
                ef.close();
                f.close();
                weatherLiteFail("alert read failed");
                return false;
            }
            if (ef.write(buf, n) != n) {
                ef.close();
                f.close();
                weatherLiteFail("alert write failed");
                return false;
            }
            left -= n;
        }
        ef.close();
    }

    f.close();

    // Persist core weather blob locally for reboot
    storeWeatherDataSD();

    WeatherLite.lastPackagePackagedAt = packagedAt;
    WeatherLite.lastPackageReceivedAt = isTimeValid((uint32_t)utcNow()) ? (uint32_t)utcNow() : packagedAt;
    WeatherLite.lastPackageMarkedStale = (flags & WPKG_FLAG_DATA_STALE) != 0;
    weatherLiteApplyIFlagsFromPackage();
    updateCurrentOutsideConditions();

    s_weatherLiteFail[0] = '\0';
    SerialPrint("weatherLiteUnpack: OK packagedAt=" + String(packagedAt) +
        " lastUpdateT=" + String(WeatherData.lastUpdateT) +
        " hourBase=" + String(WeatherData.getHourBase()) +
        " hourBaseLocal=" + String(dateifyLocal(WeatherData.getHourBase(), "mm/dd hh:nn")) +
        " timesUtcFlag=" + String(timesUtc ? 1 : 0) +
        " staleFlag=" + String(WeatherLite.lastPackageMarkedStale ? 1 : 0) +
        " events=" + String(eventCount), true);
    return true;
}

bool weatherLiteRequestFromServer(IPAddress ip) {
    if (!wifiReadyForNetwork()) {
        weatherLiteFail("Wi-Fi not ready");
        return false;
    }
    if (ip == IPAddress(0, 0, 0, 0)) {
        weatherLiteFail("weather hub has no IP");
        return false;
    }

    WeatherLite.lastRequestAttemptAt = isTimeValid((uint32_t)utcNow()) ? (uint32_t)utcNow() : WeatherLite.lastRequestAttemptAt;

    char url[64];
    snprintf(url, sizeof(url), "http://%s/WEATHERPKG", ip.toString().c_str());

    WiFiClient client;
    HTTPClient http;
    client.setTimeout(15000);
    if (!http.begin(client, url)) {
        weatherLiteFail("HTTP client failed");
        return false;
    }
    http.setTimeout(15000);
    esp_task_wdt_reset();
    int code = http.GET();
    esp_task_wdt_reset();
    if (code != 200) {
        http.end();
        char msg[80];
        if (code < 0) snprintf(msg, sizeof(msg), "could not reach %s", ip.toString().c_str());
        else snprintf(msg, sizeof(msg), "HTTP %d from %s", code, ip.toString().c_str());
        weatherLiteFail(msg);
        return false;
    }

    const int len = http.getSize();
    if (len == 0 || (len > 0 && (uint32_t)len > WEATHER_PKG_MAX_BYTES) || (len > 0 && (uint32_t)len < WEATHER_PKG_HEADER_CORE)) {
        http.end();
        weatherLiteFail(len == 0 ? "empty weather package" : "weather package size rejected");
        return false;
    }
    // HTTPClient::connected() stays true while unread bytes remain. WiFiClient::connected()
    // does not: after the hub sends Connection: close, the socket can already look dead
    // while the body is still in the receive buffer. The old loop sampled available()
    // once, waited 1 ms, then quit on !connected() with that stale count, so a 200
    // response was saved as an empty file.
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        weatherLiteFail("no HTTP body");
        return false;
    }

    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
    File out = SD.open(WEATHER_PKG_RECV_TMP_PATH, FILE_WRITE);
    if (!out) {
        http.end();
        weatherLiteFail("could not write package");
        return false;
    }

    uint8_t buf[512];
    uint32_t written = 0;
    const uint32_t expect = (len > 0) ? (uint32_t)len : 0;
    const uint32_t startMs = millis();
    uint32_t lastDataMs = startMs;
    bool sdWriteFailed = false;
    while (written < WEATHER_PKG_MAX_BYTES && (expect == 0 || written < expect)) {
        if (millis() - startMs > 30000) break;
        esp_task_wdt_reset();
        int avail = stream->available();
        if (avail <= 0) {
            if (http.connected() && millis() - lastDataMs < 15000) {
                delay(2);
                continue;
            }
            break;
        }
        size_t n = (size_t)avail > sizeof(buf) ? sizeof(buf) : (size_t)avail;
        if (expect > 0 && written + n > expect) n = (size_t)(expect - written);
        if (written + n > WEATHER_PKG_MAX_BYTES) n = (size_t)(WEATHER_PKG_MAX_BYTES - written);
        int r = stream->read(buf, n);
        if (r <= 0) {
            if (http.connected() && millis() - lastDataMs < 15000) {
                delay(2);
                continue;
            }
            break;
        }
        if (out.write(buf, (size_t)r) != (size_t)r) {
            sdWriteFailed = true;
            break;
        }
        written += (uint32_t)r;
        lastDataMs = millis();
    }
    out.close();
    http.end();

    if (sdWriteFailed) {
        sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
        weatherLiteFail("SD write failed");
        return false;
    }
    if ((expect > 0 && written != expect) || written < WEATHER_PKG_HEADER_CORE) {
        sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
        char msg[80];
        snprintf(msg, sizeof(msg), "short package %lu/%ld bytes", (unsigned long)written, (long)len);
        weatherLiteFail(msg);
        return false;
    }

    sdDeleteFile(WEATHER_PKG_PATH);
    File src = SD.open(WEATHER_PKG_RECV_TMP_PATH, FILE_READ);
    File dst = SD.open(WEATHER_PKG_PATH, FILE_WRITE);
    if (!src || !dst) {
        if (src) src.close();
        if (dst) dst.close();
        sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
        weatherLiteFail("could not save package");
        return false;
    }
    uint32_t copied = 0;
    bool copyFailed = false;
    while (src.available()) {
        int r = src.read(buf, sizeof(buf));
        if (r <= 0) break;
        if (dst.write(buf, (size_t)r) != (size_t)r) {
            copyFailed = true;
            break;
        }
        copied += (uint32_t)r;
    }
    src.close();
    dst.close();
    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
    if (copyFailed || copied != written) {
        sdDeleteFile(WEATHER_PKG_PATH);
        weatherLiteFail("package copy failed");
        return false;
    }

    return weatherLiteUnpackFile(WEATHER_PKG_PATH);
}

bool weatherLiteRequestFromAnyWeatherServer() {
    // Presence expiry is not reachability. A type-100 hub with a stale dataReceived
    // still serves GET /WEATHERPKG. Try live entries first, then expired ones.
    bool tried = false;
    for (int pass = 0; pass < 2; pass++) {
        int16_t idx = Sensors.nextServerIndex(0, true);
        while (idx >= 0) {
            ArborysDevType* d = Sensors.getDeviceByDevIndex(idx);
            const bool want = d && d->IsSet && d->IP != IPAddress(0, 0, 0, 0)
                && ((pass == 0 && !d->expired) || (pass == 1 && d->expired));
            if (want) {
                tried = true;
                if (weatherLiteRequestFromServer(d->IP)) return true;
            }
            idx = Sensors.nextServerIndex(idx + 1, true);
        }
    }
    if (!tried) weatherLiteFail("no type-100 weather server");
    return false;
}

void serviceWeatherLite(bool minuteTick) {
    if (!minuteTick) return;
    if (!wifiReadyForNetwork()) return;
    if (!isTimeValid((uint32_t)utcNow())) return;

    const uint32_t now = (uint32_t)utcNow();
    if (WeatherLite.lastRequestAttemptAt != 0 &&
        now < WeatherLite.lastRequestAttemptAt + WEATHER_LITE_MIN_REQUEST_SEC) {
        return; // rate limit (failed or still-stale producer packages)
    }

    // Freshness is NOAA lastUpdateT inside WeatherData (already packaged), not packagedAt.
    // packagedAt only reflects when the producer rebuilt the file and can be "fresh" with stale NOAA.
    bool need = false;
    const char* reason = nullptr;

    if (WeatherData.lastUpdateT == 0) {
        need = true;
        reason = "no lastUpdateT";
    } else if (now > WeatherData.lastUpdateT + WEATHER_LITE_STALE_SEC) {
        need = true;
        reason = "lastUpdateT age";
    } else if (WeatherLite.lastPackageMarkedStale) {
        need = true;
        reason = "producer stale flag";
    } else if (WeatherData.anyWeatherComponentStale()) {
        need = true;
        reason = "component stale";
    }

    if (!need) return;

    SerialPrint(String("weatherLite: requesting package (") + reason + ")", true);
    weatherLiteRequestFromAnyWeatherServer();
}

#endif
