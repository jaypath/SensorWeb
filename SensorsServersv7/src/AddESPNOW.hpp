#ifndef ADDESPNOW_HPP
    #define ADDESPNOW_HPP

    //#define _USEENCRYPTION

    #include "globals.hpp"
    #include <esp_now.h>
    #include <WiFi.h>
    #include <string.h>
    #include <esp_system.h>

// Forward declarations - avoid circular includes since globals.hpp includes this file
struct STRUCT_PrefsH;
struct STRUCT_CORE;
struct ArborysDevType;
struct ArborysSnsType;
class Devices_Sensors;

extern STRUCT_PrefsH Prefs;
extern STRUCT_CORE I;

// --- LAN Message Types: ESP-NOW 0-100, UDP 100-200 (UDP = ESP-NOW + 100) ---
constexpr uint8_t LAN_MSG_UDP_OFFSET = 100;

constexpr uint8_t ESPNOW_MSG_BROADCAST_ALIVE           = 0;  // Broadcast: device is alive (unencrypted; not offered)
constexpr uint8_t ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED   = 1;  // Broadcast: device is alive (encrypted)
constexpr uint8_t ESPNOW_MSG_WIFI_PW_REQUEST           = 2;  // Targeted: request WiFi password
constexpr uint8_t ESPNOW_MSG_WIFI_PW_RESPONSE          = 3;  // Response to type 2
constexpr uint8_t ESPNOW_MSG_WIFI_KEY_REQUIRED         = 4;  // New WiFi key required; may trigger type 2 or 102
constexpr uint8_t ESPNOW_MSG_PING_RESPONSE_REQUIRED      = 5;  // Directed ping; expects type 6 / 106
constexpr uint8_t ESPNOW_MSG_PING_RESPONSE_SUCCESS       = 6;  // Ping response
constexpr uint8_t ESPNOW_MSG_REQUEST_SENSOR_DATA         = 7;  // Request sensor data from recipient
constexpr uint8_t ESPNOW_MSG_BLOCKING_PING               = 8;  // Blocking ping; expects type 9 / 109
constexpr uint8_t ESPNOW_MSG_BLOCKING_PING_RESPONSE      = 9;  // Response to type 8 / 108
constexpr uint8_t ESPNOW_MSG_TERMINATE                   = 10; // Terminate communication, delete peer
constexpr uint8_t ESPNOW_MSG_GENERAL_ENCRYPTED           = 11; // General encrypted message
constexpr uint8_t ESPNOW_MSG_SENSOR_DATA                 = 12; // Single sensor data (payload LAN_sensor_payload_t)
constexpr uint8_t ESPNOW_MSG_BROADCAST_SERVER_PING       = 13; // Broadcast: discover servers; servers reply with type 14
constexpr uint8_t ESPNOW_MSG_SERVER_PING_RESPONSE        = 14; // Unicast server response to type 13 / 113
constexpr uint16_t ESPNOW_BLOCKING_PING_DEFAULT_MS       = 500;

constexpr uint8_t UDP_MSG_BROADCAST_ALIVE              = ESPNOW_MSG_BROADCAST_ALIVE + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_BROADCAST_ALIVE_ENCRYPTED      = ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_WIFI_PW_REQUEST              = ESPNOW_MSG_WIFI_PW_REQUEST + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_WIFI_PW_RESPONSE             = ESPNOW_MSG_WIFI_PW_RESPONSE + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_WIFI_KEY_REQUIRED            = ESPNOW_MSG_WIFI_KEY_REQUIRED + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_PING_RESPONSE_REQUIRED       = ESPNOW_MSG_PING_RESPONSE_REQUIRED + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_PING_RESPONSE_SUCCESS        = ESPNOW_MSG_PING_RESPONSE_SUCCESS + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_REQUEST_SENSOR_DATA          = ESPNOW_MSG_REQUEST_SENSOR_DATA + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_BLOCKING_PING                = ESPNOW_MSG_BLOCKING_PING + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_BLOCKING_PING_RESPONSE       = ESPNOW_MSG_BLOCKING_PING_RESPONSE + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_TERMINATE                    = ESPNOW_MSG_TERMINATE + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_GENERAL_ENCRYPTED            = ESPNOW_MSG_GENERAL_ENCRYPTED + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_SENSOR_DATA                  = ESPNOW_MSG_SENSOR_DATA + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_BROADCAST_SERVER_PING        = ESPNOW_MSG_BROADCAST_SERVER_PING + LAN_MSG_UDP_OFFSET;
constexpr uint8_t UDP_MSG_SERVER_PING_RESPONSE         = ESPNOW_MSG_SERVER_PING_RESPONSE + LAN_MSG_UDP_OFFSET;

// AES-CBC envelope: 80-byte plaintext + 16-byte IV = 96-byte ciphertext (fits payload[96])
constexpr uint8_t LAN_ENCRYPT_MSG_LEN = 96;
constexpr uint8_t LAN_PLAINTEXT_MAX = LAN_ENCRYPT_MSG_LEN - 16;

// Common encrypted payload prefix: time/id, sender IP, sender type, firmware
constexpr uint8_t LAN_META_TIME_OFF = 0;
constexpr uint8_t LAN_META_IP_OFF = 4;
constexpr uint8_t LAN_META_TYPE_OFF = 8;
constexpr uint8_t LAN_META_FW_OFF = 9;
constexpr uint8_t LAN_META_SIZE = 12;
constexpr uint8_t LAN_DEVNAME_OFF = LAN_META_SIZE;
constexpr uint8_t LAN_DEVNAME_LEN = 30;

// WiFi password request/response crypto fields (after common meta)
constexpr uint8_t LAN_WIFI_KEY_OFF = LAN_META_SIZE;
constexpr uint8_t LAN_WIFI_IV_OFF = 28;
constexpr uint8_t LAN_WIFI_NONCE_OFF = 44;
constexpr uint8_t LAN_WIFI_NAME_OFF = 52;
constexpr uint8_t LAN_WIFI_NAME_LEN = 28;

#pragma pack(push, 1)
struct LAN_payload_meta_t {
    uint32_t timeOrId;
    uint8_t  senderIP[4];
    uint8_t  senderType;
    uint8_t  firmware[3];
};
#pragma pack(pop)
static_assert(sizeof(LAN_payload_meta_t) == LAN_META_SIZE, "LAN_payload_meta_t must be 12 bytes");

// Device registration payload for broadcast server ping (13/113) and server response (14/114)
constexpr uint8_t LAN_DEVICE_REG_DEVNAME_LEN = LAN_DEVNAME_LEN;
#pragma pack(push, 1)
struct LAN_device_reg_payload_t {
    uint32_t timestamp;
    uint8_t  senderIP[4];
    uint8_t  devType;
    uint8_t  firmware[3];
    uint8_t  devFlags;
    uint8_t  wifiChannel;
    uint8_t  reserved;
    uint32_t sendingInt;
    char     devName[LAN_DEVICE_REG_DEVNAME_LEN];
};
#pragma pack(pop)
static_assert(sizeof(LAN_device_reg_payload_t) == 49, "LAN_device_reg_payload_t must be 49 bytes");

// Single-sensor LAN payload (76 bytes plaintext; fits 80-byte encrypt limit)
constexpr uint8_t LAN_SENSOR_DEVNAME_LEN = 19;
constexpr uint8_t LAN_SENSOR_SNSNAME_LEN = 30;
#pragma pack(push, 1)
struct LAN_sensor_payload_t {
    uint32_t timeStamp;
    uint8_t  senderIP[4];
    uint8_t  senderType;
    uint8_t  firmware[3];
    uint8_t  snsType;
    uint8_t  snsID;
    float    snsValue;
    uint32_t timeRead;
    uint8_t  flags;
    uint32_t SendingInt;
    char     snsName[LAN_SENSOR_SNSNAME_LEN];
    char     devName[LAN_SENSOR_DEVNAME_LEN];
};
#pragma pack(pop)
static_assert(sizeof(LAN_sensor_payload_t) == 76, "LAN_sensor_payload_t must be 76 bytes");

inline uint8_t lanMsgBaseType(uint8_t msgType) {
    return (msgType >= LAN_MSG_UDP_OFFSET) ? static_cast<uint8_t>(msgType - LAN_MSG_UDP_OFFSET) : msgType;
}
inline bool lanMsgIsUDP(uint8_t msgType) { return msgType >= LAN_MSG_UDP_OFFSET; }
inline bool lanMsgIsEncrypted(uint8_t msgType) {
    return lanMsgBaseType(msgType) != ESPNOW_MSG_BROADCAST_ALIVE;
}


// --- ESPNow Message Struct ---
#pragma pack(push, 1)
struct ESPNOW_type {
    uint8_t  senderMAC[6];      // Sender's MAC address
    uint8_t  targetMAC[6];      // Target MAC address (all 0xFF for broadcast)
    uint8_t  msgType;           // Message type (see constants above)
    uint8_t  payload[96];       // Encrypted body; see LAN_payload_meta_t and per-type layouts
};
#pragma pack(pop)

extern ESPNOW_type ESPNOWmsg;
String ESPNowError(esp_err_t result) ;

// --- ESPNow Core Functions ---
int8_t initESPNOW();
bool ensureESPNOW(); // init if needed; does not require router connection
esp_err_t addESPNOWPeer(uint8_t* macad);
esp_err_t delESPNOWPeer(uint8_t* macad);
esp_err_t delESPNOWPeer(uint64_t macad);
bool sendLANmsg_ESPNOW(ESPNOW_type msg, bool forceencrypt = false);
bool sendLANmsg_UDP(ESPNOW_type msg, IPAddress targetIP, bool forceencrypt = false);

bool encryptLANMessage(ESPNOW_type& msg, byte msglen = LAN_ENCRYPT_MSG_LEN);
bool decryptLANMessage(ESPNOW_type& msg, byte msglen = LAN_ENCRYPT_MSG_LEN);
bool broadcastServerPresence(bool broadcastPeripheral=false, uint8_t method=2);
bool broadcastServerPing(uint8_t tier = 1);
void beginServerPingTimeSync();
void endServerPingTimeSync();
bool takeServerPingTimeSync(uint32_t& outLocalTime);
bool requestWiFiPassword(const uint8_t* serverMAC, const uint8_t* nonce= nullptr);
void makeLanPingMsg(ESPNOW_type& msg, uint64_t targetMAC, uint8_t tier);
bool sendLANPingRequest(ArborysDevType* targetDevice, uint8_t tier, bool dataRequest);
bool sendLANSensorDataRequest(ArborysDevType* targetDevice, uint8_t tier);
bool sendLANSensorData(ArborysSnsType* sensor, uint64_t targetMAC, IPAddress targetIP, uint8_t tier);
bool sendLANSensorDataAll(uint64_t targetMAC, IPAddress targetIP, uint8_t tier);
bool sendLANBlockingPing(ArborysDevType* targetDevice, uint8_t tier = 1, uint16_t blockTimeMs = ESPNOW_BLOCKING_PING_DEFAULT_MS, uint32_t* rttMsOut = nullptr);
void OnESPNOWDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status);
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len);
void processLANMessage(ESPNOW_type* msg);
uint8_t registerLANSensorData(ESPNOW_type* msg);
void makeBroadcastMAC(uint8_t* mac);
bool isBroadcastMAC(uint8_t* mac);

// --- Utility for LMK configuration check ---
bool isLMKConfigured();

// --- Utility for LMK key validation ---
bool isValidLMKKey();


// Note: generateAPSSID() is declared in server.hpp to avoid duplicate declarations


#endif
