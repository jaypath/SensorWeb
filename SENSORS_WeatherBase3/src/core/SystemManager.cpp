/**
 * @file SystemManager.cpp
 * @brief System manager implementation for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 */

#include "SystemManager.hpp"
#include "../network/WiFiManager.hpp"
#include "../sensors/SensorManager.hpp"
#include "../storage/StorageManager.hpp"
#include "../display/DisplayManager.hpp"
#include "../network/ESPNowManager.hpp"
#include "../security/SecurityManager.hpp"

// Global system manager instance
SystemManager& systemManager = SystemManager::getInstance();

SystemManager::SystemManager() : 
    currentState(SystemState::UNINITIALIZED),
    wifiManager(nullptr),
    sensorManager(nullptr),
    storageManager(nullptr),
    displayManager(nullptr),
    espNowManager(nullptr),
    securityManager(nullptr),
    lastHealthCheck(0),
    lastMetricsUpdate(0),
    lastErrorReset(0),
    bootTime(0),
    rebootRequested(false),
    rebootDelay(0),
    initialized(false) {
    
    // Initialize boot time
    bootTime = millis();
}

SystemManager::~SystemManager() {
    // Cleanup will be handled by shutdown()
}

SystemManager& SystemManager::getInstance() {
    static SystemManager instance;
    return instance;
}

bool SystemManager::initialize() {
    if (initialized) {
        return true;
    }
    
    ErrorHandler::log(ErrorCode::SYSTEM_INIT_START, ErrorSeverity::INFO, "System initialization started");
    
    updateState(SystemState::INITIALIZING);
    
    // Initialize preferences
    if (!preferences.begin("system", false)) {
        ErrorHandler::log(ErrorCode::SYSTEM_PREFS_INIT_FAILED, ErrorSeverity::ERROR, "Failed to initialize system preferences");
        updateState(SystemState::ERROR);
        return false;
    }
    
    // Load configuration
    if (!loadConfiguration()) {
        ErrorHandler::log(ErrorCode::SYSTEM_CONFIG_LOAD_FAILED, ErrorSeverity::WARNING, "Failed to load system configuration, using defaults");
    }
    
    // Increment boot count
    config.bootCount++;
    saveConfiguration();
    
    // Initialize hardware
    if (!initializeHardware()) {
        ErrorHandler::log(ErrorCode::SYSTEM_HARDWARE_INIT_FAILED, ErrorSeverity::ERROR, "Hardware initialization failed");
        updateState(SystemState::ERROR);
        return false;
    }
    
    // Initialize modules
    if (!initializeModules()) {
        ErrorHandler::log(ErrorCode::SYSTEM_MODULES_INIT_FAILED, ErrorSeverity::ERROR, "Module initialization failed");
        updateState(SystemState::ERROR);
        return false;
    }
    
    // Initialize metrics
    updateMetrics();
    
    initialized = true;
    updateState(SystemState::READY);
    
    ErrorHandler::log(ErrorCode::SYSTEM_INIT_SUCCESS, ErrorSeverity::INFO, 
        StringUtils::format("System initialized successfully. Boot count: %d", config.bootCount));
    
    return true;
}

bool SystemManager::shutdown() {
    if (!initialized) {
        return true;
    }
    
    ErrorHandler::log(ErrorCode::SYSTEM_SHUTDOWN_START, ErrorSeverity::INFO, "System shutdown started");
    
    updateState(SystemState::SHUTDOWN);
    
    // Save current configuration
    saveConfiguration();
    
    // Shutdown modules in reverse order
    if (espNowManager) {
        // ESPNowManager shutdown logic would go here
    }
    
    if (displayManager) {
        // DisplayManager shutdown logic would go here
    }
    
    if (sensorManager) {
        // SensorManager shutdown logic would go here
    }
    
    if (storageManager) {
        // StorageManager shutdown logic would go here
    }
    
    if (wifiManager) {
        // WiFiManager shutdown logic would go here
    }
    
    if (securityManager) {
        // SecurityManager shutdown logic would go here
    }
    
    // Close preferences
    preferences.end();
    
    initialized = false;
    updateState(SystemState::UNINITIALIZED);
    
    ErrorHandler::log(ErrorCode::SYSTEM_SHUTDOWN_SUCCESS, ErrorSeverity::INFO, "System shutdown completed");
    
    return true;
}

bool SystemManager::updateConfig(const SystemConfig& newConfig) {
    config = newConfig;
    return saveConfiguration();
}

SystemMetrics SystemManager::getMetrics() {
    updateMetrics();
    return metrics;
}

bool SystemManager::performHealthCheck() {
    uint32_t now = millis();
    
    // Check if it's time for health check
    if (now - lastHealthCheck < config.healthCheckInterval) {
        return true;
    }
    
    lastHealthCheck = now;
    
    bool healthy = true;
    
    // Check memory usage
    if (!checkMemoryUsage()) {
        ErrorHandler::log(ErrorCode::SYSTEM_MEMORY_LOW, ErrorSeverity::WARNING, "Low memory detected");
        healthy = false;
    }
    
    // Check module health
    if (wifiManager && !wifiManager->performHealthCheck()) {
        ErrorHandler::log(ErrorCode::SYSTEM_WIFI_UNHEALTHY, ErrorSeverity::WARNING, "WiFi health check failed");
        healthy = false;
    }
    
    if (sensorManager && !sensorManager->performHealthCheck()) {
        ErrorHandler::log(ErrorCode::SYSTEM_SENSORS_UNHEALTHY, ErrorSeverity::WARNING, "Sensor health check failed");
        healthy = false;
    }
    
    if (storageManager && !storageManager->performHealthCheck()) {
        ErrorHandler::log(ErrorCode::SYSTEM_STORAGE_UNHEALTHY, ErrorSeverity::WARNING, "Storage health check failed");
        healthy = false;
    }
    
    if (espNowManager && !espNowManager->performHealthCheck()) {
        ErrorHandler::log(ErrorCode::SYSTEM_ESPNOW_UNHEALTHY, ErrorSeverity::WARNING, "ESP-NOW health check failed");
        healthy = false;
    }
    
    // Handle system errors
    handleSystemErrors();
    
    // Update state based on health
    if (!healthy && currentState == SystemState::RUNNING) {
        updateState(SystemState::ERROR);
    } else if (healthy && currentState == SystemState::ERROR) {
        updateState(SystemState::RUNNING);
    }
    
    return healthy;
}

uint32_t SystemManager::getUptime() const {
    return (millis() - bootTime) / 1000;
}

void SystemManager::requestReboot(const String& reason, uint32_t delayMs) {
    rebootRequested = true;
    rebootReason = reason;
    rebootDelay = delayMs;
    
    ErrorHandler::log(ErrorCode::SYSTEM_REBOOT_REQUESTED, ErrorSeverity::INFO, 
        StringUtils::format("Reboot requested: %s (delay: %d ms)", reason.c_str(), delayMs));
}

bool SystemManager::enterMaintenanceMode() {
    if (currentState == SystemState::MAINTENANCE) {
        return true;
    }
    
    updateState(SystemState::MAINTENANCE);
    ErrorHandler::log(ErrorCode::SYSTEM_MAINTENANCE_ENTER, ErrorSeverity::INFO, "Entered maintenance mode");
    
    return true;
}

bool SystemManager::exitMaintenanceMode() {
    if (currentState != SystemState::MAINTENANCE) {
        return true;
    }
    
    updateState(SystemState::READY);
    ErrorHandler::log(ErrorCode::SYSTEM_MAINTENANCE_EXIT, ErrorSeverity::INFO, "Exited maintenance mode");
    
    return true;
}

void SystemManager::loop() {
    // Handle reboot request
    if (rebootRequested) {
        static uint32_t rebootStartTime = 0;
        if (rebootStartTime == 0) {
            rebootStartTime = millis();
        }
        
        if (millis() - rebootStartTime >= rebootDelay) {
            ErrorHandler::log(ErrorCode::SYSTEM_REBOOT_EXECUTING, ErrorSeverity::INFO, 
                StringUtils::format("Executing reboot: %s", rebootReason.c_str()));
            
            // Perform graceful shutdown
            shutdown();
            
            // Wait a moment for shutdown to complete
            delay(100);
            
            // Restart ESP32
            ESP.restart();
        }
        return;
    }
    
    // Update system state to running if ready
    if (currentState == SystemState::READY) {
        updateState(SystemState::RUNNING);
    }
    
    // Perform periodic tasks
    uint32_t now = millis();
    
    // Update metrics periodically
    if (now - lastMetricsUpdate >= 60000) { // Every minute
        updateMetrics();
        lastMetricsUpdate = now;
    }
    
    // Reset error counters periodically
    if (now - lastErrorReset >= config.errorResetInterval) {
        resetErrorCounters();
        lastErrorReset = now;
    }
    
    // Perform maintenance tasks
    if (now - config.lastMaintenance >= 86400000) { // Every 24 hours
        performMaintenance();
        config.lastMaintenance = now;
        saveConfiguration();
    }
    
    // Call module loops
    if (wifiManager) wifiManager->loop();
    if (sensorManager) sensorManager->loop();
    if (storageManager) storageManager->loop();
    if (displayManager) displayManager->loop();
    if (espNowManager) espNowManager->loop();
    if (securityManager) securityManager->loop();
}

String SystemManager::getSystemInfo() {
    String info = "{";
    info += "\"state\":\"" + getStatusString() + "\",";
    info += "\"uptime\":" + String(getUptime()) + ",";
    info += "\"bootCount\":" + String(config.bootCount) + ",";
    info += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    info += "\"minFreeHeap\":" + String(ESP.getMinFreeHeap()) + ",";
    info += "\"cpuFreq\":" + String(ESP.getCpuFreqMHz()) + ",";
    info += "\"chipId\":\"" + String(ESP.getChipId(), HEX) + "\",";
    info += "\"flashSize\":" + String(ESP.getFlashChipSize()) + ",";
    info += "\"sdkVersion\":\"" + String(ESP.getSdkVersion()) + "\"";
    info += "}";
    
    return info;
}

String SystemManager::getStatusString() {
    switch (currentState) {
        case SystemState::UNINITIALIZED: return "Uninitialized";
        case SystemState::INITIALIZING: return "Initializing";
        case SystemState::READY: return "Ready";
        case SystemState::RUNNING: return "Running";
        case SystemState::ERROR: return "Error";
        case SystemState::RECOVERING: return "Recovering";
        case SystemState::SHUTDOWN: return "Shutdown";
        case SystemState::MAINTENANCE: return "Maintenance";
        default: return "Unknown";
    }
}

bool SystemManager::initializeHardware() {
    // Initialize serial communication
    Serial.begin(115200);
    Serial.println("SENSORS_WeatherBase3 - Hardware Initialization");
    
    // Initialize random seed
    randomSeed(analogRead(0));
    
    // Set CPU frequency if needed
    // setCpuFrequencyMhz(80); // Example: Set to 80MHz for power saving
    
    ErrorHandler::log(ErrorCode::SYSTEM_HARDWARE_INIT_SUCCESS, ErrorSeverity::INFO, "Hardware initialized successfully");
    
    return true;
}

bool SystemManager::initializeModules() {
    // Initialize managers in dependency order
    storageManager = &StorageManager::getInstance();
    if (!storageManager->initialize()) {
        ErrorHandler::log(ErrorCode::SYSTEM_STORAGE_INIT_FAILED, ErrorSeverity::ERROR, "Storage manager initialization failed");
        return false;
    }
    
    securityManager = &SecurityManager::getInstance();
    if (!securityManager->initialize()) {
        ErrorHandler::log(ErrorCode::SYSTEM_SECURITY_INIT_FAILED, ErrorSeverity::ERROR, "Security manager initialization failed");
        return false;
    }
    
    wifiManager = &WiFiManager::getInstance();
    if (!wifiManager->initialize()) {
        ErrorHandler::log(ErrorCode::SYSTEM_WIFI_INIT_FAILED, ErrorSeverity::ERROR, "WiFi manager initialization failed");
        return false;
    }
    
    espNowManager = &ESPNowManager::getInstance();
    if (!espNowManager->initialize()) {
        ErrorHandler::log(ErrorCode::SYSTEM_ESPNOW_INIT_FAILED, ErrorSeverity::ERROR, "ESP-NOW manager initialization failed");
        return false;
    }
    
    sensorManager = &SensorManager::getInstance();
    if (!sensorManager->initialize()) {
        ErrorHandler::log(ErrorCode::SYSTEM_SENSORS_INIT_FAILED, ErrorSeverity::ERROR, "Sensor manager initialization failed");
        return false;
    }
    
    displayManager = &DisplayManager::getInstance();
    if (!displayManager->initialize()) {
        ErrorHandler::log(ErrorCode::SYSTEM_DISPLAY_INIT_FAILED, ErrorSeverity::ERROR, "Display manager initialization failed");
        return false;
    }
    
    ErrorHandler::log(ErrorCode::SYSTEM_MODULES_INIT_SUCCESS, ErrorSeverity::INFO, "All modules initialized successfully");
    
    return true;
}

bool SystemManager::loadConfiguration() {
    config.bootCount = preferences.getUInt("bootCount", 0);
    config.uptime = preferences.getUInt("uptime", 0);
    config.lastMaintenance = preferences.getUInt("lastMaintenance", 0);
    config.healthCheckInterval = preferences.getUInt("healthCheckInterval", 30000);
    config.watchdogTimeout = preferences.getUInt("watchdogTimeout", 30000);
    config.maxMemoryUsage = preferences.getUInt("maxMemoryUsage", 100000);
    config.maxErrorCount = preferences.getUInt("maxErrorCount", 50);
    config.errorResetInterval = preferences.getUInt("errorResetInterval", 3600000);
    config.autoRecovery = preferences.getBool("autoRecovery", true);
    config.enableDebugLogging = preferences.getBool("enableDebugLogging", false);
    config.enableErrorLogging = preferences.getBool("enableErrorLogging", true);
    config.logRetentionDays = preferences.getUInt("logRetentionDays", 7);
    
    return true;
}

bool SystemManager::saveConfiguration() {
    preferences.putUInt("bootCount", config.bootCount);
    preferences.putUInt("uptime", config.uptime);
    preferences.putUInt("lastMaintenance", config.lastMaintenance);
    preferences.putUInt("healthCheckInterval", config.healthCheckInterval);
    preferences.putUInt("watchdogTimeout", config.watchdogTimeout);
    preferences.putUInt("maxMemoryUsage", config.maxMemoryUsage);
    preferences.putUInt("maxErrorCount", config.maxErrorCount);
    preferences.putUInt("errorResetInterval", config.errorResetInterval);
    preferences.putBool("autoRecovery", config.autoRecovery);
    preferences.putBool("enableDebugLogging", config.enableDebugLogging);
    preferences.putBool("enableErrorLogging", config.enableErrorLogging);
    preferences.putUInt("logRetentionDays", config.logRetentionDays);
    
    return true;
}

void SystemManager::updateState(SystemState newState) {
    if (currentState != newState) {
        SystemState oldState = currentState;
        currentState = newState;
        
        logSystemEvent(StringUtils::format("State changed from %s to %s", 
            getStatusString().c_str(), getStatusString().c_str()), ErrorSeverity::INFO);
    }
}

void SystemManager::updateMetrics() {
    metrics.freeHeap = ESP.getFreeHeap();
    metrics.minFreeHeap = ESP.getMinFreeHeap();
    metrics.cpuUsage = 0; // Would need to implement CPU usage monitoring
    metrics.taskCount = 0; // Would need to implement task counting
    metrics.errorCount = ErrorHandler::getTotalErrorCount();
    metrics.recoveryCount = ErrorHandler::getRecoveryCount();
    metrics.uptime = getUptime();
    metrics.temperature = 0; // Would need to implement temperature monitoring
    
    // Update minimum free heap
    if (metrics.freeHeap < metrics.minFreeHeap) {
        metrics.minFreeHeap = metrics.freeHeap;
    }
    
    metrics.lastUpdateTime = millis();
}

void SystemManager::performMaintenance() {
    ErrorHandler::log(ErrorCode::SYSTEM_MAINTENANCE_START, ErrorSeverity::INFO, "Starting system maintenance");
    
    // Perform storage cleanup
    if (storageManager) {
        storageManager->cleanupOldFiles(86400 * config.logRetentionDays); // Clean up old files
    }
    
    // Perform error log cleanup
    ErrorHandler::cleanupOldErrors(86400 * config.logRetentionDays);
    
    // Update uptime
    config.uptime += getUptime();
    
    ErrorHandler::log(ErrorCode::SYSTEM_MAINTENANCE_SUCCESS, ErrorSeverity::INFO, "System maintenance completed");
}

void SystemManager::handleSystemErrors() {
    uint32_t totalErrors = ErrorHandler::getTotalErrorCount();
    
    if (totalErrors > config.maxErrorCount) {
        if (config.autoRecovery) {
            ErrorHandler::log(ErrorCode::SYSTEM_AUTO_RECOVERY, ErrorSeverity::WARNING, 
                "Too many errors, attempting auto-recovery");
            
            // Attempt recovery by resetting error counters
            resetErrorCounters();
            
            // If still too many errors, request reboot
            if (ErrorHandler::getTotalErrorCount() > config.maxErrorCount) {
                requestReboot("Too many errors after recovery attempt");
            }
        } else {
            ErrorHandler::log(ErrorCode::SYSTEM_ERROR_THRESHOLD, ErrorSeverity::ERROR, 
                "Error threshold exceeded, manual intervention required");
        }
    }
}

void SystemManager::resetErrorCounters() {
    ErrorHandler::resetErrorCounters();
    lastErrorReset = millis();
}

bool SystemManager::checkMemoryUsage() {
    uint32_t freeHeap = ESP.getFreeHeap();
    return freeHeap > config.maxMemoryUsage;
}

void SystemManager::logSystemEvent(const String& event, ErrorSeverity severity) {
    ErrorHandler::log(ErrorCode::SYSTEM_EVENT, severity, event);
} 