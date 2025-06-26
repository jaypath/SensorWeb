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

// Helper function to check if message is sensitive
bool isSensitive(uint8_t msgType) {
    return (msgType & 0x01) != 0; // Bit 0 indicates sensitive info
}

// Helper function to check if message requires response
bool requiresResponse(uint8_t msgType) {
    return (msgType & 0x02) != 0; // Bit 1 indicates response required
}

// Helper function to check if response must be direct message
bool requiresDirectResponse(uint8_t msgType) {
    return (msgType & 0x04) != 0; // Bit 2 indicates DM response
}

/*
Do not transmit wifi info over espnow. It is too  unreliable.
// Helper function to check if response must include WiFi info
bool requiresWiFiInfo(uint8_t msgType) {
    return (msgType & 0x08) != 0; // Bit 3 indicates WiFi info required
}
*/


bool initESPNOW() {
    if (esp_now_init() != ESP_OK) {
        storeError("ESPNow: Failed to initialize");
        return false;
    }

    esp_now_register_send_cb(esp_now_send_cb_t(OnESPNOWDataSent));
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (addESPNOWPeer(broadcastAddress, false) != ESP_OK) {
        storeError("ESPNow: Failed to add broadcast peer");
        return false;
    }

    return true;
}

esp_err_t addESPNOWPeer(byte* macad, bool doEncrypt) {
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, macad, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (doEncrypt && Prefs.LMK_isSet) { //if LMK is not null
        peerInfo.channel = 3;  
        memcpy(peerInfo.lmk, Prefs.LMK, 16);
        peerInfo.encrypt = true;
    } else {
        peerInfo.channel = 0;  
        peerInfo.encrypt = false;        
    }
    
    return esp_now_add_peer(&peerInfo);
}

esp_err_t delESPNOWPeer(byte* macad) {
    return esp_now_del_peer(macad);
}

void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Prefs.lastESPNOW = I.currentTime;    
        return;
    } else {
        storeError("ESPNow: Failed to send data");
    } 
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (len < sizeof(ESPNOW_type)) {
        storeError("ESPNow: Received message too short");
        return;
    }
    
    ESPNOW_type t;
    memcpy(&t, incomingData, sizeof(t));
    
    // Received a message ...
    Prefs.lastESPNOW = I.currentTime;    

    // For all messages, register sender...
    
    /*RMB to LMB bit on msgType: 
    0: message is sensitive, 0 = no
    1: message requires response, 0 = no
    2: response must be DM, 0 = no/broadcast response
    3: response must include Wifi info (SSID And PWD)
    4: payload matters
 
    // What type of message is this? requires response? sensitive?
    
    if (isSensitive(t.msgType)) { 
        // Ping, no sensitive info requested... broadcast insecure WIFI_INFO
        WiFi_type w;
        memcpy(&w, &WIFI_INFO, sizeof(w));
        w.HAVECREDENTIALS = false;
        memset(w.PWD, 0, sizeof(w.PWD)); // Clear password
        memset(w.SSID, 0, sizeof(w.SSID)); // Clear SSID
        w.statusCode = 0; // No sensitive info and no response required

        sendESPNOW(nullptr, &w); // Broadcast my public info
    } else {
        // Add that unit as a peer
        if (addESPNOWPeer(t.senderID, true) != ESP_OK) {
            storeError("ESPNow: Failed to add a peer");
            return;
        } 

        // Now send that peer our wifi creds!
        sendESPNOW(t.senderID, &WIFI_INFO);

        // Now drop that peer!
        delESPNOWPeer(t.senderID);
    }
}

bool sendESPNOW(byte* MAC, struct WiFi_type *w_info) {
    // Use nullptr for mac then broadcast the message
    
    struct WiFi_type w;
    esp_err_t result;
    
    if (w_info == nullptr) {
        // Create a default WiFi_type structure
        memset(&w, 0, sizeof(w));
        w.HAVECREDENTIALS = false;
        w.statusCode = 0; // No sensitive info
    } else {
        memcpy(&w, w_info, sizeof(w));
        w.statusCode = 1; // Includes sensitive info unless otherwise specified
    }

    if (MAC == nullptr) {
        // This is a special case, where I will broadcast without login credentials
        uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        
        // Clear PWD and SSID for public broadcast
        memset(w.SSID, 0, sizeof(w.SSID));
        memset(w.PWD, 0, sizeof(w.PWD));
        w.HAVECREDENTIALS = false;
        w.statusCode = 0; // No sensitive info
        
        result = esp_now_send(broadcastMAC, (uint8_t *) &w, sizeof(w));
    } else {
        result = esp_now_send(MAC, (uint8_t *) &w, sizeof(w));
    }
    
    if (result != ESP_OK) {
        storeError("ESPNow: Failed to send data");
        return false;
    }

    return true;
}

// Function to broadcast server presence periodically
bool broadcastServerPresence() {
    WiFi_type w;
    memset(&w, 0, sizeof(w));
    
    // Set server info
    w.HAVECREDENTIALS = false;
    w.statusCode = 0; // No sensitive info
    
    // Copy server IP
    IPAddress serverIP = WiFi.localIP();
    w.MYIP[0] = serverIP[0];
    w.MYIP[1] = serverIP[1];
    w.MYIP[2] = serverIP[2];
    w.MYIP[3] = serverIP[3];
    
    // Copy server MAC
    uint8_t serverMAC[6];
    WiFi.macAddress(serverMAC);
    memcpy(w.MAC, serverMAC, 6);
    
    // Clear credentials for public broadcast
    memset(w.SSID, 0, sizeof(w.SSID));
    memset(w.PWD, 0, sizeof(w.PWD));
    
    return sendESPNOW(nullptr, &w);
}
  
