#ifndef ADDESPNOW_HPP
    #define ADDESPNOW_HPP

    //#define _USEENCRYPTION


    #include <esp_now.h>
    #include "globals.hpp"
    #include "utility.hpp"
    #include "server.hpp"
    #include "BootSecure.hpp"
    #include "graphics.hpp"
    #include <string.h>
    #include <esp_system.h>
    #include "timesetup.hpp"


extern STRUCT_PrefsH Prefs;
extern Screen I;


extern DevType Devices;

// --- ESPNow Message Types ---
constexpr uint8_t ESPNOW_MSG_BROADCAST_ALIVE      = 0;   // Broadcast: device is alive (all devices, periodic)
constexpr uint8_t ESPNOW_MSG_SERVER_LIST          = 1;   // Broadcast: payload contains up to 6 server addresses (MAC+IP)
constexpr uint8_t ESPNOW_MSG_WIFI_PW_REQUEST      = 2;   // Targeted: request for WiFi password, payload = 16-byte one-time key
constexpr uint8_t ESPNOW_MSG_WIFI_PW_RESPONSE     = 3;   // Private: response to type 2, payload = encrypted WiFi password
constexpr uint8_t ESPNOW_MSG_WIFI_KEY_REQUIRED    = 4;   // Private: indicates new WiFi key required, may trigger new type 2 or 128
constexpr uint8_t ESPNOW_MSG_TERMINATE            = 128; // Private: terminate communication, delete peer

// --- ESPNow Message Struct ---
struct ESPNOW_type {
    uint8_t  senderMAC[6];      // Sender's MAC address
    uint32_t senderIP;          // Sender's IP address (uint32_t)
    uint8_t  senderType;        // Device type (>=100 = server, <100 = sensor)
    uint8_t  targetMAC[6];      // Target MAC address (all 0xFF for broadcast)
    uint8_t  msgType;           // Message type (see constants above)
    uint8_t  payload[60];       // Payload (see message type for format)
    // For type 2: payload[0..15] = one-time key
    // For type 1: payload = up to 6 server addresses (each 10 bytes: 6 MAC + 4 IP)
    // For type 3: payload = encrypted WiFi password
    // For type 4: payload = as needed
    // For type 128: payload = unused
};

extern ESPNOW_type ESPNOWmsg;
String ESPNowError(esp_err_t result) ;

// --- ESPNow Core Functions ---
bool initESPNOW();
esp_err_t addESPNOWPeer(uint8_t* macad, bool doEncrypt);
esp_err_t delESPNOWPeer(uint8_t* macad);
bool sendESPNOW(const ESPNOW_type& msg);
bool broadcastServerPresence();
bool requestWiFiPassword(const uint8_t* serverMAC, const uint8_t* nonce= nullptr);
void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);

// --- Utility for server list packing ---
void packServerList(uint8_t* payload, const uint8_t serverMACs[][6], const uint32_t* serverIPs, uint8_t count);
void unpackServerList(const uint8_t* payload, uint8_t serverMACs[][6], uint32_t* serverIPs, uint8_t& count);

// Generate AP SSID based on MAC address
String generateAPSSID();

#endif
