/*ESPNow general structure...

All messages will be sent as a struct, which may or may not contain sensitive info (if not, then the sensitive fields will be zeroed)

Every X minutes, 
--a Server will broadcast it's presence. This is a type 1 message
  --a type message will include at minimum:
    --server ID (MAC) 
    --Server IP
--a sensor will accept/update server addresses and add to their internal list to send data
--no response is required

At any time, a sensor can broadcast a ping (a type 2 message). This may be optional (the sensor could simply send data via TCP/IP instead)
--all server types will register the device 
--no response is required, but if a server did not have that sensor registered then a type 1 message should be sent

At any time, a broadcase message can be sent that REQUIRES a private response from the intended recipient (a type 3 message). This broadcast message will indicate who the intended recipient is.

At any time, a private message can be sent (a type 10 message). Such a message will be encrypted with the vendor PMK and the user's LMK
--The recipient will not be required to respond.


At any time, a private message can be sent, asking for a response (a type 11 message). Such a message will be encrypted with the vendor PMK and the user's LMK
--The recipient will respond with wifi info


Messages will use the ESPNOW_type struct for their transmission


*/

#include "AddESPNOW.hpp"
#include "BootSecure.hpp"
#include <string.h>
#include <esp_system.h>
#include <time.h>

namespace {
    constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    // Default intervals (seconds)
    constexpr uint32_t DEFAULT_SERVER_BROADCAST_INTERVAL = 300; // 5 min
    constexpr uint32_t DEFAULT_SERVER_LIST_INTERVAL = 600;      // 10 min
    constexpr uint32_t DEFAULT_SENSOR_BROADCAST_INTERVAL = 600; // 10 min
}

// --- Utility: Pack/Unpack server list ---
void packServerList(uint8_t* payload, const uint8_t serverMACs[][6], const uint32_t* serverIPs, uint8_t count) {
    memset(payload, 0, 60);
    for (uint8_t i = 0; i < count && i < 6; ++i) {
        memcpy(payload + i * 10, serverMACs[i], 6);
        uint32_t ip = serverIPs[i];
        memcpy(payload + i * 10 + 6, &ip, 4);
    }
}
void unpackServerList(const uint8_t* payload, uint8_t serverMACs[][6], uint32_t* serverIPs, uint8_t& count) {
    count = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        bool allZero = true;
        for (int j = 0; j < 10; ++j) if (payload[i*10+j] != 0) allZero = false;
        if (allZero) break;
        memcpy(serverMACs[i], payload + i * 10, 6);
        memcpy(&serverIPs[i], payload + i * 10 + 6, 4);
        ++count;
    }
}

// --- ESPNow Initialization ---
bool initESPNOW() {
    if (esp_now_init() != ESP_OK) {
        storeError("ESPNow: Failed to initialize");
        return false;
    }
    esp_now_register_send_cb(OnESPNOWDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    return true;
}

// --- Peer Management ---
esp_err_t addESPNOWPeer(uint8_t* macad, bool doEncrypt) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macad, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    return esp_now_add_peer(&peerInfo);
}
esp_err_t delESPNOWPeer(uint8_t* macad) {
    return esp_now_del_peer(macad);
}

// --- Send ESPNow Message ---
bool sendESPNOW(const ESPNOW_type& msg) {
    const uint8_t* destMAC = msg.targetMAC;
    bool isBroadcast = true;
    for (int i = 0; i < 6; ++i) if (destMAC[i] != 0xFF) isBroadcast = false;
    if (!isBroadcast) {
        addESPNOWPeer(const_cast<uint8_t*>(destMAC), false);
    }
    esp_err_t result = esp_now_send(destMAC, reinterpret_cast<const uint8_t*>(&msg), sizeof(ESPNOW_type));
    if (!isBroadcast) {
        delESPNOWPeer(const_cast<uint8_t*>(destMAC));
    }
    if (result != ESP_OK) {
        storeError("ESPNow: Failed to send data");
        return false;
    }
    return true;
}

// --- ESPNow Send Callback ---
void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Prefs.lastESPNOW = I.currentTime;
    } else {
        storeError("ESPNow: Failed to send data");
    }
}

// --- ESPNow Receive Callback ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (len < static_cast<int>(sizeof(ESPNOW_type))) {
        storeError("ESPNow: Received message too short");
        return;
    }
    ESPNOW_type msg;
    memcpy(&msg, incomingData, sizeof(msg));
    Prefs.lastESPNOW = I.currentTime;

    // --- Store last server MAC if broadcast from server type 100 ---
    bool isBroadcast = true;
    for (int i = 0; i < 6; ++i) if (msg.targetMAC[i] != 0xFF) isBroadcast = false;
    if (isBroadcast && msg.senderType == 100) {
        memcpy(Prefs.LAST_SERVER, msg.senderMAC, 6);
    }

    switch (msg.msgType) {
        case ESPNOW_MSG_BROADCAST_ALIVE:
            // Device is alive; update presence table if needed
            // (User can implement presence logic here)
            break;
        case ESPNOW_MSG_SERVER_LIST:
            // Received server list; update local list
            // (User can implement server list update logic here)
            break;
        case ESPNOW_MSG_WIFI_PW_REQUEST: {
            // Received request for WiFi password (payload[0..15] = key, payload[16..31] = IV)
            ESPNOW_type resp = {};
            memcpy(resp.senderMAC, WIFI_INFO.MAC, 6);
            resp.senderIP = WIFI_INFO.MYIP;
            resp.senderType = MYTYPE;
            memcpy(resp.targetMAC, msg.senderMAC, 6);
            resp.msgType = ESPNOW_MSG_WIFI_PW_RESPONSE;
            // Encrypt password using provided key and IV
            uint8_t encrypted[48] = {0};
            uint16_t outlen = 0;
            BootSecure::encryptWithIV(WIFI_INFO.PWD, 32, (char*)msg.payload, msg.payload+16, encrypted, &outlen);
            memcpy(resp.payload, encrypted, (outlen < 48) ? outlen : 48);
            sendESPNOW(resp);
            break;
        }
        case ESPNOW_MSG_WIFI_PW_RESPONSE: {
            // Check TEMP_AES, TEMP_AES_TIME, TEMP_AES_MAC, and NONCE
            uint32_t nowt = (uint32_t)time(nullptr);
            bool valid = false;
            if (Prefs.TEMP_AES_TIME != 0 && (nowt - Prefs.TEMP_AES_TIME) <= 300) {
                for (int i = 0; i < 32; ++i) if (Prefs.TEMP_AES[i] != 0) valid = true;
            }
            // Check MAC match
            bool macMatch = true;
            for (int i = 0; i < 6; ++i) {
                if (Prefs.TEMP_AES_MAC[i] != msg.senderMAC[i]) macMatch = false;
            }
            // Check nonce
            bool nonceMatch = true;
            for (int i = 0; i < 8; ++i) {
                if (Prefs.WIFI_RECOVERY_NONCE[i] != msg.payload[32 + i]) nonceMatch = false;
            }
            if (!valid || !macMatch || !nonceMatch) {
                storeError("WiFi PW response: No valid TEMP_AES, expired, MAC mismatch, or nonce mismatch");
                memset(Prefs.TEMP_AES, 0, 32);
                Prefs.TEMP_AES_TIME = 0;
                memset(Prefs.TEMP_AES_MAC, 0, 6);
                memset(Prefs.WIFI_RECOVERY_NONCE, 0, 8);
                break;
            }
            // Decrypt
            char key[17]; memcpy(key, Prefs.TEMP_AES, 16); key[16] = 0;
            uint8_t* iv = Prefs.TEMP_AES + 16;
            uint8_t decrypted[65] = {0};
            int8_t decres = BootSecure::decryptWithIV((unsigned char*)msg.payload, key, iv, decrypted, 32); // 32 bytes encrypted
            if (decres != 0) {
                storeError("WiFi PW response: Decryption failed");
                memset(Prefs.TEMP_AES, 0, 32);
                Prefs.TEMP_AES_TIME = 0;
                memset(Prefs.TEMP_AES_MAC, 0, 6);
                memset(Prefs.WIFI_RECOVERY_NONCE, 0, 8);
                break;
            }
            // Store WiFi password
            bool pwdMatch = (memcmp(WIFI_INFO.PWD, decrypted, 64) == 0);
            memcpy(WIFI_INFO.PWD, decrypted, 64);
            WIFI_INFO.PWD[64] = 0;
            memset(Prefs.TEMP_AES, 0, 32);
            Prefs.TEMP_AES_TIME = 0;
            memset(Prefs.TEMP_AES_MAC, 0, 6);
            memset(Prefs.WIFI_RECOVERY_NONCE, 0, 8);
            putWiFiCredentials();
            // If password matches, WiFi may be down; enter AP mode and show message
            if (pwdMatch) {
                #ifdef HAS_TFT
                tft.clear();
                tft.setCursor(0, 0);
                tft.setTextColor(TFT_RED);
                tft.printf("WiFi may be down.\nAP mode enabled.\nConnect to: %s\nIP: 192.168.4.1\nRebooting in 5 min...", WiFi.softAPSSID().c_str());
                #endif
                WiFi.mode(WIFI_AP);
                WiFi.softAP(generateAPSSID().c_str(), "S3nsor.N3t!");
                delay(100);
                WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
                delay(300000); // 5 min
                controlledReboot("WiFi may be down, AP fallback", RESET_WIFI, true);
            }
            // Log success
            #ifdef _DEBUG
            Serial.println("[ESPNow] WiFi password updated from server response.");
            #endif
            break;
        }
        case ESPNOW_MSG_WIFI_KEY_REQUIRED: {
            // Check MAC and time (2 minutes)
            uint32_t nowt = (uint32_t)time(nullptr);
            bool valid = false;
            if (Prefs.TEMP_AES_TIME != 0 && (nowt - Prefs.TEMP_AES_TIME) <= 120) {
                valid = true;
            }
            bool macMatch = true;
            for (int i = 0; i < 6; ++i) {
                if (Prefs.TEMP_AES_MAC[i] != msg.senderMAC[i]) macMatch = false;
            }
            if (!valid || !macMatch) {
                storeError("WiFi KEY REQUIRED: MAC mismatch or expired");
                memset(Prefs.TEMP_AES, 0, 32);
                Prefs.TEMP_AES_TIME = 0;
                memset(Prefs.TEMP_AES_MAC, 0, 6);
                break;
            }
            // Zero out old TEMP_AES and TEMP_AES_TIME
            memset(Prefs.TEMP_AES, 0, 32);
            Prefs.TEMP_AES_TIME = 0;
            // Send a new type 2 message to the same MAC
            requestWiFiPassword(Prefs.TEMP_AES_MAC);
            break;
        }
        case ESPNOW_MSG_TERMINATE:
            // Terminate communication, delete peer if needed
            delESPNOWPeer(const_cast<uint8_t*>(msg.senderMAC));
            // Zero TEMP_AES and TEMP_AES_TIME for security
            memset(Prefs.TEMP_AES, 0, 32);
            Prefs.TEMP_AES_TIME = 0;
            memset(Prefs.TEMP_AES_MAC, 0, 6);
            break;
        default:
            // Unknown type
            break;
    }
}

// --- Broadcast Server Presence (Type 0) ---
bool broadcastServerPresence() {
    ESPNOW_type msg = {};
    memcpy(msg.senderMAC, WIFI_INFO.MAC, 6);
    msg.senderIP = WIFI_INFO.MYIP;
    msg.senderType = MYTYPE;
    memset(msg.targetMAC, 0xFF, 6);
    msg.msgType = ESPNOW_MSG_BROADCAST_ALIVE;
    memset(msg.payload, 0, 60);
    return sendESPNOW(msg);
}

// --- Broadcast Server List (Type 1) ---
bool broadcastServerList(const uint8_t serverMACs[][6], const uint32_t* serverIPs, uint8_t count) {
    // If user provided a list, use it
    if (serverMACs && serverIPs && count > 0) {
        ESPNOW_type msg = {};
        memcpy(msg.senderMAC, WIFI_INFO.MAC, 6);
        msg.senderIP = WIFI_INFO.MYIP;
        msg.senderType = MYTYPE;
        memset(msg.targetMAC, 0xFF, 6);
        msg.msgType = ESPNOW_MSG_SERVER_LIST;
        packServerList(msg.payload, serverMACs, serverIPs, count);
        return sendESPNOW(msg);
    }

    // Otherwise, search Sensors for up to 6 servers (devType >= 100)
    uint8_t found = 0;
    uint8_t macs[6][6] = {};
    uint32_t ips[6] = {};
    for (int16_t i = 0; i < Sensors.getNumDevices() && found < 6; ++i) {
        DevType* dev = Sensors.getDeviceByIndex(i);
        if (dev && dev->IsSet && dev->devType >= 100) {
            // Convert uint64_t MAC to 6 bytes
            for (int j = 0; j < 6; ++j)
                macs[found][5-j] = (dev->MAC >> (8*j)) & 0xFF;
            ips[found] = dev->IP;
            ++found;
        }
    }
    ESPNOW_type msg = {};
    memcpy(msg.senderMAC, WIFI_INFO.MAC, 6);
    msg.senderIP = WIFI_INFO.MYIP;
    msg.senderType = MYTYPE;
    memset(msg.targetMAC, 0xFF, 6);
    msg.msgType = ESPNOW_MSG_SERVER_LIST;
    packServerList(msg.payload, macs, ips, found);
    return sendESPNOW(msg);
}

// --- Request WiFi Password via ESPNow (with nonce) ---
bool requestWiFiPassword(const uint8_t* serverMAC, const uint8_t* nonce = nullptr) {
    // Generate random 16-byte key and 16-byte IV
    uint8_t key[16], iv[16];
    esp_fill_random(key, 16);
    esp_fill_random(iv, 16);
    // Store in Prefs
    memcpy(Prefs.TEMP_AES, key, 16);
    memcpy(Prefs.TEMP_AES + 16, iv, 16);
    Prefs.TEMP_AES_TIME = (uint32_t)time(nullptr);
    // Prepare ESPNow message
    ESPNOW_type msg = {};
    memcpy(msg.senderMAC, WIFI_INFO.MAC, 6);
    msg.senderIP = WIFI_INFO.MYIP;
    msg.senderType = MYTYPE;
    msg.msgType = ESPNOW_MSG_WIFI_PW_REQUEST;
    memcpy(msg.payload, Prefs.TEMP_AES, 32); // key+IV
    // Add nonce if provided
    if (nonce) {
        memcpy(msg.payload + 32, nonce, 8);
        memcpy(Prefs.WIFI_RECOVERY_NONCE, nonce, 8);
    } else {
        memset(msg.payload + 32, 0, 8);
        memset(Prefs.WIFI_RECOVERY_NONCE, 0, 8);
    }
    memset(msg.payload + 40, 0, 20); // zero rest
    // Find server MAC if not provided
    uint8_t destMAC[6] = {0};
    if (serverMAC) {
        memcpy(destMAC, serverMAC, 6);
    } else {
        bool found = false;
        for (int16_t i = 0; i < Sensors.getNumDevices(); ++i) {
            DevType* dev = Sensors.getDeviceByIndex(i);
            if (dev && dev->IsSet && dev->devType >= 100) {
                for (int j = 0; j < 6; ++j)
                    destMAC[5-j] = (dev->MAC >> (8*j)) & 0xFF;
                found = true;
                break;
            }
        }
        if (!found) {
            storeError("No server found for WiFi PW request");
            memset(Prefs.TEMP_AES, 0, 32);
            Prefs.TEMP_AES_TIME = 0;
            memset(Prefs.TEMP_AES_MAC, 0, 6);
            return false;
        }
    }
    memcpy(msg.targetMAC, destMAC, 6);
    memcpy(Prefs.TEMP_AES_MAC, destMAC, 6); // Store expected server MAC
    bool ok = sendESPNOW(msg);
    if (!ok) {
        storeError("Failed to send WiFi PW request");
        memset(Prefs.TEMP_AES, 0, 32);
        Prefs.TEMP_AES_TIME = 0;
        memset(Prefs.TEMP_AES_MAC, 0, 6);
    }
    // Log attempt
    #ifdef _DEBUG
    Serial.printf("[ESPNow] Sent WiFi PW request to %02X:%02X:%02X:%02X:%02X:%02X\n", destMAC[0], destMAC[1], destMAC[2], destMAC[3], destMAC[4], destMAC[5]);
    #endif
    return ok;
}
  
