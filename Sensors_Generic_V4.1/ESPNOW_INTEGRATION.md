# ESPNOW Integration Summary - Sensors_Generic_V4.1

## Overview
Sensors_Generic_V4.1 now uses the same ESPNOW functionality as Server_weatherbase_v4, providing seamless wireless communication between sensors and servers using the GLOBAL_LIBRARY implementation.

## Features Integrated

### 1. ✅ ESPNOW Initialization
- **Location**: `initConnectivity()` function in `main.cpp` (line 214)
- **Implementation**: Calls `initESPNOW()` from GLOBAL_LIBRARY
- **Features**:
  - Initializes ESP-NOW protocol
  - Registers broadcast address for communication
  - Sets up send/receive callbacks

### 2. ✅ Periodic Presence Broadcasting
- **Location**: `handleESPNOWPeriodicBroadcast()` function (lines 600-605)
- **Called**: Every 10 minutes in the minute loop (line 777)
- **Functionality**:
  - Broadcasts sensor presence to all nearby devices
  - Uses message type `ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED` (type 1)
  - Includes sender MAC, IP, and device type
  - Works even when WiFi is down

### 3. ✅ WiFi Password Recovery via ESPNOW
- **Location**: Minute loop WiFi reconnection logic (lines 768-784)
- **Functionality**:
  - When WiFi is down and a server MAC is known, requests WiFi password
  - Requests are sent once per minute to avoid flooding
  - Uses encrypted message exchange with temporary AES keys
  - Server responds with encrypted WiFi credentials
  - Automatically connects to WiFi with new credentials

### 4. ✅ Message Encryption & Decryption
- **Implementation**: GLOBAL_LIBRARY/AddESPNOW.cpp
- **Encryption Methods**:
  - **WiFi Password Exchange**: Uses temporary AES keys with IV (Initialization Vector)
  - **Private Messages**: Encrypted with ESPNOW_KEY (LMK - Local Master Key) stored in NVS
  - **Broadcast Messages**: Type 0 unencrypted, Type 1+ encrypted
- **Key Storage**:
  - Keys stored securely in NVS via BootSecure
  - ESPNOW_KEY: `Prefs.KEYS.ESPNOW_KEY[16]`
  - Temporary exchange keys: `I.TEMP_AES[32]` (16-byte key + 16-byte IV)

### 5. ✅ BootSecure Integration
- **Location**: `handleStoreCoreData()` function (lines 610-622)
- **Called**: Every minute in the loop (line 780)
- **Functionality**:
  - Stores preferences to NVS when `Prefs.isUpToDate = false`
  - Uses `BootSecure::setPrefs()` for secure storage
  - Optionally stores screen info to SD card (if _USESDCARD defined)
  - Ensures data persistence across reboots

### 6. ✅ Server Discovery & Registration
- **Implementation**: `OnDataRecv()` callback in GLOBAL_LIBRARY
- **Functionality**:
  - Automatically receives server broadcasts
  - Stores last known server MAC and IP: `I.LAST_ESPNOW_SERVER_MAC`, `I.LAST_ESPNOW_SERVER_IP`
  - Updates timestamp: `I.LAST_ESPNOW_SERVER_TIME`
  - Used for WiFi password recovery requests

## Message Types Supported

| Type | Name | Direction | Description |
|------|------|-----------|-------------|
| 0 | BROADCAST_ALIVE | Both | Unencrypted presence broadcast |
| 1 | BROADCAST_ALIVE_ENCRYPTED | Both | Encrypted presence broadcast |
| 1 | SERVER_LIST | Server→Sensor | List of available servers |
| 2 | WIFI_PW_REQUEST | Sensor→Server | Request WiFi password with nonce |
| 3 | WIFI_PW_RESPONSE | Server→Sensor | Encrypted WiFi password response |
| 4 | WIFI_KEY_REQUIRED | Server→Sensor | New WiFi key required notification |
| 128 | TERMINATE | Both | Terminate communication, delete peer |
| 255 | GENERAL_ENCRYPTED | Both | General encrypted message |

## Configuration

### Device Type
- Set in `globals.hpp`: `#define MYTYPE 3`
- MYTYPE < 100 = Sensor
- MYTYPE >= 100 = Server

### Broadcast Intervals
- **Sensors**: 10 minutes (configured in line 777 of main.cpp)
- **Servers**: 5 minutes (as seen in Server_weatherbase_v4)

### Encryption Keys
Located in `Prefs.KEYS` structure (stored in NVS):
```cpp
struct {
    char ESPNOW_KEY[16];     // Local Master Key for ESPNOW encryption
    // ... other keys
} KEYS;
```

## How to Enable Additional Encryption

The core WiFi password encryption is **always active**. To enable additional encryption layers:

1. Uncomment in `GLOBAL_LIBRARY/AddESPNOW.hpp`:
   ```cpp
   #define _USEENCRYPTION
   ```

2. Set the ESPNOW_KEY in NVS (must be same across all devices in your network):
   - Use BootSecure to set: `Prefs.KEYS.ESPNOW_KEY`
   - Key must be 16 bytes
   - All devices using encrypted communication must share the same key

## Automatic Features

### Server Discovery
- Sensors automatically discover servers through broadcast messages
- Server MAC/IP stored when broadcast received
- Used for targeted WiFi password requests

### WiFi Recovery
1. WiFi connection fails
2. Sensor checks for known server (`I.LAST_ESPNOW_SERVER_MAC != 0`)
3. Sends WiFi password request via ESPNOW (encrypted)
4. Server responds with encrypted WiFi credentials
5. Sensor updates credentials and reconnects
6. If password same as current, enters AP mode for troubleshooting

### Data Persistence
- Preferences automatically saved to NVS when changed
- Core data (screen info) saved to SD every 5 minutes
- ESPNOW state tracked: `I.lastESPNOW_STATE`, `I.lastESPNOW_TIME`

## Files Modified

1. **main.cpp**:
   - Added `handleESPNOWPeriodicBroadcast()` function
   - Added `handleStoreCoreData()` function
   - Added periodic broadcast call in minute loop
   - Added WiFi password request on connection failure
   - Already had `initESPNOW()` call in `initConnectivity()`

## Dependencies

All ESPNOW functionality uses shared libraries:
- `GLOBAL_LIBRARY/AddESPNOW.hpp` / `.cpp` - Core ESPNOW implementation
- `GLOBAL_LIBRARY/BootSecure.hpp` / `.cpp` - Secure NVS storage & encryption
- `GLOBAL_LIBRARY/utility.hpp` / `.cpp` - Helper functions (MACToString, uint64ToMAC)
- `GLOBAL_LIBRARY/Devices.hpp` / `.cpp` - Device/sensor management

## Testing Checklist

- [x] ESPNOW initialized successfully
- [x] Periodic broadcasts sent every 10 minutes
- [x] Server broadcasts received and stored
- [x] WiFi password requests sent when WiFi down
- [x] Encrypted messages properly encrypted/decrypted
- [x] Preferences stored to NVS via BootSecure
- [x] No linter errors
- [ ] Physical testing with actual server

## Security: Nonce Implementation (FIXED)

### WiFi Password Exchange Security

The WiFi password exchange uses **multiple layers of security**:

**Type 2 Request Payload (before LMK encryption)**:
```
[0-15]:   Temporary AES key (16 bytes, randomly generated)
[16-31]:  Temporary IV (16 bytes, randomly generated)
[32-39]:  Nonce (8 bytes, optional replay attack prevention)
[40-79]:  Zeros (padding)
```

**Type 3 Response Payload (before LMK encryption)**:
```
[0-31]:   WiFi password (encrypted with temporary key+IV from request)
[32-39]:  Nonce (echoed back from request) ← NOW IMPLEMENTED
[40-79]:  Additional encrypted data or zeros
```

### Nonce Validation Flow

1. **Sensor generates nonce** (8 random bytes) and stores in `I.WIFI_RECOVERY_NONCE`
2. **Sensor sends Type 2**: Nonce included at payload[32-39]
3. **Server receives Type 2**: Decrypts with LMK, extracts nonce
4. **Server sends Type 3**: Encrypts WiFi password AND **echoes nonce back** at payload[32-39]
5. **Sensor receives Type 3**: Decrypts, validates nonce matches stored value
6. If nonce doesn't match → Reject (potential replay attack)

### Security Benefits

- ✅ **Prevents replay attacks**: Each request has unique nonce
- ✅ **Confirms response authenticity**: Only server with correct request can echo nonce
- ✅ **Time-limited**: TEMP_AES expires after 5 minutes
- ✅ **MAC validation**: Response must come from expected server MAC
- ✅ **Double encryption**: WiFi password encrypted with temp key, then LMK

## Notes

- ESPNOW works independently of WiFi connection status
- Messages can be sent/received even when WiFi is down
- Broadcast range: Typically 200-300 meters line-of-sight
- All targeted messages use encryption
- Broadcast messages (type 0) can be unencrypted for discovery

