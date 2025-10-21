/**
 * @file SensorManager.hpp
 * @brief Sensor data management for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides comprehensive sensor management including:
 * - Sensor initialization and configuration
 * - Data collection and validation
 * - Error handling and recovery
 * - Sensor type management
 * - Data storage and retrieval
 */

#ifndef SENSOR_MANAGER_HPP
#define SENSOR_MANAGER_HPP

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include <map>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations
class SensorType;
class SensorData;

/**
 * @brief Sensor types enumeration
 * 
 * This enumeration defines all supported sensor types in the system.
 * Each type has specific characteristics and handling requirements.
 */
enum class SensorTypeEnum : uint8_t {
    UNDEFINED = 0,        ///< Undefined/not a sensor
    TEMP_DHT = 1,         ///< Temperature from DHT sensor
    RH_DHT = 2,           ///< Relative humidity from DHT sensor
    SOIL_MOISTURE = 3,    ///< Soil moisture (capacitive or resistive)
    TEMP_AHT21 = 4,       ///< Temperature from AHT21 sensor
    RH_AHT21 = 5,         ///< Relative humidity from AHT21 sensor
    DISTANCE_HCSR04 = 7,  ///< Distance from HC-SR04 ultrasonic sensor
    HUMAN_PRESENCE = 8,   ///< Human presence (mm wave)
    BMP_PRESSURE = 9,     ///< Pressure from BMP sensor
    BMP_TEMP = 10,        ///< Temperature from BMP sensor
    BMP_ALTITUDE = 11,    ///< Altitude from BMP sensor
    PRESSURE_PREDICTION = 12, ///< Pressure-derived prediction
    BME_PRESSURE = 13,    ///< Pressure from BME sensor
    BME_TEMP = 14,        ///< Temperature from BME sensor
    BME_HUMIDITY = 15,    ///< Humidity from BME sensor
    BME_ALTITUDE = 16,    ///< Altitude from BME sensor
    BME680_TEMP = 17,     ///< Temperature from BME680 sensor
    BME680_RH = 18,       ///< Relative humidity from BME680 sensor
    BME680_PRESSURE = 19, ///< Pressure from BME680 sensor
    BME680_GAS = 20,      ///< Gas sensor from BME680
    HUMAN_PRESENT = 21,   ///< Human present (mmwave)
    BINARY_ANY = 40,      ///< Any binary sensor (1=yes/true/on)
    ON_OFF_SWITCH = 41,   ///< On/off switch
    YES_NO_SWITCH = 42,   ///< Yes/no switch
    THREE_WAY_SWITCH = 43, ///< 3-way switch
    HVAC_TOTAL_TIME = 50, ///< Total HVAC time
    HEAT_ON_OFF = 55,     ///< Heat on/off (requires N DIO pins)
    AC_ON_OFF = 56,       ///< A/C on/off (requires 2 DIO pins)
    AC_FAN_ON_OFF = 57,   ///< A/C fan on/off
    BATTERY_POWER = 60,   ///< Battery power
    BATTERY_PERCENT = 61, ///< Battery percentage
    LEAK = 70,            ///< Leak detection
    NUMERICAL_ANY = 99,   ///< Any numerical value
    SERVER = 100          ///< Server type (master server)
};

/**
 * @brief Sensor states
 */
enum class SensorState : uint8_t {
    UNINITIALIZED = 0,    ///< Sensor not initialized
    INITIALIZING,         ///< Sensor initialization in progress
    READY,               ///< Sensor ready for operation
    READING,             ///< Sensor reading in progress
    ERROR,               ///< Sensor in error state
    DISABLED,            ///< Sensor disabled
    EXPIRED              ///< Sensor data expired
};

/**
 * @brief Sensor flags bit definitions
 */
enum SensorFlags : uint8_t {
    FLAG_FLAGGED = 0x01,         ///< Bit 0: Flagged for attention
    FLAG_MONITORED = 0x02,       ///< Bit 1: Monitored sensor
    FLAG_OUTSIDE = 0x04,         ///< Bit 2: Outside sensor
    FLAG_DERIVED = 0x08,         ///< Bit 3: Derived/calculated value
    FLAG_PREDICTIVE = 0x10,      ///< Bit 4: Predictive value
    FLAG_HIGH_LOW = 0x20,        ///< Bit 5: High (1) or Low (0) threshold
    FLAG_CHANGED = 0x40,         ///< Bit 6: Flag changed since last read
    FLAG_CRITICAL = 0x80         ///< Bit 7: Critical sensor (never expire)
};

/**
 * @brief Sensor configuration structure
 */
struct SensorConfig {
    uint8_t pin;                 ///< GPIO pin number
    uint8_t sensorType;          ///< Sensor type
    String name;                 ///< Sensor name
    float offset;                ///< Calibration offset
    float multiplier;            ///< Calibration multiplier
    float limitUpper;            ///< Upper limit for alerts
    float limitLower;            ///< Lower limit for alerts
    uint32_t pollingInterval;    ///< Polling interval (seconds)
    uint32_t sendingInterval;    ///< Sending interval (seconds)
    uint32_t timeout;            ///< Read timeout (milliseconds)
    uint8_t flags;               ///< Sensor flags
    bool enabled;                ///< Sensor enabled/disabled
    uint32_t maxAge;             ///< Maximum age before expiration (seconds)
    
    // Default constructor
    SensorConfig() : 
        pin(0),
        sensorType(0),
        offset(0.0f),
        multiplier(1.0f),
        limitUpper(100.0f),
        limitLower(0.0f),
        pollingInterval(60),
        sendingInterval(300),
        timeout(5000),
        flags(FLAG_MONITORED),
        enabled(true),
        maxAge(3600) {} // 1 hour default
};

/**
 * @brief Sensor data structure
 */
struct SensorData {
    uint8_t sensorId;            ///< Unique sensor ID
    uint8_t sensorType;          ///< Sensor type
    String name;                 ///< Sensor name
    float value;                 ///< Sensor value
    uint32_t timestamp;          ///< Data timestamp
    uint8_t flags;               ///< Data flags
    bool expired;                ///< Data expired flag
    uint32_t readCount;          ///< Number of successful reads
    uint32_t errorCount;         ///< Number of read errors
    float lastValue;             ///< Previous value
    uint32_t lastReadTime;       ///< Last successful read time
    
    // Default constructor
    SensorData() : 
        sensorId(0),
        sensorType(0),
        value(0.0f),
        timestamp(0),
        flags(0),
        expired(false),
        readCount(0),
        errorCount(0),
        lastValue(0.0f),
        lastReadTime(0) {}
};

/**
 * @brief Sensor statistics
 */
struct SensorStats {
    uint32_t totalSensors;       ///< Total number of sensors
    uint32_t activeSensors;      ///< Number of active sensors
    uint32_t errorSensors;       ///< Number of sensors in error
    uint32_t totalReads;         ///< Total sensor reads
    uint32_t successfulReads;    ///< Successful reads
    uint32_t failedReads;        ///< Failed reads
    uint32_t totalErrors;        ///< Total errors
    float averageReadTime;       ///< Average read time (ms)
    uint32_t lastUpdateTime;     ///< Last update timestamp
};

/**
 * @brief Comprehensive sensor management class
 * 
 * This class provides centralized sensor management with:
 * - Automatic sensor initialization and configuration
 * - Data collection and validation
 * - Error handling and recovery
 * - Sensor type management
 * - Performance monitoring
 */
class SensorManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to SensorManager instance
     */
    static SensorManager& getInstance();
    
    /**
     * @brief Initialize sensor manager
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Add a new sensor
     * @param config Sensor configuration
     * @return Sensor ID if successful, 0 otherwise
     */
    uint8_t addSensor(const SensorConfig& config);
    
    /**
     * @brief Remove a sensor
     * @param sensorId Sensor ID to remove
     * @return true if removal successful, false otherwise
     */
    bool removeSensor(uint8_t sensorId);
    
    /**
     * @brief Get sensor by ID
     * @param sensorId Sensor ID
     * @return Pointer to sensor data, nullptr if not found
     */
    SensorData* getSensor(uint8_t sensorId);
    
    /**
     * @brief Get sensor by index
     * @param index Sensor index
     * @return Pointer to sensor data, nullptr if invalid index
     */
    SensorData* getSensorByIndex(uint16_t index);
    
    /**
     * @brief Get sensor configuration
     * @param sensorId Sensor ID
     * @return Pointer to sensor configuration, nullptr if not found
     */
    SensorConfig* getSensorConfig(uint8_t sensorId);
    
    /**
     * @brief Update sensor configuration
     * @param sensorId Sensor ID
     * @param config New configuration
     * @return true if update successful, false otherwise
     */
    bool updateSensorConfig(uint8_t sensorId, const SensorConfig& config);
    
    /**
     * @brief Enable/disable sensor
     * @param sensorId Sensor ID
     * @param enabled true to enable, false to disable
     * @return true if operation successful, false otherwise
     */
    bool setSensorEnabled(uint8_t sensorId, bool enabled);
    
    /**
     * @brief Read sensor value
     * @param sensorId Sensor ID
     * @return true if read successful, false otherwise
     */
    bool readSensor(uint8_t sensorId);
    
    /**
     * @brief Read all sensors
     * @return Number of sensors successfully read
     */
    uint16_t readAllSensors();
    
    /**
     * @brief Get sensor value
     * @param sensorId Sensor ID
     * @return Sensor value, NaN if sensor not found or error
     */
    float getSensorValue(uint8_t sensorId);
    
    /**
     * @brief Get sensor value by name
     * @param name Sensor name
     * @return Sensor value, NaN if sensor not found or error
     */
    float getSensorValueByName(const String& name);
    
    /**
     * @brief Get sensor value by type
     * @param sensorType Sensor type
     * @param index Index for multiple sensors of same type (0-based)
     * @return Sensor value, NaN if sensor not found or error
     */
    float getSensorValueByType(uint8_t sensorType, uint8_t index = 0);
    
    /**
     * @brief Check if sensor is valid
     * @param sensorId Sensor ID
     * @return true if sensor valid, false otherwise
     */
    bool isSensorValid(uint8_t sensorId);
    
    /**
     * @brief Check if sensor is initialized
     * @param sensorId Sensor ID
     * @return true if sensor initialized, false otherwise
     */
    bool isSensorInitialized(uint8_t sensorId);
    
    /**
     * @brief Check if sensor data is expired
     * @param sensorId Sensor ID
     * @return true if data expired, false otherwise
     */
    bool isSensorExpired(uint8_t sensorId);
    
    /**
     * @brief Get number of sensors
     * @return Total number of sensors
     */
    uint16_t getSensorCount() const { return sensors.size(); }
    
    /**
     * @brief Get number of active sensors
     * @return Number of active sensors
     */
    uint16_t getActiveSensorCount() const;
    
    /**
     * @brief Get sensor statistics
     * @return Reference to sensor statistics
     */
    const SensorStats& getStats() const { return stats; }
    
    /**
     * @brief Clear sensor statistics
     */
    void clearStats();
    
    /**
     * @brief Find sensor by name
     * @param name Sensor name
     * @return Sensor ID if found, 0 otherwise
     */
    uint8_t findSensorByName(const String& name);
    
    /**
     * @brief Find sensor by type
     * @param sensorType Sensor type
     * @param index Index for multiple sensors of same type (0-based)
     * @return Sensor ID if found, 0 otherwise
     */
    uint8_t findSensorByType(uint8_t sensorType, uint8_t index = 0);
    
    /**
     * @brief Get sensors by type
     * @param sensorType Sensor type
     * @return Vector of sensor IDs
     */
    std::vector<uint8_t> getSensorsByType(uint8_t sensorType);
    
    /**
     * @brief Get sensors by flag
     * @param flag Flag to check
     * @return Vector of sensor IDs
     */
    std::vector<uint8_t> getSensorsByFlag(uint8_t flag);
    
    /**
     * @brief Set sensor flag
     * @param sensorId Sensor ID
     * @param flag Flag to set
     * @param value Flag value
     * @return true if operation successful, false otherwise
     */
    bool setSensorFlag(uint8_t sensorId, uint8_t flag, bool value);
    
    /**
     * @brief Get sensor flag
     * @param sensorId Sensor ID
     * @param flag Flag to check
     * @return Flag value
     */
    bool getSensorFlag(uint8_t sensorId, uint8_t flag);
    
    /**
     * @brief Validate sensor value
     * @param sensorId Sensor ID
     * @param value Value to validate
     * @return true if value valid, false otherwise
     */
    bool validateSensorValue(uint8_t sensorId, float value);
    
    /**
     * @brief Calibrate sensor
     * @param sensorId Sensor ID
     * @param knownValue Known reference value
     * @return true if calibration successful, false otherwise
     */
    bool calibrateSensor(uint8_t sensorId, float knownValue);
    
    /**
     * @brief Reset sensor
     * @param sensorId Sensor ID
     * @return true if reset successful, false otherwise
     */
    bool resetSensor(uint8_t sensorId);
    
    /**
     * @brief Reset all sensors
     * @return Number of sensors reset
     */
    uint16_t resetAllSensors();
    
    /**
     * @brief Perform sensor health check
     * @return true if sensors healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Get sensor information as JSON string
     * @param sensorId Sensor ID (0 for all sensors)
     * @return JSON string with sensor information
     */
    String getSensorInfo(uint8_t sensorId = 0);
    
    /**
     * @brief Get sensor status as human-readable string
     * @param sensorId Sensor ID (0 for all sensors)
     * @return Status string
     */
    String getSensorStatus(uint8_t sensorId = 0);
    
    /**
     * @brief Main sensor management loop
     * 
     * This method should be called regularly to handle:
     * - Sensor polling
     * - Data validation
     * - Error recovery
     * - Statistics updates
     */
    void loop();
    
    /**
     * @brief Load sensor configuration from storage
     * @return true if loading successful, false otherwise
     */
    bool loadConfiguration();
    
    /**
     * @brief Save sensor configuration to storage
     * @return true if saving successful, false otherwise
     */
    bool saveConfiguration();

private:
    // Singleton pattern - prevent external instantiation
    SensorManager();
    ~SensorManager();
    SensorManager(const SensorManager&) = delete;
    SensorManager& operator=(const SensorManager&) = delete;
    
    // Sensor storage
    std::vector<SensorData> sensors;
    std::vector<SensorConfig> configs;
    std::map<String, uint8_t> nameToId;
    std::map<uint8_t, std::vector<uint8_t>> typeToIds;
    
    // Statistics and state
    SensorStats stats;
    uint32_t lastPollTime;
    uint32_t lastUpdateTime;
    bool initialized;
    
    // Preferences for configuration storage
    Preferences preferences;
    
    // Next available sensor ID
    uint8_t nextSensorId;
    
    /**
     * @brief Initialize sensor hardware
     * @param sensorId Sensor ID
     * @return true if initialization successful, false otherwise
     */
    bool initializeSensorHardware(uint8_t sensorId);
    
    /**
     * @brief Read sensor value from hardware
     * @param sensorId Sensor ID
     * @param value Output value
     * @return true if read successful, false otherwise
     */
    bool readSensorHardware(uint8_t sensorId, float& value);
    
    /**
     * @brief Validate sensor configuration
     * @param config Sensor configuration
     * @return true if configuration valid, false otherwise
     */
    bool validateConfig(const SensorConfig& config);
    
    /**
     * @brief Update sensor statistics
     * @param sensorId Sensor ID
     * @param success true if read successful, false otherwise
     * @param readTime Read time in milliseconds
     */
    void updateStats(uint8_t sensorId, bool success, uint32_t readTime);
    
    /**
     * @brief Check sensor expiration
     * @param sensorId Sensor ID
     */
    void checkSensorExpiration(uint8_t sensorId);
    
    /**
     * @brief Handle sensor error
     * @param sensorId Sensor ID
     * @param error Error description
     */
    void handleSensorError(uint8_t sensorId, const String& error);
    
    /**
     * @brief Attempt sensor recovery
     * @param sensorId Sensor ID
     * @return true if recovery successful, false otherwise
     */
    bool attemptSensorRecovery(uint8_t sensorId);
    
    /**
     * @brief Generate unique sensor ID
     * @return Unique sensor ID
     */
    uint8_t generateSensorId();
    
    /**
     * @brief Update sensor mappings
     * @param sensorId Sensor ID
     * @param name Sensor name
     * @param type Sensor type
     */
    void updateSensorMappings(uint8_t sensorId, const String& name, uint8_t type);
    
    /**
     * @brief Remove sensor mappings
     * @param sensorId Sensor ID
     */
    void removeSensorMappings(uint8_t sensorId);
    
    /**
     * @brief Log sensor event
     * @param sensorId Sensor ID
     * @param event Event description
     * @param severity Event severity
     */
    void logSensorEvent(uint8_t sensorId, const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
    
    /**
     * @brief Get sensor type name
     * @param sensorType Sensor type
     * @return Sensor type name
     */
    String getSensorTypeName(uint8_t sensorType);
    
    /**
     * @brief Check if sensor type is valid
     * @param sensorType Sensor type
     * @return true if valid, false otherwise
     */
    bool isValidSensorType(uint8_t sensorType);
};

// Global sensor manager instance
extern SensorManager& sensorManager;

// Convenience macros for sensor management
#define SENSOR_INIT() sensorManager.initialize()
#define SENSOR_READ(id) sensorManager.readSensor(id)
#define SENSOR_READ_ALL() sensorManager.readAllSensors()
#define SENSOR_VALUE(id) sensorManager.getSensorValue(id)
#define SENSOR_VALUE_BY_NAME(name) sensorManager.getSensorValueByName(name)
#define SENSOR_VALUE_BY_TYPE(type, index) sensorManager.getSensorValueByType(type, index)
#define SENSOR_VALID(id) sensorManager.isSensorValid(id)
#define SENSOR_INITIALIZED(id) sensorManager.isSensorInitialized(id)
#define SENSOR_EXPIRED(id) sensorManager.isSensorExpired(id)
#define SENSOR_COUNT() sensorManager.getSensorCount()
#define SENSOR_ACTIVE_COUNT() sensorManager.getActiveSensorCount()
#define SENSOR_LOOP() sensorManager.loop()
#define SENSOR_HEALTH_CHECK() sensorManager.performHealthCheck()

#endif // SENSOR_MANAGER_HPP 