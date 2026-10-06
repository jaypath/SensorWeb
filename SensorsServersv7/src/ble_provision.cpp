#include "globals.hpp"
#include "ble_provision.hpp"

#if defined(ESP32) && _USE_BLE_PROV

#include "server.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"

#include <WiFi.h>
#include <WiFiProv.h>
#include <string.h>
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

#if !defined(CONFIG_NIMBLE_ENABLED)
#error "_USE_BLE_PROV requires the NimBLE host; classic ESP32 envs need custom_sdkconfig = ${nimble_esp32.custom_sdkconfig}"
#endif

namespace {

static bool s_mgrReady = false;
static bool s_advertising = false;
static bool s_eventHooked = false;
static volatile bool s_credsPending = false;
static volatile bool s_restoreAp = false;
static char s_pendingSsid[33] = {0};
static char s_pendingPass[65] = {0};
static char s_serviceName[32] = {0};
static char s_pop[32] = {0};

static void onProvEvent(arduino_event_t* sys_event);

// ESP.getEfuseMac() is little-endian: byte 0 is the last MAC octet.
// Display order AA:BB:CC:DD:EE:FF ends in DDEEFF = bytes 2,1,0.
static void buildPopAndName() {
  const uint8_t last = getPROCIDByte(Prefs.PROCID, 0);
  const uint8_t mid = getPROCIDByte(Prefs.PROCID, 1);
  const uint8_t first = getPROCIDByte(Prefs.PROCID, 2);
  snprintf(s_serviceName, sizeof(s_serviceName), "arborysnet-%02X%02X%02X", first, mid, last);
  snprintf(s_pop, sizeof(s_pop), "%s", AP_STATION_PASSWORD);
}

static void applyPendingCredentials() {
  if (!s_credsPending) return;
  s_credsPending = false;

  if (s_pendingSsid[0] == '\0') return;

  snprintf((char*)Prefs.WIFISSID, sizeof(Prefs.WIFISSID), "%s", s_pendingSsid);
  snprintf((char*)Prefs.WIFIPWD, sizeof(Prefs.WIFIPWD), "%s", s_pendingPass);
  Prefs.HAVECREDENTIALS = true;
  Prefs.isUpToDate = false;

  SerialPrint(String("BLE prov: saved SSID \"") + s_pendingSsid + "\" to Prefs", true);

  BootSecure bootSecure;
  const int8_t ret = bootSecure.setPrefs();
  if (ret < 0) {
    SerialPrint("BLE prov: Prefs NVS save failed (" + String(ret) + ")", true);
  }
}

static void restoreSoftApIfDown() {
  if (wifiReadyForNetwork()) return;
  if (!softApRunning()) {
    SerialPrint("BLE prov: soft AP down; restoring AP", true);
    enterAPStationMode();
  }
}

static bool ensureManager() {
  if (s_mgrReady) return true;

  // Copy the BLE scheme and keep AP+STA. The stock scheme forces STA, which
  // would drop the recovery soft AP the moment advertising starts.
  network_prov_mgr_config_t config;
  memset(&config, 0, sizeof(config));
  config.scheme = network_prov_scheme_ble;
  config.scheme.wifi_mode = WIFI_MODE_APSTA;

  WiFi.STA.begin(false);
  if (network_prov_mgr_init(config) != ESP_OK) {
    SerialPrint("BLE prov: manager init failed", true);
    return false;
  }
  s_mgrReady = true;
  return true;
}

static void startAdvertising() {
  if (s_advertising) return;
  if (!softApRunning()) return;
  if (!ensureManager()) return;

  buildPopAndName();

  if (!s_eventHooked) {
    WiFi.onEvent(onProvEvent);
    s_eventHooked = true;
  }

  // disable_auto_stop must run before start, or a successful session tears BLE down.
  if (network_prov_mgr_disable_auto_stop(1000) != ESP_OK) {
    SerialPrint("BLE prov: disable_auto_stop failed", true);
  }

  // Standard Espressif BLE provisioning UUID (matches the phone app).
  uint8_t uuid[16] = {
      0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
      0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};
  network_prov_scheme_ble_set_service_uuid(uuid);

  // Do not call network_prov_mgr_reset_wifi_provisioning(): that restores
  // Wi-Fi defaults and clears the soft-AP password.
  const esp_err_t err = network_prov_mgr_start_provisioning(
      NETWORK_PROV_SECURITY_1, s_pop, s_serviceName, nullptr);
  if (err == ESP_ERR_INVALID_STATE) {
    s_advertising = true;
    return;
  }
  if (err != ESP_OK) {
    SerialPrint("BLE prov: start failed (" + String((int)err) + "); will retry", true);
    return;
  }

  s_advertising = true;
  SerialPrint(String("BLE prov: advertising ") + s_serviceName + " (PoP = AP password)", true);
  tftPrint(String("BLE: ") + s_serviceName, true);
  WiFiProv.printQR(s_serviceName, s_pop, "ble");

  // start_provisioning can restart Wi-Fi and drop the soft AP. Put it back.
  restoreSoftApIfDown();
}

static void stopAdvertising(const char* reason) {
  if (!s_advertising && !s_mgrReady) return;
  if (!s_advertising) return;

  SerialPrint(String("BLE prov: stopping (") + reason + ")", true);
  s_advertising = false;
  network_prov_mgr_stop_provisioning();
}

static void onProvEvent(arduino_event_t* sys_event) {
  if (sys_event == nullptr) return;

  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      SerialPrint("BLE prov: session started", true);
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV: {
      const char* ssid = (const char*)sys_event->event_info.prov_cred_recv.ssid;
      const char* pass = (const char*)sys_event->event_info.prov_cred_recv.password;
      memset(s_pendingSsid, 0, sizeof(s_pendingSsid));
      memset(s_pendingPass, 0, sizeof(s_pendingPass));
      if (ssid) {
        strncpy(s_pendingSsid, ssid, sizeof(s_pendingSsid) - 1);
      }
      if (pass) {
        strncpy(s_pendingPass, pass, sizeof(s_pendingPass) - 1);
      }
      s_credsPending = true;
      SerialPrint(String("BLE prov: credentials received for SSID \"") + s_pendingSsid + "\"", true);
      break;
    }

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      SerialPrint("BLE prov: Wi-Fi connect failed; BLE stays up", true);
      s_restoreAp = true;
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      SerialPrint("BLE prov: credentials accepted", true);
      break;

    case ARDUINO_EVENT_PROV_END:
      s_advertising = false;
      SerialPrint("BLE prov: advertising ended", true);
      break;

    default:
      break;
  }
}

static void syncToSoftAp() {
  if (softApRunning()) {
    if (!s_advertising) startAdvertising();
  } else if (s_advertising) {
    stopAdvertising("STA recovered");
  }
}

}  // namespace

void bleProvisionBeginIfNeeded() {
  buildPopAndName();
  syncToSoftAp();
}

void bleProvisionService() {
  if (s_credsPending) {
    applyPendingCredentials();
  }
  if (s_restoreAp) {
    s_restoreAp = false;
    restoreSoftApIfDown();
  }
  syncToSoftAp();
}

void bleProvisionStop() {
  stopAdvertising("explicit stop");
}

bool bleProvisionIsActive() {
  return s_advertising;
}

const char* bleProvisionPop() {
  if (s_pop[0] == '\0') buildPopAndName();
  return s_pop;
}

const char* bleProvisionServiceName() {
  if (s_serviceName[0] == '\0') buildPopAndName();
  return s_serviceName;
}

#endif  // ESP32 && _USE_BLE_PROV
