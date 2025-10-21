/**
 * @file StorageManager.hpp
 * @brief Data storage management for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides comprehensive storage functionality including:
 * - Preferences management
 * - File system operations
 * - Data persistence and retrieval
 * - Configuration storage
 * - Log management
 */

#ifndef STORAGE_MANAGER_HPP
#define STORAGE_MANAGER_HPP

#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <vector>
#include <map>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations
struct SensorData;
struct SystemConfig;

/**
 * @brief Storage types
 */
enum class StorageType : uint8_t {
    PREFERENCES = 0,      ///< ESP32 Preferences (NVS)
    SPIFFS,              ///< SPIFFS file system
    SD_CARD,             ///< SD card storage
    EXTERNAL_FLASH       ///< External flash memory
};

/**
 * @brief Data categories for storage
 */
enum class DataCategory : uint8_t {
    SYSTEM_CONFIG = 0,   ///< System configuration
    SENSOR_CONFIG,       ///< Sensor configuration
    SENSOR_DATA,         ///< Sensor data
    NETWORK_CONFIG,      ///< Network configuration
    USER_PREFERENCES,    ///< User preferences
    LOGS,                ///< System logs
    STATISTICS,          ///< System statistics
    TEMPORARY,           ///< Temporary data
    BACKUP,              ///< Backup data
    CACHE               ///< Cache data
};

/**
 * @brief Storage configuration structure
 */
struct StorageConfig {
    bool enableSPIFFS;           ///< Enable SPIFFS file system
    bool enableSDCard;           ///< Enable SD card storage
    bool enableExternalFlash;    ///< Enable external flash
    uint32_t maxFileSize;        ///< Maximum file size (bytes)
    uint32_t maxLogSize;         ///< Maximum log file size (bytes)
    uint32_t logRetentionDays;   ///< Log retention period (days)
    uint32_t backupInterval;     ///< Backup interval (seconds)
    uint32_t cacheSize;          ///< Cache size (bytes)
    bool autoBackup;             ///< Enable automatic backup
    bool compression;            ///< Enable data compression
    
    // Default constructor
    StorageConfig() : 
        enableSPIFFS(true),
        enableSDCard(false),
        enableExternalFlash(false),
        maxFileSize(1024 * 1024),    // 1MB
        maxLogSize(512 * 1024),      // 512KB
        logRetentionDays(7),
        backupInterval(86400),       // 24 hours
        cacheSize(64 * 1024),        // 64KB
        autoBackup(true),
        compression(false) {}
};

/**
 * @brief Storage statistics
 */
struct StorageStats {
    uint32_t totalFiles;         ///< Total number of files
    uint32_t totalSize;          ///< Total storage used (bytes)
    uint32_t freeSpace;          ///< Free space available (bytes)
    uint32_t readOperations;     ///< Number of read operations
    uint32_t writeOperations;    ///< Number of write operations
    uint32_t deleteOperations;   ///< Number of delete operations
    uint32_t errorCount;         ///< Number of storage errors
    uint32_t lastBackupTime;     ///< Last backup timestamp
    uint32_t lastCleanupTime;    ///< Last cleanup timestamp
};

/**
 * @brief File information structure
 */
struct FileInfo {
    String name;                 ///< File name
    String path;                 ///< File path
    uint32_t size;               ///< File size (bytes)
    uint32_t modifiedTime;       ///< Last modification time
    DataCategory category;       ///< Data category
    StorageType storageType;     ///< Storage type
    bool isDirectory;            ///< Is directory flag
    bool isReadOnly;             ///< Is read-only flag
};

/**
 * @brief Comprehensive storage management class
 * 
 * This class provides centralized storage functionality with:
 * - Multiple storage backends (Preferences, SPIFFS, SD Card)
 * - Automatic data management and cleanup
 * - Backup and restore capabilities
 * - Performance optimization
 * - Error handling and recovery
 */
class StorageManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to StorageManager instance
     */
    static StorageManager& getInstance();
    
    /**
     * @brief Initialize storage manager
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Get storage configuration
     * @return Reference to storage configuration
     */
    const StorageConfig& getConfig() const { return config; }
    
    /**
     * @brief Update storage configuration
     * @param newConfig New configuration
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const StorageConfig& newConfig);
    
    /**
     * @brief Get storage statistics
     * @return Reference to storage statistics
     */
    const StorageStats& getStats() const { return stats; }
    
    /**
     * @brief Clear storage statistics
     */
    void clearStats();
    
    // Preferences operations
    /**
     * @brief Store string in preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param value String value
     * @return true if storage successful, false otherwise
     */
    bool storeString(const String& namespace_, const String& key, const String& value);
    
    /**
     * @brief Retrieve string from preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @return Retrieved string value
     */
    String getString(const String& namespace_, const String& key, const String& defaultValue = "");
    
    /**
     * @brief Store integer in preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param value Integer value
     * @return true if storage successful, false otherwise
     */
    bool storeInt(const String& namespace_, const String& key, int32_t value);
    
    /**
     * @brief Retrieve integer from preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @return Retrieved integer value
     */
    int32_t getInt(const String& namespace_, const String& key, int32_t defaultValue = 0);
    
    /**
     * @brief Store float in preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param value Float value
     * @return true if storage successful, false otherwise
     */
    bool storeFloat(const String& namespace_, const String& key, float value);
    
    /**
     * @brief Retrieve float from preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @return Retrieved float value
     */
    float getFloat(const String& namespace_, const String& key, float defaultValue = 0.0f);
    
    /**
     * @brief Store boolean in preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param value Boolean value
     * @return true if storage successful, false otherwise
     */
    bool storeBool(const String& namespace_, const String& key, bool value);
    
    /**
     * @brief Retrieve boolean from preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @return Retrieved boolean value
     */
    bool getBool(const String& namespace_, const String& key, bool defaultValue = false);
    
    /**
     * @brief Check if key exists in preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @return true if key exists, false otherwise
     */
    bool hasKey(const String& namespace_, const String& key);
    
    /**
     * @brief Remove key from preferences
     * @param namespace_ Preferences namespace
     * @param key Key name
     * @return true if removal successful, false otherwise
     */
    bool removeKey(const String& namespace_, const String& key);
    
    /**
     * @brief Clear all keys in namespace
     * @param namespace_ Preferences namespace
     * @return true if clearing successful, false otherwise
     */
    bool clearNamespace(const String& namespace_);
    
    // File system operations
    /**
     * @brief Write data to file
     * @param path File path
     * @param data Data to write
     * @param category Data category
     * @return true if write successful, false otherwise
     */
    bool writeFile(const String& path, const String& data, DataCategory category = DataCategory::TEMPORARY);
    
    /**
     * @brief Read data from file
     * @param path File path
     * @param data Output data
     * @return true if read successful, false otherwise
     */
    bool readFile(const String& path, String& data);
    
    /**
     * @brief Append data to file
     * @param path File path
     * @param data Data to append
     * @param category Data category
     * @return true if append successful, false otherwise
     */
    bool appendFile(const String& path, const String& data, DataCategory category = DataCategory::TEMPORARY);
    
    /**
     * @brief Delete file
     * @param path File path
     * @return true if deletion successful, false otherwise
     */
    bool deleteFile(const String& path);
    
    /**
     * @brief Check if file exists
     * @param path File path
     * @return true if file exists, false otherwise
     */
    bool fileExists(const String& path);
    
    /**
     * @brief Get file size
     * @param path File path
     * @return File size in bytes, 0 if file doesn't exist
     */
    uint32_t getFileSize(const String& path);
    
    /**
     * @brief Get file information
     * @param path File path
     * @param info Output file information
     * @return true if successful, false otherwise
     */
    bool getFileInfo(const String& path, FileInfo& info);
    
    /**
     * @brief List files in directory
     * @param path Directory path
     * @param files Output file list
     * @param recursive Include subdirectories
     * @return true if listing successful, false otherwise
     */
    bool listFiles(const String& path, std::vector<FileInfo>& files, bool recursive = false);
    
    /**
     * @brief Create directory
     * @param path Directory path
     * @return true if creation successful, false otherwise
     */
    bool createDirectory(const String& path);
    
    /**
     * @brief Remove directory
     * @param path Directory path
     * @param recursive Remove subdirectories
     * @return true if removal successful, false otherwise
     */
    bool removeDirectory(const String& path, bool recursive = false);
    
    // Sensor data operations
    /**
     * @brief Store sensor data
     * @param sensorData Sensor data to store
     * @return true if storage successful, false otherwise
     */
    bool storeSensorData(const SensorData& sensorData);
    
    /**
     * @brief Retrieve sensor data
     * @param sensorId Sensor ID
     * @param startTime Start timestamp
     * @param endTime End timestamp
     * @param data Output sensor data vector
     * @return true if retrieval successful, false otherwise
     */
    bool getSensorData(uint8_t sensorId, uint32_t startTime, uint32_t endTime, std::vector<SensorData>& data);
    
    /**
     * @brief Get latest sensor data
     * @param sensorId Sensor ID
     * @param data Output sensor data
     * @return true if retrieval successful, false otherwise
     */
    bool getLatestSensorData(uint8_t sensorId, SensorData& data);
    
    /**
     * @brief Store system configuration
     * @param config System configuration
     * @return true if storage successful, false otherwise
     */
    bool storeSystemConfig(const SystemConfig& config);
    
    /**
     * @brief Retrieve system configuration
     * @param config Output system configuration
     * @return true if retrieval successful, false otherwise
     */
    bool getSystemConfig(SystemConfig& config);
    
    // Logging operations
    /**
     * @brief Write log entry
     * @param level Log level
     * @param message Log message
     * @return true if write successful, false otherwise
     */
    bool writeLog(ErrorSeverity level, const String& message);
    
    /**
     * @brief Read log entries
     * @param startTime Start timestamp
     * @param endTime End timestamp
     * @param level Minimum log level
     * @param entries Output log entries
     * @return true if read successful, false otherwise
     */
    bool readLogs(uint32_t startTime, uint32_t endTime, ErrorSeverity level, std::vector<String>& entries);
    
    /**
     * @brief Clear old logs
     * @param maxAge Maximum age in seconds
     * @return Number of log entries cleared
     */
    uint32_t clearOldLogs(uint32_t maxAge);
    
    // Backup and restore operations
    /**
     * @brief Create backup
     * @param backupName Backup name
     * @return true if backup successful, false otherwise
     */
    bool createBackup(const String& backupName);
    
    /**
     * @brief Restore from backup
     * @param backupName Backup name
     * @return true if restore successful, false otherwise
     */
    bool restoreBackup(const String& backupName);
    
    /**
     * @brief List available backups
     * @param backups Output backup list
     * @return true if listing successful, false otherwise
     */
    bool listBackups(std::vector<String>& backups);
    
    /**
     * @brief Delete backup
     * @param backupName Backup name
     * @return true if deletion successful, false otherwise
     */
    bool deleteBackup(const String& backupName);
    
    // Utility operations
    /**
     * @brief Get free space
     * @param storageType Storage type
     * @return Free space in bytes
     */
    uint32_t getFreeSpace(StorageType storageType = StorageType::SPIFFS);
    
    /**
     * @brief Get total space
     * @param storageType Storage type
     * @return Total space in bytes
     */
    uint32_t getTotalSpace(StorageType storageType = StorageType::SPIFFS);
    
    /**
     * @brief Check storage health
     * @return true if storage healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Clean up old files
     * @param maxAge Maximum age in seconds
     * @return Number of files cleaned up
     */
    uint32_t cleanupOldFiles(uint32_t maxAge);
    
    /**
     * @brief Format storage
     * @param storageType Storage type
     * @return true if formatting successful, false otherwise
     */
    bool formatStorage(StorageType storageType = StorageType::SPIFFS);
    
    /**
     * @brief Get storage information as JSON string
     * @return JSON string with storage information
     */
    String getStorageInfo();
    
    /**
     * @brief Get storage status as human-readable string
     * @return Status string
     */
    String getStatusString();
    
    /**
     * @brief Main storage management loop
     * 
     * This method should be called regularly to handle:
     * - Automatic backups
     * - Log rotation
     * - Cache management
     * - Statistics updates
     */
    void loop();

private:
    // Singleton pattern - prevent external instantiation
    StorageManager();
    ~StorageManager();
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    
    // Configuration and state
    StorageConfig config;
    StorageStats stats;
    bool initialized;
    
    // Storage instances
    Preferences preferences;
    
    // Timing and management
    uint32_t lastBackupTime;
    uint32_t lastCleanupTime;
    uint32_t lastStatsUpdate;
    
    /**
     * @brief Initialize SPIFFS
     * @return true if initialization successful, false otherwise
     */
    bool initializeSPIFFS();
    
    /**
     * @brief Initialize SD card
     * @return true if initialization successful, false otherwise
     */
    bool initializeSDCard();
    
    /**
     * @brief Initialize external flash
     * @return true if initialization successful, false otherwise
     */
    bool initializeExternalFlash();
    
    /**
     * @brief Get storage path for category
     * @param category Data category
     * @return Storage path
     */
    String getStoragePath(DataCategory category);
    
    /**
     * @brief Generate backup filename
     * @param backupName Backup name
     * @return Backup filename
     */
    String generateBackupFilename(const String& backupName);
    
    /**
     * @brief Update storage statistics
     */
    void updateStats();
    
    /**
     * @brief Perform automatic backup
     */
    void performAutomaticBackup();
    
    /**
     * @brief Perform log rotation
     */
    void performLogRotation();
    
    /**
     * @brief Perform cache cleanup
     */
    void performCacheCleanup();
    
    /**
     * @brief Compress data
     * @param input Input data
     * @param output Compressed data
     * @return true if compression successful, false otherwise
     */
    bool compressData(const String& input, String& output);
    
    /**
     * @brief Decompress data
     * @param input Compressed data
     * @param output Decompressed data
     * @return true if decompression successful, false otherwise
     */
    bool decompressData(const String& input, String& output);
    
    /**
     * @brief Validate file path
     * @param path File path
     * @return true if path valid, false otherwise
     */
    bool validatePath(const String& path);
    
    /**
     * @brief Log storage event
     * @param event Event description
     * @param severity Event severity
     */
    void logStorageEvent(const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
    
    /**
     * @brief Get storage type name
     * @param storageType Storage type
     * @return Storage type name
     */
    String getStorageTypeName(StorageType storageType);
    
    /**
     * @brief Get data category name
     * @param category Data category
     * @return Category name
     */
    String getDataCategoryName(DataCategory category);
};

// Global storage manager instance
extern StorageManager& storageManager;

// Convenience macros for storage management
#define STORAGE_INIT() storageManager.initialize()
#define STORAGE_STORE_STRING(ns, key, value) storageManager.storeString(ns, key, value)
#define STORAGE_GET_STRING(ns, key, def) storageManager.getString(ns, key, def)
#define STORAGE_STORE_INT(ns, key, value) storageManager.storeInt(ns, key, value)
#define STORAGE_GET_INT(ns, key, def) storageManager.getInt(ns, key, def)
#define STORAGE_STORE_FLOAT(ns, key, value) storageManager.storeFloat(ns, key, value)
#define STORAGE_GET_FLOAT(ns, key, def) storageManager.getFloat(ns, key, def)
#define STORAGE_STORE_BOOL(ns, key, value) storageManager.storeBool(ns, key, value)
#define STORAGE_GET_BOOL(ns, key, def) storageManager.getBool(ns, key, def)
#define STORAGE_WRITE_FILE(path, data) storageManager.writeFile(path, data)
#define STORAGE_READ_FILE(path, data) storageManager.readFile(path, data)
#define STORAGE_LOOP() storageManager.loop()
#define STORAGE_HEALTH_CHECK() storageManager.performHealthCheck()

#endif // STORAGE_MANAGER_HPP 