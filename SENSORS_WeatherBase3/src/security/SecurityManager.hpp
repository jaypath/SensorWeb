/**
 * @file SecurityManager.hpp
 * @brief Security management for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides comprehensive security functionality including:
 * - Data encryption and decryption
 * - Authentication and authorization
 * - Access control and permissions
 * - Security monitoring and logging
 * - Certificate management
 * - Secure communication protocols
 */

#ifndef SECURITY_MANAGER_HPP
#define SECURITY_MANAGER_HPP

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include <map>
#include <functional>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations
class SystemManager;
class StorageManager;

/**
 * @brief Security levels
 */
enum class SecurityLevel : uint8_t {
    NONE = 0,           ///< No security
    LOW = 1,            ///< Basic security
    MEDIUM = 2,         ///< Standard security
    HIGH = 3,           ///< High security
    MAXIMUM = 4         ///< Maximum security
};

/**
 * @brief Authentication methods
 */
enum class AuthMethod : uint8_t {
    NONE = 0,           ///< No authentication
    PASSWORD = 1,       ///< Password authentication
    TOKEN = 2,          ///< Token-based authentication
    CERTIFICATE = 3,    ///< Certificate-based authentication
    MULTI_FACTOR = 4    ///< Multi-factor authentication
};

/**
 * @brief Permission types
 */
enum class Permission : uint8_t {
    READ = 0x01,        ///< Read permission
    WRITE = 0x02,       ///< Write permission
    DELETE = 0x04,      ///< Delete permission
    ADMIN = 0x08,       ///< Administrative permission
    CONFIG = 0x10,      ///< Configuration permission
    ALL = 0x1F          ///< All permissions
};

/**
 * @brief User roles
 */
enum class UserRole : uint8_t {
    GUEST = 0,          ///< Guest user (read-only)
    USER = 1,           ///< Regular user
    OPERATOR = 2,       ///< System operator
    ADMIN = 3,          ///< Administrator
    SUPER_ADMIN = 4     ///< Super administrator
};

/**
 * @brief Security event types
 */
enum class SecurityEventType : uint8_t {
    LOGIN_SUCCESS = 0,      ///< Successful login
    LOGIN_FAILURE = 1,      ///< Failed login attempt
    LOGOUT = 2,             ///< User logout
    ACCESS_GRANTED = 3,     ///< Access granted
    ACCESS_DENIED = 4,      ///< Access denied
    CONFIG_CHANGE = 5,      ///< Configuration change
    SECURITY_ALERT = 6,     ///< Security alert
    SYSTEM_BREACH = 7,      ///< System breach detected
    ENCRYPTION_ERROR = 8,   ///< Encryption error
    DECRYPTION_ERROR = 9,   ///< Decryption error
    CERTIFICATE_EXPIRED = 10, ///< Certificate expired
    INVALID_TOKEN = 11,     ///< Invalid token
    RATE_LIMIT_EXCEEDED = 12, ///< Rate limit exceeded
    SUSPICIOUS_ACTIVITY = 13 ///< Suspicious activity detected
};

/**
 * @brief User information structure
 */
struct User {
    String username;         ///< Username
    String passwordHash;     ///< Password hash
    UserRole role;           ///< User role
    uint8_t permissions;     ///< User permissions
    uint32_t lastLogin;      ///< Last login timestamp
    uint32_t loginCount;     ///< Login count
    uint32_t failedAttempts; ///< Failed login attempts
    bool enabled;            ///< User enabled/disabled
    uint32_t created;        ///< Account creation timestamp
    uint32_t expires;        ///< Account expiration timestamp
    
    User() : 
        role(UserRole::GUEST),
        permissions(0),
        lastLogin(0),
        loginCount(0),
        failedAttempts(0),
        enabled(true),
        created(0),
        expires(0) {}
};

/**
 * @brief Security configuration structure
 */
struct SecurityConfig {
    SecurityLevel level;         ///< Security level
    AuthMethod authMethod;       ///< Authentication method
    uint32_t sessionTimeout;     ///< Session timeout (seconds)
    uint32_t maxLoginAttempts;   ///< Maximum login attempts
    uint32_t lockoutDuration;    ///< Account lockout duration (seconds)
    uint32_t passwordMinLength;  ///< Minimum password length
    bool requireComplexPassword; ///< Require complex password
    bool enableRateLimiting;     ///< Enable rate limiting
    uint32_t rateLimitWindow;    ///< Rate limit window (seconds)
    uint32_t rateLimitMax;       ///< Maximum requests per window
    bool enableAuditLog;         ///< Enable audit logging
    uint32_t auditLogRetention;  ///< Audit log retention (days)
    bool enableEncryption;       ///< Enable data encryption
    String encryptionKey;        ///< Encryption key
    bool enableCertificates;     ///< Enable certificate validation
    uint32_t certCheckInterval;  ///< Certificate check interval (seconds)
    
    // Default constructor
    SecurityConfig() : 
        level(SecurityLevel::MEDIUM),
        authMethod(AuthMethod::PASSWORD),
        sessionTimeout(3600),        // 1 hour
        maxLoginAttempts(5),
        lockoutDuration(900),        // 15 minutes
        passwordMinLength(8),
        requireComplexPassword(true),
        enableRateLimiting(true),
        rateLimitWindow(60),         // 1 minute
        rateLimitMax(100),
        enableAuditLog(true),
        auditLogRetention(30),       // 30 days
        enableEncryption(true),
        enableCertificates(false),
        certCheckInterval(86400) {}  // 24 hours
};

/**
 * @brief Security statistics
 */
struct SecurityStats {
    uint32_t totalLogins;           ///< Total successful logins
    uint32_t failedLogins;          ///< Total failed logins
    uint32_t accessDenied;          ///< Access denied count
    uint32_t securityAlerts;        ///< Security alerts
    uint32_t encryptionErrors;      ///< Encryption errors
    uint32_t decryptionErrors;      ///< Decryption errors
    uint32_t certificateErrors;     ///< Certificate errors
    uint32_t rateLimitViolations;   ///< Rate limit violations
    uint32_t suspiciousActivities;  ///< Suspicious activities
    uint32_t lastSecurityCheck;     ///< Last security check timestamp
    uint32_t uptime;                ///< Security manager uptime
};

/**
 * @brief Security event structure
 */
struct SecurityEvent {
    SecurityEventType type;         ///< Event type
    String username;                ///< Username (if applicable)
    String description;             ///< Event description
    String ipAddress;               ///< IP address (if applicable)
    uint32_t timestamp;             ///< Event timestamp
    ErrorSeverity severity;         ///< Event severity
    String details;                 ///< Additional details
    
    SecurityEvent() : 
        type(SecurityEventType::LOGIN_SUCCESS),
        timestamp(0),
        severity(ErrorSeverity::INFO) {}
};

/**
 * @brief Comprehensive security management class
 * 
 * This class provides centralized security functionality with:
 * - User authentication and authorization
 * - Data encryption and decryption
 * - Access control and permissions
 * - Security monitoring and auditing
 * - Certificate management
 * - Threat detection and response
 */
class SecurityManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to SecurityManager instance
     */
    static SecurityManager& getInstance();
    
    /**
     * @brief Initialize security manager
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Get security configuration
     * @return Reference to security configuration
     */
    const SecurityConfig& getConfig() const { return config; }
    
    /**
     * @brief Update security configuration
     * @param newConfig New configuration
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const SecurityConfig& newConfig);
    
    /**
     * @brief Get security statistics
     * @return Reference to security statistics
     */
    const SecurityStats& getStats() const { return stats; }
    
    /**
     * @brief Clear security statistics
     */
    void clearStats();
    
    // User management
    /**
     * @brief Create new user
     * @param username Username
     * @param password Password
     * @param role User role
     * @return true if creation successful, false otherwise
     */
    bool createUser(const String& username, const String& password, UserRole role);
    
    /**
     * @brief Delete user
     * @param username Username
     * @return true if deletion successful, false otherwise
     */
    bool deleteUser(const String& username);
    
    /**
     * @brief Update user
     * @param username Username
     * @param user Updated user information
     * @return true if update successful, false otherwise
     */
    bool updateUser(const String& username, const User& user);
    
    /**
     * @brief Get user information
     * @param username Username
     * @return Pointer to user information, nullptr if not found
     */
    User* getUser(const String& username);
    
    /**
     * @brief Get all users
     * @return Vector of all users
     */
    std::vector<User> getAllUsers();
    
    /**
     * @brief Enable/disable user
     * @param username Username
     * @param enabled true to enable, false to disable
     * @return true if operation successful, false otherwise
     */
    bool setUserEnabled(const String& username, bool enabled);
    
    /**
     * @brief Change user password
     * @param username Username
     * @param newPassword New password
     * @return true if change successful, false otherwise
     */
    bool changePassword(const String& username, const String& newPassword);
    
    /**
     * @brief Reset user password
     * @param username Username
     * @return New password if reset successful, empty string otherwise
     */
    String resetPassword(const String& username);
    
    // Authentication
    /**
     * @brief Authenticate user
     * @param username Username
     * @param password Password
     * @param ipAddress Client IP address
     * @return true if authentication successful, false otherwise
     */
    bool authenticateUser(const String& username, const String& password, const String& ipAddress = "");
    
    /**
     * @brief Logout user
     * @param username Username
     * @return true if logout successful, false otherwise
     */
    bool logoutUser(const String& username);
    
    /**
     * @brief Check if user is authenticated
     * @param username Username
     * @return true if authenticated, false otherwise
     */
    bool isAuthenticated(const String& username);
    
    /**
     * @brief Generate authentication token
     * @param username Username
     * @return Authentication token
     */
    String generateAuthToken(const String& username);
    
    /**
     * @brief Validate authentication token
     * @param token Authentication token
     * @param username Output username
     * @return true if token valid, false otherwise
     */
    bool validateAuthToken(const String& token, String& username);
    
    /**
     * @brief Revoke authentication token
     * @param token Authentication token
     * @return true if revocation successful, false otherwise
     */
    bool revokeAuthToken(const String& token);
    
    // Authorization
    /**
     * @brief Check user permission
     * @param username Username
     * @param permission Required permission
     * @return true if user has permission, false otherwise
     */
    bool checkPermission(const String& username, Permission permission);
    
    /**
     * @brief Grant permission to user
     * @param username Username
     * @param permission Permission to grant
     * @return true if grant successful, false otherwise
     */
    bool grantPermission(const String& username, Permission permission);
    
    /**
     * @brief Revoke permission from user
     * @param username Username
     * @param permission Permission to revoke
     * @return true if revoke successful, false otherwise
     */
    bool revokePermission(const String& username, Permission permission);
    
    /**
     * @brief Get user permissions
     * @param username Username
     * @return User permissions
     */
    uint8_t getUserPermissions(const String& username);
    
    // Encryption
    /**
     * @brief Encrypt data
     * @param data Data to encrypt
     * @param encryptedData Output encrypted data
     * @return true if encryption successful, false otherwise
     */
    bool encryptData(const String& data, String& encryptedData);
    
    /**
     * @brief Decrypt data
     * @param encryptedData Encrypted data
     * @param data Output decrypted data
     * @return true if decryption successful, false otherwise
     */
    bool decryptData(const String& encryptedData, String& data);
    
    /**
     * @brief Encrypt file
     * @param inputPath Input file path
     * @param outputPath Output file path
     * @return true if encryption successful, false otherwise
     */
    bool encryptFile(const String& inputPath, const String& outputPath);
    
    /**
     * @brief Decrypt file
     * @param inputPath Input file path
     * @param outputPath Output file path
     * @return true if decryption successful, false otherwise
     */
    bool decryptFile(const String& inputPath, const String& outputPath);
    
    /**
     * @brief Generate encryption key
     * @param keyLength Key length in bits
     * @return Generated key
     */
    String generateEncryptionKey(uint16_t keyLength = 256);
    
    /**
     * @brief Hash password
     * @param password Password to hash
     * @return Password hash
     */
    String hashPassword(const String& password);
    
    /**
     * @brief Verify password
     * @param password Password to verify
     * @param hash Password hash
     * @return true if password matches, false otherwise
     */
    bool verifyPassword(const String& password, const String& hash);
    
    // Security monitoring
    /**
     * @brief Log security event
     * @param type Event type
     * @param username Username (if applicable)
     * @param description Event description
     * @param ipAddress IP address (if applicable)
     * @param severity Event severity
     * @param details Additional details
     */
    void logSecurityEvent(SecurityEventType type, const String& username, 
                         const String& description, const String& ipAddress = "",
                         ErrorSeverity severity = ErrorSeverity::INFO, 
                         const String& details = "");
    
    /**
     * @brief Get security events
     * @param startTime Start timestamp
     * @param endTime End timestamp
     * @param events Output security events
     * @return true if retrieval successful, false otherwise
     */
    bool getSecurityEvents(uint32_t startTime, uint32_t endTime, std::vector<SecurityEvent>& events);
    
    /**
     * @brief Clear old security events
     * @param maxAge Maximum age in seconds
     * @return Number of events cleared
     */
    uint32_t clearOldSecurityEvents(uint32_t maxAge);
    
    /**
     * @brief Check for security threats
     * @return true if threats detected, false otherwise
     */
    bool checkSecurityThreats();
    
    /**
     * @brief Perform security audit
     * @return true if audit passed, false otherwise
     */
    bool performSecurityAudit();
    
    // Rate limiting
    /**
     * @brief Check rate limit for client
     * @param clientId Client identifier (IP, username, etc.)
     * @return true if within rate limit, false otherwise
     */
    bool checkRateLimit(const String& clientId);
    
    /**
     * @brief Reset rate limit for client
     * @param clientId Client identifier
     */
    void resetRateLimit(const String& clientId);
    
    // Utility functions
    /**
     * @brief Validate password strength
     * @param password Password to validate
     * @return true if password meets requirements, false otherwise
     */
    bool validatePasswordStrength(const String& password);
    
    /**
     * @brief Generate secure random string
     * @param length String length
     * @return Random string
     */
    String generateRandomString(uint8_t length);
    
    /**
     * @brief Generate secure random number
     * @param min Minimum value
     * @param max Maximum value
     * @return Random number
     */
    uint32_t generateRandomNumber(uint32_t min, uint32_t max);
    
    /**
     * @brief Perform security health check
     * @return true if healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Get security information as JSON string
     * @return JSON string with security information
     */
    String getSecurityInfo();
    
    /**
     * @brief Get security status as human-readable string
     * @return Status string
     */
    String getStatusString();
    
    /**
     * @brief Main security management loop
     * 
     * This method should be called regularly to handle:
     * - Session management
     * - Security monitoring
     * - Threat detection
     * - Certificate validation
     * - Statistics updates
     */
    void loop();

private:
    // Singleton pattern - prevent external instantiation
    SecurityManager();
    ~SecurityManager();
    SecurityManager(const SecurityManager&) = delete;
    SecurityManager& operator=(const SecurityManager&) = delete;
    
    // Configuration and state
    SecurityConfig config;
    SecurityStats stats;
    bool initialized;
    
    // Manager references
    SystemManager* systemManager;
    StorageManager* storageManager;
    
    // User management
    std::map<String, User> users;
    std::map<String, uint32_t> activeSessions;
    std::map<String, std::vector<uint32_t>> rateLimitMap;
    std::map<String, String> authTokens;
    
    // Security events
    std::vector<SecurityEvent> securityEvents;
    
    // Timing
    uint32_t startTime;
    uint32_t lastSecurityCheck;
    uint32_t lastAuditTime;
    uint32_t lastStatsUpdate;
    
    /**
     * @brief Load users from storage
     * @return true if loading successful, false otherwise
     */
    bool loadUsers();
    
    /**
     * @brief Save users to storage
     * @return true if saving successful, false otherwise
     */
    bool saveUsers();
    
    /**
     * @brief Load security configuration
     * @return true if loading successful, false otherwise
     */
    bool loadConfiguration();
    
    /**
     * @brief Save security configuration
     * @return true if saving successful, false otherwise
     */
    bool saveConfiguration();
    
    /**
     * @brief Update security statistics
     */
    void updateStats();
    
    /**
     * @brief Clean up expired sessions
     */
    void cleanupExpiredSessions();
    
    /**
     * @brief Clean up expired tokens
     */
    void cleanupExpiredTokens();
    
    /**
     * @brief Check for suspicious activity
     * @return true if suspicious activity detected, false otherwise
     */
    bool checkSuspiciousActivity();
    
    /**
     * @brief Lock user account
     * @param username Username
     * @param duration Lockout duration (seconds)
     */
    void lockUserAccount(const String& username, uint32_t duration);
    
    /**
     * @brief Unlock user account
     * @param username Username
     */
    void unlockUserAccount(const String& username);
    
    /**
     * @brief Check if user account is locked
     * @param username Username
     * @return true if locked, false otherwise
     */
    bool isUserAccountLocked(const String& username);
    
    /**
     * @brief Log security manager event
     * @param event Event description
     * @param severity Event severity
     */
    void logSecurityManagerEvent(const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
    
    /**
     * @brief Get security level name
     * @param level Security level
     * @return Security level name
     */
    String getSecurityLevelName(SecurityLevel level);
    
    /**
     * @brief Get user role name
     * @param role User role
     * @return Role name
     */
    String getUserRoleName(UserRole role);
    
    /**
     * @brief Get permission name
     * @param permission Permission
     * @return Permission name
     */
    String getPermissionName(Permission permission);
    
    /**
     * @brief Get security event type name
     * @param type Security event type
     * @return Event type name
     */
    String getSecurityEventTypeName(SecurityEventType type);
};

// Global security manager instance
extern SecurityManager& securityManager;

// Convenience macros for security management
#define SECURITY_INIT() securityManager.initialize()
#define SECURITY_AUTHENTICATE(user, pass) securityManager.authenticateUser(user, pass)
#define SECURITY_CHECK_PERMISSION(user, perm) securityManager.checkPermission(user, perm)
#define SECURITY_ENCRYPT(data, encrypted) securityManager.encryptData(data, encrypted)
#define SECURITY_DECRYPT(encrypted, data) securityManager.decryptData(encrypted, data)
#define SECURITY_LOG_EVENT(type, user, desc) securityManager.logSecurityEvent(type, user, desc)
#define SECURITY_LOOP() securityManager.loop()
#define SECURITY_HEALTH_CHECK() securityManager.performHealthCheck()

#endif // SECURITY_MANAGER_HPP 