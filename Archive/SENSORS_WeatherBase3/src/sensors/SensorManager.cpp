/**
 * @file SensorManager.cpp
 * @brief Sensor manager implementation for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 */

#include "SensorManager.hpp"
#include "../storage/StorageManager.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"

// Global sensor manager instance
SensorManager& sensorManager = SensorManager::getInstance();

SensorManager::SensorManager() :
    stats{},
    lastPollTime(0),
    lastUpdateTime(0),
    initialized(false),
    nextSensorId(1) {}

SensorManager::~SensorManager() {}

SensorManager& SensorManager::getInstance() {
    static SensorManager instance;
    return instance;
}

bool SensorManager::initialize() {
    if (initialized) return true;
    // Load configuration from storage
    if (!loadConfiguration()) {
        ErrorHandler::log(ErrorCode::SENSOR_CONFIG_LOAD_FAILED, ErrorSeverity::WARNING, "Failed to load sensor configuration, using defaults");
    }
    initialized = true;
    return true;
}

uint8_t SensorManager::addSensor(const SensorConfig& config) {
    if (!validateConfig(config)) return 0;
    uint8_t id = generateSensorId();
    SensorData data;
    data.sensorId = id;
    data.sensorType = config.sensorType;
    data.name = config.name;
    sensors.push_back(data);
    configs.push_back(config);
    updateSensorMappings(id, config.name, config.sensorType);
    stats.totalSensors = sensors.size();
    return id;
}

bool SensorManager::removeSensor(uint8_t sensorId) {
    for (size_t i = 0; i < sensors.size(); ++i) {
        if (sensors[i].sensorId == sensorId) {
            removeSensorMappings(sensorId);
            sensors.erase(sensors.begin() + i);
            configs.erase(configs.begin() + i);
            stats.totalSensors = sensors.size();
            return true;
        }
    }
    return false;
}

SensorData* SensorManager::getSensor(uint8_t sensorId) {
    for (auto& s : sensors) {
        if (s.sensorId == sensorId) return &s;
    }
    return nullptr;
}

SensorData* SensorManager::getSensorByIndex(uint16_t index) {
    if (index < sensors.size()) return &sensors[index];
    return nullptr;
}

SensorConfig* SensorManager::getSensorConfig(uint8_t sensorId) {
    for (size_t i = 0; i < sensors.size(); ++i) {
        if (sensors[i].sensorId == sensorId) return &configs[i];
    }
    return nullptr;
}

bool SensorManager::updateSensorConfig(uint8_t sensorId, const SensorConfig& config) {
    for (size_t i = 0; i < sensors.size(); ++i) {
        if (sensors[i].sensorId == sensorId) {
            configs[i] = config;
            updateSensorMappings(sensorId, config.name, config.sensorType);
            return true;
        }
    }
    return false;
}

bool SensorManager::setSensorEnabled(uint8_t sensorId, bool enabled) {
    SensorConfig* cfg = getSensorConfig(sensorId);
    if (cfg) {
        cfg->enabled = enabled;
        return true;
    }
    return false;
}

bool SensorManager::readSensor(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    SensorConfig* cfg = getSensorConfig(sensorId);
    if (!data || !cfg || !cfg->enabled) return false;
    float value = 0.0f;
    uint32_t start = millis();
    bool ok = readSensorHardware(sensorId, value);
    uint32_t elapsed = millis() - start;
    if (ok) {
        data->lastValue = data->value;
        data->value = value;
        data->timestamp = millis();
        data->expired = false;
        data->readCount++;
        updateStats(sensorId, true, elapsed);
    } else {
        data->errorCount++;
        handleSensorError(sensorId, "Read failed");
        updateStats(sensorId, false, elapsed);
    }
    return ok;
}

uint16_t SensorManager::readAllSensors() {
    uint16_t count = 0;
    for (auto& s : sensors) {
        if (readSensor(s.sensorId)) count++;
    }
    return count;
}

float SensorManager::getSensorValue(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    if (data && !data->expired) return data->value;
    return NAN;
}

float SensorManager::getSensorValueByName(const String& name) {
    uint8_t id = findSensorByName(name);
    return getSensorValue(id);
}

float SensorManager::getSensorValueByType(uint8_t sensorType, uint8_t index) {
    std::vector<uint8_t> ids = getSensorsByType(sensorType);
    if (index < ids.size()) return getSensorValue(ids[index]);
    return NAN;
}

bool SensorManager::isSensorValid(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    return data && !data->expired;
}

bool SensorManager::isSensorInitialized(uint8_t sensorId) {
    // For now, assume all added sensors are initialized
    return getSensor(sensorId) != nullptr;
}

bool SensorManager::isSensorExpired(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    return data ? data->expired : true;
}

uint16_t SensorManager::getActiveSensorCount() const {
    uint16_t count = 0;
    for (const auto& cfg : configs) if (cfg.enabled) count++;
    return count;
}

void SensorManager::clearStats() {
    stats = {};
}

uint8_t SensorManager::findSensorByName(const String& name) {
    auto it = nameToId.find(name);
    return it != nameToId.end() ? it->second : 0;
}

uint8_t SensorManager::findSensorByType(uint8_t sensorType, uint8_t index) {
    auto it = typeToIds.find(sensorType);
    if (it != typeToIds.end() && index < it->second.size()) return it->second[index];
    return 0;
}

std::vector<uint8_t> SensorManager::getSensorsByType(uint8_t sensorType) {
    auto it = typeToIds.find(sensorType);
    return it != typeToIds.end() ? it->second : std::vector<uint8_t>{};
}

std::vector<uint8_t> SensorManager::getSensorsByFlag(uint8_t flag) {
    std::vector<uint8_t> ids;
    for (size_t i = 0; i < configs.size(); ++i) {
        if (configs[i].flags & flag) ids.push_back(sensors[i].sensorId);
    }
    return ids;
}

bool SensorManager::setSensorFlag(uint8_t sensorId, uint8_t flag, bool value) {
    SensorConfig* cfg = getSensorConfig(sensorId);
    if (!cfg) return false;
    if (value) cfg->flags |= flag;
    else cfg->flags &= ~flag;
    return true;
}

bool SensorManager::getSensorFlag(uint8_t sensorId, uint8_t flag) {
    SensorConfig* cfg = getSensorConfig(sensorId);
    return cfg ? (cfg->flags & flag) : false;
}

bool SensorManager::validateSensorValue(uint8_t sensorId, float value) {
    SensorConfig* cfg = getSensorConfig(sensorId);
    if (!cfg) return false;
    return value >= cfg->limitLower && value <= cfg->limitUpper;
}

bool SensorManager::calibrateSensor(uint8_t sensorId, float knownValue) {
    SensorData* data = getSensor(sensorId);
    SensorConfig* cfg = getSensorConfig(sensorId);
    if (!data || !cfg) return false;
    float error = knownValue - data->value;
    cfg->offset += error;
    return true;
}

bool SensorManager::resetSensor(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    if (!data) return false;
    data->value = 0.0f;
    data->expired = false;
    data->readCount = 0;
    data->errorCount = 0;
    return true;
}

uint16_t SensorManager::resetAllSensors() {
    uint16_t count = 0;
    for (auto& s : sensors) if (resetSensor(s.sensorId)) count++;
    return count;
}

bool SensorManager::performHealthCheck() {
    // Check for expired sensors
    for (auto& s : sensors) checkSensorExpiration(s.sensorId);
    return true;
}

String SensorManager::getSensorInfo(uint8_t sensorId) {
    // For brevity, return a simple JSON string
    String info = "{";
    if (sensorId == 0) {
        info += "\"count\":" + String(sensors.size()) + "}";
    } else {
        SensorData* data = getSensor(sensorId);
        if (data) {
            info += "\"id\":" + String(data->sensorId) + ",";
            info += "\"name\":\"" + data->name + "\",";
            info += "\"value\":" + String(data->value) + ",";
            info += "\"timestamp\":" + String(data->timestamp) + "}";
        } else {
            info += "\"error\":\"not found\"}";
        }
    }
    return info;
}

String SensorManager::getSensorStatus(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    if (!data) return "Unknown";
    if (data->expired) return "Expired";
    if (data->errorCount > 0) return "Error";
    return "OK";
}

void SensorManager::loop() {
    uint32_t now = millis();
    if (now - lastPollTime >= 1000) { // Poll every second
        readAllSensors();
        lastPollTime = now;
    }
}

bool SensorManager::loadConfiguration() {
    // TODO: Load sensor configs from storage
    return true;
}

bool SensorManager::saveConfiguration() {
    // TODO: Save sensor configs to storage
    return true;
}

bool SensorManager::initializeSensorHardware(uint8_t sensorId) {
    // TODO: Initialize hardware for the given sensor
    return true;
}

bool SensorManager::readSensorHardware(uint8_t sensorId, float& value) {
    // TODO: Implement hardware-specific sensor reading
    value = random(0, 100); // Simulate sensor value
    return true;
}

bool SensorManager::validateConfig(const SensorConfig& config) {
    // TODO: Add more validation as needed
    return !config.name.isEmpty();
}

void SensorManager::updateStats(uint8_t sensorId, bool success, uint32_t readTime) {
    if (success) stats.successfulReads++;
    else stats.failedReads++;
    stats.totalReads++;
    stats.lastUpdateTime = millis();
    // Update average read time
    stats.averageReadTime = (stats.averageReadTime * (stats.totalReads - 1) + readTime) / stats.totalReads;
}

void SensorManager::checkSensorExpiration(uint8_t sensorId) {
    SensorData* data = getSensor(sensorId);
    SensorConfig* cfg = getSensorConfig(sensorId);
    if (!data || !cfg) return;
    if (millis() - data->timestamp > cfg->maxAge * 1000) data->expired = true;
}

void SensorManager::handleSensorError(uint8_t sensorId, const String& error) {
    logSensorEvent(sensorId, error, ErrorSeverity::ERROR);
}

bool SensorManager::attemptSensorRecovery(uint8_t sensorId) {
    // TODO: Implement recovery logic
    return false;
}

uint8_t SensorManager::generateSensorId() {
    return nextSensorId++;
}

void SensorManager::updateSensorMappings(uint8_t sensorId, const String& name, uint8_t type) {
    nameToId[name] = sensorId;
    typeToIds[type].push_back(sensorId);
}

void SensorManager::removeSensorMappings(uint8_t sensorId) {
    for (auto it = nameToId.begin(); it != nameToId.end(); ) {
        if (it->second == sensorId) it = nameToId.erase(it);
        else ++it;
    }
    for (auto& pair : typeToIds) {
        auto& vec = pair.second;
        vec.erase(std::remove(vec.begin(), vec.end(), sensorId), vec.end());
    }
}

void SensorManager::logSensorEvent(uint8_t sensorId, const String& event, ErrorSeverity severity) {
    ErrorHandler::log(ErrorCode::SENSOR_EVENT, severity, String("Sensor ") + String(sensorId) + ": " + event);
}

String SensorManager::getSensorTypeName(uint8_t sensorType) {
    // TODO: Return human-readable sensor type name
    return String(sensorType);
}

bool SensorManager::isValidSensorType(uint8_t sensorType) {
    // TODO: Implement sensor type validation
    return true;
} 