/**
 * @file StorageManager.cpp
 * @brief Storage manager implementation for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 */

#include "StorageManager.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Global storage manager instance
StorageManager& storageManager = StorageManager::getInstance();

StorageManager::StorageManager() :
    config{},
    stats{},
    initialized(false),
    lastBackupTime(0),
    lastCleanupTime(0),
    lastStatsUpdate(0) {}

StorageManager::~StorageManager() {
    if (initialized) {
        preferences.end();
    }
}

StorageManager& StorageManager::getInstance() {
    static StorageManager instance;
    return instance;
}

bool StorageManager::initialize() {
    if (initialized) return true;
    if (!preferences.begin("storage", false)) {
        ErrorHandler::log(ErrorCode::STORAGE_PREFS_INIT_FAILED, ErrorSeverity::ERROR, "Failed to initialize storage preferences");
        return false;
    }
    if (config.enableSPIFFS && !initializeSPIFFS()) {
        ErrorHandler::log(ErrorCode::STORAGE_SPIFFS_INIT_FAILED, ErrorSeverity::ERROR, "Failed to initialize SPIFFS");
        return false;
    }
    // TODO: SD card and external flash initialization
    initialized = true;
    return true;
}

bool StorageManager::updateConfig(const StorageConfig& newConfig) {
    config = newConfig;
    // TODO: Save config to preferences
    return true;
}

void StorageManager::clearStats() {
    stats = {};
}

// Preferences operations
bool StorageManager::storeString(const String& namespace_, const String& key, const String& value) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), false)) return false;
    bool ok = prefs.putString(key.c_str(), value);
    prefs.end();
    return ok;
}

String StorageManager::getString(const String& namespace_, const String& key, const String& defaultValue) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), true)) return defaultValue;
    String val = prefs.getString(key.c_str(), defaultValue);
    prefs.end();
    return val;
}

bool StorageManager::storeInt(const String& namespace_, const String& key, int32_t value) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), false)) return false;
    bool ok = prefs.putInt(key.c_str(), value);
    prefs.end();
    return ok;
}

int32_t StorageManager::getInt(const String& namespace_, const String& key, int32_t defaultValue) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), true)) return defaultValue;
    int32_t val = prefs.getInt(key.c_str(), defaultValue);
    prefs.end();
    return val;
}

bool StorageManager::storeFloat(const String& namespace_, const String& key, float value) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), false)) return false;
    bool ok = prefs.putFloat(key.c_str(), value);
    prefs.end();
    return ok;
}

float StorageManager::getFloat(const String& namespace_, const String& key, float defaultValue) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), true)) return defaultValue;
    float val = prefs.getFloat(key.c_str(), defaultValue);
    prefs.end();
    return val;
}

bool StorageManager::storeBool(const String& namespace_, const String& key, bool value) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), false)) return false;
    bool ok = prefs.putBool(key.c_str(), value);
    prefs.end();
    return ok;
}

bool StorageManager::getBool(const String& namespace_, const String& key, bool defaultValue) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), true)) return defaultValue;
    bool val = prefs.getBool(key.c_str(), defaultValue);
    prefs.end();
    return val;
}

bool StorageManager::hasKey(const String& namespace_, const String& key) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), true)) return false;
    bool exists = prefs.isKey(key.c_str());
    prefs.end();
    return exists;
}

bool StorageManager::removeKey(const String& namespace_, const String& key) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), false)) return false;
    bool ok = prefs.remove(key.c_str());
    prefs.end();
    return ok;
}

bool StorageManager::clearNamespace(const String& namespace_) {
    Preferences prefs;
    if (!prefs.begin(namespace_.c_str(), false)) return false;
    prefs.clear();
    prefs.end();
    return true;
}

// File system operations
bool StorageManager::writeFile(const String& path, const String& data, DataCategory) {
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) return false;
    size_t written = f.print(data);
    f.close();
    return written == data.length();
}

bool StorageManager::readFile(const String& path, String& data) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;
    data = f.readString();
    f.close();
    return true;
}

bool StorageManager::appendFile(const String& path, const String& data, DataCategory) {
    File f = SPIFFS.open(path, FILE_APPEND);
    if (!f) return false;
    size_t written = f.print(data);
    f.close();
    return written == data.length();
}

bool StorageManager::deleteFile(const String& path) {
    return SPIFFS.remove(path);
}

bool StorageManager::fileExists(const String& path) {
    return SPIFFS.exists(path);
}

uint32_t StorageManager::getFileSize(const String& path) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t size = f.size();
    f.close();
    return size;
}

bool StorageManager::getFileInfo(const String& path, FileInfo& info) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;
    info.name = f.name();
    info.size = f.size();
    info.path = path;
    info.isDirectory = f.isDirectory();
    info.modifiedTime = 0; // Not available in SPIFFS
    info.category = DataCategory::TEMPORARY;
    info.storageType = StorageType::SPIFFS;
    info.isReadOnly = false;
    f.close();
    return true;
}

bool StorageManager::listFiles(const String& path, std::vector<FileInfo>& files, bool recursive) {
    File root = SPIFFS.open(path);
    if (!root || !root.isDirectory()) return false;
    File file = root.openNextFile();
    while (file) {
        FileInfo info;
        info.name = file.name();
        info.size = file.size();
        info.path = String(path) + "/" + file.name();
        info.isDirectory = file.isDirectory();
        info.modifiedTime = 0;
        info.category = DataCategory::TEMPORARY;
        info.storageType = StorageType::SPIFFS;
        info.isReadOnly = false;
        files.push_back(info);
        if (recursive && file.isDirectory()) {
            listFiles(info.path, files, true);
        }
        file = root.openNextFile();
    }
    return true;
}

bool StorageManager::createDirectory(const String& path) {
    return SPIFFS.mkdir(path);
}

bool StorageManager::removeDirectory(const String& path, bool recursive) {
    // SPIFFS does not support recursive directory removal
    return SPIFFS.rmdir(path);
}

// Sensor data operations (stubs)
bool StorageManager::storeSensorData(const SensorData&) { return true; }
bool StorageManager::getSensorData(uint8_t, uint32_t, uint32_t, std::vector<SensorData>&) { return true; }
bool StorageManager::getLatestSensorData(uint8_t, SensorData&) { return true; }
bool StorageManager::storeSystemConfig(const SystemConfig&) { return true; }
bool StorageManager::getSystemConfig(SystemConfig&) { return true; }

// Logging operations (stubs)
bool StorageManager::writeLog(ErrorSeverity, const String&) { return true; }
bool StorageManager::readLogs(uint32_t, uint32_t, ErrorSeverity, std::vector<String>&) { return true; }
uint32_t StorageManager::clearOldLogs(uint32_t) { return 0; }

// Backup and restore (stubs)
bool StorageManager::createBackup(const String&) { return true; }
bool StorageManager::restoreBackup(const String&) { return true; }
bool StorageManager::listBackups(std::vector<String>&) { return true; }
bool StorageManager::deleteBackup(const String&) { return true; }

// Utility operations
uint32_t StorageManager::getFreeSpace(StorageType) { return SPIFFS.totalBytes() - SPIFFS.usedBytes(); }
uint32_t StorageManager::getTotalSpace(StorageType) { return SPIFFS.totalBytes(); }
bool StorageManager::performHealthCheck() { return true; }
uint32_t StorageManager::cleanupOldFiles(uint32_t) { return 0; }
bool StorageManager::formatStorage(StorageType) { return SPIFFS.format(); }

String StorageManager::getStorageInfo() {
    String info = "{";
    info += "\"total\":" + String(SPIFFS.totalBytes()) + ",";
    info += "\"used\":" + String(SPIFFS.usedBytes()) + ",";
    info += "\"free\":" + String(getFreeSpace()) + "}";
    return info;
}

String StorageManager::getStatusString() {
    return initialized ? "Ready" : "Not Initialized";
}

void StorageManager::loop() {
    // Periodic tasks: backup, log rotation, cache management
    // TODO: Implement as needed
}

// Private helpers
bool StorageManager::initializeSPIFFS() {
    if (!SPIFFS.begin(true)) {
        ErrorHandler::log(ErrorCode::STORAGE_SPIFFS_INIT_FAILED, ErrorSeverity::ERROR, "SPIFFS mount failed");
        return false;
    }
    return true;
}
bool StorageManager::initializeSDCard() { return true; }
bool StorageManager::initializeExternalFlash() { return true; }
String StorageManager::getStoragePath(DataCategory) { return "/"; }
String StorageManager::generateBackupFilename(const String& backupName) { return "/backup_" + backupName + ".bak"; }
void StorageManager::updateStats() {}
void StorageManager::performAutomaticBackup() {}
void StorageManager::performLogRotation() {}
void StorageManager::performCacheCleanup() {}
bool StorageManager::compressData(const String&, String&) { return false; }
bool StorageManager::decompressData(const String&, String&) { return false; }
bool StorageManager::validatePath(const String&) { return true; }
void StorageManager::logStorageEvent(const String& event, ErrorSeverity severity) {
    ErrorHandler::log(ErrorCode::STORAGE_EVENT, severity, event);
}
String StorageManager::getStorageTypeName(StorageType type) {
    switch (type) {
        case StorageType::PREFERENCES: return "Preferences";
        case StorageType::SPIFFS: return "SPIFFS";
        case StorageType::SD_CARD: return "SD Card";
        case StorageType::EXTERNAL_FLASH: return "External Flash";
        default: return "Unknown";
    }
}
String StorageManager::getDataCategoryName(DataCategory cat) {
    switch (cat) {
        case DataCategory::SYSTEM_CONFIG: return "SystemConfig";
        case DataCategory::SENSOR_CONFIG: return "SensorConfig";
        case DataCategory::SENSOR_DATA: return "SensorData";
        case DataCategory::NETWORK_CONFIG: return "NetworkConfig";
        case DataCategory::USER_PREFERENCES: return "UserPreferences";
        case DataCategory::LOGS: return "Logs";
        case DataCategory::STATISTICS: return "Statistics";
        case DataCategory::TEMPORARY: return "Temporary";
        case DataCategory::BACKUP: return "Backup";
        case DataCategory::CACHE: return "Cache";
        default: return "Unknown";
    }
} 