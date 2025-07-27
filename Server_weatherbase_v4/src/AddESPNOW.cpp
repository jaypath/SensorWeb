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
#include "utility.hpp"
#ifdef _USETFT
#include "graphics.hpp"
#endif


char PMK_KEY_STR[17] = "KiKa.yaas1anni!~"; //note this is not stored in prefs


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
    uint8_t MACB[6] = {0};
    memset(MACB,255,6);
    addESPNOWPeer(MACB, false); //register the broadcast address

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
esp_err_t delESPNOWPeer(uint64_t macad) {
    uint8_t mac[6] = {0};
    uint64ToMAC(macad, mac);
    return esp_now_del_peer(mac);
}


// --- Send ESPNow Message ---
bool sendESPNOW(ESPNOW_type& msg) {
    bool isBroadcast = true;

    for (int i = 0; i < 6; ++i) if (msg.targetMAC[i] != 0xFF) isBroadcast = false;
    if (!isBroadcast) {
        addESPNOWPeer(msg.targetMAC, false);
    }
    esp_err_t result = esp_now_send(msg.targetMAC, (const uint8_t*) &msg, sizeof(ESPNOW_type));

    if (!isBroadcast) {
        delESPNOWPeer(msg.targetMAC);
    }
    if (result != ESP_OK) {
        SerialPrint((String) "ESPNow tried sending to " + MACToString(msg.targetMAC) + " and failed with code: " + result,true);
        storeError(ESPNowError(result).c_str(),ERROR_ESPNOW_SEND);
        return false;
    }

    I.lastESPNOW = I.currentTime;
    SerialPrint((String) "ESPNow sent to " + MACToString(msg.targetMAC) + " OK: " + result,true);

    return true;
}

String ESPNowError(esp_err_t result) {
    if (result == ESP_OK) return "OK";
    
    switch (result) {
        case ESP_ERR_NO_MEM: return "ESPNow out of memory";
        case ESP_ERR_INVALID_ARG: return "ESPNow: Invalid argument";
        case ESP_ERR_INVALID_STATE: return "ESPNow: Invalid state";
        case ESP_ERR_INVALID_SIZE: return "ESPNow: Invalid size";
        case ESP_ERR_NOT_FOUND: return "ESPNow: Resource not found";
        case ESP_ERR_TIMEOUT: return "ESPNow: timeout";
        case ESP_ERR_INVALID_RESPONSE: return "ESPNow: invalid response";
        case ESP_ERR_INVALID_CRC: return "ESPNow: invalid CRC";
        case ESP_ERR_INVALID_MAC: return "ESPNow: invalid MAC";
        case ESP_ERR_ESPNOW_NOT_FOUND: return "ESPNow: MAC recipient not found";
        default: return "ESPNow: Error undefined";
        break;
    }

}

// --- ESPNow Send Callback ---
void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        I.lastESPNOW = I.currentTime;
        I.isUpToDate = false;
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
    I.lastESPNOW = I.currentTime;

    switch (msg.msgType) {
        case ESPNOW_MSG_BROADCAST_ALIVE:
            // received a server message; update presence table will occur in main loop
            //no need to do anything here except register the server in I
            I.LAST_ESPNOW_SERVER_MAC = msg.senderMAC;
            I.LAST_ESPNOW_SERVER_IP = msg.senderIP;
            I.LAST_ESPNOW_SERVER_TIME = I.currentTime;
            I.isUpToDate = false;
            break;
        case ESPNOW_MSG_SERVER_LIST:
            // Received server list; update local list
            // (User can implement server list update logic here)
            break;
        case ESPNOW_MSG_WIFI_PW_REQUEST: {
            // Received request for WiFi password (payload[0..15] = key, payload[16..31] = IV)
            ESPNOW_type resp = {};
            resp.senderMAC = Prefs.PROCID;
            resp.senderIP = Prefs.MYIP;
            resp.senderType = MYTYPE;
            uint64ToMAC(msg.senderMAC, resp.targetMAC);
            resp.msgType = ESPNOW_MSG_WIFI_PW_RESPONSE;
            // Encrypt password using provided key and IV
            uint8_t encrypted[48] = {0};
            uint16_t outlen = 0;
                         BootSecure::encryptWithIV((const unsigned char*)Prefs.WIFIPWD, 32, (char*)msg.payload, msg.payload+16, encrypted, &outlen);
            memcpy(resp.payload, encrypted, (outlen < 48) ? outlen : 48);
            sendESPNOW(resp);
            break;
        }
        case ESPNOW_MSG_WIFI_PW_RESPONSE: {
            // Check TEMP_AES, TEMP_AES_TIME, TEMP_AES_MAC, and NONCE
            uint32_t nowt = (uint32_t)time(nullptr);
            bool valid = false;
            if (I.TEMP_AES_TIME != 0 && (nowt - I.TEMP_AES_TIME) <= 300) {
                for (int i = 0; i < 32; ++i) if (I.TEMP_AES[i] != 0) valid = true;
            }
            // Check MAC match
            bool macMatch = true;
            if (I.TEMP_AES_MAC != msg.senderMAC) macMatch = false;
            // Check nonce
            bool nonceMatch = true;
            for (int i = 0; i < 8; ++i) {
                if (I.WIFI_RECOVERY_NONCE[i] != msg.payload[32 + i]) nonceMatch = false;
            }
            if (!valid || !macMatch || !nonceMatch) {
                storeError("WiFi PW response: No valid TEMP_AES, expired, MAC mismatch, or nonce mismatch");
                memset(I.TEMP_AES, 0, 32);
                I.TEMP_AES_TIME = 0;
                I.TEMP_AES_MAC = 0;
                memset(I.WIFI_RECOVERY_NONCE, 0, 8);
                break;
            }
            // Decrypt
            char key[17]; memcpy(key, I.TEMP_AES, 16); key[16] = 0;
            uint8_t* iv = I.TEMP_AES + 16;
            uint8_t decrypted[65] = {0};
            int8_t decres = BootSecure::decryptWithIV((unsigned char*)msg.payload, key, iv, decrypted, 32); // 32 bytes encrypted
            if (decres != 0) {
                storeError("WiFi PW response: Decryption failed");
                memset(I.TEMP_AES, 0, 32);
                I.TEMP_AES_TIME = 0;
                I.TEMP_AES_MAC = 0;
                memset(I.WIFI_RECOVERY_NONCE, 0, 8);
                I.isUpToDate = false;
                break;
            }
            // Store WiFi password
            bool pwdMatch = (memcmp(Prefs.WIFIPWD, decrypted, 64) == 0);
            memcpy(Prefs.WIFIPWD, decrypted, 64);
            Prefs.WIFIPWD[64] = 0;
            Prefs.HAVECREDENTIALS = true;
            Prefs.isUpToDate = false;
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.TEMP_AES_MAC = 0;
            memset(I.WIFI_RECOVERY_NONCE, 0, 8);
            putWiFiCredentials();
            // If password matches, WiFi may be down; enter AP mode and show message
            if (pwdMatch) {
                #ifdef _USETFT
                screenWiFiDown();
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
            if (I.TEMP_AES_TIME != 0 && (nowt - I.TEMP_AES_TIME) <= 300) {
                valid = true;
            }
            bool macMatch = true;
            if (I.TEMP_AES_MAC != msg.senderMAC) macMatch = false;
            if (!valid || !macMatch) {
                storeError("WiFi KEY REQUIRED: MAC mismatch or expired");
                memset(I.TEMP_AES, 0, 32);
                I.TEMP_AES_TIME = 0;
                I.TEMP_AES_MAC = 0;
                I.isUpToDate = false;
                break;
            }
            // Zero out old TEMP_AES and TEMP_AES_TIME
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.isUpToDate = false;
            // Send a new type 2 message to the same MAC
            uint8_t mac[6] = {0};
            uint64ToMAC(I.TEMP_AES_MAC, mac);
            requestWiFiPassword(mac, nullptr);
            break;
        }
        case ESPNOW_MSG_TERMINATE:
            // Terminate communication, delete peer if needed
            delESPNOWPeer(msg.senderMAC);
            // Zero TEMP_AES and TEMP_AES_TIME for security
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.isUpToDate = false;

            I.TEMP_AES_MAC = 0;
            break;
        default:
            // Unknown type
            break;
    }
}

// --- Broadcast Server Presence (Type 0) ---
bool broadcastServerPresence() {

    ESPNOW_type msg = {};
    msg.senderMAC = Prefs.PROCID;
    msg.senderIP = Prefs.MYIP;
    msg.senderType = MYTYPE;

    memset(msg.targetMAC, 0xFF, sizeof(msg.targetMAC));
    msg.msgType = ESPNOW_MSG_BROADCAST_ALIVE;
    memset(msg.payload, 0, 60);

    return sendESPNOW(msg);
}

// --- Broadcast Server List (Type 1) ---
bool broadcastServerList(const uint8_t serverMACs[][6], const uint32_t* serverIPs, uint8_t count) {
    // If user provided a list, use it
    if (serverMACs && serverIPs && count > 0) {
        ESPNOW_type msg = {};
        msg.senderMAC = Prefs.PROCID;
        msg.senderIP = Prefs.MYIP;
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
        DevType* dev = Sensors.getDeviceByDevIndex(i);
        if (dev && dev->IsSet && dev->devType >= 100) {
            // Convert uint64_t MAC to 6 bytes
            for (int j = 0; j < 6; ++j)
                macs[found][5-j] = (dev->MAC >> (8*j)) & 0xFF;
            ips[found] = dev->IP;
            ++found;
        }
    }
    ESPNOW_type msg = {};
    msg.senderMAC = Prefs.PROCID;
    msg.senderIP = Prefs.MYIP;
    msg.senderType = MYTYPE;
    memset(msg.targetMAC, 0xFF, 6);
    msg.msgType = ESPNOW_MSG_SERVER_LIST;
    packServerList(msg.payload, macs, ips, found);
    return sendESPNOW(msg);
}

// --- Request WiFi Password via ESPNow (with nonce) ---
bool requestWiFiPassword(const uint8_t* serverMAC, const uint8_t* nonce) {
    // Generate random 16-byte key and 16-byte IV
    uint8_t key[16], iv[16];
    esp_fill_random(key, 16);
    esp_fill_random(iv, 16);
    // Store in Prefs
    memcpy(I.TEMP_AES, key, 16);
    memcpy(I.TEMP_AES + 16, iv, 16);
    I.TEMP_AES_TIME = (uint32_t)time(nullptr);
    // Prepare ESPNow message
    ESPNOW_type msg = {};
    msg.senderMAC = Prefs.PROCID;
    msg.senderIP = Prefs.MYIP;
    msg.senderType = MYTYPE;
    msg.msgType = ESPNOW_MSG_WIFI_PW_REQUEST;
    memcpy(msg.payload, I.TEMP_AES, 32); // key+IV
    // Add nonce if provided
    if (nonce) {
        memcpy(msg.payload + 32, nonce, 8);
        memcpy(I.WIFI_RECOVERY_NONCE, nonce, 8);
    } else {
        memset(msg.payload + 32, 0, 8);
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
    }
    memset(msg.payload + 40, 0, 20); // zero rest
    // Find server MAC if not provided
    uint8_t destMAC[6] = {0};
    if (serverMAC) {
        memcpy(destMAC, serverMAC, 6);
    } else {
        bool found = false;
        for (int16_t i = 0; i < Sensors.getNumDevices(); ++i) {
            DevType* dev = Sensors.getDeviceByDevIndex(i);
            if (dev && dev->IsSet && dev->devType >= 100) {
                for (int j = 0; j < 6; ++j)
                    destMAC[5-j] = (dev->MAC >> (8*j)) & 0xFF;
                found = true;
                break;
            }
        }
        if (!found) {
            storeError("No server found for WiFi PW request");
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.TEMP_AES_MAC = 0;
            return false;
        }
    }
    memcpy(msg.targetMAC, destMAC, 6);
    I.TEMP_AES_MAC = MACToUint64(destMAC); // Store expected server MAC

    bool ok = sendESPNOW(msg);
    if (!ok) {
        storeError("Failed to send WiFi PW request");
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        I.TEMP_AES_MAC = 0;

    }
    return ok;
}
  
