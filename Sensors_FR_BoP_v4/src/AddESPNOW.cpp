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

// --- Utility: Check if LMK is configured ---
bool isLMKConfigured() {
    for (int i = 0; i < 16; i++) {
        if (Prefs.KEYS.ESPNOW_KEY[i] != 0) {
            return true;
        }
    }
    return false;
}

// --- Utility: Validate LMK key format ---
bool isValidLMKKey() {
    if (!isLMKConfigured()) {
        return false;
    }
   

    //key can contain any characters, so we don't need to check for printable ASCII characters
    if (false) {
        // Check if key contains only printable ASCII characters
        for (int i = 0; i < 16; i++) {
            if (Prefs.KEYS.ESPNOW_KEY[i] != 0 && (Prefs.KEYS.ESPNOW_KEY[i] < 32 || Prefs.KEYS.ESPNOW_KEY[i] > 126)) {
                return false;
            }
        }
    }

    return true;
}


// --- Utility: Get ESPNow encryption status ---
String getESPNOWEncryptionStatus() {
    if (!isLMKConfigured()) {
        return "LMK_KEY not configured - all communications unencrypted";
    }
    
    if (!isValidLMKKey()) {
        return "LMK_KEY invalid format - all communications unencrypted";
    }
    
    return "LMK_KEY configured - private messages encrypted on channel 3";
}

// --- ESPNow Initialization ---
bool initESPNOW() {
    if (esp_now_init() != ESP_OK) {
        storeError("ESPNow: Failed to initialize");
        return false;
    }
    uint8_t MACB[6] = {0};
    memset(MACB,255,6);
    
    // Register broadcast address (no encryption, I'll handle that separately)
    addESPNOWPeer(MACB);

    esp_now_register_send_cb(OnESPNOWDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    return true;
}

// --- Peer Management ---
esp_err_t addESPNOWPeer(uint8_t* macad) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macad, 6);
    
//all ESPNOW messages are unencrypted, and the channel is set to 0, which means auto-channel selection
//encryption is handled separately, and is not required for all messages
//the LMK is used to encrypt private messages, and the PMK is used to encrypt broadcast messages and provisioning LMK

    peerInfo.channel = 0;  // use the wifi channel, and 0 defines auto-channel selection
    peerInfo.encrypt = false;
            
    
    esp_err_t result = esp_now_add_peer(&peerInfo);
    
    if (result != ESP_OK) {
        SerialPrint("Failed to add ESPNow peer: " + MACToString(macad) + " Error: " + String(result), true);
    }
    
    return result;
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

    if (msg.msgType>0) {
        if (isValidLMKKey()) {
            // Encrypt the message
            encryptESPNOWMessage(msg);
        } else {
            storeError("ESPNow: LMK not valid, message cannot be encrypted");
            return false;
        }
    }

    for (int i = 0; i < 6; ++i) if (msg.targetMAC[i] != 0xFF) isBroadcast = false;
    
    if (!isBroadcast) {
        // Determine encryption based on message type and LMK availability
        
        // Add peer 
        addESPNOWPeer(msg.targetMAC);
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
        I.lastESPNOW_TIME = I.currentTime;
        I.lastESPNOW_STATE = 1;
        I.ESPNOW_SENDS++;
        I.isUpToDate = false;
    } else {
        I.lastESPNOW_STATE = -1;
        I.lastESPNOW_TIME = I.currentTime;
        storeError("ESPNow: Failed to send data");
    }
}

// --- ESPNow Receive Callback ---
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    uint8_t mac[6] = {0};
    memcpy(mac, recv_info->src_addr, 6);
    if (len < static_cast<int>(sizeof(ESPNOW_type))) {
        storeError("ESPNow: Received message too short");
        I.lastESPNOW_STATE = -2;
        I.lastESPNOW_TIME = I.currentTime;
        return;
    }
    ESPNOW_type msg;
    memcpy(&msg, incomingData, sizeof(msg));
    
    I.LAST_ESPNOW_SERVER_MAC = msg.senderMAC;
    I.LAST_ESPNOW_SERVER_IP = msg.senderIP;
    I.LAST_ESPNOW_SERVER_TIME = I.currentTime;
    I.isUpToDate = false;
    I.lastESPNOW_STATE = 2;
    I.ESPNOW_RECEIVES++;
    I.lastESPNOW_TIME = I.currentTime;


    if (msg.msgType>0) {
        decryptESPNOWMessage(msg);
    }
    
    if (msg.msgType== ESPNOW_MSG_WIFI_PW_REQUEST) {
        // Received request for WiFi password (payload[0..15] = key, payload[16..31] = IV, payload[32..39] = nonce)
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
        
        // Echo nonce back from request to response for replay attack prevention
        memcpy(resp.payload + 32, msg.payload + 32, 8);
        
        if (!sendESPNOW(resp)) {
            storeError("ESPNow: Failed to send WiFi password response");
            return;
        }
    }
    else if (msg.msgType== ESPNOW_MSG_WIFI_PW_RESPONSE) {
        // Check TEMP_AES, TEMP_AES_TIME, TEMP_AES_MAC, and NONCE
        uint32_t nowt = (uint32_t)time(nullptr);
        bool valid = false;
        if (I.TEMP_AES_TIME != 0 && (nowt - I.TEMP_AES_TIME) <= 300) {
            for (int i = 0; i < 32; ++i) if (I.TEMP_AES[i] != 0) valid = true;
        }
        // Check MAC match
        bool macMatch = true;
        if (I.TEMP_AES_MAC != msg.senderMAC) macMatch = false;
        
        decryptESPNOWMessage(msg); //decrypt the message using the ESPNOW_KEY
        
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
            storeError("WiFi PW response: No valid TEMP_AES, expired, MAC mismatch, or nonce mismatch");
            return;
        }
        // Decrypt using the TEMP_AES and IV
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
            storeError("WiFi PW response: Decryption failed");
            return;
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
    }
    else if (msg.msgType== ESPNOW_MSG_WIFI_KEY_REQUIRED) {
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
            memset(I.WIFI_RECOVERY_NONCE, 0, 8);
            I.isUpToDate = false;
            storeError("WiFi KEY REQUIRED: MAC mismatch or expired");
            return;
        }
        // Zero out old TEMP_AES and TEMP_AES_TIME
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
        I.isUpToDate = false;
        // Send a new type 2 message to the same MAC
        uint8_t mac[6] = {0};
        uint64ToMAC(I.TEMP_AES_MAC, mac);
        requestWiFiPassword(mac, nullptr);
    }
    else if (msg.msgType== ESPNOW_MSG_TERMINATE) {
        // Terminate communication, delete peer if needed
        delESPNOWPeer(msg.senderMAC);
        // Zero TEMP_AES and TEMP_AES_TIME for security
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        I.TEMP_AES_MAC = 0;
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
        I.isUpToDate = false;
        
    }
    return ;
}

//at present I do not offer a type 0 message, so all messages are encrypted
// --- Broadcast Server Presence (Type 1) ---
bool broadcastServerPresence() {

    ESPNOW_type msg = {};
    msg.senderMAC = Prefs.PROCID;
    msg.senderIP = Prefs.MYIP;
    msg.senderType = MYTYPE;

    memset(msg.targetMAC, 0xFF, sizeof(msg.targetMAC));
    msg.msgType = ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED;
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
  


bool encryptESPNOWMessage(ESPNOW_type& msg) {
    // Encrypt the message using stored ESPNOW_KEY
    //user must know that the message must be 64 bytes or less (so that the encrypted message is 64 bytes + 16 bytes for the IV)
    //returns 1 if successful, 0 if encryption failed, -1 if payload is too large

    uint8_t encrypted[80] = {0};
    uint16_t outlen = 0;


    int8_t ret = BootSecure::encrypt((const unsigned char*)msg.payload, 64, (char*)Prefs.KEYS.ESPNOW_KEY, encrypted, &outlen,16);
    if (ret == -2) {
        storeError("ESPNow: Invalid key");
        return false;
    }
    if (ret == -3) {
        storeError("ESPNow: Failed to encrypt message");
        return false;
    }
    memcpy(msg.payload, encrypted, (outlen < 80) ? outlen : 80);

    return true;
}

bool decryptESPNOWMessage(ESPNOW_type& msg) {
    // Decrypt the message using stored ESPNOW_KEY
    //returns 1 if successful, 0 if decryption failed, -1 if payload is too large
    uint8_t decrypted[80] = {0};
    uint16_t outlen = 0;
    
    int8_t ret = BootSecure::decrypt((unsigned char*)msg.payload, (char*)Prefs.KEYS.ESPNOW_KEY, decrypted, 80,16);
    if (ret == -2) {
        storeError("ESPNow: Invalid key");
        return false;
    }
    if (ret == -3) {
        storeError("ESPNow: Failed to decrypt message");
        return false;
    }
    uint8_t tempmsg[80] = {0};
    memcpy(tempmsg, decrypted, (outlen < 80) ? outlen : 80);
    memcpy(msg.payload, tempmsg, 80);
    return true;

}