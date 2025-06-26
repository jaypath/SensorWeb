# SD Card Storage System Upgrade

## Overview
The SD card storage system has been upgraded to work with the new `Devices_Sensors` class while maintaining backward compatibility with the deprecated `SensorVal` struct.

## New Storage Architecture

### 1. **Complete System Storage** ✅ IMPLEMENTED
- **Function**: `storeSensorsSD()` and `readSensorsSD()`
- **Purpose**: Store and retrieve the entire `Devices_Sensors` class
- **File**: `/Data/DevicesSensors.dat`
- **Format**: Binary dump of the entire class structure

### 2. **Individual Sensor Data Storage** ✅ IMPLEMENTED
- **Function**: `storeSensorDataSD()` and `readSensorDataSD()`
- **Purpose**: Store and retrieve individual sensor readings
- **File Format**: `/Data/Sensor_{MAC}_{Type}_{ID}_v4.dat`
- **Data Structure**: Optimized record format for efficient storage and retrieval

## New Functions

### Storage Functions
```cpp
// Store by device and sensor indices
bool storeSensorDataSD(int16_t deviceIndex, int16_t sensorIndex);

// Store by device MAC and sensor info
bool storeSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID);
```

### Retrieval Functions
```cpp
// Read with time range
bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, 
                     uint32_t t[], double v[], byte *N, uint32_t *samples, 
                     uint32_t starttime, uint32_t endtime, byte avgN);

// Read from start time to end
bool readSensorDataSD(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, 
                     uint32_t t[], double v[], byte *N, uint32_t *samples, 
                     uint32_t starttime, byte avgN);
```

## Data Record Structure

### New SensorDataRecord Structure
```cpp
struct SensorDataRecord {
    uint64_t deviceMAC;      // Device MAC address
    uint8_t sensorType;      // Sensor type
    uint8_t sensorID;        // Sensor ID
    double sensorValue;      // Sensor value
    uint32_t timeRead;       // Time when sensor was read
    uint32_t timeLogged;     // Time when data was logged
    uint8_t flags;           // Sensor flags
    uint32_t sendingInt;     // Sending interval
};
```

### Benefits of New Structure
- **Efficient Storage**: Only essential data is stored
- **Fast Retrieval**: Optimized for time-based queries
- **Device-Centric**: Organized by device MAC address
- **Version Control**: `_v4.dat` suffix for future compatibility

## Backward Compatibility

### Legacy Functions (DEPRECATED but Functional)
```cpp
bool storeSensorSD(struct SensorVal *S);
bool readSensorSD(struct SensorVal *S, uint32_t t[], double v[], byte *N, uint32_t *samples, uint32_t starttime, uint32_t endtime, byte avgN);
```

### Compatibility Features
- **Automatic Conversion**: Old `SensorVal` structs are converted to new format
- **IP-to-MAC Mapping**: Old IP-based device IDs are converted to MAC format
- **Dual Storage**: Both old and new storage methods are used simultaneously
- **Seamless Migration**: Existing code continues to work

## File Naming Convention

### New Format
```
/Data/Sensor_{MAC}_{Type}_{ID}_v4.dat
```

### Examples
- `/Data/Sensor_AABBCCDDEEFF_4_0_v4.dat` (MAC-based device)
- `/Data/Sensor_FFC0A80101_4_0_v4.dat` (IP-based device with 0xFF prefix)

### Legacy Format (Still Supported)
```
/Data/Sensor_{MAC}_v3.dat
/Data/Sensor_{ArdID}.{Type}.{ID}_v3.dat
```

## Migration Strategy

### Phase 1: Dual Storage (CURRENT)
- New data is stored in both old and new formats
- Legacy functions continue to work
- Gradual transition without breaking existing functionality

### Phase 2: New Format Only (FUTURE)
- Remove legacy storage calls
- Update all code to use new functions
- Clean up old file formats

### Phase 3: Optimization (FUTURE)
- Implement data compression
- Add data retention policies
- Optimize for specific query patterns

## Usage Examples

### Storing Sensor Data
```cpp
// Method 1: By indices (most efficient)
int16_t deviceIndex = Sensors.findDev(deviceMAC, false);
int16_t sensorIndex = Sensors.findSns(deviceIndex, sensorType, sensorID);
storeSensorDataSD(deviceIndex, sensorIndex);

// Method 2: By MAC and sensor info
storeSensorDataSD(deviceMAC, sensorType, sensorID);

// Method 3: Legacy (still works)
SensorVal S;
// ... fill S with data ...
storeSensorSD(&S);
```

### Reading Sensor Data
```cpp
// New method
uint32_t times[100];
double values[100];
byte N = 100;
uint32_t samples;
uint32_t startTime = I.currentTime - 86400; // Last 24 hours
uint32_t endTime = I.currentTime;
byte avgN = 5; // Average every 5 readings

bool success = readSensorDataSD(deviceMAC, sensorType, sensorID, 
                               times, values, &N, &samples, 
                               startTime, endTime, avgN);

// Legacy method (still works)
SensorVal S;
// ... fill S with device/sensor info ...
bool success = readSensorSD(&S, times, values, &N, &samples, startTime, endTime, avgN);
```

## Performance Improvements

### Storage Efficiency
- **Reduced File Size**: New format is more compact
- **Faster Writes**: Optimized record structure
- **Better Organization**: Device-centric file structure

### Retrieval Efficiency
- **Faster Queries**: Optimized for time-based searches
- **Memory Efficient**: Only loads required data
- **Parallel Access**: Multiple sensors can be read simultaneously

## Error Handling

### New Error Categories
- **Invalid Indices**: Device or sensor index out of range
- **Device Not Found**: MAC address not in system
- **Sensor Not Found**: Sensor type/ID combination not found
- **File I/O Errors**: SD card access issues

### Error Recovery
- **Graceful Degradation**: System continues with partial data
- **Automatic Retry**: Failed operations are retried
- **Error Logging**: All errors are logged for debugging

## Testing Recommendations

### 1. **Storage Testing**
- Test with various sensor types
- Test with different time ranges
- Test with large datasets
- Test with network interruptions

### 2. **Retrieval Testing**
- Test time-based queries
- Test averaging functionality
- Test with missing data
- Test with corrupted files

### 3. **Compatibility Testing**
- Test legacy function calls
- Test mixed old/new data
- Test file format conversion
- Test backward compatibility

### 4. **Performance Testing**
- Test storage speed
- Test retrieval speed
- Test memory usage
- Test SD card wear

## Next Steps

1. **Deploy and Test**: Apply the upgrade and monitor performance
2. **Monitor Storage**: Watch for storage efficiency improvements
3. **Update Documentation**: Update any external documentation
4. **Plan Migration**: Schedule removal of legacy functions
5. **Optimize Further**: Consider compression and retention policies

## Notes

- The upgrade maintains full backward compatibility
- Existing data files are preserved
- New data is stored in both formats during transition
- Performance should improve with the new format
- Error handling is more robust 