#ifndef ADDESPNOW_HPP
    #define ADDESPNOW_HPP

    //#define _USEENCRYPTION


    #include <esp_now.h>
    #include "globals.hpp"
    #include "utility.hpp"
    #include "server.hpp"
    #include "BootSecure.hpp"
    #include <string.h>
    #include <esp_system.h>
    #include "timesetup.hpp"


extern STRUCT_PrefsH Prefs;
extern STRUCT_CORE I;


extern DevType Devices;

// --- ESPNow Message Types ---
constexpr uint8_t ESPNOW_MSG_BROADCAST_ALIVE      = 0;   // Broadcast: device is alive (all devices, periodic)
constexpr uint8_t ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED = 1;   // Private: device is alive (all devices, periodic)
constexpr uint8_t ESPNOW_MSG_SERVER_LIST          = 1;   // Private: payload contains up to 6 server addresses (MAC+IP)
constexpr uint8_t ESPNOW_MSG_WIFI_PW_REQUEST      = 2;   // Targeted: request for WiFi password, payload = 16-byte one-time key
constexpr uint8_t ESPNOW_MSG_WIFI_PW_RESPONSE     = 3;   // Private: response to type 2, payload = encrypted WiFi password
constexpr uint8_t ESPNOW_MSG_WIFI_KEY_REQUIRED    = 4;   // Private: indicates new WiFi key required, may trigger new type 2 or 128
constexpr uint8_t ESPNOW_MSG_PING_RESPONSE_REQUIRED    = 5;   // Private: ping the recipient and demand a ping response
constexpr uint8_t ESPNOW_MSG_PING_RESPONSE_SUCCESS    = 6;   // Private: ping response received
constexpr uint8_t ESPNOW_MSG_TERMINATE            = 128; // Private: terminate communication, delete peer
constexpr uint8_t ESPNOW_MSG_GENERAL_ENCRYPTED    = 255; // Private: general encrypted message, which will be nx128 bits + 128 bit IV

// --- ESPNow Message Struct ---
struct ESPNOW_type {
    uint8_t  senderMAC[6];      // Sender's MAC address
    uint32_t senderIP;          // Sender's IP address (uint32_t)
    uint8_t  senderType;        // Device type (>=100 = server, <100 = sensor)
    uint8_t  targetMAC[6];      // Target MAC address (all 0xFF for broadcast). in byte format for ESPNOW
    uint8_t  msgType;           // Message type (see constants above)
    uint8_t  payload[80];       // Payload (see message type for format)
    // For type 2: payload[0..15] = one-time key (encrypted)
    // For type 1: payload = up to 6 server addresses (each 10 bytes: 6 MAC + 4 IP)
    // For type 3: payload = encrypted WiFi password (encrypted)
    // For type 4: payload = as needed (encrypted)
    // For type 255: payload = general encrypted message, which will be nx128 bits + 128 bit IV
    // for type 0: payload = none, just a broadcast of my presence
    // for type 128: payload = unused, this is a special message for the server to send to the sensor to terminate the connection
};

extern ESPNOW_type ESPNOWmsg;
String ESPNowError(esp_err_t result) ;

// --- ESPNow Core Functions ---
bool initESPNOW();
esp_err_t addESPNOWPeer(uint8_t* macad);
esp_err_t delESPNOWPeer(uint8_t* macad);
esp_err_t delESPNOWPeer(uint64_t macad);
bool sendESPNOW(const ESPNOW_type& msg);
bool encryptESPNOWMessage(ESPNOW_type& msg);
bool decryptESPNOWMessage(ESPNOW_type& msg);
bool broadcastServerPresence();
bool requestWiFiPassword(const uint8_t* serverMAC, const uint8_t* nonce= nullptr);
bool sendPingRequest(const uint8_t* targetMAC);
void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);

// --- Utility for server list packing ---
void packServerList(uint8_t* payload, const uint8_t serverMACs[][6], const uint32_t* serverIPs, uint8_t count);
void unpackServerList(const uint8_t* payload, uint8_t serverMACs[][6], uint32_t* serverIPs, uint8_t& count);

// --- Utility for LMK configuration check ---
bool isLMKConfigured();

// --- Utility for LMK key validation ---
bool isValidLMKKey();


// --- Utility for encryption status reporting ---
String getESPNOWEncryptionStatus();

// Generate AP SSID based on MAC address
String generateAPSSID();

#endif
