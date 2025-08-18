#ifndef ADDESPNOW_HPP
#define ADDESPNOW_HPP

#if defined(_USE32)

#include <esp_now.h>
#include <esp_wifi.h>
#include "globals.hpp"
#include "utility.hpp"
#include "Devices.hpp"

// --- ESPNow Message Types ---
constexpr uint8_t ESPNOW_MSG_BROADCAST_ALIVE = 0;

// --- ESPNow Message Struct ---
struct ESPNOW_type {
  uint64_t  senderMAC;
  uint32_t  senderIP;
  uint8_t   senderType;
  uint8_t   targetMAC[6];
  uint8_t   msgType;
  uint8_t   payload[60];
};

bool initESPNOW();
void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
// Use legacy signature for older SDK; we'll wrap to new in cpp if needed
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);

#endif

#endif

