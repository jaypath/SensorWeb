/**
 * @file ErrorHandler.hpp
 * @brief Comprehensive error handling framework for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides a centralized error handling system with:
 * - Standardized error codes
 * - Error logging and tracking
 * - Automatic retry mechanisms
 * - Recovery strategies
 * - Performance monitoring
 */

#ifndef ERROR_HANDLER_HPP
#define ERROR_HANDLER_HPP

#include <Arduino.h>
#include <Preferences.h>

/**
 * @brief Standardized error codes for the system
 * 
 * These codes provide consistent error identification across all modules.
 * Each code represents a specific type of error that can occur in the system.
 */
enum class ErrorCode : uint8_t {
    SUCCESS = 0,                    ///< Operation completed successfully
    WIFI_CONNECTION_FAILED,         ///< WiFi connection attempt failed
    WIFI_CREDENTIALS_INVALID,       ///< WiFi credentials are invalid or missing
    SENSOR_READ_FAILED,             ///< Sensor reading operation failed
    SENSOR_INIT_FAILED,             ///< Sensor initialization failed
    SD_CARD_ERROR,                  ///< SD card operation failed
    SD_CARD_MOUNT_FAILED,           ///< SD card mounting failed
    SD_CARD_WRITE_FAILED,           ///< SD card write operation failed
    SD_CARD_READ_FAILED,            ///< SD card read operation failed
    MEMORY_ERROR,                   ///< Memory allocation or access error
    MEMORY_FRAGMENTATION,           ///< Memory fragmentation detected
    NETWORK_ERROR,                  ///< General network communication error
    HTTP_REQUEST_FAILED,            ///< HTTP request failed
    ESPNOW_ERROR,                   ///< ESPNow communication error
    ESPNOW_SEND_FAILED,             ///< ESPNow message send failed
    ESPNOW_RECEIVE_FAILED,          ///< ESPNow message receive failed
    SECURITY_ERROR,                 ///< Security-related error
    ENCRYPTION_FAILED,              ///< Encryption operation failed
    DECRYPTION_FAILED,              ///< Decryption operation failed
    DISPLAY_ERROR,                  ///< Display/TFT operation failed
    DISPLAY_INIT_FAILED,            ///< Display initialization failed
    TIME_SYNC_FAILED,               ///< Time synchronization failed
    NTP_SYNC_FAILED,                ///< NTP time sync failed
    CONFIG_ERROR,                   ///< Configuration error
    CONFIG_LOAD_FAILED,             ///< Configuration loading failed
    CONFIG_SAVE_FAILED,             ///< Configuration saving failed
    OTA_UPDATE_FAILED,              ///< OTA update failed
    WATCHDOG_TIMEOUT,               ///< Watchdog timer timeout
    SYSTEM_CRITICAL_ERROR,          ///< Critical system error requiring reboot
    UNKNOWN_ERROR                   ///< Unknown or unclassified error
};

/**
 * @brief Error severity levels for logging and handling
 */
enum class ErrorSeverity : uint8_t {
    DEBUG = 0,      ///< Debug information
    INFO = 1,       ///< Informational message
    WARNING = 2,    ///< Warning - system can continue
    ERROR = 3,      ///< Error - operation failed but system can recover
    CRITICAL = 4,   ///< Critical error - system may need reboot
    FATAL = 5       ///< Fatal error - system must reboot immediately
};

/**
 * @brief Error context information
 */
struct ErrorContext {
    ErrorCode code;                 ///< Error code
    ErrorSeverity severity;         ///< Error severity
    String message;                 ///< Human-readable error message
    String module;                  ///< Module where error occurred
    String function;                ///< Function where error occurred
    uint32_t timestamp;             ///< Error timestamp
    uint32_t line;                  ///< Line number where error occurred
    String file;                    ///< File where error occurred
    uint32_t retryCount;            ///< Number of retry attempts
    uint32_t maxRetries;            ///< Maximum retry attempts allowed
};

/**
 * @brief Error statistics tracking
 */
struct ErrorStats {
    uint32_t totalErrors;           ///< Total number of errors
    uint32_t errorsByCode[32];      ///< Error count by error code
    uint32_t errorsBySeverity[6];   ///< Error count by severity level
    uint32_t lastErrorTime;         ///< Timestamp of last error
    uint32_t recoveryCount;         ///< Number of successful recoveries
    uint32_t rebootCount;           ///< Number of reboots due to errors
};

/**
 * @brief Comprehensive error handling system
 * 
 * This class provides centralized error handling with:
 * - Error logging and tracking
 * - Automatic retry mechanisms
 * - Recovery strategies
 * - Performance monitoring
 * - Error statistics
 */
class ErrorHandler {
public:
    /**
     * @brief Initialize the error handler
     * @return true if initialization successful, false otherwise
     */
    static bool initialize();
    
    /**
     * @brief Handle an error with automatic recovery
     * @param code Error code
     * @param message Error message
     * @param module Module name where error occurred
     * @param function Function name where error occurred
     * @param line Line number where error occurred
     * @param file File name where error occurred
     * @return true if error was handled successfully, false otherwise
     */
    static bool handleError(ErrorCode code, 
                           const String& message,
                           const String& module = "",
                           const String& function = "",
                           uint32_t line = 0,
                           const String& file = "");
    
    /**
     * @brief Log an error without automatic handling
     * @param code Error code
     * @param message Error message
     * @param severity Error severity level
     * @param module Module name where error occurred
     * @param function Function name where error occurred
     * @param line Line number where error occurred
     * @param file File name where error occurred
     */
    static void logError(ErrorCode code,
                        const String& message,
                        ErrorSeverity severity = ErrorSeverity::ERROR,
                        const String& module = "",
                        const String& function = "",
                        uint32_t line = 0,
                        const String& file = "");
    
    /**
     * @brief Check if an error should trigger a retry
     * @param code Error code to check
     * @return true if retry should be attempted, false otherwise
     */
    static bool shouldRetry(ErrorCode code);
    
    /**
     * @brief Reset error count for a specific error code
     * @param code Error code to reset
     */
    static void resetErrorCount(ErrorCode code);
    
    /**
     * @brief Get error statistics
     * @return Reference to error statistics
     */
    static const ErrorStats& getErrorStats();
    
    /**
     * @brief Get error context for the last error
     * @return Reference to last error context
     */
    static const ErrorContext& getLastError();
    
    /**
     * @brief Clear all error statistics
     */
    static void clearErrorStats();
    
    /**
     * @brief Get human-readable error message for error code
     * @param code Error code
     * @return Human-readable error message
     */
    static String getErrorMessage(ErrorCode code);
    
    /**
     * @brief Get error severity for error code
     * @param code Error code
     * @return Error severity level
     */
    static ErrorSeverity getErrorSeverity(ErrorCode code);
    
    /**
     * @brief Check if system should reboot due to errors
     * @return true if reboot is recommended, false otherwise
     */
    static bool shouldReboot();
    
    /**
     * @brief Perform system health check
     * @return true if system is healthy, false otherwise
     */
    static bool performHealthCheck();
    
    /**
     * @brief Save error statistics to persistent storage
     * @return true if save successful, false otherwise
     */
    static bool saveErrorStats();
    
    /**
     * @brief Load error statistics from persistent storage
     * @return true if load successful, false otherwise
     */
    static bool loadErrorStats();

private:
    static ErrorStats stats;                    ///< Error statistics
    static ErrorContext lastError;              ///< Last error context
    static Preferences preferences;             ///< Preferences for persistent storage
    static bool initialized;                    ///< Initialization flag
    
    /**
     * @brief Attempt automatic recovery for an error
     * @param context Error context
     * @return true if recovery successful, false otherwise
     */
    static bool attemptRecovery(const ErrorContext& context);
    
    /**
     * @brief Update error statistics
     * @param context Error context
     */
    static void updateStats(const ErrorContext& context);
    
    /**
     * @brief Determine recovery strategy for error
     * @param code Error code
     * @return Recovery strategy string
     */
    static String getRecoveryStrategy(ErrorCode code);
    
    /**
     * @brief Check if error requires immediate reboot
     * @param code Error code
     * @return true if reboot required, false otherwise
     */
    static bool requiresReboot(ErrorCode code);
    
    /**
     * @brief Log error to appropriate output
     * @param context Error context
     */
    static void logToOutput(const ErrorContext& context);
};

// Convenience macros for error handling
#define ERROR_HANDLE(code, message) \
    ErrorHandler::handleError(code, message, __FILE__, __FUNCTION__, __LINE__)

#define ERROR_LOG(code, message, severity) \
    ErrorHandler::logError(code, message, severity, __FILE__, __FUNCTION__, __LINE__)

#define ERROR_CHECK(condition, code, message) \
    if (!(condition)) { \
        ERROR_HANDLE(code, message); \
        return false; \
    }

#define ERROR_RETURN(condition, code, message, return_value) \
    if (!(condition)) { \
        ERROR_HANDLE(code, message); \
        return return_value; \
    }

#endif // ERROR_HANDLER_HPP 