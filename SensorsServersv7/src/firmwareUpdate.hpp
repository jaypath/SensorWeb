#ifndef FIRMWAREUPDATE_HPP
#define FIRMWAREUPDATE_HPP

#include <Arduino.h>
#include <ArduinoJson.h>
#include "globals.hpp"

// Chunked network OTA: peripheral discovers via UDP, then pulls 8 KB blocks over one keep-alive HTTP client.
#define FW_CHUNK_BLOCK_SIZE 8192
#define FW_CHUNK_RETRY_SEC 30
#define FW_CHUNK_TIMEOUT_MULTIPLIER 2
#define FW_CHUNK_SESSION_MAX_SEC 86400
#define FW_CHUNK_PULL_BUDGET_MS 4000   // yield to main loop after this much pull work per tick
#define FW_CHUNK_RECONNECT_MAX 8

bool parseFirmwareFromJson(JsonVariantConst variant, FirmwareVersion& out);
// Parses "<devicename>-<major>.<minor>.<patch>.bin" (version is after the last hyphen).
bool parseFirmwareDeviceAndVersionFromBinName(const char* filename, char* deviceOut, size_t deviceLen,
    FirmwareVersion& versionOut);
bool parseFirmwareVersionFromBinName(const char* filename, FirmwareVersion& out);
bool buildFirmwareBinPath(const char* deviceName, const FirmwareVersion& version, char* out, size_t outLen);
bool findHighestSDFirmwareForDevice(const char* deviceName, FirmwareVersion& versionOut,
    char* filePathOut, size_t pathLen, uint16_t* crcOut, uint32_t* sizeOut);
/** Deletes /Firmware/<device>-x.y.z.bin files with version strictly older than newerVersion. */
uint8_t deleteOlderSDFirmwareForDevice(const char* deviceName, const FirmwareVersion& newerVersion);
/** After a successful /Firmware upload, prune older bins for that device. Returns count deleted. */
uint8_t pruneOlderSDFirmwareAfterUpload(const char* uploadedPath);
uint16_t computeFileCRC(const char* filePath);
uint16_t computeBufferCRC(const uint8_t* data, size_t len);
bool checkAndApplySDFirmwareOnBoot();
void peripheralFirmwareHourlyCheck();
void processChunkFirmwareTick();
bool isFirmwareChunkSessionActive();
/**
 * HTTP FirmwareRequest to one server. Blocks for the inline yes/no reply.
 * Returns 1 = FirmwareAvailable (and starts chunk download if startDownload),
 *         0 = FirmwareUnavailable, -1 = transport/parse error.
 */
int8_t sendMSG_FirmwareRequest(IPAddress& serverIP, bool startDownload = true, uint16_t timeoutMs = 5000);
/** Poll known servers over HTTP until yes, all say no, or none reachable. Same return codes. */
int8_t checkFirmwareFromServersHTTP(bool startDownload = true, uint16_t timeoutMs = 5000);
void processJSONMessage_FirmwareRequest(JsonObject root, String& responseMsg);
void processJSONMessage_FirmwareAvailable(JsonObject root, String& responseMsg);
void processJSONMessage_FirmwareUnavailable(JsonObject root, String& responseMsg);
void handleFirmwareBlock();
void handleFirmwareEnc();
#ifdef _USE32
void handleFirmwareEncRaw();
#endif
/** Empty when no OTA download is in progress; else ", received packet N/M for x.y.z from a.b.c.d" */
String getFirmwareReceiveProgressSuffix();

#endif
