#include "AddESPNOW.hpp"

#if defined(_USE32)

static bool addPeer(const uint8_t* mac) {
  esp_now_peer_info_t p = {}; memcpy(p.peer_addr, mac, 6); p.channel = 0; p.encrypt = false; return esp_now_add_peer(&p)==ESP_OK;
}
static bool delPeer(const uint8_t* mac) { return esp_now_del_peer(mac)==ESP_OK; }

bool initESPNOW() {
  if (esp_now_init() != ESP_OK) { storeError("ESPNow: Failed to initialize", ERROR_ESPNOW_SEND); return false; }
  uint8_t bcast[6]; memset(bcast,0xFF,6); addPeer(bcast);
  esp_now_register_send_cb(OnESPNOWDataSent);
  // Adapter for IDF v4 callback signature differences
  esp_now_register_recv_cb([](const esp_now_recv_info* info, const uint8_t* incomingData, int len){
    const uint8_t* mac = info ? info->src_addr : nullptr;
    OnDataRecv(mac, incomingData, len);
  });
  return true;
}

void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  I.lastESPNOW = I.currentTime;
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len < (int)sizeof(ESPNOW_type)) return;
  ESPNOW_type msg; memcpy(&msg, incomingData, sizeof(msg));
  I.lastESPNOW = I.currentTime;
  if (msg.msgType == ESPNOW_MSG_BROADCAST_ALIVE && msg.senderType >= 100) {
    // Register server device; treat as a server (devType>=100)
    int16_t devIdx = DeviceStore.addDevice(msg.senderMAC, msg.senderIP, "Server", 60, 0);
    if (devIdx >= 0) {
      DevType* d = DeviceStore.getDeviceByDevIndex(devIdx);
      if (d) d->devType = msg.senderType;
    }
    I.LAST_ESPNOW_SERVER_MAC = msg.senderMAC;
    I.LAST_ESPNOW_SERVER_IP = msg.senderIP;
    I.LAST_ESPNOW_SERVER_TIME = I.currentTime;
  }
}

#endif

