/**
 * @file ESPNowManager.cpp
 * @brief ESP-NOW manager implementation for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 */

#include "ESPNowManager.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Global ESP-NOW manager instance
ESPNowManager& espNowManager = ESPNowManager::getInstance();

ESPNowManager::ESPNowManager() :
    config{},
    stats{},
    currentState(ESPNowState::UNINITIALIZED),
    initialized(false),
    nextMessageId(1),
    lastHeartbeat(0),
    lastPeerCheck(0),
    lastStatsUpdate(0) {
    memset(localMAC, 0, sizeof(localMAC));
}

ESPNowManager::~ESPNowManager() {}

ESPNowManager& ESPNowManager::getInstance() {
    static ESPNowManager instance;
    return instance;
}

bool ESPNowManager::initialize() {
    if (initialized) return true;
    WiFi.mode(WIFI_STA);
    WiFi.macAddress(localMAC);
    if (!initializeESPNow()) {
        ErrorHandler::log(ErrorCode::ESPNOW_INIT_FAILED, ErrorSeverity::ERROR, "ESP-NOW initialization failed");
        return false;
    }
    initialized = true;
    updateStats();
    currentState = ESPNowState::READY;
    return true;
}

bool ESPNowManager::updateConfig(const ESPNowConfig& newConfig) {
    config = newConfig;
    // TODO: Save config to preferences
    return true;
}

void ESPNowManager::clearStats() {
    stats = {};
}

bool ESPNowManager::addPeer(const uint8_t* mac, const String& name, const String& deviceType) {
    String macStr = macToString(mac);
    ESPNowPeer peer;
    memcpy(peer.mac, mac, 6);
    peer.name = name;
    peer.deviceType = deviceType;
    peer.isActive = true;
    peer.lastSeen = millis();
    peers[macStr] = peer;
    return true;
}

bool ESPNowManager::removePeer(const uint8_t* mac) {
    String macStr = macToString(mac);
    return peers.erase(macStr) > 0;
}

ESPNowPeer* ESPNowManager::getPeer(const uint8_t* mac) {
    String macStr = macToString(mac);
    auto it = peers.find(macStr);
    return it != peers.end() ? &it->second : nullptr;
}

std::vector<ESPNowPeer> ESPNowManager::getAllPeers() {
    std::vector<ESPNowPeer> v;
    for (const auto& p : peers) v.push_back(p.second);
    return v;
}

std::vector<ESPNowPeer> ESPNowManager::getActivePeers() {
    std::vector<ESPNowPeer> v;
    for (const auto& p : peers) if (p.second.isActive) v.push_back(p.second);
    return v;
}

bool ESPNowManager::hasPeer(const uint8_t* mac) {
    String macStr = macToString(mac);
    return peers.find(macStr) != peers.end();
}

bool ESPNowManager::updatePeer(const uint8_t* mac, const String& name, const String& deviceType) {
    ESPNowPeer* peer = getPeer(mac);
    if (!peer) return false;
    peer->name = name;
    peer->deviceType = deviceType;
    peer->lastSeen = millis();
    return true;
}

uint8_t ESPNowManager::discoverPeers(uint32_t timeout) {
    // TODO: Implement peer discovery
    return peers.size();
}

bool ESPNowManager::sendMessage(const uint8_t* mac, ESPNowMessageType type, const uint8_t* data, uint16_t dataLength, bool requiresAck) {
    ESPNowMessage msg;
    msg.messageId = generateMessageId();
    msg.type = type;
    memcpy(msg.senderMac, localMAC, 6);
    memcpy(msg.receiverMac, mac, 6);
    msg.timestamp = millis();
    msg.dataLength = dataLength > 250 ? 250 : dataLength;
    memcpy(msg.data, data, msg.dataLength);
    msg.requiresAck = requiresAck;
    msg.checksum = calculateChecksum(msg);
    // TODO: Send via esp_now_send
    stats.messagesSent++;
    return true;
}

bool ESPNowManager::sendMessage(const uint8_t* mac, ESPNowMessageType type, const String& data, bool requiresAck) {
    return sendMessage(mac, type, (const uint8_t*)data.c_str(), data.length(), requiresAck);
}

bool ESPNowManager::sendSensorData(const uint8_t* mac, const SensorData&) {
    // TODO: Serialize and send sensor data
    return true;
}

bool ESPNowManager::sendSystemStatus(const uint8_t* mac, const String&) {
    // TODO: Serialize and send system status
    return true;
}

bool ESPNowManager::sendCommand(const uint8_t* mac, const String&) {
    // TODO: Serialize and send command
    return true;
}

bool ESPNowManager::sendBroadcast(ESPNowMessageType type, const uint8_t* data, uint16_t dataLength) {
    uint8_t broadcastMac[6];
    memset(broadcastMac, 0xFF, 6);
    return sendMessage(broadcastMac, type, data, dataLength, false);
}

bool ESPNowManager::sendBroadcast(ESPNowMessageType type, const String& data) {
    return sendBroadcast(type, (const uint8_t*)data.c_str(), data.length());
}

bool ESPNowManager::sendHeartbeat(const uint8_t* mac) {
    // TODO: Send heartbeat message
    return true;
}

bool ESPNowManager::sendAck(const uint8_t* mac, uint8_t messageId) {
    // TODO: Send ACK message
    return true;
}

bool ESPNowManager::sendNack(const uint8_t* mac, uint8_t messageId, uint8_t errorCode) {
    // TODO: Send NACK message
    return true;
}

bool ESPNowManager::hasMessage() {
    return !messageQueue.empty();
}

bool ESPNowManager::getMessage(ESPNowMessage& message) {
    if (messageQueue.empty()) return false;
    message = messageQueue.front();
    messageQueue.pop();
    return true;
}

uint8_t ESPNowManager::processMessages() {
    // TODO: Process received messages
    return 0;
}

String ESPNowManager::getLocalMAC() {
    return macToString(localMAC);
}

void ESPNowManager::getLocalMAC(uint8_t* mac) {
    memcpy(mac, localMAC, 6);
}

String ESPNowManager::macToString(const uint8_t* mac) {
    char buf[18];
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool ESPNowManager::stringToMAC(const String& macStr, uint8_t* mac) {
    return sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

bool ESPNowManager::compareMAC(const uint8_t* mac1, const uint8_t* mac2) {
    return memcmp(mac1, mac2, 6) == 0;
}

bool ESPNowManager::isBroadcastMAC(const uint8_t* mac) {
    for (int i = 0; i < 6; ++i) if (mac[i] != 0xFF) return false;
    return true;
}

bool ESPNowManager::performHealthCheck() {
    // TODO: Check peer timeouts, message queue, etc.
    return true;
}

String ESPNowManager::getESPNowInfo() {
    String info = "{";
    info += "\"peers\":" + String(peers.size()) + ",";
    info += "\"messagesSent\":" + String(stats.messagesSent) + ",";
    info += "\"messagesReceived\":" + String(stats.messagesReceived) + "}";
    return info;
}

String ESPNowManager::getStatusString() {
    switch (currentState) {
        case ESPNowState::UNINITIALIZED: return "Uninitialized";
        case ESPNowState::INITIALIZING: return "Initializing";
        case ESPNowState::READY: return "Ready";
        case ESPNowState::SENDING: return "Sending";
        case ESPNowState::RECEIVING: return "Receiving";
        case ESPNowState::ERROR: return "Error";
        case ESPNowState::DISABLED: return "Disabled";
        default: return "Unknown";
    }
}

void ESPNowManager::loop() {
    // TODO: Periodic peer checks, message processing, heartbeat
}

// Private helpers
bool ESPNowManager::initializeESPNow() {
    if (esp_now_init() != ESP_OK) {
        ErrorHandler::log(ErrorCode::ESPNOW_INIT_FAILED, ErrorSeverity::ERROR, "esp_now_init failed");
        return false;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);
    return true;
}

void ESPNowManager::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    // TODO: Handle send callback
}

void ESPNowManager::onDataReceived(const uint8_t* mac, const uint8_t* data, int dataLen) {
    // TODO: Handle receive callback, push to messageQueue
}

void ESPNowManager::processReceivedMessage(const uint8_t* mac, const uint8_t* data, int dataLen) {
    // TODO: Parse and process received message
}

void ESPNowManager::handleAck(const uint8_t* mac, uint8_t messageId) {
    // TODO: Handle ACK
}

void ESPNowManager::handleNack(const uint8_t* mac, uint8_t messageId, uint8_t errorCode) {
    // TODO: Handle NACK
}

void ESPNowManager::handleHeartbeat(const uint8_t* mac) {
    // TODO: Handle heartbeat
}

void ESPNowManager::handlePeerDiscovery(const uint8_t* mac, const uint8_t* data, int dataLen) {
    // TODO: Handle peer discovery
}

void ESPNowManager::updatePeerInfo(const uint8_t* mac, int32_t rssi) {
    ESPNowPeer* peer = getPeer(mac);
    if (peer) {
        peer->rssi = rssi;
        peer->lastSeen = millis();
    }
}

void ESPNowManager::checkPeerTimeouts() {
    // TODO: Remove inactive peers
}

uint8_t ESPNowManager::generateMessageId() {
    return nextMessageId++;
}

uint8_t ESPNowManager::calculateChecksum(const ESPNowMessage& message) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < message.dataLength; ++i) sum += message.data[i];
    return sum;
}

bool ESPNowManager::validateChecksum(const ESPNowMessage& message) {
    return calculateChecksum(message) == message.checksum;
}

bool ESPNowManager::encryptData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen) {
    // TODO: Implement encryption
    memcpy(output, input, inputLen);
    outputLen = inputLen;
    return true;
}

bool ESPNowManager::decryptData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen) {
    // TODO: Implement decryption
    memcpy(output, input, inputLen);
    outputLen = inputLen;
    return true;
}

bool ESPNowManager::compressData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen) {
    // TODO: Implement compression
    memcpy(output, input, inputLen);
    outputLen = inputLen;
    return true;
}

bool ESPNowManager::decompressData(const uint8_t* input, uint16_t inputLen, uint8_t* output, uint16_t& outputLen) {
    // TODO: Implement decompression
    memcpy(output, input, inputLen);
    outputLen = inputLen;
    return true;
}

void ESPNowManager::updateStats() {
    stats.lastUpdateTime = millis();
}

void ESPNowManager::logESPNowEvent(const String& event, ErrorSeverity severity) {
    ErrorHandler::log(ErrorCode::ESPNOW_EVENT, severity, event);
}

String ESPNowManager::getMessageTypeName(ESPNowMessageType type) {
    switch (type) {
        case ESPNowMessageType::SENSOR_DATA: return "SensorData";
        case ESPNowMessageType::SYSTEM_STATUS: return "SystemStatus";
        case ESPNowMessageType::CONFIG_UPDATE: return "ConfigUpdate";
        case ESPNowMessageType::COMMAND: return "Command";
        case ESPNowMessageType::RESPONSE: return "Response";
        case ESPNowMessageType::HEARTBEAT: return "Heartbeat";
        case ESPNowMessageType::PEER_DISCOVERY: return "PeerDiscovery";
        case ESPNowMessageType::ERROR_REPORT: return "ErrorReport";
        case ESPNowMessageType::DATA_REQUEST: return "DataRequest";
        case ESPNowMessageType::DATA_RESPONSE: return "DataResponse";
        case ESPNowMessageType::PEER_INFO: return "PeerInfo";
        case ESPNowMessageType::ACK: return "Ack";
        case ESPNowMessageType::NACK: return "Nack";
        case ESPNowMessageType::BROADCAST: return "Broadcast";
        case ESPNowMessageType::UNKNOWN: return "Unknown";
        default: return "Unknown";
    }
}

String ESPNowManager::getStateName(ESPNowState state) {
    switch (state) {
        case ESPNowState::UNINITIALIZED: return "Uninitialized";
        case ESPNowState::INITIALIZING: return "Initializing";
        case ESPNowState::READY: return "Ready";
        case ESPNowState::SENDING: return "Sending";
        case ESPNowState::RECEIVING: return "Receiving";
        case ESPNowState::ERROR: return "Error";
        case ESPNowState::DISABLED: return "Disabled";
        default: return "Unknown";
    }
} 