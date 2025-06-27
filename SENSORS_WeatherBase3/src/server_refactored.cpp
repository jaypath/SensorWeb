#include "server.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "SDCard.hpp"
#include <esp_system.h>

// New modular system includes
#include "core/SystemManager.hpp"
#include "network/WebServerManager.hpp"
#include "network/WiFiManager.hpp"
#include "sensors/SensorManager.hpp"
#include "storage/StorageManager.hpp"
#include "security/SecurityManager.hpp"
#include "utils/ErrorHandler.hpp"
#include "utils/StringUtils.hpp"

using namespace WeatherStation;

// Legacy compatibility - keep some global references for gradual migration
extern uint32_t I_currentTime;
extern uint32_t lastPwdRequestMinute;
extern STRUCT_PrefsH Prefs;
extern bool requestWiFiPassword(const uint8_t* serverMAC);

// Legacy server instance (will be replaced by WebServerManager)
#ifdef _USE8266
    ESP8266WebServer server(80);
#endif
#ifdef _USE32
    WebServer server(80);
#endif

// Legacy variables for gradual migration
String WEBHTML;
WiFi_type WIFI_INFO;

#ifdef _DEBUG
void SerialWrite(String msg) {
    Serial.printf("%s", msg.c_str());
    return;
}
#endif

// ============================================================================
// REFACTORED SERVER IMPLEMENTATION USING NEW MODULAR SYSTEM
// ============================================================================

/**
 * @brief Initialize the refactored server system
 */
ErrorCode initializeRefactoredServer() {
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        // Initialize all managers through SystemManager
        auto system_manager = SystemManager::getInstance();
        ErrorCode result = system_manager->initialize();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to initialize system manager");
        }
        
        // Initialize web server manager
        auto web_server = WebServerManager::getInstance();
        result = web_server->initialize();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to initialize web server manager");
        }
        
        // Register API endpoints
        registerAPIEndpoints();
        
        // Register legacy compatibility endpoints
        registerLegacyEndpoints();
        
        error_handler.logInfo("Refactored server system initialized successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::SERVER_INIT_FAILED, 
                                    "Server initialization exception: " + std::string(e.what()));
    }
}

/**
 * @brief Register modern REST API endpoints
 */
void registerAPIEndpoints() {
    auto web_server = WebServerManager::getInstance();
    
    // System endpoints
    web_server->registerEndpoint("GET", "/api/system/status", handleSystemStatus);
    web_server->registerEndpoint("POST", "/api/system/reboot", handleSystemReboot);
    web_server->registerEndpoint("GET", "/api/system/info", handleSystemInfo);
    
    // Sensor endpoints
    web_server->registerEndpoint("GET", "/api/sensors", handleGetSensors);
    web_server->registerEndpoint("GET", "/api/sensors/{id}", handleGetSensor);
    web_server->registerEndpoint("POST", "/api/sensors", handleAddSensor);
    web_server->registerEndpoint("PUT", "/api/sensors/{id}", handleUpdateSensor);
    web_server->registerEndpoint("DELETE", "/api/sensors/{id}", handleDeleteSensor);
    
    // Weather endpoints
    web_server->registerEndpoint("GET", "/api/weather/current", handleGetCurrentWeather);
    web_server->registerEndpoint("GET", "/api/weather/forecast", handleGetWeatherForecast);
    web_server->registerEndpoint("GET", "/api/weather/history", handleGetWeatherHistory);
    
    // Configuration endpoints
    web_server->registerEndpoint("GET", "/api/config", handleGetConfig);
    web_server->registerEndpoint("PUT", "/api/config", handleUpdateConfig);
    web_server->registerEndpoint("POST", "/api/config/wifi", handleSetWiFi);
    web_server->registerEndpoint("POST", "/api/config/reset", handleResetConfig);
    
    // Data endpoints
    web_server->registerEndpoint("GET", "/api/data/export", handleExportData);
    web_server->registerEndpoint("POST", "/api/data/import", handleImportData);
    web_server->registerEndpoint("DELETE", "/api/data/clear", handleClearData);
}

/**
 * @brief Register legacy compatibility endpoints
 */
void registerLegacyEndpoints() {
    auto web_server = WebServerManager::getInstance();
    
    // Legacy endpoints for backward compatibility
    web_server->registerEndpoint("GET", "/", handleLegacyRoot);
    web_server->registerEndpoint("GET", "/ALL", handleLegacyAll);
    web_server->registerEndpoint("GET", "/GETSTATUS", handleLegacyGetStatus);
    web_server->registerEndpoint("GET", "/REQUESTWEATHER", handleLegacyRequestWeather);
    web_server->registerEndpoint("GET", "/TIMEUPDATE", handleLegacyTimeUpdate);
    web_server->registerEndpoint("POST", "/SETWIFI", handleLegacySetWiFi);
    web_server->registerEndpoint("GET", "/SETTINGS", handleLegacySettings);
    web_server->registerEndpoint("POST", "/", handleLegacyPost);
}

// ============================================================================
// MODERN API HANDLERS
// ============================================================================

/**
 * @brief Handle system status request
 */
void handleSystemStatus(const WebRequest& request, WebResponse& response) {
    auto system_manager = SystemManager::getInstance();
    auto sensor_manager = SensorManager::getInstance();
    auto wifi_manager = WiFiManager::getInstance();
    
    // Build JSON response
    JsonDocument doc;
    doc["status"] = "online";
    doc["uptime"] = system_manager->getUptime();
    doc["free_memory"] = system_manager->getFreeMemory();
    doc["sensor_count"] = sensor_manager->getSensorCount();
    doc["wifi_connected"] = wifi_manager->isConnected();
    doc["wifi_strength"] = wifi_manager->getSignalStrength();
    doc["last_error"] = system_manager->getLastError();
    doc["version"] = system_manager->getVersion();
    
    response.setJson(doc);
    response.setStatus(200);
}

/**
 * @brief Handle system reboot request
 */
void handleSystemReboot(const WebRequest& request, WebResponse& response) {
    auto system_manager = SystemManager::getInstance();
    
    // Schedule reboot
    system_manager->scheduleReboot(10); // 10 seconds delay
    
    JsonDocument doc;
    doc["message"] = "System will reboot in 10 seconds";
    doc["reboot_time"] = system_manager->getUptime() + 10;
    
    response.setJson(doc);
    response.setStatus(200);
}

/**
 * @brief Handle system information request
 */
void handleSystemInfo(const WebRequest& request, WebResponse& response) {
    auto system_manager = SystemManager::getInstance();
    auto storage_manager = StorageManager::getInstance();
    
    JsonDocument doc;
    doc["device_name"] = system_manager->getDeviceName();
    doc["mac_address"] = system_manager->getMACAddress();
    doc["ip_address"] = system_manager->getIPAddress();
    doc["firmware_version"] = system_manager->getVersion();
    doc["build_date"] = system_manager->getBuildDate();
    doc["storage_used"] = storage_manager->getStorageUsed();
    doc["storage_total"] = storage_manager->getStorageTotal();
    doc["system_metrics"] = system_manager->getMetrics();
    
    response.setJson(doc);
    response.setStatus(200);
}

/**
 * @brief Handle get all sensors request
 */
void handleGetSensors(const WebRequest& request, WebResponse& response) {
    auto sensor_manager = SensorManager::getInstance();
    
    JsonDocument doc;
    JsonArray sensors_array = doc.createNestedArray("sensors");
    
    auto sensors = sensor_manager->getAllSensors();
    for (const auto& sensor : sensors) {
        JsonObject sensor_obj = sensors_array.createNestedObject();
        sensor_obj["id"] = sensor.id;
        sensor_obj["name"] = sensor.name;
        sensor_obj["type"] = static_cast<int>(sensor.type);
        sensor_obj["value"] = sensor.value;
        sensor_obj["unit"] = sensor.unit;
        sensor_obj["last_read"] = sensor.last_read_time;
        sensor_obj["status"] = static_cast<int>(sensor.status);
    }
    
    doc["count"] = sensors.size();
    doc["total_count"] = sensor_manager->getSensorCount();
    
    response.setJson(doc);
    response.setStatus(200);
}

/**
 * @brief Handle get specific sensor request
 */
void handleGetSensor(const WebRequest& request, WebResponse& response) {
    auto sensor_manager = SensorManager::getInstance();
    
    // Extract sensor ID from URL parameter
    std::string sensor_id = request.getPathParameter("id");
    if (sensor_id.empty()) {
        response.setStatus(400);
        response.setBody("Missing sensor ID");
        return;
    }
    
    auto sensor = sensor_manager->getSensor(sensor_id);
    if (!sensor) {
        response.setStatus(404);
        response.setBody("Sensor not found");
        return;
    }
    
    JsonDocument doc;
    doc["id"] = sensor->id;
    doc["name"] = sensor->name;
    doc["type"] = static_cast<int>(sensor->type);
    doc["value"] = sensor->value;
    doc["unit"] = sensor->unit;
    doc["last_read"] = sensor->last_read_time;
    doc["status"] = static_cast<int>(sensor->status);
    doc["config"] = sensor->config;
    
    response.setJson(doc);
    response.setStatus(200);
}

/**
 * @brief Handle add sensor request
 */
void handleAddSensor(const WebRequest& request, WebResponse& response) {
    auto sensor_manager = SensorManager::getInstance();
    
    // Parse JSON request body
    JsonDocument doc;
    if (!request.parseJson(doc)) {
        response.setStatus(400);
        response.setBody("Invalid JSON");
        return;
    }
    
    // Extract sensor data
    SensorConfig config;
    config.name = doc["name"] | "";
    config.type = static_cast<SensorType>(doc["type"] | 0);
    config.unit = doc["unit"] | "";
    config.read_interval = doc["read_interval"] | 3600;
    config.enabled = doc["enabled"] | true;
    
    // Add sensor
    std::string sensor_id = sensor_manager->addSensor(config);
    if (sensor_id.empty()) {
        response.setStatus(500);
        response.setBody("Failed to add sensor");
        return;
    }
    
    JsonDocument response_doc;
    response_doc["sensor_id"] = sensor_id;
    response_doc["message"] = "Sensor added successfully";
    
    response.setJson(response_doc);
    response.setStatus(201);
}

/**
 * @brief Handle get current weather request
 */
void handleGetCurrentWeather(const WebRequest& request, WebResponse& response) {
    auto sensor_manager = SensorManager::getInstance();
    
    JsonDocument doc;
    
    // Get temperature sensors
    auto temp_sensors = sensor_manager->getSensorsByType(SensorType::TEMPERATURE);
    if (!temp_sensors.empty()) {
        doc["temperature"] = temp_sensors[0]->value;
        doc["temperature_unit"] = temp_sensors[0]->unit;
    }
    
    // Get humidity sensors
    auto humidity_sensors = sensor_manager->getSensorsByType(SensorType::HUMIDITY);
    if (!humidity_sensors.empty()) {
        doc["humidity"] = humidity_sensors[0]->value;
        doc["humidity_unit"] = humidity_sensors[0]->unit;
    }
    
    // Get pressure sensors
    auto pressure_sensors = sensor_manager->getSensorsByType(SensorType::PRESSURE);
    if (!pressure_sensors.empty()) {
        doc["pressure"] = pressure_sensors[0]->value;
        doc["pressure_unit"] = pressure_sensors[0]->unit;
    }
    
    doc["timestamp"] = time(nullptr);
    doc["source"] = "local_sensors";
    
    response.setJson(doc);
    response.setStatus(200);
}

/**
 * @brief Handle WiFi configuration request
 */
void handleSetWiFi(const WebRequest& request, WebResponse& response) {
    auto wifi_manager = WiFiManager::getInstance();
    
    // Parse JSON request body
    JsonDocument doc;
    if (!request.parseJson(doc)) {
        response.setStatus(400);
        response.setBody("Invalid JSON");
        return;
    }
    
    std::string ssid = doc["ssid"] | "";
    std::string password = doc["password"] | "";
    
    if (ssid.empty()) {
        response.setStatus(400);
        response.setBody("SSID is required");
        return;
    }
    
    // Set WiFi credentials
    WiFiConfig config;
    config.ssid = ssid;
    config.password = password;
    config.auto_connect = true;
    
    ErrorCode result = wifi_manager->setCredentials(config);
    if (result != ErrorCode::SUCCESS) {
        response.setStatus(500);
        response.setBody("Failed to set WiFi credentials");
        return;
    }
    
    JsonDocument response_doc;
    response_doc["message"] = "WiFi credentials updated";
    response_doc["ssid"] = ssid;
    
    response.setJson(response_doc);
    response.setStatus(200);
}

// ============================================================================
// LEGACY COMPATIBILITY HANDLERS
// ============================================================================

/**
 * @brief Legacy root handler - redirects to modern API
 */
void handleLegacyRoot(const WebRequest& request, WebResponse& response) {
    // Redirect to modern API or serve legacy HTML
    response.setStatus(302);
    response.setHeader("Location", "/api/system/status");
}

/**
 * @brief Legacy get status handler
 */
void handleLegacyGetStatus(const WebRequest& request, WebResponse& response) {
    auto system_manager = SystemManager::getInstance();
    auto sensor_manager = SensorManager::getInstance();
    
    // Format legacy response
    std::string response_text = "STATUS:" + std::to_string(time(nullptr)) + ";";
    response_text += "ALIVESINCE:" + std::to_string(system_manager->getUptime()) + ";";
    response_text += "NUMDEVS:" + std::to_string(sensor_manager->getSensorCount()) + ";";
    
    response.setBody(response_text);
    response.setStatus(200);
}

/**
 * @brief Legacy weather request handler
 */
void handleLegacyRequestWeather(const WebRequest& request, WebResponse& response) {
    auto sensor_manager = SensorManager::getInstance();
    
    std::string response_text = "";
    
    if (request.getParameters().empty()) {
        // Return current weather data
        auto temp_sensors = sensor_manager->getSensorsByType(SensorType::TEMPERATURE);
        auto humidity_sensors = sensor_manager->getSensorsByType(SensorType::HUMIDITY);
        
        if (!temp_sensors.empty()) {
            response_text += std::to_string(temp_sensors[0]->value) + ";";
        } else {
            response_text += "0;";
        }
        
        if (!humidity_sensors.empty()) {
            response_text += std::to_string(humidity_sensors[0]->value) + ";";
        } else {
            response_text += "0;";
        }
        
        // Add placeholder values for legacy compatibility
        response_text += "0;"; // daily max
        response_text += "0;"; // daily min
        response_text += "0;"; // daily weather ID
        response_text += "0;"; // POP
        response_text += "0;"; // snow
        response_text += "0;"; // sunrise
        response_text += "0;"; // sunset
    } else {
        // Handle specific parameter requests
        for (const auto& param : request.getParameters()) {
            if (param.first == "hourly_temp") {
                auto temp_sensors = sensor_manager->getSensorsByType(SensorType::TEMPERATURE);
                if (!temp_sensors.empty()) {
                    response_text += std::to_string(temp_sensors[0]->value) + ";";
                } else {
                    response_text += "0;";
                }
            }
            // Add more parameter handling as needed
        }
    }
    
    response.setBody(response_text);
    response.setStatus(200);
}

/**
 * @brief Legacy WiFi setup handler
 */
void handleLegacySetWiFi(const WebRequest& request, WebResponse& response) {
    auto wifi_manager = WiFiManager::getInstance();
    
    std::string ssid = request.getParameter("SSID");
    std::string password = request.getParameter("PWD");
    
    if (ssid.empty()) {
        response.setBody("Missing SSID parameter");
        response.setStatus(400);
        return;
    }
    
    WiFiConfig config;
    config.ssid = ssid;
    config.password = password;
    config.auto_connect = true;
    
    ErrorCode result = wifi_manager->setCredentials(config);
    if (result == ErrorCode::SUCCESS) {
        response.setBody("OK, stored new credentials.\nSSID:" + ssid + "\nPWD: NOT_SHOWN");
        response.setStatus(201);
    } else {
        response.setBody("Failed to store credentials");
        response.setStatus(500);
    }
}

/**
 * @brief Legacy settings handler
 */
void handleLegacySettings(const WebRequest& request, WebResponse& response) {
    auto system_manager = SystemManager::getInstance();
    auto sensor_manager = SensorManager::getInstance();
    
    // Build legacy HTML response
    std::string html = "<html><body>";
    html += "<h2>System Settings</h2>";
    html += "<p>Device Name: " + system_manager->getDeviceName() + "</p>";
    html += "<p>Uptime: " + std::to_string(system_manager->getUptime()) + " seconds</p>";
    html += "<p>Sensor Count: " + std::to_string(sensor_manager->getSensorCount()) + "</p>";
    html += "<p>Free Memory: " + std::to_string(system_manager->getFreeMemory()) + " bytes</p>";
    html += "</body></html>";
    
    response.setBody(html);
    response.setStatus(200);
    response.setHeader("Content-Type", "text/html");
}

/**
 * @brief Legacy POST handler for sensor data
 */
void handleLegacyPost(const WebRequest& request, WebResponse& response) {
    auto sensor_manager = SensorManager::getInstance();
    
    // Parse JSON or form data
    JsonDocument doc;
    bool json_parsed = request.parseJson(doc);
    
    if (json_parsed) {
        // Handle JSON data
        if (doc.containsKey("sensors") && doc["sensors"].is<JsonArray>()) {
            JsonArray sensors_array = doc["sensors"];
            
            for (JsonObject sensor : sensors_array) {
                if (sensor.containsKey("type") && sensor.containsKey("name") && sensor.containsKey("value")) {
                    SensorConfig config;
                    config.name = sensor["name"].as<std::string>();
                    config.type = static_cast<SensorType>(sensor["type"].as<int>());
                    config.value = sensor["value"].as<double>();
                    
                    sensor_manager->addSensor(config);
                }
            }
            
            response.setBody("Data received successfully");
            response.setStatus(200);
            return;
        }
    } else {
        // Handle form data
        std::string sensor_name = request.getParameter("varName");
        std::string sensor_value = request.getParameter("varValue");
        
        if (!sensor_name.empty() && !sensor_value.empty()) {
            SensorConfig config;
            config.name = sensor_name;
            config.value = std::stod(sensor_value);
            config.type = SensorType::CUSTOM;
            
            sensor_manager->addSensor(config);
            
            response.setBody("Form data received successfully");
            response.setStatus(200);
            return;
        }
    }
    
    response.setBody("No valid data received");
    response.setStatus(400);
}

// ============================================================================
// LEGACY COMPATIBILITY FUNCTIONS (GRADUAL MIGRATION)
// ============================================================================

/**
 * @brief Legacy WiFi status check - delegates to WiFiManager
 */
bool WifiStatus(void) {
    auto wifi_manager = WiFiManager::getInstance();
    return wifi_manager->isConnected();
}

/**
 * @brief Legacy WiFi connection - delegates to WiFiManager
 */
uint8_t connectWiFi() {
    auto wifi_manager = WiFiManager::getInstance();
    
    if (wifi_manager->isConnected()) {
        return 0; // Success
    }
    
    // Try to connect
    ErrorCode result = wifi_manager->connect();
    if (result == ErrorCode::SUCCESS) {
        return 0; // Success
    }
    
    return 255; // Failure
}

/**
 * @brief Legacy server message - delegates to WebServerManager
 */
bool Server_Message(String& URL, String& payload, int& httpCode) {
    auto web_server = WebServerManager::getInstance();
    
    // Convert legacy URL to modern request
    WebRequest request;
    request.setMethod("GET");
    request.setUrl(URL.c_str());
    
    WebResponse response;
    ErrorCode result = web_server->sendRequest(request, response);
    
    if (result == ErrorCode::SUCCESS) {
        httpCode = response.getStatus();
        payload = response.getBody().c_str();
        return true;
    }
    
    return false;
}

/**
 * @brief Legacy secure server message - delegates to WebServerManager with security
 */
bool Server_SecureMessage(String& URL, String& payload, int& httpCode, String& cacert) {
    auto web_server = WebServerManager::getInstance();
    auto security_manager = SecurityManager::getInstance();
    
    // Convert legacy URL to modern secure request
    WebRequest request;
    request.setMethod("GET");
    request.setUrl(URL.c_str());
    request.setSecure(true);
    
    WebResponse response;
    ErrorCode result = web_server->sendSecureRequest(request, response, cacert.c_str());
    
    if (result == ErrorCode::SUCCESS) {
        httpCode = response.getStatus();
        payload = response.getBody().c_str();
        return true;
    }
    
    return false;
}

/**
 * @brief Legacy AP SSID generation - delegates to WiFiManager
 */
String generateAPSSID() {
    auto wifi_manager = WiFiManager::getInstance();
    return String(wifi_manager->generateAPSSID().c_str());
}

// ============================================================================
// MAINTAINED LEGACY HANDLERS (FOR GRADUAL MIGRATION)
// ============================================================================

void handleReboot() {
    auto system_manager = SystemManager::getInstance();
    system_manager->scheduleReboot(10);
    
    WEBHTML = "Rebooting in 10 sec";
    serverTextClose(200, false);
    delay(10000);
    ESP.restart();
}

void handleCLEARSENSOR() {
    auto sensor_manager = SensorManager::getInstance();
    
    int sensor_num = -1;
    for (uint8_t i = 0; i < server.args(); i++) {
        if (server.argName(i) == "SensorNum") {
            sensor_num = server.arg(i).toInt();
        }
    }
    
    if (sensor_num >= 0) {
        sensor_manager->clearSensor(sensor_num);
    }
    
    server.sendHeader("Location", "/");
    WEBHTML = "Updated-- Press Back Button";
    serverTextClose(302, false);
}

void handleREQUESTUPDATE() {
    auto sensor_manager = SensorManager::getInstance();
    
    byte sensor_num = 0;
    for (uint8_t i = 0; i < server.args(); i++) {
        if (server.argName(i) == "SensorNum") {
            sensor_num = server.arg(i).toInt();
        }
    }
    
    // Trigger sensor update
    sensor_manager->updateSensor(sensor_num);
    
    server.sendHeader("Location", "/");
    WEBHTML = "Updated-- Press Back Button";
    serverTextClose(302, false);
}

// ============================================================================
// LEGACY HELPER FUNCTIONS (MAINTAINED FOR COMPATIBILITY)
// ============================================================================

void serverTextHeader() {
    WEBHTML = "<html><head><title>Weather Station</title></head>";
}

void serverTextWiFiForm() {
    WEBHTML += "<FORM action=\"/SETWIFI\" method=\"post\">";
    WEBHTML += "SSID: <input type=\"text\" name=\"SSID\"><br>";
    WEBHTML += "Password: <input type=\"password\" name=\"PWD\"><br>";
    WEBHTML += "<button type=\"submit\">Set WiFi</button>";
    WEBHTML += "</FORM>";
}

void serverTextClose(int htmlcode, bool asHTML) {
    if (asHTML) {
        server.send(htmlcode, "text/html", WEBHTML);
    } else {
        server.send(htmlcode, "text/plain", WEBHTML);
    }
    WEBHTML = "";
}

// ============================================================================
// INITIALIZATION AND SETUP
// ============================================================================

/**
 * @brief Initialize the server system (called from main.cpp)
 */
void setupServer() {
    ErrorCode result = initializeRefactoredServer();
    if (result != ErrorCode::SUCCESS) {
        ErrorHandler::getInstance().logError(result, "Failed to initialize refactored server");
        // Fall back to legacy initialization if needed
    }
}

/**
 * @brief Update the server system (called from main.cpp loop)
 */
void updateServer() {
    auto system_manager = SystemManager::getInstance();
    auto web_server = WebServerManager::getInstance();
    
    // Update all managers
    system_manager->update();
    web_server->update();
    
    // Handle legacy server if still needed
    server.handleClient();
}

/**
 * @brief Shutdown the server system
 */
void shutdownServer() {
    auto system_manager = SystemManager::getInstance();
    auto web_server = WebServerManager::getInstance();
    
    web_server->shutdown();
    system_manager->shutdown();
} 