/**
 * @file SystemManager.hpp
 * @brief Main system coordinator for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides centralized system management including:
 * - System initialization and shutdown
 * - Module coordination and lifecycle management
 * - System state monitoring and health checks
 * - Configuration management
 * - Performance monitoring
 */

#ifndef SYSTEM_MANAGER_HPP
#define SYSTEM_MANAGER_HPP

#include <Arduino.h>
#include <Preferences.h>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations for modules
class WiFiManager;
class SensorManager;
class StorageManager;
class DisplayManager;
class ESPNowManager;
class SecurityManager;

/**
 * @brief System operating states
 */
enum class SystemState : uint8_t {
    UNINITIALIZED = 0,    ///< System not yet initialized
    INITIALIZING,         ///< System initialization in progress
    READY,               ///< System ready for normal operation
    RUNNING,             ///< System running normally
    ERROR,               ///< System in error state
    RECOVERING,          ///< System attempting recovery
    SHUTDOWN,            ///< System shutdown in progress
    MAINTENANCE          ///< System in maintenance mode
};

/**
 * @brief System configuration structure
 */
struct SystemConfig {
    // General settings
    uint32_t bootCount;              ///< Number of system boots
    uint32_t uptime;                 ///< System uptime in seconds
    uint32_t lastMaintenance;        ///< Last maintenance timestamp
    
    // Performance settings
    uint32_t healthCheckInterval;    ///< Health check interval (ms)
    uint32_t watchdogTimeout;        ///< Watchdog timeout (ms)
    uint32_t maxMemoryUsage;         ///< Maximum memory usage threshold
    
    // Error handling settings
    uint32_t maxErrorCount;          ///< Maximum errors before reboot
    uint32_t errorResetInterval;     ///< Error count reset interval (ms)
    bool autoRecovery;               ///< Enable automatic error recovery
    
    // Logging settings
    bool enableDebugLogging;         ///< Enable debug logging
    bool enableErrorLogging;         ///< Enable error logging
    uint32_t logRetentionDays;       ///< Log retention period
    
    // Default constructor with sensible defaults
    SystemConfig() : 
        bootCount(0),
        uptime(0),
        lastMaintenance(0),
        healthCheckInterval(30000),  // 30 seconds
        watchdogTimeout(30000),      // 30 seconds
        maxMemoryUsage(100000),      // 100KB
        maxErrorCount(50),
        errorResetInterval(3600000), // 1 hour
        autoRecovery(true),
        enableDebugLogging(false),
        enableErrorLogging(true),
        logRetentionDays(7) {}
};

/**
 * @brief System performance metrics
 */
struct SystemMetrics {
    uint32_t freeHeap;               ///< Free heap memory (bytes)
    uint32_t minFreeHeap;            ///< Minimum free heap since boot
    uint32_t cpuUsage;               ///< CPU usage percentage
    uint32_t taskCount;              ///< Number of active tasks
    uint32_t errorCount;             ///< Total error count
    uint32_t recoveryCount;          ///< Successful recovery count
    uint32_t uptime;                 ///< System uptime (seconds)
    float temperature;               ///< System temperature (°C)
};

/**
 * @brief Main system coordinator class
 * 
 * This class serves as the central coordinator for the entire system,
 * managing initialization, state transitions, and coordination between
 * all system modules.
 */
class SystemManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to SystemManager instance
     */
    static SystemManager& getInstance();
    
    /**
     * @brief Initialize the system
     * @return true if initialization successful, false otherwise
     * 
     * This method performs complete system initialization including:
     * - Hardware initialization
     * - Module initialization
     * - Configuration loading
     * - Security checks
     */
    bool initialize();
    
    /**
     * @brief Shutdown the system gracefully
     * @return true if shutdown successful, false otherwise
     */
    bool shutdown();
    
    /**
     * @brief Get current system state
     * @return Current system state
     */
    SystemState getState() const { return currentState; }
    
    /**
     * @brief Get system configuration
     * @return Reference to system configuration
     */
    const SystemConfig& getConfig() const { return config; }
    
    /**
     * @brief Update system configuration
     * @param newConfig New configuration
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const SystemConfig& newConfig);
    
    /**
     * @brief Get system metrics
     * @return Current system metrics
     */
    SystemMetrics getMetrics();
    
    /**
     * @brief Perform system health check
     * @return true if system healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Get system uptime
     * @return Uptime in seconds
     */
    uint32_t getUptime() const;
    
    /**
     * @brief Get boot count
     * @return Number of system boots
     */
    uint32_t getBootCount() const { return config.bootCount; }
    
    /**
     * @brief Check if system is ready
     * @return true if system ready, false otherwise
     */
    bool isReady() const { return currentState == SystemState::READY || currentState == SystemState::RUNNING; }
    
    /**
     * @brief Check if system is running
     * @return true if system running, false otherwise
     */
    bool isRunning() const { return currentState == SystemState::RUNNING; }
    
    /**
     * @brief Check if system is in error state
     * @return true if system in error, false otherwise
     */
    bool isInError() const { return currentState == SystemState::ERROR; }
    
    /**
     * @brief Request system reboot
     * @param reason Reason for reboot
     * @param delayMs Delay before reboot (ms)
     */
    void requestReboot(const String& reason = "", uint32_t delayMs = 1000);
    
    /**
     * @brief Enter maintenance mode
     * @return true if successful, false otherwise
     */
    bool enterMaintenanceMode();
    
    /**
     * @brief Exit maintenance mode
     * @return true if successful, false otherwise
     */
    bool exitMaintenanceMode();
    
    /**
     * @brief Get module references (for coordination)
     */
    WiFiManager* getWiFiManager() const { return wifiManager; }
    SensorManager* getSensorManager() const { return sensorManager; }
    StorageManager* getStorageManager() const { return storageManager; }
    DisplayManager* getDisplayManager() const { return displayManager; }
    ESPNowManager* getESPNowManager() const { return espNowManager; }
    SecurityManager* getSecurityManager() const { return securityManager; }
    
    /**
     * @brief Main system loop
     * 
     * This method should be called from the main loop() function.
     * It handles system monitoring, health checks, and state management.
     */
    void loop();
    
    /**
     * @brief Get system information as JSON string
     * @return JSON string with system information
     */
    String getSystemInfo();
    
    /**
     * @brief Get system status as human-readable string
     * @return Status string
     */
    String getStatusString();

private:
    // Singleton pattern - prevent external instantiation
    SystemManager();
    ~SystemManager();
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;
    
    // System state
    SystemState currentState;
    SystemConfig config;
    SystemMetrics metrics;
    
    // Module pointers
    WiFiManager* wifiManager;
    SensorManager* sensorManager;
    StorageManager* storageManager;
    DisplayManager* displayManager;
    ESPNowManager* espNowManager;
    SecurityManager* securityManager;
    
    // Timing and monitoring
    uint32_t lastHealthCheck;
    uint32_t lastMetricsUpdate;
    uint32_t lastErrorReset;
    uint32_t bootTime;
    bool rebootRequested;
    String rebootReason;
    uint32_t rebootDelay;
    
    // Preferences for persistent storage
    Preferences preferences;
    bool initialized;
    
    /**
     * @brief Initialize hardware components
     * @return true if successful, false otherwise
     */
    bool initializeHardware();
    
    /**
     * @brief Initialize system modules
     * @return true if successful, false otherwise
     */
    bool initializeModules();
    
    /**
     * @brief Load system configuration
     * @return true if successful, false otherwise
     */
    bool loadConfiguration();
    
    /**
     * @brief Save system configuration
     * @return true if successful, false otherwise
     */
    bool saveConfiguration();
    
    /**
     * @brief Update system state
     * @param newState New system state
     */
    void updateState(SystemState newState);
    
    /**
     * @brief Update system metrics
     */
    void updateMetrics();
    
    /**
     * @brief Perform system maintenance
     */
    void performMaintenance();
    
    /**
     * @brief Handle system errors
     */
    void handleSystemErrors();
    
    /**
     * @brief Reset error counters
     */
    void resetErrorCounters();
    
    /**
     * @brief Check memory usage
     * @return true if memory usage acceptable, false otherwise
     */
    bool checkMemoryUsage();
    
    /**
     * @brief Log system event
     * @param event Event description
     * @param severity Event severity
     */
    void logSystemEvent(const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
};

// Global system manager instance
extern SystemManager& systemManager;

// Convenience macros for system management
#define SYSTEM_INIT() systemManager.initialize()
#define SYSTEM_LOOP() systemManager.loop()
#define SYSTEM_READY() systemManager.isReady()
#define SYSTEM_RUNNING() systemManager.isRunning()
#define SYSTEM_ERROR() systemManager.isInError()
#define SYSTEM_REBOOT(reason) systemManager.requestReboot(reason)
#define SYSTEM_UPTIME() systemManager.getUptime()
#define SYSTEM_HEALTH_CHECK() systemManager.performHealthCheck()

#endif // SYSTEM_MANAGER_HPP 