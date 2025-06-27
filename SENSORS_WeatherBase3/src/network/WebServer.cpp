/**
 * @file WebServer.cpp
 * @brief Web server manager implementation for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 */

#include "WebServer.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Global web server manager instance
WebServerManager& webServerManager = WebServerManager::getInstance();

WebServerManager::WebServerManager() :
    config{},
    stats{},
    initialized(false),
    running(false),
    httpServer(nullptr),
    wsServer(nullptr),
    systemManager(nullptr),
    sensorManager(nullptr),
    storageManager(nullptr),
    wifiManager(nullptr),
    nextClientId(1),
    startTime(0),
    lastStatsUpdate(0) {}

WebServerManager::~WebServerManager() {
    stop();
}

WebServerManager& WebServerManager::getInstance() {
    static WebServerManager instance;
    return instance;
}

bool WebServerManager::initialize() {
    if (initialized) return true;
    if (!initializeHTTPServer() || !initializeWebSocketServer()) {
        ErrorHandler::log(ErrorCode::WEBSERVER_INIT_FAILED, ErrorSeverity::ERROR, "Web server initialization failed");
        return false;
    }
    registerDefaultEndpoints();
    initialized = true;
    running = true;
    startTime = millis();
    return true;
}

bool WebServerManager::updateConfig(const WebServerConfig& newConfig) {
    config = newConfig;
    // TODO: Save config to storage
    return true;
}

void WebServerManager::clearStats() {
    stats = {};
}

bool WebServerManager::start() {
    if (!initialized) return initialize();
    running = true;
    return true;
}

bool WebServerManager::stop() {
    running = false;
    if (httpServer) { delete httpServer; httpServer = nullptr; }
    if (wsServer) { delete wsServer; wsServer = nullptr; }
    return true;
}

bool WebServerManager::isRunning() const {
    return running;
}

bool WebServerManager::registerEndpoint(const APIEndpoint& endpoint) {
    endpointMap[endpoint.path] = endpoint;
    endpoints.push_back(endpoint);
    return true;
}

bool WebServerManager::unregisterEndpoint(const String& path) {
    endpointMap.erase(path);
    endpoints.erase(std::remove_if(endpoints.begin(), endpoints.end(), [&](const APIEndpoint& e){ return e.path == path; }), endpoints.end());
    return true;
}

std::vector<APIEndpoint> WebServerManager::getEndpoints() {
    return endpoints;
}

bool WebServerManager::sendWebSocketMessage(uint8_t clientId, const String& message) {
    if (!wsServer) return false;
    wsServer->sendTXT(clientId, message);
    return true;
}

uint8_t WebServerManager::broadcastWebSocketMessage(const String& message) {
    if (!wsServer) return 0;
    wsServer->broadcastTXT(message);
    return wsClients.size();
}

std::vector<WebSocketClient> WebServerManager::getWebSocketClients() {
    std::vector<WebSocketClient> v;
    for (const auto& c : wsClients) v.push_back(c.second);
    return v;
}

bool WebServerManager::disconnectWebSocketClient(uint8_t clientId) {
    if (!wsServer) return false;
    wsServer->disconnect(clientId);
    wsClients.erase(clientId);
    return true;
}

bool WebServerManager::setCredentials(const String& username, const String& password) {
    authUsername = username;
    authPassword = password;
    return true;
}

bool WebServerManager::isAuthenticated(WebServer& server) {
    // TODO: Implement authentication check
    return true;
}

bool WebServerManager::requireAuthentication(WebServer& server) {
    // TODO: Implement authentication enforcement
    return true;
}

void WebServerManager::sendJSONResponse(WebServer& server, HTTPResponseCode code, const JsonDocument& json) {
    String out;
    serializeJson(json, out);
    sendJSONResponse(server, code, out);
}

void WebServerManager::sendJSONResponse(WebServer& server, HTTPResponseCode code, const String& json) {
    server.send((uint16_t)code, "application/json", json);
}

void WebServerManager::sendErrorResponse(WebServer& server, HTTPResponseCode code, const String& message) {
    server.send((uint16_t)code, "application/json", String("{\"error\":\"") + message + "\"}");
}

void WebServerManager::sendSuccessResponse(WebServer& server, const String& message) {
    server.send(200, "application/json", String("{\"success\":\"") + message + "\"}");
}

bool WebServerManager::sendFileResponse(WebServer& server, const String& path, const String& contentType) {
    // TODO: Implement file serving
    return false;
}

bool WebServerManager::checkRateLimit(const String& clientIP) {
    // TODO: Implement rate limiting
    return true;
}

void WebServerManager::resetRateLimit(const String& clientIP) {
    // TODO: Implement rate limit reset
}

String WebServerManager::getClientIP(WebServer& server) {
    return server.client().remoteIP().toString();
}

String WebServerManager::getMethodString(HTTPMethod method) {
    switch (method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        case HTTP_PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}

bool WebServerManager::parseJSONRequest(WebServer& server, JsonDocument& json) {
    DeserializationError err = deserializeJson(json, server.arg("plain"));
    return !err;
}

bool WebServerManager::validateJSONSchema(const JsonDocument& json, const std::vector<String>& requiredFields) {
    for (const auto& field : requiredFields) {
        if (!json.containsKey(field)) return false;
    }
    return true;
}

bool WebServerManager::performHealthCheck() {
    // TODO: Implement health check
    return true;
}

String WebServerManager::getWebServerInfo() {
    String info = "{";
    info += "\"requests\":" + String(stats.totalRequests) + ",";
    info += "\"wsConnections\":" + String(stats.wsConnections) + "}";
    return info;
}

String WebServerManager::getStatusString() {
    return running ? "Running" : "Stopped";
}

void WebServerManager::loop() {
    if (httpServer) httpServer->handleClient();
    if (wsServer) wsServer->loop();
    // TODO: Periodic stats update, client cleanup
}

// Private helpers
bool WebServerManager::initializeHTTPServer() {
    httpServer = new WebServer(config.port);
    // TODO: Register handlers
    httpServer->begin();
    return true;
}

bool WebServerManager::initializeWebSocketServer() {
    wsServer = new WebSocketsServer(config.wsPort);
    wsServer->begin();
    wsServer->onEvent(onWebSocketEvent);
    return true;
}

bool WebServerManager::registerDefaultEndpoints() {
    // TODO: Register default REST API endpoints
    return true;
}

void WebServerManager::handleHTTPRequest(WebServer& server) {
    // TODO: Implement request handling
}

void WebServerManager::handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    // TODO: Implement WebSocket event handling
}

void WebServerManager::processWebSocketMessage(uint8_t clientId, const String& message) {
    // TODO: Implement WebSocket message processing
}

void WebServerManager::updateWebSocketActivity(uint8_t clientId) {
    // TODO: Update client activity timestamp
}

void WebServerManager::cleanupInactiveWebSocketClients() {
    // TODO: Remove inactive clients
}

void WebServerManager::updateStats() {
    stats.lastRequestTime = millis();
}

void WebServerManager::addCORSHeaders(WebServer& server) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,PATCH,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
}

String WebServerManager::generateAuthToken(const String& username, const String& password) {
    // TODO: Implement token generation
    return username + ":token";
}

bool WebServerManager::validateAuthToken(const String& token) {
    // TODO: Implement token validation
    return true;
}

void WebServerManager::logWebServerEvent(const String& event, ErrorSeverity severity) {
    ErrorHandler::log(ErrorCode::WEBSERVER_EVENT, severity, event);
}

String WebServerManager::getResponseCodeString(HTTPResponseCode code) {
    return String((uint16_t)code);
}

String WebServerManager::getContentType(const String& extension) {
    if (extension == ".html") return "text/html";
    if (extension == ".json") return "application/json";
    if (extension == ".css") return "text/css";
    if (extension == ".js") return "application/javascript";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".ico") return "image/x-icon";
    return "application/octet-stream";
} 