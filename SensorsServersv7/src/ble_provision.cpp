#include "globals.hpp"
#include "ble_provision.hpp"

#if defined(ESP32) && _USE_BLE_PROV

#include "server.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"

#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_bt.h>
#include <network_provisioning/manager.h>

#if !((defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)) && __has_include("esp_bt.h"))
#error "_USE_BLE_PROV requires Bluetooth (NimBLE) in the Arduino-ESP32 build"
#endif
#if !defined(CONFIG_NIMBLE_ENABLED)
#error "_USE_BLE_PROV requires the NimBLE host; classic ESP32 envs need custom_sdkconfig = ${nimble_esp32.custom_sdkconfig}"
#endif

namespace {

constexpr char kPopPrefix[] = "sn";  // Espressif app PoP; full string = sn + 6 hex MAC bytes

static bool s_started = false;
static bool s_stopping = false;
static bool s_stopped = false;
static bool s_eventHooked = false;
static volatile bool s_stopRequested = false;
static volatile bool s_credsPending = false;
static char s_pendingSsid[33] = {0};
static char s_pendingPass[65] = {0};
static char s_serviceName[32] = {0};
static char s_pop[16] = {0};

static void buildPopAndName() {
  const uint8_t b3 = getPROCIDByte(Prefs.PROCID, 3);
  const uint8_t b4 = getPROCIDByte(Prefs.PROCID, 4);
  const uint8_t b5 = getPROCIDByte(Prefs.PROCID, 5);
  snprintf(s_serviceName, sizeof(s_serviceName), "PROV_%02X%02X%02X", b3, b4, b5);
  snprintf(s_pop, sizeof(s_pop), "%s%02X%02X%02X", kPopPrefix, b3, b4, b5);
}

static bool pastDeadline() {
  return millis() >= BLE_PROV_MAX_MS;
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

static void releaseBtMemory() {
  // After FREE_BTDM/FREE_BLE scheme handler, permanently reclaim controller BSS for the heap.
  // Failure is fine if already released or BT never started.
#if CONFIG_IDF_TARGET_ESP32
  esp_bt_mem_release(ESP_BT_MODE_BTDM);
#else
  esp_bt_mem_release(ESP_BT_MODE_BLE);
#endif
}

static void stopProvisioningInternal(const char* reason) {
  if (s_stopped || s_stopping) return;
  if (!s_started) {
    s_stopped = true;
    s_stopRequested = false;
    return;
  }
  s_stopping = true;
  SerialPrint(String("BLE prov: tearing down (") + reason + ")", true);

  WiFiProv.endProvision();
  // Ensure manager is fully gone even if auto-stop already ran.
  network_prov_mgr_deinit();
  releaseBtMemory();

  s_started = false;
  s_stopping = false;
  s_stopped = true;
  s_stopRequested = false;
  SerialPrint("BLE prov: stopped; BT memory released", true);
}

static void onProvEvent(arduino_event_t* sys_event) {
  if (sys_event == nullptr) return;

  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      SerialPrint("BLE prov: advertising — use Espressif app (BLE)", true);
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
      SerialPrint("BLE prov: Wi-Fi connect failed (BLE stays up until timeout)", true);
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      // Credentials worked — drop BLE ASAP so STA / ESP-NOW are undisturbed.
      SerialPrint("BLE prov: success — requesting immediate teardown", true);
      s_stopRequested = true;
      break;

    case ARDUINO_EVENT_PROV_END:
      s_started = false;
      s_stopped = true;
      releaseBtMemory();
      SerialPrint("BLE prov: provisioning ended", true);
      break;

    default:
      break;
  }
}

}  // namespace

void bleProvisionBeginIfNeeded() {
  if (s_started || s_stopped) return;
  if (pastDeadline()) {
    s_stopped = true;
    SerialPrint("BLE prov: skipped (past 30-minute boot window)", true);
    releaseBtMemory();
    return;
  }
  if (Prefs.HAVECREDENTIALS && Prefs.WIFISSID[0] != '\0') {
    s_stopped = true;
    SerialPrint("BLE prov: skipped (Wi-Fi credentials already present)", true);
    // Never initialize BT this boot — reclaim reserved BT controller memory for heap/Wi-Fi.
    releaseBtMemory();
    return;
  }

  buildPopAndName();

  if (!s_eventHooked) {
    WiFi.onEvent(onProvEvent);
    s_eventHooked = true;
  }

  // Standard Espressif BLE provisioning UUID (matches WiFiProv examples / phone apps).
  uint8_t uuid[16] = {
      0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
      0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};

  // FREE_BTDM (ESP32) / FREE_BLE (S3+) reclaim stack RAM when provisioning stops.
#if CONFIG_IDF_TARGET_ESP32
  const scheme_handler_t handler = NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM;
#else
  const scheme_handler_t handler = NETWORK_PROV_SCHEME_HANDLER_FREE_BLE;
#endif

  SerialPrint(String("BLE prov: starting service=") + s_serviceName + " pop=" + s_pop, true);
  tftPrint(String("BLE: ") + s_serviceName + " PoP " + s_pop, true);

  WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE,
      handler,
      NETWORK_PROV_SECURITY_1,
      s_pop,
      s_serviceName,
      nullptr,
      uuid,
      true  // reset IDF provisioned flag so SoftAP-only devices still show BLE portal
  );

  // Keep BLE alive until we explicitly tear down (success or 30-minute deadline).
  WiFiProv.disableAutoStop(1000);

  WiFiProv.printQR(s_serviceName, s_pop, "ble");

  s_started = true;
  s_stopped = false;
}

void bleProvisionService() {
  if (s_credsPending) {
    applyPendingCredentials();
  }

  if (s_stopRequested) {
    stopProvisioningInternal("success/request");
    return;
  }

  if (s_started && !s_stopped && pastDeadline()) {
    stopProvisioningInternal("30-minute boot deadline");
  }
}

void bleProvisionStop() {
  s_stopRequested = true;
  stopProvisioningInternal("explicit stop");
}

bool bleProvisionIsActive() {
  return s_started && !s_stopped;
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
