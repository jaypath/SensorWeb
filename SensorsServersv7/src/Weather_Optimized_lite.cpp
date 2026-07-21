#include "globals.hpp"

#ifdef _USEWEATHERLITE

#include "Weather_Optimized_lite.hpp"
#include "Devices.hpp"
#include "utility.hpp"
#include "server.hpp"
#include <HTTPClient.h>

WeatherLiteState WeatherLite;

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
        storeError("weatherLiteUnpack: open failed", ERROR_SD_WEATHERDATAREAD, true);
        return false;
    }

    uint8_t verMajor = 0, verMinor = 0;
    uint16_t headerBytes = 0;
    if (!readExact(f, &verMajor, 1) || !readExact(f, &verMinor, 1) || !readExact(f, &headerBytes, 2)) {
        f.close();
        return false;
    }
    if (verMajor > WEATHER_PKG_VER_MAJOR) {
        f.close();
        storeError("weatherLiteUnpack: incompatible major version", ERROR_SD_WEATHERDATAREAD, true);
        return false;
    }
    if (headerBytes < WEATHER_PKG_HEADER_CORE || headerBytes > 4096) {
        f.close();
        return false;
    }

    // Header only (reserved size is 2× today's used layout). Never load package body into RAM.
    uint8_t header[WEATHER_PKG_HEADER_BYTES];
    if (headerBytes > sizeof(header) || headerBytes < WEATHER_PKG_HEADER_CORE) {
        f.close();
        return false;
    }
    header[0] = verMajor;
    header[1] = verMinor;
    memcpy(header + 2, &headerBytes, 2);
    if (headerBytes > 4) {
        if (!readExact(f, header + 4, headerBytes - 4)) {
            f.close();
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
        storeError("weatherLiteUnpack: WeatherData ABI mismatch", ERROR_SD_WEATHERDATAREAD, true);
        return false;
    }
    if (sectionCount == 0 || sectionCount > WEATHER_PKG_MAX_SECTIONS) {
        f.close();
        return false;
    }
    if (totalSize > WEATHER_PKG_MAX_BYTES || totalSize < headerBytes) {
        f.close();
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
        return false;
    }

    // Stream WeatherData object into memory
    if (!f.seek(sections[weatherIdx].start)) {
        f.close();
        return false;
    }
    if (!readExact(f, &WeatherData, sizeof(WeatherInfoOptimized))) {
        f.close();
        return false;
    }

    // Replace Events directory contents
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
            return false;
        }
        char evPath[32];
        snprintf(evPath, sizeof(evPath), "/Data/Events/%u.txt", (unsigned)(e + 1));
        File ef = SD.open(evPath, FILE_WRITE);
        if (!ef) {
            f.close();
            return false;
        }
        uint8_t buf[256];
        uint32_t left = len;
        while (left > 0) {
            uint32_t n = left > sizeof(buf) ? sizeof(buf) : left;
            if (!readExact(f, buf, n)) {
                ef.close();
                f.close();
                return false;
            }
            if (ef.write(buf, n) != n) {
                ef.close();
                f.close();
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
    WeatherLite.lastPackageReceivedAt = isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : packagedAt;
    WeatherLite.lastPackageMarkedStale = (flags & WPKG_FLAG_DATA_STALE) != 0;
    weatherLiteApplyIFlagsFromPackage();
    updateCurrentOutsideConditions();

    SerialPrint("weatherLiteUnpack: OK packagedAt=" + String(packagedAt) +
        " lastUpdateT=" + String(WeatherData.lastUpdateT) +
        " staleFlag=" + String(WeatherLite.lastPackageMarkedStale ? 1 : 0) +
        " events=" + String(eventCount), true);
    return true;
}

bool weatherLiteRequestFromServer(IPAddress ip) {
    if (!wifiReadyForNetwork()) return false;
    if (ip == IPAddress(0, 0, 0, 0)) return false;

    WeatherLite.lastRequestAttemptAt = isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : WeatherLite.lastRequestAttemptAt;

    char url[64];
    snprintf(url, sizeof(url), "http://%s/WEATHERPKG", ip.toString().c_str());

    WiFiClient client;
    HTTPClient http;
    client.setTimeout(15000);
    if (!http.begin(client, url)) return false;
    http.setTimeout(15000);
    esp_task_wdt_reset();
    int code = http.GET();
    esp_task_wdt_reset();
    if (code != 200) {
        http.end();
        SerialPrint("weatherLiteRequest: HTTP " + String(code) + " from " + ip.toString(), true);
        return false;
    }

    int len = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        return false;
    }

    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
    File out = SD.open(WEATHER_PKG_RECV_TMP_PATH, FILE_WRITE);
    if (!out) {
        http.end();
        return false;
    }

    uint8_t buf[512];
    uint32_t written = 0;
    const uint32_t maxBytes = WEATHER_PKG_MAX_BYTES;
    uint32_t startMs = millis();
    while ((len > 0 && written < (uint32_t)len) || (len < 0 && stream->available())) {
        if (millis() - startMs > 30000) break;
        size_t avail = stream->available();
        if (avail == 0) {
            delay(1);
            if (!stream->connected() && avail == 0) break;
            continue;
        }
        size_t n = avail > sizeof(buf) ? sizeof(buf) : avail;
        if (written + n > maxBytes) n = maxBytes - written;
        int r = stream->readBytes(buf, n);
        if (r <= 0) break;
        if (out.write(buf, (size_t)r) != (size_t)r) {
            out.close();
            http.end();
            sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
            return false;
        }
        written += (uint32_t)r;
        if (written >= maxBytes) break;
        esp_task_wdt_reset();
    }
    out.close();
    http.end();

    if (written < WEATHER_PKG_HEADER_CORE) {
        sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
        return false;
    }

    sdDeleteFile(WEATHER_PKG_PATH);
    // Move recv tmp -> package path via copy
    File src = SD.open(WEATHER_PKG_RECV_TMP_PATH, FILE_READ);
    File dst = SD.open(WEATHER_PKG_PATH, FILE_WRITE);
    if (!src || !dst) {
        if (src) src.close();
        if (dst) dst.close();
        sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
        return false;
    }
    while (src.available()) {
        int r = src.read(buf, sizeof(buf));
        if (r <= 0) break;
        dst.write(buf, (size_t)r);
    }
    src.close();
    dst.close();
    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);

    return weatherLiteUnpackFile(WEATHER_PKG_PATH);
}

bool weatherLiteRequestFromAnyWeatherServer() {
    int16_t idx = Sensors.nextServerIndex(0, true); // type 100 only
    while (idx >= 0) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(idx);
        if (d && d->IsSet && !d->expired && d->IP != IPAddress(0, 0, 0, 0)) {
            if (weatherLiteRequestFromServer(d->IP)) return true;
        }
        idx = Sensors.nextServerIndex(idx + 1, true);
    }
    return false;
}

void serviceWeatherLite(bool minuteTick) {
    if (!minuteTick) return;
    if (!wifiReadyForNetwork()) return;
    if (!isTimeValid(I.currentTime)) return;

    const uint32_t now = (uint32_t)I.currentTime;
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
