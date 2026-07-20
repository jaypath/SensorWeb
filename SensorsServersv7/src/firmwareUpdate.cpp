#include "firmwareUpdate.hpp"
#include "globals.hpp"
#include "utility.hpp"
#include "server.hpp"
#include "BootSecure.hpp"
#include "AddESPNOW.hpp"
#include <esp_app_format.h>
#include "esp_ota_ops.h"

static constexpr uint16_t FW_ENC_PLAIN_CHUNK = 3968;
static constexpr uint16_t FW_ENC_MAX_CIPHER = 4096;
static constexpr uint16_t FW_HTTP_ENC_MAX = 8192;
static constexpr uint16_t FW_HTTP_ENC_MAX_FRAMED = FW_HTTP_ENC_MAX + 2;
static constexpr uint16_t FW_HTTP_ENC_MAX_PADDED = ((FW_HTTP_ENC_MAX_FRAMED + 15) / 16) * 16;
static constexpr uint16_t FW_HTTP_ENC_MAX_CIPHER = FW_HTTP_ENC_MAX_PADDED + 16;

bool parseFirmwareFromJson(JsonVariantConst variant, FirmwareVersion& out) {
    if (!variant.is<JsonArrayConst>()) return false;
    JsonArrayConst arr = variant.as<JsonArrayConst>();
    if (arr.size() < 3) return false;
    for (uint8_t i = 0; i < 3; i++) {
        if (!arr[i].is<int>()) return false;
        int val = arr[i].as<int>();
        if (val < 0 || val > 255) return false;
        out.v[i] = (uint8_t)val;
    }
    return true;
}

static const char* firmwareBinBaseName(const char* filename) {
    if (!filename) return nullptr;
    const char* slash = strrchr(filename, '/');
    return slash ? slash + 1 : filename;
}

bool parseFirmwareDeviceAndVersionFromBinName(const char* filename, char* deviceOut, size_t deviceLen,
    FirmwareVersion& versionOut) {
    const char* base = firmwareBinBaseName(filename);
    if (!base) return false;

    size_t baseLen = strlen(base);
    if (baseLen < 8) return false; // min: "a-0.0.0.bin"
    if (strcasecmp(base + baseLen - 4, ".bin") != 0) return false;

    const char* dot = base + baseLen - 4;
    const char* lastHyphen = nullptr;
    for (const char* p = base; p < dot; ++p) {
        if (*p == '-') lastHyphen = p;
    }
    if (!lastHyphen || lastHyphen == base) return false;

    size_t nameLen = (size_t)(lastHyphen - base);
    if (nameLen == 0) return false;
    if (deviceOut) {
        if (nameLen >= deviceLen) return false;
        memcpy(deviceOut, base, nameLen);
        deviceOut[nameLen] = '\0';
    }

    char verText[20];
    size_t verLen = (size_t)(dot - lastHyphen - 1);
    if (verLen == 0 || verLen >= sizeof(verText)) return false;
    memcpy(verText, lastHyphen + 1, verLen);
    verText[verLen] = '\0';
    return versionOut.fromText(verText);
}

bool parseFirmwareVersionFromBinName(const char* filename, FirmwareVersion& out) {
    return parseFirmwareDeviceAndVersionFromBinName(filename, nullptr, 0, out);
}

bool buildFirmwareBinPath(const char* deviceName, const FirmwareVersion& version, char* out, size_t outLen) {
    if (!deviceName || !deviceName[0] || version.isUnset() || !out || outLen < 20) return false;
    char verText[16];
    version.toChar(verText, sizeof(verText));
    int n = snprintf(out, outLen, "/Firmware/%s-%s.bin", deviceName, verText);
    return n > 0 && (size_t)n < outLen;
}

uint16_t computeFileCRC(const char* filePath) {
    #ifndef _USESDCARD
    (void)filePath;
    return 0;
    #else
    if (!filePath || !SD.exists(filePath)) return 0;
    File f = SD.open(filePath, FILE_READ);
    if (!f) return 0;
    uint8_t sum1 = 0;
    uint8_t sum2 = 0;
    uint8_t buf[512];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            sum1 = (uint8_t)((sum1 + buf[i]) % 255);
            sum2 = (uint8_t)((sum2 + sum1) % 255);
        }
        esp_task_wdt_reset();
    }
    f.close();
    return (uint16_t)((sum2 << 8) | sum1);
    #endif
}

static void updateRunningCRC(uint16_t* crc, const uint8_t* data, uint16_t len) {
    if (!crc || !data || len == 0) return;
    uint8_t sum1 = (uint8_t)(*crc & 0xFF);
    uint8_t sum2 = (uint8_t)((*crc >> 8) & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        sum1 = (uint8_t)((sum1 + data[i]) % 255);
        sum2 = (uint8_t)((sum2 + sum1) % 255);
    }
    *crc = (uint16_t)((sum2 << 8) | sum1);
}

uint16_t computeBufferCRC(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    uint8_t sum1 = 0;
    uint8_t sum2 = 0;
    for (size_t i = 0; i < len; i++) {
        sum1 = (uint8_t)((sum1 + data[i]) % 255);
        sum2 = (uint8_t)((sum2 + sum1) % 255);
    }
    return (uint16_t)((sum2 << 8) | sum1);
}

static void logFirmwareTransferError(const String& msg) {
    SerialPrint(msg, true);
    storeError(msg, ERROR_UNDEFINED, true);
}

static void logOtaEndError(const char* context, esp_err_t err) {
    if (err == ESP_OK) return;
    logFirmwareTransferError(String(context) + ": esp_ota_end failed: " + String(esp_err_to_name(err)));
}

static void logOtaBeginError(const char* context, esp_err_t err,
    const esp_partition_t* partition, size_t imageSize) {
    if (err == ESP_OK) return;
    String msg = String(context) + ": esp_ota_begin failed: " + String(esp_err_to_name(err));
    if (partition) {
        msg += " partition=" + String(partition->label)
            + " max=" + String((unsigned)partition->size) + "B";
    }
    if (imageSize > 0) {
        msg += " image=" + String((unsigned)imageSize) + "B";
    }
    logFirmwareTransferError(msg);
}

#ifdef _USESDCARD
static bool readEmbeddedFirmwareVersionFromBinFile(const char* filePath, FirmwareVersion& out) {
    if (!filePath || !SD.exists(filePath)) return false;

    File f = SD.open(filePath, FILE_READ);
    if (!f) return false;

    constexpr size_t kAppDescOffset = 0x20;
    if (f.size() < (int)(kAppDescOffset + sizeof(esp_app_desc_t))) {
        f.close();
        return false;
    }
    if (!f.seek(kAppDescOffset)) {
        f.close();
        return false;
    }

    esp_app_desc_t desc = {};
    if (f.read((uint8_t*)&desc, sizeof(desc)) != sizeof(desc)) {
        f.close();
        return false;
    }
    f.close();

    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) return false;
    return out.fromText(desc.version);
}

static bool sdFirmwareFileNameMatchesEmbedded(const char* filePath, const FirmwareVersion& nameVersion) {
    FirmwareVersion embedded;
    if (!readEmbeddedFirmwareVersionFromBinFile(filePath, embedded)) {
        SerialPrint("SD firmware: could not read embedded version from " + String(filePath), true, 5);
        return false;
    }

    if (nameVersion.compare(embedded) != 0) {
        char nameBuf[16], embeddedBuf[16];
        nameVersion.toChar(nameBuf, sizeof(nameBuf));
        embedded.toChar(embeddedBuf, sizeof(embeddedBuf));
        SerialPrint(String("SD firmware: filename/version mismatch for ") + filePath
            + " (name " + nameBuf + ", embedded " + embeddedBuf + ")", true);
        return false;
    }
    return true;
}

bool findHighestSDFirmwareForDevice(const char* deviceName, FirmwareVersion& versionOut,
    char* filePathOut, size_t pathLen, uint16_t* crcOut, uint32_t* sizeOut) {
    #ifndef _USESDCARD
    (void)deviceName; (void)versionOut; (void)filePathOut; (void)pathLen; (void)crcOut; (void)sizeOut;
    return false;
    #else
    if (!deviceName || !deviceName[0]) return false;
    static const char* kFirmwareDir = "/Firmware";
    if (!SD.exists(kFirmwareDir)) return false;

    File dir = SD.open(kFirmwareDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }

    FirmwareVersion bestVersion;
    bestVersion.clear();
    char bestPath[96] = "";
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            char parsedDevice[64];
            FirmwareVersion ver;
            if (!parseFirmwareDeviceAndVersionFromBinName(entry.name(), parsedDevice, sizeof(parsedDevice), ver)) {
                entry.close();
                entry = dir.openNextFile();
                continue;
            }
            if (strcmp(parsedDevice, deviceName) != 0) {
                entry.close();
                entry = dir.openNextFile();
                continue;
            }

            char candidatePath[96];
            snprintf(candidatePath, sizeof(candidatePath), "%s/%s", kFirmwareDir, entry.name());
            if (!sdFirmwareFileNameMatchesEmbedded(candidatePath, ver)) {
                entry.close();
                entry = dir.openNextFile();
                continue;
            }

            if (bestVersion.isUnset() || ver.compare(bestVersion) > 0) {
                bestVersion = ver;
                strncpy(bestPath, candidatePath, sizeof(bestPath) - 1);
                bestPath[sizeof(bestPath) - 1] = '\0';
            }
        }
        entry.close();
        entry = dir.openNextFile();
        esp_task_wdt_reset();
    }
    dir.close();

    if (bestVersion.isUnset()) return false;
    versionOut = bestVersion;

    //serial print the best version and the current version
    char bestVerBuf[16], curVerBuf[16];
    bestVersion.toChar(bestVerBuf, sizeof(bestVerBuf));
    FirmwareVersion currentFw;
    getLocalFirmware(currentFw);
    currentFw.toChar(curVerBuf, sizeof(curVerBuf));
    SerialPrint("Best version: " + String(bestVerBuf) + " Current version: " + String(curVerBuf), true);
    
    if (filePathOut && pathLen) {
        strncpy(filePathOut, bestPath, pathLen - 1);
        filePathOut[pathLen - 1] = '\0';
    }
    if (crcOut) *crcOut = computeFileCRC(bestPath);
    if (sizeOut) {
        File f = SD.open(bestPath, FILE_READ);
        *sizeOut = f ? (uint32_t)f.size() : 0;
        if (f) f.close();
    }

    return true;
    #endif
}

static bool writeFirmwareFileToOTASlot(const char* filePath, const FirmwareVersion& newVersion) {
    if (!filePath || newVersion.isUnset()) return false;
    File firmware = SD.open(filePath, FILE_READ);
    if (!firmware) {
        logFirmwareTransferError("SD firmware: failed to open " + String(filePath));
        return false;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        storeError("SD firmware: no OTA update partition available", ERROR_UNDEFINED, true);
        firmware.close();
        return false;
    }

    const size_t imageSize = firmware.size();
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, imageSize, &ota_handle);
    if (err != ESP_OK) {
        logOtaBeginError("SD firmware", err, update_partition, imageSize);
        firmware.close();
        return false;
    }

    uint8_t buf[1024];
    while (firmware.available()) {
        size_t n = firmware.read(buf, sizeof(buf));
        if (n == 0) break;
        err = esp_ota_write(ota_handle, buf, n);
        if (err != ESP_OK) {
            logFirmwareTransferError("SD firmware: esp_ota_write failed");
            logOtaEndError("SD firmware cleanup", esp_ota_end(ota_handle));
            firmware.close();
            return false;
        }
        esp_task_wdt_reset();
    }
    firmware.close();

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        logOtaEndError("SD firmware", err);
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        logFirmwareTransferError("SD firmware: set boot partition failed");
        return false;
    }

    FirmwareVersion installedFw = newVersion;
    esp_app_desc_t installedDesc = {};
    if (esp_ota_get_partition_description(update_partition, &installedDesc) == ESP_OK) {
        FirmwareVersion embeddedFw;
        if (embeddedFw.fromText(installedDesc.version)) {
            installedFw = embeddedFw;
        }
    }
    Prefs.FIRMWARE = installedFw;
    Prefs.isUpToDate = false;
    BootSecure bootSecure;
    bootSecure.setPrefs(true);
    Sensors.updateMyDeviceVersion();
    char verText[16];
    installedFw.toChar(verText, sizeof(verText));
    SerialPrint("SD firmware: installed " + String(verText) + ", rebooting", true);
    logSystemEvent("FW transfer OK v" + String(verText), EVENT_FIRMWARE_UPDATED);

    controlledReboot("SD firmware update", RESET_OTA, true);
    return true;
}
#endif

bool checkAndApplySDFirmwareOnBoot() {
    #if !defined(_USESDCARD) || defined(_USELOWPOWER)
    return false;
    #else
    if (Prefs.DEVICENAME[0] == '\0') return false;

    FirmwareVersion version;
    char path[96];
    if (!findHighestSDFirmwareForDevice(Prefs.DEVICENAME, version, path, sizeof(path), nullptr, nullptr)) {
        return false;
    }
    FirmwareVersion localFw;
    getLocalFirmware(localFw);
    if (version.compare(localFw) <= 0) {
        return false;
    }

    if (!sdFirmwareFileNameMatchesEmbedded(path, version)) {
        storeError("SD firmware rejected: filename does not match embedded version at " + String(path),
            ERROR_UNDEFINED, true);
        return false;
    }

    char verText[16];
    version.toChar(verText, sizeof(verText));
    SerialPrint("SD firmware: found newer version " + String(verText) + " at " + String(path), true);
    #ifdef _USETFT
    tftPrint("New firmware version " + String(verText) + " found, flashing...", true);
    #endif
    return writeFirmwareFileToOTASlot(path, version);
    #endif
}

static bool encryptHttpPayload(const uint8_t* payload, uint16_t payloadLen, uint8_t* encBuf, uint16_t* encLen) {
    if (!payload || payloadLen == 0 || payloadLen > FW_HTTP_ENC_MAX || !encBuf || !encLen) return false;
    if (!isValidLMKKey()) return false;

    const uint16_t framedLen = payloadLen + 2;
    uint8_t framed[FW_HTTP_ENC_MAX_FRAMED];
    framed[0] = payloadLen & 0xFF;
    framed[1] = (payloadLen >> 8) & 0xFF;
    memcpy(framed + 2, payload, payloadLen);
    return BootSecure::encrypt(framed, framedLen, (char*)Prefs.KEYS.ESPNOW_KEY, encBuf, encLen, 16) == 1;
}

static bool decryptHttpPayload(uint8_t* encBuf, uint16_t encLen, uint8_t* plain, uint16_t plainMax, uint16_t* payloadLenOut) {
    if (!encBuf || encLen < 32 || encLen > FW_HTTP_ENC_MAX_CIPHER || (encLen % 16) != 0) return false;
    if (!isValidLMKKey()) return false;
    if (BootSecure::decrypt(encBuf, (char*)Prefs.KEYS.ESPNOW_KEY, plain, encLen, 16) != 1) return false;
    uint16_t payloadLen = (uint16_t)plain[0] | ((uint16_t)plain[1] << 8);
    if (payloadLen == 0 || payloadLen + 2 > plainMax) return false;
    if (payloadLenOut) *payloadLenOut = payloadLen;
    return true;
}

static bool encryptStreamFrame(const uint8_t* data, uint16_t dataLen, uint8_t* encBuf, uint16_t* encLen) {
    if (!encBuf || !encLen) return false;
    uint8_t framed[FW_ENC_PLAIN_CHUNK + 2];
    framed[0] = dataLen & 0xFF;
    framed[1] = (dataLen >> 8) & 0xFF;
    if (dataLen > 0 && data) memcpy(framed + 2, data, dataLen);
    return BootSecure::encrypt(framed, dataLen + 2, (char*)Prefs.KEYS.ESPNOW_KEY, encBuf, encLen, 16) == 1;
}

static bool writeEncryptedStreamFrame(WiFiClient& client, const uint8_t* data, uint16_t dataLen) {
    uint8_t encBuf[FW_ENC_MAX_CIPHER];
    uint16_t encLen = 0;
    if (!encryptStreamFrame(data, dataLen, encBuf, &encLen)) return false;
    esp_task_wdt_reset();
    uint8_t hdr[2] = {(uint8_t)(encLen & 0xFF), (uint8_t)((encLen >> 8) & 0xFF)};
    if (client.write(hdr, 2) != 2) return false;
    esp_task_wdt_reset();
    if (client.write(encBuf, encLen) != encLen) return false;
    esp_task_wdt_reset();
    return true;
}

static bool streamFirmwareFileEncrypted(WiFiClient& client, const char* filePath) {
    File firmware = SD.open(filePath, FILE_READ);
    if (!firmware) return false;
    uint8_t buf[FW_ENC_PLAIN_CHUNK];
    while (firmware.available()) {
        uint16_t n = (uint16_t)firmware.read(buf, sizeof(buf));
        if (n == 0) break;
        if (!writeEncryptedStreamFrame(client, buf, n)) {
            firmware.close();
            return false;
        }
        esp_task_wdt_reset();
    }
    firmware.close();
    uint8_t eofHdr[2] = {0, 0};
    return client.write(eofHdr, 2) == 2;
}

static bool readExact(WiFiClient& client, uint8_t* buf, size_t len, uint32_t timeoutMs) {
    size_t got = 0;
    uint32_t start = millis();
    while (got < len) {
        if (client.available()) {
            int n = client.read(buf + got, len - got);
            if (n > 0) got += (size_t)n;
        } else if ((millis() - start) > timeoutMs) {
            return false;
        } else {
            delay(1);
        }
    }
    return true;
}

static bool skipHttpHeaders(WiFiClient& client, uint32_t timeoutMs) {
    String line;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        while (client.available()) {
            char c = (char)client.read();
            if (c == '\n') {
                line.trim();
                if (line.length() == 0) return true;
                line = "";
            } else if (c != '\r') {
                line += c;
            }
        }
        delay(1);
    }
    return false;
}

struct FwBlockHttpMeta {
    int httpCode = 0;
    uint32_t blockIndex = 0;
    uint32_t blockLength = 0;
    uint16_t blockCrc = 0;
    uint32_t firmwareSize = 0;
    uint32_t totalBlocks = 0;
};

static bool readHttpLine(WiFiClient& client, String& line, uint32_t timeoutMs) {
    line = "";
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        while (client.available()) {
            char c = (char)client.read();
            if (c == '\n') {
                line.trim();
                return true;
            }
            if (c != '\r') line += c;
        }
        delay(1);
        esp_task_wdt_reset();
    }
    return false;
}

static bool parseFwBlockHttpResponse(WiFiClient& client, FwBlockHttpMeta& meta, uint8_t* body,
    size_t bodyCap, uint32_t timeoutMs) {
    memset(&meta, 0, sizeof(meta));
    String line;
    if (!readHttpLine(client, line, timeoutMs)) return false;
    if (line.indexOf("200") < 0) {
        if (line.startsWith("HTTP/")) {
            int sp = line.indexOf(' ');
            if (sp > 0) meta.httpCode = line.substring(sp + 1).toInt();
        }
        return false;
    }
    meta.httpCode = 200;
    uint32_t contentLength = 0;
    while (true) {
        if (!readHttpLine(client, line, timeoutMs)) return false;
        if (line.length() == 0) break;
        if (line.startsWith("Content-Length:")) {
            contentLength = (uint32_t)line.substring(15).toInt();
        } else if (line.startsWith("X-Block-Index:")) {
            meta.blockIndex = (uint32_t)line.substring(14).toInt();
        } else if (line.startsWith("X-Block-Length:")) {
            meta.blockLength = (uint32_t)line.substring(15).toInt();
        } else if (line.startsWith("X-Block-Crc:")) {
            meta.blockCrc = (uint16_t)line.substring(12).toInt();
        } else if (line.startsWith("X-Firmware-Size:")) {
            meta.firmwareSize = (uint32_t)line.substring(16).toInt();
        } else if (line.startsWith("X-Total-Blocks:")) {
            meta.totalBlocks = (uint32_t)line.substring(15).toInt();
        }
    }
    if (meta.blockLength == 0 && contentLength > 0) meta.blockLength = contentLength;
    if (meta.blockLength == 0 || meta.blockLength > bodyCap) return false;
    return readExact(client, body, meta.blockLength, timeoutMs);
}

#if defined(_USESDCARD) && _IS_SERVER_HUB
#define FW_SERVER_TRACK_SLOTS 12

struct ServerFwChunkTrack {
    char deviceName[30];
    FirmwareVersion version;
    time_t lastActivity;
    uint8_t nextRequestMinutes;
};

static ServerFwChunkTrack s_serverFwTracks[FW_SERVER_TRACK_SLOTS];

static int findServerFwTrackSlot(const char* deviceName, bool alloc) {
    int empty = -1;
    for (int i = 0; i < FW_SERVER_TRACK_SLOTS; i++) {
        if (s_serverFwTracks[i].deviceName[0] == '\0') {
            if (empty < 0) empty = i;
            continue;
        }
        if (strcmp(s_serverFwTracks[i].deviceName, deviceName) == 0) return i;
    }
    if (!alloc) return -1;
    int slot = empty >= 0 ? empty : 0;
    memset(&s_serverFwTracks[slot], 0, sizeof(s_serverFwTracks[slot]));
    strncpy(s_serverFwTracks[slot].deviceName, deviceName, sizeof(s_serverFwTracks[slot].deviceName) - 1);
    return slot;
}

static void touchServerFwTrack(const char* deviceName, const FirmwareVersion& version, uint8_t nextRequestMinutes) {
    int slot = findServerFwTrackSlot(deviceName, true);
    if (slot < 0) return;
    s_serverFwTracks[slot].version = version;
    s_serverFwTracks[slot].nextRequestMinutes = nextRequestMinutes;
    if (isTimeValid(I.currentTime)) s_serverFwTracks[slot].lastActivity = I.currentTime;
}

static void logFwServerBlockSent(const char* deviceName, uint32_t blockIndex, uint32_t totalBlocks) {
    char msg[96];
    snprintf(msg, sizeof(msg), "FWreq: %s,  %lu/%lu",
        deviceName, (unsigned long)blockIndex, (unsigned long)totalBlocks);
    logSystemEvent(msg, EVENT_FIRMWARE_UPDATED);
}

static bool serverShouldIgnoreFirmwareDiscovery(const char* deviceName) {
    int slot = findServerFwTrackSlot(deviceName, false);
    if (slot < 0) return false;
    if (!isTimeValid(I.currentTime) || !isTimeValid(s_serverFwTracks[slot].lastActivity)) return false;
    time_t ignoreSec = (time_t)s_serverFwTracks[slot].nextRequestMinutes
        * FW_CHUNK_DISCOVERY_IGNORE_MULTIPLIER * 60;
    return (I.currentTime - s_serverFwTracks[slot].lastActivity) < ignoreSec;
}

static bool serverFwVersionConflict(const char* deviceName, const FirmwareVersion& version) {
    int slot = findServerFwTrackSlot(deviceName, false);
    if (slot < 0) return false;
    if (!isTimeValid(I.currentTime) || !isTimeValid(s_serverFwTracks[slot].lastActivity)) return false;
    time_t activeSec = (time_t)FW_CHUNK_SESSION_MAX_SEC;
    if ((I.currentTime - s_serverFwTracks[slot].lastActivity) > activeSec) return false;
    return s_serverFwTracks[slot].version.compare(version) != 0;
}

void handleFirmwareBlock() {
    registerHTTPMessage("fwBlk");

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing body");
        return;
    }

    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }
    if (!doc["msgType"].is<const char*>() || strcmp(doc["msgType"], "FirmwareBlockRequest") != 0) {
        server.send(400, "text/plain", "Invalid msgType");
        return;
    }

    const char* targetDevice = doc["targetDeviceName"] | "";
    const char* senderDevice = doc["senderDeviceName"] | "";
    if (targetDevice[0] == '\0' || senderDevice[0] == '\0') {
        server.send(400, "text/plain", "Missing device name");
        return;
    }

    FirmwareVersion version;
    if (!parseFirmwareFromJson(doc["firmware"], version)) {
        server.send(400, "text/plain", "Missing firmware version");
        return;
    }

    uint32_t blockIndex = doc["blockIndex"] | 0;
    uint32_t blockSize = doc["blockSize"] | (uint32_t)FW_CHUNK_BLOCK_SIZE;
    uint8_t nextRequestMinutes = doc["nextRequestMinutes"] | (uint8_t)FW_CHUNK_NEXT_REQUEST_MINUTES;
    if (blockSize == 0 || blockSize > FW_CHUNK_BLOCK_SIZE) {
        server.send(400, "text/plain", "Invalid block size");
        return;
    }

    if (serverFwVersionConflict(senderDevice, version)) {
        server.send(409, "text/plain", "Firmware version conflict");
        return;
    }

    char path[96];
    if (!buildFirmwareBinPath(targetDevice, version, path, sizeof(path)) || !SD.exists(path)) {
        server.send(404, "text/plain", "Firmware not found");
        return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        server.send(500, "text/plain", "Open failed");
        return;
    }

    const uint32_t fileSize = (uint32_t)f.size();
    const uint32_t totalBlocks = (fileSize + blockSize - 1) / blockSize;
    if (blockIndex >= totalBlocks) {
        f.close();
        server.send(400, "text/plain", "Block index out of range");
        return;
    }

    const uint32_t offset = blockIndex * blockSize;
    const uint32_t toRead = (offset + blockSize > fileSize) ? (fileSize - offset) : blockSize;
    if (!f.seek(offset)) {
        f.close();
        server.send(500, "text/plain", "Seek failed");
        return;
    }

    uint8_t* buf = (uint8_t*)malloc(toRead);
    if (!buf) {
        f.close();
        server.send(500, "text/plain", "Out of memory");
        return;
    }
    size_t got = f.read(buf, toRead);
    f.close();
    if (got != toRead) {
        free(buf);
        server.send(500, "text/plain", "Read failed");
        return;
    }

    const uint16_t blockCrc = computeBufferCRC(buf, got);
    touchServerFwTrack(senderDevice, version, nextRequestMinutes);

    server.sendHeader("Connection", "close");
    server.sendHeader("X-Block-Index", String(blockIndex));
    server.sendHeader("X-Block-Length", String((unsigned)got));
    server.sendHeader("X-Block-Crc", String(blockCrc));
    if (blockIndex == 0) {
        server.sendHeader("X-Firmware-Size", String(fileSize));
        server.sendHeader("X-Total-Blocks", String(totalBlocks));
    }
    server.setContentLength(got);
    server.send(200, "application/octet-stream", "");
    WiFiClient client = server.client();
    if (client) {
        size_t sent = 0;
        while (sent < got) {
            int n = client.write(buf + sent, got - sent);
            if (n <= 0) break;
            sent += (size_t)n;
            esp_task_wdt_reset();
        }
        if (sent == got) {
            logFwServerBlockSent(senderDevice, blockIndex, totalBlocks);
        }
    }
    free(buf);
}
#else
void handleFirmwareBlock() {
    server.send(404, "text/plain", "Not available");
}
#endif

#if _HAS_LOCAL_SENSORS
static uint8_t fwCheckMinute = 255;
static uint8_t fwCheckHourDone = 255;
static bool fwResponseReceived = false;

struct FwChunkSession {
    bool active = false;
    IPAddress serverIP;
    FirmwareVersion version;
    uint32_t expectedSize = 0;
    uint16_t expectedFullCrc = 0;
    uint32_t totalBlocks = 0;
    uint32_t currentBlockIndex = 0;
    uint16_t runningFullCrc = 0;
    esp_ota_handle_t otaHandle = 0;
    const esp_partition_t* updatePartition = nullptr;
    time_t sessionStartTime = 0;
    time_t lastSuccessTime = 0;
    time_t nextRequestDue = 0;
    bool inRetryMode = false;
};

static FwChunkSession s_chunk;

static void abortChunkSession(const char* errMsg) {
    if (s_chunk.otaHandle) {
        esp_ota_abort(s_chunk.otaHandle);
    }
    memset(&s_chunk, 0, sizeof(s_chunk));
    fwResponseReceived = false;
    if (errMsg && errMsg[0]) {
        storeError(errMsg, ERROR_UNDEFINED, true);
        logSystemEvent(String(errMsg), EVENT_FIRMWARE_UPDATED);
    }
}

static uint32_t chunkStallTimeoutSec() {
    if (s_chunk.totalBlocks == 0) return 3600;
    return s_chunk.totalBlocks * FW_CHUNK_NEXT_REQUEST_MINUTES * 60 * FW_CHUNK_TIMEOUT_MULTIPLIER;
}

static void scheduleChunkAfterSuccess() {
    if (!isTimeValid(I.currentTime)) return;
    s_chunk.inRetryMode = false;
    s_chunk.nextRequestDue = I.currentTime + (time_t)(FW_CHUNK_NEXT_REQUEST_MINUTES * 60);
}

static void scheduleChunkRetry() {
    if (!isTimeValid(I.currentTime)) return;
    s_chunk.inRetryMode = true;
    s_chunk.nextRequestDue = I.currentTime + (time_t)(FW_CHUNK_RETRY_MINUTES * 60);
}

static bool finalizeChunkSession() {
    if (s_chunk.runningFullCrc != s_chunk.expectedFullCrc) {
        abortChunkSession("FW chunk image CRC fail");
        return false;
    }
    esp_err_t endErr = esp_ota_end(s_chunk.otaHandle);
    s_chunk.otaHandle = 0;
    if (endErr != ESP_OK) {
        logOtaEndError("FW chunk", endErr);
        abortChunkSession("FW chunk ota_end fail");
        return false;
    }
    if (esp_ota_set_boot_partition(s_chunk.updatePartition) != ESP_OK) {
        abortChunkSession("FW chunk boot part fail");
        return false;
    }

    FirmwareVersion installedFw = s_chunk.version;
    esp_app_desc_t installedDesc = {};
    if (esp_ota_get_partition_description(s_chunk.updatePartition, &installedDesc) == ESP_OK) {
        FirmwareVersion embeddedFw;
        if (embeddedFw.fromText(installedDesc.version)) installedFw = embeddedFw;
    }
    Prefs.FIRMWARE = installedFw;
    Prefs.isUpToDate = false;
    BootSecure bootSecure;
    bootSecure.setPrefs(true);
    Sensors.updateMyDeviceVersion();

    char verText[16];
    installedFw.toChar(verText, sizeof(verText));
    SerialPrint("FW chunk installed " + String(verText) + ", rebooting", true);
    logSystemEvent("FW chunk OK v" + String(verText), EVENT_FIRMWARE_UPDATED);
    memset(&s_chunk, 0, sizeof(s_chunk));
    controlledReboot("Network firmware update", RESET_OTA, true);
    return true;
}

static bool startChunkSession(IPAddress serverIP, const FirmwareVersion& version,
    uint16_t expectedCrc, uint32_t expectedSize) {
    if (expectedSize == 0 || version.isUnset()) return false;

    const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        storeError("FW chunk: no OTA partition", ERROR_UNDEFINED, true);
        return false;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t beginErr = esp_ota_begin(part, expectedSize, &handle);
    if (beginErr != ESP_OK) {
        logOtaBeginError("FW chunk", beginErr, part, expectedSize);
        return false;
    }

    memset(&s_chunk, 0, sizeof(s_chunk));
    s_chunk.active = true;
    s_chunk.serverIP = serverIP;
    s_chunk.version = version;
    s_chunk.expectedSize = expectedSize;
    s_chunk.expectedFullCrc = expectedCrc;
    s_chunk.totalBlocks = (expectedSize + FW_CHUNK_BLOCK_SIZE - 1) / FW_CHUNK_BLOCK_SIZE;
    s_chunk.currentBlockIndex = 0;
    s_chunk.otaHandle = handle;
    s_chunk.updatePartition = part;
    if (isTimeValid(I.currentTime)) {
        s_chunk.sessionStartTime = I.currentTime;
        s_chunk.lastSuccessTime = I.currentTime;
        s_chunk.nextRequestDue = I.currentTime + (time_t)(FW_CHUNK_NEXT_REQUEST_MINUTES * 60);
    }

    char verText[16];
    version.toChar(verText, sizeof(verText));
    logSystemEvent(String("FW chunk start v") + verText + " from " + serverIP.toString(), EVENT_FIRMWARE_UPDATED);
    return true;
}

static bool requestOneFirmwareBlock() {
    if (!s_chunk.active || !wifiReadyForNetwork()) return false;
    if (!isTimeValid(I.currentTime)) return false;

    char json[320];
    snprintf(json, sizeof(json),
        "{\"msgType\":\"FirmwareBlockRequest\",\"targetDeviceName\":\"%s\","
        "\"senderDeviceName\":\"%s\",\"senderIP\":\"%s\",\"firmware\":%s,"
        "\"blockIndex\":%lu,\"blockSize\":%u,\"nextRequestMinutes\":%u}",
        Prefs.DEVICENAME, Prefs.DEVICENAME, WiFi.localIP().toString().c_str(),
        firmwareJsonArray(s_chunk.version).c_str(),
        (unsigned long)s_chunk.currentBlockIndex, (unsigned)FW_CHUNK_BLOCK_SIZE,
        (unsigned)FW_CHUNK_NEXT_REQUEST_MINUTES);

    {
        char reqMsg[96];
        char verText[16];
        s_chunk.version.toChar(verText, sizeof(verText));
        snprintf(reqMsg, sizeof(reqMsg), "FW req block %lu/%lu v%s to %s",
            (unsigned long)s_chunk.currentBlockIndex, (unsigned long)s_chunk.totalBlocks,
            verText, s_chunk.serverIP.toString().c_str());
        logSystemEvent(reqMsg, EVENT_FIRMWARE_UPDATED);
    }

    WiFiClient client;
    client.setTimeout(20000);
    if (!client.connect(s_chunk.serverIP, 80)) {
        char err[72];
        snprintf(err, sizeof(err), "FW chunk no conn blk %lu srv %s",
            (unsigned long)s_chunk.currentBlockIndex, s_chunk.serverIP.toString().c_str());
        storeError(err, ERROR_UNDEFINED, false);
        scheduleChunkRetry();
        return false;
    }

    String req = String("POST /FIRMWARE_BLOCK HTTP/1.1\r\nHost: ")
        + s_chunk.serverIP.toString()
        + "\r\nContent-Type: application/json\r\nContent-Length: "
        + String(strlen(json)) + "\r\nConnection: close\r\n\r\n" + json;
    client.print(req);
    esp_task_wdt_reset();

    uint8_t* body = (uint8_t*)malloc(FW_CHUNK_BLOCK_SIZE);
    if (!body) {
        client.stop();
        scheduleChunkRetry();
        return false;
    }

    FwBlockHttpMeta meta;
    bool ok = parseFwBlockHttpResponse(client, meta, body, FW_CHUNK_BLOCK_SIZE, 20000);
    client.stop();

    if (!ok || meta.httpCode != 200) {
        free(body);
        char err[72];
        snprintf(err, sizeof(err), "FW chunk HTTP fail blk %lu code %d",
            (unsigned long)s_chunk.currentBlockIndex, meta.httpCode);
        storeError(err, ERROR_UNDEFINED, false);
        scheduleChunkRetry();
        return false;
    }

    if (meta.blockIndex != s_chunk.currentBlockIndex) {
        free(body);
        scheduleChunkRetry();
        return false;
    }
    if (meta.blockLength == 0 || meta.blockLength > FW_CHUNK_BLOCK_SIZE) {
        free(body);
        scheduleChunkRetry();
        return false;
    }
    if (meta.blockIndex == 0) {
        if (meta.firmwareSize > 0 && meta.firmwareSize != s_chunk.expectedSize) {
            free(body);
            abortChunkSession("FW chunk size mismatch");
            return false;
        }
        if (meta.totalBlocks > 0 && meta.totalBlocks != s_chunk.totalBlocks) {
            free(body);
            abortChunkSession("FW chunk blocks mismatch");
            return false;
        }
    }

    const uint16_t gotCrc = computeBufferCRC(body, meta.blockLength);
    if (gotCrc != meta.blockCrc) {
        free(body);
        scheduleChunkRetry();
        return false;
    }

    const uint32_t offset = s_chunk.currentBlockIndex * FW_CHUNK_BLOCK_SIZE;
    if (esp_ota_write_with_offset(s_chunk.otaHandle, body, meta.blockLength, offset) != ESP_OK) {
        free(body);
        abortChunkSession("FW chunk ota_write fail");
        return false;
    }
    updateRunningCRC(&s_chunk.runningFullCrc, body, (uint16_t)meta.blockLength);
    free(body);
    s_chunk.currentBlockIndex++;
    s_chunk.lastSuccessTime = I.currentTime;

    char progressMsg[96];
    char verText[16];
    s_chunk.version.toChar(verText, sizeof(verText));
    snprintf(progressMsg, sizeof(progressMsg), "FW recv block %lu/%lu v%s from %s",
        (unsigned long)s_chunk.currentBlockIndex, (unsigned long)s_chunk.totalBlocks,
        verText, s_chunk.serverIP.toString().c_str());
    SerialPrint(String(progressMsg), true);
    logSystemEvent(progressMsg, EVENT_FIRMWARE_UPDATED);

    if (s_chunk.currentBlockIndex >= s_chunk.totalBlocks) {
        return finalizeChunkSession();
    }
    scheduleChunkAfterSuccess();
    return true;
}

void processChunkFirmwareTick() {
    if (!s_chunk.active) return;
    if (!wifiReadyForNetwork() || !isTimeValid(I.currentTime)) return;

    const time_t now = I.currentTime;
    if ((now - s_chunk.sessionStartTime) > (time_t)FW_CHUNK_SESSION_MAX_SEC) {
        char err[72];
        snprintf(err, sizeof(err), "FW chunk session max age blk %lu/%lu",
            (unsigned long)s_chunk.currentBlockIndex, (unsigned long)s_chunk.totalBlocks);
        abortChunkSession(err);
        return;
    }
    if ((now - s_chunk.lastSuccessTime) > (time_t)chunkStallTimeoutSec()) {
        char err[72];
        snprintf(err, sizeof(err), "FW chunk timeout blk %lu/%lu srv %s",
            (unsigned long)s_chunk.currentBlockIndex, (unsigned long)s_chunk.totalBlocks,
            s_chunk.serverIP.toString().c_str());
        abortChunkSession(err);
        return;
    }
    if (now < s_chunk.nextRequestDue) return;

    requestOneFirmwareBlock();
    esp_task_wdt_reset();
}

void processJSONMessage_FirmwareAvailable(JsonObject root, String& responseMsg) {
    responseMsg = "OK";
    if (fwResponseReceived || s_chunk.active) return;

    FirmwareVersion newVersion;
    if (!parseFirmwareFromJson(root["newFirmware"], newVersion)) return;

    FirmwareVersion localFw;
    getLocalFirmware(localFw);
    if (newVersion.compare(localFw) <= 0) return;

    uint16_t expectedCRC = 0;
    if (root["newFirmwareCRC"].is<uint16_t>()) expectedCRC = root["newFirmwareCRC"];
    else if (root["newFirmWareCRC"].is<uint16_t>()) expectedCRC = root["newFirmWareCRC"];

    uint32_t expectedSize = 0;
    if (root["newFirmwareSize"].is<uint32_t>()) expectedSize = root["newFirmwareSize"];
    else if (root["newFirmwareSize"].is<unsigned long>()) expectedSize = (uint32_t)root["newFirmwareSize"].as<unsigned long>();
    if (expectedSize == 0) return;

    IPAddress serverIP(0, 0, 0, 0);
    if (root["senderIP"].is<const char*>()) serverIP.fromString(root["senderIP"].as<String>());
    else if (root["senderDevice"].is<JsonObject>()) {
        JsonObject sender = root["senderDevice"];
        if (sender["ip"].is<const char*>()) serverIP.fromString(sender["ip"].as<String>());
    }
    if (serverIP == IPAddress(0, 0, 0, 0)) return;

    char verText[16];
    newVersion.toChar(verText, sizeof(verText));
    logSystemEvent(String("FW available from ") + serverIP.toString() + " v" + verText, EVENT_FIRMWARE_UPDATED);

    fwResponseReceived = true;
    if (!startChunkSession(serverIP, newVersion, expectedCRC, expectedSize)) {
        fwResponseReceived = false;
    }
}

void processJSONMessage_FirmwareUnavailable(JsonObject root, String& responseMsg) {
    (void)root;
    responseMsg = "OK";
}

void peripheralFirmwareHourlyCheck() {
    if (s_chunk.active) return;
    if (!wifiReadyForNetwork()) return;
    if (Sensors.countServers() == 0) return;

    if (fwCheckMinute == 255) fwCheckMinute = (uint8_t)random(0, 60);
    uint8_t hr = (uint8_t)hour();
    if ((uint8_t)minute() != fwCheckMinute || fwCheckHourDone == hr) return;

    fwCheckHourDone = hr;
    fwCheckMinute = (uint8_t)random(0, 60);
    fwResponseReceived = false;

    FirmwareVersion localFw;
    getLocalFirmware(localFw);
    char json[256];
    snprintf(json, sizeof(json),
        "{\"msgType\":\"FirmwareRequest\",\"senderDeviceName\":\"%s\",\"senderIP\":\"%s\",\"senderFirmware\":%s}",
        Prefs.DEVICENAME, WiFi.localIP().toString().c_str(), firmwareJsonArray(localFw).c_str());

    {
        char verText[16];
        localFw.toChar(verText, sizeof(verText));
        logSystemEvent(String("FW inquiry sent v") + verText + " via UDP", EVENT_FIRMWARE_UPDATED);
    }

    sendUDPMessage((uint8_t*)json, IPAddress(0, 0, 0, 0), (uint16_t)strlen(json), "fwReq");

    uint32_t start = millis();
    while ((millis() - start) < 5000) {
        receiveUDPMessage();
        server.handleClient();
        esp_task_wdt_reset();
        delay(10);
    }
}
#else
void processJSONMessage_FirmwareAvailable(JsonObject root, String& responseMsg) {
    (void)root;
    responseMsg = "OK";
}
void processJSONMessage_FirmwareUnavailable(JsonObject root, String& responseMsg) {
    (void)root;
    responseMsg = "OK";
}
void peripheralFirmwareHourlyCheck() {}
void processChunkFirmwareTick() {}
#endif

#if defined(_USESDCARD) && _IS_SERVER_HUB
void processJSONMessage_FirmwareRequest(JsonObject root, String& responseMsg) {
    responseMsg = "OK";
    if (_MYTYPE < 100) return;

    String deviceName;
    FirmwareVersion deviceFirmware;
    deviceFirmware.clear();
    IPAddress senderIP(0, 0, 0, 0);
    bool haveDeviceFirmware = false;

    if (root["senderDeviceName"].is<const char*>()) deviceName = root["senderDeviceName"].as<String>();
    if (root["senderFirmware"].is<JsonVariantConst>()) {
        haveDeviceFirmware = parseFirmwareFromJson(root["senderFirmware"], deviceFirmware);
    }
    if (root["senderIP"].is<const char*>()) senderIP.fromString(root["senderIP"].as<String>());

    if (deviceName.length() == 0 || senderIP == IPAddress(0, 0, 0, 0) || !haveDeviceFirmware) {
        responseMsg = "Missing firmware request fields";
        logSystemEvent("FWreq: rejected (missing fields)", EVENT_FIRMWARE_UPDATED);
        return;
    }

    if (serverShouldIgnoreFirmwareDiscovery(deviceName.c_str())) {
        logSystemEvent(String("FWreq: ") + deviceName + ",  cooldown", EVENT_FIRMWARE_UPDATED);
        return;
    }

    FirmwareVersion bestVersion;
    char bestPath[96];
    uint16_t crc = 0;
    uint32_t size = 0;
    bool haveFirmware = findHighestSDFirmwareForDevice(deviceName.c_str(), bestVersion,
        bestPath, sizeof(bestPath), &crc, &size);

    if (!haveFirmware) {
        logSystemEvent(String("FWreq: ") + deviceName + ",  no image", EVENT_FIRMWARE_UPDATED);
        return;
    }
    if (bestVersion.compare(deviceFirmware) <= 0) {
        logSystemEvent(String("FWreq: ") + deviceName + ",  up to date", EVENT_FIRMWARE_UPDATED);
        return;
    }

    char verText[16];
    bestVersion.toChar(verText, sizeof(verText));
    touchServerFwTrack(deviceName.c_str(), bestVersion, FW_CHUNK_NEXT_REQUEST_MINUTES);

    char json[320];
    snprintf(json, sizeof(json),
        "{\"msgType\":\"FirmwareAvailable\",\"senderDeviceID\":\"%s\",\"senderIP\":\"%s\","
        "\"newFirmware\":%s,\"newFirmwareCRC\":%u,\"newFirmwareSize\":%lu}",
        MACToString(Prefs.PROCID).c_str(), WiFi.localIP().toString().c_str(),
        firmwareJsonArray(bestVersion).c_str(), crc, (unsigned long)size);

    IPAddress ip = senderIP;
    int16_t rc = sendHTTPSJSON(ip, json, "fwResp");
    if (rc < 200 || rc >= 400) {
        logSystemEvent(String("FWreq: ") + deviceName + ",  " + verText + " offer failed", EVENT_FIRMWARE_UPDATED);
    } else {
        logSystemEvent(String("FWreq: ") + deviceName + ",  " + verText + " offered", EVENT_FIRMWARE_UPDATED);
    }
}
#else
void processJSONMessage_FirmwareRequest(JsonObject root, String& responseMsg) {
    (void)root;
    responseMsg = "OK";
}
#endif

#ifdef _USE32
void handleFirmwareEncRaw() {
  absorbBinaryPostRaw(FW_HTTP_ENC_MAX_CIPHER);
}
#endif

void handleFirmwareEnc() {
    registerHTTPMessage("fwEnc");

    uint8_t* encBuf = nullptr;
    size_t encLen = 0;
#ifdef _USE32
    if (!takeBinaryPostBody(&encBuf, &encLen, 32, FW_HTTP_ENC_MAX_CIPHER)) {
        server.send(400, "text/plain", "Invalid or missing body");
        return;
    }
#else
    if (!server.hasHeader("Content-Length")) {
        server.send(400, "text/plain", "Missing Content-Length");
        return;
    }
    encLen = (size_t)server.header("Content-Length").toInt();
    if (encLen < 32 || encLen > FW_HTTP_ENC_MAX_CIPHER) {
        server.send(400, "text/plain", "Invalid length");
        return;
    }
    encBuf = (uint8_t*)malloc(encLen);
    if (!encBuf) {
        server.send(500, "text/plain", "Out of memory");
        return;
    }
    WiFiClient client = server.client();
    size_t got = 0;
    uint32_t startMs = millis();
    while (got < encLen && (millis() - startMs) < 10000) {
        int avail = client.available();
        if (avail > 0) {
            int chunk = client.read(encBuf + got, encLen - got);
            if (chunk > 0) got += (size_t)chunk;
        } else {
            delay(1);
        }
    }
    if (got != encLen) {
        free(encBuf);
        server.send(400, "text/plain", "Incomplete body");
        return;
    }
#endif

    uint8_t plain[FW_HTTP_ENC_MAX + 16];
    uint16_t payloadLen = 0;
    if (!decryptHttpPayload(encBuf, (uint16_t)encLen, plain, sizeof(plain), &payloadLen)) {
        free(encBuf);
        server.send(500, "text/plain", "Decrypt failed");
        return;
    }
    free(encBuf);

    plain[payloadLen + 2] = '\0';
    String json = String((char*)(plain + 2));
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json)) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }
    if (!doc["msgType"].is<const char*>() || strcmp(doc["msgType"], "FirmwareDownload") != 0) {
        server.send(400, "text/plain", "Invalid msgType");
        return;
    }

    const char* deviceName = doc["targetDeviceName"] | "";
    FirmwareVersion version;
    if (deviceName[0] == '\0' || !parseFirmwareFromJson(doc["firmware"], version)) {
        server.send(400, "text/plain", "Missing fields");
        return;
    }

    #ifndef _USESDCARD
    server.send(404, "text/plain", "No SD card");
    return;
    #else
    char path[96];
    if (!buildFirmwareBinPath(deviceName, version, path, sizeof(path)) || !SD.exists(path)) {
        server.send(404, "text/plain", "Firmware not found");
        return;
    }

    char verText[16];
    version.toChar(verText, sizeof(verText));
    logSystemEvent(String("FW transfer started ") + deviceName + " v" + verText, EVENT_FIRMWARE_UPDATED);

    server.sendHeader("Connection", "close");
    server.send(200, "application/octet-stream", "");
    if (!streamFirmwareFileEncrypted(server.client(), path)) {
        logFirmwareTransferError("FW transfer failed " + String(deviceName) + " v" + verText);
    } else {
        logSystemEvent(String("FW transfer OK ") + deviceName + " v" + verText, EVENT_FIRMWARE_UPDATED);
    }
    #endif
}

String getFirmwareReceiveProgressSuffix() {
#if _HAS_LOCAL_SENSORS
    if (!s_chunk.active || s_chunk.totalBlocks == 0) return "";
    char verText[16];
    s_chunk.version.toChar(verText, sizeof(verText));
    char buf[96];
    snprintf(buf, sizeof(buf), ", received packet %lu/%lu for %s from %s",
        (unsigned long)s_chunk.currentBlockIndex, (unsigned long)s_chunk.totalBlocks,
        verText, s_chunk.serverIP.toString().c_str());
    return String(buf);
#else
    return "";
#endif
}
