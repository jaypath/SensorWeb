#ifndef ADDESPNOW_HPP
    #define ADDESPNOW_HPP

    //#define _USEENCRYPTION


    #include <esp_now.h>
    #include "globals.hpp"
    #include "utility.hpp"
    #include "server.hpp"
    #include "BootSecure.hpp"


extern STRUCT_PrefsH Prefs;
extern Screen I;
extern char PMK_KEY_STR[17];
extern DeviceType Devices;

struct ESPNOW_type {
    byte senderIP[4]; //4 bytes
    byte senderID[6]; //MAC address
    byte senderType; //what is my device type? Note that anything >=100 is a server type and anything less is a sensor type
    byte targetID[6]; //MAC address of target, all zeros for anyone    
    byte SSID[33];
    byte PWD[65];
    uint8_t msgType; /*RMB to LMB bit: 
    0: message is sensitive, 0 = no
    1: message requires response, 0 = no
    2: response must be DM, 0 = no/broadcast response
    3: response must include Wifi info (SSID And PWD)
    4: payload matters
    */
    uint8_t payload[50]; //for future use, or for any additional payload. total size must be <200
  };

  ESPNOW_type ESPNOWmsg;

// Helper functions for message type checking
bool isSensitive(uint8_t msgType);
bool requiresResponse(uint8_t msgType);
bool requiresDirectResponse(uint8_t msgType);
bool requiresWiFiInfo(uint8_t msgType);

// Core ESPNow functions
bool initESPNOW();
esp_err_t addESPNOWPeer(byte* macad,bool doEncrypt);
esp_err_t delESPNOWPeer(byte* macad);
bool sendESPNOW(byte* MAC = nullptr, struct WiFi_type *w = nullptr);
bool broadcastServerPresence();

// Callback functions
void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);


#endif
