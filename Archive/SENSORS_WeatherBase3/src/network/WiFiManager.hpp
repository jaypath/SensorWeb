/**
 * @file WiFiManager.hpp
 * @brief WiFi connection management for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides comprehensive WiFi functionality including:
 * - Connection establishment and management
 * - Credential handling and security
 * - Automatic reconnection with exponential backoff
 * - AP mode fallback for configuration
 * - Connection monitoring and diagnostics
 */

#ifndef WIFI_MANAGER_HPP
#define WIFI_MANAGER_HPP

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations
class ESPNowManager;

/**
 * @brief WiFi connection states
 */
enum class WiFiState : uint8_t {
    DISCONNECTED = 0,     ///< Not connected to any network
    CONNECTING,           ///< Attempting to connect
    CONNECTED,            ///< Successfully connected
    CONNECTION_FAILED,    ///< Connection attempt failed
    AP_MODE,              ///< Operating in AP mode
    SCANNING,             ///< Scanning for networks
    ERROR                 ///< WiFi error state
};

/**
 * @brief WiFi network information
 */
struct WiFiNetwork {
    String ssid;          ///< Network SSID
    int32_t rssi;         ///< Signal strength
    uint8_t encryption;   ///< Encryption type
    uint8_t channel;      ///< Channel number
    bool isConnected;     ///< Currently connected to this network
    
    WiFiNetwork() : rssi(0), encryption(0), channel(0), isConnected(false) {}
};

/**
 * @brief WiFi configuration structure
 */
struct WiFiConfig {
    String ssid;                      ///< Network SSID
    String password;                  ///< Network password
    uint32_t connectionTimeout;       ///< Connection timeout (ms)
    uint32_t reconnectInterval;       ///< Reconnection interval (ms)
    uint32_t maxReconnectAttempts;    ///< Maximum reconnection attempts
    bool enableAPMode;                ///< Enable AP mode fallback
    String apSSID;                    ///< AP mode SSID
    String apPassword;                ///< AP mode password
    uint32_t apTimeout;               ///< AP mode timeout (ms)
    bool enablePowerSave;             ///< Enable WiFi power saving
    uint8_t txPower;                  ///< WiFi transmit power (dBm)
    
    // Default constructor with sensible defaults
    WiFiConfig() : 
        connectionTimeout(30000),     // 30 seconds
        reconnectInterval(5000),      // 5 seconds
        maxReconnectAttempts(10),
        enableAPMode(true),
        apSSID("SensorNet-Config"),
        apPassword("S3nsor.N3t!"),
        apTimeout(300000),            // 5 minutes
        enablePowerSave(false),
        txPower(20) {}                // 20 dBm
};

/**
 * @brief WiFi connection statistics
 */
struct WiFiStats {
    uint32_t connectionAttempts;      ///< Total connection attempts
    uint32_t successfulConnections;   ///< Successful connections
    uint32_t failedConnections;       ///< Failed connections
    uint32_t totalUptime;             ///< Total WiFi uptime (seconds)
    uint32_t lastConnectionTime;      ///< Last connection timestamp
    uint32_t disconnectionCount;      ///< Number of disconnections
    float averageRSSI;                ///< Average signal strength
    uint32_t bytesReceived;           ///< Total bytes received
    uint32_t bytesSent;               ///< Total bytes sent
};

/**
 * @brief Comprehensive WiFi management class
 * 
 * This class provides centralized WiFi functionality with:
 * - Automatic connection management
 * - Credential encryption and storage
 * - Connection monitoring and recovery
 * - AP mode fallback capabilities
 * - Performance optimization
 */
class WiFiManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to WiFiManager instance
     */
    static WiFiManager& getInstance();
    
    /**
     * @brief Initialize WiFi manager
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Connect to WiFi network
     * @param ssid Network SSID
     * @param password Network password
     * @param timeout Connection timeout in milliseconds
     * @return true if connection successful, false otherwise
     */
    bool connect(const String& ssid, const String& password, uint32_t timeout = 30000);
    
    /**
     * @brief Connect using stored credentials
     * @param timeout Connection timeout in milliseconds
     * @return true if connection successful, false otherwise
     */
    bool connect(uint32_t timeout = 30000);
    
    /**
     * @brief Disconnect from current network
     * @return true if disconnection successful, false otherwise
     */
    bool disconnect();
    
    /**
     * @brief Check if connected to WiFi
     * @return true if connected, false otherwise
     */
    bool isConnected() const;
    
    /**
     * @brief Get current connection state
     * @return Current WiFi state
     */
    WiFiState getState() const { return currentState; }
    
    /**
     * @brief Get current IP address
     * @return IP address as string
     */
    String getIPAddress() const;
    
    /**
     * @brief Get current MAC address
     * @return MAC address as string
     */
    String getMACAddress() const;
    
    /**
     * @brief Get current signal strength
     * @return RSSI value
     */
    int32_t getRSSI() const;
    
    /**
     * @brief Get current network SSID
     * @return Network SSID
     */
    String getSSID() const;
    
    /**
     * @brief Get current network BSSID
     * @return BSSID as string
     */
    String getBSSID() const;
    
    /**
     * @brief Get current network channel
     * @return Channel number
     */
    uint8_t getChannel() const;
    
    /**
     * @brief Get WiFi configuration
     * @return Reference to WiFi configuration
     */
    const WiFiConfig& getConfig() const { return config; }
    
    /**
     * @brief Update WiFi configuration
     * @param newConfig New configuration
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const WiFiConfig& newConfig);
    
    /**
     * @brief Get WiFi statistics
     * @return Reference to WiFi statistics
     */
    const WiFiStats& getStats() const { return stats; }
    
    /**
     * @brief Clear WiFi statistics
     */
    void clearStats();
    
    /**
     * @brief Scan for available networks
     * @param maxNetworks Maximum number of networks to return
     * @return Vector of discovered networks
     */
    std::vector<WiFiNetwork> scanNetworks(uint8_t maxNetworks = 20);
    
    /**
     * @brief Start AP mode
     * @param ssid AP SSID (uses config if empty)
     * @param password AP password (uses config if empty)
     * @param timeout AP timeout in milliseconds
     * @return true if AP started successfully, false otherwise
     */
    bool startAP(const String& ssid = "", const String& password = "", uint32_t timeout = 300000);
    
    /**
     * @brief Stop AP mode
     * @return true if AP stopped successfully, false otherwise
     */
    bool stopAP();
    
    /**
     * @brief Check if AP mode is active
     * @return true if AP mode active, false otherwise
     */
    bool isAPMode() const;
    
    /**
     * @brief Get AP IP address
     * @return AP IP address as string
     */
    String getAPIPAddress() const;
    
    /**
     * @brief Get AP SSID
     * @return AP SSID
     */
    String getAPSSID() const;
    
    /**
     * @brief Store WiFi credentials
     * @param ssid Network SSID
     * @param password Network password
     * @return true if storage successful, false otherwise
     */
    bool storeCredentials(const String& ssid, const String& password);
    
    /**
     * @brief Load stored credentials
     * @param ssid Output SSID
     * @param password Output password
     * @return true if credentials loaded, false otherwise
     */
    bool loadCredentials(String& ssid, String& password);
    
    /**
     * @brief Clear stored credentials
     * @return true if cleared successfully, false otherwise
     */
    bool clearCredentials();
    
    /**
     * @brief Check if credentials are stored
     * @return true if credentials available, false otherwise
     */
    bool hasCredentials() const;
    
    /**
     * @brief Enable/disable WiFi power saving
     * @param enable true to enable, false to disable
     * @return true if setting applied successfully, false otherwise
     */
    bool setPowerSave(bool enable);
    
    /**
     * @brief Set WiFi transmit power
     * @param power Transmit power in dBm (0-20)
     * @return true if setting applied successfully, false otherwise
     */
    bool setTxPower(uint8_t power);
    
    /**
     * @brief Get WiFi diagnostic information
     * @return Diagnostic information as JSON string
     */
    String getDiagnostics();
    
    /**
     * @brief Perform WiFi health check
     * @return true if WiFi healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Get connection quality score (0-100)
     * @return Quality score
     */
    uint8_t getConnectionQuality() const;
    
    /**
     * @brief Main WiFi management loop
     * 
     * This method should be called regularly to handle:
     * - Connection monitoring
     * - Automatic reconnection
     * - AP mode timeout
     * - Statistics updates
     */
    void loop();
    
    /**
     * @brief Get WiFi information as JSON string
     * @return JSON string with WiFi information
     */
    String getWiFiInfo();
    
    /**
     * @brief Get WiFi status as human-readable string
     * @return Status string
     */
    String getStatusString();

private:
    // Singleton pattern - prevent external instantiation
    WiFiManager();
    ~WiFiManager();
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    
    // WiFi state
    WiFiState currentState;
    WiFiConfig config;
    WiFiStats stats;
    
    // Connection management
    uint32_t lastConnectionAttempt;
    uint32_t lastReconnectAttempt;
    uint32_t connectionStartTime;
    uint32_t reconnectAttempts;
    bool autoReconnect;
    
    // AP mode management
    bool apModeActive;
    uint32_t apStartTime;
    String apSSID;
    String apPassword;
    uint32_t apTimeout;
    
    // Network information
    String currentSSID;
    String currentBSSID;
    uint8_t currentChannel;
    int32_t currentRSSI;
    uint32_t connectionUptime;
    
    // Preferences for credential storage
    Preferences preferences;
    bool initialized;
    
    // ESPNow manager reference
    ESPNowManager* espNowManager;
    
    /**
     * @brief Attempt WiFi connection
     * @param ssid Network SSID
     * @param password Network password
     * @param timeout Connection timeout
     * @return true if connection successful, false otherwise
     */
    bool attemptConnection(const String& ssid, const String& password, uint32_t timeout);
    
    /**
     * @brief Handle connection success
     */
    void handleConnectionSuccess();
    
    /**
     * @brief Handle connection failure
     */
    void handleConnectionFailure();
    
    /**
     * @brief Handle disconnection
     */
    void handleDisconnection();
    
    /**
     * @brief Attempt automatic reconnection
     * @return true if reconnection successful, false otherwise
     */
    bool attemptReconnection();
    
    /**
     * @brief Update connection statistics
     */
    void updateStats();
    
    /**
     * @brief Update signal strength
     */
    void updateRSSI();
    
    /**
     * @brief Check AP mode timeout
     */
    void checkAPTimeout();
    
    /**
     * @brief Generate AP SSID based on MAC address
     * @return Generated AP SSID
     */
    String generateAPSSID();
    
    /**
     * @brief Encrypt credentials for storage
     * @param input Input string to encrypt
     * @param output Output encrypted string
     * @return true if encryption successful, false otherwise
     */
    bool encryptCredentials(const String& input, String& output);
    
    /**
     * @brief Decrypt credentials from storage
     * @param input Encrypted string
     * @param output Decrypted string
     * @return true if decryption successful, false otherwise
     */
    bool decryptCredentials(const String& input, String& output);
    
    /**
     * @brief Load configuration from preferences
     * @return true if loading successful, false otherwise
     */
    bool loadConfiguration();
    
    /**
     * @brief Save configuration to preferences
     * @return true if saving successful, false otherwise
     */
    bool saveConfiguration();
    
    /**
     * @brief Update WiFi state
     * @param newState New WiFi state
     */
    void updateState(WiFiState newState);
    
    /**
     * @brief Log WiFi event
     * @param event Event description
     * @param severity Event severity
     */
    void logWiFiEvent(const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
    
    /**
     * @brief Calculate connection quality score
     * @return Quality score (0-100)
     */
    uint8_t calculateConnectionQuality() const;
};

// Global WiFi manager instance
extern WiFiManager& wifiManager;

// Convenience macros for WiFi management
#define WIFI_INIT() wifiManager.initialize()
#define WIFI_CONNECT(ssid, password) wifiManager.connect(ssid, password)
#define WIFI_CONNECT_STORED() wifiManager.connect()
#define WIFI_DISCONNECT() wifiManager.disconnect()
#define WIFI_CONNECTED() wifiManager.isConnected()
#define WIFI_AP_MODE() wifiManager.isAPMode()
#define WIFI_IP() wifiManager.getIPAddress()
#define WIFI_RSSI() wifiManager.getRSSI()
#define WIFI_SSID() wifiManager.getSSID()
#define WIFI_LOOP() wifiManager.loop()
#define WIFI_HEALTH_CHECK() wifiManager.performHealthCheck()

#endif // WIFI_MANAGER_HPP 