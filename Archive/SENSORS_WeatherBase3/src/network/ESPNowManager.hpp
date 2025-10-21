/**
 * @file ESPNowManager.hpp
 * @brief ESP-NOW communication management for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides comprehensive ESP-NOW functionality including:
 * - Peer management and discovery
 * - Message transmission and reception
 * - Data encryption and security
 * - Connection monitoring and recovery
 * - Performance optimization
 */

#ifndef ESPNOW_MANAGER_HPP
#define ESPNOW_MANAGER_HPP

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <queue>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations
struct SensorData;
struct SystemConfig;

/**
 * @brief ESP-NOW message types
 */
enum class ESPNowMessageType : uint8_t {
    SENSOR_DATA = 0x01,      ///< Sensor data message
    SYSTEM_STATUS = 0x02,    ///< System status message
    CONFIG_UPDATE = 0x03,    ///< Configuration update
    COMMAND = 0x04,          ///< Command message
    RESPONSE = 0x05,         ///< Response message
    HEARTBEAT = 0x06,        ///< Heartbeat message
    PEER_DISCOVERY = 0x07,   ///< Peer discovery message
    ERROR_REPORT = 0x08,     ///< Error report message
    DATA_REQUEST = 0x09,     ///< Data request message
    DATA_RESPONSE = 0x0A,    ///< Data response message
    PEER_INFO = 0x0B,        ///< Peer information
    ACK = 0x0C,              ///< Acknowledgment
    NACK = 0x0D,             ///< Negative acknowledgment
    BROADCAST = 0x0E,        ///< Broadcast message
    UNKNOWN = 0xFF           ///< Unknown message type
};

/**
 * @brief ESP-NOW connection states
 */
enum class ESPNowState : uint8_t {
    UNINITIALIZED = 0,    ///< ESP-NOW not initialized
    INITIALIZING,         ///< Initialization in progress
    READY,               ///< Ready for communication
    SENDING,             ///< Sending message
    RECEIVING,           ///< Receiving message
    ERROR,               ///< Error state
    DISABLED             ///< ESP-NOW disabled
};

/**
 * @brief ESP-NOW peer information
 */
struct ESPNowPeer {
    uint8_t mac[6];              ///< MAC address
    String name;                 ///< Peer name
    String deviceType;           ///< Device type
    uint32_t lastSeen;           ///< Last seen timestamp
    uint32_t messageCount;       ///< Total messages received
    uint32_t errorCount;         ///< Error count
    bool isActive;               ///< Active status
    int32_t rssi;                ///< Signal strength
    uint32_t lastHeartbeat;      ///< Last heartbeat timestamp
    bool requiresAck;            ///< Requires acknowledgment
    
    ESPNowPeer() : 
        lastSeen(0),
        messageCount(0),
        errorCount(0),
        isActive(false),
        rssi(0),
        lastHeartbeat(0),
        requiresAck(true) {
        memset(mac, 0, 6);
    }
};

/**
 * @brief ESP-NOW message structure
 */
struct ESPNowMessage {
    uint8_t messageId;           ///< Unique message ID
    ESPNowMessageType type;      ///< Message type
    uint8_t senderMac[6];        ///< Sender MAC address
    uint8_t receiverMac[6];      ///< Receiver MAC address (0xFF for broadcast)
    uint32_t timestamp;          ///< Message timestamp
    uint16_t dataLength;         ///< Data length
    uint8_t data[250];           ///< Message data (max 250 bytes)
    uint8_t checksum;            ///< Message checksum
    bool requiresAck;            ///< Requires acknowledgment
    uint8_t retryCount;          ///< Retry count
    
    ESPNowMessage() : 
        messageId(0),
        type(ESPNowMessageType::UNKNOWN),
        timestamp(0),
        dataLength(0),
        checksum(0),
        requiresAck(true),
        retryCount(0) {
        memset(senderMac, 0, 6);
        memset(receiverMac, 0xFF, 6);
        memset(data, 0, 250);
    }
};

/**
 * @brief ESP-NOW configuration structure
 */
struct ESPNowConfig {
    uint8_t channel;             ///< WiFi channel
    uint8_t role;                ///< ESP-NOW role (ESP_NOW_ROLE_COMBO)
    uint32_t timeout;            ///< Message timeout (ms)
    uint32_t retryInterval;      ///< Retry interval (ms)
    uint8_t maxRetries;          ///< Maximum retry attempts
    uint32_t heartbeatInterval;  ///< Heartbeat interval (ms)
    uint32_t peerTimeout;        ///< Peer timeout (seconds)
    bool enableEncryption;       ///< Enable message encryption
    bool enableCompression;      ///< Enable message compression
    uint32_t maxMessageSize;     ///< Maximum message size (bytes)
    bool enableBroadcast;        ///< Enable broadcast messages
    bool enablePeerDiscovery;    ///< Enable peer discovery
    
    // Default constructor
    ESPNowConfig() : 
        channel(1),
        role(ESP_NOW_ROLE_COMBO),
        timeout(5000),           // 5 seconds
        retryInterval(1000),     // 1 second
        maxRetries(3),
        heartbeatInterval(30000), // 30 seconds
        peerTimeout(300),        // 5 minutes
        enableEncryption(true),
        enableCompression(false),
        maxMessageSize(250),
        enableBroadcast(true),
        enablePeerDiscovery(true) {}
};

/**
 * @brief ESP-NOW statistics
 */
struct ESPNowStats {
    uint32_t messagesSent;       ///< Total messages sent
    uint32_t messagesReceived;   ///< Total messages received
    uint32_t messagesDropped;    ///< Messages dropped
    uint32_t errors;             ///< Total errors
    uint32_t retries;            ///< Total retries
    uint32_t acksReceived;       ///< Acknowledgments received
    uint32_t nacksReceived;      ///< Negative acknowledgments received
    uint32_t broadcastMessages;  ///< Broadcast messages sent
    uint32_t peerCount;          ///< Number of peers
    uint32_t activePeers;        ///< Number of active peers
    float averageLatency;        ///< Average message latency (ms)
    uint32_t lastUpdateTime;     ///< Last update timestamp
};

/**
 * @brief Comprehensive ESP-NOW management class
 * 
 * This class provides centralized ESP-NOW functionality with:
 * - Automatic peer management and discovery
 * - Reliable message transmission with acknowledgments
 * - Message encryption and security
 * - Performance monitoring and optimization
 * - Error handling and recovery
 */
class ESPNowManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to ESPNowManager instance
     */
    static ESPNowManager& getInstance();
    
    /**
     * @brief Initialize ESP-NOW manager
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Get ESP-NOW configuration
     * @return Reference to ESP-NOW configuration
     */
    const ESPNowConfig& getConfig() const { return config; }
    
    /**
     * @brief Update ESP-NOW configuration
     * @param newConfig New configuration
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const ESPNowConfig& newConfig);
    
    /**
     * @brief Get ESP-NOW statistics
     * @return Reference to ESP-NOW statistics
     */
    const ESPNowStats& getStats() const { return stats; }
    
    /**
     * @brief Clear ESP-NOW statistics
     */
    void clearStats();
    
    // Peer management
    /**
     * @brief Add peer
     * @param mac MAC address
     * @param name Peer name
     * @param deviceType Device type
     * @return true if addition successful, false otherwise
     */
    bool addPeer(const uint8_t* mac, const String& name = "", const String& deviceType = "");
    
    /**
     * @brief Remove peer
     * @param mac MAC address
     * @return true if removal successful, false otherwise
     */
    bool removePeer(const uint8_t* mac);
    
    /**
     * @brief Get peer information
     * @param mac MAC address
     * @return Pointer to peer information, nullptr if not found
     */
    ESPNowPeer* getPeer(const uint8_t* mac);
    
    /**
     * @brief Get all peers
     * @return Vector of peer information
     */
    std::vector<ESPNowPeer> getAllPeers();
    
    /**
     * @brief Get active peers
     * @return Vector of active peer information
     */
    std::vector<ESPNowPeer> getActivePeers();
    
    /**
     * @brief Check if peer exists
     * @param mac MAC address
     * @return true if peer exists, false otherwise
     */
    bool hasPeer(const uint8_t* mac);
    
    /**
     * @brief Update peer information
     * @param mac MAC address
     * @param name Peer name
     * @param deviceType Device type
     * @return true if update successful, false otherwise
     */
    bool updatePeer(const uint8_t* mac, const String& name, const String& deviceType);
    
    /**
     * @brief Discover peers
     * @param timeout Discovery timeout (ms)
     * @return Number of peers discovered
     */
    uint8_t discoverPeers(uint32_t timeout = 10000);
    
    // Message transmission
    /**
     * @brief Send message to peer
     * @param mac MAC address
     * @param type Message type
     * @param data Message data
     * @param dataLength Data length
     * @param requiresAck Require acknowledgment
     * @return true if send successful, false otherwise
     */
    bool sendMessage(const uint8_t* mac, ESPNowMessageType type, const uint8_t* data, uint16_t dataLength, bool requiresAck = true);
    
    /**
     * @brief Send message to peer (string data)
     * @param mac MAC address
     * @param type Message type
     * @param data String data
     * @param requiresAck Require acknowledgment
     * @return true if send successful, false otherwise
     */
    bool sendMessage(const uint8_t* mac, ESPNowMessageType type, const String& data, bool requiresAck = true);
    
    /**
     * @brief Send sensor data
     * @param mac MAC address
     * @param sensorData Sensor data
     * @return true if send successful, false otherwise
     */
    bool sendSensorData(const uint8_t* mac, const SensorData& sensorData);
    
    /**
     * @brief Send system status
     * @param mac MAC address
     * @param status Status information
     * @return true if send successful, false otherwise
     */
    bool sendSystemStatus(const uint8_t* mac, const String& status);
    
    /**
     * @brief Send command
     * @param mac MAC address
     * @param command Command string
     * @return true if send successful, false otherwise
     */
    bool sendCommand(const uint8_t* mac, const String& command);
    
    /**
     * @brief Send broadcast message
     * @param type Message type
     * @param data Message data
     * @param dataLength Data length
     * @return true if send successful, false otherwise
     */
    bool sendBroadcast(ESPNowMessageType type, const uint8_t* data, uint16_t dataLength);
    
    /**
     * @brief Send broadcast message (string data)
     * @param type Message type
     * @param data String data
     * @return true if send successful, false otherwise
     */
    bool sendBroadcast(ESPNowMessageType type, const String& data);
    
    /**
     * @brief Send heartbeat
     * @param mac MAC address (nullptr for broadcast)
     * @return true if send successful, false otherwise
     */
    bool sendHeartbeat(const uint8_t* mac = nullptr);
    
    /**
     * @brief Send acknowledgment
     * @param mac MAC address
     * @param messageId Message ID to acknowledge
     * @return true if send successful, false otherwise
     */
    bool sendAck(const uint8_t* mac, uint8_t messageId);
    
    /**
     * @brief Send negative acknowledgment
     * @param mac MAC address
     * @param messageId Message ID
     * @param errorCode Error code
     * @return true if send successful, false otherwise
     */
    bool sendNack(const uint8_t* mac, uint8_t messageId, uint8_t errorCode);
    
    // Message reception
    /**
     * @brief Check if message available
     * @return true if message available, false otherwise
     */
    bool hasMessage();
    
    /**
     * @brief Get next message
     * @param message Output message
     * @return true if message retrieved, false otherwise
     */
    bool getMessage(ESPNowMessage& message);
    
    /**
     * @brief Process received messages
     * @return Number of messages processed
     */
    uint8_t processMessages();
    
    // Utility functions
    /**
     * @brief Get current state
     * @return Current ESP-NOW state
     */
    ESPNowState getState() const { return currentState; }
    
    /**
     * @brief Check if ESP-NOW is ready
     * @return true if ready, false otherwise
     */
    bool isReady() const { return currentState == ESPNowState::READY; }
    
    /**
     * @brief Get local MAC address
     * @return Local MAC address as string
     */
    String getLocalMAC();
    
    /**
     * @brief Get local MAC address as bytes
     * @param mac Output MAC address
     */
    void getLocalMAC(uint8_t* mac);
    
    /**
     * @brief Convert MAC address to string
     * @param mac MAC address
     * @return MAC address as string
     */
    static String macToString(const uint8_t* mac);
    
    /**
     * @brief Convert string to MAC address
     * @param macStr MAC address string
     * @param mac Output MAC address
     * @return true if conversion successful, false otherwise
     */
    static bool stringToMAC(const String& macStr, uint8_t* mac);
    
    /**
     * @brief Compare MAC addresses
     * @param mac1 First MAC address
     * @param mac2 Second MAC address
     * @return true if addresses match, false otherwise
     */
    static bool compareMAC(const uint8_t* mac1, const uint8_t* mac2);
    
    /**
     * @brief Check if MAC address is broadcast
     * @param mac MAC address
     * @return true if broadcast address, false otherwise
     */
    static bool isBroadcastMAC(const uint8_t* mac);
    
    /**
     * @brief Perform ESP-NOW health check
     * @return true if healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Get ESP-NOW information as JSON string
     * @return JSON string with ESP-NOW information
     */
    String getESPNowInfo();
    
    /**
     * @brief Get ESP-NOW status as human-readable string
     * @return Status string
     */
    String getStatusString();
    
    /**
     * @brief Main ESP-NOW management loop
     * 
     * This method should be called regularly to handle:
     * - Message processing
     * - Peer management
     * - Heartbeat transmission
     * - Statistics updates
     */
    void loop();

private:
    // Singleton pattern - prevent external instantiation
    ESPNowManager();
    ~ESPNowManager();
    ESPNowManager(const ESPNowManager&) = delete;
    ESPNowManager& operator=(const ESPNowManager&) = delete;
    
    // Configuration and state
    ESPNowConfig config;
    ESPNowStats stats;
    ESPNowState currentState;
    bool initialized;
    
    // Peer management
    std::map<String, ESPNowPeer> peers;
    uint8_t localMAC[6];
    
    // Message handling
    std::queue<ESPNowMessage> messageQueue;
    uint8_t nextMessageId;
    std::map<uint8_t, ESPNowMessage> pendingAcks;
    
    // Timing and management
    uint32_t lastHeartbeat;
    uint32_t lastPeerCheck;
    uint32_t lastStatsUpdate;
    
    // Callback functions
    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
    static void onDataReceived(const uint8_t* mac, const uint8_t* data, int dataLen);
    
    /**
     * @brief Initialize ESP-NOW
     * @return true if initialization successful, false otherwise
     */
    bool initializeESPNow();
    
    /**
     * @brief Process received message
     * @param mac Sender MAC address
     * @param data Message data
     * @param dataLen Data length
     */
    void processReceivedMessage(const uint8_t* mac, const uint8_t* data, int dataLen);
    
    /**
     * @brief Handle message acknowledgment
     * @param mac Sender MAC address
     * @param messageId Message ID
     */
    void handleAck(const uint8_t* mac, uint8_t messageId);
    
    /**
     * @brief Handle negative acknowledgment
     * @param mac Sender MAC address
     * @param messageId Message ID
     * @param errorCode Error code
     */
    void handleNack(const uint8_t* mac, uint8_t messageId, uint8_t errorCode);
    
    /**
     * @brief Handle heartbeat message
     * @param mac Sender MAC address
     */
    void handleHeartbeat(const uint8_t* mac);
    
    /**
     * @brief Handle peer discovery message
     * @param mac Sender MAC address
     * @param data Message data
     * @param dataLen Data length
     */
    void handlePeerDiscovery(const uint8_t* mac, const uint8_t* data, int dataLen);
    
    /**
     * @brief Update peer information
     * @param mac MAC address
     * @param rssi Signal strength
     */
    void updatePeerInfo(const uint8_t* mac, int32_t rssi);
    
    /**
     * @brief Check peer timeouts
     */
    void checkPeerTimeouts();
    
    /**
     * @brief Generate message ID
     * @return Unique message ID
     */
    uint8_t generateMessageId();
    
    /**
     * @brief Calculate message checksum
     * @param message Message to calculate checksum for
     * @return Checksum value
     */
    uint8_t calculateChecksum(const ESPNowMessage& message);
    
    /**
     * @brief Validate message checksum
     * @param message Message to validate
     * @return true if checksum valid, false otherwise
     */
    bool validateChecksum(const ESPNowMessage& message);
    
    /**
     * @brief Encrypt message data
     * @param input Input data
     * @param inputLen Input length
     * @param output Output data
     * @param outputLen Output length
     * @return true if encryption successful, false otherwise
     */
    bool encryptData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen);
    
    /**
     * @brief Decrypt message data
     * @param input Input data
     * @param inputLen Input length
     * @param output Output data
     * @param outputLen Output length
     * @return true if decryption successful, false otherwise
     */
    bool decryptData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen);
    
    /**
     * @brief Compress message data
     * @param input Input data
     * @param inputLen Input length
     * @param output Output data
     * @param outputLen Output length
     * @return true if compression successful, false otherwise
     */
    bool compressData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen);
    
    /**
     * @brief Decompress message data
     * @param input Input data
     * @param inputLen Input length
     * @param output Output data
     * @param outputLen Output length
     * @return true if decompression successful, false otherwise
     */
    bool decompressData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen);
    
    /**
     * @brief Update ESP-NOW statistics
     */
    void updateStats();
    
    /**
     * @brief Log ESP-NOW event
     * @param event Event description
     * @param severity Event severity
     */
    void logESPNowEvent(const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
    
    /**
     * @brief Get message type name
     * @param type Message type
     * @return Message type name
     */
    String getMessageTypeName(ESPNowMessageType type);
    
    /**
     * @brief Get ESP-NOW state name
     * @param state ESP-NOW state
     * @return State name
     */
    String getStateName(ESPNowState state);
};

// Global ESP-NOW manager instance
extern ESPNowManager& espNowManager;

// Convenience macros for ESP-NOW management
#define ESPNOW_INIT() espNowManager.initialize()
#define ESPNOW_READY() espNowManager.isReady()
#define ESPNOW_SEND(mac, type, data) espNowManager.sendMessage(mac, type, data)
#define ESPNOW_SEND_STRING(mac, type, data) espNowManager.sendMessage(mac, type, data)
#define ESPNOW_BROADCAST(type, data) espNowManager.sendBroadcast(type, data)
#define ESPNOW_HEARTBEAT() espNowManager.sendHeartbeat()
#define ESPNOW_HAS_MESSAGE() espNowManager.hasMessage()
#define ESPNOW_GET_MESSAGE(msg) espNowManager.getMessage(msg)
#define ESPNOW_PROCESS_MESSAGES() espNowManager.processMessages()
#define ESPNOW_LOOP() espNowManager.loop()
#define ESPNOW_HEALTH_CHECK() espNowManager.performHealthCheck()

#endif // ESPNOW_MANAGER_HPP 