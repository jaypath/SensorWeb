/**
 * @file ErrorHandler.cpp
 * @brief Implementation of the comprehensive error handling framework
 * @author System Refactoring Team
 * @date 2024
 */

#include "ErrorHandler.hpp"
#include <esp_system.h>
#include <esp_task_wdt.h>

// Static member initialization
ErrorStats ErrorHandler::stats = {};
ErrorContext ErrorHandler::lastError = {};
Preferences ErrorHandler::preferences;
bool ErrorHandler::initialized = false;

// Maximum retry counts for different error types
static const uint32_t MAX_RETRIES_BY_CODE[] = {
    0,  // SUCCESS
    5,  // WIFI_CONNECTION_FAILED
    3,  // WIFI_CREDENTIALS_INVALID
    3,  // SENSOR_READ_FAILED
    2,  // SENSOR_INIT_FAILED
    3,  // SD_CARD_ERROR
    2,  // SD_CARD_MOUNT_FAILED
    3,  // SD_CARD_WRITE_FAILED
    3,  // SD_CARD_READ_FAILED
    1,  // MEMORY_ERROR
    1,  // MEMORY_FRAGMENTATION
    5,  // NETWORK_ERROR
    3,  // HTTP_REQUEST_FAILED
    3,  // ESPNOW_ERROR
    3,  // ESPNOW_SEND_FAILED
    3,  // ESPNOW_RECEIVE_FAILED
    1,  // SECURITY_ERROR
    1,  // ENCRYPTION_FAILED
    1,  // DECRYPTION_FAILED
    2,  // DISPLAY_ERROR
    2,  // DISPLAY_INIT_FAILED
    3,  // TIME_SYNC_FAILED
    3,  // NTP_SYNC_FAILED
    2,  // CONFIG_ERROR
    2,  // CONFIG_LOAD_FAILED
    2,  // CONFIG_SAVE_FAILED
    2,  // OTA_UPDATE_FAILED
    1,  // WATCHDOG_TIMEOUT
    1,  // SYSTEM_CRITICAL_ERROR
    1   // UNKNOWN_ERROR
};

// Error severity mapping
static const ErrorSeverity SEVERITY_BY_CODE[] = {
    ErrorSeverity::INFO,      // SUCCESS
    ErrorSeverity::ERROR,     // WIFI_CONNECTION_FAILED
    ErrorSeverity::ERROR,     // WIFI_CREDENTIALS_INVALID
    ErrorSeverity::WARNING,   // SENSOR_READ_FAILED
    ErrorSeverity::ERROR,     // SENSOR_INIT_FAILED
    ErrorSeverity::ERROR,     // SD_CARD_ERROR
    ErrorSeverity::CRITICAL,  // SD_CARD_MOUNT_FAILED
    ErrorSeverity::ERROR,     // SD_CARD_WRITE_FAILED
    ErrorSeverity::ERROR,     // SD_CARD_READ_FAILED
    ErrorSeverity::CRITICAL,  // MEMORY_ERROR
    ErrorSeverity::WARNING,   // MEMORY_FRAGMENTATION
    ErrorSeverity::ERROR,     // NETWORK_ERROR
    ErrorSeverity::ERROR,     // HTTP_REQUEST_FAILED
    ErrorSeverity::ERROR,     // ESPNOW_ERROR
    ErrorSeverity::WARNING,   // ESPNOW_SEND_FAILED
    ErrorSeverity::WARNING,   // ESPNOW_RECEIVE_FAILED
    ErrorSeverity::CRITICAL,  // SECURITY_ERROR
    ErrorSeverity::CRITICAL,  // ENCRYPTION_FAILED
    ErrorSeverity::CRITICAL,  // DECRYPTION_FAILED
    ErrorSeverity::WARNING,   // DISPLAY_ERROR
    ErrorSeverity::ERROR,     // DISPLAY_INIT_FAILED
    ErrorSeverity::WARNING,   // TIME_SYNC_FAILED
    ErrorSeverity::WARNING,   // NTP_SYNC_FAILED
    ErrorSeverity::ERROR,     // CONFIG_ERROR
    ErrorSeverity::ERROR,     // CONFIG_LOAD_FAILED
    ErrorSeverity::ERROR,     // CONFIG_SAVE_FAILED
    ErrorSeverity::ERROR,     // OTA_UPDATE_FAILED
    ErrorSeverity::FATAL,     // WATCHDOG_TIMEOUT
    ErrorSeverity::FATAL,     // SYSTEM_CRITICAL_ERROR
    ErrorSeverity::ERROR      // UNKNOWN_ERROR
};

bool ErrorHandler::initialize() {
    if (initialized) {
        return true;
    }
    
    // Initialize preferences for persistent storage
    if (!preferences.begin("error_handler", false)) {
        #ifdef _DEBUG
        Serial.println("ErrorHandler: Failed to initialize preferences");
        #endif
        return false;
    }
    
    // Load existing error statistics
    loadErrorStats();
    
    // Reset statistics if this is a fresh boot
    uint32_t lastBootTime = preferences.getULong("last_boot", 0);
    uint32_t currentTime = millis();
    if (lastBootTime == 0 || (currentTime - lastBootTime) > 86400000) { // 24 hours
        clearErrorStats();
    }
    preferences.putULong("last_boot", currentTime);
    
    initialized = true;
    
    #ifdef _DEBUG
    Serial.println("ErrorHandler: Initialized successfully");
    #endif
    
    return true;
}

bool ErrorHandler::handleError(ErrorCode code, 
                              const String& message,
                              const String& module,
                              const String& function,
                              uint32_t line,
                              const String& file) {
    
    if (!initialized) {
        initialize();
    }
    
    // Create error context
    ErrorContext context;
    context.code = code;
    context.severity = getErrorSeverity(code);
    context.message = message;
    context.module = module;
    context.function = function;
    context.line = line;
    context.file = file;
    context.timestamp = millis();
    context.maxRetries = MAX_RETRIES_BY_CODE[static_cast<uint8_t>(code)];
    
    // Check if we should retry
    if (shouldRetry(code)) {
        context.retryCount = stats.errorsByCode[static_cast<uint8_t>(code)] + 1;
        if (context.retryCount <= context.maxRetries) {
            #ifdef _DEBUG
            Serial.printf("ErrorHandler: Retrying error %d (attempt %d/%d)\n", 
                         static_cast<int>(code), context.retryCount, context.maxRetries);
            #endif
            updateStats(context);
            return true; // Allow retry
        }
    }
    
    // Update statistics
    updateStats(context);
    
    // Store last error
    lastError = context;
    
    // Log the error
    logToOutput(context);
    
    // Check if immediate reboot is required
    if (requiresReboot(code)) {
        #ifdef _DEBUG
        Serial.printf("ErrorHandler: Critical error detected, rebooting in 5 seconds\n");
        #endif
        saveErrorStats();
        delay(5000);
        esp_restart();
        return false;
    }
    
    // Attempt recovery
    if (attemptRecovery(context)) {
        stats.recoveryCount++;
        #ifdef _DEBUG
        Serial.printf("ErrorHandler: Recovery successful for error %d\n", static_cast<int>(code));
        #endif
        return true;
    }
    
    // Save statistics
    saveErrorStats();
    
    return false;
}

void ErrorHandler::logError(ErrorCode code,
                           const String& message,
                           ErrorSeverity severity,
                           const String& module,
                           const String& function,
                           uint32_t line,
                           const String& file) {
    
    if (!initialized) {
        initialize();
    }
    
    ErrorContext context;
    context.code = code;
    context.severity = severity;
    context.message = message;
    context.module = module;
    context.function = function;
    context.line = line;
    context.file = file;
    context.timestamp = millis();
    context.retryCount = 0;
    context.maxRetries = 0;
    
    updateStats(context);
    lastError = context;
    logToOutput(context);
}

bool ErrorHandler::shouldRetry(ErrorCode code) {
    uint8_t codeIndex = static_cast<uint8_t>(code);
    if (codeIndex >= sizeof(MAX_RETRIES_BY_CODE) / sizeof(MAX_RETRIES_BY_CODE[0])) {
        return false;
    }
    
    // Don't retry critical or fatal errors
    ErrorSeverity severity = getErrorSeverity(code);
    if (severity >= ErrorSeverity::CRITICAL) {
        return false;
    }
    
    // Check if we've exceeded max retries
    uint32_t currentErrors = stats.errorsByCode[codeIndex];
    uint32_t maxRetries = MAX_RETRIES_BY_CODE[codeIndex];
    
    return currentErrors < maxRetries;
}

void ErrorHandler::resetErrorCount(ErrorCode code) {
    uint8_t codeIndex = static_cast<uint8_t>(code);
    if (codeIndex < sizeof(stats.errorsByCode) / sizeof(stats.errorsByCode[0])) {
        stats.errorsByCode[codeIndex] = 0;
        saveErrorStats();
    }
}

const ErrorStats& ErrorHandler::getErrorStats() {
    return stats;
}

const ErrorContext& ErrorHandler::getLastError() {
    return lastError;
}

void ErrorHandler::clearErrorStats() {
    memset(&stats, 0, sizeof(stats));
    saveErrorStats();
}

String ErrorHandler::getErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Operation completed successfully";
        case ErrorCode::WIFI_CONNECTION_FAILED: return "WiFi connection failed";
        case ErrorCode::WIFI_CREDENTIALS_INVALID: return "WiFi credentials invalid";
        case ErrorCode::SENSOR_READ_FAILED: return "Sensor read failed";
        case ErrorCode::SENSOR_INIT_FAILED: return "Sensor initialization failed";
        case ErrorCode::SD_CARD_ERROR: return "SD card error";
        case ErrorCode::SD_CARD_MOUNT_FAILED: return "SD card mount failed";
        case ErrorCode::SD_CARD_WRITE_FAILED: return "SD card write failed";
        case ErrorCode::SD_CARD_READ_FAILED: return "SD card read failed";
        case ErrorCode::MEMORY_ERROR: return "Memory error";
        case ErrorCode::MEMORY_FRAGMENTATION: return "Memory fragmentation";
        case ErrorCode::NETWORK_ERROR: return "Network error";
        case ErrorCode::HTTP_REQUEST_FAILED: return "HTTP request failed";
        case ErrorCode::ESPNOW_ERROR: return "ESPNow error";
        case ErrorCode::ESPNOW_SEND_FAILED: return "ESPNow send failed";
        case ErrorCode::ESPNOW_RECEIVE_FAILED: return "ESPNow receive failed";
        case ErrorCode::SECURITY_ERROR: return "Security error";
        case ErrorCode::ENCRYPTION_FAILED: return "Encryption failed";
        case ErrorCode::DECRYPTION_FAILED: return "Decryption failed";
        case ErrorCode::DISPLAY_ERROR: return "Display error";
        case ErrorCode::DISPLAY_INIT_FAILED: return "Display initialization failed";
        case ErrorCode::TIME_SYNC_FAILED: return "Time sync failed";
        case ErrorCode::NTP_SYNC_FAILED: return "NTP sync failed";
        case ErrorCode::CONFIG_ERROR: return "Configuration error";
        case ErrorCode::CONFIG_LOAD_FAILED: return "Configuration load failed";
        case ErrorCode::CONFIG_SAVE_FAILED: return "Configuration save failed";
        case ErrorCode::OTA_UPDATE_FAILED: return "OTA update failed";
        case ErrorCode::WATCHDOG_TIMEOUT: return "Watchdog timeout";
        case ErrorCode::SYSTEM_CRITICAL_ERROR: return "System critical error";
        case ErrorCode::UNKNOWN_ERROR: return "Unknown error";
        default: return "Undefined error";
    }
}

ErrorSeverity ErrorHandler::getErrorSeverity(ErrorCode code) {
    uint8_t codeIndex = static_cast<uint8_t>(code);
    if (codeIndex < sizeof(SEVERITY_BY_CODE) / sizeof(SEVERITY_BY_CODE[0])) {
        return SEVERITY_BY_CODE[codeIndex];
    }
    return ErrorSeverity::ERROR;
}

bool ErrorHandler::shouldReboot() {
    // Check for fatal errors
    if (stats.errorsBySeverity[static_cast<uint8_t>(ErrorSeverity::FATAL)] > 0) {
        return true;
    }
    
    // Check for too many critical errors
    if (stats.errorsBySeverity[static_cast<uint8_t>(ErrorSeverity::CRITICAL)] > 5) {
        return true;
    }
    
    // Check for memory issues
    if (stats.errorsByCode[static_cast<uint8_t>(ErrorCode::MEMORY_ERROR)] > 3) {
        return true;
    }
    
    // Check for watchdog timeouts
    if (stats.errorsByCode[static_cast<uint8_t>(ErrorCode::WATCHDOG_TIMEOUT)] > 0) {
        return true;
    }
    
    return false;
}

bool ErrorHandler::performHealthCheck() {
    // Check free memory
    size_t freeHeap = esp_get_free_heap_size();
    if (freeHeap < 10000) { // Less than 10KB free
        logError(ErrorCode::MEMORY_FRAGMENTATION, 
                "Low memory detected: " + String(freeHeap) + " bytes free",
                ErrorSeverity::WARNING);
        return false;
    }
    
    // Check error statistics
    if (stats.totalErrors > 100) {
        logError(ErrorCode::SYSTEM_CRITICAL_ERROR,
                "Too many errors detected: " + String(stats.totalErrors),
                ErrorSeverity::CRITICAL);
        return false;
    }
    
    return true;
}

bool ErrorHandler::saveErrorStats() {
    if (!preferences.isValid()) {
        return false;
    }
    
    preferences.putULong("total_errors", stats.totalErrors);
    preferences.putULong("last_error_time", stats.lastErrorTime);
    preferences.putULong("recovery_count", stats.recoveryCount);
    preferences.putULong("reboot_count", stats.rebootCount);
    
    // Save error counts by code
    for (int i = 0; i < 32; i++) {
        String key = "errors_code_" + String(i);
        preferences.putULong(key.c_str(), stats.errorsByCode[i]);
    }
    
    // Save error counts by severity
    for (int i = 0; i < 6; i++) {
        String key = "errors_severity_" + String(i);
        preferences.putULong(key.c_str(), stats.errorsBySeverity[i]);
    }
    
    return true;
}

bool ErrorHandler::loadErrorStats() {
    if (!preferences.isValid()) {
        return false;
    }
    
    stats.totalErrors = preferences.getULong("total_errors", 0);
    stats.lastErrorTime = preferences.getULong("last_error_time", 0);
    stats.recoveryCount = preferences.getULong("recovery_count", 0);
    stats.rebootCount = preferences.getULong("reboot_count", 0);
    
    // Load error counts by code
    for (int i = 0; i < 32; i++) {
        String key = "errors_code_" + String(i);
        stats.errorsByCode[i] = preferences.getULong(key.c_str(), 0);
    }
    
    // Load error counts by severity
    for (int i = 0; i < 6; i++) {
        String key = "errors_severity_" + String(i);
        stats.errorsBySeverity[i] = preferences.getULong(key.c_str(), 0);
    }
    
    return true;
}

bool ErrorHandler::attemptRecovery(const ErrorContext& context) {
    switch (context.code) {
        case ErrorCode::WIFI_CONNECTION_FAILED:
            // WiFi recovery is handled by WiFiManager
            return true;
            
        case ErrorCode::SENSOR_READ_FAILED:
            // Sensor recovery is handled by SensorManager
            return true;
            
        case ErrorCode::SD_CARD_ERROR:
            // SD card recovery is handled by StorageManager
            return true;
            
        case ErrorCode::NETWORK_ERROR:
            // Network recovery is handled by NetworkManager
            return true;
            
        default:
            // No automatic recovery for other errors
            return false;
    }
}

void ErrorHandler::updateStats(const ErrorContext& context) {
    stats.totalErrors++;
    stats.lastErrorTime = context.timestamp;
    
    uint8_t codeIndex = static_cast<uint8_t>(context.code);
    if (codeIndex < sizeof(stats.errorsByCode) / sizeof(stats.errorsByCode[0])) {
        stats.errorsByCode[codeIndex]++;
    }
    
    uint8_t severityIndex = static_cast<uint8_t>(context.severity);
    if (severityIndex < sizeof(stats.errorsBySeverity) / sizeof(stats.errorsBySeverity[0])) {
        stats.errorsBySeverity[severityIndex]++;
    }
}

String ErrorHandler::getRecoveryStrategy(ErrorCode code) {
    switch (code) {
        case ErrorCode::WIFI_CONNECTION_FAILED:
            return "Retry with exponential backoff";
        case ErrorCode::SENSOR_READ_FAILED:
            return "Retry sensor reading";
        case ErrorCode::SD_CARD_ERROR:
            return "Remount SD card";
        case ErrorCode::NETWORK_ERROR:
            return "Retry network operation";
        case ErrorCode::MEMORY_ERROR:
            return "Free memory and retry";
        default:
            return "No automatic recovery available";
    }
}

bool ErrorHandler::requiresReboot(ErrorCode code) {
    return code == ErrorCode::WATCHDOG_TIMEOUT ||
           code == ErrorCode::SYSTEM_CRITICAL_ERROR ||
           code == ErrorCode::SECURITY_ERROR;
}

void ErrorHandler::logToOutput(const ErrorContext& context) {
    String severityStr;
    switch (context.severity) {
        case ErrorSeverity::DEBUG: severityStr = "DEBUG"; break;
        case ErrorSeverity::INFO: severityStr = "INFO"; break;
        case ErrorSeverity::WARNING: severityStr = "WARN"; break;
        case ErrorSeverity::ERROR: severityStr = "ERROR"; break;
        case ErrorSeverity::CRITICAL: severityStr = "CRIT"; break;
        case ErrorSeverity::FATAL: severityStr = "FATAL"; break;
    }
    
    String logMessage = String("[") + severityStr + "] ";
    logMessage += "Error " + String(static_cast<int>(context.code)) + ": ";
    logMessage += context.message;
    
    if (!context.module.isEmpty()) {
        logMessage += " (Module: " + context.module;
        if (!context.function.isEmpty()) {
            logMessage += ", Function: " + context.function;
        }
        if (context.line > 0) {
            logMessage += ", Line: " + String(context.line);
        }
        logMessage += ")";
    }
    
    #ifdef _DEBUG
    Serial.println(logMessage);
    #endif
    
    // TODO: Add logging to SD card or other persistent storage
    // TODO: Add remote logging capabilities
} 