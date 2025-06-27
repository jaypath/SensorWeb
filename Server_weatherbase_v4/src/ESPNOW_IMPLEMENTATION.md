# ESPNow Implementation Documentation

## Overview
This document describes the ESPNow communication implementation for the SENSORS_WeatherBase3 weather station server. ESPNow enables peer-to-peer communication between ESP32 devices without requiring a WiFi router.

## Architecture

### Message Types
ESPNow messages use the `ESPNOW_type` struct with the following message type bits (RMB to LMB):
- **Bit 0**: Message is sensitive (1 = yes, 0 = no)
- **Bit 1**: Message requires response (1 = yes, 0 = no)
- **Bit 2**: Response must be direct message (1 = yes, 0 = broadcast)
- **Bit 3**: Response must include WiFi info (1 = yes, 0 = no)
- **Bit 4**: Payload matters (1 = yes, 0 = no)

### Device Types
- **Server devices**: Device type >= 100
- **Sensor devices**: Device type < 100

## Key Functions

### Core Functions

#### `initESPNOW()`
Initializes ESPNow communication:
- Initializes ESPNow protocol
- Registers send and receive callbacks
- Adds broadcast peer (FF:FF:FF:FF:FF:FF)
- Returns `true` on success, `false` on failure

#### `sendESPNOW(byte* MAC, struct WiFi_type *w_info)`
Sends ESPNow messages:
- **MAC = nullptr**: Broadcasts public WiFi info without credentials
- **MAC = specific address**: Sends private message to specific device
- **w_info = nullptr**: Creates default WiFi_type structure
- **w_info = WiFi_type**: Uses provided WiFi information

#### `broadcastServerPresence()`
Broadcasts server presence without sensitive information:
- Includes server IP and MAC address
- Clears SSID and password fields
- Sets `HAVECREDENTIALS = false`
- Sets `statusCode = 0` (no sensitive info)

### Helper Functions

#### Message Type Checking
- `isSensitive(uint8_t msgType)`: Checks if message contains sensitive info
- `requiresResponse(uint8_t msgType)`: Checks if message requires response
- `requiresDirectResponse(uint8_t msgType)`: Checks if response must be direct message
- `requiresWiFiInfo(uint8_t msgType)`: Checks if response must include WiFi info

#### Peer Management
- `addESPNOWPeer(byte* macad, bool doEncrypt)`: Adds peer to ESPNow network
- `delESPNOWPeer(byte* macad)`: Removes peer from ESPNow network

### Callback Functions

#### `OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)`
Handles send completion:
- Updates `Prefs.lastESPNOW` timestamp on success
- Logs error on failure

#### `OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len)`
Handles received messages:
- Validates message length
- Updates `Prefs.lastESPNOW` timestamp
- Processes message based on type:
  - **Sensitive messages**: Broadcasts public WiFi info
  - **Non-sensitive messages**: Sends private WiFi credentials

## Integration

### Setup Integration
ESPNow is initialized in `main.cpp` setup function:
```cpp
tft.print("Initializing ESPNow... ");
if (initESPNOW()) {
    tft.setTextColor(TFT_GREEN);
    tft.printf("OK.\n");
    tft.setTextColor(FG_COLOR);
    
    // Broadcast initial server presence
    broadcastServerPresence();
} else {
    tft.setTextColor(TFT_RED);
    tft.printf("FAILED.\n");
    tft.setTextColor(FG_COLOR);
    storeError("ESPNow initialization failed");
}
```

### Loop Integration
Periodic broadcasting is handled in the main loop:
```cpp
if (minute()%5==0) {
    //overwrite sensors to the sd card
    writeSensorsSD();
    storeScreenInfoSD();
    
    // Broadcast server presence via ESPNow every 5 minutes
    if (WiFi.status() == WL_CONNECTED) {
        broadcastServerPresence();
    }
}
```

## Data Structures

### ESPNOW_type
```cpp
struct ESPNOW_type {
    byte senderIP[4];      // 4 bytes - sender's IP address
    byte senderID[6];      // 6 bytes - sender's MAC address
    byte senderType;       // 1 byte - device type (>=100 = server, <100 = sensor)
    byte targetID[6];      // 6 bytes - target MAC address (all zeros for broadcast)
    byte SSID[33];         // 33 bytes - WiFi SSID
    byte PWD[65];          // 65 bytes - WiFi password
    uint8_t msgType;       // 1 byte - message type flags
    uint8_t payload[50];   // 50 bytes - additional payload data
};
```

### WiFi_type
```cpp
struct WiFi_type {
    byte MYIP[4];          // 4 bytes - device IP address
    byte MAC[6];           // 6 bytes - device MAC address
    byte SSID[33];         // 33 bytes - WiFi SSID
    byte PWD[65];          // 65 bytes - WiFi password
    bool HAVECREDENTIALS;  // 1 byte - whether credentials are available
    uint8_t statusCode;    // 1 byte - status flags
};
```

## Security Features

### Encryption Support
- ESPNow supports encryption using LMK (Link Master Key)
- Encryption is enabled when `Prefs.LMK_isSet` is true
- Encrypted peers use channel 3, unencrypted use channel 0

### Credential Protection
- Public broadcasts never include WiFi credentials
- Private messages include credentials only when explicitly requested
- Sensitive information is cleared before public broadcasts

## Communication Flow

### Server Discovery
1. **Server broadcasts presence** every 5 minutes via `broadcastServerPresence()`
2. **Sensors receive broadcast** and add server to their peer list
3. **No response required** for presence broadcasts

### Sensor Registration
1. **Sensor sends ping** (type 2 message) to discover servers
2. **Server receives ping** and registers sensor as peer
3. **Server responds** with WiFi credentials if requested
4. **Server removes peer** after sending credentials

### Private Communication
1. **Device sends private message** (type 10/11) to specific target
2. **Target receives message** and processes based on type
3. **Response sent** if required by message type
4. **Encryption used** if LMK is configured

## Error Handling

### Initialization Errors
- ESPNow initialization failure is logged and displayed
- System continues operation without ESPNow functionality

### Send Errors
- Failed sends are logged via `storeError()`
- Send status is tracked in `OnESPNOWDataSent()`

### Receive Errors
- Messages too short are rejected
- Invalid message types are handled gracefully
- Peer management errors are logged

## Configuration

### Timing
- **Broadcast interval**: Every 5 minutes
- **Initial broadcast**: After successful initialization
- **Broadcast condition**: Only when WiFi is connected

### Network Settings
- **Broadcast address**: FF:FF:FF:FF:FF:FF
- **Channel**: 0 (unencrypted) or 3 (encrypted)
- **Encryption**: Optional, based on LMK configuration

## Benefits

### For Servers
- **Automatic discovery**: Sensors can find servers without manual configuration
- **Load distribution**: Multiple servers can share sensor load
- **Fault tolerance**: Sensors can switch servers if one fails

### For Sensors
- **Easy setup**: No need to manually configure server addresses
- **Dynamic discovery**: Can find servers automatically
- **Redundancy**: Can connect to multiple servers

### For Network
- **Reduced WiFi traffic**: Direct peer-to-peer communication
- **Lower latency**: No router hop required
- **Better reliability**: Works even if WiFi router is down

## Future Enhancements

### Planned Features
- **Multi-server support**: Allow sensors to connect to multiple servers
- **Load balancing**: Distribute sensor load across available servers
- **Encryption**: Implement full encryption for all communications
- **Message queuing**: Handle message delivery guarantees

### Potential Improvements
- **Dynamic channel selection**: Automatically select best WiFi channel
- **Signal strength optimization**: Adjust transmission power based on distance
- **Message compression**: Reduce message size for better performance
- **Reliable delivery**: Implement acknowledgment and retry mechanisms 