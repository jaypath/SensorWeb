/**
 * @file WebServer.hpp
 * @brief Web server and REST API for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides comprehensive web functionality including:
 * - REST API endpoints for data access
 * - Real-time WebSocket updates
 * - Configuration management interface
 * - System monitoring and diagnostics
 * - Security and authentication
 */

#ifndef WEB_SERVER_HPP
#define WEB_SERVER_HPP

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include <functional>
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Forward declarations
class SystemManager;
class SensorManager;
class StorageManager;
class WiFiManager;

/**
 * @brief HTTP response codes
 */
enum class HTTPResponseCode : uint16_t {
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    CONFLICT = 409,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    SERVICE_UNAVAILABLE = 503
};

/**
 * @brief API endpoint types
 */
enum class EndpointType : uint8_t {
    GET = 0x01,
    POST = 0x02,
    PUT = 0x04,
    DELETE = 0x08,
    PATCH = 0x10,
    ALL = 0x1F
};

/**
 * @brief API endpoint structure
 */
struct APIEndpoint {
    String path;                    ///< Endpoint path
    EndpointType methods;           ///< Supported HTTP methods
    String description;             ///< Endpoint description
    bool requiresAuth;              ///< Requires authentication
    std::function<void()> handler;  ///< Request handler function
    
    APIEndpoint() : methods(EndpointType::GET), requiresAuth(false) {}
};

/**
 * @brief WebSocket client information
 */
struct WebSocketClient {
    uint8_t id;                     ///< Client ID
    String ip;                      ///< Client IP address
    uint32_t connectTime;           ///< Connection timestamp
    uint32_t lastActivity;          ///< Last activity timestamp
    bool authenticated;             ///< Authentication status
    String userAgent;               ///< User agent string
    
    WebSocketClient() : id(0), connectTime(0), lastActivity(0), authenticated(false) {}
};

/**
 * @brief Web server configuration
 */
struct WebServerConfig {
    uint16_t port;                  ///< Server port
    uint16_t wsPort;                ///< WebSocket port
    uint32_t maxClients;            ///< Maximum concurrent clients
    uint32_t requestTimeout;        ///< Request timeout (ms)
    uint32_t wsTimeout;             ///< WebSocket timeout (ms)
    bool enableCORS;                ///< Enable CORS headers
    bool enableCompression;         ///< Enable response compression
    bool enableAuthentication;      ///< Enable authentication
    String username;                ///< Admin username
    String password;                ///< Admin password
    uint32_t sessionTimeout;        ///< Session timeout (seconds)
    bool enableRateLimiting;        ///< Enable rate limiting
    uint32_t rateLimitRequests;     ///< Requests per minute
    uint32_t rateLimitWindow;       ///< Rate limit window (seconds)
    
    // Default constructor
    WebServerConfig() : 
        port(80),
        wsPort(81),
        maxClients(10),
        requestTimeout(30000),      // 30 seconds
        wsTimeout(60000),           // 60 seconds
        enableCORS(true),
        enableCompression(true),
        enableAuthentication(true),
        username("admin"),
        password("S3nsor.N3t!"),
        sessionTimeout(3600),       // 1 hour
        enableRateLimiting(true),
        rateLimitRequests(60),      // 60 requests per minute
        rateLimitWindow(60) {}      // 1 minute window
};

/**
 * @brief Web server statistics
 */
struct WebServerStats {
    uint32_t totalRequests;         ///< Total HTTP requests
    uint32_t successfulRequests;    ///< Successful requests
    uint32_t failedRequests;        ///< Failed requests
    uint32_t wsConnections;         ///< WebSocket connections
    uint32_t wsMessages;            ///< WebSocket messages
    uint32_t authenticationFailures; ///< Authentication failures
    uint32_t rateLimitHits;         ///< Rate limit violations
    uint32_t lastRequestTime;       ///< Last request timestamp
    uint32_t uptime;                ///< Server uptime (seconds)
    float averageResponseTime;      ///< Average response time (ms)
};

/**
 * @brief Comprehensive web server class
 * 
 * This class provides a complete web interface with:
 * - REST API endpoints for all system functions
 * - Real-time WebSocket updates
 * - Configuration management
 * - System monitoring and diagnostics
 * - Security and authentication
 * - Rate limiting and CORS support
 */
class WebServerManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to WebServerManager instance
     */
    static WebServerManager& getInstance();
    
    /**
     * @brief Initialize web server
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Get web server configuration
     * @return Reference to web server configuration
     */
    const WebServerConfig& getConfig() const { return config; }
    
    /**
     * @brief Update web server configuration
     * @param newConfig New configuration
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const WebServerConfig& newConfig);
    
    /**
     * @brief Get web server statistics
     * @return Reference to web server statistics
     */
    const WebServerStats& getStats() const { return stats; }
    
    /**
     * @brief Clear web server statistics
     */
    void clearStats();
    
    // Server management
    /**
     * @brief Start web server
     * @return true if start successful, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop web server
     * @return true if stop successful, false otherwise
     */
    bool stop();
    
    /**
     * @brief Check if server is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Get server port
     * @return Server port
     */
    uint16_t getPort() const { return config.port; }
    
    /**
     * @brief Get WebSocket port
     * @return WebSocket port
     */
    uint16_t getWebSocketPort() const { return config.wsPort; }
    
    // API endpoint management
    /**
     * @brief Register API endpoint
     * @param endpoint API endpoint configuration
     * @return true if registration successful, false otherwise
     */
    bool registerEndpoint(const APIEndpoint& endpoint);
    
    /**
     * @brief Unregister API endpoint
     * @param path Endpoint path
     * @return true if unregistration successful, false otherwise
     */
    bool unregisterEndpoint(const String& path);
    
    /**
     * @brief Get registered endpoints
     * @return Vector of registered endpoints
     */
    std::vector<APIEndpoint> getEndpoints();
    
    // WebSocket management
    /**
     * @brief Send message to WebSocket client
     * @param clientId Client ID
     * @param message Message to send
     * @return true if send successful, false otherwise
     */
    bool sendWebSocketMessage(uint8_t clientId, const String& message);
    
    /**
     * @brief Broadcast message to all WebSocket clients
     * @param message Message to broadcast
     * @return Number of clients that received the message
     */
    uint8_t broadcastWebSocketMessage(const String& message);
    
    /**
     * @brief Get WebSocket clients
     * @return Vector of connected clients
     */
    std::vector<WebSocketClient> getWebSocketClients();
    
    /**
     * @brief Disconnect WebSocket client
     * @param clientId Client ID
     * @return true if disconnect successful, false otherwise
     */
    bool disconnectWebSocketClient(uint8_t clientId);
    
    // Authentication
    /**
     * @brief Set authentication credentials
     * @param username Username
     * @param password Password
     * @return true if set successful, false otherwise
     */
    bool setCredentials(const String& username, const String& password);
    
    /**
     * @brief Check if request is authenticated
     * @param request WebServer request
     * @return true if authenticated, false otherwise
     */
    bool isAuthenticated(WebServer& server);
    
    /**
     * @brief Require authentication for request
     * @param server WebServer instance
     * @return true if authentication successful, false otherwise
     */
    bool requireAuthentication(WebServer& server);
    
    // Response helpers
    /**
     * @brief Send JSON response
     * @param server WebServer instance
     * @param code HTTP response code
     * @param json JSON data
     */
    void sendJSONResponse(WebServer& server, HTTPResponseCode code, const JsonDocument& json);
    
    /**
     * @brief Send JSON response
     * @param server WebServer instance
     * @param code HTTP response code
     * @param json JSON string
     */
    void sendJSONResponse(WebServer& server, HTTPResponseCode code, const String& json);
    
    /**
     * @brief Send error response
     * @param server WebServer instance
     * @param code HTTP response code
     * @param message Error message
     */
    void sendErrorResponse(WebServer& server, HTTPResponseCode code, const String& message);
    
    /**
     * @brief Send success response
     * @param server WebServer instance
     * @param message Success message
     */
    void sendSuccessResponse(WebServer& server, const String& message);
    
    /**
     * @brief Send file response
     * @param server WebServer instance
     * @param path File path
     * @param contentType Content type
     * @return true if send successful, false otherwise
     */
    bool sendFileResponse(WebServer& server, const String& path, const String& contentType);
    
    // Rate limiting
    /**
     * @brief Check rate limit for client
     * @param clientIP Client IP address
     * @return true if within rate limit, false otherwise
     */
    bool checkRateLimit(const String& clientIP);
    
    /**
     * @brief Reset rate limit for client
     * @param clientIP Client IP address
     */
    void resetRateLimit(const String& clientIP);
    
    // Utility functions
    /**
     * @brief Get client IP address
     * @param server WebServer instance
     * @return Client IP address
     */
    String getClientIP(WebServer& server);
    
    /**
     * @brief Get request method as string
     * @param method HTTP method
     * @return Method string
     */
    String getMethodString(HTTPMethod method);
    
    /**
     * @brief Parse JSON request body
     * @param server WebServer instance
     * @param json Output JSON document
     * @return true if parse successful, false otherwise
     */
    bool parseJSONRequest(WebServer& server, JsonDocument& json);
    
    /**
     * @brief Validate JSON schema
     * @param json JSON document
     * @param requiredFields Required field names
     * @return true if valid, false otherwise
     */
    bool validateJSONSchema(const JsonDocument& json, const std::vector<String>& requiredFields);
    
    /**
     * @brief Perform web server health check
     * @return true if healthy, false otherwise
     */
    bool performHealthCheck();
    
    /**
     * @brief Get web server information as JSON string
     * @return JSON string with server information
     */
    String getWebServerInfo();
    
    /**
     * @brief Get web server status as human-readable string
     * @return Status string
     */
    String getStatusString();
    
    /**
     * @brief Main web server loop
     * 
     * This method should be called regularly to handle:
     * - HTTP requests
     * - WebSocket connections
     * - Client management
     * - Statistics updates
     */
    void loop();

private:
    // Singleton pattern - prevent external instantiation
    WebServerManager();
    ~WebServerManager();
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;
    
    // Configuration and state
    WebServerConfig config;
    WebServerStats stats;
    bool initialized;
    bool running;
    
    // Server instances
    WebServer* httpServer;
    WebSocketsServer* wsServer;
    
    // Manager references
    SystemManager* systemManager;
    SensorManager* sensorManager;
    StorageManager* storageManager;
    WiFiManager* wifiManager;
    
    // Endpoint management
    std::vector<APIEndpoint> endpoints;
    std::map<String, APIEndpoint> endpointMap;
    
    // WebSocket management
    std::map<uint8_t, WebSocketClient> wsClients;
    uint8_t nextClientId;
    
    // Rate limiting
    std::map<String, std::vector<uint32_t>> rateLimitMap;
    
    // Authentication
    String authUsername;
    String authPassword;
    
    // Timing
    uint32_t startTime;
    uint32_t lastStatsUpdate;
    
    // Callback functions
    static void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    
    /**
     * @brief Initialize HTTP server
     * @return true if initialization successful, false otherwise
     */
    bool initializeHTTPServer();
    
    /**
     * @brief Initialize WebSocket server
     * @return true if initialization successful, false otherwise
     */
    bool initializeWebSocketServer();
    
    /**
     * @brief Register default endpoints
     * @return true if registration successful, false otherwise
     */
    bool registerDefaultEndpoints();
    
    /**
     * @brief Handle HTTP request
     * @param server WebServer instance
     */
    void handleHTTPRequest(WebServer& server);
    
    /**
     * @brief Handle WebSocket event
     * @param num Client number
     * @param type Event type
     * @param payload Event payload
     * @param length Payload length
     */
    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    
    /**
     * @brief Process WebSocket message
     * @param clientId Client ID
     * @param message Message content
     */
    void processWebSocketMessage(uint8_t clientId, const String& message);
    
    /**
     * @brief Update WebSocket client activity
     * @param clientId Client ID
     */
    void updateWebSocketActivity(uint8_t clientId);
    
    /**
     * @brief Clean up inactive WebSocket clients
     */
    void cleanupInactiveWebSocketClients();
    
    /**
     * @brief Update web server statistics
     */
    void updateStats();
    
    /**
     * @brief Add CORS headers
     * @param server WebServer instance
     */
    void addCORSHeaders(WebServer& server);
    
    /**
     * @brief Generate authentication token
     * @param username Username
     * @param password Password
     * @return Authentication token
     */
    String generateAuthToken(const String& username, const String& password);
    
    /**
     * @brief Validate authentication token
     * @param token Authentication token
     * @return true if valid, false otherwise
     */
    bool validateAuthToken(const String& token);
    
    /**
     * @brief Log web server event
     * @param event Event description
     * @param severity Event severity
     */
    void logWebServerEvent(const String& event, ErrorSeverity severity = ErrorSeverity::INFO);
    
    /**
     * @brief Get HTTP response code string
     * @param code HTTP response code
     * @return Response code string
     */
    String getResponseCodeString(HTTPResponseCode code);
    
    /**
     * @brief Get content type for file extension
     * @param extension File extension
     * @return Content type
     */
    String getContentType(const String& extension);
};

// Global web server manager instance
extern WebServerManager& webServerManager;

// Convenience macros for web server management
#define WEBSERVER_INIT() webServerManager.initialize()
#define WEBSERVER_START() webServerManager.start()
#define WEBSERVER_STOP() webServerManager.stop()
#define WEBSERVER_RUNNING() webServerManager.isRunning()
#define WEBSERVER_LOOP() webServerManager.loop()
#define WEBSERVER_BROADCAST(msg) webServerManager.broadcastWebSocketMessage(msg)
#define WEBSERVER_HEALTH_CHECK() webServerManager.performHealthCheck()

#endif // WEB_SERVER_HPP 