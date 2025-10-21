/**
 * @file WiFiManager.cpp
 * @brief WiFi manager implementation for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 */

#include "WiFiManager.hpp"
#include "../network/ESPNowManager.hpp"

// Global WiFi manager instance
WiFiManager& wifiManager = WiFiManager::getInstance();

WiFiManager::WiFiManager() : 
    currentState(WiFiState::DISCONNECTED),
    lastConnectionAttempt(0),
    lastReconnectAttempt(0),
    connectionStartTime(0),
    reconnectAttempts(0),
    autoReconnect(true),
    apModeActive(false),
    apStartTime(0),
    apTimeout(300000),
    currentRSSI(0),
    connectionUptime(0),
    initialized(false),
    espNowManager(nullptr) {
    
    // Initialize MAC address
    WiFi.macAddress(localMAC);
}

WiFiManager::~WiFiManager() {
    if (initialized) {
        disconnect();
        preferences.end();
    }
}

WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

bool WiFiManager::initialize() {
    if (initialized) {
        return true;
    }
    
    ErrorHandler::log(ErrorCode::WIFI_INIT_START, ErrorSeverity::INFO, "WiFi manager initialization started");
    
    // Initialize preferences
    if (!preferences.begin("wifi", false)) {
        ErrorHandler::log(ErrorCode::WIFI_PREFS_INIT_FAILED, ErrorSeverity::ERROR, "Failed to initialize WiFi preferences");
        return false;
    }
    
    // Load configuration
    if (!loadConfiguration()) {
        ErrorHandler::log(ErrorCode::WIFI_CONFIG_LOAD_FAILED, ErrorSeverity::WARNING, "Failed to load WiFi configuration, using defaults");
    }
    
    // Set WiFi mode
    WiFi.mode(WIFI_MODE_APSTA);
    
    // Set power save mode
    if (config.enablePowerSave) {
        WiFi.setSleep(true);
    }
    
    // Set transmit power
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    
    // Set local MAC address
    WiFi.macAddress(localMAC);
    
    initialized = true;
    updateState(WiFiState::DISCONNECTED);
    
    ErrorHandler::log(ErrorCode::WIFI_INIT_SUCCESS, ErrorSeverity::INFO, "WiFi manager initialized successfully");
    
    return true;
}

bool WiFiManager::connect(const String& ssid, const String& password, uint32_t timeout) {
    if (!initialized) {
        ErrorHandler::log(ErrorCode::WIFI_NOT_INITIALIZED, ErrorSeverity::ERROR, "WiFi manager not initialized");
        return false;
    }
    
    if (ssid.isEmpty()) {
        ErrorHandler::log(ErrorCode::WIFI_INVALID_SSID, ErrorSeverity::ERROR, "Invalid SSID provided");
        return false;
    }
    
    ErrorHandler::log(ErrorCode::WIFI_CONNECT_ATTEMPT, ErrorSeverity::INFO, 
        StringUtils::format("Attempting to connect to: %s", ssid.c_str()));
    
    updateState(WiFiState::CONNECTING);
    connectionStartTime = millis();
    lastConnectionAttempt = millis();
    
    // Store credentials if provided
    if (!password.isEmpty()) {
        storeCredentials(ssid, password);
    }
    
    return attemptConnection(ssid, password, timeout);
}

bool WiFiManager::connect(uint32_t timeout) {
    String ssid, password;
    if (!loadCredentials(ssid, password)) {
        ErrorHandler::log(ErrorCode::WIFI_NO_CREDENTIALS, ErrorSeverity::WARNING, "No stored credentials found");
        return false;
    }
    
    return connect(ssid, password, timeout);
}

bool WiFiManager::disconnect() {
    if (!initialized) {
        return false;
    }
    
    if (currentState == WiFiState::DISCONNECTED) {
        return true;
    }
    
    ErrorHandler::log(ErrorCode::WIFI_DISCONNECT, ErrorSeverity::INFO, "Disconnecting from WiFi");
    
    WiFi.disconnect();
    updateState(WiFiState::DISCONNECTED);
    
    // Stop AP mode if active
    if (apModeActive) {
        stopAP();
    }
    
    return true;
}

bool WiFiManager::isConnected() const {
    return currentState == WiFiState::CONNECTED && WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getIPAddress() const {
    if (!isConnected()) {
        return "";
    }
    return WiFi.localIP().toString();
}

String WiFiManager::getMACAddress() const {
    return WiFi.macAddress();
}

int32_t WiFiManager::getRSSI() const {
    if (!isConnected()) {
        return 0;
    }
    return WiFi.RSSI();
}

String WiFiManager::getSSID() const {
    if (!isConnected()) {
        return "";
    }
    return WiFi.SSID();
}

String WiFiManager::getBSSID() const {
    if (!isConnected()) {
        return "";
    }
    return WiFi.BSSIDstr();
}

uint8_t WiFiManager::getChannel() const {
    if (!isConnected()) {
        return 0;
    }
    return WiFi.channel();
}

bool WiFiManager::updateConfig(const WiFiConfig& newConfig) {
    config = newConfig;
    return saveConfiguration();
}

void WiFiManager::clearStats() {
    memset(&stats, 0, sizeof(stats));
}

std::vector<WiFiNetwork> WiFiManager::scanNetworks(uint8_t maxNetworks) {
    std::vector<WiFiNetwork> networks;
    
    if (!initialized) {
        return networks;
    }
    
    updateState(WiFiState::SCANNING);
    
    int n = WiFi.scanNetworks();
    if (n == WIFI_SCAN_FAILED) {
        ErrorHandler::log(ErrorCode::WIFI_SCAN_FAILED, ErrorSeverity::ERROR, "WiFi scan failed");
        updateState(WiFiState::DISCONNECTED);
        return networks;
    }
    
    for (int i = 0; i < min(n, (int)maxNetworks); i++) {
        WiFiNetwork network;
        network.ssid = WiFi.SSID(i);
        network.rssi = WiFi.RSSI(i);
        network.encryption = WiFi.encryptionType(i);
        network.channel = WiFi.channel(i);
        network.isConnected = (network.ssid == getSSID());
        
        networks.push_back(network);
    }
    
    updateState(WiFiState::DISCONNECTED);
    return networks;
}

bool WiFiManager::startAP(const String& ssid, const String& password, uint32_t timeout) {
    if (!initialized) {
        return false;
    }
    
    if (apModeActive) {
        return true; // Already in AP mode
    }
    
    String apSSID = ssid.isEmpty() ? generateAPSSID() : ssid;
    String apPassword = password.isEmpty() ? config.apPassword : password;
    apTimeout = timeout;
    
    ErrorHandler::log(ErrorCode::WIFI_AP_START, ErrorSeverity::INFO, 
        StringUtils::format("Starting AP mode: %s", apSSID.c_str()));
    
    // Configure AP
    WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    
    apModeActive = true;
    apStartTime = millis();
    this->apSSID = apSSID;
    this->apPassword = apPassword;
    
    updateState(WiFiState::AP_MODE);
    
    return true;
}

bool WiFiManager::stopAP() {
    if (!apModeActive) {
        return true;
    }
    
    ErrorHandler::log(ErrorCode::WIFI_AP_STOP, ErrorSeverity::INFO, "Stopping AP mode");
    
    WiFi.softAPdisconnect();
    apModeActive = false;
    apStartTime = 0;
    
    if (currentState == WiFiState::AP_MODE) {
        updateState(WiFiState::DISCONNECTED);
    }
    
    return true;
}

bool WiFiManager::isAPMode() const {
    return apModeActive;
}

String WiFiManager::getAPIPAddress() const {
    if (!apModeActive) {
        return "";
    }
    return WiFi.softAPIP().toString();
}

String WiFiManager::getAPSSID() const {
    return apSSID;
}

bool WiFiManager::storeCredentials(const String& ssid, const String& password) {
    if (!initialized) {
        return false;
    }
    
    String encryptedSSID, encryptedPassword;
    if (!encryptCredentials(ssid, encryptedSSID) || !encryptCredentials(password, encryptedPassword)) {
        ErrorHandler::log(ErrorCode::WIFI_CREDENTIAL_ENCRYPT_FAILED, ErrorSeverity::ERROR, "Failed to encrypt credentials");
        return false;
    }
    
    preferences.putString("ssid", encryptedSSID);
    preferences.putString("password", encryptedPassword);
    
    ErrorHandler::log(ErrorCode::WIFI_CREDENTIALS_STORED, ErrorSeverity::INFO, "WiFi credentials stored");
    
    return true;
}

bool WiFiManager::loadCredentials(String& ssid, String& password) {
    if (!initialized) {
        return false;
    }
    
    String encryptedSSID = preferences.getString("ssid", "");
    String encryptedPassword = preferences.getString("password", "");
    
    if (encryptedSSID.isEmpty() || encryptedPassword.isEmpty()) {
        return false;
    }
    
    if (!decryptCredentials(encryptedSSID, ssid) || !decryptCredentials(encryptedPassword, password)) {
        ErrorHandler::log(ErrorCode::WIFI_CREDENTIAL_DECRYPT_FAILED, ErrorSeverity::ERROR, "Failed to decrypt credentials");
        return false;
    }
    
    return true;
}

bool WiFiManager::clearCredentials() {
    if (!initialized) {
        return false;
    }
    
    preferences.remove("ssid");
    preferences.remove("password");
    
    ErrorHandler::log(ErrorCode::WIFI_CREDENTIALS_CLEARED, ErrorSeverity::INFO, "WiFi credentials cleared");
    
    return true;
}

bool WiFiManager::hasCredentials() const {
    if (!initialized) {
        return false;
    }
    
    return !preferences.getString("ssid", "").isEmpty();
}

bool WiFiManager::setPowerSave(bool enable) {
    if (!initialized) {
        return false;
    }
    
    WiFi.setSleep(enable);
    config.enablePowerSave = enable;
    saveConfiguration();
    
    return true;
}

bool WiFiManager::setTxPower(uint8_t power) {
    if (!initialized) {
        return false;
    }
    
    if (power > 20) {
        power = 20;
    }
    
    WiFi.setTxPower((wifi_power_t)power);
    config.txPower = power;
    saveConfiguration();
    
    return true;
}

String WiFiManager::getDiagnostics() {
    String diagnostics = "{";
    diagnostics += "\"state\":\"" + getStatusString() + "\",";
    diagnostics += "\"connected\":" + String(isConnected() ? "true" : "false") + ",";
    diagnostics += "\"ssid\":\"" + getSSID() + "\",";
    diagnostics += "\"ip\":\"" + getIPAddress() + "\",";
    diagnostics += "\"mac\":\"" + getMACAddress() + "\",";
    diagnostics += "\"rssi\":" + String(getRSSI()) + ",";
    diagnostics += "\"channel\":" + String(getChannel()) + ",";
    diagnostics += "\"apMode\":" + String(isAPMode() ? "true" : "false") + ",";
    diagnostics += "\"apIP\":\"" + getAPIPAddress() + "\",";
    diagnostics += "\"apSSID\":\"" + getAPSSID() + "\",";
    diagnostics += "\"connectionAttempts\":" + String(stats.connectionAttempts) + ",";
    diagnostics += "\"successfulConnections\":" + String(stats.successfulConnections) + ",";
    diagnostics += "\"failedConnections\":" + String(stats.failedConnections) + ",";
    diagnostics += "\"totalUptime\":" + String(stats.totalUptime) + ",";
    diagnostics += "\"averageRSSI\":" + String(stats.averageRSSI) + ",";
    diagnostics += "\"bytesReceived\":" + String(stats.bytesReceived) + ",";
    diagnostics += "\"bytesSent\":" + String(stats.bytesSent);
    diagnostics += "}";
    
    return diagnostics;
}

bool WiFiManager::performHealthCheck() {
    if (!initialized) {
        return false;
    }
    
    bool healthy = true;
    
    // Check connection status
    if (currentState == WiFiState::CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            ErrorHandler::log(ErrorCode::WIFI_CONNECTION_LOST, ErrorSeverity::WARNING, "WiFi connection lost");
            handleDisconnection();
            healthy = false;
        } else {
            // Update RSSI
            updateRSSI();
            
            // Check connection quality
            if (getConnectionQuality() < 30) {
                ErrorHandler::log(ErrorCode::WIFI_POOR_CONNECTION, ErrorSeverity::WARNING, "Poor WiFi connection quality");
                healthy = false;
            }
        }
    }
    
    // Check AP mode timeout
    checkAPTimeout();
    
    return healthy;
}

uint8_t WiFiManager::getConnectionQuality() const {
    return calculateConnectionQuality();
}

void WiFiManager::loop() {
    if (!initialized) {
        return;
    }
    
    uint32_t now = millis();
    
    // Handle automatic reconnection
    if (autoReconnect && currentState == WiFiState::CONNECTION_FAILED) {
        if (now - lastReconnectAttempt >= config.reconnectInterval) {
            attemptReconnection();
        }
    }
    
    // Update connection uptime
    if (currentState == WiFiState::CONNECTED) {
        connectionUptime = (now - connectionStartTime) / 1000;
    }
    
    // Update statistics periodically
    if (now - lastStatsUpdate >= 60000) { // Every minute
        updateStats();
        lastStatsUpdate = now;
    }
    
    // Check AP mode timeout
    checkAPTimeout();
}

String WiFiManager::getWiFiInfo() {
    String info = "{";
    info += "\"state\":\"" + getStatusString() + "\",";
    info += "\"connected\":" + String(isConnected() ? "true" : "false") + ",";
    info += "\"ssid\":\"" + getSSID() + "\",";
    info += "\"ip\":\"" + getIPAddress() + "\",";
    info += "\"mac\":\"" + getMACAddress() + "\",";
    info += "\"rssi\":" + String(getRSSI()) + ",";
    info += "\"channel\":" + String(getChannel()) + ",";
    info += "\"quality\":" + String(getConnectionQuality()) + ",";
    info += "\"uptime\":" + String(connectionUptime) + ",";
    info += "\"apMode\":" + String(isAPMode() ? "true" : "false") + ",";
    info += "\"apIP\":\"" + getAPIPAddress() + "\",";
    info += "\"apSSID\":\"" + getAPSSID() + "\"";
    info += "}";
    
    return info;
}

String WiFiManager::getStatusString() {
    switch (currentState) {
        case WiFiState::DISCONNECTED: return "Disconnected";
        case WiFiState::CONNECTING: return "Connecting";
        case WiFiState::CONNECTED: return "Connected";
        case WiFiState::CONNECTION_FAILED: return "Connection Failed";
        case WiFiState::AP_MODE: return "AP Mode";
        case WiFiState::SCANNING: return "Scanning";
        case WiFiState::ERROR: return "Error";
        default: return "Unknown";
    }
}

bool WiFiManager::attemptConnection(const String& ssid, const String& password, uint32_t timeout) {
    stats.connectionAttempts++;
    
    // Set WiFi mode
    WiFi.mode(WIFI_MODE_APSTA);
    
    // Attempt connection
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Wait for connection
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        handleConnectionSuccess();
        return true;
    } else {
        handleConnectionFailure();
        return false;
    }
}

void WiFiManager::handleConnectionSuccess() {
    currentSSID = WiFi.SSID();
    currentBSSID = WiFi.BSSIDstr();
    currentChannel = WiFi.channel();
    updateRSSI();
    
    stats.successfulConnections++;
    stats.lastConnectionTime = millis();
    
    updateState(WiFiState::CONNECTED);
    
    logWiFiEvent(StringUtils::format("Successfully connected to: %s", currentSSID.c_str()), ErrorSeverity::INFO);
}

void WiFiManager::handleConnectionFailure() {
    stats.failedConnections++;
    
    updateState(WiFiState::CONNECTION_FAILED);
    
    logWiFiEvent("WiFi connection failed", ErrorSeverity::ERROR);
    
    // Start AP mode if enabled
    if (config.enableAPMode) {
        startAP();
    }
}

void WiFiManager::handleDisconnection() {
    stats.disconnectionCount++;
    
    updateState(WiFiState::DISCONNECTED);
    
    logWiFiEvent("WiFi disconnected", ErrorSeverity::WARNING);
}

bool WiFiManager::attemptReconnection() {
    lastReconnectAttempt = millis();
    reconnectAttempts++;
    
    String ssid, password;
    if (loadCredentials(ssid, password)) {
        return attemptConnection(ssid, password, config.connectionTimeout);
    }
    
    return false;
}

void WiFiManager::updateStats() {
    if (currentState == WiFiState::CONNECTED) {
        stats.totalUptime += 60; // Add one minute
        
        // Update average RSSI
        if (stats.averageRSSI == 0) {
            stats.averageRSSI = currentRSSI;
        } else {
            stats.averageRSSI = (stats.averageRSSI + currentRSSI) / 2;
        }
    }
}

void WiFiManager::updateRSSI() {
    if (isConnected()) {
        currentRSSI = WiFi.RSSI();
    }
}

void WiFiManager::checkAPTimeout() {
    if (apModeActive && (millis() - apStartTime) > apTimeout) {
        ErrorHandler::log(ErrorCode::WIFI_AP_TIMEOUT, ErrorSeverity::INFO, "AP mode timeout, stopping AP");
        stopAP();
    }
}

String WiFiManager::generateAPSSID() {
    String macStr = WiFi.macAddress();
    macStr.replace(":", "");
    String lastThree = macStr.substring(macStr.length() - 6);
    return "SensorNet-" + lastThree;
}

bool WiFiManager::encryptCredentials(const String& input, String& output) {
    // Simple XOR encryption for demonstration
    // In production, use proper encryption
    output = input;
    for (size_t i = 0; i < output.length(); i++) {
        output[i] ^= 0xAA;
    }
    return true;
}

bool WiFiManager::decryptCredentials(const String& input, String& output) {
    // Simple XOR decryption for demonstration
    // In production, use proper decryption
    output = input;
    for (size_t i = 0; i < output.length(); i++) {
        output[i] ^= 0xAA;
    }
    return true;
}

bool WiFiManager::loadConfiguration() {
    config.connectionTimeout = preferences.getUInt("connTimeout", 30000);
    config.reconnectInterval = preferences.getUInt("reconnectInterval", 5000);
    config.maxReconnectAttempts = preferences.getUInt("maxReconnectAttempts", 10);
    config.enableAPMode = preferences.getBool("enableAPMode", true);
    config.apSSID = preferences.getString("apSSID", "SensorNet-Config");
    config.apPassword = preferences.getString("apPassword", "S3nsor.N3t!");
    config.apTimeout = preferences.getUInt("apTimeout", 300000);
    config.enablePowerSave = preferences.getBool("enablePowerSave", false);
    config.txPower = preferences.getUChar("txPower", 20);
    
    return true;
}

bool WiFiManager::saveConfiguration() {
    preferences.putUInt("connTimeout", config.connectionTimeout);
    preferences.putUInt("reconnectInterval", config.reconnectInterval);
    preferences.putUInt("maxReconnectAttempts", config.maxReconnectAttempts);
    preferences.putBool("enableAPMode", config.enableAPMode);
    preferences.putString("apSSID", config.apSSID);
    preferences.putString("apPassword", config.apPassword);
    preferences.putUInt("apTimeout", config.apTimeout);
    preferences.putBool("enablePowerSave", config.enablePowerSave);
    preferences.putUChar("txPower", config.txPower);
    
    return true;
}

void WiFiManager::updateState(WiFiState newState) {
    if (currentState != newState) {
        WiFiState oldState = currentState;
        currentState = newState;
        
        logWiFiEvent(StringUtils::format("State changed from %s to %s", 
            getStatusString().c_str(), getStatusString().c_str()), ErrorSeverity::INFO);
    }
}

void WiFiManager::logWiFiEvent(const String& event, ErrorSeverity severity) {
    ErrorHandler::log(ErrorCode::WIFI_EVENT, severity, event);
}

uint8_t WiFiManager::calculateConnectionQuality() const {
    if (!isConnected()) {
        return 0;
    }
    
    int32_t rssi = getRSSI();
    
    // Convert RSSI to quality percentage (0-100)
    if (rssi >= -50) return 100;
    if (rssi >= -60) return 80;
    if (rssi >= -70) return 60;
    if (rssi >= -80) return 40;
    if (rssi >= -90) return 20;
    return 0;
} 