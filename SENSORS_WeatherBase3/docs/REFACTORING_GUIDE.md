# SENSORS_WeatherBase3 Refactoring Guide

## Overview

This guide provides comprehensive documentation for the refactored SENSORS_WeatherBase3 codebase. The refactoring focuses on **modularization**, **documentation**, **duplication removal**, and **error handling** improvements.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [New Module Structure](#new-module-structure)
3. [Error Handling Framework](#error-handling-framework)
4. [String Utilities](#string-utilities)
5. [Coding Standards](#coding-standards)
6. [Migration Guide](#migration-guide)
7. [Best Practices](#best-practices)
8. [Testing Guidelines](#testing-guidelines)

## Architecture Overview

### Design Principles

The refactored codebase follows these core principles:

1. **Single Responsibility**: Each class/module has one clear purpose
2. **Dependency Injection**: Loose coupling between components
3. **Error-First Design**: Comprehensive error handling at all levels
4. **Memory Efficiency**: Optimized for ESP32 constraints
5. **Testability**: Modular design enables unit testing

### System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
├─────────────────────────────────────────────────────────────┤
│  main.cpp (Entry Point)                                     │
│  SystemManager (Orchestration)                              │
├─────────────────────────────────────────────────────────────┤
│                    Service Layer                            │
├─────────────────────────────────────────────────────────────┤
│  WiFiManager    │  SensorManager  │  DisplayManager        │
│  ESPNowManager  │  StorageManager │  SecurityManager       │
├─────────────────────────────────────────────────────────────┤
│                    Utility Layer                            │
├─────────────────────────────────────────────────────────────┤
│  ErrorHandler   │  StringUtils    │  TimeUtils             │
│  NetworkUtils   │  FileUtils      │  MathUtils             │
├─────────────────────────────────────────────────────────────┤
│                    Hardware Layer                           │
├─────────────────────────────────────────────────────────────┤
│  ESP32 APIs     │  Sensor Drivers │  Display Drivers       │
└─────────────────────────────────────────────────────────────┘
```

## New Module Structure

### Core Modules

#### SystemManager
- **Purpose**: Main system coordinator
- **Responsibilities**: 
  - Initialize all subsystems
  - Manage system state
  - Coordinate between modules
  - Handle system lifecycle

#### ErrorHandler
- **Purpose**: Centralized error management
- **Features**:
  - Standardized error codes
  - Automatic retry mechanisms
  - Error logging and statistics
  - Recovery strategies

#### StringUtils
- **Purpose**: String manipulation utilities
- **Features**:
  - String parsing and tokenization
  - Formatting and conversion
  - Validation and sanitization
  - Memory-efficient operations

### Network Modules

#### WiFiManager
- **Purpose**: WiFi connection management
- **Features**:
  - Connection establishment
  - Credential management
  - Automatic reconnection
  - AP mode fallback

#### ESPNowManager
- **Purpose**: ESPNow communication
- **Features**:
  - Message sending/receiving
  - Peer management
  - Encryption/decryption
  - Error recovery

#### WebServer
- **Purpose**: HTTP server functionality
- **Features**:
  - REST API endpoints
  - Web interface
  - Configuration management
  - Data serving

### Sensor Modules

#### SensorManager
- **Purpose**: Sensor data management
- **Features**:
  - Sensor initialization
  - Data collection
  - Value validation
  - Error handling

#### SensorTypes
- **Purpose**: Sensor type definitions
- **Features**:
  - Type-specific configurations
  - Calibration data
  - Conversion formulas
  - Validation rules

### Storage Modules

#### SDCardManager
- **Purpose**: SD card operations
- **Features**:
  - File system management
  - Data logging
  - Configuration storage
  - Error recovery

#### PreferencesManager
- **Purpose**: NVS preferences
- **Features**:
  - Encrypted storage
  - Configuration persistence
  - Default values
  - Migration support

### Display Modules

#### DisplayManager
- **Purpose**: TFT display management
- **Features**:
  - Display initialization
  - Screen management
  - UI updates
  - Power management

#### GraphicsEngine
- **Purpose**: Graphics rendering
- **Features**:
  - Drawing primitives
  - Text rendering
  - Image display
  - Animation support

## Error Handling Framework

### Error Codes

The system uses standardized error codes for consistent error identification:

```cpp
enum class ErrorCode : uint8_t {
    SUCCESS = 0,
    WIFI_CONNECTION_FAILED,
    SENSOR_READ_FAILED,
    SD_CARD_ERROR,
    MEMORY_ERROR,
    NETWORK_ERROR,
    SECURITY_ERROR,
    // ... more codes
};
```

### Error Severity Levels

```cpp
enum class ErrorSeverity : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4,
    FATAL = 5
};
```

### Usage Examples

#### Basic Error Handling
```cpp
// Handle an error with automatic recovery
if (!WiFiManager::connect()) {
    ERROR_HANDLE(ErrorCode::WIFI_CONNECTION_FAILED, "Failed to connect to WiFi");
    return false;
}
```

#### Error Logging
```cpp
// Log an error without automatic handling
ERROR_LOG(ErrorCode::SENSOR_READ_FAILED, "Temperature sensor read failed", ErrorSeverity::WARNING);
```

#### Error Checking
```cpp
// Check condition and return on error
ERROR_CHECK(sensorValue > 0, ErrorCode::SENSOR_READ_FAILED, "Invalid sensor value");

// Check condition and return specific value on error
ERROR_RETURN(sensorValue > 0, ErrorCode::SENSOR_READ_FAILED, "Invalid sensor value", -1);
```

### Error Recovery

The ErrorHandler provides automatic recovery for common errors:

- **WiFi Connection**: Exponential backoff retry
- **Sensor Reading**: Retry with delay
- **SD Card**: Remount and retry
- **Network**: Retry with different servers

### Error Statistics

The system tracks error statistics for monitoring and debugging:

```cpp
const ErrorStats& stats = ErrorHandler::getErrorStats();
Serial.printf("Total errors: %d\n", stats.totalErrors);
Serial.printf("Recoveries: %d\n", stats.recoveryCount);
```

## String Utilities

### Common Operations

#### String Splitting
```cpp
// Split by character
std::vector<String> parts = StringUtils::split("a,b,c", ',');
// Result: ["a", "b", "c"]

// Split by string
std::vector<String> parts = StringUtils::split("a--b--c", "--");
// Result: ["a", "b", "c"]
```

#### String Formatting
```cpp
// Format with printf-style
String formatted = StringUtils::format("Temperature: %.2f°C", 23.456);
// Result: "Temperature: 23.46°C"

// Convert numbers
String temp = StringUtils::toString(23.456, 2);
// Result: "23.46"
```

#### String Validation
```cpp
// Check if numeric
if (StringUtils::isNumeric("123.45")) {
    float value = StringUtils::toFloat("123.45");
}

// Check if hexadecimal
if (StringUtils::isHexadecimal("1A2B3C")) {
    // Process hex value
}
```

#### Network Utilities
```cpp
// MAC address conversion
uint8_t mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
String macStr = StringUtils::macToString(mac);
// Result: "12:34:56:78:9A:BC"

// IP address conversion
uint32_t ip = 0xC0A80101; // 192.168.1.1
String ipStr = StringUtils::ipToString(ip);
// Result: "192.168.1.1"
```

### Convenience Macros

The StringUtils provides convenience macros for common operations:

```cpp
// Common operations with macros
auto parts = STR_SPLIT("a,b,c", ',');
String trimmed = STR_TRIM("  hello  ");
String lower = STR_TO_LOWER("Hello World");
bool starts = STR_STARTS_WITH("Hello", "He");
int value = STR_TO_INT("123");
```

## Coding Standards

### Naming Conventions

#### Classes
- **PascalCase**: `WiFiManager`, `ErrorHandler`, `StringUtils`
- **Descriptive names**: Clear purpose indication

#### Methods
- **camelCase**: `connectWiFi()`, `handleError()`, `split()`
- **Verb-noun format**: Action followed by object

#### Variables
- **camelCase**: `sensorValue`, `connectionStatus`, `errorCount`
- **Constants**: `UPPER_SNAKE_CASE`: `MAX_RETRIES`, `DEFAULT_TIMEOUT`

#### Files
- **PascalCase**: `WiFiManager.hpp`, `ErrorHandler.cpp`
- **Descriptive**: Clear module indication

### Documentation Standards

#### File Headers
```cpp
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
 */
```

#### Class Documentation
```cpp
/**
 * @brief WiFi connection manager
 * 
 * This class provides centralized WiFi functionality with:
 * - Automatic connection management
 * - Credential encryption and storage
 * - Connection monitoring and recovery
 * - AP mode fallback capabilities
 * 
 * @example
 * @code
 * WiFiManager wifi;
 * if (wifi.initialize()) {
 *     wifi.connect("MyNetwork", "password");
 * }
 * @endcode
 */
class WiFiManager {
    // Implementation
};
```

#### Method Documentation
```cpp
/**
 * @brief Connect to WiFi network
 * @param ssid Network SSID
 * @param password Network password
 * @param timeout Connection timeout in milliseconds (default: 30000)
 * @return true if connection successful, false otherwise
 * 
 * This method attempts to connect to the specified WiFi network.
 * It includes automatic retry logic with exponential backoff.
 * 
 * @note This method is thread-safe and can be called from any context.
 * @warning Password is stored in encrypted format in NVS.
 * 
 * @see disconnect()
 * @see isConnected()
 */
bool connect(const String& ssid, const String& password, uint32_t timeout = 30000);
```

### Code Organization

#### Header Files
```cpp
#ifndef MODULE_NAME_HPP
#define MODULE_NAME_HPP

// System includes
#include <Arduino.h>
#include <WiFi.h>

// Project includes
#include "ErrorHandler.hpp"
#include "StringUtils.hpp"

// Forward declarations
class SomeClass;

// Constants
#define DEFAULT_TIMEOUT 30000
#define MAX_RETRIES 5

// Enums
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED
};

// Class definition
class ModuleName {
public:
    // Public interface
    ModuleName();
    ~ModuleName();
    
    // Public methods
    bool initialize();
    bool connect();
    void disconnect();
    
private:
    // Private members
    ConnectionState state;
    uint32_t retryCount;
    
    // Private methods
    bool attemptConnection();
    void updateState(ConnectionState newState);
};

#endif // MODULE_NAME_HPP
```

#### Implementation Files
```cpp
/**
 * @file ModuleName.cpp
 * @brief Implementation of ModuleName class
 */

#include "ModuleName.hpp"
#include "ErrorHandler.hpp"

// Static member initialization
ConnectionState ModuleName::state = ConnectionState::DISCONNECTED;

// Constructor
ModuleName::ModuleName() : retryCount(0) {
    // Initialization code
}

// Destructor
ModuleName::~ModuleName() {
    // Cleanup code
}

// Public methods
bool ModuleName::initialize() {
    // Implementation
}

bool ModuleName::connect() {
    // Implementation with error handling
    if (!attemptConnection()) {
        ERROR_HANDLE(ErrorCode::WIFI_CONNECTION_FAILED, "Connection failed");
        return false;
    }
    return true;
}

// Private methods
bool ModuleName::attemptConnection() {
    // Implementation
}
```

### Error Handling Patterns

#### Function-Level Error Handling
```cpp
bool someFunction() {
    // Validate inputs
    ERROR_CHECK(input != nullptr, ErrorCode::INVALID_PARAMETER, "Input is null");
    
    // Perform operation
    if (!performOperation()) {
        ERROR_HANDLE(ErrorCode::OPERATION_FAILED, "Operation failed");
        return false;
    }
    
    return true;
}
```

#### Class-Level Error Handling
```cpp
class SomeClass {
public:
    bool initialize() {
        ERROR_CHECK(validateConfiguration(), ErrorCode::CONFIG_ERROR, "Invalid configuration");
        
        if (!setupHardware()) {
            ERROR_HANDLE(ErrorCode::HARDWARE_ERROR, "Hardware setup failed");
            return false;
        }
        
        return true;
    }
    
private:
    bool validateConfiguration() {
        // Validation logic
        return true;
    }
    
    bool setupHardware() {
        // Hardware setup
        return true;
    }
};
```

## Migration Guide

### Phase 1: Foundation (Week 1-2)

#### Step 1: Add Error Handling
1. Include ErrorHandler in existing files
2. Replace manual error handling with ErrorHandler calls
3. Add error codes for existing error conditions

```cpp
// Before
if (!SD.begin(41)) {
    Serial.println("SD mount failed");
    return false;
}

// After
if (!SD.begin(41)) {
    ERROR_HANDLE(ErrorCode::SD_CARD_MOUNT_FAILED, "SD card mount failed");
    return false;
}
```

#### Step 2: Add String Utilities
1. Replace manual string operations with StringUtils
2. Update string parsing code
3. Standardize string formatting

```cpp
// Before
String parts[3];
int index = 0;
int lastIndex = 0;
while ((index = input.indexOf(',', lastIndex)) != -1) {
    parts[count] = input.substring(lastIndex, index);
    lastIndex = index + 1;
    count++;
}

// After
auto parts = StringUtils::split(input, ',');
```

### Phase 2: Module Extraction (Week 3-4)

#### Step 1: Identify Modules
1. Analyze existing code for logical groupings
2. Identify responsibilities and dependencies
3. Plan module boundaries

#### Step 2: Extract Classes
1. Create new module files
2. Move related functionality
3. Update includes and dependencies

#### Step 3: Update Main
1. Simplify main.cpp
2. Use SystemManager for coordination
3. Remove direct hardware access

### Phase 3: Integration (Week 5-6)

#### Step 1: Update Dependencies
1. Fix include paths
2. Resolve compilation errors
3. Update build configuration

#### Step 2: Test Integration
1. Verify all modules work together
2. Test error handling
3. Validate functionality

## Best Practices

### Memory Management

#### Stack vs Heap
- **Prefer stack allocation** for small, short-lived objects
- **Use heap sparingly** and always check for allocation success
- **Avoid dynamic allocation** in critical paths

```cpp
// Good: Stack allocation
char buffer[64];
snprintf(buffer, sizeof(buffer), "Value: %d", value);

// Avoid: Heap allocation in critical paths
char* buffer = new char[64]; // Don't do this
```

#### String Management
- **Use String objects** for convenience
- **Prefer const char*** for performance-critical code
- **Avoid string concatenation** in loops

```cpp
// Good: Single string operation
String result = StringUtils::format("Temperature: %.2f°C", temp);

// Avoid: Multiple concatenations
String result = "Temperature: " + String(temp) + "°C"; // Less efficient
```

### Performance Optimization

#### Function Inlining
- **Inline small, frequently called functions**
- **Use const references** for large parameters
- **Avoid virtual functions** in performance-critical code

```cpp
// Good: Inline function
inline bool isValidValue(float value) {
    return value >= 0.0f && value <= 100.0f;
}

// Good: Const reference
void processData(const String& data) {
    // Process data
}
```

#### Loop Optimization
- **Cache loop bounds** for large loops
- **Use iterators** instead of indices where appropriate
- **Avoid function calls** in tight loops

```cpp
// Good: Cached bounds
size_t count = items.size();
for (size_t i = 0; i < count; ++i) {
    processItem(items[i]);
}

// Avoid: Function call in loop
for (size_t i = 0; i < items.size(); ++i) { // size() called each iteration
    processItem(items[i]);
}
```

### Error Handling Best Practices

#### Fail Fast
- **Validate inputs early**
- **Return errors immediately**
- **Don't continue with invalid state**

```cpp
bool processData(const String& data) {
    // Validate immediately
    ERROR_CHECK(!data.isEmpty(), ErrorCode::INVALID_PARAMETER, "Data is empty");
    ERROR_CHECK(data.length() < MAX_LENGTH, ErrorCode::INVALID_PARAMETER, "Data too long");
    
    // Process valid data
    return processValidData(data);
}
```

#### Error Propagation
- **Propagate errors up the call stack**
- **Add context to error messages**
- **Don't swallow errors**

```cpp
bool highLevelFunction() {
    if (!lowLevelFunction()) {
        ERROR_HANDLE(ErrorCode::LOW_LEVEL_FAILED, "Low level function failed in high level context");
        return false;
    }
    return true;
}
```

#### Recovery Strategies
- **Implement appropriate recovery for each error type**
- **Use exponential backoff for retries**
- **Provide fallback mechanisms**

```cpp
bool connectWithRetry() {
    uint32_t backoff = 1000; // Start with 1 second
    
    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        if (attemptConnection()) {
            return true;
        }
        
        ERROR_LOG(ErrorCode::CONNECTION_FAILED, 
                  "Connection attempt " + String(attempt + 1) + " failed",
                  ErrorSeverity::WARNING);
        
        delay(backoff);
        backoff *= 2; // Exponential backoff
    }
    
    ERROR_HANDLE(ErrorCode::CONNECTION_FAILED, "All connection attempts failed");
    return false;
}
```

## Testing Guidelines

### Unit Testing

#### Test Structure
```cpp
#include <unity.h>
#include "StringUtils.hpp"

void setUp(void) {
    // Setup code
}

void tearDown(void) {
    // Cleanup code
}

void test_string_split(void) {
    // Arrange
    String input = "a,b,c";
    
    // Act
    auto result = StringUtils::split(input, ',');
    
    // Assert
    TEST_ASSERT_EQUAL(3, result.size());
    TEST_ASSERT_EQUAL_STRING("a", result[0].c_str());
    TEST_ASSERT_EQUAL_STRING("b", result[1].c_str());
    TEST_ASSERT_EQUAL_STRING("c", result[2].c_str());
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_string_split);
    UNITY_END();
}
```

#### Test Categories
- **Unit tests**: Test individual functions
- **Integration tests**: Test module interactions
- **System tests**: Test full system functionality
- **Performance tests**: Test timing and memory usage

### Test Coverage

#### Required Coverage
- **Function coverage**: 100% of functions tested
- **Line coverage**: >80% of code lines tested
- **Branch coverage**: >70% of branches tested
- **Error path coverage**: All error conditions tested

#### Test Organization
```
test/
├── unit/           # Unit tests
│   ├── StringUtils_test.cpp
│   ├── ErrorHandler_test.cpp
│   └── WiFiManager_test.cpp
├── integration/    # Integration tests
│   ├── SystemManager_test.cpp
│   └── Network_test.cpp
└── system/         # System tests
    ├── FullSystem_test.cpp
    └── Performance_test.cpp
```

### Continuous Integration

#### Automated Testing
- **Run tests on every commit**
- **Generate coverage reports**
- **Fail build on test failure**
- **Performance regression detection**

#### Test Environment
- **Hardware-in-the-loop testing**
- **Simulated sensor data**
- **Network simulation**
- **Error injection testing**

---

## Conclusion

This refactoring guide provides a comprehensive framework for modernizing the SENSORS_WeatherBase3 codebase. By following these guidelines, developers can:

1. **Improve code quality** through better organization and error handling
2. **Reduce maintenance burden** through modular design
3. **Enhance reliability** through comprehensive error handling
4. **Increase productivity** through standardized utilities and patterns

The refactoring is designed to be **incremental** and **backward-compatible**, allowing for gradual migration while maintaining system functionality.

For questions or contributions, please refer to the project documentation or contact the development team. 