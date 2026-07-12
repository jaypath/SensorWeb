#ifndef FIRMWAREUPDATE_HPP
#define FIRMWAREUPDATE_HPP

#include <Arduino.h>
#include <ArduinoJson.h>
#include "globals.hpp"

// Chunked network OTA (peripheral pulls 8 KB blocks from server SD).
#define FW_CHUNK_BLOCK_SIZE 8192
#define FW_CHUNK_NEXT_REQUEST_MINUTES 5
#define FW_CHUNK_RETRY_MINUTES 1
#define FW_CHUNK_TIMEOUT_MULTIPLIER 2
#define FW_CHUNK_SESSION_MAX_SEC 86400
#define FW_CHUNK_DISCOVERY_IGNORE_MULTIPLIER 4

bool parseFirmwareFromJson(JsonVariantConst variant, FirmwareVersion& out);
// Parses "<devicename>-<major>.<minor>.<patch>.bin" (version is after the last hyphen).
bool parseFirmwareDeviceAndVersionFromBinName(const char* filename, char* deviceOut, size_t deviceLen,
    FirmwareVersion& versionOut);
bool parseFirmwareVersionFromBinName(const char* filename, FirmwareVersion& out);
bool buildFirmwareBinPath(const char* deviceName, const FirmwareVersion& version, char* out, size_t outLen);
bool findHighestSDFirmwareForDevice(const char* deviceName, FirmwareVersion& versionOut,
    char* filePathOut, size_t pathLen, uint16_t* crcOut, uint32_t* sizeOut);
uint16_t computeFileCRC(const char* filePath);
uint16_t computeBufferCRC(const uint8_t* data, size_t len);
bool checkAndApplySDFirmwareOnBoot();
void peripheralFirmwareHourlyCheck();
void processChunkFirmwareTick();
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
