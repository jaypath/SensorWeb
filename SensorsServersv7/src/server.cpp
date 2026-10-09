#include "globals.hpp"
#include "server.hpp"
#include "hardware_fault.hpp"
#include "Devices.hpp"
#include "SDCard.hpp"
#include "agg_links.hpp"
#if _HAS_LOCAL_SENSORS
#include "interrupt_triggers.hpp"
#include "actuators.hpp"
#endif
#ifdef _USENETWORKMONITOR
#if _USENETWORKMONITOR > 0
#include "NetworkMonitor.hpp"
#endif
#endif
#ifdef _USETFT
  #ifdef _ISCLOCK480X480
    #include "Clock480X480.hpp"
    extern LGFX tft;
  #else
    #include "graphics.hpp"
    extern LGFX tft;
    extern STRUCT_GRAPHICS GRAPHICS;
  #endif
#endif

#ifdef _USELEDMATRIX
#include "LEDMatrix.hpp"
#endif

#include "BootSecure.hpp"
#include "ble_provision.hpp"
#include <Preferences.h>
#include "AddESPNOW.hpp"
#include "firmwareUpdate.hpp"
#if _SUPABASE_RUNTIME
#include "supabase_prefs.hpp"
#include <SupabaseClient.hpp>
#endif

#include <ssl_client.h> // Ensure this is at the top of server.cpp


#include <string.h> // For memset
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#ifdef _USE32
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif


#ifdef _USEUDP
#include <lwip/sockets.h> // Essential for low-level IGMP control
#include <lwip/igmp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

extern WiFiUDP LAN_UDP;
//server
String WEBHTML;
byte CURRENT_DEVICEVIEWER_DEVINDEX = 0;  // Track current device index in device viewer
byte CURRENT_DEVICEVIEWER_DEVNUMBER = 0; // track the number of devices, which may not align with the index

extern STRUCT_PrefsH Prefs;
//extern bool requestWiFiPassword(const uint8_t* serverMAC);


namespace {
  constexpr uint32_t IGMP_REFRESH_INTERVAL_SEC = 60;
  constexpr uint32_t IGMP_STALE_UDP_SEC = 120;
  time_t s_lastIgmpRefreshTime = 0;
}

/**
 * @brief Refresh IGMP membership for the UDP multicast group.
 * Sends IGMPv2 Membership Reports so IGMP-snooping routers/APs keep forwarding 239.x traffic.
 */
void refreshIGMPMembership() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  struct netif *netif = netif_default;
  if (netif == NULL) {
    SerialPrint("IGMP: No default netif for membership report", true);
    return;
  }

  IPAddress multicastIP(_USEUDP_MULTICAST);
  ip4_addr_t groupaddr;
  IP4_ADDR(&groupaddr,
           multicastIP[0],
           multicastIP[1],
           multicastIP[2],
           multicastIP[3]);

  LOCK_TCPIP_CORE();

  if (isTimeValid((uint32_t)utcNow()) && isTimeValid(I.UDP_LAST_INCOMINGMSG_TIME)
      && utcNow() - I.UDP_LAST_INCOMINGMSG_TIME > IGMP_STALE_UDP_SEC) {
    igmp_leavegroup_netif(netif, &groupaddr);
    igmp_joingroup_netif(netif, &groupaddr);
    UNLOCK_TCPIP_CORE();
    SerialPrint("IGMP: Leave/rejoin recovery for " + multicastIP.toString(), true);
    return;
  }

  if (igmp_lookfor_group(netif, &groupaddr) == NULL) {
    err_t err = igmp_joingroup_netif(netif, &groupaddr);
    UNLOCK_TCPIP_CORE();
    if (err != ERR_OK) {
      SerialPrint("IGMP: joingroup failed, err=" + String((int)err), true);
      return;
    }
    SerialPrint("IGMP: Joined multicast group " + multicastIP.toString(), true);
    return;
  }

  igmp_report_groups(netif);
  UNLOCK_TCPIP_CORE();
}

void maybeRefreshIGMPMembership() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!isTimeValid((uint32_t)utcNow())) {
    return;
  }
  if (s_lastIgmpRefreshTime != 0
      && utcNow() - s_lastIgmpRefreshTime < IGMP_REFRESH_INTERVAL_SEC) {
    return;
  }
  s_lastIgmpRefreshTime = utcNow();
  refreshIGMPMembership();
}
#endif

#if _HAS_LOCAL_SENSORS
extern STRUCT_SNSHISTORY SensorHistory;
#endif

void maybeExitAPStationMode();

static bool isRssiValid(int32_t rssi) {
  return rssi < 0 && rssi > -150 && rssi > -999;
}

static const char* rssiHtmlColor(int32_t rssi) {
  if (!isRssiValid(rssi)) return nullptr;
  if (rssi > -60) return "#28a745";
  if (rssi > -70) return "#d4a017";
  return "#dc3545";
}

static String formatRssiHtml(int32_t rssi, const char* suffix = " dBm") {
  if (!isRssiValid(rssi)) return "n/a";
  const char* color = rssiHtmlColor(rssi);
  String val = String(rssi) + suffix;
  if (!color) return val;
  return String("<span style=\"color:") + color + ";font-weight:bold;\">" + val + "</span>";
}

static String formatCommTime(uint32_t t) {
  return (t > 0) ? dateifyLocal(t, "mm/dd/yyyy hh:nn:ss") : String("???");
}

static void appendCommTableRow(const char* label, const String& value) {
  WEBHTML += "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">";
  WEBHTML += label;
  WEBHTML += "</td><td style=\"padding: 8px; border: 1px solid #ddd;\">";
  WEBHTML += value;
  WEBHTML += "</td></tr>";
}

static void appendBroadcastForm(const char* action, const char* label, const char* color = "#2196F3") {
  WEBHTML += "<form action=\"";
  WEBHTML += action;
  WEBHTML += "\" method=\"post\" style=\"margin: 10px 0;\">";
  WEBHTML += "<button type=\"submit\" style=\"padding:8px 16px; background-color:";
  WEBHTML += color;
  WEBHTML += "; color:white; border:none; border-radius:4px; font-size:14px; cursor:pointer;\">";
  WEBHTML += label;
  WEBHTML += "</button></form>";
}

static String bssidToString(const uint8_t* bssid);
static const char* preferredCommunicationsLabel();

static void appendCommunicationsSection() {
  WEBHTML += "<h3>Communications</h3>";
  appendBroadcastForm("/REQUEST_BROADCAST", "Broadcast Now (ArborysMesh ESP + UDP)");

  WEBHTML += "<table style=\"width:100%;border-collapse:collapse;max-width:420px\">";
  appendCommTableRow("ArborysMesh in / out", String(I.MESH_RECEIVES) + " / " + String(I.MESH_SENDS));
  {
    const MeshStats& ms = meshStats();
    appendCommTableRow("Mesh errors in / out / RX dropped",
        String(I.MESH_INCOMING_ERRORS) + " / " + String(I.MESH_OUTGOING_ERRORS) + " / " + String(ms.rxDropped));
    appendCommTableRow("Mesh relays sent / suppressed",
        String(ms.relaysSent) + " / " + String(ms.relaysSuppressed));
    appendCommTableRow("Last mesh frame heard from",
        ms.lastRxRssi ? (MACToString(ms.lastRxFromMac) + " (" + formatRssiHtml(ms.lastRxRssi) + ")") : String("none yet"));
  }
  #ifdef _USEUDP
  appendCommTableRow("UDP in / out", String(I.UDP_RECEIVES) + " / " + String(I.UDP_SENDS));
  #endif
  appendCommTableRow("HTTP in / out", String(I.HTTP_RECEIVES) + " / " + String(I.HTTP_SENDS));
  WEBHTML += "</table>";

  WEBHTML += "<p style=\"margin-top:8px\">";
  appendBroadcastForm("/REQUEST_BROADCAST_ESP", "Broadcast ArborysMesh", "#4CAF50");
  #ifdef _USEUDP
  appendBroadcastForm("/REQUEST_BROADCAST_UDP", "Broadcast UDP", "#FF9800");
  #endif
  WEBHTML += "</p>";
}

static bool isHttpUiBrowseMessage(const char* messageType) {
  if (!messageType || messageType[0] == '\0') return true;
  static const char* kUiBrowseTypes[] = {
    "MainPage", "DeviceViewer", "DVNext", "DVPrev", "DVPing", "DVDelete",
    "DataHx", "AvgHx",
    "CONFIG", "ConfigIn", "ConfigDel", "ConfigOtaSwitch",
    "GSHEET", "GSHEETOut", "GSHEETUp", "GSHEETShare", "GSHEETDelete",
    "Weather", "WthrLoc", "WthrRef", "WthrZip", "WthrAddr", "WTHRREQ", "TIMEUPD",
    "SDCard", "SDDir", "SDDownload", "SDUp", "SDDelSns", "SDStoreDev",
    "SDSaveScr", "SDSaveWthr", "SDSysLog",
    "ErrorLog", "RebootDebug", "REBOOT",
    "404", "Broadcast", "STATUS", "MeshSet",
    "SnsOvrd", "SnsLim", "SnsUpd", "ReadReq", "API_SNS_READ_NOW",
  };
  for (const char* uiType : kUiBrowseTypes) {
    if (strcmp(messageType, uiType) == 0) return true;
  }
  return false;
}

static void syncWifiDownFlags(bool connected);

// Set before esp_wifi_stop so the Wi-Fi task's disconnect event is not a failure.
static volatile bool s_wifiFocusHold = false;
static bool s_wifiStoppedForFocus = false;
static bool s_wifiResumePending = false;

//wifi event registration 
void WiFiEvent(WiFiEvent_t event) {
  I.WiFiLastEvent = event;
  String s = WiFiEventtoString(event);
  SerialPrint("WiFiEvent: " + s, true);

  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      updateWifiChannel();
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      String msg = "STA got IP " + WiFi.localIP().toString();
      logSystemEvent(msg, EVENT_WIFI_CONNECTED);
      updateWifiChannel();
      syncDeviceIPFromWifi();
      I.wifiDownSince = 0;
      I.wifiFailCount = 0;
      maybeExitAPStationMode();
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      if (s_wifiFocusHold) break;
      logSystemEvent("STA lost IP (still may be associated)", EVENT_WIFI_DISCONNECTED);
      syncWifiDownFlags(false);
      syncDeviceIPFromWifi();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (s_wifiFocusHold) break;
      logSystemEvent("STA disconnected from AP", EVENT_WIFI_DISCONNECTED);
      syncWifiDownFlags(false);
      syncDeviceIPFromWifi();
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      logSystemEvent("WiFi AP started", EVENT_WIFI_AP_STARTED);
      updateWifiChannel();
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      logSystemEvent("WiFi AP stopped", EVENT_WIFI_AP_STOPPED);
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      logSystemEvent("AP client connected", EVENT_WIFI_AP_CLIENT_CONNECTED);
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      logSystemEvent("AP client disconnected", EVENT_WIFI_AP_CLIENT_DISCONNECTED);
      break;
    default:
      break;
  }
}

String WiFiEventtoString(WiFiEvent_t event) {
  String s = "Unknown WiFi event";
  switch (event) {    
      case ARDUINO_EVENT_WIFI_READY:
          s = "Wifi ready";
          break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
          s = "IP address: " + WiFi.localIP().toString();
          break;
      case ARDUINO_EVENT_WIFI_STA_LOST_IP:
          s = "Lost IP address";
          break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
          s = "Disconnected from AP";
          break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
          s = "Connected to AP";
          break;
      case ARDUINO_EVENT_WIFI_STA_START:
          s = "STA Started";
          break;
      case ARDUINO_EVENT_WIFI_STA_STOP:
          s = "STA Stopped";
          break;
      case ARDUINO_EVENT_WIFI_AP_START:
          s = "AP Started";
          break;
      case ARDUINO_EVENT_WIFI_AP_STOP:
          s = "AP Stopped";
          break;
      case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
          s = "APSTA IP assigned";
          break;
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
          s = "APSTA STA connected";
          break;
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
          s = "APSTA STA disconnected";
          break;
      case ARDUINO_EVENT_WIFI_SCAN_DONE:
        s = "Wifi scan done";
        break;
      
      default:
          break;
  }
  return s;
}


//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif
#ifdef _USE32

    WebServer server(80);
#endif






// Helper function to format bytes into human-readable format
String formatBytes(uint64_t bytes) {
  if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
    return String(bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
  } else if (bytes >= (1024ULL * 1024ULL)) {
    return String(bytes / (1024.0 * 1024.0), 1) + " MB";
  } else if (bytes >= 1024ULL) {
    return String(bytes / 1024.0, 1) + " KB";
  } else {
    return String(bytes) + " bytes";
  }
}

// Use ESP-IDF certificate bundle when cacert is "" or "*" or "bundle" (requires USE_CERT_BUNDLE in platformio.ini + sdkconfig cert bundle).
#if defined(_USE_CERT_BUNDLE)
extern "C" {
    extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
    extern const uint8_t x509_crt_imported_bundle_bin_end[]   asm("_binary_x509_crt_bundle_end");
}
#endif

static constexpr size_t HTTP_JSON_INTERNAL_PARSE_MARGIN = 49152;
static constexpr uint32_t HTTP_JSON_STREAM_TIMEOUT_MS = 60000;

static size_t httpJsonInternalParseBudget() {
  size_t freeHeap = esp_get_free_heap_size();
  if (freeHeap <= HTTP_JSON_INTERNAL_PARSE_MARGIN) return 0;
  size_t budget = freeHeap - HTTP_JSON_INTERNAL_PARSE_MARGIN;
  return (budget > 400000) ? 400000 : budget;
}

static String httpJsonDocSummary(const JsonDocument& doc) {
  if (doc.isNull()) return "empty";
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!root.isNull()) {
    return "object keys=" + String(root.size()) + " json=" + String(measureJson(doc)) + "B";
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (!arr.isNull()) {
    return "array len=" + String(arr.size()) + " json=" + String(measureJson(doc)) + "B";
  }
  return "parsed";
}

static bool httpJsonDocLooksParsed(const JsonDocument& doc, DeserializationError err) {
  if (err || doc.overflowed() || doc.isNull()) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!root.isNull() && root.size() > 0) return true;
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  return !arr.isNull() && arr.size() > 0;
}

#ifdef _USESDCARD
static bool writeRawToSD(const char* path, const uint8_t* data, size_t len) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  size_t written = f.write(data, len);
  f.flush();
  f.close();
  return written == len;
}

static bool readSdFileBytes(File& rf, char* dest, size_t len) {
  size_t pos = 0;
  while (pos < len) {
    size_t chunk = (len - pos > 4096) ? 4096 : (len - pos);
    int n = rf.read((uint8_t*)dest + pos, chunk);
    if (n <= 0) return false;
    pos += (size_t)n;
    if ((pos % 16384) < (size_t)n) esp_task_wdt_reset();
  }
  dest[len] = '\0';
  return true;
}
#endif

static DeserializationError deserializeHttpJsonPayload(
    JsonDocument& doc, const char* payload, size_t len,
    const JsonDocument* filter, const char* urlForLog) {

  if (!payload || len == 0) return DeserializationError::EmptyInput;

  auto tryParseBuffer = [&](const char* src, size_t srcLen) -> DeserializationError {
    doc.clear();
    if (filter) {
      return deserializeJson(doc, src, srcLen, DeserializationOption::Filter(*filter));
    }
    return deserializeJson(doc, src, srcLen);
  };

  const size_t internalBudget = httpJsonInternalParseBudget();
  if (len <= internalBudget) {
    char* internalCopy = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!internalCopy) internalCopy = (char*)malloc(len + 1);
    if (internalCopy) {
      memcpy(internalCopy, payload, len);
      internalCopy[len] = '\0';
      esp_task_wdt_reset();
      DeserializationError err = tryParseBuffer(internalCopy, len);
      free(internalCopy);
      if (httpJsonDocLooksParsed(doc, err)) return DeserializationError::Ok;
      if (err && err != DeserializationError::NoMemory) {
        SerialPrint(("SendHTTPMessage: internal RAM parse failed for " + String(urlForLog) + ": " + String(err.c_str())).c_str(), true);
        return err;
      }
      SerialPrint(("SendHTTPMessage: internal RAM parse incomplete for " + String(urlForLog) + " err=" + String(err.c_str()) + " " + httpJsonDocSummary(doc)).c_str(), true);
    } else {
      SerialPrint(("SendHTTPMessage: internal alloc failed for " + String(len) + " bytes (" + String(urlForLog) + ")").c_str(), true);
    }
  } else {
    SerialPrint(("SendHTTPMessage: payload " + String(len) + " exceeds internal budget " + String(internalBudget) + " (" + String(urlForLog) + ")").c_str(), true);
  }

#ifdef _USESDCARD
  const char* tmpPath = "/Data/HttpJsonParse.tmp";
  if (writeRawToSD(tmpPath, (const uint8_t*)payload, len)) {
    File rf = SD.open(tmpPath, FILE_READ);
    if (rf) {
      const size_t fileLen = rf.size();
      if (fileLen > 0 && fileLen == len) {
        if (fileLen <= internalBudget) {
          char* sdCopy = (char*)heap_caps_malloc(fileLen + 1, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
          if (!sdCopy) sdCopy = (char*)malloc(fileLen + 1);
          if (sdCopy && readSdFileBytes(rf, sdCopy, fileLen)) {
            esp_task_wdt_reset();
            DeserializationError err = tryParseBuffer(sdCopy, fileLen);
            free(sdCopy);
            if (httpJsonDocLooksParsed(doc, err)) {
              rf.close();
              SD.remove(tmpPath);
              return DeserializationError::Ok;
            }
            if (err && err != DeserializationError::NoMemory) {
              rf.close();
              SD.remove(tmpPath);
              SerialPrint(("SendHTTPMessage: SD buffer parse failed for " + String(urlForLog) + ": " + String(err.c_str())).c_str(), true);
              return err;
            }
          } else if (sdCopy) {
            free(sdCopy);
          }
        }

        rf.seek(0);
        rf.setTimeout(HTTP_JSON_STREAM_TIMEOUT_MS);
        esp_task_wdt_reset();
        doc.clear();
        DeserializationError err = filter
            ? deserializeJson(doc, rf, DeserializationOption::Filter(*filter))
            : deserializeJson(doc, rf);
        rf.close();
        SD.remove(tmpPath);
        if (httpJsonDocLooksParsed(doc, err)) return DeserializationError::Ok;
        if (err) {
          SerialPrint(("SendHTTPMessage: SD stream parse failed for " + String(urlForLog) + ": " + String(err.c_str())).c_str(), true);
          return err;
        }
        SerialPrint(("SendHTTPMessage: SD stream parse incomplete for " + String(urlForLog) + " " + httpJsonDocSummary(doc)).c_str(), true);
      } else {
        rf.close();
        SD.remove(tmpPath);
        SerialPrint(("SendHTTPMessage: SD temp file size mismatch for " + String(urlForLog) + " (file=" + String(fileLen) + " payload=" + String(len) + ")").c_str(), true);
      }
    } else {
      SerialPrint(("SendHTTPMessage: could not open SD temp file for " + String(urlForLog)).c_str(), true);
    }
  } else {
    SerialPrint(("SendHTTPMessage: could not write SD temp file for " + String(urlForLog)).c_str(), true);
  }
#endif

  esp_task_wdt_reset();
  doc.clear();
  DeserializationError err = tryParseBuffer(payload, len);
  if (httpJsonDocLooksParsed(doc, err)) return DeserializationError::Ok;
  if (!err) {
    SerialPrint(("SendHTTPMessage: parse returned Ok but empty doc for " + String(urlForLog) + " (" + httpJsonDocSummary(doc) + ")").c_str(), true);
    return DeserializationError::NoMemory;
  }
  SerialPrint(("SendHTTPMessage: direct parse failed for " + String(urlForLog) + ": " + String(err.c_str())).c_str(), true);
  return err;
}

#ifdef _USESDCARD
static void dumpHttpPayloadForDebug(const char* payload, size_t len, const char* url) {
  if (!payload || len == 0) return;
  if (writeRawToSD("/Data/HttpDebug_last.json", (const uint8_t*)payload, len)) {
    SerialPrint("SendHTTPMessage: Wrote /Data/HttpDebug_last.json for inspection", true);
  }
  char preview[81];
  size_t n = (len < 80) ? len : 80;
  memcpy(preview, payload, n);
  preview[n] = '\0';
  SerialPrint(("SendHTTPMessage: Payload preview: " + String(preview)).c_str(), true);
}
#endif

// A simple helper to let HTTPClient write directly into your M.payload
class PayloadWrapper : public Stream { // Change Print to Stream
  public:
      HTTPMessage* msg;
      size_t written = 0;
      PayloadWrapper(HTTPMessage* m) : msg(m) {}
  
      // Required by Stream but not used for this
      int available() override { return 0; }
      int read() override { return -1; }
      int peek() override { return -1; }
      void flush() override {}
  
      // The actual workhorse
      size_t write(uint8_t c) override {
          if (written + 1 >= msg->payloadSize) {
              if (!msg->resizePayload(msg->payloadSize + 4096)) return 0;
          }
          msg->payload.get()[written++] = (char)c;
          msg->payload.get()[written] = '\0';
          return 1;
      }
  
      size_t write(const uint8_t *buffer, size_t size) override {
          if (written + size + 1 >= msg->payloadSize) {
              if (!msg->resizePayload(msg->payloadSize + size + 4096)) return 0;
          }
          memcpy(&msg->payload.get()[written], buffer, size);
          written += size;
          msg->payload.get()[written] = '\0';
          if ((written % 16384) < size) {
              esp_task_wdt_reset();
          }
          return size;
      }
  };

// Collects binary HTTP response bodies (e.g. encrypted POST_ENC replies).
class EncResponseCollector : public Stream {
public:
    uint8_t* buf = nullptr;
    size_t maxLen = 0;
    size_t written = 0;

    EncResponseCollector(uint8_t* out, size_t outMax) : buf(out), maxLen(outMax) {}

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    size_t write(uint8_t c) override {
        return write(&c, 1);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!buf || size == 0) return 0;
        size_t room = (written < maxLen) ? (maxLen - written) : 0;
        if (size > room) size = room;
        if (size == 0) return 0;
        memcpy(buf + written, buffer, size);
        written += size;
        if ((written % 16384) < size) esp_task_wdt_reset();
        return size;
    }
};


bool SendHTTPMessage(HTTPMessage& M) {
  if (!M.url) {
      M.success = false;
      return false;
  }

  if (!wifiReadyForNetwork()) {
    M.success = false;
    M.httpCode = 0;
    return false;
  }

  WiFiClient wfclient;
  WiFiClientSecure wfsclient;
  HTTPClient http;


  const uint32_t clientTimeoutMs = (M.timeout > 0) ? M.timeout : 20000;
  const uint32_t handshakeTimeoutMs = (clientTimeoutMs > 35000) ? 35000 : clientTimeoutMs;

  bool isSecure = String(M.url.get()).startsWith("https");
  if (!isSecure) {
    wfclient.setTimeout(clientTimeoutMs);
    http.begin(wfclient,M.url.get());
  } else {

    //Setup Security
    if (!M.allowInsecure) {
        bool use_bundle = (M.cacert.get() == nullptr || strcmp(M.cacert.get(), "*") == 0 || strcmp(M.cacert.get(), "bundle") == 0 || strcmp(M.cacert.get(), "BUNDLE") == 0 || strcmp(M.cacert.get(), "") == 0);
        if (use_bundle) {
            #if defined(_USE_CERT_BUNDLE)
            wfsclient.setCACertBundle(x509_crt_imported_bundle_bin_start, (size_t)(x509_crt_imported_bundle_bin_end - x509_crt_imported_bundle_bin_start));
            #else
            M.success = false;
            storeError("SendHTTPMessage: No certificate bundle found", ERROR_HTTP_POST,true);
            SerialPrint("SendHTTPMessage: No certificate bundle found", true);
            return false;
            #endif
        } else {
            wfsclient.setCACert(M.cacert.get());
        }
    } else {
        SerialPrint("SendHTTPMessage: WARNING - insecure connection for " + String(M.url.get()), true);
        wfsclient.setInsecure();
    }
    wfsclient.setTimeout(clientTimeoutMs);
    wfsclient.setHandshakeTimeout(handshakeTimeoutMs);
      // 2. Initialize Connection
    if (!http.begin(wfsclient, M.url.get())) {
      M.success = false;
      SerialPrint("SendHTTPMessage: Failed to initialize connection for " + String(M.url.get()), true);
      storeError("SendHTTPMessage: Failed for " + String(M.url.get()), ERROR_HTTP_REQUEST,true);
      return false;
    }

  }

//  http.useHTTP10(true); //always prefer HTTP/1.0 for no chunked encoding
  http.setTimeout(clientTimeoutMs);

  // 3. Headers & Method execution
  if (M.contentType) http.addHeader("Content-Type", M.contentType.get());
  
  // Parse Extra Headers (Key: Value\nKey: Value)
  if (M.extraHeaders) {
      String headers = String(M.extraHeaders.get());
      int start = 0;
      while (start < headers.length()) {
          int end = headers.indexOf('\n', start);
          String line = (end == -1) ? headers.substring(start) : headers.substring(start, end);
          int colon = line.indexOf(':');
          if (colon != -1) {
              http.addHeader(line.substring(0, colon), line.substring(colon + 1));
          }
          if (end == -1) break;
          start = end + 1; 
      }
  }

  const char* method = M.method ? M.method.get() : "GET";
  esp_task_wdt_reset();
  M.httpCode = http.sendRequest(method, (uint8_t*)M.body.get(), M.body ? strlen(M.body.get()) : 0);
  esp_task_wdt_reset();


  if (M.httpCode < 200 || M.httpCode >= 400) {
    SerialPrint("SendHTTPMessage: Failed with code: " + String(M.httpCode) + " for " + String(M.url.get()), true);
    SerialPrint("SendHTTPMessage: Error: " + String(http.errorToString(M.httpCode).c_str()), true);
    ERRORCODES errType = ERROR_HTTP_RESPONSE;
    if (M.httpCode == HTTPC_ERROR_READ_TIMEOUT || M.httpCode == HTTPC_ERROR_CONNECTION_LOST) {
      errType = ERROR_HTTP_TIMEOUT;
    }
    M.success = false;
    http.end();
    // Close the client first. storeError on a peripheral opens another HTTP client.
    storeError("SendHTTPMessage: Failed with code: " + String(M.httpCode) + " for " + String(M.url.get()), errType, true);

    return false;
  }

  if (M.httpCode == 304) {
    M.success = true;
    http.end();
    return true;
  }

  // 4. Handle Response
  int serverSize = http.getSize();

  if (serverSize == 0) {
    SerialPrint("SendHTTPMessage: FYI: No payload from " + String(M.url.get()), true);
    M.success = (M.responseDoc == nullptr);
    http.end();
    if (!M.success) {
      storeError("SendHTTPMessage: Empty body for " + String(M.url.get()), ERROR_JSON_PARSE, true);
    }
    return M.success;
  }
    
  if (serverSize == -1) {
    // Case: Chunked Encoding (Unknown size)

    if (!M.payload ) { //user did not make a payload in advance, I'll have to guess

      size_t ramSize = 0;
      if (M.usePSRAM) {
        //correct call is for me to set the payload size, but the caller is allowed to do so
        //use 400kb of esp_get_free_psrampsram if that is available
        ramSize = ESP.getFreePsram();

        if (ramSize >= 450 * 1024) ramSize = 400 * 1024;
        else if (ramSize > 250*1024) ramSize = 200 * 1024;
        else if (ramSize > 150*1024) ramSize = 100 * 1024;
        else if (ramSize > 100*1024) ramSize = 50 * 1024;
        else if (ramSize > 50*1024) ramSize = 20 * 1024;
        else if (ramSize > 20*1024) ramSize = 10 * 1024;
        else {
          M.usePSRAM = false;
          ramSize = 2*1024;
        }
      } else {
        ramSize = esp_get_free_heap_size();
        if (ramSize >= 70 * 1024) ramSize = 20 * 1024;
        else if (ramSize > 50*1024) ramSize = 10 * 1024;
        else ramSize = 2*1024;
      }
      M.initPayload(ramSize);
    }

  } else {
    if (!M.payload) {
      if (!M.initPayload(serverSize + 1)) {
        SerialPrint("SendHTTPMessage: Failed to initialize payload for " + String(M.url.get()) + " with size " + String(serverSize + 1), true);
        M.success = false;
        http.end();
        storeError("SendHTTPMessage: Failed to initialize payload for " + String(M.url.get()), ERROR_HTTP_RESPONSE,true);
        return false;
      }
    } else {
      if (M.payloadSize < serverSize) {
        if (!M.resizePayload(serverSize + 1)) {
          SerialPrint("SendHTTPMessage: Failed to resize payload for " + String(M.url.get()) + " with size " + String(serverSize + 1), true);
          M.success = false;
          http.end();
          storeError("SendHTTPMessage: Failed to resize payload for " + String(M.url.get()) + " with size " + String(serverSize + 1), ERROR_HTTP_RESPONSE,true);
          return false;
        }
      }
    }

  } 

  //now stream the response to the payload
  PayloadWrapper wrapper(&M);
  http.writeToStream(&wrapper);
  esp_task_wdt_reset();
  M.success = true;

  size_t payloadLen = (M.payload && M.payload.get()) ? strlen(M.payload.get()) : 0;
  if (payloadLen > 0) {
    SerialPrint(("SendHTTPMessage: Downloaded " + String(payloadLen) + " bytes from " + String(M.url.get())).c_str(), true);
  }

  if (M.success && M.responseDoc && M.payload) {
    esp_task_wdt_reset();
    DeserializationError error = deserializeHttpJsonPayload(
        *M.responseDoc, M.payload.get(), payloadLen, M.filter, M.url.get());
    esp_task_wdt_reset();

    if (!error && M.responseDoc->overflowed()) {
      error = DeserializationError::NoMemory;
    }
    if (error) {
      M.success = false;
      SerialPrint("SendHTTPMessage: Failed to deserialize JSON for " + String(M.url.get()) + " with error: " + String(error.c_str()), true);
      http.end();
      storeError("SendHTTPMessage: Failed to deserialize JSON for " + String(M.url.get()) + " with error: " + String(error.c_str()), ERROR_JSON_PARSE, true);
#ifdef _USESDCARD
      dumpHttpPayloadForDebug(M.payload.get(), payloadLen, M.url.get());
#endif
    } else {
      SerialPrint(("SendHTTPMessage: Parsed " + httpJsonDocSummary(*M.responseDoc) + " for " + String(M.url.get())).c_str(), true);
    }
  }

  http.end();
  return M.success;
}


int8_t measureWifiLinkStatus() {
  //2 - usable IP/gateway/SSID/RSSI, but wifi.status() != WL_CONNECTED
  //1 - WL_CONNECTED with valid IP and gateway
  //0 - unknown status
  //-1 - no valid IP address
  //-2 - no valid RSSI range
  //-3 - no valid SSID
  //-4 - no valid gateway

  const int32_t rssi = WiFi.RSSI();
  const bool hasSsid = WiFi.SSID().length() > 0;
  const bool hasIp = WiFi.localIP() != IPAddress(0, 0, 0, 0);
  const bool hasGw = WiFi.gatewayIP() != IPAddress(0, 0, 0, 0);
  const bool rssiOk = (rssi < 0 && rssi > -150);

  // WL_CONNECTED alone is not enough — associated-without-DHCP reports connected + 0.0.0.0.
  if (WiFi.status() == WL_CONNECTED) {
    if (!hasIp) {
      I.WiFiStatus = -1;
      return -1;
    }
    if (!hasGw) {
      I.WiFiStatus = -4;
      return -4;
    }
    I.WiFiStatus = 1;
    return 1;
  }

  I.WiFiStatus = 0;
  if (!hasSsid) {
    I.WiFiStatus = -3;
  } else if (!hasGw) {
    I.WiFiStatus = -4;
  } else if (!hasIp) {
    I.WiFiStatus = -1;
  } else if (!rssiOk) {
    I.WiFiStatus = -2;
  } else {
    I.WiFiStatus = 2;
  }
  return I.WiFiStatus;
}

void updateRSSI(bool forceUpdate) {
  constexpr time_t RSSI_POLL_INTERVAL_SEC = 5;
  if (!forceUpdate && I.lastRSSItime != 0 && utcNow() - I.lastRSSItime < RSSI_POLL_INTERVAL_SEC) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    I.RSSIcurrent = -999;
    I.lastRSSItime = utcNow();
    return;
  }

  int16_t rssi = WiFi.RSSI();
  if (!isRssiValid(rssi)) {
    I.RSSIcurrent = -999;
  } else {
    I.RSSIcurrent = rssi;
    if (!isRssiValid(I.RSSIlow) || rssi < I.RSSIlow) {
      I.RSSIlow = rssi;
    }
    if (!isRssiValid(I.RSSIhigh) || rssi > I.RSSIhigh) {
      I.RSSIhigh = rssi;
    }
  }
  I.lastRSSItime = utcNow();
}

void syncDeviceIPFromWifi() {
  int16_t devIndex = Sensors.findMyDeviceIndex();
  if (devIndex < 0) return;
  ArborysDevType* device = Sensors.getDeviceByDevIndex(devIndex);
  if (!device || !device->IsSet) return;

  const bool connected = (measureWifiLinkStatus() >= 1);
  IPAddress newIP = connected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  if (device->IP == newIP) return;

  IPAddress oldIP = device->IP;
  device->IP = newIP;
  SerialPrint("syncDeviceIPFromWifi: " + oldIP.toString() + " -> " + newIP.toString(), true);
  #ifdef _USESDCARD
  storeDevicesSensorsSD();
  #endif
}

static void syncWifiDownFlags(bool connected) {
  if (s_wifiFocusHold) return;
  if (connected) {
    I.wifiDownSince = 0;
    I.wifiFailCount = 0;
    return;
  }
  if (I.wifiDownSince == 0 && isTimeValid((uint32_t)utcNow())) {
    I.wifiDownSince = utcNow();
  }
}

static bool haveWifiCredentials() {
  return Prefs.HAVECREDENTIALS && Prefs.WIFISSID[0] != 0;
}

struct WifiApCandidate {
  bool found = false;
  int32_t rssi = -127;
  int32_t channel = 0;
  uint8_t bssid[6] = {0};
};

static String bssidToString(const uint8_t* bssid) {
  if (!bssid) return "00:00:00:00:00:00";
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  return String(buf);
}

// Last radio that actually carried traffic. RTC survives ESP.restart(), so the
// WIFI FAILED reboot can rejoin this BSSID before it scans.
static constexpr uint32_t WIFI_ANCHOR_MAGIC = 0x57494631u;
struct WifiAnchorRtc {
  uint32_t magic;
  uint8_t bssid[6];
  uint8_t channel;
  uint8_t valid;
};
RTC_NOINIT_ATTR static WifiAnchorRtc s_wifiAnchorRtc;

struct StaAnchor {
  bool valid = false;
  uint8_t channel = 0;
  int32_t rssi = -127;
  uint8_t bssid[6] = {0};
};

static bool loadRtcAnchor(StaAnchor& out) {
  out = StaAnchor{};
  if (s_wifiAnchorRtc.magic != WIFI_ANCHOR_MAGIC || !s_wifiAnchorRtc.valid) return false;
  if (s_wifiAnchorRtc.channel < AP_WIFI_CHANNEL_MIN || s_wifiAnchorRtc.channel > AP_WIFI_CHANNEL_MAX) return false;
  out.valid = true;
  out.channel = s_wifiAnchorRtc.channel;
  memcpy(out.bssid, s_wifiAnchorRtc.bssid, 6);
  return true;
}

static void saveRtcAnchor(const StaAnchor& a) {
  if (!a.valid) return;
  s_wifiAnchorRtc.magic = WIFI_ANCHOR_MAGIC;
  s_wifiAnchorRtc.channel = a.channel;
  s_wifiAnchorRtc.valid = 1;
  memcpy(s_wifiAnchorRtc.bssid, a.bssid, 6);
}

static StaAnchor captureLiveAnchor() {
  StaAnchor a;
  if (!wifiReadyForNetwork()) return a;
  const uint8_t* bssid = WiFi.BSSID();
  const int ch = WiFi.channel();
  if (!bssid || ch < AP_WIFI_CHANNEL_MIN || ch > AP_WIFI_CHANNEL_MAX) return a;
  a.valid = true;
  a.channel = (uint8_t)ch;
  a.rssi = WiFi.RSSI();
  memcpy(a.bssid, bssid, 6);
  saveRtcAnchor(a);
  return a;
}

static bool joinAnchor(const StaAnchor& a) {
  if (!haveWifiCredentials()) return false;
  if (softApRunning()) {
    // The portal owns the radio on channel 1. Do not retune it to chase STA.
    return false;
  }
  #ifdef _USE32
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  if (WiFi.getMode() != WIFI_MODE_STA && WiFi.getMode() != WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_MODE_STA);
  }
  #endif
  // Already on this BSSID and channel: begin() again would bounce the link.
  if (a.valid && WiFi.status() == WL_CONNECTED) {
    const uint8_t* live = WiFi.BSSID();
    if (live && WiFi.channel() == (int)a.channel && memcmp(live, a.bssid, 6) == 0
        && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      WiFi.setSleep(WIFI_PS_NONE);
      return true;
    }
  }
  // begin() while still associated ignores the new channel and leaves the scan channel.
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false);
    delay(20);
    esp_task_wdt_reset();
  }
  if (a.valid) {
    SerialPrint("WiFi: rejoining saved AP " + bssidToString(a.bssid) + " ch=" + String(a.channel), true);
    WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD, a.channel, a.bssid, true);
  } else {
    SerialPrint("WiFi: no saved AP; joining SSID on channel 1", true);
    WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD, AP_WIFI_CHANNEL_MIN);
  }
  WiFi.setSleep(WIFI_PS_NONE);
  return true;
}

static bool waitStaReady(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    esp_task_wdt_reset();
    if (wifiReadyForNetwork()) return true;
    delay(100);
  }
  return wifiReadyForNetwork();
}

// Active scan. The caller must copy the results before any WiFi.begin, which clears them.
// priorOut receives the radio that was working before the scan, if any.
static int16_t scanNetworksKeepSta(StaAnchor* priorOut) {
  const StaAnchor prior = captureLiveAnchor();
  if (priorOut) *priorOut = prior;
  esp_task_wdt_reset();
  const int16_t found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  esp_task_wdt_reset();
  return found;
}

// A scan or channel hop can leave WL_CONNECTED and the old IP while the radio sits
// on the last channel it visited. That still counts as having left the saved AP.
static bool staStillHome(const StaAnchor& prior) {
  if (!prior.valid || !wifiReadyForNetwork()) return false;
  if (WiFi.channel() != (int)prior.channel) return false;
  const uint8_t* bssid = WiFi.BSSID();
  return bssid && memcmp(bssid, prior.bssid, 6) == 0;
}

static void restoreAnchorIfDropped(const StaAnchor& prior) {
  if (!prior.valid || staStillHome(prior)) return;
  SerialPrint("WiFi left " + bssidToString(prior.bssid) + " ch=" + String(prior.channel)
      + "; rejoining", true);
  if (!joinAnchor(prior)) return;
  // Do not return while the radio is still on the scan channel. The next
  // capture would store that channel as the AP to use after a reboot.
  if (!waitStaReady(15000) || !staStillHome(prior)) {
    SerialPrint("WiFi: previous AP did not come back on ch=" + String(prior.channel), true);
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false);
    return;
  }
  captureLiveAnchor();
}

static void noteWifiFailPending() {
  Preferences prefs;
  if (!prefs.begin("wififail", false)) return;
  prefs.putUChar("pending", 1);
  prefs.putUChar("cause", (uint8_t)RESET_WIFI);
  prefs.putString("msg", "WiFi failed");
  prefs.end();
}

static void reportWifiFailToHubs() {
  static bool s_settled = false;
  if (s_settled || !wifiReadyForNetwork()) return;

  Preferences prefs;
  if (!prefs.begin("wififail", true)) return;
  const uint8_t pending = prefs.getUChar("pending", 0);
  prefs.end();
  if (pending != 1) {
    s_settled = true;
    return;
  }

  // Hub IPs arrive after the device list is filled. Keep the NVS flag until one exists.
  bool hubKnown = false;
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) continue;
    if (d->IP == IPAddress(0, 0, 0, 0) || d->IP == WiFi.localIP()) continue;
    hubKnown = true;
    break;
  }
  if (!hubKnown) return;

  if (!prefs.begin("wififail", false)) return;
  prefs.putUChar("pending", 0);
  prefs.end();
  s_settled = true;
  // storeError on a peripheral forwards to hubs only while STA is up.
  storeError("WiFi failed", ERROR_REBOOT_TRIGGERED, true);
  SerialPrint("Reported WiFi failed to hubs", true);
}

// Scan for Prefs.WIFISSID and return the strongest AP. Does not persist the BSSID.
static bool findBestApForConfiguredSsid(WifiApCandidate& out) {
  out = WifiApCandidate{};
  if (Prefs.WIFISSID[0] == '\0') return false;

  StaAnchor prior;
  const int numNetworks = scanNetworksKeepSta(&prior);
  if (numNetworks <= 0) {
    WiFi.scanDelete();
    restoreAnchorIfDropped(prior);
    return false;
  }

  for (int i = 0; i < numNetworks; i++) {
    if (WiFi.SSID(i) != Prefs.WIFISSID) continue;
    const int32_t rssi = WiFi.RSSI(i);
    if (!out.found || rssi > out.rssi) {
      const uint8_t* bssid = WiFi.BSSID(i);
      if (!bssid) continue;
      out.found = true;
      out.rssi = rssi;
      out.channel = WiFi.channel(i);
      memcpy(out.bssid, bssid, 6);
    }
  }
  WiFi.scanDelete();
  // Leave a dropped link down if the caller is about to join a stronger AP.
  // If this scan is the end of the decision, the caller rejoins prior.
  if (!out.found) restoreAnchorIfDropped(prior);
  return out.found;
}

// Re-apply the soft-AP PSK if a STA begin/mode change left the beacon up but open
// or with a different password. Phones report that as a wrong password.
static void ensureSoftApCredentials() {
  if (!softApRunning()) return;
  // STA and the soft AP share one radio. Once the router link is up, leave that
  // channel alone; maybeExitAPStationMode() removes the AP. Forcing channel 1 here
  // would drop the link the wizard just brought up.
  if (wifiReadyForNetwork()) return;

  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) != ESP_OK) return;

  const bool passwordOk = conf.ap.authmode == WIFI_AUTH_WPA2_PSK
      && strncmp((const char*)conf.ap.password, AP_STATION_PASSWORD, sizeof(conf.ap.password)) == 0;
  const bool channelOk = conf.ap.channel == AP_WIFI_CHANNEL_MIN;
  char expectedSsid[33];
  snprintf(expectedSsid, sizeof(expectedSsid), "SensorNet-%02X%02X%02X%02X%02X%02X",
      getPROCIDByte(Prefs.PROCID, 0), getPROCIDByte(Prefs.PROCID, 1), getPROCIDByte(Prefs.PROCID, 2),
      getPROCIDByte(Prefs.PROCID, 3), getPROCIDByte(Prefs.PROCID, 4), getPROCIDByte(Prefs.PROCID, 5));
  const bool ssidOk = strncmp((const char*)conf.ap.ssid, expectedSsid, sizeof(conf.ap.ssid)) == 0;
  if (passwordOk && ssidOk && channelOk) return;

  static uint32_t s_lastReapplyMs = 0;
  const uint32_t nowMs = millis();
  if (s_lastReapplyMs != 0 && (nowMs - s_lastReapplyMs) < 15000UL) return;
  s_lastReapplyMs = nowMs ? nowMs : 1;

  SerialPrint("Soft AP password or SSID was cleared; reapplying", true);
  String wifiID;
  String wifiPWD;
  IPAddress apIP;
  connectSoftAP(&wifiID, &wifiPWD, &apIP);
}

// First join uses the saved BSSID and channel and does not scan.
// The following join scans. If that scan finds nothing, or the new radio does not
// come up, the saved radio is put back. Soft AP owns channel 1 and is not retuned.
static bool s_bootAnchorTried = false;
static uint32_t s_staDownSinceMs = 0;
static uint32_t s_staDownRetryMs = 0;

static void beginWifiPreferBestBssid() {
  // Boot and the setup wizard call this. Runtime recovery does not: it rejoins the
  // saved radio through joinAnchor(), which leaves a channel-1 portal untouched.
  if (!s_bootAnchorTried) {
    s_bootAnchorTried = true;
    StaAnchor saved;
    if (loadRtcAnchor(saved)) {
      joinAnchor(saved);
      return;
    }
  }

  const StaAnchor prior = captureLiveAnchor();
  WifiApCandidate best;
  if (findBestApForConfiguredSsid(best) && best.channel > 0) {
    const bool same = prior.valid && memcmp(best.bssid, prior.bssid, 6) == 0;
    const int32_t improvement = prior.valid ? (best.rssi - prior.rssi) : WIFI_BSSID_ROAM_MIN_IMPROVEMENT_DB;
    if (prior.valid && (same || improvement < WIFI_BSSID_ROAM_MIN_IMPROVEMENT_DB)) {
      restoreAnchorIfDropped(prior);
      return;
    }
    SerialPrint("WiFi: joining strongest BSSID " + bssidToString(best.bssid) +
        " ch=" + String(best.channel) + " rssi=" + String(best.rssi) +
        " for SSID " + String(Prefs.WIFISSID), true);
    // begin() while still associated ignores the new channel and leaves the scan channel.
    WiFi.disconnect(false);
    delay(20);
    esp_task_wdt_reset();
    WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD, best.channel, best.bssid, true);
    WiFi.setSleep(WIFI_PS_NONE);
    if (!waitStaReady(15000) || WiFi.channel() != best.channel) {
      SerialPrint("WiFi: new AP did not come up on ch=" + String(best.channel) + "; reverting", true);
      if (prior.valid) joinAnchor(prior);
      else {
        StaAnchor saved;
        if (loadRtcAnchor(saved)) joinAnchor(saved);
      }
    } else {
      captureLiveAnchor();
    }
    return;
  }

  SerialPrint("WiFi: no BSSID scan match for " + String(Prefs.WIFISSID), true);
  if (prior.valid) joinAnchor(prior);
  else joinAnchor(StaAnchor{});
}

static bool initialSetupRequirementsMet() {
  if (!haveWifiCredentials()) return false;
  if (Prefs.TimeZoneOffset > 50400) return false;
  #ifdef _USEWEATHER
  if (Prefs.LATITUDE == 0 && Prefs.LONGITUDE == 0) return false;
  #endif
  // Cloud claim is optional: unclaimed devices run LAN-only.
  return true;
}

void syncInitialSetupState() {
  if (initialSetupRequirementsMet()) {
    I.initialSetupFinalized = true;
    I.initialSetupExitPending = false;
  }
}

void resetEphemeralCoreWifiState() {
  I.initialSetupExitPending = false;
  I.apModeEnteredTime = 0;
  I.apLastClientActivity = 0;
  I.apLastReconnectCheckTime = 0;
  I.apLastChannelScanTime = 0;
}

void reconcileWifiStateAfterCoreLoad() {
  resetEphemeralCoreWifiState();
  syncInitialSetupState();
  CheckWifiStatus(WIFI_CHECK_NORMAL);
}

void holdWifiForLocalFocus() {
  if (s_wifiFocusHold) return;
  if (softApRunning()) return;
  const wifi_mode_t mode = WiFi.getMode();
  s_wifiFocusHold = true;
  __sync_synchronize();
  I.wifiDownSince = 0;
  I.wifiFailCount = 0;
  s_staDownSinceMs = 0;
  s_staDownRetryMs = 0;
  if (mode == WIFI_MODE_NULL) return;
  if (esp_wifi_stop() == ESP_OK) {
    s_wifiStoppedForFocus = true;
    SerialPrint("Garage focus: Wi-Fi paused for distance", true);
  } else {
    SerialPrint("Garage focus: Wi-Fi pause failed", true);
  }
}

void releaseWifiFromLocalFocus() {
  if (s_wifiFocusHold) {
    s_wifiFocusHold = false;
    I.wifiDownSince = 0;
    I.wifiFailCount = 0;
    s_staDownSinceMs = 0;
    // A fresh reconnect is already in flight. Do not start a second join this pass.
    s_staDownRetryMs = millis();
    if (s_staDownRetryMs == 0) s_staDownRetryMs = 1;
    if (s_wifiStoppedForFocus) {
      s_wifiStoppedForFocus = false;
      if (esp_wifi_start() == ESP_OK) {
        esp_wifi_connect();
        s_wifiResumePending = true;
        SerialPrint("Garage focus ended: Wi-Fi resuming", true);
      }
    }
  }
  if (s_wifiResumePending && wifiReadyForNetwork()) {
    s_wifiResumePending = false;
#ifdef _USEUDP
    connectUDP();
#endif
  }
}

int8_t CheckWifiStatus(WifiCheckMode mode) {
  if (s_wifiFocusHold) return 0;
  const int8_t linkStatus = measureWifiLinkStatus();
  const bool connected = (linkStatus >= 1);

  syncWifiDownFlags(connected);
  syncDeviceIPFromWifi();

  if (connected) {
    s_staDownSinceMs = 0;
    s_staDownRetryMs = 0;
    s_bootAnchorTried = false;
    captureLiveAnchor();
    reportWifiFailToHubs();
    maybeExitAPStationMode();
    return linkStatus;
  }

  if (mode == WIFI_CHECK_BOOT) {
    if (!haveWifiCredentials()) {
      enterAPStationMode();
      return linkStatus;
    }
    const uint32_t bootDeadline = millis() + WIFI_BOOT_MAX_MS;
    uint8_t attempt = 0;
    while ((int32_t)(bootDeadline - millis()) > 1000) {
      ++attempt;
      const uint32_t remaining = bootDeadline - millis();
      const uint16_t tryMs = (remaining > WIFI_BOOT_TRY_MS) ? WIFI_BOOT_TRY_MS : (uint16_t)remaining;
      #ifdef _USETFT
      tftPrint("WiFi Attempt " + String(attempt) + "...",
          false, TFT_WHITE, 2, 1, false, -1, -1);
      #endif
      SerialPrint("Boot WiFi attempt " + String(attempt) + " (" + String(tryMs) + " ms, budget "
          + String(WIFI_BOOT_MAX_MS) + " ms)", true);
      if (tryWifi(tryMs, true) == 1) {
        #ifdef _USETFT
        tftPrint(" OK", true, TFT_GREEN);
        #endif
        syncWifiDownFlags(true);
        syncDeviceIPFromWifi();
        return measureWifiLinkStatus();
      }
      #ifdef _USETFT
      tftPrint(" Fail", true, TFT_RED);
      #endif
    }
    SerialPrint("Boot WiFi failed within " + String(WIFI_BOOT_MAX_MS) + " ms; entering AP mode", true);
    enterAPStationMode();
    return linkStatus;
  }

  // No saved password: the soft AP is the only way in.
  if (!haveWifiCredentials()) {
    if (!softApRunning()) {
      enterAPStationMode();
    }
    return linkStatus;
  }

  // Boot already opened the portal because the saved credentials did not connect.
  // Leave it on channel 1. Do not scan, rejoin, or reboot over the top of it.
  if (softApRunning()) return linkStatus;

  // Saved password: rejoin the last radio. Do not open the soft AP and do not scan.
  maybeRecoverWifiWithoutIp();
  if (wifiReadyForNetwork()) return measureWifiLinkStatus();

  const uint32_t nowMs = millis();
  if (s_staDownSinceMs == 0) s_staDownSinceMs = nowMs ? nowMs : 1;
  const bool downLongByClock = I.wifiDownSince && isTimeValid((uint32_t)utcNow())
      && (utcNow() - I.wifiDownSince >= WIFI_DOWN_AP_THRESHOLD_SEC);
  const bool downLongByMillis = (nowMs - s_staDownSinceMs) >= (WIFI_DOWN_AP_THRESHOLD_SEC * 1000UL);
  if (downLongByClock || downLongByMillis) {
    noteWifiFailPending();
    controlledReboot("WiFi failed", RESET_WIFI, true);
    return linkStatus;
  }

  if (s_staDownRetryMs == 0 || (nowMs - s_staDownRetryMs) >= (WIFI_AP_STA_RECONNECT_SEC * 1000UL)) {
    s_staDownRetryMs = nowMs ? nowMs : 1;
    StaAnchor saved;
    if (!loadRtcAnchor(saved)) saved = StaAnchor{};
    joinAnchor(saved);
  }

  return linkStatus;
}

bool wifiReadyForNetwork() {
  const int8_t s = measureWifiLinkStatus();
  return s == 1 || s == 2;
}

bool softApRunning() {
  wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    return false;
  }
  return WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
}

int16_t tryWifi(uint16_t delayms, bool checkCredentials) {
  
  if (checkCredentials) {
    if (!Prefs.HAVECREDENTIALS) {
      tftPrint("No credentials", true);
      return -1000;
    }
  }
    
  if (Prefs.WIFISSID[0] == 0) return -1000;

  #ifdef _USE32
  // Configure WiFi for WPA2/WPA3 compatibility
  // This helps with mixed WPA2/WPA3 networks (transition mode)
  WiFi.setAutoReconnect(true);
  // Credentials live in Prefs. Do not store STA/AP config in WiFi NVS (persistent defaults true on this core).
  WiFi.persistent(false);
  #endif

  // Scan for strongest BSSID of this SSID, then join it (not persisted).
  beginWifiPreferBestBssid();
  WiFi.setSleep(WIFI_PS_NONE);
        
  SerialPrint("I.WiFiLastEvent: " + WiFiEventtoString(I.WiFiLastEvent), true);
    
  delay(100);

  // Wait for connection AND IP assignment with timeout (e.g., 80% of total delayms)
  uint32_t startTime = millis();
  uint32_t firstTryTimeout = delayms; 
    
    // First check for connection
    while (measureWifiLinkStatus() < 1 && (millis() - startTime) < firstTryTimeout) {
      SerialPrint("Waiting for connection... " + String(millis() - startTime) + " of " + String(firstTryTimeout) + " milliseconds", true);
      delay(100);
    }
    
    if (I.WiFiStatus >= 1) {
      if (!Prefs.HAVECREDENTIALS) {
        Prefs.isUpToDate = true;
      }
      I.wifiFailCount = 0;
      return 1; 

    } else {
      return -1; 
    }

}

int16_t connectWiFi(uint8_t retryLimit, uint16_t tryTimeoutMs) {
  // Blocking connect for setup wizard after user saves credentials.
  uint8_t retries = 0;
  while (measureWifiLinkStatus() != 1 && retries < retryLimit) {
    tryWifi(tryTimeoutMs, true);
    retries++;
    SerialPrint("connectWiFi: attempt #" + String(retries), true);
  }

  if (measureWifiLinkStatus() == 1) {
    SerialPrint("connectWiFi: WiFi connected", true);
    syncDeviceIPFromWifi();
    updateWifiChannel();
    return retries;
  }

  return -1000;
}

void startWifiConnectAsync() {
  if (!haveWifiCredentials()) return;
  if (softApRunning()) return;
  // Already associated / usable — do not re-issue WiFi.begin (can bounce soft-AP in APSTA).
  if (wifiReadyForNetwork()) return;
  // Time-debounce begin(); do NOT gate on WL_IDLE_STATUS — on Arduino-ESP32 3.x that
  // means associated-without-IP / lost-IP, not "still negotiating", and blocking on it
  // left devices stuck in soft-AP after prolonged outages.
  static uint32_t s_lastBeginMs = 0;
  const uint32_t nowMs = millis();
  if (s_lastBeginMs != 0
      && (nowMs - s_lastBeginMs) < (WIFI_AP_STA_RECONNECT_SEC * 1000UL)) {
    return;
  }

  #ifdef _USE32
  // Auto-reconnect scans while the STA is down and breaks soft-AP authentication.
  WiFi.setAutoReconnect(!softApRunning());
  WiFi.persistent(false);
  #endif
  // Preserve AP+STA if soft-AP is already up; never force a mode flip here.
  if (softApRunning()) {
    if (WiFi.getMode() != WIFI_MODE_APSTA) {
      WiFi.mode(WIFI_MODE_APSTA);
    }
  } else if (WiFi.getMode() != WIFI_MODE_STA && WiFi.getMode() != WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_MODE_STA);
  }
  StaAnchor saved;
  if (!loadRtcAnchor(saved)) saved = StaAnchor{};
  joinAnchor(saved);
  s_lastBeginMs = nowMs;
  SerialPrint("startWifiConnectAsync: rejoin saved AP started", true);
}

void maybeRecoverWifiWithoutIp() {
  if (!haveWifiCredentials()) return;
  // Soft AP is channel 1 with the portal password. Do not disconnect or scan over it.
  if (softApRunning()) return;

  static uint32_t s_zeroIpSinceMs = 0;
  static uint32_t s_lastRecoverMs = 0;
  static uint32_t s_beginAfterDisconnectMs = 0;

  const uint32_t nowMs = millis();

  // Complete a pending begin after a forced disconnect (avoid long delay in the loop).
  if (s_beginAfterDisconnectMs != 0) {
    if ((int32_t)(nowMs - s_beginAfterDisconnectMs) < 300) return;
    s_beginAfterDisconnectMs = 0;
    #ifdef _USE32
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    #endif
    if (softApRunning()) {
      if (WiFi.getMode() != WIFI_MODE_APSTA) {
        WiFi.mode(WIFI_MODE_APSTA);
      }
    } else if (WiFi.getMode() != WIFI_MODE_STA && WiFi.getMode() != WIFI_MODE_APSTA) {
      WiFi.mode(WIFI_MODE_STA);
    }
    StaAnchor saved;
    if (!loadRtcAnchor(saved)) saved = StaAnchor{};
    joinAnchor(saved);
    SerialPrint("maybeRecoverWifiWithoutIp: rejoin after disconnect", true);
    return;
  }

  if (wifiReadyForNetwork()) {
    s_zeroIpSinceMs = 0;
    return;
  }

  // Associated (or Arduino-ESP32 3.x idle/associated-without-IP) but no DHCP address.
  const wl_status_t st = WiFi.status();
  const bool associatedNoIp = (WiFi.localIP() == IPAddress(0, 0, 0, 0))
      && (st == WL_CONNECTED || st == WL_IDLE_STATUS);
  if (!associatedNoIp) {
    s_zeroIpSinceMs = 0;
    return;
  }

  if (s_zeroIpSinceMs == 0) {
    s_zeroIpSinceMs = nowMs ? nowMs : 1;
    SerialPrint("maybeRecoverWifiWithoutIp: associated without IP; waiting for DHCP", true);
    return;
  }
  if ((nowMs - s_zeroIpSinceMs) < WIFI_ZERO_IP_GRACE_MS) return;
  if (s_lastRecoverMs != 0 && (nowMs - s_lastRecoverMs) < WIFI_ZERO_IP_RECOVER_INTERVAL_MS) {
    return;
  }

  s_lastRecoverMs = nowMs;
  if (I.wifiFailCount < 255) I.wifiFailCount++;

  if (I.wifiFailCount >= WIFI_ZERO_IP_REBOOT_AFTER) {
    noteWifiFailPending();
    controlledReboot("WiFi failed", RESET_WIFI, true);
    return;
  }

  SerialPrint("maybeRecoverWifiWithoutIp: disconnect+reconnect attempt #"
      + String(I.wifiFailCount) + " status=" + String((int)st)
      + " ip=" + WiFi.localIP().toString(), true);
  // false = keep credentials in flash; force a fresh association/DHCP cycle.
  WiFi.disconnect(false);
  s_beginAfterDisconnectMs = nowMs ? nowMs : 1;
  s_zeroIpSinceMs = 0;
}

void maybeOptimizeWifiBssid() {
  static time_t s_lastOptimizeTime = 0;

  if (!haveWifiCredentials()) return;
  if (!wifiReadyForNetwork()) return;
  if (softApRunning()) return;
  if (!isTimeValid((uint32_t)utcNow())) return;

  // Start the 180-minute clock on first eligible call; do not rescan immediately after boot connect.
  if (s_lastOptimizeTime == 0) {
    s_lastOptimizeTime = utcNow();
    captureLiveAnchor();
    return;
  }
  if (utcNow() >= s_lastOptimizeTime
      && (utcNow() - s_lastOptimizeTime) < WIFI_BSSID_OPTIMIZE_INTERVAL_SEC) {
    return;
  }
  s_lastOptimizeTime = utcNow();

  const StaAnchor prior = captureLiveAnchor();
  if (!prior.valid) {
    SerialPrint("WiFi BSSID optimize: no current AP; skipping", true);
    return;
  }
  if (isRssiValid(prior.rssi) && prior.rssi > WIFI_BSSID_OPTIMIZE_SKIP_ABOVE_DB) {
    SerialPrint("WiFi BSSID optimize: rssi=" + String(prior.rssi) + " is fine; not searching", true);
    return;
  }

  WifiApCandidate best;
  if (!findBestApForConfiguredSsid(best)) {
    SerialPrint("WiFi BSSID optimize: scan found no APs for " + String(Prefs.WIFISSID), true);
    restoreAnchorIfDropped(prior);
    return;
  }

  const bool sameAp = (memcmp(best.bssid, prior.bssid, 6) == 0);
  const int32_t improvement = best.rssi - prior.rssi;
  if (sameAp || improvement < WIFI_BSSID_ROAM_MIN_IMPROVEMENT_DB) {
    SerialPrint("WiFi BSSID optimize: keeping " + bssidToString(prior.bssid) +
        " ch=" + String(prior.channel), true);
    restoreAnchorIfDropped(prior);
    return;
  }

  SerialPrint("WiFi BSSID optimize: roaming " + bssidToString(prior.bssid) +
      " rssi=" + String(prior.rssi) + " -> " + bssidToString(best.bssid) +
      " rssi=" + String(best.rssi) + " ch=" + String(best.channel), true);
  WiFi.disconnect(false);
  delay(20);
  esp_task_wdt_reset();
  WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD, best.channel, best.bssid, true);
  WiFi.setSleep(WIFI_PS_NONE);
  if (!waitStaReady(15000) || WiFi.channel() != best.channel) {
    SerialPrint("WiFi BSSID optimize: new AP did not come up on ch=" + String(best.channel) + "; reverting", true);
    joinAnchor(prior);
  } else {
    captureLiveAnchor();
  }
}

bool connectUDP() {
  #ifdef _USEUDP

  #ifndef _USELOWPOWER
    // Disable WiFi Sleep to ensure we don't miss packets
    WiFi.setSleep(false);
  #endif

  IPAddress multicastIP(_USEUDP_MULTICAST);
  //1. wifi connected
  //2. Start the UDP server on the port defined (WiFiUDP automatically binds to device IP)
  
  if (LAN_UDP.begin(IPAddress(0,0,0,0), _USEUDP)) { //listen for any incoming packets
    SerialPrint("UDP bound to port " + String(_USEUDP), true);
  
    
    // Then join the multicast group specifically
    // This allows the hardware to pass through packets sent to the multicast group
    if (LAN_UDP.beginMulticast(multicastIP, _USEUDP)) {
        SerialPrint("Joined Multicast Group: " + multicastIP.toString(), true);
    }

    refreshIGMPMembership();
    
    return true;

  } 
  
#endif
return false;
}

namespace {
  uint32_t s_apEnterMillis = 0;
  uint32_t s_apLastChannelScanMillis = 0;
  uint32_t s_apLastReconnectMillis = 0;
  volatile bool s_apChannelScanListen = false;
  volatile bool s_apChannelScanGotResponse = false;

  bool apEspNowStaleFor(uint32_t seconds) {
    if (!isTimeValid(I.MESH_LAST_INCOMINGMSG_TIME)) return true;
    if (!isTimeValid((uint32_t)utcNow())) {
      return (millis() - s_apEnterMillis) >= (seconds * 1000UL);
    }
    return (utcNow() - I.MESH_LAST_INCOMINGMSG_TIME) >= seconds;
  }

  bool apClientIdleFor(uint32_t seconds) {
    if (isTimeValid(I.apLastClientActivity) && isTimeValid((uint32_t)utcNow())) {
      return (utcNow() - I.apLastClientActivity) >= seconds;
    }
    if (isTimeValid(I.apModeEnteredTime) && isTimeValid((uint32_t)utcNow())) {
      return (utcNow() - I.apModeEnteredTime) >= seconds;
    }
    return (millis() - s_apEnterMillis) >= (seconds * 1000UL);
  }

  void maybeSendApModeEntryServerPing() {
    if (!apEspNowStaleFor(AP_ESP_NOW_STALE_PING_SEC)) return;
    if (!ensureESPNOW()) return;
    SerialPrint("AP mode: sending broadcast server ping (type 13)", true);
    broadcastServerPing(1);
  }

  bool runApModeChannelScan() {
    if (!ensureESPNOW()) return false;

    SerialPrint("AP mode: starting ESP-NOW channel scan", true);
    s_apChannelScanListen = true;
    s_apChannelScanGotResponse = false;

    bool found = false;
    for (uint8_t ch = AP_WIFI_CHANNEL_MIN; ch <= AP_WIFI_CHANNEL_MAX; ++ch) {
      if (!setWifiRfChannel(ch)) continue;

      broadcastServerPing(1);

      const uint32_t stepStart = millis();
      while (millis() - stepStart < AP_CHANNEL_SCAN_STEP_MS) {
        delay(10);
        esp_task_wdt_reset();
        server.handleClient();
        if (s_apChannelScanGotResponse) {
          found = true;
          break;
        }
      }
      if (found) break;
    }

    s_apChannelScanListen = false;

    if (!found && _I_AM_SERVER) {
      SerialPrint("AP mode: channel scan failed, server defaulting to channel 1", true);
      setWifiRfChannel(1);
    }

    s_apLastChannelScanMillis = millis();
    if (isTimeValid((uint32_t)utcNow())) {
      I.apLastChannelScanTime = utcNow();
    }

    SerialPrint(String("AP mode: channel scan ") + (found ? "found server" : "no server"), true);
    // The hop leaves the radio off channel 1. Put the portal back, password included.
    String wifiID;
    String wifiPWD;
    IPAddress apIP;
    connectSoftAP(&wifiID, &wifiPWD, &apIP);
    return found;
  }

  bool shouldRunApChannelScan() {
    if (wifiReadyForNetwork()) return false;
    // With known STA credentials, prioritize router rejoin: RF channel hops in APSTA
    // interrupt association/DHCP and can wedge reconnect. ESP-NOW scan is only useful
    // when there are no credentials to recover with.
    if (haveWifiCredentials()) return false;
    if (!apEspNowStaleFor(AP_CHANNEL_SCAN_IDLE_SEC)) return false;
    if (!apClientIdleFor(AP_CHANNEL_SCAN_IDLE_SEC)) return false;

    if (_I_AM_SERVER) {
      return s_apLastChannelScanMillis == 0;
    }

    if (s_apLastChannelScanMillis == 0) return true;
    if (isTimeValid(I.apLastChannelScanTime) && isTimeValid((uint32_t)utcNow())) {
      return (utcNow() - I.apLastChannelScanTime) >= AP_CHANNEL_SCAN_IDLE_SEC;
    }
    return (millis() - s_apLastChannelScanMillis) >= (AP_CHANNEL_SCAN_IDLE_SEC * 1000UL);
  }
}

bool setWifiRfChannel(uint8_t channel) {
  if (channel < AP_WIFI_CHANNEL_MIN || channel > AP_WIFI_CHANNEL_MAX) return false;
  #ifdef _USE32
  if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    SerialPrint("setWifiRfChannel: failed to set channel " + String(channel), true);
    return false;
  }
  I.WifiChannel = channel;
  return true;
  #else
  (void)channel;
  return false;
  #endif
}

void noteApModeServerPingResponse(uint8_t wifiChannel) {
  if (!s_apChannelScanListen) return;
  if (wifiChannel < AP_WIFI_CHANNEL_MIN || wifiChannel > AP_WIFI_CHANNEL_MAX) return;
  s_apChannelScanGotResponse = true;
}

void enterAPStationMode() {
  if (softApRunning()) return;

  registerHTTPMessage("APStation");
  String wifiPWD;
  String wifiID;
  IPAddress apIP;

  SerialPrint("Init non-blocking AP Station Mode... ", false);

  // Start the setup network before any matrix or sensor init. A missing MAX7219
  // has no ACK, and blocking here would also skip BLE (it waits for the soft AP).
  connectSoftAP(&wifiID, &wifiPWD, &apIP);
  if (!softApRunning()) {
    SerialPrint("Failed to start soft AP", true);
    return;
  }

  if (initESPNOW() == 1) {
    SerialPrint("ArborysMesh initialized in AP mode", true);
  } else {
    SerialPrint("ArborysMesh init failed in AP mode", true);
  }

  server.begin();
  WiFi.setAutoReconnect(false);

  I.apLastClientActivity = 0;
  I.apLastChannelScanTime = 0;
  s_apLastChannelScanMillis = 0;
  s_apEnterMillis = millis();
  s_apLastReconnectMillis = millis();
  if (isTimeValid((uint32_t)utcNow())) {
    I.apModeEnteredTime = utcNow();
    // Defer first STA reconnect so soft-AP can stabilize (WiFi.begin can bounce AP briefly).
    I.apLastReconnectCheckTime = utcNow();
  } else {
    I.apModeEnteredTime = 0;
    I.apLastReconnectCheckTime = 0;
  }

  SerialPrint("AP Station ID: ", false);
  SerialPrint(wifiID, true);
  SerialPrint("AP Station Password: ", false);
  SerialPrint(wifiPWD, true);
  SerialPrint("AP Station IP: ", false);
  SerialPrint(apIP.toString(), true);

  maybeSendApModeEntryServerPing();

  #ifdef _USETFT
//  tftPrint("AP mode: join " + wifiID + " / " + wifiPWD + " -> http://" + apIP.toString(), true, TFT_YELLOW);
  #endif
}

void maybeExitAPStationMode() {
  // Mode bits only. softAPdisconnect() enables the AP while clearing it, so never call it
  // unless the driver already reports AP or AP+STA. A zero soft-AP IP still counts.
  const wifi_mode_t mode = WiFi.getMode();
  if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) return;
  // Soft-AP is the recovery surface while router STA is down — only tear it down
  // once STA is actually usable. (Previously this exited whenever setup was finalized,
  // which caused AP start/stop thrashing every loop while WiFi was failed.)
  if (!wifiReadyForNetwork()) return;
  if (!I.initialSetupFinalized) return;
  if (!initialSetupRequirementsMet()) return;

  if (I.initialSetupExitPending) {
    if (WiFi.softAPgetStationNum() > 0) return;
    if (!apClientIdleFor(3)) return;
    I.initialSetupExitPending = false;
  }

  exitAPStationMode();
}

void exitAPStationMode() {
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    WiFi.softAPdisconnect(true);
    SerialPrint("exitAPStationMode: soft AP stopped, STA active", true);
  }

  I.apLastReconnectCheckTime = 0;
  I.apLastClientActivity = 0;
  I.apModeEnteredTime = 0;
  I.apLastChannelScanTime = 0;
  s_apLastChannelScanMillis = 0;
  s_apEnterMillis = 0;
  s_apLastReconnectMillis = 0;
  s_apChannelScanListen = false;
  s_apChannelScanGotResponse = false;

  updateWifiChannel();
  WiFi.setAutoReconnect(true);

  #ifdef _USEUDP
  if (wifiReadyForNetwork()) {
    connectUDP();
  }
  #endif
}

void serviceAPStationMode() {
  if (!softApRunning()) return;

  ensureSoftApCredentials();
  maybeExitAPStationMode();
  if (!softApRunning()) return;

  static uint32_t lastHttpActivitySeen = 0;
  if (I.HTTP_LAST_INCOMINGMSG_TIME != lastHttpActivitySeen) {
    lastHttpActivitySeen = I.HTTP_LAST_INCOMINGMSG_TIME;
    if (isTimeValid((uint32_t)utcNow())) {
      I.apLastClientActivity = utcNow();
    }
  }

  if (shouldRunApChannelScan()) {
    runApModeChannelScan();
  }
}

uint32_t getApStationEnterMillis() {
  return s_apEnterMillis;
}

bool apStationUserActive() {
  if (!softApRunning()) return false;
  if (WiFi.softAPgetStationNum() > 0) return true;
  if (isTimeValid(I.apLastClientActivity) && isTimeValid((uint32_t)utcNow())
      && (utcNow() - I.apLastClientActivity) < 60) {
    return true;
  }
  // Fallback when wall clock is unset: treat recent HTTP (by millis) as activity.
  static uint32_t lastHttpStamp = 0;
  static uint32_t lastHttpActivityMs = 0;
  if (I.HTTP_LAST_INCOMINGMSG_TIME != lastHttpStamp) {
    lastHttpStamp = I.HTTP_LAST_INCOMINGMSG_TIME;
    lastHttpActivityMs = millis();
  }
  if (lastHttpActivityMs != 0 && (millis() - lastHttpActivityMs) < 60000UL) {
    return true;
  }
  return false;
}

// Helper function to URL encode strings
String urlEncode(const String& str) {
  String encoded = "";
  for (unsigned int i = 0; i < str.length(); i++) {
      char c = str.charAt(i);
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
          encoded += c;
      } else if (c == ' ') {
          encoded += '+';
      } else {
          encoded += '%';
          if (c < 16) {
              encoded += '0';
          }
          encoded += String(c, HEX);
      }
  }
  return encoded;
}


// ==================== STREAMLINED SETUP SYSTEM ====================
//
// This section provides a modern, streamlined setup experience with:
//
// 1. HELPER FUNCTIONS - Reusable logic for WiFi, location, and timezone
//    - connectToWiFi()              : Connect to WiFi network
//    - lookupLocationFromAddress()  : Get coordinates from address
//
// 2. API HANDLERS - RESTful JSON endpoints for AJAX calls
//    - POST /api/wifi               : Connect to WiFi (returns JSON)
//    - POST /api/location           : Lookup location (returns JSON)
//    - POST /api/timezone           : Save timezone (returns JSON)
//    - POST /api/complete-setup     : Complete setup (returns JSON)
//    - GET  /api/setup-status       : Get setup completion status (returns JSON)
//
// 3. SETUP WIZARD - Single-page guided setup interface
//    - GET /InitialSetup            : Interactive wizard with 3 steps
//      Step 1: WiFi Configuration
//      Step 2: Location Setup (optional)
//      Step 3: Timezone Configuration
//
// Benefits:
//   - Cleaner code separation
//   - Better user experience with wizard interface
//   - Reusable logic across handlers
//   - Modern AJAX-based interactions
//   - Backward compatibility maintained
//
// =====================================================================

/**
 * Helper: Connect to WiFi network
 * Returns: true if connection successful, false otherwise
 */
bool connectToWiFi(const String& ssid, const String& password, const String& lmk_key) {
  if (ssid.length() == 0) {
    SerialPrint("connectToWiFi: Empty SSID provided", true);
    return false;
  }
  if (password.length() == 0) {
    SerialPrint("connectToWiFi: Empty password provided", true);
    return false;
  }

    
  // Save LMK key if provided
  if (lmk_key.length() > 0) {
    memset(Prefs.KEYS.ESPNOW_KEY, 0, sizeof(Prefs.KEYS.ESPNOW_KEY));
    strncpy((char*)Prefs.KEYS.ESPNOW_KEY, lmk_key.c_str(), 16);
  }
  

  //save new wifi, pwd, and lmk_key to prefs
  snprintf((char*)Prefs.WIFISSID, sizeof(Prefs.WIFISSID), "%s", ssid.c_str());
  snprintf((char*)Prefs.WIFIPWD, sizeof(Prefs.WIFIPWD), "%s", password.c_str());
  snprintf((char*)Prefs.KEYS.ESPNOW_KEY, sizeof(Prefs.KEYS.ESPNOW_KEY), "%s", lmk_key.c_str());
  Prefs.HAVECREDENTIALS = true;
  Prefs.isUpToDate = false;
  // The saved BSSID belongs to the previous network. Scan for this SSID.
  s_wifiAnchorRtc.magic = 0;
  s_wifiAnchorRtc.valid = 0;
  s_bootAnchorTried = true;

  // SoftAP/HTTP path won — free BLE immediately so STA + ESP-NOW are not sharing the radio with BT.
  bleProvisionStop();

  // Attempt WiFi connection
  SerialPrint("Attempting WiFi connection to: " + ssid, true);

  connectWiFi(WIFI_BOOT_RETRY_LIMIT, WIFI_BOOT_TRY_MS);
  
  if (wifiReadyForNetwork()) {
    SerialPrint("WiFi connected! IP: " + WiFi.localIP().toString(), true);

    //store prefs and core now  
    handleStoreCoreData(); //update prefs and core now  
    
    return true;
  }
  
  SerialPrint("WiFi connection failed", true);
  if (!softApRunning()) {
    enterAPStationMode();
  } else {
    String wifiID;
    String wifiPWD;
    IPAddress apIP;
    connectSoftAP(&wifiID, &wifiPWD, &apIP);
  }
  return false;
}

/**
 * Helper: Lookup location coordinates from address
 * Returns: true if lookup successful, false otherwise
 */
bool lookupLocationFromAddress(const String& street, const String& city, const String& state, const String& zipcode, double* lat, double* lon) {
  // Validate inputs
  if (street.length() == 0 || city.length() == 0 || state.length() != 2 || zipcode.length() != 5) {
    SerialPrint("lookupLocationFromAddress: Invalid address parameters", true);
    return false;
  }
  
  // Validate ZIP code is numeric
  for (int i = 0; i < 5; i++) {
    if (!isdigit(zipcode.charAt(i))) {
      SerialPrint("lookupLocationFromAddress: ZIP code must be numeric", true);
      return false;
    }
  }
  
  // Check WiFi status
  if (!wifiReadyForNetwork()) {
    SerialPrint("lookupLocationFromAddress: WiFi not connected", true);
    return false;
  }
  
  // Call existing handler function
  bool result = handlerForWeatherAddress(street, city, state, zipcode);
  
  if (result && lat != nullptr && lon != nullptr) {
    *lat = Prefs.LATITUDE;
    *lon = Prefs.LONGITUDE;
    Prefs.isUpToDate = false;
    
    // Save to NVS
    BootSecure bootSecure;
    int8_t ret = bootSecure.setPrefs();
    if (ret < 0) {
      SerialPrint("lookupLocationFromAddress: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
    } else {
      SerialPrint("Location lookup successful and saved: " + String(*lat, 6) + ", " + String(*lon, 6), true);
    }
  }
  
  return result;
}


// ==================== API HANDLERS (JSON RESPONSES) ====================

/**
 * API: Connect to WiFi
 * POST /api/wifi
 * Parameters: ssid, password, lmk_key (optional)
 * Returns: JSON with status
 */
void apiConnectToWiFi() {
  registerHTTPMessage("API_WiFi");
  
  String ssid = "";
  String password = "";
  String lmk_key = "";
  String deviceName = "";
  
  if (server.hasArg("deviceName")) {
    deviceName = server.arg("deviceName");
    if (deviceName.length() > 0) {
      snprintf((char*)Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", deviceName.c_str());
      Prefs.isUpToDate = false; // Mark as needing to be saved
  
      //now update the DeviceStore with the new device name
      int16_t deviceIndex = Sensors.findMyDeviceIndex();
      if (!Sensors.updateDeviceName(deviceIndex, deviceName)) {
        SerialPrint("Failed to update device name", true);
        storeError("Failed to update device name");
      } 
    }
  
  }
  if (server.hasArg("ssid") ) {
    ssid = server.arg("ssid");
    if (ssid.length() == 0) {

      server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
      return;
    }
  }
  if (server.hasArg("password")) {
    password = server.arg("password");
  }
      
  if (server.hasArg("lmk_key")) {
    lmk_key = server.arg("lmk_key");
  }

  if (connectToWiFi(ssid, password, lmk_key)) {
    server.send(200, "application/json", "{\"success\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Connection failed\"}");
  }
  
}

/**
 * API: Lookup location from address
 * POST /api/location
 * Parameters: street, city, state, zipcode
 * Returns: JSON with lat/lon
 */
void apiLookupLocation() {
  registerHTTPMessage("API_Loc");
  
  String street = server.hasArg("street") ? server.arg("street") : "";
  String city = server.hasArg("city") ? server.arg("city") : "";
  String state = server.hasArg("state") ? server.arg("state") : "";
  String zipcode = server.hasArg("zipcode") ? server.arg("zipcode") : "";
  
  double lat = 0, lon = 0;
  bool success = lookupLocationFromAddress(street, city, state, zipcode, &lat, &lon);
  
  if (success) {
    String json = "{\"success\":true,\"latitude\":" + String(lat, 6) + ",\"longitude\":" + String(lon, 6) + "}";
    
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Location lookup failed\"}");
  }
}

// Recompute DST state and local time after timezone offset/DST fields change.
static void applyTimezoneToCurrentTime() {
  I.UTCTime = now();
  DSTsetup();
  I.currentTime = I.UTCTime + Prefs.TimeZoneOffset + (Prefs.DST > 0 ? (Prefs.DST - 1) * Prefs.DSTOffset : 0);
  I.currentSecond = second();
}

static void sendTimezoneDetectJson(bool success) {
  String dstStart = Prefs.DSTStartUnixTime ? dateify(Prefs.DSTStartUnixTime, "mm/dd/yyyy hh:nn") : "";
  String dstEnd = Prefs.DSTEndUnixTime ? dateify(Prefs.DSTEndUnixTime, "mm/dd/yyyy hh:nn") : "";
  String json = "{\"success\":" + String(success ? "true" : "false") +
                ",\"utc_offset\":" + String(Prefs.TimeZoneOffset) +
                ",\"dst_enabled\":" + String(Prefs.DST) +
                ",\"dst_offset\":" + String(Prefs.DSTOffset) +
                ",\"dst_start_date\":\"" + dstStart + "\"" +
                ",\"dst_end_date\":\"" + dstEnd + "\"}";
  server.send(200, "application/json", json);
}

/**
 * API: Auto-detect timezone
 * GET /api/timezone
 * Returns: JSON with timezone info
 */
void apiDetectTimezone() {
  registerHTTPMessage("API_TZ");
  
  bool success = getTimezoneInfo();
  if (success==false) {
    Prefs.TimeZoneOffset = 90000; //some arbitrarily large and impossible value
  } else {
    applyTimezoneToCurrentTime();
  }

  sendTimezoneDetectJson(success);
}

/**
 * API: Auto-detect DST rules only (does not change UTC offset)
 * GET /api/timezone/dst
 */
void apiDetectDST() {
  registerHTTPMessage("API_DST");

  bool success = getTimezoneInfo();
  if (success) {
    applyTimezoneToCurrentTime();
    Prefs.isUpToDate = false;
  }

  sendTimezoneDetectJson(success);
}

void apiSaveTimezone() {
  registerHTTPMessage("API_TZ");
  
  int32_t utc_offset = server.hasArg("utc_offset") ? server.arg("utc_offset").toInt() : 0;
  uint8_t dst_enabled = server.hasArg("dst_enabled") ? server.arg("dst_enabled").toInt() : 0;
  int32_t dst_offset = server.hasArg("dst_offset") ? server.arg("dst_offset").toInt() : 0;

  Prefs.TimeZoneOffset = utc_offset;
  Prefs.DST = dst_enabled; //note that 0=no DST here, 1=DST not active, 2=DST active
  if (server.hasArg("dst_start_date")) {
    Prefs.DSTStartUnixTime = convertStrTime(server.arg("dst_start_date"), false);
  }
  if (server.hasArg("dst_end_date")) {
    Prefs.DSTEndUnixTime = convertStrTime(server.arg("dst_end_date"), false);
  }
  Prefs.DSTOffset = dst_offset;
  Prefs.isUpToDate = false;

  applyTimezoneToCurrentTime();

  BootSecure bootSecure;
  int8_t ret = bootSecure.setPrefs(true);

  bool saved = (ret > 0);
  String json = "{\"success\":" + String(saved ? "true" : "false") +
                ",\"message\":\"" + String(saved ? "Timezone settings saved" : "Failed to save timezone to NVS") + "\"}";
  server.send(200, "application/json", json);
  
  if (saved) {
    SerialPrint("Timezone settings saved: UTC offset = " + String(utc_offset), true);
  } else {
    SerialPrint("Timezone settings save failed, setPrefs returned " + String(ret), true);
  }
}

void handleSNS_READ_NOW() {
  #if _HAS_LOCAL_SENSORS
  registerHTTPMessage("API_SNS_READ_NOW");
  int16_t snsType = server.hasArg("snsType") ? server.arg("snsType").toInt() : 0;
  int16_t snsID = server.hasArg("snsID") ? server.arg("snsID").toInt() : 0;
  int16_t snsIndex = Sensors.findMySensorBySnsTypeAndID(snsType, snsID);
  ArborysSnsType* sensor = Sensors.snsIndexToPointer(snsIndex);
  double value = 0;
  if (sensor == nullptr) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Sensor not found\"}");
    return;
  }
  int8_t success = ReadData(sensor, true, true);
  if (success>0) {
    value = sensor->snsValue;
    server.send(200, "application/json", "{\"success\":true,\"value\":" + String(value) + "}");
  } else {
    server.send(200, "application/json", "{\"success\":false,\"value\":" + String(value) + "}");
  }
#endif
}

/**
 * API: Get setup status
 * GET /api/setup-status
 * Returns: JSON with current setup completion status
 */
void apiGetSetupStatus() {
  registerHTTPMessage("API_Setup");

  bool wifistatus = wifiReadyForNetwork();
  bool wifi_configured = haveWifiCredentials();
  bool location_configured = (Prefs.LATITUDE != 0 || Prefs.LONGITUDE != 0);
  bool timezone_configured = (Prefs.TimeZoneOffset <= 50400);
  bool setup_complete = initialSetupRequirementsMet() && I.initialSetupFinalized;
  
  String json = "{\"wifi_configured\":" + String(wifi_configured ? "true" : "false") +
                ",\"wifi_connected\":" + String(wifistatus ? "true" : "false") +
                ",\"location_configured\":" + String(location_configured ? "true" : "false") +
                ",\"timezone_configured\":" + String(timezone_configured ? "true" : "false") +
                ",\"setup_complete\":" + String(setup_complete ? "true" : "false");
  #if _SUPABASE_RUNTIME
  json += ",\"supabase_claimed\":" + String(supabaseHasStoredCredentials() ? "true" : "false");
  json += ",\"supabase_connected\":" + String(supabaseIsConnected() ? "true" : "false");
  json += ",\"site_label\":\"";
  if (supabaseHasStoredCredentials()) json += supabaseSiteSlug();
  json += "\"";
  json += ",\"site_slug\":\"";
  if (supabaseHasStoredCredentials()) json += supabaseSiteSlug();
  json += "\"";
  #endif
  
  if (wifi_configured) {
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  }
  if (location_configured) {
    json += ",\"latitude\":" + String(Prefs.LATITUDE, 6) + ",\"longitude\":" + String(Prefs.LONGITUDE, 6);
  }
  if (timezone_configured) {
    json += ",\"utc_offset\":" + String(Prefs.TimeZoneOffset);
  }
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleApiCompleteSetup() {
  registerHTTPMessage("API_OK");

  if (!initialSetupRequirementsMet()) {
    String missing = "";
    if (!haveWifiCredentials()) missing += "WiFi credentials; ";
    #ifdef _USEWEATHER
    if (Prefs.LATITUDE == 0 && Prefs.LONGITUDE == 0) missing += "location; ";
    #endif
    if (Prefs.TimeZoneOffset > 50400) missing += "timezone; ";
    server.send(400, "application/json",
      "{\"success\":false,\"error\":\"Setup incomplete: " + missing + "required before finishing\"}");
    return;
  }

  I.initialSetupFinalized = true;
  I.initialSetupExitPending = true;
  storeCoreData(true);

  #ifdef _USETFT
  tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
  tft.setCursor(0, tft.height()-200);
  tft.setTextColor(TFT_WHITE);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.println("Setup complete. Rebooting...");
  #endif

  server.send(200, "application/json",
    "{\"success\":true,\"message\":\"Setup is complete. The device will reboot now to refresh weather data and the display.\"}");

  delay(250);
  controlledReboot("Initial setup complete", RESET_NEWWIFI, true);
}

#if _SUPABASE_RUNTIME
// ArborysNet location fields (UI: site label / site description).
#ifndef ARBORYSNET_SITE_LABEL_MAX
#define ARBORYSNET_SITE_LABEL_MAX 24
#endif
#ifndef ARBORYSNET_SITE_DESC_MAX
#define ARBORYSNET_SITE_DESC_MAX 64
#endif

/** Normalize to site label: lowercase a-z0-9_-, max 24, default "home". */
static String arborysNetNormalizeLabel(const String& raw) {
  String out;
  out.reserve(ARBORYSNET_SITE_LABEL_MAX);
  for (size_t i = 0; i < (size_t)raw.length() && out.length() < ARBORYSNET_SITE_LABEL_MAX; ++i) {
    char c = raw.charAt(i);
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
      if (out.length() == 0 && (c == '_' || c == '-')) continue;
      out += c;
    }
  }
  if (out.length() == 0) out = "home";
  return out;
}

/** Trim + cap site description at 64 chars. */
static String arborysNetNormalizeDescription(const String& raw, const String& fallbackLabel) {
  String out = raw;
  out.trim();
  if (out.length() == 0) out = fallbackLabel;
  if (out.length() > ARBORYSNET_SITE_DESC_MAX) {
    out = out.substring(0, ARBORYSNET_SITE_DESC_MAX);
  }
  return out;
}

/** Persist SITE_SLUG after an assign/ensure during setup. */
static void supabasePersistSiteSlug(const char* slug) {
  if (!slug || !slug[0]) slug = "home";
  strncpy(Prefs.SITE_SLUG, slug, sizeof(Prefs.SITE_SLUG) - 1);
  Prefs.SITE_SLUG[sizeof(Prefs.SITE_SLUG) - 1] = '\0';
  Prefs.isUpToDate = false;
  BootSecure boot;
  boot.setPrefs(true);

  SupabaseConfig cfg = Supabase.config();
  strncpy(cfg.siteSlug, Prefs.SITE_SLUG, sizeof(cfg.siteSlug) - 1);
  cfg.siteSlug[sizeof(cfg.siteSlug) - 1] = '\0';
  Supabase.begin(cfg);
}

static String supabaseSitesArrayJson(const SupabaseSiteDto* sites, uint16_t count) {
  String json = "[";
  for (uint16_t i = 0; i < count; i++) {
    if (i) json += ",";
    const char* label = sites[i].slug[0] ? sites[i].slug : "home";
    const char* desc = sites[i].name[0] ? sites[i].name : label;
    json += "{\"id\":\"";
    json += sites[i].id;
    json += "\",\"label\":\"";
    json += label;
    json += "\",\"description\":\"";
    json += desc;
    json += "\"}";
  }
  json += "]";
  return json;
}

// Keep off the stack � WiFiClientSecure TLS already uses a large stack frame.
static SupabaseSiteDto s_setupSites[ARBORYSNET_MAX_SITES];

// TLS mint+listSites must not run on the WebServer/loop stack (mbedtls overflows
// ~8KB and freezes the LAN server). Worker + pending poll keeps handleClient responsive.
enum : uint8_t { SB_SITES_IDLE = 0, SB_SITES_BUSY = 1, SB_SITES_OK = 2, SB_SITES_ERR = 3 };
struct ArbSitesJob {
  volatile uint8_t state;
  bool autoAssign;
  bool invalidDevice;
  char err[96];
  // Enough for 10 sites with full id/label/description payloads.
  char json[4096];
};
static ArbSitesJob s_arbSitesJob = {};
static TaskHandle_t s_arbSitesTask = nullptr;

/** JSON error for ArborysNet APIs; clears NVS claim only on confirmed invalid_device. */
static String supabaseApiFailJson(const char* fallbackMsg) {
  String err = Supabase.lastErrorMessage();
  if (err.length() == 0) err = Supabase.lastErrorCode();
  if (err.length() == 0 && fallbackMsg) err = fallbackMsg;
  const bool invalid = supabaseClearClaimIfInvalidDevice();
  String out = "{\"success\":false,\"error\":\"";
  out += err;
  out += "\"";
  if (Supabase.lastErrorCode()[0]) {
    out += ",\"code\":\"";
    out += Supabase.lastErrorCode();
    out += "\"";
  }
  if (invalid) out += ",\"invalid_device\":true";
  out += "}";
  return out;
}

static bool supabaseRefreshSitesForSetup(uint16_t* countOut, bool autoAssignIfSingle,
                                         bool* autoAssignedOut);

static void arbSitesWorkerTask(void* /*arg*/) {
  uint16_t count = 0;
  bool autoAssigned = false;
  esp_task_wdt_reset();
  s_arbSitesJob.invalidDevice = false;
  if (!supabaseRefreshSitesForSetup(&count, s_arbSitesJob.autoAssign, &autoAssigned)) {
    String err = Supabase.lastErrorMessage();
    if (err.length() == 0) err = Supabase.lastErrorCode();
    if (err.length() == 0) err = "locations fetch failed";
    // Include code so EmptyInput/auth vs HTTP failures are distinguishable in the UI.
    if (Supabase.lastErrorCode()[0] && err.indexOf(Supabase.lastErrorCode()) < 0) {
      err = String(Supabase.lastErrorCode()) + ": " + err;
    }
    strncpy(s_arbSitesJob.err, err.c_str(), sizeof(s_arbSitesJob.err) - 1);
    s_arbSitesJob.err[sizeof(s_arbSitesJob.err) - 1] = '\0';
    if (supabaseClearClaimIfInvalidDevice()) {
      s_arbSitesJob.invalidDevice = true;
    }
    arborysNetLogClientError("locations fetch", ERROR_ARBORYSNET_SYNC);
    s_arbSitesJob.state = SB_SITES_ERR;
  } else {
    {
      char ev[48];
      snprintf(ev, sizeof(ev), "locations fetched (%u)", (unsigned)count);
      arborysNetLogEvent(ev, EVENT_ARBORYSNET_LOCATION);
    }
    String json = "{\"success\":true,\"current\":\"";
    json += supabaseSiteSlug();
    json += "\",\"site_auto_selected\":";
    json += (autoAssigned || (s_arbSitesJob.autoAssign && count <= 1)) ? "true" : "false";
    json += ",\"sites\":";
    json += supabaseSitesArrayJson(s_setupSites, count);
    json += "}";
    if (json.length() >= sizeof(s_arbSitesJob.json)) {
      strncpy(s_arbSitesJob.err, "sites response too large", sizeof(s_arbSitesJob.err) - 1);
      s_arbSitesJob.err[sizeof(s_arbSitesJob.err) - 1] = '\0';
      s_arbSitesJob.state = SB_SITES_ERR;
    } else {
      memcpy(s_arbSitesJob.json, json.c_str(), json.length() + 1);
      s_arbSitesJob.state = SB_SITES_OK;
    }
  }
  s_arbSitesTask = nullptr;
  vTaskDelete(nullptr);
}

/**
 * List sites for the claimed user (max 10). If none exist, create+assign "home".
 * When autoAssignIfSingle and exactly one site, assign this device to it.
 * CONFIG page must call with autoAssignIfSingle=false (list only).
 */
static bool supabaseRefreshSitesForSetup(uint16_t* countOut, bool autoAssignIfSingle,
                                         bool* autoAssignedOut) {
  if (countOut) *countOut = 0;
  if (autoAssignedOut) *autoAssignedOut = false;
  if (!countOut) return false;

  supabaseBeginFromPrefs();
  Supabase.setUtcOffset(Prefs.TimeZoneOffset);
  esp_task_wdt_reset();

  if (Supabase.listSites(s_setupSites, ARBORYSNET_MAX_SITES, countOut) != SupabaseError::Ok) {
    return false;
  }
  esp_task_wdt_reset();

  // Only mutate cloud state when the user has zero sites (Home must always exist).
  if (*countOut == 0) {
    if (Supabase.setDeviceSite("home", "home") != SupabaseError::Ok) {
      return false;
    }
    esp_task_wdt_reset();
    supabasePersistSiteSlug("home");
    if (autoAssignedOut) *autoAssignedOut = true;
    if (Supabase.listSites(s_setupSites, ARBORYSNET_MAX_SITES, countOut) != SupabaseError::Ok) {
      return false;
    }
    if (*countOut == 0) {
      memset(&s_setupSites[0], 0, sizeof(s_setupSites[0]));
      strncpy(s_setupSites[0].slug, "home", sizeof(s_setupSites[0].slug) - 1);
      strncpy(s_setupSites[0].name, "home", sizeof(s_setupSites[0].name) - 1);
      *countOut = 1;
    }
  } else if (autoAssignIfSingle && *countOut == 1) {
    const char* slug = s_setupSites[0].slug[0] ? s_setupSites[0].slug : "home";
    const char* name = s_setupSites[0].name[0] ? s_setupSites[0].name : slug;
    // Skip extra TLS if already on this site locally.
    if (strcmp(supabaseSiteSlug(), slug) == 0) {
      if (autoAssignedOut) *autoAssignedOut = true;
    } else if (Supabase.setDeviceSite(slug, name) == SupabaseError::Ok) {
      esp_task_wdt_reset();
      supabasePersistSiteSlug(slug);
      if (autoAssignedOut) *autoAssignedOut = true;
    }
  }

  return true;
}

void apiSupabaseClaim() {
  registerHTTPMessage("API_SB_Claim");
  if (!wifiReadyForNetwork()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"WiFi required to claim\"}");
    return;
  }
  String claimCode = server.hasArg("claim_code") ? server.arg("claim_code") : "";
  if (claimCode.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"claim_code required\"}");
    return;
  }

  // Keep this handler short: claim + persist only. Site mint/list is a separate
  // /api/arborysnet/sites call so the browser does not sit through multiple TLS
  // round-trips (that was causing "Failed to fetch" / dropped connections).
  SupabaseConfig cfg;
  cfg.clear();
  cfg.applyDefaults();
  SupabaseClient::macToString(ESP.getEfuseMac(), cfg.deviceMac);
  cfg.utcOffsetSec = Prefs.TimeZoneOffset;
  Supabase.begin(cfg);

  esp_task_wdt_reset();
  if (Supabase.claimDevice(claimCode.c_str()) != SupabaseError::Ok) {
    String err = Supabase.lastErrorMessage();
    if (err.length() == 0) err = Supabase.lastErrorCode();
    arborysNetLogClientError("claim failed", ERROR_ARBORYSNET_CLAIM);
    server.send(400, "application/json",
                "{\"success\":false,\"error\":\"" + err + "\"}");
    return;
  }
  esp_task_wdt_reset();

  if (!supabasePersistClaimedPrefs()) {
    arborysNetStoreError("claim persist failed", ERROR_ARBORYSNET_CLAIM);
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Failed to save credentials\"}");
    return;
  }

  arborysNetLogEvent("device claimed", EVENT_ARBORYSNET_CLAIMED);

  String json = "{\"success\":true,\"claimed\":true,\"site_slug\":\"";
  json += supabaseSiteSlug();
  json += "\",\"user_id\":\"";
  json += Prefs.SUPABASE_USER_ID;
  json += "\"}";
  server.send(200, "application/json", json);
}

void apiSupabaseQuit() {
  registerHTTPMessage("API_SB_Quit");
  const bool hadCreds = supabaseHasStoredCredentials() || Prefs.SUPABASE_CLAIMED;
  supabaseQuitArborysNet();
  String json = "{\"success\":true,\"quit\":true,\"was_claimed\":";
  json += hadCreds ? "true" : "false";
  json += "}";
  server.send(200, "application/json", json);
}

void apiSupabaseUploadToggle() {
  registerHTTPMessage("API_SB_Upload");
  if (!server.hasArg("enabled")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"enabled required\"}");
    return;
  }
  const String v = server.arg("enabled");
  Prefs.UPLOAD_TO_SUPABASE =
      (v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "yes");
  Prefs.isUpToDate = false;
  BootSecure boot;
  boot.setPrefs(true);
  String json = "{\"success\":true,\"upload_to_supabase\":";
  json += Prefs.UPLOAD_TO_SUPABASE ? "true" : "false";
  json += "}";
  server.send(200, "application/json", json);
}

void apiSupabaseSites() {
  registerHTTPMessage("API_SB_Sites");
  if (!supabaseHasStoredCredentials()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Device not claimed\"}");
    return;
  }
  if (!wifiReadyForNetwork()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"WiFi required\"}");
    return;
  }

  // auto_assign=1: used by setup wizard. CONFIG page must NOT auto-assign �
  // that added extra TLS handshakes and was resetting the hub on /CONFIG.
  const bool autoAssign =
      server.hasArg("auto_assign") &&
      (server.arg("auto_assign") == "1" || server.arg("auto_assign") == "true");

  if (s_arbSitesJob.state == SB_SITES_BUSY) {
    server.send(200, "application/json", "{\"success\":false,\"pending\":true}");
    return;
  }
  if (s_arbSitesJob.state == SB_SITES_OK) {
    server.send(200, "application/json", s_arbSitesJob.json);
    s_arbSitesJob.state = SB_SITES_IDLE;
    return;
  }
  if (s_arbSitesJob.state == SB_SITES_ERR) {
    String out = "{\"success\":false,\"error\":\"";
    out += s_arbSitesJob.err;
    out += "\"";
    if (s_arbSitesJob.invalidDevice) out += ",\"invalid_device\":true";
    out += "}";
    server.send(400, "application/json", out);
    s_arbSitesJob.state = SB_SITES_IDLE;
    return;
  }

  // Shared Supabase TLS in use (boot Auth/Ping/Query, expired poll, etc.) �
  // tell the browser immediately instead of starting a worker that will tls_busy.
  if (SupabaseClient::isTlsBusy()) {
    const uint32_t heldMs = SupabaseClient::tlsHeldForMs();
    String out = "{\"success\":false,\"tls_busy\":true,\"held_ms\":";
    out += String(heldMs);
    out += ",\"error\":\"Please wait, TLS client occupied";
    if (heldMs > 0) {
      out += " (";
      out += String(heldMs / 1000);
      out += "s)";
    }
    out += "\"}";
    server.send(200, "application/json", out);
    return;
  }

  s_arbSitesJob.autoAssign = autoAssign;
  s_arbSitesJob.err[0] = '\0';
  s_arbSitesJob.json[0] = '\0';
  s_arbSitesJob.state = SB_SITES_BUSY;

  BaseType_t ok = xTaskCreatePinnedToCore(
      arbSitesWorkerTask, "arbSites", 32768, nullptr, 1, &s_arbSitesTask, 1);
  if (ok != pdPASS) {
    s_arbSitesJob.state = SB_SITES_IDLE;
    s_arbSitesTask = nullptr;
    server.send(500, "application/json",
                "{\"success\":false,\"error\":\"Failed to start locations worker\"}");
    return;
  }
  server.send(200, "application/json", "{\"success\":false,\"pending\":true}");
}

static void apiSupabaseAssignSiteInternal(bool allowCreate) {
  if (!supabaseHasStoredCredentials()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Device not claimed on ArborysNet\"}");
    return;
  }
  if (!wifiReadyForNetwork()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"WiFi required\"}");
    return;
  }
  String rawLabel = server.hasArg("site_label") ? server.arg("site_label")
                    : (server.hasArg("site") ? server.arg("site") : "");
  String rawDesc = server.hasArg("site_description") ? server.arg("site_description")
                   : (server.hasArg("site_name") ? server.arg("site_name") : "");
  String label = arborysNetNormalizeLabel(rawLabel);
  String desc = arborysNetNormalizeDescription(rawDesc, label);
  if (rawLabel.length() == 0) {
    server.send(400, "application/json",
                "{\"success\":false,\"error\":\"site label required\"}");
    return;
  }

  supabaseBeginFromPrefs();
  Supabase.setUtcOffset(Prefs.TimeZoneOffset);

  if (SupabaseClient::isTlsBusy()) {
    server.send(200, "application/json",
                "{\"success\":false,\"tls_busy\":true,\"error\":\"Please wait, TLS client occupied\"}");
    return;
  }

  // Firmware-side cap when creating a new location (DB also enforces after SQL applied).
  if (allowCreate) {
    uint16_t existing = 0;
    if (Supabase.listSites(s_setupSites, ARBORYSNET_MAX_SITES, &existing) == SupabaseError::Ok) {
      bool known = false;
      for (uint16_t i = 0; i < existing; i++) {
        if (strcmp(s_setupSites[i].slug, label.c_str()) == 0) { known = true; break; }
      }
      if (!known && existing >= ARBORYSNET_MAX_SITES) {
        server.send(400, "application/json",
                    "{\"success\":false,\"error\":\"Maximum 10 locations per account\"}");
        return;
      }
    }
  }

  if (Supabase.setDeviceSite(label.c_str(), desc.c_str()) != SupabaseError::Ok) {
    arborysNetLogClientError(allowCreate ? "create location" : "assign location",
                             ERROR_ARBORYSNET_SYNC);
    server.send(400, "application/json", supabaseApiFailJson("location update failed"));
    return;
  }

  strncpy(Prefs.SITE_SLUG, label.c_str(), sizeof(Prefs.SITE_SLUG) - 1);
  Prefs.SITE_SLUG[sizeof(Prefs.SITE_SLUG) - 1] = '\0';
  Prefs.isUpToDate = false;
  BootSecure boot;
  boot.setPrefs(true);

  {
    char ev[72];
    snprintf(ev, sizeof(ev), "%s location %s", allowCreate ? "created" : "assigned",
             Prefs.SITE_SLUG);
    arborysNetLogEvent(ev, EVENT_ARBORYSNET_LOCATION);
  }

  String json = "{\"success\":true,\"label\":\"";
  json += Prefs.SITE_SLUG;
  json += "\",\"description\":\"";
  json += desc;
  json += "\",\"created\":";
  json += allowCreate ? "true" : "false";
  json += "}";
  server.send(200, "application/json", json);
}

void apiSupabaseSite() {
  registerHTTPMessage("API_SB_Site");
  apiSupabaseAssignSiteInternal(false);
}

#if _IS_SERVER_HUB
void apiSupabaseSiteCreate() {
  registerHTTPMessage("API_SB_SiteCreate");
  apiSupabaseAssignSiteInternal(true);
}

void apiSupabaseSiteDelete() {
  registerHTTPMessage("API_SB_SiteDel");
  if (!supabaseHasStoredCredentials()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Device not claimed\"}");
    return;
  }
  if (!wifiReadyForNetwork()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"WiFi required\"}");
    return;
  }
  String rawLabel = server.hasArg("site_label") ? server.arg("site_label")
                    : (server.hasArg("site") ? server.arg("site") : "");
  String site = arborysNetNormalizeLabel(rawLabel);
  if (rawLabel.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"site label required\"}");
    return;
  }

  supabaseBeginFromPrefs();
  Supabase.setUtcOffset(Prefs.TimeZoneOffset);

  if (Supabase.deleteSite(site.c_str()) != SupabaseError::Ok) {
    arborysNetLogClientError("delete location", ERROR_ARBORYSNET_SYNC);
    server.send(400, "application/json", supabaseApiFailJson("delete failed"));
    return;
  }

  // Refresh this hub's site in case it was on the deleted site
  char cloudSite[33];
  if (Supabase.fetchOwnSite(cloudSite, sizeof(cloudSite)) == SupabaseError::Ok && cloudSite[0]) {
    if (strcmp(Prefs.SITE_SLUG, cloudSite) != 0) {
      strncpy(Prefs.SITE_SLUG, cloudSite, sizeof(Prefs.SITE_SLUG) - 1);
      Prefs.SITE_SLUG[sizeof(Prefs.SITE_SLUG) - 1] = '\0';
      Prefs.isUpToDate = false;
      BootSecure boot;
      boot.setPrefs(true);
      SupabaseConfig cfg = Supabase.config();
      strncpy(cfg.siteSlug, cloudSite, sizeof(cfg.siteSlug) - 1);
      cfg.siteSlug[sizeof(cfg.siteSlug) - 1] = '\0';
      Supabase.begin(cfg);
    }
  }

  {
    char ev[64];
    snprintf(ev, sizeof(ev), "deleted location %s", site.c_str());
    arborysNetLogEvent(ev, EVENT_ARBORYSNET_LOCATION);
  }

  String json = "{\"success\":true,\"deleted\":\"";
  json += site;
  json += "\",\"current\":\"";
  json += supabaseSiteSlug();
  json += "\"}";
  server.send(200, "application/json", json);
}

void apiSupabaseInventory() {
  registerHTTPMessage("API_SB_Inv");
  SupabaseHubInventoryResult r;
  if (!supabaseHubInventorySync(&r)) {
    // Prefer live client error (may be invalid_device) over summary buffer.
    if (Supabase.lastErrorCode()[0] || Supabase.lastErrorMessage()[0]) {
      arborysNetLogClientError("inventory", ERROR_ARBORYSNET_SYNC);
      server.send(400, "application/json", supabaseApiFailJson("inventory failed"));
    } else {
      String err = r.error[0] ? String(r.error) : "inventory failed";
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"" + err + "\"}");
    }
    return;
  }
  String json = "{\"success\":true,\"site\":\"";
  json += supabaseSiteSlug();
  json += "\",\"sensors_queried\":";
  json += String(r.sensorsQueried);
  json += ",\"sensors_added\":";
  json += String(r.sensorsAdded);
  json += ",\"devices_added\":";
  json += String(r.devicesAdded);
  json += "}";
  server.send(200, "application/json", json);
}
#endif
#endif // _SUPABASE_RUNTIME

/**
 * API: Scan for WiFi networks
 * GET /api/wifi-scan
 * Returns: JSON array of available WiFi networks
 */
void apiScanWiFi() {
  registerHTTPMessage("API_Scan");

  SerialPrint("Scanning for WiFi networks...", true);
  #ifdef _USETFT
    tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
    tft.setCursor(0, tft.height()-200);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.println("Scanning networks...");
    tft.setTextColor(TFT_WHITE);
    #endif

  // Stay in the current mode. If the scan drops a working STA, rejoin that same AP.
  // Restore the portal only after the results are copied out; restarting the AP
  // clears the scan list.
  const bool apWasUp = softApRunning();
  StaAnchor scanPrior;
  int numNetworks = scanNetworksKeepSta(&scanPrior);

  // Wait for scan to complete (if async was false, this should be immediate)
  if (numNetworks < 0) {
    // Scan might be in progress, wait a bit
    delay(2000);
    numNetworks = WiFi.scanComplete();
  }

  // Build JSON array of networks
  String json = "{\"success\":true,\"networks\":[";
  String lastnetwork = "";
  byte count=0;
  for (int i = 0; i < numNetworks; i++) {
    if (WiFi.SSID(i)!=lastnetwork) {
      lastnetwork = WiFi.SSID(i);
      if (count>0) json += ",";
      count++;
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\"";
      json += ",\"rssi\":" + String(WiFi.RSSI(i));
      json += ",\"encryption\":" + String(WiFi.encryptionType(i));
      json += "}";      
    }
    
  }
  json += "]}";
  
  // Clean up scan results, then put the portal back on channel 1 with its password.
  WiFi.scanDelete();
  if (apWasUp) {
    String wifiID;
    String wifiPWD;
    IPAddress apIP;
    connectSoftAP(&wifiID, &wifiPWD, &apIP);
  } else {
    restoreAnchorIfDropped(scanPrior);
  }

  if (numNetworks == 0 || count == 0) {
    #ifdef _USETFT
    tft.setTextColor(TFT_RED);
    tft.println("No networks found. Enter Manually.");
    tft.setTextColor(TFT_WHITE);
    #endif
    server.send(200, "application/json", "{\"success\":true,\"networks\":[]}");
  } else {
    SerialPrint("Found " + String(count) + " WiFi networks", true);
    #ifdef _USETFT
    tft.setTextColor(TFT_GREEN);
    tft.printf("%d networks found.\n",count);
    tft.setTextColor(TFT_WHITE);
    #endif
    server.send(200, "application/json", json);
  }
}

/**
 * API: Clear WiFi credentials and disconnect station
 * POST /api/clear-wifi
 * Returns: JSON response indicating success or failure
 */
void apiClearWiFi() {
  registerHTTPMessage("API_ClearWiFi");
  
  SerialPrint("Clearing WiFi credentials and disconnecting station...", true);
  
  #ifdef _USE32
    // Ensure we're in AP+STA mode so AP remains active
    wifi_mode_t currentMode = WiFi.getMode();
    if (currentMode != WIFI_MODE_APSTA) {
      WiFi.mode(WIFI_MODE_APSTA);
      delay(100);
      SerialPrint("Switched to AP+STA mode", true);
    }
    
    // Disconnect from station WiFi (but keep AP running)
    WiFi.disconnect(false); // false = don't erase credentials from flash
    delay(500);
    SerialPrint("Disconnected from station WiFi", true);
  #else
    // For ESP8266, disconnect station
    WiFi.disconnect();
    delay(500);
  #endif
  
  // Clear WiFi credentials from Prefs
  memset(Prefs.WIFISSID, 0, sizeof(Prefs.WIFISSID));
  memset(Prefs.WIFIPWD, 0, sizeof(Prefs.WIFIPWD));
  Prefs.HAVECREDENTIALS = false;
  Prefs.isUpToDate = false;
  I.initialSetupFinalized = false;
  I.initialSetupExitPending = false;
  
  // Save preferences
  BootSecure bootSecure;
  int8_t ret = bootSecure.setPrefs();
  
  if (ret == 0) {
    SerialPrint("WiFi credentials cleared and saved successfully", true);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi credentials cleared. Device disconnected from network but remains accessible via AP.\"}");
  } else {
    SerialPrint("Failed to save cleared credentials: " + String(ret), true);
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Failed to save preferences: " + String(ret) + "\"}");
  }
}

// ==================== SETUP WIZARD PAGE ====================

/**
 * Initial Setup Wizard - Single-page guided setup
 * GET /InitialSetup
 */
void handleInitialSetup() {

  registerHTTPMessage("Init");
  WEBHTML = "";
  serverTextHeader("Initial Setup");
  serverTextStreamBegin(200, true);

  if (hardwareFaultMask() != 0) {
    char failed[96];
    hardwareFaultSummary(failed, sizeof(failed));
    WEBHTML += "<div style=\"max-width:900px;margin:12px auto;padding:12px 16px;background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;border-radius:4px;font-weight:bold;\">";
    WEBHTML += failed;
    WEBHTML += " failed. The system will boot into error mode and will not send sensor data.</div>";
    serverTextFlush(true);
  }
  
  serverTextAppend(R"===(
<style>
  .setup-container { max-width: 900px; margin: 20px auto; padding: 20px; font-family: Arial, sans-serif; }
  .setup-step { background: #f8f9fa; border: 2px solid #dee2e6; border-radius: 8px; padding: 20px; margin: 15px 0; }
  .setup-step.active { border-color: #2196F3; background: #e3f2fd; }
  .setup-step.completed { border-color: #4CAF50; background: #e8f5e8; }
  .step-header { display: flex; align-items: center; margin-bottom: 15px; cursor: pointer; }
  .step-number { width: 35px; height: 35px; border-radius: 50%; background: #ccc; color: white; display: flex; align-items: center; justify-content: center; font-weight: bold; margin-right: 15px; }
  .active .step-number { background: #2196F3; }
  .completed .step-number { background: #4CAF50; }
  .completed .step-number::after { content: "x"; }
  .step-title { font-size: 18px; font-weight: bold; flex-grow: 1; }
  .step-content { display: none; padding-left: 50px; }
  .active .step-content, .completed .step-content { display: block; }
  .form-group { margin: 15px 0; }
  .form-group label { display: block; margin-bottom: 5px; font-weight: bold; }
  .form-group input, .form-group select { width: 100%; max-width: 400px; padding: 10px; border: 1px solid #ccc; border-radius: 4px; font-size: 14px; }
  .btn { padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 5px; }
  .btn-primary { background: #4CAF50; color: white; }
  .btn-secondary { background: #2196F3; color: white; }
  .btn-danger { background: #f44336; color: white; }
  .btn:disabled { background: #ccc; cursor: not-allowed; }
  .status-message { padding: 12px; border-radius: 4px; margin: 10px 0; display: none; }
  .status-success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
  .status-error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  .status-info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
  .spinner { border: 3px solid #f3f3f3; border-top: 3px solid #3498db; border-radius: 50%; width: 20px; height: 20px; animation: spin 1s linear infinite; display: inline-block; margin-left: 10px; }
  @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
</style>

<body>
)===");
  // Allow navigation to Main/Status/etc. for intentional no-WiFi (ESP-NOW) operation.
  appendStandardPageNav();
  WEBHTML += R"===(
<div class="setup-container">
  <p>Welcome! Let's configure your system.</p>
  )===";
  serverTextFlush(true);

  CheckWifiStatus(WIFI_CHECK_NORMAL);
  {
    const wifi_mode_t mode = WiFi.getMode();
    const char* modeLabel = "OFF";
    if (mode == WIFI_MODE_STA) modeLabel = "STA mode";
    else if (mode == WIFI_MODE_AP) modeLabel = "AP mode";
    else if (mode == WIFI_MODE_APSTA) modeLabel = "AP+STA mode";

    String ssid = WiFi.SSID();
    if (ssid.length() == 0 && Prefs.WIFISSID[0] != 0) ssid = (const char*)Prefs.WIFISSID;

    const IPAddress ip = wifiReadyForNetwork() ? WiFi.localIP() : IPAddress(0, 0, 0, 0);

    WEBHTML += "<p>WiFi: ";
    WEBHTML += modeLabel;
    WEBHTML += "<br>SSID: ";
    WEBHTML += ssid;
    WEBHTML += "<br>IP: ";
    WEBHTML += ip.toString();
    WEBHTML += "<br>RSSI: ";
    WEBHTML += formatRssiHtml(WiFi.RSSI());
    WEBHTML += "<br>Location: ";
    WEBHTML += (Prefs.LATITUDE != 0 || Prefs.LONGITUDE != 0) ? "saved" : "none";
    WEBHTML += "<br>Timezone: ";
    WEBHTML += (Prefs.TimeZoneOffset <= 50400) ? "saved" : "not set";
    WEBHTML += "</p>";
  }
  WEBHTML += "<button class=\"btn btn-danger\" onclick=\"clearWiFiCredentials()\">Clear credentials</button>";

  serverTextAppend(R"===( 
  <!-- Step 1: WiFi Configuration -->
  <div class="setup-step" id="step1">
    <div class="step-header" onclick="toggleStep('step1')">
      <div class="step-number">1</div>
      <div class="step-title">WiFi Configuration</div>
    </div>
    <div class="step-content">
      <p>Connect your device to your WiFi network.</p>
      <div id="wifi-status" class="status-message"></div>
      
      <div class="form-group">
        <label for="ssid">Network Name (SSID) *</label>
        <input type="text" id="ssid" list="ssid-list" placeholder="Select or enter your WiFi network name" autocomplete="off">
        <datalist id="ssid-list">
          <!-- WiFi networks will be populated here -->
        </datalist>
        <button class="btn btn-secondary" onclick="scanWiFiNetworks()" id="scan-btn" style="margin-top: 5px; padding: 8px 16px; font-size: 14px;">
          Scan for Networks
        </button>
        <div id="wifi-scan-results" style="margin-top:8px;font-size:13px;"></div>
      </div>
      
      <div class="form-group">
        <label for="password">Password *</label>
        <input type="password" id="password" placeholder="Enter WiFi password">
      </div>
      
      <div class="form-group">
        <label for="lmk_key">Local Security Key (16 characters)</label>
        <input type="text" id="lmk_key" maxlength="16" placeholder="Optional - for ArborysMesh encryption">
      </div>

      <div class="form-group">
        <label for="DeviceName">Device Name</label>
        <input type="text" id="DeviceName" maxlength="32" placeholder="Enter a name for your device">
      </div>

      <button class="btn btn-primary" onclick="connectWiFi()" id="wifi-btn">Connect to WiFi</button>
    </div>
  </div>
  
  <!-- Step 2: Location Setup -->
  <div class="setup-step" id="step2">
    <div class="step-header" onclick="toggleStep('step2')">
      <div class="step-number">2</div>
      <div class="step-title">Location Setup (Optional)</div>
    </div>
    <div class="step-content">
      <p>Enter your address to get weather information. This step is optional if not using weather.</p>
      <div id="location-status" class="status-message"></div>
      
      <div class="form-group">
        <label for="street">Street Address</label>
        <input type="text" id="street" placeholder="123 Main St">
      </div>
      
      <div class="form-group">
        <label for="city">City</label>
        <input type="text" id="city" placeholder="Boston">
      </div>
      
      <div class="form-group">
        <label for="state">State (2-letter code)</label>
        <input type="text" id="state" maxlength="2" placeholder="MA" style="width: 80px; text-transform: uppercase;">
      </div>
      
      <div class="form-group">
        <label for="zipcode">ZIP Code</label>
        <input type="text" id="zipcode" maxlength="5" placeholder="02101" pattern="[0-9]{5}">
      </div>
      
      <button class="btn btn-primary" onclick="lookupLocation()" id="location-btn">Lookup Location</button>
      <button class="btn btn-secondary" onclick="skipLocation()">Skip This Step</button>
    </div>
  </div>
  
  <!-- Step 3: Timezone Configuration -->
  <div class="setup-step" id="step3">
    <div class="step-header" onclick="toggleStep('step3')">
      <div class="step-number">3</div>
      <div class="step-title">Timezone Configuration</div>
    </div>
    <div class="step-content">
      <p>Configure your timezone settings.</p>
      <div id="timezone-status" class="status-message"></div>
      
      <button class="btn btn-secondary" onclick="autoDetectTimezone()" id="detect-tz-btn">Auto-Detect Timezone</button>
      
      <div id="timezone-form" style="display:none; margin-top: 20px;">
        <div class="form-group">
          <label for="utc_offset">UTC Offset (seconds)</label>
          <input type="number" id="utc_offset" value="-18000" step="900">
        </div>
        
        <div class="form-group">
          <label for="dst_enabled">DST (0 - none, 1 - inactive, 2 - active)</label>
          <input type="number" id="dst_enabled" value="0" step="1">          
        </div>
        
        <div class="form-group">
          <label for="dst_start_date">DST Start</label>
          <input type="text" id="dst_start_date" placeholder="mm/dd/yyyy hh:nn" maxlength="20" style="width: 100%;">          
        </div>
        <div class="form-group">
          <label for="dst_end_date">DST End</label>
          <input type="text" id="dst_end_date" placeholder="mm/dd/yyyy hh:nn" maxlength="20" style="width: 100%;">          
        </div>
        <div class="form-group">
          <label for="dst_offset">DST Offset (seconds)</label>
          <input type="number" id="dst_offset" value="3600" step="600">
        </div>
        
        <button class="btn btn-primary" onclick="saveTimezone()">Save Timezone</button>
      </div>
    </div>
  </div>
)===");

  #if _SUPABASE_RUNTIME
  serverTextAppend(R"===(
  <!-- Step 4: ArborysNet (optional) -->
  <div class="setup-step" id="step4">
    <div class="step-header" onclick="toggleStep('step4')">
      <div class="step-number">4</div>
      <div class="step-title">ArborysNet (Optional)</div>
    </div>
    <div class="step-content">
      <p>Optionally claim this device with your ArborysNet code and choose a location. You can skip this and finish setup for LAN-only use; claim later from CONFIG if desired.</p>
      <div id="cloud-status" class="status-message"></div>
      <p id="claimed-line" style="margin:12px 0 8px;">Connected: <strong id="connected-flag">No</strong></p>
      <span id="setup-has-creds" style="display:none;">0</span>
      <div class="form-group">
        <label for="claim_code">Claim code</label>
        <input type="text" id="claim_code" maxlength="8" placeholder="ABCD" style="text-transform:uppercase;">
      </div>
      <button class="btn btn-primary" onclick="claimDevice()" id="claim-btn">Claim device</button>
      <div id="site-section" style="margin-top: 20px;">
        <div class="form-group">
          <label for="site_select">Location</label>
          <select id="site_select"></select>
        </div>
        <button class="btn btn-secondary" onclick="loadSites()" id="refresh-sites-btn" disabled>Refresh locations</button>
        <button class="btn btn-secondary" onclick="assignSite()" id="site-btn" style="margin-left:8px;">Save location</button>
)===");
  #if _IS_SERVER_HUB
  WEBHTML += R"===(
        <div style="margin-top: 16px; padding-top: 12px; border-top: 1px solid #ccc;">
          <p><strong>New location (hub)</strong></p>
          <div class="form-group">
            <label for="new_site_description">Site description (max 64)</label>
            <input type="text" id="new_site_description" maxlength="64" placeholder="Downtown garage">
          </div>
          <div class="form-group">
            <label for="new_site_label">Site label (max 24)</label>
            <input type="text" id="new_site_label" maxlength="24" placeholder="garage" style="text-transform:lowercase;">
          </div>
          <button class="btn btn-secondary" onclick="createSite()" id="create-site-btn">Create &amp; assign</button>
        </div>
)===";
  #endif
  WEBHTML += R"===(
      </div>
    </div>
  </div>
)===";
  #endif

  WEBHTML += R"===(
  <!-- Complete Setup -->
  <div style="text-align: center; margin: 30px 0;">
    <div id="complete-status" class="status-message"></div>
    <button class="btn btn-primary" id="complete-btn" onclick="completeSetup()" disabled style="font-size: 18px; padding: 15px 30px;">
      Complete Setup
    </button>
  </div>
</div>

<script>
let setupState = {
  wifiConfigured: false,
  locationConfigured: false,
  timezoneConfigured: false,
  cloudConfigured: false
};
const SUPABASE_ENABLED = )===";

  #if _SUPABASE_RUNTIME
  WEBHTML += "true";
  #else
  WEBHTML += "false";
  #endif

  WEBHTML += R"===(;
const IS_HUB = )===";

  #if _IS_SERVER_HUB
  WEBHTML += "true";
  #else
  WEBHTML += "false";
  #endif

  serverTextAppend(R"===(;

// Toggle step visibility
function toggleStep(stepId) {
  const step = document.getElementById(stepId);
  const content = step.querySelector('.step-content');
  if (content.style.display === 'block') {
    content.style.display = 'none';
  } else {
    content.style.display = 'block';
  }
}

// Show status message
function showStatus(elementId, message, type) {
  const el = document.getElementById(elementId);
  el.textContent = message;
  el.className = 'status-message status-' + type;
  el.style.display = 'block';
}

// Mark step as completed
function completeStep(stepNum) {
  const step = document.getElementById('step' + stepNum);
  step.classList.add('completed');
  step.classList.remove('active');
  // Required steps are 1-3; step 4 (ArborysNet) is optional but still opened for convenience.
  if (stepNum < 3) {
    const nextStep = document.getElementById('step' + (stepNum + 1));
    if (nextStep) nextStep.classList.add('active');
  } else if (stepNum === 3 && SUPABASE_ENABLED) {
    const step4 = document.getElementById('step4');
    if (step4 && !step4.classList.contains('completed')) step4.classList.add('active');
  }
  checkSetupComplete();
}

// Check if setup is complete (ArborysNet / step 4 is optional)
function checkSetupComplete() {
  const ok = setupState.wifiConfigured && setupState.locationConfigured && setupState.timezoneConfigured;
  document.getElementById('complete-btn').disabled = !ok;
}

// Clear WiFi credentials
async function clearWiFiCredentials() {
  if (!confirm('Are you sure you want to clear WiFi credentials? The device will disconnect from the current network but remain accessible via AP mode.')) {
    return;
  }
  
  try {
    const response = await fetch('/api/clear-wifi', { method: 'POST' });
    const data = await response.json();
    
    if (data.success) {
      alert('WiFi credentials cleared successfully. The device has disconnected from the network.');
      // Reload the page to show updated status
      setTimeout(() => {
        window.location.reload();
      }, 1000);
    } else {
      alert('Failed to clear WiFi credentials: ' + (data.error || 'Unknown error'));
    }
  } catch (error) {
    alert('Error: ' + error.message);
  }
}

// Clear WiFi credentials
async function clearWiFiCredentials() {
  if (!confirm('Are you sure you want to clear WiFi credentials? The device will disconnect from the current network but remain accessible via AP mode.')) {
    return;
  }
  
  try {
    const response = await fetch('/api/clear-wifi', { method: 'POST' });
    const data = await response.json();
    
    if (data.success) {
      alert('WiFi credentials cleared successfully. The device has disconnected from the network.');
      // Reload the page to show updated status
      setTimeout(() => {
        window.location.reload();
      }, 1000);
    } else {
      alert('Failed to clear WiFi credentials: ' + (data.error || 'Unknown error'));
    }
  } catch (error) {
    alert('Error: ' + error.message);
  }
}

function rssiColor(rssi) {
  if (rssi > -60) return '#28a745';
  if (rssi > -70) return '#d4a017';
  return '#dc3545';
}

// Scan for WiFi networks
async function scanWiFiNetworks() {
  showStatus('wifi-status', 'Scanning for networks...', 'info');
  const scanBtn = document.getElementById('scan-btn');
  scanBtn.disabled = true;
  scanBtn.textContent = 'Scanning...';
  
  try {
    const response = await fetch('/api/wifi-scan');
    
    // Check if response is OK before parsing
    if (!response.ok) {
      throw new Error('HTTP error: ' + response.status);
    }
    
    const data = await response.json();
    
    // Check if data has the expected structure
    if (data && data.success !== undefined) {
      if (data.success && Array.isArray(data.networks)) {
        const datalist = document.getElementById('ssid-list');
        const resultsDiv = document.getElementById('wifi-scan-results');
        datalist.innerHTML = '';
        if (resultsDiv) resultsDiv.innerHTML = '';
        
        if (data.networks.length > 0) {
          // Sort networks by signal strength (RSSI)
          data.networks.sort((a, b) => b.rssi - a.rssi);
          
          let resultsHtml = '';
          // Add each network to the datalist
          data.networks.forEach(network => {
            const option = document.createElement('option');
            option.value = network.ssid;
            // Show signal strength indicator
            const bars = network.rssi > -50 ? '++++' : network.rssi > -60 ? '+++' : network.rssi > -70 ? '++' : '+';
            const lock = network.encryption > 0 ? '[enc] ' : '[open]';
            option.textContent = lock + network.ssid + ' ' + bars;
            datalist.appendChild(option);
            resultsHtml += '<div>' + lock + network.ssid + ' <span style="color:' + rssiColor(network.rssi) + ';font-weight:bold;">' + network.rssi + ' dBm</span></div>';
          });
          if (resultsDiv) resultsDiv.innerHTML = resultsHtml;
          
          showStatus('wifi-status', 'Found ' + data.networks.length + ' networks. Select from the dropdown.', 'success');
        } else {
          showStatus('wifi-status', 'No networks found. You can still enter SSID manually.', 'info');
        }
      } else {
        showStatus('wifi-status', 'No networks found. You can still enter SSID manually.', 'info');
      }
    } else {
      throw new Error('Invalid response format');
    }
  } catch (error) {
    // If connection was lost (e.g., WiFi disconnected during scan), the scan may have still succeeded
    // Show a more helpful message
    if (error.message.includes('Failed to fetch') || error.message.includes('NetworkError')) {
      showStatus('wifi-status', 'Scan completed but connection was interrupted. The scan may have succeeded - try refreshing the page or check if networks appear in the dropdown.', 'info');
    } else {
      showStatus('wifi-status', 'Scan failed: ' + error.message + '. You can still enter SSID manually.', 'error');
    }
  } finally {
    scanBtn.disabled = false;
    scanBtn.textContent = 'Scan for Networks';
  }
}

// Connect to WiFi
async function connectWiFi() {
  const deviceName = document.getElementById('DeviceName').value;
  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('password').value;
  const lmk_key = document.getElementById('lmk_key').value;
  
  if (!ssid) {
    showStatus('wifi-status', 'Please enter a network name (SSID)', 'error');
    return;
  }
  
  if (!deviceName) {
    showStatus('wifi-status', 'Please enter a device name', 'error');
    return;
  }

  showStatus('wifi-status', 'Connecting to WiFi...', 'info');
  document.getElementById('wifi-btn').disabled = true;
  
  const formData = new FormData();
  formData.append('ssid', ssid);
  formData.append('password', password);
  formData.append('lmk_key', lmk_key);
  formData.append('deviceName', deviceName);

  try {
    const response = await fetch('/api/wifi', { method: 'POST', body: formData });
    const data = await response.json();
    
    if (data.success) {
      showStatus('wifi-status', 'Connected! IP: ' + data.ip, 'success');
      setupState.wifiConfigured = true;
      completeStep(1);
    } else {
      showStatus('wifi-status', 'Connection failed: ' + data.error, 'error');
      document.getElementById('wifi-btn').disabled = false;
    }
  } catch (error) {
    showStatus('wifi-status', 'Error: ' + error.message, 'error');
    document.getElementById('wifi-btn').disabled = false;
  }
}

// Lookup location
async function lookupLocation() {
  const street = document.getElementById('street').value;
  const city = document.getElementById('city').value;
  const state = document.getElementById('state').value;
  const zipcode = document.getElementById('zipcode').value;
  
  if (!street || !city || !state || !zipcode) {
    showStatus('location-status', 'Please fill in all address fields', 'error');
    return;
  }
  
  showStatus('location-status', 'Looking up location...', 'info');
  document.getElementById('location-btn').disabled = true;
  
  const formData = new FormData();
  formData.append('street', street);
  formData.append('city', city);
  formData.append('state', state);
  formData.append('zipcode', zipcode);
  
  try {
    const response = await fetch('/api/location', { method: 'POST', body: formData });
    const data = await response.json();
    
    if (data.success) {
      showStatus('location-status', 'Location found: ' + data.latitude.toFixed(4) + ', ' + data.longitude.toFixed(4), 'success');
      setupState.locationConfigured = true;
      completeStep(2);
    } else {
      showStatus('location-status', 'Lookup failed: ' + data.error, 'error');
      document.getElementById('location-btn').disabled = false;
    }
  } catch (error) {
    showStatus('location-status', 'Error: ' + error.message, 'error');
    document.getElementById('location-btn').disabled = false;
  }
}

// Skip location step
function skipLocation() {
  setupState.locationConfigured = true;
  showStatus('location-status', 'Location setup skipped', 'info');
  completeStep(2);
}

// Valid UTC offset: within +/-14h and not the unconfigured/failure sentinels (90000, 99999).
function isValidTimezoneOffset(offset) {
  return offset !== undefined && offset >= -50400 && offset <= 50400 && offset !== 90000;
}

// Auto-detect timezone
async function autoDetectTimezone() {
  showStatus('timezone-status', 'Detecting timezone...', 'info');
  document.getElementById('detect-tz-btn').disabled = true;
  
  try {
    const response = await fetch('/api/timezone');
    const data = await response.json();
    
    if (data.success === true && isValidTimezoneOffset(data.utc_offset)) {
      // Populate form (dst_start/end are mm/dd/yyyy hh:nn strings from API)
      document.getElementById('utc_offset').value = data.utc_offset;
      document.getElementById('dst_enabled').value = data.dst_enabled;
      document.getElementById('dst_start_date').value = data.dst_start_date || '';
      document.getElementById('dst_end_date').value = data.dst_end_date || '';
      document.getElementById('dst_offset').value = data.dst_offset !== undefined ? data.dst_offset : '';
      
      showStatus('timezone-status', 'Timezone detected. Please review and save.', 'success');
      document.getElementById('timezone-form').style.display = 'block';
    } else {
      showStatus('timezone-status', 'Auto-detection failed. Please set manually.', 'info');
      document.getElementById('timezone-form').style.display = 'block';
    }
  } catch (error) {
    showStatus('timezone-status', 'Detection error. Please set manually.', 'info');
    document.getElementById('timezone-form').style.display = 'block';
  } finally {
    document.getElementById('detect-tz-btn').disabled = false;
  }
}

// Save timezone
async function saveTimezone() {
  const utc_offset = document.getElementById('utc_offset').value;
  const dst_enabled = document.getElementById('dst_enabled').value;
  const dst_start_date = document.getElementById('dst_start_date').value;
  const dst_end_date = document.getElementById('dst_end_date').value;
  const dst_offset = document.getElementById('dst_offset').value;
  
  const formData = new FormData();
  formData.append('utc_offset', utc_offset);
  formData.append('dst_enabled', dst_enabled);
  formData.append('dst_offset', dst_offset);
  formData.append('dst_start_date', dst_start_date);
  formData.append('dst_end_date', dst_end_date);
  
  try {
    const response = await fetch('/api/timezone', { method: 'POST', body: formData });
    const data = await response.json();
    
    if (data.success) {
      showStatus('timezone-status', 'Timezone saved!', 'success');
      setupState.timezoneConfigured = true;
      completeStep(3);
    } else {
      showStatus('timezone-status', 'Failed to save timezone', 'error');
    }
  } catch (error) {
    showStatus('timezone-status', 'Error: ' + error.message, 'error');
  }
}

function isDeviceConnected() {
  const flag = document.getElementById('connected-flag');
  return !!(flag && flag.textContent.trim() === 'Yes');
}

function hasStoredCreds() {
  const el = document.getElementById('setup-has-creds');
  return !!(el && el.textContent.trim() === '1');
}

function setHasCredsUi(has) {
  const el = document.getElementById('setup-has-creds');
  if (el) el.textContent = has ? '1' : '0';
  const refreshBtn = document.getElementById('refresh-sites-btn');
  if (refreshBtn) refreshBtn.disabled = !has;
}

function handleSetupApiFailure(data, fallbackMsg) {
  const err = (data && data.error) ? data.error : (fallbackMsg || 'failed');
  const invalid = !!(data && (data.invalid_device || data.code === 'invalid_device' ||
    /invalid_device|Invalid device credentials/i.test(String(err))));
  setConnectedUi(false);
  if (invalid) {
    setHasCredsUi(false);
    showStatus('cloud-status', err + ' (credentials cleared. Re-claim required)', 'error');
    setupState.cloudConfigured = false;
    checkSetupComplete();
    return true;
  }
  showStatus('cloud-status', err, 'error');
  return false;
}

function setConnectedUi(connected) {
  const flag = document.getElementById('connected-flag');
  if (flag) flag.textContent = connected ? 'Yes' : 'No';
}

async function loadSites(pendingLeft) {
  if (!SUPABASE_ENABLED) return;
  const refreshBtn = document.getElementById('refresh-sites-btn');
  if (!hasStoredCreds()) {
    showStatus('cloud-status', 'Claim the device before refreshing locations', 'error');
    return;
  }
  if (pendingLeft === undefined) pendingLeft = 40;
  if (refreshBtn) refreshBtn.disabled = true;
  try {
    showStatus('cloud-status', 'Loading locations...', 'info');
    const response = await fetch('/api/arborysnet/sites');
    const data = await response.json();
    if (data.pending) {
      if (pendingLeft <= 0) {
        showStatus('cloud-status', 'Locations still loading - tap Refresh locations to retry', 'error');
        return;
      }
      await new Promise(r => setTimeout(r, 1500));
      return loadSites(pendingLeft - 1);
    }
    if (data.tls_busy || (data.error && data.error.indexOf('TLS client occupied') >= 0)) {
      showStatus('cloud-status', data.error || 'Please wait, TLS client occupied', 'info');
      return;
    }
    if (!data.success) {
      handleSetupApiFailure(data, 'Failed to load locations');
      return;
    }
    setConnectedUi(true);
    applySitesUi(data);
  } catch (error) {
    setConnectedUi(false);
    showStatus('cloud-status', 'Error loading locations: ' + error.message, 'error');
  } finally {
    if (refreshBtn && hasStoredCreds()) refreshBtn.disabled = false;
  }
}

function applySitesUi(data) {
  const sites = data.sites || [];
  const sel = document.getElementById('site_select');
  if (!sel) return;

  sel.innerHTML = '';
  if (sites.length === 0) {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = '(no locations - refresh or create one)';
    sel.appendChild(opt);
    showStatus('cloud-status', data.sites_error || 'No locations yet. Create one or refresh.', 'info');
    checkSetupComplete();
    return;
  }

  sites.forEach(s => {
    const label = s.label || s.slug || '';
    const desc = s.description || s.name || label;
    const opt = document.createElement('option');
    opt.value = label;
    opt.textContent = desc + (label && label !== desc ? ' [' + label + ']' : '');
    if (label === data.current || label === data.site_slug) opt.selected = true;
    sel.appendChild(opt);
  });
  if (data.current) sel.value = data.current;
  showStatus('cloud-status', 'Choose a location, then Save location.', 'info');
  checkSetupComplete();
}

async function claimDevice() {
  if (!SUPABASE_ENABLED) return;
  const code = document.getElementById('claim_code').value;
  if (!code) {
    showStatus('cloud-status', 'Enter claim code', 'error');
    return;
  }
  showStatus('cloud-status', 'Claiming device...', 'info');
  document.getElementById('claim-btn').disabled = true;
  const formData = new FormData();
  formData.append('claim_code', code);
  try {
    const response = await fetch('/api/arborysnet/claim', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      setHasCredsUi(true);
      setConnectedUi(true);
      setupState.cloudConfigured = false;
      const step4 = document.getElementById('step4');
      if (step4) {
        step4.classList.add('active');
        step4.classList.remove('completed');
      }
      showStatus('cloud-status', 'Connected: Yes. Tap Refresh locations, then choose a location.', 'success');
      checkSetupComplete();
    } else {
      setConnectedUi(false);
      showStatus('cloud-status', data.error || 'Claim failed', 'error');
      setupState.cloudConfigured = false;
      checkSetupComplete();
    }
  } catch (error) {
    showStatus('cloud-status', 'Error: ' + error.message, 'error');
    setupState.cloudConfigured = false;
    checkSetupComplete();
  } finally {
    document.getElementById('claim-btn').disabled = false;
  }
}

async function assignSite() {
  if (!SUPABASE_ENABLED) return;
  if (!hasStoredCreds()) {
    showStatus('cloud-status', 'Claim the device first', 'error');
    return;
  }
  const label = document.getElementById('site_select').value;
  if (!label) {
    showStatus('cloud-status', 'Select a location (refresh the list first)', 'error');
    return;
  }
  const formData = new FormData();
  formData.append('site_label', label);
  try {
    const response = await fetch('/api/arborysnet/site', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      setConnectedUi(true);
      showStatus('cloud-status', 'Location saved: ' + (data.label || data.site || label), 'success');
      setupState.cloudConfigured = true;
      completeStep(4);
    } else {
      handleSetupApiFailure(data, 'Failed to save location');
    }
  } catch (error) {
    setConnectedUi(false);
    showStatus('cloud-status', 'Error: ' + error.message, 'error');
  }
}

async function createSite() {
  if (!SUPABASE_ENABLED || !IS_HUB) return;
  if (!hasStoredCreds()) {
    showStatus('cloud-status', 'Claim the device first', 'error');
    return;
  }
  const descriptionEl = document.getElementById('new_site_description');
  const labelEl = document.getElementById('new_site_label');
  const description = descriptionEl ? descriptionEl.value : '';
  let label = labelEl ? labelEl.value : '';
  if (!label && description) {
    label = description.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').substring(0, 24);
  }
  if (!label) {
    showStatus('cloud-status', 'Enter a site label or description', 'error');
    return;
  }
  if (label.length > 24) label = label.substring(0, 24);
  const formData = new FormData();
  formData.append('site_label', label);
  if (description) formData.append('site_description', description.substring(0, 64));
  try {
    const response = await fetch('/api/arborysnet/site/create', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      setConnectedUi(true);
      const saved = data.label || data.site || label;
      showStatus('cloud-status', 'Created location: ' + saved, 'success');
      await loadSites();
      const sel = document.getElementById('site_select');
      if (sel) sel.value = saved;
      setupState.cloudConfigured = true;
      completeStep(4);
    } else {
      handleSetupApiFailure(data, 'Create failed');
    }
  } catch (error) {
    setConnectedUi(false);
    showStatus('cloud-status', 'Error: ' + error.message, 'error');
  }
}

// Complete setup (device reboots to refresh weather and display)
async function completeSetup() {
  if (!confirm('Finish setup? The device will reboot to apply settings and refresh the screen.')) {
    return;
  }
  const completeBtn = document.getElementById('complete-btn');
  completeBtn.disabled = true;
  showStatus('complete-status', 'Saving configuration...', 'info');
  try {
    const response = await fetch('/api/complete-setup', { method: 'POST' });
    const data = await response.json();
    if (data.success) {
      showStatus('complete-status', data.message, 'success');
    } else {
      showStatus('complete-status', data.error || 'Setup could not be completed', 'error');
      completeBtn.disabled = false;
    }
  } catch (error) {
    showStatus('complete-status', 'Error: ' + error.message, 'error');
    completeBtn.disabled = false;
  }
}

// Initialize - check current status
async function initSetup() {
  try {
    const response = await fetch('/api/setup-status');
    const status = await response.json();
    
    if (status.wifi_configured) {
      setupState.wifiConfigured = true;
      document.getElementById('step1').classList.add('completed');
      showStatus('wifi-status', 'Already configured: ' + status.ip, 'success');
    } else {
      document.getElementById('step1').classList.add('active');
    }
    
    if (status.location_configured) {
      setupState.locationConfigured = true;
      document.getElementById('step2').classList.add('completed');
      showStatus('location-status', 'Location: ' + status.latitude.toFixed(4) + ', ' + status.longitude.toFixed(4), 'success');
    }
    
    if (status.timezone_configured) {
      setupState.timezoneConfigured = true;
      document.getElementById('step3').classList.add('completed');
      showStatus('timezone-status', 'Configured: UTC offset = ' + status.utc_offset, 'success');
    }

    if (SUPABASE_ENABLED) {
      const step4 = document.getElementById('step4');
      const connected = !!(status.supabase_connected);
      const hasCreds = !!(status.supabase_claimed || status.supabase_connected);
      setConnectedUi(connected);
      setHasCredsUi(hasCreds);
      if (connected && status.site_slug) {
        setupState.cloudConfigured = true;
        if (step4) {
          step4.classList.add('completed');
          step4.classList.remove('active');
        }
        showStatus('cloud-status', 'Connected: Yes. Location: ' + status.site_slug + '. Refresh locations to change.', 'success');
      } else if (hasCreds) {
        setupState.cloudConfigured = false;
        if (step4) step4.classList.add('active');
        showStatus('cloud-status', 'Credentials stored. Tap Refresh locations to connect, or Complete Setup to finish without cloud.', 'info');
      } else {
        setupState.cloudConfigured = false;
        if (setupState.timezoneConfigured && step4) step4.classList.add('active');
        if (setupState.timezoneConfigured) {
          showStatus('cloud-status', 'Optional: claim with ArborysNet, or tap Complete Setup to finish LAN-only.', 'info');
        }
      }
    }
    
    checkSetupComplete();
  } catch (error) {
    console.error('Failed to load setup status:', error);
    document.getElementById('step1').classList.add('active');
  }
}

// Run on page load
initSetup();
</script>
</body></html>
)===");
  
  serverTextClose(200, true);
}

void handleReboot() {
  snprintf(I.HTTP_LAST_INCOMINGMSG_TYPE, sizeof(I.HTTP_LAST_INCOMINGMSG_TYPE), "REBOOT");
  WEBHTML = "Rebooting in 3 sec";
  
  handleStoreCoreData();

  serverTextClose(200, false);  //This returns to the main page
  delay(3000);
  controlledReboot("Rebooting",RESET_USER,true);
}

void handleCLEARSENSOR() {
  int j=-1;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  initSensor(j);

  server.sendHeader("Location", "/");
  
  WEBHTML= "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}

void handleREQUESTUPDATE() {

  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  // Get the device IP for the specified sensor
  ArborysDevType* device = Sensors.getDeviceBySnsIndex(j);
  if (device) {
    HTTPMessage M;
    char url[100];
    snprintf(url, 99, "%s/UPDATEALLSENSORREADS", device->IP.toString().c_str());
    M.setUrl(url);
    M.setMethod("GET");
    M.usePSRAM = false;
    M.timeout = 5000;
    M.allowInsecure = true;

    if (SendHTTPMessage(M)) {
      WEBHTML = "Updated-- Press Back Button";  //This returns to the main page
      serverTextClose(M.httpCode,false);
    } else {
      WEBHTML = "Failed to update sensor reads";
      serverTextClose(M.httpCode,false);
    }
  }

  server.sendHeader("Location", "/");
  WEBHTML= "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}


#if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
#include "WeatherPkg.hpp"
#ifdef _USEWEATHERLITE
#include "Weather_Optimized_lite.hpp"

static File s_weatherPkgRawFile;
static uint32_t s_weatherPkgRawWritten = 0;

void handleWEATHERPKG_raw() {
  // Stream POST body to SD — never hold the full package in RAM.
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START) {
    s_weatherPkgRawWritten = 0;
    if (s_weatherPkgRawFile) s_weatherPkgRawFile.close();
    int cl = server.clientContentLength();
    if (cl < (int)WEATHER_PKG_HEADER_CORE || (size_t)cl > WEATHER_PKG_MAX_BYTES) return;
    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
    s_weatherPkgRawFile = SD.open(WEATHER_PKG_RECV_TMP_PATH, FILE_WRITE);
    return;
  }
  if (raw.status == RAW_WRITE) {
    if (!s_weatherPkgRawFile) return;
    if (s_weatherPkgRawWritten + raw.currentSize > WEATHER_PKG_MAX_BYTES) return;
    s_weatherPkgRawFile.write(raw.buf, raw.currentSize);
    s_weatherPkgRawWritten += raw.currentSize;
    return;
  }
  if (raw.status == RAW_END) {
    if (s_weatherPkgRawFile) s_weatherPkgRawFile.close();
    return;
  }
  if (raw.status == RAW_ABORTED) {
    if (s_weatherPkgRawFile) s_weatherPkgRawFile.close();
    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
    s_weatherPkgRawWritten = 0;
  }
}
#endif

void handleWEATHERPKG() {
  // SECURITY: plain HTTP weather package on LAN — see WeatherPkg.hpp.
  registerHTTPMessage("WTHRPKG");

#ifdef _USEWEATHER
  if (server.method() == HTTP_GET) {
    if (!WeatherData.ensureWeatherPackageFile()) {
      server.send(503, "text/plain", "Weather package unavailable");
      return;
    }
    File f = SD.open(WEATHER_PKG_PATH, FILE_READ);
    if (!f) {
      server.send(404, "text/plain", "Package not found");
      return;
    }
    server.streamFile(f, "application/octet-stream");
    f.close();
    return;
  }
  server.send(405, "text/plain", "GET only on weather producer");
  return;
#endif

#ifdef _USEWEATHERLITE
  if (server.method() == HTTP_POST) {
    if (!FileOrDirectoryExists(WEATHER_PKG_RECV_TMP_PATH)) {
      server.send(400, "text/plain", "No package body");
      return;
    }
    if (weatherLiteUnpackFile(WEATHER_PKG_RECV_TMP_PATH)) {
      // Promote recv tmp to canonical package path (already unpacked into WeatherData/Events)
      sdDeleteFile(WEATHER_PKG_PATH);
      File src = SD.open(WEATHER_PKG_RECV_TMP_PATH, FILE_READ);
      File dst = SD.open(WEATHER_PKG_PATH, FILE_WRITE);
      if (src && dst) {
        uint8_t buf[512];
        while (src.available()) {
          int r = src.read(buf, sizeof(buf));
          if (r <= 0) break;
          dst.write(buf, (size_t)r);
        }
      }
      if (src) src.close();
      if (dst) dst.close();
      sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
      WeatherLite.lastRequestAttemptAt =
          isTimeValid((uint32_t)utcNow()) ? (uint32_t)utcNow() : WeatherLite.lastRequestAttemptAt;
      server.send(200, "text/plain", "OK");
      return;
    }
    sdDeleteFile(WEATHER_PKG_RECV_TMP_PATH);
    server.send(400, "text/plain", "Unpack failed");
    return;
  }
  server.send(405, "text/plain", "POST only on weather lite");
#endif
}
#endif

#ifdef _USEWEATHER
bool sendWeatherPackageHttp(IPAddress ip) {
  if (!wifiReadyForNetwork()) return false;
  if (!WeatherData.ensureWeatherPackageFile()) return false;

  File f = SD.open(WEATHER_PKG_PATH, FILE_READ);
  if (!f) return false;
  const size_t sz = f.size();
  if (sz == 0 || sz > WEATHER_PKG_MAX_BYTES) {
    f.close();
    return false;
  }

  char url[64];
  snprintf(url, sizeof(url), "http://%s/WEATHERPKG", ip.toString().c_str());
  WiFiClient client;
  HTTPClient http;
  client.setTimeout(15000);
  if (!http.begin(client, url)) {
    f.close();
    return false;
  }
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/octet-stream");
  esp_task_wdt_reset();
  int code = http.sendRequest("POST", &f, sz);
  esp_task_wdt_reset();
  f.close();
  http.end();
  SerialPrint("sendWeatherPackageHttp to " + ip.toString() + " code=" + String(code), true);
  return code >= 200 && code < 300;
}

void serviceWeatherPackagePush(bool minuteTick) {
  if (!minuteTick) return;
  if (!wifiReadyForNetwork()) return;
  if (!isTimeValid((uint32_t)utcNow())) return;

  // A size-mismatched package is deleted at boot. If Wi-Fi or the clock was not
  // ready then, finish the NOAA fetch and rewrite on this minute instead of the
  // hourly push.
  if (WeatherData.recoverCorruptWeatherPackage(false) == 2) return;

  static uint32_t nextPushDue = 0;
  static int16_t pushDevIndex = -1; // -1 idle, else scanning devices

  if (pushDevIndex < 0) {
    if (nextPushDue == 0) {
      nextPushDue = (uint32_t)utcNow() + 55u * 60u + (uint32_t)random(1, 16) * 60u;
    }
    if ((uint32_t)utcNow() < nextPushDue) return;
    if (!WeatherData.buildWeatherPackageFile(true)) {
      nextPushDue = (uint32_t)utcNow() + 10u * 60u; // retry sooner on failure
      return;
    }
    pushDevIndex = 0;
  }

  while (pushDevIndex < NUMDEVICES) {
    int16_t di = pushDevIndex++;
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d || !d->IsSet || d->devType != 101) continue;
    if (d->IP == IPAddress(0, 0, 0, 0)) continue;
    sendWeatherPackageHttp(d->IP);
    return; // one device per minute tick
  }

  pushDevIndex = -1;
  nextPushDue = (uint32_t)utcNow() + 55u * 60u + (uint32_t)random(1, 16) * 60u;
}
#endif

void handleREQUESTWEATHER() {
  #if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
  registerHTTPMessage("WTHRREQ");
//if no parameters passed, return current temp, max, min, today weather ID, pop, and snow amount
//otherwise, return the index value for the requested value

  int8_t dailyT[2];

  WEBHTML = "";
  if (server.args()==0) {
        WEBHTML += (String) WeatherData.getTemperature((uint32_t)utcNow()) + ";"; //current temp
        WeatherData.getDailyTemp(0,dailyT);
        WEBHTML += (String) dailyT[0] + ";"; //dailymax
        WEBHTML += (String) dailyT[1] + ";"; //dailymin
        WEBHTML += (String) WeatherData.getDailyWeatherID(0) + ";"; //dailyID
        WEBHTML += (String) WeatherData.getDailyPoP(0) + ";"; //POP
        WEBHTML += (String) WeatherData.flag_snow + ";"; 
        WEBHTML += (String) WeatherData.sunrise + ";"; // UTC unix
        WEBHTML += (String) WeatherData.sunset + ";";  // UTC unix
  } else {
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i)=="hourly_temp") WEBHTML += (String) WeatherData.getTemperature((uint32_t)utcNow() + server.arg(i).toInt()*3600,false,false) + ";";
      WeatherData.getDailyTemp(server.arg(i).toInt(),dailyT);
      if (server.argName(i)=="daily_tempMax") WEBHTML += (String) dailyT[0] + ";";
      if (server.argName(i)=="daily_tempMin") WEBHTML += (String) dailyT[1] + ";";
      if (server.argName(i)=="daily_weatherID") WEBHTML += (String) WeatherData.getDailyWeatherID(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="hourly_weatherID") WEBHTML += (String) WeatherData.getWeatherID((uint32_t)utcNow() + server.arg(i).toInt()*3600) + ";";
      if (server.argName(i)=="daily_pop") WEBHTML += (String) WeatherData.getDailyPoP(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="daily_snow") WEBHTML += (String) WeatherData.getDailySnow(server.arg(i).toInt()) + ";";
      if (server.argName(i)=="hourly_pop") WEBHTML += (String) WeatherData.getPoP((uint32_t)utcNow() + server.arg(i).toInt()*3600) + ";";
      if (server.argName(i)=="hourly_snow") WEBHTML += (String) WeatherData.getSnow((uint32_t)utcNow() + server.arg(i).toInt()*3600) + ";";      //note snow is returned in mm!!
      if (server.argName(i)=="sunrise") WEBHTML += (String) WeatherData.sunrise + ";"; // UTC unix
      if (server.argName(i)=="sunset") WEBHTML += (String) WeatherData.sunset + ";";   // UTC unix
      if (server.argName(i)=="hour") {
        uint32_t temptime = server.arg(i).toDouble();
        if (temptime==0) WEBHTML += (String) hour() + ";";
        else WEBHTML += (String) hour(temptime) + ";";
      }
      if (server.argName(i)=="isFlagged") WEBHTML += (String) I.isFlagged + ";";
      if (server.argName(i)=="isAC") WEBHTML += (String) I.isAC + ";";
      if (server.argName(i)=="isHeat") WEBHTML += (String) I.isHeat + ";";
      if (server.argName(i)=="isSoilDry") WEBHTML += (String) I.isSoilDry + ";";
      if (server.argName(i)=="isHot") WEBHTML += (String) I.isHot + ";";
      if (server.argName(i)=="isCold") WEBHTML += (String) I.isCold + ";";
      if (server.argName(i)=="isLeak") WEBHTML += (String) I.isLeak + ";";
      if (server.argName(i)=="isExpired") WEBHTML += (String) I.isExpired + ";";

    }
  }
  #else
  WEBHTML = "Weather not enabled on this device.";
  #endif

  serverTextClose(200,false);

  return;
}

void handleTIMEUPDATE() {
  registerHTTPMessage("TIMEUPD");

  timeClient.forceUpdate();
  updateTime(); 

  server.sendHeader("Location", "/");


  WEBHTML = "Updated-- Press Back Button";  //This returns to the main page
  serverTextClose(302,false);

  return;
}


#if _IS_SERVER_HUB
static String csvDeviceField(const char* text) {
  String s = text ? String(text) : String();
  if (s.indexOf(',') < 0 && s.indexOf('"') < 0 && s.indexOf('\n') < 0 && s.indexOf('\r') < 0) return s;
  s.replace("\"", "\"\"");
  return "\"" + s + "\"";
}

// Inventory for get_IP_from_hub. Live means this hub heard the device within 30 minutes.
// Sensor expired flags are a separate clock and are not used here.
static bool deviceHeardRecently(const ArborysDevType* d, int16_t index) {
  if (!d) return false;
  if (index == I.MY_DEVICE_INDEX) return true;
  if (d->dataReceived == 0) return false;
  const uint32_t now = (uint32_t)utcNow();
  if (!isTimeValid(now) || now < d->dataReceived) return true;
  return (now - d->dataReceived) <= PERIPH_SERVER_STALE_SEC;
}

void handleDEVICES() {
  const bool named = server.hasArg("name");
  const String want = named ? server.arg("name") : String();
  String csv;
  bool found = false;
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!d || !d->IsSet || d->devName[0] == '\0') continue;
    if (named && want != String(d->devName)) continue;
    found = true;
    csv += csvDeviceField(d->devName);
    csv += ",";
    csv += d->IP.toString();
    csv += deviceHeardRecently(d, i) ? ",ok," : ",exp,";
    csv += String(d->dataReceived);
    csv += "\n";
  }
  if (named && !found) {
    server.send(404, "text/plain", "not found");
    return;
  }
  server.send(200, "text/csv", csv);
}
#endif

void handleSTATUS() {
  //registerHTTPMessage("STATUS");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Status");
  serverTextStreamBegin(200, true);

  appendStandardPageNav();

  WEBHTML += "Devices/sensors: " + String(Sensors.getNumDevices()) + " / " + String(Sensors.getNumSensors()) + "<br>";
  WEBHTML += "Alive since: " + String(I.ALIVESINCE ? dateifyLocal(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Reboots today: " + String(I.rebootsToday) + "<br>";
  WEBHTML += "Last error: " + String(I.lastError[0] ? I.lastError : "(none)");
  if (I.lastError[0] && I.lastErrorTime) {
    WEBHTML += " @" + String(dateifyLocal(I.lastErrorTime,"mm/dd/yyyy hh:nn:ss"));
  }
  WEBHTML += "<br>";
  {
    const char* logMsg = getLastSystemLogMessage();
    WEBHTML += "Last log: " + String(logMsg && logMsg[0] ? logMsg : "(none)");
    if (getLastSystemLogTime()) {
      WEBHTML += " @" + String(dateifyLocal(getLastSystemLogTime(),"mm/dd/yyyy hh:nn:ss"));
    }
    WEBHTML += "<br>";
  }
  WEBHTML += "Last reboot: " + lastReset2String() + "<br>";
  WEBHTML += "---------------------<br>";
  serverTextFlush(true);

  WEBHTML += "<p><strong>WiFi:</strong> " + String(wifiReadyForNetwork() ? "Connected" : "Disconnected");
  WEBHTML += " (" + getWiFiModeString() + ")<br>";
  wifi_mode_t t = WiFi.getMode();
  if (t == WIFI_MODE_APSTA || t == WIFI_MODE_STA) {
    WEBHTML += "SSID: " + WiFi.SSID() + " IP: " + WiFi.localIP().toString() + "<br>";
  }
  WEBHTML += "Channel: " + String(WiFi.channel()) + "<br>";
  {
    const uint8_t* bssid = WiFi.BSSID();
    const bool haveAp = bssid && (bssid[0] | bssid[1] | bssid[2] | bssid[3] | bssid[4] | bssid[5]);
    WEBHTML += "Channel AP: ";
    if (haveAp) WEBHTML += bssidToString(bssid) + " (" + formatRssiHtml(WiFi.RSSI()) + ")";
    else WEBHTML += "none";
    WEBHTML += "<br>";
  }
  if (t == WIFI_MODE_APSTA || t == WIFI_MODE_AP) {
    WEBHTML += "AP: " + WiFi.softAPSSID() + " @ " + WiFi.softAPIP().toString()
        + " clients: " + String(WiFi.softAPgetStationNum()) + "<br>";
  }
  if (bleProvisionIsActive()) {
    WEBHTML += "BLE: " + String(bleProvisionServiceName()) + " (PoP = AP password)<br>";
  }
  WEBHTML += "RSSI: " + formatRssiHtml(I.RSSIcurrent) + "</p>";
  serverTextFlush(true);

#if _IS_SERVER_HUB
  #ifdef _USE32
  WEBHTML += "Heap free/min: " + String(esp_get_free_heap_size()) + " / " + String(esp_get_minimum_free_heap_size()) + "<br>";
  WEBHTML += "PSRAM: " + String(ESP.getFreePsram()) + " / " + String(ESP.getPsramSize()) + "<br>";
  serverTextFlush(true);
  #endif
  #if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
  auto nmTime = [](time_t t) -> String {
    return t ? dateifyLocal(t, "mm/dd/yyyy hh:nn:ss") : String("???");
  };
  WEBHTML = WEBHTML + "<strong>RSSI detail:</strong> best " + formatRssiHtml(I.RSSIhigh) + ", worst " + formatRssiHtml(I.RSSIlow) + "<br>";
  WEBHTML = WEBHTML + "AP switches: " + String(NetworkMonitor.bssid.changeCount)
      + " @" + nmTime(NetworkMonitor.bssid.lastAttemptTime) + "<br>";
  WEBHTML = WEBHTML + "Gateway Latency: " + String(NetworkMonitor.gatewayLatency.ping.avgRttMs) + " ms avg<br>";
  #endif
#endif

  WEBHTML += "---------------------<br>";
  serverTextFlush(true);
  appendCommunicationsSection();
  serverTextFlush(true);
  WEBHTML += "---------------------<br>";
  #if _IS_SERVER_HUB && defined(_USEWEATHER)
  WEBHTML += "Weather last retrieved: " + String(WeatherData.lastUpdateT ? dateifyLocal(WeatherData.lastUpdateT,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Weather last failure: " + String(WeatherData.lastUpdateError ? dateifyLocal(WeatherData.lastUpdateError,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  #endif

  serverTextClose(200, true);
}


void handleRoot(void) {
  registerHTTPMessage("MainPage");
  // Bare "/" defaults to WiFi setup when credentials are missing or STA has no usable IP
  // (e.g. 0.0.0.0 after a failed reconnect). "/?main" bypasses that for intentional
  // no-WiFi / ESP-NOW use from the header nav.
  const bool forceMain = server.hasArg("main");
  if (!forceMain && (!haveWifiCredentials() || !wifiReadyForNetwork())) {
    SerialPrint(
      !haveWifiCredentials()
        ? "No stored WiFi credentials, redirecting root to InitialSetup"
        : "WiFi not ready (no usable IP), redirecting root to InitialSetup",
      true);
    server.sendHeader("Location", "/InitialSetup");
    server.send(302, "text/plain", "Redirecting to setup...");
    return;
  }
  renderDeviceViewerPage();
}

void appendStandardPageNav(bool includeWiFiConfig) {
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/?main\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a> ";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/MESH_SETTINGS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px;\">Mesh Settings</a> ";
#if _HAS_LOCAL_SENSORS
  {
    bool showSwitch = false;
    for (int16_t si = 0; si < NUMSENSORS && !showSwitch; si++) {
      ArborysSnsType* s = Sensors.snsIndexToPointer(si);
      if (!s || !s->IsSet || s->deviceIndex != I.MY_DEVICE_INDEX) continue;
      if (isSwitchStateOutputType(s->snsType) || isSwitchStateInterruptType(s->snsType)) showSwitch = true;
    }
    if (showSwitch) {
      WEBHTML = WEBHTML + "<a href=\"/SWITCHSTATE\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #3F51B5; color: white; text-decoration: none; border-radius: 4px;\">SwitchState</a> ";
    }
  }
#endif
  serverTextFlush(false);
  WEBHTML = WEBHTML + "<a href=\"/REGISTER_DEVICE\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #009688; color: white; text-decoration: none; border-radius: 4px;\">Register Device</a> ";
  #ifdef _USESDCARD
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
  #endif
  serverTextFlush(false);
  #ifdef _USEWEATHER
  WEBHTML = WEBHTML + "<a href=\"/WEATHER\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #607D8B; color: white; text-decoration: none; border-radius: 4px;\">Weather</a> ";
  #endif
  #ifdef _USEGSHEET
  WEBHTML = WEBHTML + "<a href=\"/GSHEET\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #E91E63; color: white; text-decoration: none; border-radius: 4px;\">GSheets</a> ";
  #endif
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a>";
  if (includeWiFiConfig) {
    WEBHTML = WEBHTML + " <a href=\"/InitialSetup\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px;\">WiFi Config</a>";
  }
  WEBHTML = WEBHTML + "</div>";
  serverTextFlush(true);
}

static String formatArborysDeviceFirmware(const ArborysDevType* device) {
  if (!device) return "0.0.0";
  char buf[16];
  device->firmware.toChar(buf, sizeof(buf));
  return String(buf);
}

// Marks: * if any sensor is flagged (Flags bit0 usable); (exp) if expired and critical bit usable.
// Remote OverrideFlags force those flag bits to 0. Local sensors never use OverrideFlags.
static void collectDeviceViewerNameMarks(bool deviceFlagged[NUMDEVICES], bool deviceExpired[NUMDEVICES]) {
  for (int16_t di = 0; di < NUMDEVICES; di++) {
    deviceFlagged[di] = false;
    deviceExpired[di] = false;
  }
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* sensor = Sensors.snsIndexToPointer(si);
    if (!sensor || !sensor->IsSet) continue;
    if (sensor->deviceIndex < 0 || sensor->deviceIndex >= NUMDEVICES) continue;

    // * only when flagged and still monitored-or-critical for alert relevance
    if (Sensors.isSensorFlagBitUsed(si, 0) &&
        (Sensors.isSensorFlagBitUsed(si, 1) || Sensors.isSensorFlagBitUsed(si, 7))) {
      deviceFlagged[sensor->deviceIndex] = true;
    }
    if (sensor->expired && Sensors.isSensorFlagBitUsed(si, 7)) {
      deviceExpired[sensor->deviceIndex] = true;
    }
  }
}

static String formatDeviceViewerName(const ArborysDevType* device, bool flagged, bool expired, bool includeFirmware) {
  if (!device) return "";
  String label = String(device->devName);
  if (includeFirmware) label += " v" + formatArborysDeviceFirmware(device);
  if (flagged) label += "*";
  if (expired) label += "(exp)";
  return label;
}

static void appendDeviceFirmwareInfoRow(const ArborysDevType* device) {
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Firmware Version:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + formatArborysDeviceFirmware(device) + "</td></tr>";
}

#ifndef _USEGSHEET
static void appendAllDevicesFirmwareTable() {
  bool deviceFlagged[NUMDEVICES];
  bool deviceExpired[NUMDEVICES];
  collectDeviceViewerNameMarks(deviceFlagged, deviceExpired);

  WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
  WEBHTML = WEBHTML + "<h4>Registered Devices</h4>";
  WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #e9ecef;\">";
  WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Device Name</th>";
  WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">IP Address</th>";
  WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type</th>";
  WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Firmware</th>";
  WEBHTML = WEBHTML + "</tr>";
  for (int16_t di = 0; di < NUMDEVICES; di++) {
    if (!Sensors.isDeviceInit(di)) continue;
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d) continue;
    WEBHTML = WEBHTML + "<tr>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"/?devIndex=" + String(di) + "\">" + formatDeviceViewerName(d, deviceFlagged[di], deviceExpired[di], false) + "</a></td>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + d->IP.toString() + "\" target=\"_blank\">" + d->IP.toString() + "</a></td>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(d->devType) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + formatArborysDeviceFirmware(d) + firmwareTransferStatusForDevice(d->devName) + "</td>";
    WEBHTML = WEBHTML + "</tr>";
    serverTextFlush(true);
  }
  WEBHTML = WEBHTML + "</table></div>";
  serverTextFlush(true);
}
#endif

#ifndef CONTENT_LENGTH_UNKNOWN
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#endif

namespace {
  bool s_webStreamActive = false;
  // Keep the in-RAM HTML buffer small. Classic ESP32 peripherals OOM and blank the
  // page after the header when WEBHTML grows (CONFIG/STATUS/InitialSetup/Main).
  // Small TCP writes also survive lossy / low-RSSI links better than one large send.
  constexpr size_t WEBHTML_CHUNK_BYTES = 512;

  void serverTextSendSlices(const char* data, size_t len) {
    for (size_t off = 0; off < len; ) {
      const size_t n = (len - off > WEBHTML_CHUNK_BYTES) ? WEBHTML_CHUNK_BYTES : (len - off);
      server.sendContent(data + off, n);
      off += n;
      yield();
      esp_task_wdt_reset();
    }
  }

  void serverTextSendBufferedChunks(const char* contentType, int htmlcode) {
    // Stream an already-built buffer in slices so send() does not hold a second full copy.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(htmlcode, contentType, "");
    serverTextSendSlices(WEBHTML.c_str(), WEBHTML.length());
    server.sendContent("");
    WEBHTML = "";
  }
}

void serverTextHeader(String pagename) {
  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) Prefs.DEVICENAME + "</title>";
  WEBHTML += R"===(<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}
  body {  font-family: arial, sans-serif; }
  </style></head>
  )===";
  // Open body here so streamed pages still look complete if generation stops mid-handler.
  WEBHTML += "<body>";
  WEBHTML += "<h1>" + (String) Prefs.DEVICENAME + " - " + pagename + "</h1>";
  WEBHTML += "<h2>Current Time: " + (String) dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "</h2>";
  ArborysDevType* myDev = nullptr;
  uint16_t myDeviceIndex = Sensors.findMyDeviceIndex();
  if (myDeviceIndex != (uint16_t)-1) myDev = Sensors.getDeviceByDevIndex(myDeviceIndex);
  WEBHTML += "<h3>Current Version: " + formatArborysDeviceFirmware(myDev) + getFirmwareReceiveProgressSuffix() + "</h3>";
  WEBHTML += "<h3>Device IP: " + (String) WiFi.localIP().toString() + "</h3>";  
}

void serverTextStreamBegin(int htmlcode, bool asHTML) {
  if (s_webStreamActive) return;
  server.client().setTimeout(20000); // low-RSSI clients need longer than the default
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(htmlcode, asHTML ? "text/html" : "text/plain", "");
  s_webStreamActive = true;
  if (WEBHTML.length() > 0) {
    // Header can exceed one chunk � send in slices.
    serverTextSendSlices(WEBHTML.c_str(), WEBHTML.length());
    WEBHTML = "";
    WEBHTML.reserve(WEBHTML_CHUNK_BYTES);
  }
}

void serverTextFlush(bool force) {
  if (!s_webStreamActive || WEBHTML.length() == 0) return;
  if (!force && WEBHTML.length() < WEBHTML_CHUNK_BYTES) return;
  serverTextSendSlices(WEBHTML.c_str(), WEBHTML.length());
  WEBHTML = "";
  WEBHTML.reserve(WEBHTML_CHUNK_BYTES);
}

void serverTextAppend(const char* s) {
  if (!s || !*s) return;
  if (!s_webStreamActive) {
    WEBHTML += s;
    return;
  }
  // Stream large literals in fixed-size pieces so WEBHTML never holds an 11KB+ blob.
  serverTextFlush(true);
  serverTextSendSlices(s, strlen(s));
}

void serverTextClose(int htmlcode, bool asHTML) {
  if (asHTML) {
    WEBHTML += "</body></html>\n";
  }

  if (s_webStreamActive) {
    if (WEBHTML.length() > 0) {
      serverTextSendSlices(WEBHTML.c_str(), WEBHTML.length());
      WEBHTML = "";
    }
    server.sendContent(""); // final zero-length chunk
    s_webStreamActive = false;
    return;
  }

  const char* contentType = asHTML ? "text/html" : "text/plain";
  if (WEBHTML.length() > WEBHTML_CHUNK_BYTES) {
    serverTextSendBufferedChunks(contentType, htmlcode);
    return;
  }

  server.send(htmlcode, contentType, WEBHTML.c_str());
  WEBHTML = "";
}

static void appendSensorTablePlotCells(int16_t j, ArborysSnsType* sensor) {
#if _IS_SERVER_HUB
  WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA_MOVINGAVERAGE?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&windowSize=1800&numPointsX=48\" target=\"_blank\" rel=\"noopener noreferrer\">AvgHx</a></td>";
  WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&N=50\" target=\"_blank\" rel=\"noopener noreferrer\">History</a></td>";
#else
  (void)j;
  (void)sensor;
#endif
}

#if _HAS_LOCAL_SENSORS
static bool sensorHistoryUsesLocalMemory(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID) {
  int16_t snsIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
  return snsIndex >= 0 && Sensors.isMySensor(snsIndex);
}
#endif

static bool retrieveSensorDataForWebPlot(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID, byte* N,
    uint32_t* t, double* v, uint8_t* f, uint32_t starttime, uint32_t endtime, bool forwardOrder) {
#if _HAS_LOCAL_SENSORS
  if (sensorHistoryUsesLocalMemory(deviceMAC, snsType, snsID)) {
    return retrieveSensorDataFromMemory(deviceMAC, snsType, snsID, N, t, v, f, starttime, endtime, forwardOrder);
  }
#endif
#ifdef _USESDCARD
  return retrieveSensorDataFromSD(deviceMAC, snsType, snsID, N, t, v, f, starttime, endtime, forwardOrder);
#else
  (void)deviceMAC; (void)snsType; (void)snsID; (void)N; (void)t; (void)v; (void)f;
  (void)starttime; (void)endtime; (void)forwardOrder;
  return false;
#endif
}

static bool retrieveMovingAverageForWebPlot(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID,
    uint32_t starttime, uint32_t endtime, uint32_t windowSize, uint16_t* numPointsX,
    double* averagedValues, uint32_t* averagedTimes, uint8_t* averagedFlags, bool forwardOrder) {
#if _HAS_LOCAL_SENSORS
  if (sensorHistoryUsesLocalMemory(deviceMAC, snsType, snsID)) {
    return retrieveMovingAverageSensorDataFromMemory(deviceMAC, snsType, snsID, starttime, endtime,
        windowSize, numPointsX, averagedValues, averagedTimes, averagedFlags, forwardOrder);
  }
#endif
#ifdef _USESDCARD
  return retrieveMovingAverageSensorDataFromSD(deviceMAC, snsType, snsID, starttime, endtime,
      windowSize, numPointsX, averagedValues, averagedTimes, averagedFlags, forwardOrder);
#else
  (void)deviceMAC; (void)snsType; (void)snsID; (void)starttime; (void)endtime; (void)windowSize;
  (void)numPointsX; (void)averagedValues; (void)averagedTimes; (void)averagedFlags; (void)forwardOrder;
  return false;
#endif
}

#if _IS_SERVER_HUB
static String formatLimitInputValue(float v) {
  if (isnan(v)) return "?";
  return String(v, 4);
}

static String formatIntervalInputValue(uint32_t v) {
  if (v > 65535) v = 65535;
  return String(v);
}

static void appendSensorTableOverrideConfigCell(int16_t j, ArborysSnsType* sensor) {
  WEBHTML = WEBHTML + "<td style=\"padding: 8px;\">";
  WEBHTML = WEBHTML + "<details>";
  WEBHTML = WEBHTML + "<summary style=\"cursor: pointer; font-weight: bold; color: #4CAF50;\">Sensor Override Flags</summary>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SENSOR_OVERRIDE_UPDATE\" style=\"margin-top: 10px; padding: 10px; background-color: #f9f9f9; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsIndex\" value=\"" + String(j) + "\">";
  WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
  WEBHTML = WEBHTML + "<label style=\"font-weight: bold; display: block; margin-bottom: 4px;\">OverrideFlags:</label>";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: repeat(2, 1fr); gap: 4px; margin-left: 10px;\">";
  uint8_t currentOverrideFlags = sensor->OverrideFlags;
  // A checked bit forces that flag to 0, whatever the sensor sent.
  // Monitored after that opens the header and can open the icon boxes.
  // Critical after that opens the header and the 1.05× expiry recheck, and joins icon boxes already open.
  const char* overrideFlagNames[] = {"Clear flagged", "Clear monitored", "Clear low power", "Clear derived", "Clear outside", "Clear high/low", "Clear changed", "Clear critical"};
  for (int i = 0; i < 8; i++) {
    WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 4px;\">";
    WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"override_flag_bit" + String(i) + "\" value=\"1\"";
    if (bitRead(currentOverrideFlags, i)) WEBHTML = WEBHTML + " checked";
    WEBHTML = WEBHTML + ">";
    WEBHTML = WEBHTML + "<span style=\"font-size: 11px;\">" + String(i) + ":" + String(overrideFlagNames[i]) + "</span>";
    WEBHTML = WEBHTML + "</label>";
    serverTextFlush(false);
  }
  WEBHTML = WEBHTML + "</div></div>";
  WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Update Override</button>";
  WEBHTML = WEBHTML + "</form></details>";
  serverTextFlush(true);

  WEBHTML = WEBHTML + "<details style=\"margin-top: 8px;\">";
  WEBHTML = WEBHTML + "<summary style=\"cursor: pointer; font-weight: bold; color: #4CAF50;\">Sensor Limit Settings</summary>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SENSOR_LIMITS_UPDATE\" style=\"margin-top: 10px; padding: 10px; background-color: #f9f9f9; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsIndex\" value=\"" + String(j) + "\">";
  WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
  WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Upper Limit:</label>";
  WEBHTML = WEBHTML + "<input type=\"text\" name=\"limitHigh\" value=\"" + formatLimitInputValue(sensor->limitHigh) + "\" style=\"width: 100px; padding: 4px;\">";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
  WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Lower Limit:</label>";
  WEBHTML = WEBHTML + "<input type=\"text\" name=\"limitLow\" value=\"" + formatLimitInputValue(sensor->limitLow) + "\" style=\"width: 100px; padding: 4px;\">";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
  WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Poll Int (s):</label>";
  WEBHTML = WEBHTML + "<input type=\"number\" min=\"1\" max=\"65535\" name=\"intervalPoll\" value=\"" + formatIntervalInputValue(sensor->PollingInt) + "\" style=\"width: 100px; padding: 4px;\">";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
  WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Send Int (s):</label>";
  WEBHTML = WEBHTML + "<input type=\"number\" min=\"0\" max=\"65535\" name=\"intervalSend\" value=\"" + formatIntervalInputValue(sensor->SendingInt) + "\" style=\"width: 100px; padding: 4px;\">";
  WEBHTML = WEBHTML + "<span style=\"font-size: 11px; color: #666; margin-left: 8px;\">0 = only on alarm change</span>";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Submit</button>";
  WEBHTML = WEBHTML + "</form></details></td>";
  serverTextFlush(true);
}
#endif

static void appendSensorTableConfigCell(int16_t j, ArborysSnsType* sensor) {
  #if _HAS_LOCAL_SENSORS
  if (Sensors.isMySensor(j)) {
    int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(j);
    if (prefsIndex >= 0 && prefsIndex < _SENSORNUM) {
      WEBHTML = WEBHTML + "<td style=\"padding: 8px;\">";
      WEBHTML = WEBHTML + "<details>";
      WEBHTML = WEBHTML + "<summary style=\"cursor: pointer; font-weight: bold; color: #4CAF50;\">Sensor Details and Config</summary>";
      WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SENSOR_UPDATE\" style=\"margin-top: 10px; padding: 10px; background-color: #f9f9f9; border: 1px solid #ddd;\">";
      WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsType\" value=\"" + String(sensor->snsType) + "\">";
      WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsID\" value=\"" + String(sensor->snsID) + "\">";
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Sensor Name:</label>";
      WEBHTML = WEBHTML + "<input type=\"text\" name=\"sensorName\" maxlength=\"29\" value=\"" + String(sensor->snsName) + "\" style=\"width: 200px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      serverTextFlush(false);
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Max Limit:</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" name=\"limitMax\" value=\"" + String(Prefs.SNS_LIMIT_MAX[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Min Limit:</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" name=\"limitMin\" value=\"" + String(Prefs.SNS_LIMIT_MIN[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      serverTextFlush(false);
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Poll Int (s):</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" name=\"intervalPoll\" value=\"" + String(Prefs.SNS_INTERVAL_POLL[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
  WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Send Int (s):</label>";
  WEBHTML = WEBHTML + "<input type=\"number\" min=\"0\" name=\"intervalSend\" value=\"" + String(Prefs.SNS_INTERVAL_SEND[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
  WEBHTML = WEBHTML + "<span style=\"font-size: 11px; color: #666; margin-left: 8px;\">0 = only on alarm change</span>";
      WEBHTML = WEBHTML + "</div>";
      serverTextFlush(false);
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"font-weight: bold; display: block; margin-bottom: 4px;\">Flags:</label>";
      WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: repeat(2, 1fr); gap: 4px; margin-left: 10px;\">";
      uint16_t currentFlags = Prefs.SNS_FLAGS[prefsIndex];
      const char* flagNames[] = {"Flagged", "Monitored", "LowPower", "Derived/Predictive", "Outside", "High/Low", "Changed", "Critical"};
      bool readOnlyBits[] = {true, false, true, false, false, true, true, false};
      for (int i = 0; i < 8; i++) {
        WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 4px;\">";
        WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"flag_bit" + String(i) + "\" value=\"1\"";
        if (bitRead(currentFlags, i)) WEBHTML = WEBHTML + " checked";
        if (readOnlyBits[i]) WEBHTML = WEBHTML + " disabled";
        WEBHTML = WEBHTML + ">";
        WEBHTML = WEBHTML + "<span style=\"font-size: 11px;";
        if (readOnlyBits[i]) WEBHTML = WEBHTML + " color: #888;";
        WEBHTML = WEBHTML + "\">" + String(i) + ":" + String(flagNames[i]);
        if (readOnlyBits[i]) WEBHTML = WEBHTML + " (auto)";
        WEBHTML = WEBHTML + "</span></label>";
        serverTextFlush(false);
      }
      WEBHTML = WEBHTML + "</div></div>";
      serverTextFlush(true);
      WEBHTML = WEBHTML + "<div style=\"display: flex; gap: 8px; margin-top: 8px;\">";
      WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Update</button>";
      WEBHTML = WEBHTML + "</form>";
      WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SENSOR_READ_SEND_NOW\" style=\"display: inline;\">";
      WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsType\" value=\"" + String(sensor->snsType) + "\">";
      WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsID\" value=\"" + String(sensor->snsID) + "\">";
      WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer;\">Read & Send</button>";
      WEBHTML = WEBHTML + "</form>";
      WEBHTML = WEBHTML + "<a href=\"/SENSOR_SETUP?snsType=" + String(sensor->snsType) + "&snsID=" + String(sensor->snsID) + "\" style=\"display: inline-block; padding: 6px 12px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer; text-decoration: none;\">Setup</a>";
      WEBHTML = WEBHTML + "</div></details></td>";
      serverTextFlush(true);
    } else {
      WEBHTML = WEBHTML + "<td style=\"padding: 8px;\">Configuration data not found because Index out of bounds: " + String(prefsIndex) + "</td>";
    }
    return;
  }
  #endif
  #if _IS_SERVER_HUB
  appendSensorTableOverrideConfigCell(j, sensor);
  #endif
}

static void appendSensorTableFlagsCell(ArborysSnsType* sensor) {
  #if _IS_SERVER_HUB
  char flagsBuf[9];
  char overrideFlagsBuf[9];
  Byte2Bin(sensor->Flags, flagsBuf);
  Byte2Bin(sensor->OverrideFlags, overrideFlagsBuf);
  WEBHTML = WEBHTML + "<td>F:" + String(flagsBuf) + "<br>O:" + String(overrideFlagsBuf) + "</td>";
  #endif
}

static void appendSensorTableFlaggedCell(ArborysSnsType* sensor) {
  WEBHTML = WEBHTML + "<td>" + (String) bitRead(sensor->Flags,0) + (String) (bitRead(sensor->Flags,6) ? "*" : "" ) + "</td>";
}

static void appendSensorTableExpiredCell(int16_t j, ArborysSnsType* sensor) {
  #if _HAS_LOCAL_SENSORS
  if (Sensors.isMySensor(j)) {
    WEBHTML = WEBHTML + "<td>" + (String) ((bitRead(sensor->Flags,7))?"Y":"N") + "</td>";
    return;
  }
  #endif
  #if _IS_SERVER_HUB
  WEBHTML = WEBHTML + "<td>" + (String) ((sensor->expired==0)?((bitRead(sensor->Flags,7))?"N*":"n"):((bitRead(sensor->Flags,7))?"<font color=\"#EE4B2B\">Y*</font>":"<font color=\"#EE4B2B\">y</font>")) + "</td>";
  #else
  WEBHTML = WEBHTML + "<td>" + (String) ((bitRead(sensor->Flags,7))?"Y":"N") + "</td>";
  #endif
}

void rootTableFill(int16_t j) {
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(j);
  if (!sensor) return;

  WEBHTML = WEBHTML + "<tr>";
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsName + "</td>";
#if _IS_SERVER_HUB
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsType + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsID + "</td>";
  WEBHTML = WEBHTML + "<td>" + Sensors.sensorIsOfType(sensor) + "</td>";
#endif
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsValue + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) (sensor->timeLogged ? dateifyLocal(sensor->timeLogged,"mm/dd hh:nn:ss") : "???") + "</td>";
#if _IS_SERVER_HUB
  appendSensorTableFlagsCell(sensor);
#endif
  appendSensorTableFlaggedCell(sensor);
  appendSensorTableExpiredCell(j, sensor);
  appendSensorTablePlotCells(j, sensor);
  serverTextFlush(true);
  appendSensorTableConfigCell(j, sensor);
  WEBHTML = WEBHTML + "</tr>";
  serverTextFlush(true);
}

void handleNotFound(){
  registerHTTPMessage("404");
  I.HTTP_INCOMING_ERRORS++;
  WEBHTML= "404: Not found"; // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
  serverTextClose(404,false);

}

void handleRETRIEVEDATA() {
  registerHTTPMessage("DataHx");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Sensor History");
  byte  N=100;
  uint8_t snsType = 0, snsID = 0;
  uint64_t deviceMAC = 0;
  uint32_t starttime=0, endtime=0;

  if (server.args()==0) {
    WEBHTML="Inappropriate call... use RETRIEVEDATA?MAC=1234567890AB&type=1&id=1&N=100&starttime=1234567890&endtime=1731761847";
    serverTextClose(401,false);
    return;
  }

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"MAC") {
      String macStr = server.arg(k);
      deviceMAC = strtoull(macStr.c_str(), NULL, 10);
      
    }
    if ((String)server.argName(k) == (String)"type")  snsType=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"id")  snsID=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"N")  N=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"starttime")  starttime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"endtime")  endtime=server.arg(k).toInt(); 
  }

  if (deviceMAC==0 || snsType == 0 || snsID==0) {
    WEBHTML="Inappropriate call... you provided MAC=" + (String) deviceMAC + ", snsType=" + snsType + ", and snsID=" + snsID + ". None of these can be zero";
    serverTextClose(302,false);
    return;
  }

  if (N>100) N=100; //don't let the array get too large!
  if (N==0) N=50;
  
  if (endtime<=0) endtime=-1; //this makes endtime the max value, will just read N values.

  uint32_t t[N]={0};
  double v[N]={0};
  uint8_t f[N]={0};
  uint32_t sampn=0; //sample number
  
  bool success=false;
  if (starttime>0) {
    success = retrieveSensorDataForWebPlot(deviceMAC, snsType, snsID, &N, t, v, f, starttime, endtime, true);
  } else {
    success = retrieveSensorDataForWebPlot(deviceMAC, snsType, snsID, &N, t, v, f, 0, endtime, true);
  }
  if (success == false)  {
    WEBHTML= "Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  SerialPrint("handleRETRIEVEDATA: N=" + (String) N + " t[0]=" + (String) t[0] + " t[N-1]=" + (String) t[N-1] + " v[0]=" + (String) v[0] + " v[N-1]=" + (String) v[N-1]);
  
  addPlotToHTML(t,v,N,deviceMAC,snsType,snsID);
}

void handleRETRIEVEDATA_MOVINGAVERAGE() {
  registerHTTPMessage("AvgHx");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Moving Average");

  if (server.args()==0) {
    WEBHTML="Inappropriate call... use RETRIEVEDATA_MOVINGAVERAGE?MAC=1234567890AB&type=1&id=1&starttime=1234567890&endtime=1731761847&windowSize=10&numPointsX=10 or RETRIEVEDATA_MOVINGAVERAGE?MAC=1234567890AB&type=1&id=1&endtime=1731761847&windowSize=10&numPointsX=10";
    serverTextClose(401,false);
    return;
  }


  uint64_t deviceMAC = 0;
  uint8_t snsType = 0, snsID = 0;
  uint32_t starttime=0, endtime=0;
  uint32_t windowSizeN=0;
  uint16_t numPointsX=0;

  for (byte k=0;k<server.args();k++) {
    if ((String)server.argName(k) == (String)"MAC") {
      String macStr = server.arg(k);
       deviceMAC = strtoull(macStr.c_str(), NULL, 10);
    }
    if ((String)server.argName(k) == (String)"type")  snsType=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"id")  snsID=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"starttime")  starttime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"endtime")  endtime=server.arg(k).toInt(); 
    if ((String)server.argName(k) == (String)"windowSizeN")  windowSizeN=server.arg(k).toInt();
    if ((String)server.argName(k) == (String)"windowSize")  windowSizeN=server.arg(k).toInt(); // New parameter name 
    if ((String)server.argName(k) == (String)"numPointsX")  numPointsX=server.arg(k).toInt(); 
  }

  if (deviceMAC==0 || snsType == 0 || snsID==0) {
    WEBHTML="Inappropriate call... invalid device MAC or sensor ID";
    serverTextClose(302,false);
    return;
  }

  if (windowSizeN==0) windowSizeN=30*60; //in minutes
  if (numPointsX==0 || numPointsX>100) numPointsX=0;

  if (endtime==0) endtime=-1;
  if (starttime>=endtime) {
    WEBHTML="Inappropriate call... starttime >= endtime";
    serverTextClose(302,false);
    return;
  }

  uint32_t t[100]={0};
  double v[100]={0};
  uint8_t f[100]={0};
  bool success=false;

  success = retrieveMovingAverageForWebPlot(deviceMAC, snsType, snsID, starttime, endtime, windowSizeN,
      &numPointsX, v, t, f, true);
  
  if (success == false)  {
    WEBHTML= "handleRETRIEVEDATA_MOVINGAVERAGE: Failed to read associated file.";
    serverTextClose(401,false);
    return;
  }

  addPlotToHTML(t,v,numPointsX,deviceMAC,snsType,snsID);
}

void addPlotToHTML(uint32_t t[], double v[], byte N, uint64_t deviceMAC, uint8_t snsType, uint8_t snsID) {


  // Find sensor in Devices_Sensors class
  int16_t sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
  ArborysSnsType* sensor = Sensors.snsIndexToPointer(sensorIndex);

  WEBHTML = "<!DOCTYPE html><html><head><title>" + (String) Prefs.DEVICENAME + "</title>\n";
  WEBHTML =WEBHTML  + (String) "<style> table {  font-family: arial, sans-serif;  border-collapse: collapse;width: 100%;} td, th {  border: 1px solid #dddddd;  text-align: left;  padding: 8px;}tr:nth-child(even) {  background-color: #dddddd;}";
  WEBHTML =WEBHTML  + (String) "body {  font-family: arial, sans-serif; }";
  WEBHTML =WEBHTML  + "</style></head>";
  WEBHTML =WEBHTML  + "<script src=\"https://www.gstatic.com/charts/loader.js\"></script>\n";
  serverTextStreamBegin(200, true);
  WEBHTML = WEBHTML + "<h1>" + (String) Prefs.DEVICENAME + "</h1>";
  WEBHTML = WEBHTML + "<br>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br>\n";

  WEBHTML = WEBHTML + "<p>";

  if (sensorIndex<0)   WEBHTML += "WARNING!! Device: " + String(deviceMAC, HEX) + " sensor type: " + (String) snsType + " id: " + (String) snsID + " was NOT found in the active list, though I did find an associated file. <br>";
  else {
    WEBHTML += "Request for Device: " + String(deviceMAC, HEX) + " sensor: " + (String) sensor->snsName + " type: " + (String) sensor->snsType + " id: " + (String) sensor->snsID + "<br>";
  }

  WEBHTML += "Start time: " + (String) dateifyLocal(t[0],"mm/dd/yyyy hh:nn:ss") + " to " + (String) dateifyLocal(t[N-1],"mm/dd/yyyy hh:nn:ss")  +  "<br>";
  
  //add chart
  WEBHTML += "<br>-----------------------<br>\n";
  WEBHTML += "<div id=\"myChart\" style=\"width:100%; max-width:800px; height:600px;\"></div>\n";
  WEBHTML += "<br>-----------------------<br>\n";

  WEBHTML += "</p>\n";

  WEBHTML =WEBHTML  + "<script>";

  //chart functions
    WEBHTML =WEBHTML  + "google.charts.load('current',{packages:['corechart']});\n";
    WEBHTML =WEBHTML  + "google.charts.setOnLoadCallback(drawChart);\n";
    
    WEBHTML += "function drawChart() {\n";

    WEBHTML += "const data = google.visualization.arrayToDataTable([\n";
    WEBHTML += "['t','val'],\n";

    for (byte jj = 0;jj<N;jj++) {
      if (isTimeValid(t[jj])==false) continue;
      WEBHTML += "[" + (String) (int64_t) (((int64_t) t[jj] - (int64_t) t[N-1])/60) + "," + (String) v[jj] + "]";
      if (jj<N-1) WEBHTML += ",";
      WEBHTML += "\n";
      serverTextFlush(true);
    }
    WEBHTML += "]);\n\n";

        // Set Options
    WEBHTML += "const options = {\n";
    if (sensor) {
      WEBHTML += "hAxis: {title: 'Historical data for " + (String) sensor->snsName + ", minutes from last'}, \n";
    } else {
      WEBHTML += "hAxis: {title: 'Historical data in hours'}, \n";
    }
    WEBHTML += "vAxis: {title: 'Value'},\n";
    WEBHTML += "legend: 'none'\n};\n";

    WEBHTML += "const chart = new google.visualization.LineChart(document.getElementById('myChart'));\n";
    WEBHTML += "chart.draw(data, options);\n"; 
    WEBHTML += "}\n";  

  WEBHTML += "</script> \n";
    WEBHTML += "Returned " + (String) N + " avg samples. <br>\n";

    WEBHTML += "unixtime,value<br>\n";
  for (byte j=0;j<N;j++) {
    WEBHTML += (String) t[j] + "," + (String) v[j] + "<br>\n";
    serverTextFlush(true);
  }
  
  serverTextClose(200);
}



void handleCONFIG() {
  registerHTTPMessage("CONFIG");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("System Configuration");
  serverTextStreamBegin(200, true);

  // Navigation buttons (WiFi Config only on this page)
  appendStandardPageNav(true);
  serverTextFlush(true);

  WEBHTML = WEBHTML + "<p>This page is used to configure editable system parameters.</p>";
  
  // Start the form and grid container
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/CONFIG\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Editable fields from STRUCT_CORE I in specified order
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">UTC Offset (sec)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" id=\"utc_offset\" name=\"utc_offset\" value=\"" + (String) Prefs.TimeZoneOffset + "\" maxlength=\"7\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";

  //add DST value box where values are 0 (no DST) or 1 (DST is used) or 2 (DST is active)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">DST is used in this locale (0 = no, 1 = yes but not active, 2 = active)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" id=\"dst_enabled\" name=\"dst_enabled\" value=\"" + (String) Prefs.DST + "\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  //add DST start unixtime and end unixtime and DST offset
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">DST Start</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" id=\"dst_start_date\" name=\"dst_start_date\" value=\"" + (String) dateify(Prefs.DSTStartUnixTime,"mm/dd/yyyy hh:nn") + "\" maxlength=\"20\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">DST End</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" id=\"dst_end_date\" name=\"dst_end_date\" value=\"" + (String) dateify(Prefs.DSTEndUnixTime,"mm/dd/yyyy hh:nn") + "\" maxlength=\"20\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  serverTextFlush(true);
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">DST Offset (sec)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" id=\"dst_offset\" name=\"dst_offset\" value=\"" + (String) Prefs.DSTOffset + "\" maxlength=\"10\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Autodetect</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">";
  WEBHTML = WEBHTML + "<input type=\"button\" id=\"detect-tz-btn\" value=\"Autodetect Timezone\" onclick=\"autodetectTimezone()\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "<input type=\"button\" id=\"detect-dst-btn\" value=\"Autodetect Daylight Savings\" onclick=\"autodetectDST()\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</div>";
    
  // Device Name Configuration
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f0f0f0;\">Device Name</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"text\" id=\"deviceName\" name=\"deviceName\" value=\"" + String(Prefs.DEVICENAME) + "\" maxlength=\"32\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";

  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  serverTextFlush(true);

  #if _SUPABASE_RUNTIME
  WEBHTML += "<br><hr><h3>ArborysNet</h3>";
  WEBHTML += "<div id=\"cfg-cloud-status\" style=\"margin:8px 0;\"></div>";
  {
    const bool hasCreds = supabaseHasStoredCredentials();
    const bool connected = supabaseIsConnected();
    WEBHTML += "<p style=\"margin:8px 0;\">Connected: <strong id=\"cfg-connected-flag\">";
    WEBHTML += connected ? "Yes" : "No";
    WEBHTML += "</strong>";
    WEBHTML += " <span id=\"cfg-has-creds\" style=\"display:none\">";
    WEBHTML += hasCreds ? "1" : "0";
    WEBHTML += "</span>";
    WEBHTML += " &nbsp; Current location: <strong id=\"cfg-current-site\">";
    if (hasCreds) WEBHTML += supabaseSiteSlug();
    WEBHTML += "</strong></p>";
    WEBHTML += "<div style=\"margin:10px 0;\"><label>Claim code </label>";
    WEBHTML += "<input type=\"text\" id=\"cfg_claim_code\" maxlength=\"8\" style=\"padding:8px; text-transform:uppercase;\"> ";
    WEBHTML += "<button type=\"button\" onclick=\"cfgClaimDevice()\" style=\"padding:8px 16px; background:#4CAF50; color:white; border:none; border-radius:4px; cursor:pointer;\">Claim / Re-claim</button> ";
    WEBHTML += "<button type=\"button\" onclick=\"cfgQuitArborysNet()\" id=\"cfg-quit-arborysnet-btn\" style=\"padding:8px 16px; background:#f44336; color:white; border:none; border-radius:4px; cursor:pointer;\"";
    if (!hasCreds) WEBHTML += " disabled";
    WEBHTML += ">Quit ArborysNet</button></div>";
    WEBHTML += "<p style=\"font-size:13px;color:#666;margin:4px 0 10px;\">Quit clears this device&apos;s local cloud credentials (claim stays in Supabase until you reclaim).</p>";
    WEBHTML += "<div id=\"cfg-site-wrap\" style=\"margin:10px 0;\"><label>Location </label>";
    WEBHTML += "<div style=\"margin-top:6px;\">";
    WEBHTML += "<select id=\"cfg_site_select\" size=\"6\" style=\"padding:8px; min-width:280px; width:100%; max-width:420px; display:block;\">";
    if (hasCreds) {
      const char* cur = supabaseSiteSlug();
      WEBHTML += "<option value=\"";
      WEBHTML += cur;
      WEBHTML += "\" selected>";
      WEBHTML += cur;
      WEBHTML += " (current)</option>";
    } else {
      WEBHTML += "<option value=\"\">(claim device, then Refresh locations)</option>";
    }
    WEBHTML += "</select></div>";
    WEBHTML += "<div style=\"margin-top:8px;\">";
    WEBHTML += "<button type=\"button\" onclick=\"cfgLoadSites()\" id=\"cfg-refresh-sites-btn\" style=\"padding:8px 16px; background:#607D8B; color:white; border:none; border-radius:4px; cursor:pointer;\"";
    if (!hasCreds) WEBHTML += " disabled";
    WEBHTML += ">Refresh locations</button> ";
    WEBHTML += "<button type=\"button\" onclick=\"cfgAssignSite()\" style=\"padding:8px 16px; background:#2196F3; color:white; border:none; border-radius:4px; cursor:pointer;\">Save location</button></div></div>";
    WEBHTML += "<p style=\"font-size:13px;color:#666;margin:4px 0 10px;\">Location list loads from ArborysNet only when you tap <em>Refresh locations</em>.</p>";
    WEBHTML += "<div style=\"margin:10px 0;\"><label><input type=\"checkbox\" id=\"cfg_upload_supabase\" ";
    if (Prefs.UPLOAD_TO_SUPABASE) WEBHTML += "checked ";
    WEBHTML += "onchange=\"cfgSetUploadToSupabase(this.checked)\"> Upload sensor readings to ArborysNet</label>";
    WEBHTML += "<p style=\"font-size:12px;color:#666;margin:4px 0 0;\">Default on for hubs, off for peripherals. Peripherals still upload if no server contact for 6 hours.</p></div>";
    #if _IS_SERVER_HUB
    WEBHTML += "<div id=\"cfg-create-wrap\" style=\"margin:14px 0; padding:12px; border:1px solid #ddd;";
    if (!hasCreds) WEBHTML += "display:none;";
    WEBHTML += "\"><strong>New location (hub)</strong><br>";
    WEBHTML += "<input type=\"text\" id=\"cfg_new_site_description\" placeholder=\"Site description\" maxlength=\"64\" style=\"padding:8px; margin:4px;\"> ";
    WEBHTML += "<input type=\"text\" id=\"cfg_new_site_label\" placeholder=\"Site label\" maxlength=\"24\" style=\"padding:8px; margin:4px; text-transform:lowercase;\"> ";
    WEBHTML += "<button type=\"button\" onclick=\"cfgCreateSite()\" style=\"padding:8px 16px; background:#FF9800; color:white; border:none; border-radius:4px; cursor:pointer;\">Create &amp; assign</button></div>";
    WEBHTML += "<div id=\"cfg-delete-wrap\" style=\"margin:14px 0; padding:12px; border:1px solid #f5c6cb;";
    if (!hasCreds) WEBHTML += "display:none;";
    WEBHTML += "\"><strong>Delete location (hub)</strong>";
    WEBHTML += "<div id=\"cfg-site-list\" style=\"margin:8px 0; font-size:14px;\"><em>Refresh locations to list</em></div>";
    WEBHTML += "<p style=\"font-size:13px;color:#666;\">Deleting a location moves its devices to another location (or creates <code>home</code> if it was the last one).</p>";
    WEBHTML += "</div>";
    WEBHTML += "<div id=\"cfg-inventory-wrap\" style=\"margin:14px 0; padding:12px; border:1px solid #c8e6c9;";
    if (!hasCreds) WEBHTML += "display:none;";
    WEBHTML += "\"><strong>Peripheral inventory (hub)</strong><br>";
    WEBHTML += "<p style=\"font-size:13px;color:#666;\">Queries this location for sensors that reported in the last 24 hours and adds any unknown peripherals. Manual only for now (automatic poll deferred).</p>";
    WEBHTML += "<button type=\"button\" onclick=\"cfgQueryArborysNet()\" id=\"cfg-inventory-btn\" style=\"padding:8px 16px; background:#009688; color:white; border:none; border-radius:4px; cursor:pointer;\">Query ArborysNet</button>";
    WEBHTML += "</div>";
    #endif
    serverTextFlush(true);
  }
  #endif

  WEBHTML = WEBHTML + "<br><a href=\"/REBOOT\" style=\"display: inline-block; margin-top: 8px; padding: 10px 20px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">Reboot</a>";

  //make another form that changes the ota slot
  {
    String otaSwitchLabel = "Switch to OTA slot ?: ver ?";
    const esp_partition_t* targetPart = esp_ota_get_next_update_partition(NULL);
    const int8_t targetSlot = otaPartitionSlotNumber(targetPart);
    String nextVer = "?";
    FirmwareVersion nextFw;
    if (getOtaPartitionFirmwareVersion(targetPart, nextFw)) {
      char verBuf[16];
      nextFw.toChar(verBuf, sizeof(verBuf));
      nextVer = verBuf;
    }
    const String slotStr = (targetSlot >= 0) ? String(targetSlot) : "?";
    otaSwitchLabel = "Switch to OTA slot " + slotStr + ": ver " + nextVer;
    WEBHTML = WEBHTML + "<br><br><form method=\"POST\" action=\"/CONFIG_OTA_SWITCH\" style=\"display: inline;\">";
    WEBHTML = WEBHTML + "<input type=\"submit\" value=\"" + otaSwitchLabel + "\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
    WEBHTML = WEBHTML + "</form>";
  }

  // Reset button
  WEBHTML = WEBHTML + "<br><br><form method=\"POST\" action=\"/CONFIG_DELETE\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Reset All Settings\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  serverTextFlush(true);
  
  WEBHTML = WEBHTML + "</body>";
  WEBHTML = WEBHTML + "<script>";
  serverTextAppend(R"===(
function isValidTimezoneOffset(offset) {
  return offset !== undefined && offset >= -50400 && offset <= 50400 && offset !== 90000;
}

function fillDstFields(data) {
  document.getElementById('dst_enabled').value = data.dst_enabled;
  document.getElementById('dst_start_date').value = data.dst_start_date || '';
  document.getElementById('dst_end_date').value = data.dst_end_date || '';
  document.getElementById('dst_offset').value = data.dst_offset !== undefined ? data.dst_offset : '';
}

async function autodetectTimezone() {
  const btn = document.getElementById('detect-tz-btn');
  btn.disabled = true;
  btn.value = "Detecting...";
  try {
    const response = await fetch('/api/timezone');
    const data = await response.json();
    if (data.success === true && isValidTimezoneOffset(data.utc_offset)) {
      document.getElementById('utc_offset').value = data.utc_offset;
      alert('Timezone (UTC offset) detected!');
    } else {
      alert('Timezone detection failed. Please set manually.');
    }
  } catch (error) {
    alert('Timezone detection failed. Please set manually.');
  } finally {
    btn.disabled = false;
    btn.value = "Autodetect Timezone";
  }
}

async function autodetectDST() {
  const btn = document.getElementById('detect-dst-btn');
  btn.disabled = true;
  btn.value = "Detecting...";
  try {
    const response = await fetch('/api/timezone/dst');
    const data = await response.json();
    if (data.success === true) {
      fillDstFields(data);
      alert('Daylight saving time settings detected!');
    } else {
      alert('DST detection failed. Please set manually.');
    }
  } catch (error) {
    alert('DST detection failed. Please set manually.');
  } finally {
    btn.disabled = false;
    btn.value = "Autodetect Daylight Savings";
  }
}
)===");
  serverTextFlush(true);

  #if _SUPABASE_RUNTIME
  // Stream ArborysNet JS in small pieces � a single ~11KB String append OOMs classic ESP32 peripherals.
  WEBHTML += "const CFG_IS_HUB=";
  #if _IS_SERVER_HUB
  WEBHTML += "true;";
  #else
  WEBHTML += "false;";
  #endif
  serverTextAppend(R"===(
function cfgHasCreds() {
  const el = document.getElementById('cfg-has-creds');
  return !!(el && el.textContent.trim() === '1');
}
function cfgIsConnected() {
  const flag = document.getElementById('cfg-connected-flag');
  return !!(flag && flag.textContent.trim() === 'Yes');
}
function cfgSetConnectedUi(connected) {
  // Live-success badge only � does not gate actions or clear credentials.
  const flag = document.getElementById('cfg-connected-flag');
  if (flag) flag.textContent = connected ? 'Yes' : 'No';
}
function cfgSetHasCreds(has) {
  const el = document.getElementById('cfg-has-creds');
  if (el) el.textContent = has ? '1' : '0';
  const refreshBtn = document.getElementById('cfg-refresh-sites-btn');
  if (refreshBtn) refreshBtn.disabled = !has;
  const quitBtn = document.getElementById('cfg-quit-arborysnet-btn');
  if (quitBtn) quitBtn.disabled = !has;
  ['cfg-create-wrap', 'cfg-delete-wrap', 'cfg-inventory-wrap'].forEach(function(id) {
    const w = document.getElementById(id);
    if (w) w.style.display = has ? 'block' : 'none';
  });
  if (!has) {
    const cur = document.getElementById('cfg-current-site');
    if (cur) cur.textContent = '';
  }
}
function cfgHandleApiFailure(data, fallbackMsg) {
  const err = (data && data.error) ? data.error : (fallbackMsg || 'failed');
  const invalid = !!(data && (data.invalid_device || data.code === 'invalid_device' ||
    /invalid_device|Invalid device credentials/i.test(String(err))));
  cfgSetConnectedUi(false);
  if (invalid) {
    cfgSetHasCreds(false);
    cfgCloudMsg(err + ' (credentials cleared. Re-claim required)');
    return true;
  }
  cfgCloudMsg(err);
  return false;
}
function cfgCloudMsg(msg) {
  const el = document.getElementById('cfg-cloud-status');
  if (el) el.textContent = msg || '';
}
function cfgStamp() {
  try { return new Date().toLocaleString(); } catch (e) { return ''; }
}
function cfgEsc(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
)===");
  serverTextFlush(true);

  #if _IS_SERVER_HUB
  serverTextAppend(R"===(
function cfgRenderSiteList(sites, current) {
  const list = document.getElementById('cfg-site-list');
  if (!list) return;
  const rows = sites || [];
  if (!rows.length) {
    list.innerHTML = '<em>No locations</em>';
    return;
  }
  const onlyOne = rows.length === 1;
  let html = '<table style="border-collapse:collapse;width:100%;max-width:480px;"><tr><th style="text-align:left;padding:4px;border-bottom:1px solid #ddd;">Description</th><th style="text-align:left;padding:4px;border-bottom:1px solid #ddd;">Label</th><th></th></tr>';
  rows.forEach(s => {
    const label = s.label || s.slug || '';
    if (!label) return;
    const desc = s.description || s.name || label;
    const isCurrent = (label === current);
    const isSoleHome = onlyOne && (label === 'home' || label === current);
    html += '<tr><td style="padding:6px 4px;">' + cfgEsc(desc) + (isCurrent ? ' <em>(current)</em>' : '') +
      '</td><td style="padding:6px 4px;"><code>' + cfgEsc(label) + '</code></td><td style="padding:6px 4px;">';
    if (isSoleHome) {
      html += '<span style="color:#666;font-size:13px;">Cannot delete (only location)</span>';
    } else {
      html += '<button type="button" data-site="' + cfgEsc(label) + '" onclick="cfgDeleteSite(this.getAttribute(\'data-site\'))" style="padding:4px 10px;background:#f44336;color:white;border:none;border-radius:4px;cursor:pointer;">Delete</button>';
    }
    html += '</td></tr>';
  });
  html += '</table>';
  list.innerHTML = html;
}
)===");
  #else
  WEBHTML += "function cfgRenderSiteList(sites, current) {}\n";
  #endif
  serverTextFlush(true);

  serverTextAppend(R"===(
async function cfgLoadSites() {
  if (!cfgHasCreds()) {
    cfgCloudMsg('Claim the device before refreshing locations');
    return;
  }
  const refreshBtn = document.getElementById('cfg-refresh-sites-btn');
  if (refreshBtn) refreshBtn.disabled = true;
  const sel = document.getElementById('cfg_site_select');
  const prevValue = sel ? sel.value : '';
  try {
    cfgCloudMsg('Loading locations...');
    for (let attempt = 0; attempt < 40; attempt++) {
      let response;
      let data;
      try {
        response = await fetch('/api/arborysnet/sites');
        data = await response.json();
      } catch (parseErr) {
        cfgSetConnectedUi(false);
        cfgCloudMsg('Error refreshing sites: ' + (parseErr && parseErr.message ? parseErr.message : 'bad response'));
        return;
      }
      if (data.pending) {
        cfgCloudMsg('Loading locations... (' + (attempt + 1) + ')');
        await new Promise(r => setTimeout(r, 1500));
        continue;
      }
      if (data.tls_busy) {
        cfgCloudMsg(data.error || 'Please wait, TLS client occupied');
        return;
      }
      if (!data.success) {
        cfgHandleApiFailure(data, 'HTTP ' + response.status);
        return;
      }
      const sites = data.sites || [];
      if (sel) {
        sel.innerHTML = '';
        if (!sites.length) {
          const fallback = data.current || prevValue || 'home';
          const opt = document.createElement('option');
          opt.value = fallback;
          opt.textContent = fallback + ' (current)';
          opt.selected = true;
          sel.appendChild(opt);
        } else {
          let matched = false;
          sites.forEach(s => {
            const label = s.label || s.slug || '';
            if (!label) return;
            const desc = s.description || s.name || label;
            const opt = document.createElement('option');
            opt.value = label;
            opt.textContent = (label === data.current)
              ? (label + ' (current)')
              : ((desc === label) ? label : (desc + ' [' + label + ']'));
            if (label === data.current || label === prevValue) {
              opt.selected = true;
              matched = true;
            }
            sel.appendChild(opt);
          });
          if (!matched && data.current) {
            const opt = document.createElement('option');
            opt.value = data.current;
            opt.textContent = data.current + ' (current)';
            opt.selected = true;
            sel.insertBefore(opt, sel.firstChild);
          }
          if (data.current) sel.value = data.current;
          else if (prevValue) sel.value = prevValue;
        }
      }
      const cur = document.getElementById('cfg-current-site');
      if (cur) cur.textContent = (sel && sel.value) || data.current || '';
      cfgRenderSiteList(sites.length ? sites : [{ label: (data.current || prevValue || 'home'), description: (data.current || prevValue || 'home') }], data.current || (sel && sel.value) || '');
      cfgSetConnectedUi(true);
      cfgCloudMsg('Sites refreshed at ' + cfgStamp());
      return;
    }
    cfgCloudMsg('Error refreshing sites: still loading after timeout. Tap Refresh locations to retry.');
  } catch (e) { cfgCloudMsg('Error refreshing sites: ' + e.message); }
  finally {
    if (refreshBtn && cfgHasCreds()) refreshBtn.disabled = false;
  }
}
)===");
  serverTextFlush(true);

  serverTextAppend(R"===(
async function cfgClaimDevice() {
  const code = document.getElementById('cfg_claim_code').value;
  if (!code) { cfgCloudMsg('Enter claim code'); return; }
  const formData = new FormData();
  formData.append('claim_code', code);
  cfgCloudMsg('Claiming...');
  try {
    const response = await fetch('/api/arborysnet/claim', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      cfgSetHasCreds(true);
      cfgSetConnectedUi(true);
      cfgCloudMsg('Connected: Yes. Tap Refresh locations, then choose a location.');
    } else {
      cfgSetConnectedUi(false);
      cfgCloudMsg(data.error || 'Claim failed');
    }
  } catch (e) { cfgCloudMsg('Error: ' + e.message); }
}
async function cfgQuitArborysNet() {
  if (!cfgHasCreds()) {
    cfgCloudMsg('Device is not claimed');
    return;
  }
  if (!confirm('Quit ArborysNet on this device? Local cloud credentials will be cleared. You can claim again later with a code.')) {
    return;
  }
  const quitBtn = document.getElementById('cfg-quit-arborysnet-btn');
  if (quitBtn) quitBtn.disabled = true;
  cfgCloudMsg('Clearing ArborysNet credentials...');
  try {
    const response = await fetch('/api/arborysnet/quit', { method: 'POST' });
    const data = await response.json();
    if (data.success) {
      cfgSetHasCreds(false);
      cfgSetConnectedUi(false);
      const code = document.getElementById('cfg_claim_code');
      if (code) code.value = '';
      cfgCloudMsg('ArborysNet quit. Local credentials cleared. Device is LAN-only until re-claimed.');
    } else {
      cfgCloudMsg(data.error || 'Quit failed');
      if (quitBtn && cfgHasCreds()) quitBtn.disabled = false;
    }
  } catch (e) {
    cfgCloudMsg('Error: ' + e.message);
    if (quitBtn && cfgHasCreds()) quitBtn.disabled = false;
  }
}
async function cfgSetUploadToSupabase(enabled) {
  const formData = new FormData();
  formData.append('enabled', enabled ? '1' : '0');
  try {
    const response = await fetch('/api/arborysnet/upload', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      cfgCloudMsg(enabled ? 'Upload to ArborysNet enabled' : 'Upload to ArborysNet disabled');
    } else {
      cfgCloudMsg(data.error || 'Failed to save upload setting');
      const cb = document.getElementById('cfg_upload_supabase');
      if (cb) cb.checked = !enabled;
    }
  } catch (e) {
    cfgCloudMsg('Error: ' + e.message);
    const cb = document.getElementById('cfg_upload_supabase');
    if (cb) cb.checked = !enabled;
  }
}
async function cfgAssignSite() {
  if (!cfgHasCreds()) { cfgCloudMsg('Claim the device first'); return; }
  const label = document.getElementById('cfg_site_select').value;
  if (!label) { cfgCloudMsg('Select a location (refresh the list first)'); return; }
  const formData = new FormData();
  formData.append('site_label', label);
  try {
    const response = await fetch('/api/arborysnet/site', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      cfgSetConnectedUi(true);
      const cur = document.getElementById('cfg-current-site');
      if (cur) cur.textContent = data.label || data.site || label;
      cfgCloudMsg('Location saved: ' + (data.label || data.site || label));
    } else {
      cfgHandleApiFailure(data, 'Failed');
    }
  } catch (e) { cfgSetConnectedUi(false); cfgCloudMsg('Error: ' + e.message); }
}
)===");
  serverTextFlush(true);

  #if _IS_SERVER_HUB
  serverTextAppend(R"===(
async function cfgCreateSite() {
  if (!CFG_IS_HUB) return;
  if (!cfgHasCreds()) { cfgCloudMsg('Claim the device first'); return; }
  const descEl = document.getElementById('cfg_new_site_description');
  const labelEl = document.getElementById('cfg_new_site_label');
  const description = descEl ? descEl.value : '';
  let label = labelEl ? labelEl.value : '';
  if (!label && description) {
    label = description.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').substring(0, 24);
  }
  if (!label) { cfgCloudMsg('Enter a site label or description'); return; }
  if (label.length > 24) label = label.substring(0, 24);
  const formData = new FormData();
  formData.append('site_label', label);
  if (description) formData.append('site_description', description.substring(0, 64));
  try {
    const response = await fetch('/api/arborysnet/site/create', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      const saved = data.label || data.site || label;
      cfgCloudMsg('Created: ' + saved);
      await cfgLoadSites();
      document.getElementById('cfg_site_select').value = saved;
      const cur = document.getElementById('cfg-current-site');
      if (cur) cur.textContent = saved;
    } else {
      cfgHandleApiFailure(data, 'Create failed');
    }
  } catch (e) { cfgSetConnectedUi(false); cfgCloudMsg('Error: ' + e.message); }
}
async function cfgDeleteSite(label) {
  if (!CFG_IS_HUB || !label) return;
  if (!cfgHasCreds()) { cfgCloudMsg('Claim the device first'); return; }
  const msg = 'Delete location \"' + label + '\"?' +
    '\\n\\nDevices on this location will be moved to another location (or to a new \"home\" if this is the last one).' +
    '\\n\\nThis cannot be undone.';
  if (!confirm(msg)) return;
  if (!confirm('Confirm delete location \"' + label + '\"?')) return;
  const formData = new FormData();
  formData.append('site_label', label);
  cfgCloudMsg('Deleting ' + label + '...');
  try {
    const response = await fetch('/api/arborysnet/site/delete', { method: 'POST', body: formData });
    const data = await response.json();
    if (data.success) {
      cfgCloudMsg('Deleted \"' + label + '\". Current: ' + (data.current || ''));
      await cfgLoadSites();
    } else {
      cfgHandleApiFailure(data, 'Delete failed');
    }
  } catch (e) { cfgSetConnectedUi(false); cfgCloudMsg('Error: ' + e.message); }
}
async function cfgQueryArborysNet() {
  if (!CFG_IS_HUB) return;
  if (!cfgHasCreds()) { cfgCloudMsg('Claim the device first'); return; }
  const btn = document.getElementById('cfg-inventory-btn');
  if (btn) { btn.disabled = true; btn.textContent = 'Querying...'; }
  cfgCloudMsg('Querying ArborysNet for location peripherals...');
  try {
    const response = await fetch('/api/arborysnet/inventory', { method: 'POST' });
    const data = await response.json();
    if (data.success) {
      cfgSetConnectedUi(true);
      cfgCloudMsg('Inventory: queried ' + data.sensors_queried +
        ' sensor(s), added ' + data.sensors_added + ' sensor(s) / ' +
        data.devices_added + ' device(s).');
    } else {
      cfgHandleApiFailure(data, 'Inventory query failed');
    }
  } catch (e) { cfgSetConnectedUi(false); cfgCloudMsg('Error: ' + e.message); }
  finally {
    if (btn) { btn.disabled = false; btn.textContent = 'Query ArborysNet'; }
  }
}
)===");
  serverTextFlush(true);
  #endif
  #endif

  WEBHTML = WEBHTML + "</script>";
  WEBHTML = WEBHTML + "</html>";

  serverTextClose(200, true);
}

void handleGSHEET() {
  registerHTTPMessage("GSHEET");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Google Sheets");
  serverTextStreamBegin(200, true);
  #if defined(_USEGSHEET)
  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush(true);

  WEBHTML = WEBHTML + "<p>This page displays Google Sheets configuration and status.</p>";
  
  // Start the form and grid container
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/GSHEET\">";
  WEBHTML = WEBHTML + "<div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0;\">";
  
  // Header row
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Field Name</div>";
  WEBHTML = WEBHTML + "<div style=\"background-color: #f0f0f0; padding: 12px; font-weight: bold; border: 1px solid #ddd;\">Value</div>";

  // Google Sheets Configuration Section (Editable)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9; font-weight: bold;\">Google Sheets Configuration (Editable)</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">useGsheet</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"checkbox\" name=\"useGsheet\" value=\"1\"" + ((GSheetInfo.useGsheet) ? " checked" : "") + " style=\"width: 20px; height: 20px;\"></div>";
  
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">uploadGsheetIntervalMinutes</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\"><input type=\"number\" name=\"uploadGsheetIntervalMinutes\" value=\"" + (String) GSheetInfo.uploadGsheetIntervalMinutes + "\" min=\"1\" max=\"1440\" style=\"width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 4px;\"></div>";
  
  // Google Sheets Status Information (Read-only)
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9; font-weight: bold;\">Google Sheets Status Information</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd; background-color: #f9f9f9;\"></div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetUploadSuccess</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) GSheetInfo.lastGsheetUploadSuccess + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">uploadGsheetFailCount</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) GSheetInfo.uploadGsheetFailCount + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastErrorTime</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) ((GSheetInfo.lastErrorTime) ? dateifyLocal(GSheetInfo.lastErrorTime) : "N/A") + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetResponse</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.lastGsheetResponse) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">lastGsheetFunction</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.lastGsheetFunction) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">GsheetID</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.GsheetID) + "</div>";

  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">GsheetName</div>";
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + String(GSheetInfo.GsheetName) + "</div>";


  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Google Sheets Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Upload now button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_UPLOAD_NOW\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Upload to Gsheets Immediately\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Share all sheets button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_SHARE_ALL\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Share Device Sheets\" style=\"padding: 10px 20px; background-color: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Delete all sheets button
  WEBHTML = WEBHTML + "<br><form action=\"/GSHEET_DELETE_ALL\" method=\"post\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete All Sheets\" style=\"padding: 10px 20px; background-color: #F44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  #else
  WEBHTML = WEBHTML + "<p>Google Sheets upload is not available on this device</p>";
  #endif
  
  WEBHTML = WEBHTML + "</body></html>";

  serverTextClose(200, true);
}

void handleGSHEET_POST() {
  registerHTTPMessage("GSHEETOut");

  #if defined(_USEGSHEET) 
  // Process Google Sheets configuration
  if (server.hasArg("useGsheet")) {
    GSheetInfo.useGsheet = true;
  } else {
    GSheetInfo.useGsheet = false;
  }
  if (server.hasArg("uploadGsheetIntervalMinutes")) {
    GSheetInfo.uploadGsheetIntervalMinutes = server.arg("uploadGsheetIntervalMinutes").toInt();
  }
  
  // Validate Google Sheets configuration
  if (GSheetInfo.uploadGsheetIntervalMinutes < 1) {
    GSheetInfo.uploadGsheetIntervalMinutes = 1;
  }
  if (GSheetInfo.uploadGsheetIntervalMinutes > 1440) {
    GSheetInfo.uploadGsheetIntervalMinutes = 1440;
  }
  
  #if defined(_USESDCARD)
  // Save to SD card
  if (storeGsheetInfoSD()) {
    SerialPrint("Google Sheets configuration saved to SD card", true);
  } else {
    SerialPrint("Failed to save Google Sheets configuration to SD card", true);
  }
  #endif

  // Redirect back to the Google Sheets configuration page
  server.sendHeader("Location", "/GSHEET");
  #else
  server.sendHeader("Location", "/CONFIG");
  #endif
  
  server.send(302, "text/plain", "");
}

void handleGSHEET_UPLOAD_NOW() {
  registerHTTPMessage("GSHEETUp");
  #ifdef _USEGSHEET
  int8_t result = Gsheet_uploadData();
  String msg = "Triggered immediate upload. Result: " + String(result) + ", " + GsheetUploadErrorString();
  
  #else
  String msg = "GSHEET upload not enabled on this device";
  #endif
  server.send(200, "text/plain", msg);
}

void handleGSHEET_SHARE_ALL() {
  registerHTTPMessage("GSHEETShare");
  #ifdef _USEGSHEET
  file_grantPermissions();
  String msg = "Share all sheets triggered. ";
  SerialPrint(msg, true);
  #else
  String msg = "GSHEET upload not enabled on this device";
  #endif
  server.send(200, "text/plain", msg);
}

void handleGSHEET_DELETE_ALL() {
  registerHTTPMessage("GSHEETDelete");
  #ifdef _USEGSHEET
  file_deleteAllSheets();
  String msg = "Delete all sheets triggered.";
  SerialPrint(msg, true);
  #else
  String msg = "GSHEET upload not enabled on this device";
  #endif
  server.send(200, "text/plain", msg);
}

void handleREQUEST_BROADCAST() {
  bool result = broadcastServerPresence(true, 2);
  SerialPrint("Broadcast (ArborysMesh ESP+UDP): " + String(result ? "Success" : "Failed"), true);
  server.sendHeader("Location", "/STATUS");
  server.send(302, "text/plain", result ? "Success" : "Failed");
}

void handleREQUEST_BROADCAST_ESP() {
  bool result = broadcastServerPresence(true, 0);
  SerialPrint("Broadcast ArborysMesh: " + String(result ? "Success" : "Failed"), true);
  server.sendHeader("Location", "/STATUS");
  server.send(302, "text/plain", result ? "Success" : "Failed");
}

void handleREQUEST_BROADCAST_UDP() {
  bool result = broadcastServerPresence(true, 1);
  SerialPrint("Broadcast UDPLan: " + String(result ? "Success" : "Failed"), true);
  server.sendHeader("Location", "/STATUS");
  server.send(302, "text/plain", result ? "Success" : "Failed");
}

// Generate AP SSID based on MAC address: "SensorNet-" + last MAC in hex
String generateAPSSID() {
    char ssid[25];
    // Format: "SensorNet-" + all 6 bytes of MAC in hex (12 characters) in order 1,2,3,4,5,6
    snprintf(ssid, sizeof(ssid), "SensorNet-%02X%02X%02X%02X%02X%02X", 
             getPROCIDByte(Prefs.PROCID, 0), getPROCIDByte(Prefs.PROCID, 1), getPROCIDByte(Prefs.PROCID, 2),
             getPROCIDByte(Prefs.PROCID, 3), getPROCIDByte(Prefs.PROCID, 4), getPROCIDByte(Prefs.PROCID, 5));
    return String(ssid);
}

// Connect to Soft AP mode (combined AP-station mode)
void connectSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP) {
  if (WiFi.getMode() != WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_MODE_APSTA); // Combined AP and station mode
  }

  *wifiID = generateAPSSID();
  *apIP = IPAddress(192, 168, 4, 1);
  *wifiPWD = AP_STATION_PASSWORD;
  

  SerialPrint("Setting config for soft AP", true);
  // Configure AP with proper error handling
  if (!WiFi.softAPConfig(*apIP, *apIP, IPAddress(255, 255, 255, 0))) {
    SerialPrint("Failed to configure AP", true);
    return;
  } 
  SerialPrint("Config for soft AP set", true);
  
  // Channel 1 is the ESP-NOW home channel. A shared-radio STA scan was moving this
  // beacon mid-handshake, and phones reported that as a wrong password.
  const int apChannel = AP_WIFI_CHANNEL_MIN;
  if (!WiFi.softAP(wifiID->c_str(), wifiPWD->c_str(), apChannel, 0, 4, false,
      WIFI_AUTH_WPA2_PSK, WIFI_CIPHER_TYPE_CCMP)) {
    SerialPrint("Failed to start AP", true);
    return;
  }
  SerialPrint("AP starting on channel " + String(apChannel) + " with AP password", true);
  updateWifiChannel();
}

void handleCONFIG_POST() {
  registerHTTPMessage("ConfigIn");
  
  // Process form submissions and update editable fields
  // Timezone configuration is now handled through the timezone setup page

  #if defined(_USEWEATHER) && defined(_USETFT)

  if (server.hasArg("IntervalHourlyWeather")) {
    GRAPHICS.IntervalHourlyWeatherDisplay = server.arg("IntervalHourlyWeather").toInt();
  }


  #endif
  
  // Process device name
  if (server.hasArg("deviceName")) {
    String newDeviceName = server.arg("deviceName");
    if (newDeviceName.length() > 0) {
      if (newDeviceName.length() > 32) newDeviceName = newDeviceName.substring(0, 32);
      
      snprintf((char*)Prefs.DEVICENAME, sizeof(Prefs.DEVICENAME), "%s", newDeviceName.c_str());
      Prefs.isUpToDate = false; // Mark as needing to be saved

      //now update the DeviceStore with the new device name
      ArborysDevType* myDevice = Sensors.getDeviceByDevIndex(I.MY_DEVICE_INDEX);
      if (myDevice) {
        strncpy(myDevice->devName, newDeviceName.c_str(), sizeof(myDevice->devName) - 1);
        myDevice->devName[sizeof(myDevice->devName) - 1] = '\0';
        //store the device to SD card
        #ifdef _USESDCARD
        storeDevicesSensorsSD();
        #endif
      } else {
        SerialPrint("Failed to update device name", true);
        storeError("Failed to update device name", ERROR_DEVICE_NAME_INVALID, true);
        storeError("My device not found", ERROR_DEVICE_MDEVICE_NOTFOUND, true);
      }
    } else {
      SerialPrint("Device name is empty", true);
      storeError("Device name is empty", ERROR_DEVICE_NAME_INVALID, true);
    }

  }
  if (server.hasArg("dst_enabled")) {
    Prefs.DST = server.arg("dst_enabled").toInt();
    Prefs.isUpToDate = false;
  }
  if (server.hasArg("dst_start_date")) {
     String tmp = server.arg("dst_start_date");
     Prefs.DSTStartUnixTime = convertStrTime(tmp, false);     
    Prefs.isUpToDate = false;
  }
  if (server.hasArg("dst_end_date")) {
    String tmp = server.arg("dst_end_date");
    Prefs.DSTEndUnixTime = convertStrTime(tmp, false);     
    Prefs.isUpToDate = false;
  }
  if (server.hasArg("dst_offset")) {
    Prefs.DSTOffset = server.arg("dst_offset").toInt();
    Prefs.isUpToDate = false;
  }
  if (server.hasArg("utc_offset")) {
    Prefs.TimeZoneOffset = server.arg("utc_offset").toInt();
    Prefs.isUpToDate = false;
  }

  


#if defined(_USEWEATHER) && defined(_USETFT)

if (GRAPHICS.IntervalHourlyWeatherDisplay < 1) {
  GRAPHICS.IntervalHourlyWeatherDisplay = 1;
}

#endif

  // Save the updated configuration to persistent storage
  I.isUpToDate = false;
  
  // Redirect back to the configuration page
  server.sendHeader("Location", "/CONFIG");
  server.send(302, "text/plain", "Configuration updated. Redirecting...");
}

void handleCONFIG_DELETE() {
  registerHTTPMessage("ConfigDel");
  
  // Delete all config data
  int8_t ret =delete_all_core_data(true,true); 
   //just reboot, regardless of any failures
   
  Prefs.isUpToDate = true; //this prevents prefs from updating in controlled reboot
  controlledReboot("User Request",RESET_USER,true);
}

void handleCONFIG_OTA_SWITCH() {
  registerHTTPMessage("ConfigOtaSwitch");
  String failureDetail;
  int16_t result = force_switch_ota_slot(-1, &failureDetail);
  if (result == 1) {
    server.send(302, "text/plain", "OTA slot switched. Restarting in 3 seconds...");
    delay(3000);
    recordRebootIssue(RESET_OTA);
    esp_restart();
  } else if (result == 0) {
    server.send(200, "text/plain", "OTA slot already active. No restart needed. Redirecting to status page...");
  } else {
    String msg = "OTA slot could not be switched.";
    if (failureDetail.length() > 0) {
      msg += " " + failureDetail;
    }
    server.send(200, "text/plain", msg);
  }
}

#ifdef _USE_HEADER_INFO_ALERT
static void showSensorLanMsgBanner(const char* snsName) {
  char shortName[11] = "";
  if (snsName && snsName[0] != '\0') {
    strncpy(shortName, snsName, 10);
    shortName[10] = '\0';
  }
  char banner[24];
  snprintf(banner, sizeof(banner), "Msg [%s]", shortName[0] ? shortName : "sensor");
  HeaderInfoAlert(banner, TFT_YELLOW, TFT_BLACK, 60);
}
#endif

#if _IS_SERVER_HUB
static bool parseLimitFormValue(const String& raw, float& out) {
  String s = raw;
  s.trim();
  if (s.length() == 0 || s == "?") return false;
  char* end = nullptr;
  out = strtof(s.c_str(), &end);
  if (!end || end == s.c_str()) return false;
  while (*end == ' ') end++;
  if (*end != '\0') return false;
  if (isnan(out) || isinf(out)) return false;
  return true;
}

// Empty / "?" means "not provided" (leave unchanged). Invalid number fails.
static bool parseOptionalIntervalFormValue(const String& raw, uint32_t& out, bool& provided, uint32_t minVal = 1) {
  String s = raw;
  s.trim();
  provided = false;
  if (s.length() == 0 || s == "?") return true;
  char* end = nullptr;
  unsigned long v = strtoul(s.c_str(), &end, 10);
  if (!end || end == s.c_str()) return false;
  while (*end == ' ') end++;
  if (*end != '\0') return false;
  if (v < minVal || v > 65535) return false;
  out = (uint32_t)v;
  provided = true;
  return true;
}

static void redirectSensorLimitsResult(bool ok, const String& snsName, const char* err) {
  String loc = "/?limits=" + String(ok ? "success" : "failed")
      + "&limitsensor=" + urlEncode(snsName);
  if (!ok && err && err[0] != '\0') {
    loc += "&limiterr=" + urlEncode(String(err));
  }
  server.sendHeader("Location", loc);
  server.send(302, "text/plain", ok ? "Sensor limits updated." : "Sensor limit update failed.");
}

void handleSENSOR_OVERRIDE_UPDATE() {
  registerHTTPMessage("SnsOvrd");

  if (!server.hasArg("snsIndex")) {
    server.send(400, "text/plain", "Missing sensor index");
    return;
  }

  int16_t snsIndex = server.arg("snsIndex").toInt();
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
  if (!sensor || !sensor->IsSet) {
    server.send(400, "text/plain", "Invalid sensor index");
    return;
  }

  uint8_t overrideFlags = 0;
  for (int i = 0; i < 8; i++) {
    String flagName = "override_flag_bit" + String(i);
    if (server.hasArg(flagName)) {
      bitSet(overrideFlags, i);
    }
  }

  sensor->OverrideFlags = overrideFlags;
  I.isUpToDate = false;

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Sensor override flags updated. Redirecting...");
}

void handleSENSOR_LIMITS_UPDATE() {
  registerHTTPMessage("SnsLim");

  if (!server.hasArg("snsIndex")) {
    server.send(400, "text/plain", "Missing sensor index");
    return;
  }

  int16_t snsIndex = server.arg("snsIndex").toInt();
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
  if (!sensor || !sensor->IsSet) {
    server.send(400, "text/plain", "Invalid sensor index");
    return;
  }

  const String snsName = String(sensor->snsName);
  if (Sensors.isMySensor(snsIndex)) {
    redirectSensorLimitsResult(false, snsName, "local sensor");
    return;
  }
  float limitHigh = NAN;
  float limitLow = NAN;
  if (!server.hasArg("limitHigh") || !server.hasArg("limitLow") ||
      !parseLimitFormValue(server.arg("limitHigh"), limitHigh) ||
      !parseLimitFormValue(server.arg("limitLow"), limitLow)) {
    redirectSensorLimitsResult(false, snsName, "invalid limits");
    return;
  }

  uint32_t intervalPoll = 0;
  uint32_t intervalSend = 0;
  bool havePoll = false;
  bool haveSend = false;
  if ((server.hasArg("intervalPoll") &&
       !parseOptionalIntervalFormValue(server.arg("intervalPoll"), intervalPoll, havePoll)) ||
      (server.hasArg("intervalSend") &&
       !parseOptionalIntervalFormValue(server.arg("intervalSend"), intervalSend, haveSend, 0))) {
    redirectSensorLimitsResult(false, snsName, "invalid intervals");
    return;
  }

  ArborysDevType* device = Sensors.getDeviceBySnsIndex(snsIndex);
  if (!device || !device->IsSet) {
    redirectSensorLimitsResult(false, snsName, "device not found");
    return;
  }
  if (device->IP == IPAddress(0, 0, 0, 0)) {
    redirectSensorLimitsResult(false, snsName, "device IP unknown");
    return;
  }

  ArborysDevType* me = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  if (!me) {
    redirectSensorLimitsResult(false, snsName, "local device missing");
    return;
  }

#ifdef _USE_HEADER_INFO_ALERT
  showSensorLanMsgBanner(sensor->snsName);
#endif

  String json = "{\"msgType\":\"setLimits\",";
  json += JSONbuilder_device(me);
  json += ",\"toMAC\":\"";
  json += MACToString(device->MAC, '\0', true);
  json += "\",\"snsType\":";
  json += String(sensor->snsType);
  json += ",\"snsID\":";
  json += String(sensor->snsID);
  json += ",\"limitHigh\":";
  json += String(limitHigh, 4);
  json += ",\"limitLow\":";
  json += String(limitLow, 4);
  if (havePoll) {
    json += ",\"pollingInt\":";
    json += String(intervalPoll);
  }
  if (haveSend) {
    json += ",\"sendingInt\":";
    json += String(intervalSend);
  }
  json += "}";

  bool ok = false;
  if (isValidLMKKey()) {
    const int16_t code = sendHTTPSJSON(device->IP, json.c_str(), "setLimits", 10000);
    ok = (code >= 200 && code < 400);
  } else {
    String httpBody = json;
    JSONbuilder_encodeHTTP(httpBody);
    ok = (sendHTTPJSON(device->IP, httpBody.c_str(), "setLimits", 10000) == 200);
  }

  if (ok) {
    sensor->limitHigh = limitHigh;
    sensor->limitLow = limitLow;
    if (havePoll) sensor->PollingInt = intervalPoll;
    if (haveSend) {
      sensor->SendingInt = intervalSend;
      if (intervalSend != 0 && (device->SendingInt == 0 || intervalSend < device->SendingInt)) {
        device->SendingInt = intervalSend;
      }
    }
    I.isUpToDate = false;
    redirectSensorLimitsResult(true, snsName, nullptr);
    SerialPrint("Sensor limits/timing sent to " + snsName + " (" + device->IP.toString() + ")", true);
  } else {
    redirectSensorLimitsResult(false, snsName, "no acknowledgment");
    SerialPrint("Sensor limit update failed for " + snsName, true);
  }
}
#endif

#if _HAS_LOCAL_SENSORS
static String switchStateLabel(const ArborysSnsType* s) {
  if (!s) return "—";
  if (s->snsType == SNS_COUNTDOWN || s->snsType == SNS_COUNTDOWN_INV) {
    if (s->snsValue > 0.0) return "ON (" + String((int)floor(s->snsValue)) + "s)";
    return "OFF";
  }
  const uint8_t forceLeft = InterruptTriggers_webForceRemainingSec(s);
  if (forceLeft > 0) {
    return String(bitRead(s->Flags, 0) ? "ON" : "OFF") + " (web " + String(forceLeft) + "s)";
  }
  return bitRead(s->Flags, 0) ? "ON" : "OFF";
}

void handleSWITCHSTATE() {
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("SwitchState");
  serverTextStreamBegin(200, true);
  appendStandardPageNav();

  WEBHTML += "<h2>Controlled outputs (types 71–79)</h2>";
  WEBHTML += "<p>Set ON/OFF for up to 255 seconds. Automatic logic continues (countdown, motion, clock schedule). "
             "Clock-style outputs require a duration and return to schedule when it expires.</p>";
  WEBHTML += "<table style=\"width:100%; border-collapse:collapse; margin-bottom:20px;\">";
  WEBHTML += "<tr style=\"background:#f0f0f0;\">"
             "<th style=\"border:1px solid #ddd; padding:8px; text-align:left;\">Name</th>"
             "<th style=\"border:1px solid #ddd; padding:8px;\">Type</th>"
             "<th style=\"border:1px solid #ddd; padding:8px;\">State</th>"
             "<th style=\"border:1px solid #ddd; padding:8px;\">Action</th></tr>";

  uint8_t outCount = 0;
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.snsIndexToPointer(si);
    if (!s || !s->IsSet || s->deviceIndex != I.MY_DEVICE_INDEX) continue;
    if (!isSwitchStateOutputType(s->snsType)) continue;
    outCount++;

    WEBHTML += "<tr>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px;\">" + String(s->snsName) + "</td>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px; text-align:center;\">" + String(s->snsType) + "." + String(s->snsID) + "</td>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px; text-align:center;\">" + switchStateLabel(s) + "</td>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px;\">";
    WEBHTML += "<form method=\"POST\" action=\"/SWITCHSTATE\" style=\"display:inline-block; margin:2px;\">";
    WEBHTML += "<input type=\"hidden\" name=\"action\" value=\"set\">";
    WEBHTML += "<input type=\"hidden\" name=\"snsType\" value=\"" + String(s->snsType) + "\">";
    WEBHTML += "<input type=\"hidden\" name=\"snsID\" value=\"" + String(s->snsID) + "\">";
    WEBHTML += "<input type=\"hidden\" name=\"state\" value=\"1\">";
    WEBHTML += "<label>sec <input type=\"number\" name=\"seconds\" min=\"1\" max=\"255\" value=\"60\" style=\"width:64px;\"></label> ";
    WEBHTML += "<button type=\"submit\" style=\"padding:6px 12px; background:#4CAF50; color:white; border:none; border-radius:4px; cursor:pointer;\">ON</button>";
    WEBHTML += "</form> ";
    WEBHTML += "<form method=\"POST\" action=\"/SWITCHSTATE\" style=\"display:inline-block; margin:2px;\">";
    WEBHTML += "<input type=\"hidden\" name=\"action\" value=\"set\">";
    WEBHTML += "<input type=\"hidden\" name=\"snsType\" value=\"" + String(s->snsType) + "\">";
    WEBHTML += "<input type=\"hidden\" name=\"snsID\" value=\"" + String(s->snsID) + "\">";
    WEBHTML += "<input type=\"hidden\" name=\"state\" value=\"0\">";
    WEBHTML += "<label>sec <input type=\"number\" name=\"seconds\" min=\"0\" max=\"255\" value=\"0\" style=\"width:64px;\" title=\"Type 73/74: ignored (turns off now). Type 75: force off for N seconds.\"></label> ";
    WEBHTML += "<button type=\"submit\" style=\"padding:6px 12px; background:#f44336; color:white; border:none; border-radius:4px; cursor:pointer;\">OFF</button>";
    WEBHTML += "</form></td></tr>";
    serverTextFlush(false);
  }
  if (outCount == 0) {
    WEBHTML += "<tr><td colspan=\"4\" style=\"border:1px solid #ddd; padding:8px;\">No local output switches (types 71–79).</td></tr>";
  }
  WEBHTML += "</table>";
  serverTextFlush(true);

  WEBHTML += "<h2>Interrupt triggers (presence and button)</h2>";
  WEBHTML += "<p>Trigger a rising-edge action (same as a physical button / motion pulse). Does not hold state.</p>";
  WEBHTML += "<table style=\"width:100%; border-collapse:collapse;\">";
  WEBHTML += "<tr style=\"background:#f0f0f0;\">"
             "<th style=\"border:1px solid #ddd; padding:8px; text-align:left;\">Name</th>"
             "<th style=\"border:1px solid #ddd; padding:8px;\">Type</th>"
             "<th style=\"border:1px solid #ddd; padding:8px;\">Value</th>"
             "<th style=\"border:1px solid #ddd; padding:8px;\">Action</th></tr>";

  uint8_t irqCount = 0;
  for (int16_t si = 0; si < NUMSENSORS; si++) {
    ArborysSnsType* s = Sensors.snsIndexToPointer(si);
    if (!s || !s->IsSet || s->deviceIndex != I.MY_DEVICE_INDEX) continue;
    if (!isSwitchStateInterruptType(s->snsType)) continue;
    irqCount++;

    WEBHTML += "<tr>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px;\">" + String(s->snsName) + "</td>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px; text-align:center;\">" + String(s->snsType) + "." + String(s->snsID) + "</td>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px; text-align:center;\">" + String(s->snsValue, 1) + "</td>";
    WEBHTML += "<td style=\"border:1px solid #ddd; padding:8px;\">";
#if _USEINTERRUPT
    WEBHTML += "<form method=\"POST\" action=\"/SWITCHSTATE\" style=\"display:inline;\">";
    WEBHTML += "<input type=\"hidden\" name=\"action\" value=\"trigger\">";
    WEBHTML += "<input type=\"hidden\" name=\"snsType\" value=\"" + String(s->snsType) + "\">";
    WEBHTML += "<input type=\"hidden\" name=\"snsID\" value=\"" + String(s->snsID) + "\">";
    WEBHTML += "<button type=\"submit\" style=\"padding:6px 12px; background:#2196F3; color:white; border:none; border-radius:4px; cursor:pointer;\">Trigger</button>";
    WEBHTML += "</form>";
#else
    WEBHTML += "<em>interrupts not enabled</em>";
#endif
    WEBHTML += "</td></tr>";
    serverTextFlush(false);
  }
  if (irqCount == 0) {
    WEBHTML += "<tr><td colspan=\"4\" style=\"border:1px solid #ddd; padding:8px;\">No local interrupt sensors.</td></tr>";
  }
  WEBHTML += "</table>";
  serverTextClose(200, true);
}

void handleSWITCHSTATE_POST() {
  registerHTTPMessage("SwState");
  if (!server.hasArg("action") || !server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }

  const String action = server.arg("action");
  const uint8_t snsType = (uint8_t)server.arg("snsType").toInt();
  const uint8_t snsID = (uint8_t)server.arg("snsID").toInt();
  const int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
  if (!sensor || !sensor->IsSet || sensor->deviceIndex != I.MY_DEVICE_INDEX) {
    server.send(404, "text/plain", "Sensor not found");
    return;
  }

  bool ok = false;
  if (action == "set") {
    if (!isSwitchStateOutputType(snsType)) {
      server.send(400, "text/plain", "Not an output switch type");
      return;
    }
    const bool on = server.hasArg("state") && server.arg("state").toInt() != 0;
    int sec = server.hasArg("seconds") ? server.arg("seconds").toInt() : 0;
    if (sec < 0) sec = 0;
    if (sec > 255) sec = 255;
    // Type 75 OFF with 0 seconds: treat as brief force-off then auto (use 1s minimum pulse).
    if (!on && snsType == SNS_SWITCH && sec == 0) sec = 1;
    ok = InterruptTriggers_webSetOutput(sensor, on, (uint8_t)sec);
    if (!ok) {
      server.send(400, "text/plain", "Set rejected (check seconds 1–255 for ON / clock outputs)");
      return;
    }
  } else if (action == "trigger") {
    if (!isSwitchStateInterruptType(snsType)) {
      server.send(400, "text/plain", "Not an interrupt sensor type");
      return;
    }
#if _USEINTERRUPT
    ok = InterruptTriggers_simulateRisingEdge(sensor);
#else
    ok = false;
#endif
    if (!ok) {
      server.send(400, "text/plain", "Trigger failed");
      return;
    }
  } else {
    server.send(400, "text/plain", "Unknown action");
    return;
  }

  server.sendHeader("Location", "/SWITCHSTATE");
  server.send(302, "text/plain", "OK");
}
#endif

#if _HAS_LOCAL_SENSORS
void handleSENSOR_UPDATE_POST() {
  registerHTTPMessage("SnsUpd");
  
  // Get sensor identifiers from form
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing sensor identifiers");
    return;
  }
  
  uint8_t snsType = server.arg("snsType").toInt();
  uint8_t snsID = server.arg("snsID").toInt();

    // First, get the sensor object so we can update it
    int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
    ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);


  // Get the prefs index for this sensor
  int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(snsIndex);
  SerialPrint("Updating sensor: " + String(snsType) + "." + String(snsID) + " with Prefs index: " + String(prefsIndex),true);
  
  if (prefsIndex < 0 || prefsIndex >= _SENSORNUM) {
    server.send(400, "text/plain", "Invalid sensor or sensor not found");
    return;
  }
  
  
  // Update sensor name
  if (server.hasArg("sensorName") && sensor) {
    String newName = server.arg("sensorName");
    if (newName.length() > 0 && newName.length() < 30) {
      strncpy(sensor->snsName, newName.c_str(), 29);
      sensor->snsName[29] = '\0'; // Ensure null termination
      SerialPrint("Updated sensor name to: " + newName, true);
    }
  }
  
  // Update sensor limits
  if (server.hasArg("limitMax")) {
    Prefs.SNS_LIMIT_MAX[prefsIndex] = server.arg("limitMax").toDouble();
  }
  if (server.hasArg("limitMin")) {
    Prefs.SNS_LIMIT_MIN[prefsIndex] = server.arg("limitMin").toDouble();
  }
  
  // Update intervals
  if (server.hasArg("intervalPoll")) {
    Prefs.SNS_INTERVAL_POLL[prefsIndex] = server.arg("intervalPoll").toInt();
  }
  if (server.hasArg("intervalSend")) {
    Prefs.SNS_INTERVAL_SEND[prefsIndex] = server.arg("intervalSend").toInt();
  }
  
  // Update flags - reconstruct from individual bits (preserve bits 8+ e.g. auto-zero)
  uint16_t flags = Prefs.SNS_FLAGS[prefsIndex] & 0xFF00;
  for (int i = 0; i < 8; i++) {
    String flagName = "flag_bit" + String(i);
    if (server.hasArg(flagName)) {
      bitSet(flags, i);
    }
  }
  Prefs.SNS_FLAGS[prefsIndex] = flags;
  
  // Also update the sensor's flags/limits in the Sensors array
  if (sensor) {
    const uint8_t lastflag = sensor->Flags;
    if (IS_INTERRUPT_SENSOR_TYPE(sensor->snsType)) {
      if (normalizeHumanPresenceLimits(Prefs.SNS_LIMIT_MAX[prefsIndex], Prefs.SNS_LIMIT_MIN[prefsIndex])) {
        Prefs.isUpToDate = false;
      }
    }
    sensor->SendingInt = Prefs.SNS_INTERVAL_SEND[prefsIndex];
    sensor->PollingInt = Prefs.SNS_INTERVAL_POLL[prefsIndex];
    sensor->limitHigh = (float)Prefs.SNS_LIMIT_MAX[prefsIndex];
    sensor->limitLow = (float)Prefs.SNS_LIMIT_MIN[prefsIndex];
    sensor->Flags = flags;
    applyAlarmFlags(sensor, Prefs.SNS_LIMIT_MAX[prefsIndex], Prefs.SNS_LIMIT_MIN[prefsIndex], lastflag);
  }
  
  // Mark Prefs as needing to be saved
  Prefs.isUpToDate = false;
  
  // Redirect back to root page
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Sensor configuration updated. Redirecting...");
}

void handleSENSOR_READ_SEND_NOW() {
  registerHTTPMessage("ReadReq");
    
  // Get sensor identifiers from form
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing sensor identifiers");
    return;
  }
  
  uint8_t snsType = server.arg("snsType").toInt();
  uint8_t snsID = server.arg("snsID").toInt();
  
  // Find the sensor
  int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  if (snsIndex < 0) {
    server.send(400, "text/plain", "Sensor not found");
    return;
  }
  
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
  if (!sensor) {
    server.send(400, "text/plain", "Invalid sensor");
    return;
  }
  
  // Force read the sensor data
  int8_t readResult = ReadData(sensor, true); // forceRead = true

  SendData(snsIndex,true,-1,true); //send the data to the servers using broadcast udp

  String resultMsg = "";
  if (readResult == 1) {
    resultMsg = "Sensor read successfully. Value: " + String(sensor->snsValue);
    SerialPrint("Forced sensor read: " + String(sensor->snsName) + " = " + String(sensor->snsValue), true);
  } else if (readResult == -1) {
    resultMsg = "Error: Not my sensor";
  } else if (readResult == -2) {
    resultMsg = "Error: Not registered";
  } else if (readResult == -10) {
    resultMsg = "Error: Reading invalid";
  } else {
    resultMsg = "Sensor read status: " + String(readResult);
  }
  
  
  SerialPrint(resultMsg, true);
  
  // Redirect back to root page
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", resultMsg);
}

#if _IS_SERVER_HUB
static void redirectAggregateSetup(uint8_t snsType, uint8_t snsID, const char* err) {
  String loc = "/SENSOR_SETUP?snsType=" + String(snsType) + "&snsID=" + String(snsID);
  if (err && err[0]) loc += "&linkErr=" + String(err);
  server.sendHeader("Location", loc);
  server.send(302, "text/plain", "Redirecting");
}

static void requestRegisteredSensors() {
  for (int16_t i = 0; i < NUMDEVICES; i++) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!d || !d->IsSet || d->MAC == 0 || d->MAC == (uint64_t)ESP.getEfuseMac()) continue;
    sendMSG_DataRequest(d, -1, true);
  }
}

static bool aggregateProfileMatch(uint8_t snsType, int16_t candidateIndex, uint8_t candidateType, const char* category) {
  if (candidateType == SNS_AGGREGATE) {
    if (!category || !category[0]) return false;
    const int16_t pi = SensorHistory.getSensorHistoryIndex(candidateIndex);
    if (pi < 0) return false;
    char cat[24];
    Actuators_aggregateRule(pi, nullptr, 0, cat, sizeof(cat), nullptr);
    return cat[0] && strcasecmp(cat, category) == 0;
  }
  if (snsType == SNS_BRYANT_OAT) return Sensors.isSensorOfType(candidateType, "temperature");
  if (category && category[0]) return Sensors.isSensorOfType(candidateType, category);
  return true;
}

static void appendAggregateCheckbox(uint64_t mac, uint8_t st, uint8_t sid, const String& name, bool checked) {
  const String macStr = MACToString(mac, '\0', true);
  WEBHTML += "<label style=\"display:block; margin: 4px 0;\"><input type=\"checkbox\" name=\"link\" value=\"";
  WEBHTML += macStr + "," + String(st) + "," + String(sid) + "\"";
  if (checked) WEBHTML += " checked";
  WEBHTML += "> " + name + " (" + AggLinks_groupName(st) + " ";
  WEBHTML += String(st) + "." + String(sid) + ")</label>";
}

static String aggregateRegisterNote() {
  String html = "<p>Sensor list requested. On the sensor's Setup page, check the sensors to include. Refresh that page if the list is still empty.</p><ul>";
  bool any = false;
  const int16_t me = Sensors.findMyDeviceIndex();
  for (int16_t i = 0; i < NUMSENSORS; i++) {
    ArborysSnsType* s = Sensors.getSensorBySnsIndex(i);
    if (!s || !s->IsSet || s->deviceIndex != me) continue;
    if (s->snsType != SNS_AGGREGATE && s->snsType != SNS_BRYANT_OAT) continue;
    any = true;
    html += "<li><a href=\"/SENSOR_SETUP?snsType=" + String(s->snsType) + "&snsID=" + String(s->snsID) + "\">";
    html += String(s->snsName);
    html += "</a></li>";
  }
  html += "</ul>";
  if (!any) return "<p>Device registered.</p>";
  return html;
}

static void appendAggregateLinkForm(int16_t prefsIndex, uint8_t snsType, uint8_t snsID) {
  char op[8];
  char category[24];
  uint8_t place = 0;
  Actuators_aggregateRule(prefsIndex, op, sizeof(op), category, sizeof(category), &place);
  if (snsType == SNS_BRYANT_OAT) {
    WEBHTML += "<h3>Outside temperature fallback</h3>";
    WEBHTML += "<p>While a Bryant outdoor frame is fresh, this sensor uses that outside temperature. Otherwise it uses the newest fresh temperature on a non-server device: a sensor checked here, or a temperature flagged outside on a registered peripheral. If neither is fresh, it uses the newest outside temperature on a registered weather server, preferring that server's outdoor temperature aggregate. A sample older than that sensor's send interval plus 25% is left out.</p>";
  } else {
    WEBHTML += "<h3>Linked sensors</h3>";
    WEBHTML += "<p>Rule: ";
    WEBHTML += op;
    if (category[0]) {
      WEBHTML += " of ";
      WEBHTML += category;
    }
    if (place == 1) WEBHTML += ", indoor";
    else if (place == 2) WEBHTML += ", outdoor";
    WEBHTML += ". Check the sensors to include. A checked sensor is used whether or not it is flagged outside. ";
    WEBHTML += "A sample older than that sensor's send interval plus 25% is left out. ";
#if !_HUB_REGISTERED_ONLY
    if (category[0] && place != 1) {
      WEBHTML += "Until you save a selection, this uses ";
      WEBHTML += category;
      WEBHTML += (place == 2) ? " readings that are flagged outside. " : " readings that are not flagged outside. ";
      WEBHTML += "After you save, only checked sensors count. Save with none checked and the value stays NAN. ";
    }
#endif
    WEBHTML += "It is not sent on its interval unless monitored. A direct request still returns it. ";
    WEBHTML += "If it is critical, a limit cross or an expiry change, in either direction, is sent even when it is not monitored. ";
    WEBHTML += "An average of one kind of sensor leaves out a NaN, expired, or out-of-range member and does not alarm for that. It alarms only when the average is outside its limits.</p>";
  }
  if (server.hasArg("linkErr")) {
    const String e = server.arg("linkErr");
    WEBHTML += "<p style=\"color:#a33;\">";
    if (e == "mismatch") WEBHTML += "Those sensors are not the same kind. Temperature can be combined with temperature. Pressure cannot be combined with temperature.";
    else if (e == "rule") WEBHTML += "A checked sensor does not match this rule's sensor group.";
    else if (e == "place") WEBHTML += "A checked sensor does not match this rule's indoor or outdoor filter.";
    else if (e == "full") WEBHTML += "The link table is full (10 devices, 40 sensors).";
    else WEBHTML += "That sensor could not be saved.";
    WEBHTML += "</p>";
  }
  WEBHTML += "<form method=\"POST\" action=\"/AGG_PULL\" style=\"margin: 12px 0;\">";
  WEBHTML += "<input type=\"hidden\" name=\"snsType\" value=\"" + String(snsType) + "\">";
  WEBHTML += "<input type=\"hidden\" name=\"snsID\" value=\"" + String(snsID) + "\">";
  WEBHTML += "<button type=\"submit\" style=\"padding: 8px 16px; background-color: #009688; color: white; border: none; border-radius: 4px; cursor: pointer;\">Request readings</button>";
  WEBHTML += "</form>";

  uint64_t forceMac = 0;
  uint8_t forceType = 0;
  uint8_t forceId = 0;
  bool haveForce = false;
  if (server.hasArg("forceMac")) {
    String hex;
    const String raw = server.arg("forceMac");
    for (unsigned i = 0; i < raw.length(); i++) {
      const char c = raw.charAt(i);
      if (isxdigit((unsigned char)c)) hex += c;
    }
    uint64_t mac = 0;
    if (hex.length() > 0 && stringToUInt64(hex, &mac, true) && mac != 0) {
      forceType = (uint8_t)server.arg("forceType").toInt();
      forceId = (uint8_t)server.arg("forceId").toInt();
      const bool self = mac == (uint64_t)ESP.getEfuseMac() && forceType == snsType && forceId == snsID;
      if (!self && forceType != 0) {
        forceMac = mac;
        haveForce = true;
      }
    }
  }

  WEBHTML += "<form method=\"POST\" action=\"/AGG_LINKS\">";
  WEBHTML += "<input type=\"hidden\" name=\"snsType\" value=\"" + String(snsType) + "\">";
  WEBHTML += "<input type=\"hidden\" name=\"snsID\" value=\"" + String(snsID) + "\">";
  bool any = false;
  bool forceShown = false;
  const int16_t me = Sensors.findMyDeviceIndex();
  for (int16_t di = 0; di < NUMDEVICES; di++) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d || !d->IsSet || d->MAC == 0) continue;
    bool header = false;
    for (int16_t si = 0; si < NUMSENSORS; si++) {
      ArborysSnsType* s = Sensors.getSensorBySnsIndex(si);
      if (!s || !s->IsSet || s->deviceIndex != di) continue;
      if (di == me && s->snsType == snsType && s->snsID == snsID) continue;
      const bool picked = AggLinks_isPicked((uint8_t)prefsIndex, d->MAC, s->snsType, s->snsID);
      const bool profile = aggregateProfileMatch(snsType, si, s->snsType, category);
      const bool forced = haveForce && d->MAC == forceMac && s->snsType == forceType && s->snsID == forceId;
      if (!profile && !picked && !forced) continue;
      if (!header) {
        WEBHTML += "<h4 style=\"margin: 14px 0 6px;\">" + String(d->devName) + " (" + d->IP.toString() + ")</h4>";
        header = true;
      }
      any = true;
      if (forced) forceShown = true;
      appendAggregateCheckbox(d->MAC, s->snsType, s->snsID, String(s->snsName), picked || forced);
    }
  }

  AggPick saved[AGG_MAX_LINKS];
  const uint8_t savedN = AggLinks_linksForPrefs((uint8_t)prefsIndex, saved, AGG_MAX_LINKS);
  bool extraHeader = false;
  for (uint8_t i = 0; i < savedN; i++) {
    if (saved[i].mac == 0) continue;
    if (Sensors.findSensor(saved[i].mac, saved[i].snsType, saved[i].snsID) >= 0) continue;
    if (!extraHeader) {
      WEBHTML += "<h4 style=\"margin: 14px 0 6px;\">Saved, not reported yet</h4>";
      extraHeader = true;
    }
    any = true;
    if (haveForce && saved[i].mac == forceMac && saved[i].snsType == forceType && saved[i].snsID == forceId) forceShown = true;
    appendAggregateCheckbox(saved[i].mac, saved[i].snsType, saved[i].snsID, String("Not reported yet"), true);
  }
  if (haveForce && !forceShown) {
    WEBHTML += "<h4 style=\"margin: 14px 0 6px;\">Added by hand</h4>";
    any = true;
    appendAggregateCheckbox(forceMac, forceType, forceId, String("Not reported yet"), true);
  }
  if (!any) {
    WEBHTML += "<p>No known ";
    WEBHTML += (category[0] ? category : "matching");
    WEBHTML += " sensors yet. Request readings, or add one below.</p>";
  }
  WEBHTML += "<button type=\"submit\" style=\"margin-top: 10px; padding: 8px 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Save links</button>";
  WEBHTML += "</form>";
  WEBHTML += "<h3>Add a sensor</h3>";
  WEBHTML += "<p>Use this for a sensor that is not in the list, including a type this average would not list on its own. MAC is the device address with no separators.</p>";
  WEBHTML += "<form method=\"GET\" action=\"/SENSOR_SETUP\" style=\"margin: 12px 0;\">";
  WEBHTML += "<input type=\"hidden\" name=\"snsType\" value=\"" + String(snsType) + "\">";
  WEBHTML += "<input type=\"hidden\" name=\"snsID\" value=\"" + String(snsID) + "\">";
  WEBHTML += "<label style=\"display:block; margin: 6px 0;\">MAC <input name=\"forceMac\" required style=\"width: 160px; padding: 4px;\"></label>";
  WEBHTML += "<label style=\"display:block; margin: 6px 0;\">Type <input type=\"number\" min=\"1\" max=\"255\" name=\"forceType\" required style=\"width: 80px; padding: 4px;\"></label>";
  WEBHTML += "<label style=\"display:block; margin: 6px 0;\">ID <input type=\"number\" min=\"0\" max=\"255\" name=\"forceId\" required style=\"width: 80px; padding: 4px;\"></label>";
  WEBHTML += "<button type=\"submit\" style=\"padding: 8px 16px; background-color: #607D8B; color: white; border: none; border-radius: 4px; cursor: pointer;\">Add to list</button>";
  WEBHTML += "</form>";
}
#endif

void handleSensorSetup() {
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing snsType or snsID");
    return;
  }
  uint8_t snsType = (uint8_t)server.arg("snsType").toInt();
  uint8_t snsID = (uint8_t)server.arg("snsID").toInt();
  int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  if (snsIndex < 0) {
    server.send(400, "text/plain", "Sensor not found");
    return;
  }
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
  if (!sensor) {
    server.send(400, "text/plain", "Invalid sensor");
    return;
  }

  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Sensor Setup: " + String(sensor->snsName));
  serverTextStreamBegin(200, true);
  WEBHTML = WEBHTML + "<p><a href=\"/\">Back to Main</a></p>";
  serverTextFlush(true);

  // Calibration is available for every sensor. The values live in Prefs (the source of truth);
  // if no live reading is available for this sensor it simply shows NaN and the user can still
  // type the min/max manually.
  int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(snsIndex);
  if (prefsIndex < 0) {
    WEBHTML = WEBHTML + "<p>Calibration unavailable: this sensor has no Prefs storage slot.</p>";
  } else {
    bool isSoil = (snsType == 33 || snsType == 3 || snsType == 34 || snsType == 35);
    bool usesScaling = sensorUsesScaling(snsType);
    if (isSoil) {
      WEBHTML = WEBHTML + "<p>Use the live measurement below to take measurements in bone dry soil and fully saturated soil. Enter both of those values below. Alternatively (though less accurate), use air (dry) and water (wet) for measurements. If you enter the same value for both, the calibration will be ignored.</p>";
    } else {
      WEBHTML = WEBHTML + "<p>Enter the calibration min and max for this sensor. If a live reading is unavailable the value will show NaN; you can still enter the limits manually. If you enter the same value for both, the calibration will be ignored.</p>";
    }

    //live readout (updates every second; shows NaN when no live value is available)
    WEBHTML = WEBHTML + "<div style=\"margin-bottom: 10px;\"><label style=\"display: inline-block; width: 80px;\">LIVE VALUE:</label>";
    WEBHTML = WEBHTML + "<div id=\"sensor_value\" style=\"font-size: 24px; font-weight: bold;\">Taking measurements...</div>";
    WEBHTML = WEBHTML + "<script>";
    WEBHTML = WEBHTML + "var currentLiveValue = NaN;"; //most recent valid live reading
    WEBHTML = WEBHTML + "function pollSensor() {";
    WEBHTML = WEBHTML + "  fetch('/api/SNS_READ_NOW?snsType=" + String(snsType) + "&snsID=" + String(snsID) + "')";
    WEBHTML = WEBHTML + "    .then(response => response.json())";
    WEBHTML = WEBHTML + "    .then(data => {";
    WEBHTML = WEBHTML + "      var v = data.success ? Number(data.value) : NaN;";
    WEBHTML = WEBHTML + "      currentLiveValue = v;";
    WEBHTML = WEBHTML + "      document.getElementById('sensor_value').innerHTML = 'Value: ' + (isNaN(v) ? 'NaN' : v.toPrecision(3));";
    WEBHTML = WEBHTML + "    })";
    WEBHTML = WEBHTML + "    .catch(function() { currentLiveValue = NaN; document.getElementById('sensor_value').innerHTML = 'Value: NaN'; })";
    WEBHTML = WEBHTML + "    .finally(function() { setTimeout(pollSensor, 500); });"; //schedule next read 500ms after this one finishes
    WEBHTML = WEBHTML + "}";
    WEBHTML = WEBHTML + "function useLive(fieldId) {";
    WEBHTML = WEBHTML + "  if (isNaN(currentLiveValue)) { alert('No live value available yet.'); return; }";
    WEBHTML = WEBHTML + "  document.getElementById(fieldId).value = currentLiveValue.toPrecision(4);";
    WEBHTML = WEBHTML + "}";
    WEBHTML = WEBHTML + "pollSensor();";
    WEBHTML = WEBHTML + "</script>";

    String calibMinStr = isnan(Prefs.SNS_CALIB_MIN[prefsIndex]) ? "" : String(Prefs.SNS_CALIB_MIN[prefsIndex], 3);
    String calibMaxStr = isnan(Prefs.SNS_CALIB_MAX[prefsIndex]) ? "" : String(Prefs.SNS_CALIB_MAX[prefsIndex], 3);
    String minLabel = isSoil ? "Fully wet soil/mud:" : "Calibration min:";
    String maxLabel = isSoil ? "Fully dry soil/sand:" : "Calibration max:";

    WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SNS_CALIBRATION\" style=\"max-width: 400px;\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsType\" value=\"" + String(snsType) + "\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"snsID\" value=\"" + String(snsID) + "\">";
    WEBHTML = WEBHTML + "<div style=\"margin-bottom: 10px;\"><label style=\"display: inline-block; width: 130px;\">" + minLabel + "</label>";
    WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" id=\"minval\" name=\"minval\" required style=\"width: 120px; padding: 4px;\" value=\"" + calibMinStr + "\">";
    WEBHTML = WEBHTML + "<button type=\"button\" onclick=\"useLive('minval')\" style=\"margin-left: 6px; padding: 4px 10px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">Use live</button></div>";
    WEBHTML = WEBHTML + "<div style=\"margin-bottom: 10px;\"><label style=\"display: inline-block; width: 130px;\">" + maxLabel + "</label>";
    WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" id=\"maxval\" name=\"maxval\" required style=\"width: 120px; padding: 4px;\" value=\"" + calibMaxStr + "\">";
    WEBHTML = WEBHTML + "<button type=\"button\" onclick=\"useLive('maxval')\" style=\"margin-left: 6px; padding: 4px 10px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">Use live</button></div>";
    if (usesScaling) {
      bool autoZero = bitRead(Prefs.SNS_FLAGS[prefsIndex], SNS_FLAG_BIT_AUTOZERO);
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 10px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 8px;\">";
      WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"autozero\" value=\"1\"";
      if (autoZero) WEBHTML = WEBHTML + " checked";
      WEBHTML = WEBHTML + ">";
      WEBHTML = WEBHTML + "<span>Auto-zero (adjust calibration min so scaled readings never go below 0)</span>";
      WEBHTML = WEBHTML + "</label></div>";
    }
    WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 8px 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Apply Calibration</button>";
    WEBHTML = WEBHTML + "</form>";
  }

#if _IS_SERVER_HUB
  if ((snsType == SNS_AGGREGATE || snsType == SNS_BRYANT_OAT) && prefsIndex >= 0 && prefsIndex <= 255) {
    appendAggregateLinkForm(prefsIndex, snsType, snsID);
  }
#endif

  serverTextClose(200, true);
}

#if _IS_SERVER_HUB
void handleAGG_LINKS() {
  registerHTTPMessage("AggLinks");
  if (!server.hasArg("snsType") || !server.hasArg("snsID")) {
    server.send(400, "text/plain", "Missing snsType or snsID");
    return;
  }
  const uint8_t snsType = (uint8_t)server.arg("snsType").toInt();
  const uint8_t snsID = (uint8_t)server.arg("snsID").toInt();
  if (snsType != SNS_AGGREGATE && snsType != SNS_BRYANT_OAT) {
    server.send(400, "text/plain", "Not an aggregate sensor");
    return;
  }
  const int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  const int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(snsIndex);
  if (prefsIndex < 0 || prefsIndex > 255) {
    server.send(400, "text/plain", "Sensor has no prefs slot");
    return;
  }
  AggPick picks[AGG_MAX_LINKS];
  uint8_t n = 0;
  for (int i = 0; i < server.args(); i++) {
    if (server.argName(i) != "link") continue;
    if (n >= AGG_MAX_LINKS) {
      redirectAggregateSetup(snsType, snsID, "full");
      return;
    }
    const String v = server.arg(i);
    const int c1 = v.indexOf(',');
    const int c2 = (c1 >= 0) ? v.indexOf(',', c1 + 1) : -1;
    if (c1 < 1 || c2 < 0) continue;
    uint64_t mac = 0;
    if (!stringToUInt64(v.substring(0, c1), &mac, true) || mac == 0) continue;
    const uint8_t st = (uint8_t)v.substring(c1 + 1, c2).toInt();
    const uint8_t sid = (uint8_t)v.substring(c2 + 1).toInt();
    if (st == 0) continue;
    picks[n].mac = mac;
    picks[n].snsType = st;
    picks[n].snsID = sid;
    n++;
  }
  String err;
  if (!AggLinks_setPicks((uint8_t)prefsIndex, picks, n, err)) {
    redirectAggregateSetup(snsType, snsID, err.c_str());
    return;
  }
  redirectAggregateSetup(snsType, snsID, "");
}

void handleAGG_PULL() {
  registerHTTPMessage("AggPull");
  const uint8_t snsType = server.hasArg("snsType") ? (uint8_t)server.arg("snsType").toInt() : SNS_AGGREGATE;
  const uint8_t snsID = server.hasArg("snsID") ? (uint8_t)server.arg("snsID").toInt() : 0;
  requestRegisteredSensors();
  redirectAggregateSetup(snsType, snsID, "");
}
#endif

void handleSNS_CALIBRATION() {
  if (!server.hasArg("snsType") || !server.hasArg("snsID") || !server.hasArg("minval") || !server.hasArg("maxval")) {
    server.send(400, "text/plain", "Missing snsType, snsID, minval, or maxval");
    return;
  }
  uint8_t snsType = (uint8_t)server.arg("snsType").toInt();
  uint8_t snsID = (uint8_t)server.arg("snsID").toInt();
  double minval = server.arg("minval").toDouble();
  double maxval = server.arg("maxval").toDouble();
  int16_t snsIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
  ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);

  if (!sensor) {
    server.send(400, "text/plain", "Sensor not found");
    return;
  }
  int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(snsIndex);
  if (prefsIndex < 0) {
    server.send(400, "text/plain", "Sensor has no Prefs slot");
    return;
  }

  //calibration is stored only in Prefs (the single source of truth)
  if (Prefs.SNS_CALIB_MIN[prefsIndex] != minval) {
    Prefs.SNS_CALIB_MIN[prefsIndex] = minval;
    Prefs.isUpToDate = false;
  }
  if (Prefs.SNS_CALIB_MAX[prefsIndex] != maxval) {
    Prefs.SNS_CALIB_MAX[prefsIndex] = maxval;
    Prefs.isUpToDate = false;
  }
  if (sensorUsesScaling(snsType)) {
    bool autoZero = server.hasArg("autozero");
    if ((bool)bitRead(Prefs.SNS_FLAGS[prefsIndex], SNS_FLAG_BIT_AUTOZERO) != autoZero) {
      bitWrite(Prefs.SNS_FLAGS[prefsIndex], SNS_FLAG_BIT_AUTOZERO, autoZero ? 1 : 0);
      Prefs.isUpToDate = false;
    }
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Calibration applied. Redirecting...");
}
#endif



// Helper to get external IP
String getPublicIP(uint16_t timeoutMs) {
  HTTPMessage M;
  M.allowInsecure = true;
  M.usePSRAM = false;
  M.timeout = timeoutMs;
  M.setUrl("http://api.ipify.org");
  M.setMethod("GET");

  if (SendHTTPMessage(M)) {
    return String(M.payload.get());
  }
  return "";
}

static bool readIpLatLon(JsonDocument& doc, const char* latKey, const char* lonKey, double& lat, double& lon) {
  if (doc["success"].is<bool>() && !doc["success"].as<bool>()) return false;
  if (doc["status"].is<const char*>() && strcmp(doc["status"].as<const char*>(), "success") != 0) return false;
  if (doc[latKey].isNull() || doc[lonKey].isNull()) return false;
  lat = doc[latKey].as<double>();
  lon = doc[lonKey].as<double>();
  if (!isfinite(lat) || !isfinite(lon)) return false;
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
  if (lat == 0.0 && lon == 0.0) return false;
  return true;
}

static bool fetchIpLatLon(const char* url, const char* latKey, const char* lonKey, bool https, uint16_t timeoutMs, double& lat, double& lon) {
  JsonDocument doc;
  HTTPMessage M;
  M.usePSRAM = false;
  M.timeout = timeoutMs;
  M.setUrl(url);
  M.setMethod("GET");
  M.setContentType("application/json");
  M.responseDoc = &doc;
  if (https) M.setCacert("bundle");
  else M.allowInsecure = true;
  if (!SendHTTPMessage(M)) return false;
  return readIpLatLon(doc, latKey, lonKey, lat, lon);
}

bool lookupCoordinatesFromPublicIp(uint16_t timeoutMs) {
  if (!wifiReadyForNetwork()) return false;
  if (Prefs.LATITUDE != 0.0 || Prefs.LONGITUDE != 0.0) return true;

  double lat = 0;
  double lon = 0;
  const bool found =
      fetchIpLatLon("https://ipwho.is/", "latitude", "longitude", true, timeoutMs, lat, lon) ||
      fetchIpLatLon("http://ip-api.com/json/?fields=status,lat,lon", "lat", "lon", false, timeoutMs, lat, lon);
  if (!found) {
    SerialPrint("IP location lookup failed", true);
    return false;
  }

  Prefs.LATITUDE = lat;
  Prefs.LONGITUDE = lon;
  Prefs.isUpToDate = false;
  SerialPrint("IP location: " + String(lat, 6) + ", " + String(lon, 6), true);
  return true;
}



void handleWeather() {
  registerHTTPMessage("Weather");
  WEBHTML = "";
  serverTextHeader("Weather Data");
  serverTextStreamBegin(200, true);
  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush(true);

  #ifdef _USEWEATHER
  
  #ifdef _USEDETAILEDWEATHERWEBHTML
  // Display current weather data
  if (Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) {
    WEBHTML = WEBHTML + "<h3>Current Weather Information</h3>";
  } else {
    WEBHTML = WEBHTML + "<p>Location not set, Weather data is not available.</p>";
    WEBHTML = WEBHTML + "<p>Please set your location below (enter your address or lat/lon).</p>";
  }
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Field</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Value</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  //last update time
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Last Update Time</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateT) ? dateifyLocal(WeatherData.lastUpdateT) : "???") + "</td></tr>";

  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Last Failure Time</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateError) ? dateifyLocal(WeatherData.lastUpdateError) : "???") + "</td></tr>";

  {
    static const char* kCompNames[] = {"Grid", "Hourly", "GridFcst", "Daily", "Alerts", "Sun", "Pressure"};
    for (uint8_t ci = 0; ci < WC_COUNT; ci++) {
      const WeatherComponentStatus& st = WeatherData.componentStatus[ci];
      const bool fresh = WeatherData.isComponentDataFresh((WeatherComponent)ci);
      String val = st.lastAttemptT ? String(dateifyLocal(st.lastAttemptT)) : String("never");
      val += st.lastSucceeded ? " OK" : " FAIL";
      val += fresh ? " fresh" : " stale";
      if (!fresh) val += " (retry 3m)";
      WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>" + String(kCompNames[ci]) + " status</strong></td>";
      WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + val + "</td></tr>";
    }
  }

  // Basic weather data
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Latitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Prefs.LATITUDE, 6) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Longitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Prefs.LONGITUDE, 6) + "</td></tr>";
    
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Sunrise</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.sunrise) ? dateifyLocal(WeatherData.sunrise) : "???") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Sunset</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.sunset) ? dateifyLocal(WeatherData.sunset) : "???") + "</td></tr>";

  {
    const int16_t pressureHpa = WeatherData.getPressure();
    String pressureVal = "???";
    if (pressureHpa != WEATHER_INVALID_PRESSURE) {
      pressureVal = String(pressureHpa) + " hPa";
      const uint32_t observedAt = WeatherData.getPressureObservedAt();
      if (observedAt) pressureVal += " (" + String(dateifyLocal(observedAt)) + ")";
    }
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Pressure</strong></td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + pressureVal + "</td></tr>";
  }
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Rain Flag</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + (WeatherData.flag_rain ? "Yes" : "No") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Snow Flag</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + (WeatherData.flag_snow ? "Yes" : "No") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Ice Flag</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + (WeatherData.flag_ice ? "Yes" : "No") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Grid X</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.getGridX()) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Grid Y</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.getGridY()) + "</td></tr>";
  
  // Current weather icon
  int16_t currentWeatherID = WeatherData.getWeatherID(0);
  if (currentWeatherID < 0) currentWeatherID = 999;
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Current Weather</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + WeatherData.nameWeatherIcon(currentWeatherID) + "</td></tr>";

  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Number of events</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(WeatherData.NumWeatherEvents) + "</td></tr>";

  WEBHTML = WEBHTML + "</table>";
  
  // Daily forecast for next 3 days
  WEBHTML = WEBHTML + "<h3>3-Day Forecast</h3>";
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Day</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Max Temp</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Min Temp</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Weather</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  for (int day = 0; day < 3; day++) {
    int8_t dailyT[2];
    WeatherData.getDailyTemp(day, dailyT);
    int16_t weatherID = WeatherData.getDailyWeatherID(day);
    
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">Day " + String(day + 1) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[0]) + "F</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(dailyT[1]) + "F</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + WeatherData.nameWeatherIcon(weatherID) + "</td></tr>";
  }
  
  WEBHTML = WEBHTML + "</table>";
  #endif

  // Configuration form for lat/lon
  WEBHTML = WEBHTML + "<h3>Update Location</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WEATHER\">";
  WEBHTML = WEBHTML + "<p><label for=\"lat\">Latitude:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"lat\" name=\"lat\" step=\"0.000001\" min=\"-90\" max=\"90\" value=\"" + String(Prefs.LATITUDE, 14) + "\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"lon\">Longitude:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"number\" id=\"lon\" name=\"lon\" step=\"0.000001\" min=\"-180\" max=\"180\" value=\"" + String(Prefs.LONGITUDE, 14) + "\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Update Location\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Address lookup form (submits directly to server)
  WEBHTML = WEBHTML + "<h3>Lookup Coordinates from Address</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WeatherAddress\">";
  WEBHTML = WEBHTML + "<p><label for=\"street\">Street Address:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"street\" name=\"street\" placeholder=\"123 Main St\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"city\">City:</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"city\" name=\"city\" placeholder=\"Boston\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"state\">State (2-letter code):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"state\" name=\"state\" pattern=\"[A-Za-z]{2}\" maxlength=\"2\" placeholder=\"MA\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<p><label for=\"zipcode\">ZIP Code (5 digits):</label><br>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"zipcode\" name=\"zipcode\" pattern=\"[0-9]{5}\" maxlength=\"5\" placeholder=\"12345\" style=\"width: 300px; padding: 8px; margin: 5px 0;\"></p>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Lookup Coordinates\" style=\"padding: 10px 20px; background-color: #FF9800; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Refresh weather button
  WEBHTML = WEBHTML + "<h3>Weather Actions</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/WeatherRefresh\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Refresh Weather Now\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
#else
  WEBHTML = WEBHTML + "Weather not enabled on this device.";
  #endif
  
  WEBHTML = WEBHTML + "</body>";
  WEBHTML = WEBHTML + "</html>";

    
  serverTextClose(200, true);
}

void handleWeather_POST() {
  registerHTTPMessage("WthrLoc");
  #ifdef _USEWEATHER
  // Handle location update
  if (server.hasArg("lat") && server.hasArg("lon")) {
    double newLat = server.arg("lat").toDouble();
    double newLon = server.arg("lon").toDouble();
    
    // Validate coordinates
    if (newLat >= -90.0 && newLat <= 90.0 && newLon >= -180.0 && newLon <= 180.0) {
      // Update Prefs only (WeatherData now uses Prefs.LATITUDE/LONGITUDE)
      Prefs.LATITUDE = newLat;
      Prefs.LONGITUDE = newLon;
      Prefs.isUpToDate = false;
      
      // Save to NVS
      BootSecure bootSecure;
      int8_t ret = bootSecure.setPrefs();
      if (ret < 0) {
        SerialPrint("handleWeather_POST: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
      } else {
        SerialPrint("Coordinates updated and saved: " + String(newLat, 6) + ", " + String(newLon, 6), true);
      }
      
      // Force weather update with new coordinates
      WeatherData.updateWeather(0);
      
      server.sendHeader("Location", "/WEATHER");
      server.send(302, "text/plain", "Location updated successfully. Weather data refreshed.");
    } else {
      server.sendHeader("Location", "/WEATHER");
      server.send(302, "text/plain", "Invalid coordinates. Please check your input.");
    }
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Missing coordinates. Please try again.");
  }
  #else
  WEBHTML = "Weather not enabled on this device.";
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Weather not enabled on this device.");
  #endif
}

void handleWeatherRefresh() {
  registerHTTPMessage("WthrRef");
  #ifdef _USEWEATHER
  bool updateResult = WeatherData.updateWeather(0);
  if (updateResult) {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather data refreshed successfully.");
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather update failed. Please try again.");
  }
  #elif defined(_USEWEATHERLITE)
  bool updateResult = weatherLiteRequestFromAnyWeatherServer();
  if (updateResult) {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Weather package received.");
  } else {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Weather package request failed.");
  }
  #else
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Weather not enabled on this device.");
#endif
}

void handleWeatherZip() {
  registerHTTPMessage("WthrZip");
  
  // Handle ZIP code lookup (legacy function for backward compatibility)
  if (server.hasArg("zipcode")) {
    String zipCode = server.arg("zipcode");
    
    // Validate ZIP code format
    if (zipCode.length() != 5) {
      server.sendHeader("Content-Type", "application/json");
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid ZIP code format. Must be 5 digits.\"}");
      return;
    }
    
    for (int i = 0; i < 5; i++) {
      if (!isdigit(zipCode.charAt(i))) {
        server.sendHeader("Content-Type", "application/json");
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid ZIP code format. Must contain only digits.\"}");
        return;
      }
    }
    
    // Lookup coordinates from ZIP code
    bool success = getCoordinatesFromZipCode(zipCode);
    if (success) {
      // Return JSON response with coordinates (don't update WeatherData yet)
      String jsonResponse = "{\"success\":true,\"latitude\":" + String(Prefs.LATITUDE, 14) + ",\"longitude\":" + String(Prefs.LONGITUDE, 14) + "}";
      server.sendHeader("Content-Type", "application/json");
      server.send(200, "application/json", jsonResponse);
    } else {
      server.sendHeader("Content-Type", "application/json");
      server.send(500, "application/json", "{\"success\":false,\"message\":\"ZIP code lookup failed. Please check the ZIP code or try again later.\"}");
    }
  } else {
    server.sendHeader("Content-Type", "application/json");
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No ZIP code provided. Please try again.\"}");
  }
  
}


void handleWeatherAddress() {
  registerHTTPMessage("WthrAddr");

  if (server.hasArg("street") && server.hasArg("city") && server.hasArg("state") && server.hasArg("zipcode")) {
    String street = server.arg("street");
    String city = server.arg("city");
    String state = server.arg("state");
    String zipCode = server.arg("zipcode");

    // Use helper function for validation and lookup
    double lat = 0, lon = 0;
    bool success = lookupLocationFromAddress(street, city, state, zipCode, &lat, &lon);

    if (success) {
      // Redirect back to weather page with success message
      String redirectUrl = "/WEATHER?success=address_lookup&lat=" + String(lat, 6) + "&lon=" + String(lon, 6);
      server.sendHeader("Location", redirectUrl);
      server.send(302, "text/plain", "Address lookup successful. Coordinates updated and weather refreshed.");
      // Prefs already saved by lookupLocationFromAddress
    } else {
      server.sendHeader("Location", "/WEATHER?error=lookup_failed");
      server.send(302, "text/plain", "Address lookup failed. Please check the address or try again later.");
    } 
  } else {  
    server.sendHeader("Location", "/WEATHER?error=missing_fields");
    server.send(302, "text/plain", "Missing required address fields. Please provide street, city, state, and ZIP code.");
  }
  
}

bool handlerForWeatherAddress(String street, String city, String state, String zipCode) {
  
  //assume data is valid   
  if (wifiReadyForNetwork()) {
    // Lookup coordinates from complete address
    #ifdef _USETFT
    tft.fillRect(0, tft.height()-200, tft.width(), 100, TFT_BLACK);
    tft.setCursor(0, tft.height()-200);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN);
    tft.println("Getting coordinates from address");
    tft.setTextColor(FG_COLOR);
    #endif
  
    if (getCoordinatesFromAddress(zipCode, street, city, state)) {
      
      if (Prefs.LATITUDE!=0 && Prefs.LONGITUDE!=0) {
        #ifdef _USETFT
        tft.setTextColor(TFT_GREEN);
        tft.println("Geolocation set");
        tft.setTextColor(FG_COLOR);
        #endif
    
        return true;
      } else {
        #ifdef _USETFT
        tft.setTextColor(TFT_RED);
        tft.println("Geolocation not set");
        tft.setTextColor(FG_COLOR);
        #endif    
        return false;
      }
    } else {
      #ifdef _USETFT
      tft.setTextColor(TFT_RED);
      tft.println("Geolocation lookup failed");
      tft.setTextColor(FG_COLOR);
      #endif
      return false;
    }
  } else {
    #ifdef _USETFT
    tft.setTextColor(TFT_RED);
    tft.println("WiFi is down, cannot lookup geolocation");
    tft.setTextColor(FG_COLOR);
    #endif
    return false;
  }
  
  return false;
  
}

#ifdef _USESDCARD
static String sanitizeSdBrowsePath(String path) {
  if (path.length() == 0) return "/";
  path.replace("\\", "/");
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  if (!path.startsWith("/")) path = "/" + path;
  if (path.indexOf("..") >= 0) return "/";
  if (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  return path;
}

static String joinSdPath(const String& base, const String& name) {
  if (base == "/") return "/" + name;
  if (base.endsWith("/")) return base + name;
  return base + "/" + name;
}

static String getSdParentPath(const String& path) {
  if (path == "/" || path.length() <= 1) return "";
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

static String sdEntryBaseName(const String& entryName) {
  int slash = entryName.lastIndexOf('/');
  if (slash >= 0 && slash < (int)entryName.length() - 1) {
    return entryName.substring(slash + 1);
  }
  return entryName;
}

static void sortStringArray(String* names, int count) {
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (names[i].compareTo(names[j]) > 0) {
        String tmp = names[i];
        names[i] = names[j];
        names[j] = tmp;
      }
    }
  }
}

static String sanitizeUploadFileName(String name) {
  name.trim();
  name = sdEntryBaseName(name);
  if (name.length() == 0 || name == "." || name == "..") return "";
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return "";
  if (name.length() > 96) name = name.substring(0, 96);
  return name;
}

static bool ensureSdDirExists(const String& dirPath) {
  String path = sanitizeSdBrowsePath(dirPath);
  if (path == "/" || path.length() == 0) return true;
  if (SD.exists(path.c_str())) return true;

  String parent = getSdParentPath(path);
  if (parent.length() > 0 && parent != path) {
    if (!ensureSdDirExists(parent)) return false;
  }
  return SD.mkdir(path.c_str());
}

static void redirectSdDirResult(const String& browsePath, bool ok, const String& msg) {
  server.sendHeader("Location", "/SDCARD?path=" + urlEncode(browsePath) + "&dir=" + (ok ? "ok" : "fail") + "&msg=" + urlEncode(msg));
  server.send(302, "text/plain", msg);
}

static bool sdIsFirmwareRoot(const String& path) {
  String p = sanitizeSdBrowsePath(path);
  p.toLowerCase();
  return p == "/firmware";
}

// Deletes every file and subdirectory inside /Firmware. The folder itself stays.
static void deleteFirmwareFolderContents(int& deleted, int& failed) {
  deleted = 0;
  failed = 0;
  const String root = "/Firmware";
  while (true) {
    File dir = SD.open(root.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      failed++;
      return;
    }
    String base;
    bool isDir = false;
    bool found = false;
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      base = sdEntryBaseName(entry.name());
      isDir = entry.isDirectory();
      entry.close();
      if (base.length() == 0 || base == "." || base == "..") continue;
      found = true;
      break;
    }
    dir.close();
    if (!found) return;

    const String target = joinSdPath(root, base);
    esp_task_wdt_reset();
    const bool ok = isDir ? sdRemoveDirectoryRecursive(target.c_str()) : sdDeleteFile(target.c_str());
    if (!ok) {
      failed++;
      storeError("Firmware delete failed (" + target + ")", ERROR_SD_FILEDEL, true);
      return;
    }
    deleted++;
  }
}

static void appendSdCardDirectoryManageForms(const String& currentPath) {
  String ep = "/SDCARD?path=" + urlEncode(currentPath);
  const bool firmwareDir = sdIsFirmwareRoot(currentPath);

  WEBHTML = WEBHTML + "<h3>Directory Management</h3>";
  if (firmwareDir) {
    WEBHTML = WEBHTML + "<p><strong>/Firmware</strong> accepts firmware uploads and file deletes. "
      "Use <code>&lt;devicename&gt;-&lt;x.x.x&gt;.bin</code> (version after the last hyphen). "
      "Uploading a newer version automatically deletes older binaries for that device. "
      "Delete All removes every file and subfolder here and leaves /Firmware in place.</p>";
    WEBHTML = WEBHTML + "<form method=\"POST\" action=\"" + ep + "\" style=\"margin: 8px 0;\" onsubmit=\"return confirm('Delete ALL files and subfolders in /Firmware? This cannot be undone.');\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"action\" value=\"deleteall\">";
    WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete All\" style=\"padding: 8px 16px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\"></form>";
    WEBHTML = WEBHTML + "<form method=\"POST\" action=\"" + ep + "\" onsubmit=\"return confirm('Delete this directory and ALL contents?');\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"action\" value=\"rmdir\">";
    WEBHTML = WEBHTML + "Delete subdirectory: <input name=\"dirname\" maxlength=\"64\" required> <input type=\"submit\" value=\"Delete Directory\"></form>";
  } else {
    WEBHTML = WEBHTML + "<form method=\"POST\" action=\"" + ep + "\"><input type=\"hidden\" name=\"action\" value=\"mkdir\">";
    WEBHTML = WEBHTML + "New directory: <input name=\"dirname\" maxlength=\"64\" required> <input type=\"submit\" value=\"Create Directory\"></form>";
    WEBHTML = WEBHTML + "<form method=\"POST\" action=\"" + ep + "\" onsubmit=\"return confirm('Delete this directory and ALL contents?');\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"action\" value=\"rmdir\">";
    WEBHTML = WEBHTML + "Delete directory: <input name=\"dirname\" maxlength=\"64\" required> <input type=\"submit\" value=\"Delete Directory\"></form>";
    WEBHTML = WEBHTML + "<p>Protected (folder delete blocked): /Icons, /Data, /System Volume Information, and /Firmware root only. "
      "Individual files in those folders can still be deleted from the list above. "
      "<a href=\"/SDCARD?path=%2FFirmware\">Browse /Firmware</a> to upload or delete OTA binaries.</p>";
  }
}

static String jsonEscape(const String& s) {
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

static void appendSdCardUploadForm(const String& currentPath) {
  String jsPath = currentPath;
  jsPath.replace("\\", "\\\\");
  jsPath.replace("\"", "\\\"");

  WEBHTML = WEBHTML + "<h3>Upload File or Folder</h3>";
  WEBHTML = WEBHTML + "<p>Upload to <strong>" + currentPath + "</strong>. Existing files are replaced. Folders may contain up to <strong>50 files</strong> (uploaded one at a time). Dropping a file or folder starts upload immediately.</p>";
  if (currentPath.equalsIgnoreCase("/Firmware")) {
    WEBHTML = WEBHTML + "<p>Firmware files: name each binary <code>&lt;devicename&gt;-&lt;x.x.x&gt;.bin</code> (e.g. <code>PleasantB-9.0.0.bin</code>). The version is the segment after the <strong>last</strong> hyphen. "
      "A successful upload of a newer version deletes older <code>&lt;devicename&gt;-*.bin</code> files for that device.</p>";
  }
  WEBHTML = WEBHTML + "<form id=\"sd-upload-form\" method=\"POST\" action=\"/SDCARD_UPLOAD?path=" + urlEncode(currentPath) + "\" enctype=\"multipart/form-data\">";
  WEBHTML = WEBHTML + "<div id=\"sd-drop-zone\" style=\"border: 2px dashed #999; padding: 24px; margin: 10px 0; text-align: center; cursor: pointer; background-color: #fafafa;\">";
  WEBHTML = WEBHTML + "Drag and drop a file or folder here to upload now";
  WEBHTML = WEBHTML + "<input type=\"file\" name=\"upload\" id=\"sd-file-input\" style=\"display:none;\">";
  WEBHTML = WEBHTML + "<input type=\"file\" id=\"sd-folder-input\" webkitdirectory directory multiple style=\"display:none;\">";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<p id=\"sd-upload-name\" style=\"color:#555;\"></p>";
  WEBHTML = WEBHTML + "<button type=\"button\" id=\"sd-choose-file\" style=\"padding: 8px 16px; margin-right: 8px; background-color: #607D8B; color: white; border: none; border-radius: 4px; cursor: pointer;\">Choose File</button>";
  WEBHTML = WEBHTML + "<button type=\"button\" id=\"sd-choose-folder\" style=\"padding: 8px 16px; margin-right: 8px; background-color: #607D8B; color: white; border: none; border-radius: 4px; cursor: pointer;\">Choose Folder</button>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Upload Selected File\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  WEBHTML = WEBHTML + "<script>\n";
  WEBHTML = WEBHTML + "const SD_BASE_PATH = \"" + jsPath + "\";\n";
  WEBHTML = WEBHTML + "const SD_MAX_FOLDER_FILES = 50;\n";
  serverTextAppend(R"===(
(function(){
  const zone = document.getElementById('sd-drop-zone');
  const input = document.getElementById('sd-file-input');
  const folderInput = document.getElementById('sd-folder-input');
  const label = document.getElementById('sd-upload-name');
  const chooseFile = document.getElementById('sd-choose-file');
  const chooseFolder = document.getElementById('sd-choose-folder');
  if (!zone || !input || !folderInput) return;

  function joinPath(base, sub) {
    if (!sub) return base;
    if (base === '/') return '/' + sub.replace(/^\/+/, '');
    return base.replace(/\/+$/, '') + '/' + sub.replace(/^\/+/, '');
  }

  function showSelected(text) {
    if (label) label.textContent = text;
  }

  function redirectAfterUpload(ok, fail, lastError, lastName) {
    let redirect = '/SDCARD?path=' + encodeURIComponent(SD_BASE_PATH);
    if (fail === 0) {
      if (ok === 1 && lastName) {
        redirect += '&upload=ok&file=' + encodeURIComponent(lastName);
      } else {
        redirect += '&upload=ok&msg=' + encodeURIComponent('Upload complete: ' + ok + ' file(s)');
      }
    } else {
      redirect += '&upload=fail&msg=' + encodeURIComponent('Upload: ' + ok + ' ok, ' + fail + ' failed. ' + lastError);
    }
    window.location.href = redirect;
  }

  function readDirectoryEntries(dirEntry, prefix) {
    return new Promise(function(resolve, reject) {
      const reader = dirEntry.createReader();
      const entries = [];
      function readBatch() {
        reader.readEntries(function(batch) {
          if (!batch.length) {
            resolve(entries);
            return;
          }
          entries.push.apply(entries, batch);
          readBatch();
        }, reject);
      }
      readBatch();
    }).then(function(allEntries) {
      const files = [];
      const pending = [];
      for (const entry of allEntries) {
        const rel = prefix ? prefix + '/' + entry.name : entry.name;
        if (entry.isFile) {
          pending.push(new Promise(function(res, rej) {
            entry.file(function(file) {
              files.push({ file: file, relativePath: rel });
              res();
            }, rej);
          }));
        } else if (entry.isDirectory) {
          pending.push(readDirectoryEntries(entry, rel).then(function(nested) {
            files.push.apply(files, nested);
          }));
        }
      }
      return Promise.all(pending).then(function() { return files; });
    });
  }

  async function uploadOneFile(file, targetDir) {
    const fd = new FormData();
    fd.append('upload', file, file.name);
    const url = '/SDCARD_UPLOAD?path=' + encodeURIComponent(targetDir) + '&ajax=1';
    const resp = await fetch(url, { method: 'POST', body: fd });
    const data = await resp.json();
    if (!resp.ok || !data.ok) {
      throw new Error(data.msg || 'Upload failed');
    }
    return data.file || file.name;
  }

  async function uploadFolderFiles(fileEntries) {
    if (!fileEntries.length) {
      showSelected('No files found in folder.');
      return;
    }
    if (fileEntries.length > SD_MAX_FOLDER_FILES) {
      showSelected('Too many files (' + fileEntries.length + '). Maximum is ' + SD_MAX_FOLDER_FILES + '.');
      return;
    }

    let ok = 0;
    let fail = 0;
    let lastError = '';
    let lastName = '';
    for (let i = 0; i < fileEntries.length; i++) {
      const rel = (fileEntries[i].relativePath || fileEntries[i].file.name).replace(/\\/g, '/');
      const parts = rel.split('/');
      parts.pop();
      const subDir = parts.join('/');
      const targetDir = joinPath(SD_BASE_PATH, subDir);
      showSelected('Uploading ' + (i + 1) + '/' + fileEntries.length + ': ' + rel);
      try {
        const uploadFile = fileEntries[i].file;
        lastName = await uploadOneFile(uploadFile, targetDir);
        ok++;
      } catch (err) {
        fail++;
        lastError = err.message || 'Upload failed';
      }
    }
    redirectAfterUpload(ok, fail, lastError, lastName);
  }

  async function uploadDroppedFiles(fileList) {
    const files = Array.from(fileList || []);
    if (!files.length) return;
    let ok = 0;
    let fail = 0;
    let lastError = '';
    let lastName = '';
    for (let i = 0; i < files.length; i++) {
      showSelected('Uploading ' + (i + 1) + '/' + files.length + ': ' + files[i].name);
      try {
        lastName = await uploadOneFile(files[i], SD_BASE_PATH);
        ok++;
      } catch (err) {
        fail++;
        lastError = err.message || 'Upload failed';
      }
    }
    redirectAfterUpload(ok, fail, lastError, lastName);
  }

  chooseFile.addEventListener('click', function(e) {
    e.preventDefault();
    input.click();
  });
  chooseFolder.addEventListener('click', function(e) {
    e.preventDefault();
    folderInput.click();
  });
  zone.addEventListener('click', function() { input.click(); });

  zone.addEventListener('dragover', function(e) {
    e.preventDefault();
    zone.style.backgroundColor = '#eef6ff';
  });
  zone.addEventListener('dragleave', function() {
    zone.style.backgroundColor = '#fafafa';
  });
  zone.addEventListener('drop', function(e) {
    e.preventDefault();
    zone.style.backgroundColor = '#fafafa';
    const items = e.dataTransfer.items;
    if (items && items.length > 0 && items[0].webkitGetAsEntry) {
      const entry = items[0].webkitGetAsEntry();
      if (entry && entry.isDirectory) {
        showSelected('Reading folder: ' + entry.name + '...');
        readDirectoryEntries(entry, entry.name).then(uploadFolderFiles).catch(function(err) {
          showSelected('Folder read failed: ' + err.message);
        });
        return;
      }
    }
    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      showSelected('Uploading: ' + e.dataTransfer.files[0].name);
      uploadDroppedFiles(e.dataTransfer.files);
    }
  });

  input.addEventListener('change', function() {
    if (input.files && input.files.length > 0) {
      showSelected('Selected file: ' + input.files[0].name + ' (click Upload Selected File)');
    }
  });

  folderInput.addEventListener('change', function() {
    if (!folderInput.files || folderInput.files.length === 0) return;
    const entries = Array.from(folderInput.files).map(function(f) {
      return { file: f, relativePath: f.webkitRelativePath || f.name };
    });
    uploadFolderFiles(entries);
  });
})();
</script>)===")
  ;
}

static File sdcardUploadFile;
static String sdcardUploadTargetPath;
static String sdcardUploadError;
static size_t sdcardUploadBytesWritten = 0;
static const size_t SDCARD_UPLOAD_MAX_BYTES = 8UL * 1024UL * 1024UL;

static void logSdUploadError(const String& targetPath, const String& message) {
  if (message == "Upload aborted") return;
  String errMsg = "SD upload failed";
  if (targetPath.length() > 0) errMsg += " (" + targetPath + ")";
  errMsg += ": " + message;
  storeError(errMsg, ERROR_SD_FILEWRITE, true);
}

void handleSDCARD_UPLOAD() {
  registerHTTPMessage("SDUp");
  String dir = "/";
  if (server.hasArg("path")) dir = sanitizeSdBrowsePath(server.arg("path"));

  bool ajax = server.hasArg("ajax");
  String uploadError = sdcardUploadError;
  size_t bytesWritten = sdcardUploadBytesWritten;
  String targetPath = sdcardUploadTargetPath;

  sdcardUploadError = "";
  sdcardUploadBytesWritten = 0;
  sdcardUploadTargetPath = "";

  if (uploadError.length() > 0) {
    logSdUploadError(targetPath, uploadError);
  } else if (bytesWritten == 0) {
    storeError("SD upload failed: No file received", ERROR_SD_FILEWRITE, true);
  } else {
    pruneOlderSDFirmwareAfterUpload(targetPath.c_str());
  }

  if (ajax) {
    if (uploadError.length() > 0) {
      server.send(500, "application/json", "{\"ok\":false,\"msg\":\"" + jsonEscape(uploadError) + "\"}");
    } else if (bytesWritten > 0) {
      server.send(200, "application/json", "{\"ok\":true,\"file\":\"" + jsonEscape(sdEntryBaseName(targetPath)) + "\"}");
    } else {
      server.send(500, "application/json", "{\"ok\":false,\"msg\":\"No file received\"}");
    }
    return;
  }

  String redirect = "/SDCARD?path=" + urlEncode(dir);
  if (uploadError.length() > 0) {
    redirect += "&upload=fail&msg=" + urlEncode(uploadError);
  } else if (bytesWritten > 0) {
    redirect += "&upload=ok&file=" + urlEncode(sdEntryBaseName(targetPath));
  } else {
    redirect += "&upload=fail&msg=" + urlEncode("No file received");
  }

  server.sendHeader("Location", redirect);
  server.send(303, "text/plain", "");
}

void handleSDCARD_UPLOADFile() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    sdcardUploadError = "";
    sdcardUploadBytesWritten = 0;
    sdcardUploadTargetPath = "";
    if (sdcardUploadFile) {
      sdcardUploadFile.close();
      sdcardUploadFile = File();
    }

    String dir = server.hasArg("path") ? sanitizeSdBrowsePath(server.arg("path")) : "/";
    String fileName = sanitizeUploadFileName(upload.filename);
    if (fileName.length() == 0) {
      sdcardUploadError = "Invalid file name";
      return;
    }

    sdcardUploadTargetPath = joinSdPath(dir, fileName);
    if (!ensureSdDirExists(dir)) {
      sdcardUploadError = "Could not create directory on SD card";
      return;
    }
    SD.remove(sdcardUploadTargetPath.c_str());
    sdcardUploadFile = SD.open(sdcardUploadTargetPath.c_str(), FILE_WRITE);
    if (!sdcardUploadFile) {
      sdcardUploadError = "Could not create file on SD card";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (sdcardUploadError.length() > 0) return;
    if (!sdcardUploadFile) {
      sdcardUploadError = "File not open for writing";
      return;
    }
    if (sdcardUploadBytesWritten + upload.currentSize > SDCARD_UPLOAD_MAX_BYTES) {
      sdcardUploadError = "File exceeds 8 MB limit";
      sdcardUploadFile.close();
      sdcardUploadFile = File();
      SD.remove(sdcardUploadTargetPath.c_str());
      return;
    }
    size_t written = sdcardUploadFile.write(upload.buf, upload.currentSize);
    sdcardUploadBytesWritten += written;
    if (written != upload.currentSize) {
      sdcardUploadError = "SD write failed";
      sdcardUploadFile.close();
      sdcardUploadFile = File();
      SD.remove(sdcardUploadTargetPath.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (sdcardUploadFile) {
      sdcardUploadFile.close();
      sdcardUploadFile = File();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (sdcardUploadFile) {
      sdcardUploadFile.close();
      sdcardUploadFile = File();
    }
    if (sdcardUploadTargetPath.length() > 0) SD.remove(sdcardUploadTargetPath.c_str());
    sdcardUploadError = "Upload aborted";
  }
}

static void appendSdCardDirectoryListing(const String& currentPath) {
  const int MAX_ENTRIES = 64;
  String dirNames[MAX_ENTRIES];
  String fileNames[MAX_ENTRIES];
  uint64_t fileSizes[MAX_ENTRIES];
  int dirCount = 0;
  int fileCount = 0;
  bool truncated = false;

  WEBHTML = WEBHTML + "<h3>File Browser</h3>";
  WEBHTML = WEBHTML + "<p><strong>Current path:</strong> " + currentPath + "</p>";
  appendSdCardUploadForm(currentPath);
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Name</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Size</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Last Updated</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Type</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Actions</th>";
  WEBHTML = WEBHTML + "</tr>";

  String parentPath = getSdParentPath(currentPath);
  String browseEp = "/SDCARD?path=" + urlEncode(currentPath);
  if (parentPath.length() > 0) {
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "<a href=\"/SDCARD?path=" + urlEncode(parentPath) + "\">..</a>";
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\">N/A</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">N/A</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Directory</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\"></td></tr>";
  }

  File dir = SD.open(currentPath.c_str());
  if (!dir || !dir.isDirectory()) {
    WEBHTML = WEBHTML + "<tr><td colspan=\"5\" style=\"border: 1px solid #ddd; padding: 8px; text-align: center; color: #c00;\">Could not open directory</td></tr>";
    WEBHTML = WEBHTML + "</table>";
    if (dir) dir.close();
    appendSdCardDirectoryManageForms(currentPath);
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    String baseName = sdEntryBaseName(entry.name());
    if (baseName.length() == 0 || baseName == "." || baseName == "..") {
      entry.close();
      entry = dir.openNextFile();
      continue;
    }

    if (entry.isDirectory()) {
      if (dirCount < MAX_ENTRIES) {
        dirNames[dirCount++] = baseName;
      } else {
        truncated = true;
      }
    } else if (fileCount < MAX_ENTRIES) {
      fileNames[fileCount] = baseName;
      fileSizes[fileCount] = entry.size();
      fileCount++;
    } else {
      truncated = true;
    }

    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  sortStringArray(dirNames, dirCount);
  sortStringArray(fileNames, fileCount);

  for (int i = 0; i < dirCount; i++) {
    String childPath = joinSdPath(currentPath, dirNames[i]);
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "<a href=\"/SDCARD?path=" + urlEncode(childPath) + "\">" + dirNames[i] + "/</a>";
    WEBHTML = WEBHTML + "</td><td style=\"border: 1px solid #ddd; padding: 8px;\">N/A</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">N/A</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">Directory</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">";
    if (!sdDirectoryIsProtected(childPath.c_str())) {
      WEBHTML = WEBHTML + "<form method=\"POST\" action=\"" + browseEp + "\" style=\"display:inline\" onsubmit=\"return confirm('Delete directory " + dirNames[i] + " and ALL contents?');\">";
      WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"action\" value=\"rmdir\">";
      WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"dirname\" value=\"" + dirNames[i] + "\">";
      WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete\" style=\"padding: 4px 8px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
      WEBHTML = WEBHTML + "</form>";
    }
    WEBHTML = WEBHTML + "</td></tr>";
  }

  for (int i = 0; i < fileCount; i++) {
    String filePath = joinSdPath(currentPath, fileNames[i]);
    String lastUpdated = "Unknown";
    File f = SD.open(filePath.c_str(), FILE_READ);
    if (f) {
      time_t fileTime = f.getLastWrite();
      if (fileTime > 0) lastUpdated = String(dateifyLocal(fileTime, "mm/dd/yyyy hh:nn:ss"));
      f.close();
    }
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "<a href=\"/SDCARD_DOWNLOAD?path=" + urlEncode(filePath) + "\">" + fileNames[i] + "</a>";
    WEBHTML = WEBHTML + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(fileSizes[i]) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + lastUpdated + "</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">File</td>";
    WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">";
    WEBHTML = WEBHTML + "<form method=\"POST\" action=\"" + browseEp + "\" style=\"display:inline\" onsubmit=\"return confirm('Delete file " + fileNames[i] + "?');\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"action\" value=\"unlink\">";
    WEBHTML = WEBHTML + "<input type=\"hidden\" name=\"filename\" value=\"" + fileNames[i] + "\">";
    WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete\" style=\"padding: 4px 8px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
    WEBHTML = WEBHTML + "</form></td></tr>";
  }

  if (dirCount == 0 && fileCount == 0 && parentPath.length() == 0) {
    WEBHTML = WEBHTML + "<tr><td colspan=\"5\" style=\"border: 1px solid #ddd; padding: 8px; text-align: center; color: #666;\">Directory is empty</td></tr>";
  } else if (dirCount == 0 && fileCount == 0) {
    WEBHTML = WEBHTML + "<tr><td colspan=\"5\" style=\"border: 1px solid #ddd; padding: 8px; text-align: center; color: #666;\">No files in this directory</td></tr>";
  }

  if (truncated) {
    WEBHTML = WEBHTML + "<tr><td colspan=\"5\" style=\"border: 1px solid #ddd; padding: 8px; text-align: center; color: #666;\">Listing truncated (64 entries max per type)</td></tr>";
  }

  WEBHTML = WEBHTML + "</table>";
  WEBHTML = WEBHTML + "<p><strong>Directories:</strong> " + String(dirCount) + ", <strong>Files:</strong> " + String(fileCount) + "</p>";
  appendSdCardDirectoryManageForms(currentPath);
}
#endif

void handleSDCARD_DIR() {
#ifdef _USESDCARD
  registerHTTPMessage("SDDir");

  String parentPath = server.hasArg("path") ? sanitizeSdBrowsePath(server.arg("path")) : "/";

  if (!server.hasArg("action")) {
    redirectSdDirResult(parentPath, false, "Missing action");
    return;
  }

  String action = server.arg("action");

  if (action == "unlink") {
    if (!server.hasArg("filename")) {
      redirectSdDirResult(parentPath, false, "Missing file name");
      return;
    }
    String fileName = sanitizeUploadFileName(server.arg("filename"));
    if (fileName.length() == 0) {
      redirectSdDirResult(parentPath, false, "Invalid file name");
      return;
    }
    String targetPath = joinSdPath(parentPath, fileName);
    bool ok = sdDeleteFile(targetPath.c_str());
    if (!ok) {
      storeError("SD file delete failed (" + targetPath + ")", ERROR_SD_FILEDEL, true);
    }
    redirectSdDirResult(parentPath, ok, ok ? ("Deleted " + fileName) : "Delete failed");
    return;
  }

  if (action == "deleteall") {
    if (!sdIsFirmwareRoot(parentPath)) {
      redirectSdDirResult(parentPath, false, "Delete All is only for /Firmware");
      return;
    }
    int deleted = 0;
    int failed = 0;
    deleteFirmwareFolderContents(deleted, failed);
    String msg;
    if (failed > 0) msg = "Deleted " + String(deleted) + ", " + String(failed) + " failed";
    else if (deleted == 0) msg = "Nothing to delete";
    else msg = "Deleted " + String(deleted) + " item" + (deleted == 1 ? "" : "s");
    redirectSdDirResult(parentPath, failed == 0, msg);
    return;
  }

  if (!server.hasArg("dirname")) {
    redirectSdDirResult(parentPath, false, "Missing directory name");
    return;
  }

  String dirName = sanitizeUploadFileName(server.arg("dirname"));
  if (dirName.length() == 0) {
    redirectSdDirResult(parentPath, false, "Invalid directory name");
    return;
  }

  String targetPath = joinSdPath(parentPath, dirName);

  if (action == "mkdir") {
    if (SD.exists(targetPath.c_str())) {
      redirectSdDirResult(parentPath, false, "Already exists");
      return;
    }
    bool ok = ensureSdDirExists(targetPath);
    redirectSdDirResult(parentPath, ok, ok ? "Created" : "Create failed");
    return;
  }

  if (!SD.exists(targetPath.c_str())) {
    redirectSdDirResult(parentPath, false, "Not found");
    return;
  }
  File dir = SD.open(targetPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    redirectSdDirResult(parentPath, false, "Not a directory");
    return;
  }
  dir.close();
  if (sdDirectoryIsProtected(targetPath.c_str())) {
    redirectSdDirResult(parentPath, false, "Protected directory");
    return;
  }
  if (action != "rmdir") {
    redirectSdDirResult(parentPath, false, "Unknown action");
    return;
  }

  bool ok = sdRemoveDirectoryRecursive(targetPath.c_str());
  redirectSdDirResult(parentPath, ok, ok ? "Deleted" : "Delete failed");
#else
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "SD card not enabled");
#endif
}

void handleSDCARD_DOWNLOAD() {
  registerHTTPMessage("SDDownload");

#ifdef _USESDCARD
  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path parameter");
    return;
  }

  String filePath = sanitizeSdBrowsePath(server.arg("path"));
  if (filePath.length() <= 1) {
    server.send(400, "text/plain", "Invalid file path");
    return;
  }

  if (!SD.exists(filePath.c_str())) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File f = SD.open(filePath.c_str(), FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    server.send(404, "text/plain", "Not a file");
    return;
  }

  String fileName = sdEntryBaseName(filePath);
  String contentType = "application/octet-stream";
  if (fileName.endsWith(".json")) {
    contentType = "application/json";
  } else if (fileName.endsWith(".txt") || fileName.endsWith(".dat") || fileName.endsWith(".log")) {
    contentType = "text/plain";
  } else if (fileName.endsWith(".html") || fileName.endsWith(".htm")) {
    contentType = "text/html";
  } else if (fileName.endsWith(".csv")) {
    contentType = "text/csv";
  }

  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
  server.sendHeader("Cache-Control", "no-store");
  esp_task_wdt_reset();
  server.streamFile(f, contentType);
  f.close();
#else
  server.send(404, "text/plain", "SD card not enabled");
#endif
}

void handleSDCARD() {
  
  
  registerHTTPMessage("SDCard");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("SD Card Configuration");
  serverTextStreamBegin(200, true);
  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush(true);
  
  // SD Card Information
  WEBHTML = WEBHTML + "<h3>SD Card Information</h3>";
  
  #ifndef _USESDCARD
    WEBHTML = WEBHTML + "There is no SD card installed or it is not enabled.<br>"; 
      
  #else

  ensureSdDirExists("/Firmware");

  // Error log button
    WEBHTML = WEBHTML + "<a href=\"/ERROR_LOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View Error Log</a> ";
    WEBHTML = WEBHTML + "<a href=\"/SDCARD_SYSTEMLOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #3F51B5; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View System Log</a><br><br>";

    WEBHTML = WEBHTML + "</font>---------------------<br>";      
  
  // Get SD card size information
  uint64_t cardSize = SD.cardSize();
  uint64_t totalBytes = cardSize;  // Use cardSize instead of SD.totalBytes()
  uint64_t usedBytes = SD.usedBytes();  // Direct usage, not subtraction
  
  // Sanity check: if usedBytes is larger than cardSize, something is wrong
  if (usedBytes > cardSize) {
    SerialPrint("WARNING: usedBytes > cardSize! This indicates a library issue.", true);
    SerialPrint("usedBytes: " + String(usedBytes) + ", cardSize: " + String(cardSize), true);
    
    // Try alternative approach - estimate used space based on actual files
    uint64_t estimatedUsedBytes = 0;
    File root = SD.open("/Data");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        estimatedUsedBytes += file.size();
        file = root.openNextFile();
      }
      root.close();
      
      if (estimatedUsedBytes < cardSize) {
        usedBytes = estimatedUsedBytes;
        SerialPrint("Using estimated used space from file sizes: " + String(usedBytes) + " bytes", true);
      }
    }
  }
  

  
  // Additional debugging for potential issues
  if (cardSize < (1024ULL * 1024ULL * 1024ULL)) {
    SerialPrint("WARNING: Card size is less than 1GB - this might indicate a detection issue", true);
  }
  if (usedBytes > (1024ULL * 1024ULL * 1024ULL * 100ULL)) {
    SerialPrint("WARNING: Used bytes is extremely large (>100GB) - this might indicate a calculation bug", true);
  }
  
  // Check if this looks like a 16GB card (common size)
  if (cardSize >= (15ULL * 1024ULL * 1024ULL * 1024ULL) && cardSize <= (16ULL * 1024ULL * 1024ULL * 1024ULL)) {
    SerialPrint("Detected ~16GB card - this should show ~14.8GB free space", true);
    
      // If this is a 16GB card but usedBytes is suspiciously large, try to estimate
      if (usedBytes > (2ULL * 1024ULL * 1024ULL * 1024ULL)) {
        SerialPrint("WARNING: 16GB card showing >2GB used - likely a library bug", true);
        SerialPrint("Attempting to estimate actual used space from files...", true);
        
        // Force recalculation of used space from actual files
        uint64_t actualUsedBytes = 0;
        File root = SD.open("/Data");
        if (root && root.isDirectory()) {
          File file = root.openNextFile();
          while (file) {
            actualUsedBytes += file.size();
            file = root.openNextFile();
          }
          root.close();
          
          if (actualUsedBytes < usedBytes && actualUsedBytes < cardSize) {
            usedBytes = actualUsedBytes;
            SerialPrint("Corrected used space from " + String(usedBytes) + " to " + String(actualUsedBytes) + " bytes", true);
          }
        }
      }
  }
  
  SerialPrint("==========================", true);
  
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f0f0f0;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Property</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 8px; text-align: left;\">Value</th>";
  WEBHTML = WEBHTML + "</tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Card Size</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(cardSize) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Total Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(totalBytes) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Used Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(usedBytes) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Free Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + formatBytes(totalBytes - usedBytes) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Number of Devices</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Sensors.getNumDevices()) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Number of Sensors</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Sensors.getNumSensors()) + "</td></tr>";
  
  WEBHTML = WEBHTML + "</table>";
  
  #ifndef _USEGSHEET
  // Debug information (raw values) — omitted on GSheets servers to reduce flash size
  WEBHTML = WEBHTML + "<details style=\"margin: 10px 0;\">";
  WEBHTML = WEBHTML + "<summary style=\"cursor: pointer; color: #666;\"><strong>Debug Information (Raw Values)</strong></summary>";
  WEBHTML = WEBHTML + "<table style=\"width:100%; border-collapse: collapse; margin: 10px 0; font-family: monospace; font-size: 12px;\">";
  WEBHTML = WEBHTML + "<tr style=\"background-color: #f8f8f8;\">";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">Property</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">Raw Bytes</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">MB</th>";
  WEBHTML = WEBHTML + "<th style=\"border: 1px solid #ddd; padding: 4px; text-align: left;\">GB</th>";
  WEBHTML = WEBHTML + "</tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\"><strong>SD.cardSize()</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(cardSize) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(cardSize / (1024.0 * 1024.0), 2) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(cardSize / (1024.0 * 1024.0 * 1024.0), 2) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\"><strong>SD.usedBytes()</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(usedBytes) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(usedBytes / (1024.0 * 1024.0), 2) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(usedBytes / (1024.0 * 1024.0 * 1024.0), 2) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\"><strong>Free Space</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String(totalBytes - usedBytes) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String((totalBytes - usedBytes) / (1024.0 * 1024.0), 2) + "</td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 4px;\">" + String((totalBytes - usedBytes) / (1024.0 * 1024.0 * 1024.0), 2) + "</td></tr>";
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\">";
  WEBHTML = WEBHTML + "<div style=\"margin: 10px 0; padding: 10px; background-color: #f0f8ff; border-left: 4px solid #0066cc;\">";
  WEBHTML = WEBHTML + "The debug information shows raw values returned by the file table. ";
  WEBHTML = WEBHTML + "<strong>Note:</strong> Free space may be incorrect in the following cases:";
  WEBHTML = WEBHTML + "<ul style=\"margin: 5px 0; padding-left: 20px;\">";
  WEBHTML = WEBHTML + "<li>Issues with large cards (>16GB)</li>";
  WEBHTML = WEBHTML + "<li>SD card filesystem limitations (FAT16/FAT32)</li>";
  WEBHTML = WEBHTML + "<li>Card formatting or partition issues</li>";
  WEBHTML = WEBHTML + "<li>Files not being properly closed or deleted</li>";
  WEBHTML = WEBHTML + "<li>Fragmentation or corruption of the filesystem</li>";
  WEBHTML = WEBHTML + "</ul>";
  WEBHTML = WEBHTML + "A warning will follow this message if an error is likely.";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "</td></tr>";
  if (usedBytes > (2ULL * 1024ULL * 1024ULL * 1024ULL) && cardSize >= (15ULL * 1024ULL * 1024ULL * 1024ULL)) {
    WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 4px;\">";
    WEBHTML = WEBHTML + "<div style=\"margin: 10px 0; padding: 10px; background-color: #fff3cd; border-left: 4px solid #ffc107;\">";
    WEBHTML = WEBHTML + "<strong>WARNING:</strong> Large card detected with suspicious used space values. ";
    WEBHTML = WEBHTML + "The system has attempted to correct this by scanning actual files. ";
    WEBHTML = WEBHTML + "You should manually check SD card usage if concerned about the values.";
    WEBHTML = WEBHTML + "</div>";
    WEBHTML = WEBHTML + "</td></tr>";
  }

  WEBHTML = WEBHTML + "</table>";
  WEBHTML = WEBHTML + "</details>";
  #endif
  
  String browsePath = "/";
  if (server.hasArg("path")) {
    browsePath = sanitizeSdBrowsePath(server.arg("path"));
  }
  if (server.hasArg("upload")) {
    if (server.arg("upload") == "ok") {
      if (server.hasArg("msg")) {
        WEBHTML = WEBHTML + "<p style=\"color: #2e7d32; font-weight: bold;\">" + server.arg("msg") + "</p>";
      } else {
        WEBHTML = WEBHTML + "<p style=\"color: #2e7d32; font-weight: bold;\">Upload successful: " + server.arg("file") + "</p>";
      }
    } else if (server.hasArg("msg")) {
      WEBHTML = WEBHTML + "<p style=\"color: #c62828; font-weight: bold;\">Upload failed: " + server.arg("msg") + "</p>";
    }
  }
  if (server.hasArg("dir") && server.hasArg("msg")) {
    if (server.arg("dir") == "ok") {
      WEBHTML = WEBHTML + "<p style=\"color: #2e7d32; font-weight: bold;\">" + server.arg("msg") + "</p>";
    } else if (server.arg("dir") == "fail") {
      WEBHTML = WEBHTML + "<p style=\"color: #c62828; font-weight: bold;\">" + server.arg("msg") + "</p>";
    }
  }
  appendSdCardDirectoryListing(browsePath);

  WEBHTML = WEBHTML + "</font>---------------------<br>";      
  // Error log button
    WEBHTML = WEBHTML + "<a href=\"/ERROR_LOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View Error Log</a> ";
    WEBHTML = WEBHTML + "<a href=\"/SDCARD_SYSTEMLOG\" target=\"_blank\" style=\"display: inline-block; padding: 10px 20px; background-color: #3F51B5; color: white; text-decoration: none; border-radius: 4px; cursor: pointer;\">View System Log</a><br><br>";
  
  // Action Buttons
  WEBHTML = WEBHTML + "<h3>SD Card Actions</h3>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_DELETE_SENSORS\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Delete All Sensor History Files\" style=\"padding: 10px 20px; background-color: #f44336; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  // Save buttons
  WEBHTML = WEBHTML + "<br><br>";
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_SAVE_SCREENFLAGS\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Save ScreenFlags.dat Now\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_SAVE_WEATHERDATA\" style=\"display: inline; margin-right: 10px;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Save WeatherData.dat Now\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/SDCARD_STORE_DEVICES\" style=\"display: inline;\">";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Store DevicesSensors Data Now\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  
  #endif
  
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200, true);

  return;

}

void handleSDCARD_DELETE_SENSORS() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDDelSns");
  
  uint16_t deletedCount = deleteSensorDataSD();
  if (deletedCount > 0) {
    SerialPrint("Deleted " + String(deletedCount) + " sensor history files", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Deleted " + String(deletedCount) + " sensor history files.");
  } else {
    SerialPrint("No files found.", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "No files found.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_STORE_DEVICES() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDStoreDev");
  
  // Store the devices and sensors data to SD card
  if (storeDevicesSensorsSD()) {
    SerialPrint("DevicesSensors data stored to SD card successfully", true);
    server.send(302, "text/plain", "DevicesSensors data stored to SD card successfully.");
  } else {
    SerialPrint("Failed to store DevicesSensors data to SD card", true);
    server.send(302, "text/plain", "Failed to store DevicesSensors data to SD card.");
  }
  #endif
}

void handleSDCARD_SAVE_SCREENFLAGS() {
  #ifdef _USESDCARD
  registerHTTPMessage("SDSaveScr");
  
  // Save ScreenFlags.dat immediately
  if (storeScreenInfoSD()) {
    SerialPrint("ScreenFlags.dat saved to SD card successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "ScreenFlags.dat saved to SD card successfully.");
  } else {
    SerialPrint("Failed to save ScreenFlags.dat to SD card", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to save ScreenFlags.dat to SD card.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleSDCARD_SAVE_WEATHERDATA() {
  #ifdef _USESDCARD
  #ifdef _USEWEATHER
  registerHTTPMessage("SDSaveWthr");
  
  // Save WeatherData.dat immediately
  if (storeWeatherDataSD()) {
    SerialPrint("WeatherData.dat saved to SD card successfully", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "WeatherData.dat saved to SD card successfully.");
  } else {
    SerialPrint("Failed to save WeatherData.dat to SD card", true);
    server.sendHeader("Location", "/SDCARD");
    server.send(302, "text/plain", "Failed to save WeatherData.dat to SD card.");
  }
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
  #endif
}

static String htmlEscapeText(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (unsigned int i = 0; i < input.length(); ++i) {
    const char c = input.charAt(i);
    if (c == '&') output += "&amp;";
    else if (c == '<') output += "&lt;";
    else if (c == '>') output += "&gt;";
    else output += c;
  }
  return output;
}

void handleSDCARD_SYSTEMLOG() {
  registerHTTPMessage("SDSysLog");

  #ifdef _USESDCARD
  const char* filename = "/Data/systemlog.txt";
  String htmlContent = "<html><head><title>System Log</title>";
  htmlContent += "<style>";
  htmlContent += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  htmlContent += "h1 { color: #333; border-bottom: 2px solid #3F51B5; padding-bottom: 10px; }";
  htmlContent += ".log-entry { background-color: white; padding: 10px; margin: 5px 0; border-radius: 5px; border-left: 4px solid #3F51B5; font-family: monospace; font-size: 13px; }";
  htmlContent += ".back-button { padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; display: inline-block; margin-top: 20px; }";
  htmlContent += "</style></head><body>";
  htmlContent += "<h1>System Log</h1>";
  htmlContent += "<p>Last 25 entries from /Data/systemlog.txt (newest at bottom):</p>";

  if (!SD.exists(filename)) {
    htmlContent += "<p>System log file does not exist yet.</p>";
    htmlContent += "<a href=\"/SDCARD\" class=\"back-button\">Back to SD Card</a>";
    htmlContent += "</body></html>";
    server.send(200, "text/html", htmlContent);
    return;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file) {
    htmlContent += "<p>Could not open system log file.</p>";
    htmlContent += "<a href=\"/SDCARD\" class=\"back-button\">Back to SD Card</a>";
    htmlContent += "</body></html>";
    server.send(200, "text/html", htmlContent);
    return;
  }

  const size_t tailBytes = 4096;
  const size_t fileSize = file.size();
  const size_t startPos = (fileSize > tailBytes) ? (fileSize - tailBytes) : 0;
  file.seek(startPos);

  String chunk;
  chunk.reserve((fileSize > tailBytes) ? tailBytes : fileSize);
  while (file.available()) {
    chunk += (char)file.read();
  }
  file.close();

  if (startPos > 0) {
    const int firstNewline = chunk.indexOf('\n');
    if (firstNewline >= 0) {
      chunk = chunk.substring(firstNewline + 1);
    }
  }

  String lines[25];
  uint8_t lineCount = 0;
  int lineStart = 0;
  while (lineStart < (int)chunk.length()) {
    int lineEnd = chunk.indexOf('\n', lineStart);
    if (lineEnd < 0) lineEnd = chunk.length();
    String line = chunk.substring(lineStart, lineEnd);
    line.trim();
    if (line.length() > 0) {
      if (lineCount < 25) {
        lines[lineCount++] = line;
      } else {
        for (uint8_t i = 0; i < 24; ++i) {
          lines[i] = lines[i + 1];
        }
        lines[24] = line;
      }
    }
    lineStart = lineEnd + 1;
  }

  if (lineCount == 0) {
    htmlContent += "<p>System log file is empty.</p>";
  } else {
    for (uint8_t i = 0; i < lineCount; ++i) {
      // Lines are "utcUnix|description|code" (UTC on disk). Convert time for display.
      String display = lines[i];
      const int p1 = display.indexOf('|');
      if (p1 > 0) {
        String tsField = display.substring(0, p1);
        bool allDigits = tsField.length() > 0;
        for (int c = 0; c < (int)tsField.length() && allDigits; ++c) {
          if (tsField.charAt(c) < '0' || tsField.charAt(c) > '9') allDigits = false;
        }
        if (allDigits) {
          const uint32_t utcTs = (uint32_t)strtoul(tsField.c_str(), nullptr, 10);
          const String localTs = utcTs ? String(dateifyLocal(utcTs, "yyyy-mm-dd hh:nn:ss")) : String("???");
          display = localTs + display.substring(p1);
        }
      }
      htmlContent += "<div class=\"log-entry\">" + htmlEscapeText(display) + "</div>";
    }
    htmlContent += "<p><strong>Entries shown: " + String(lineCount) + "</strong></p>";
  }

  htmlContent += "<a href=\"/SDCARD\" class=\"back-button\">Back to SD Card</a>";
  htmlContent += "</body></html>";
  server.send(200, "text/html", htmlContent);
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/SDCARD");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleERROR_LOG() {
  registerHTTPMessage("ErrorLog");

  #ifdef _USESDCARD
  // Format the content for display
  String htmlContent = "<html><head><title>Error Log</title>";
  htmlContent += "<style>";
  htmlContent += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  htmlContent += "h1 { color: #333; border-bottom: 2px solid #f44336; padding-bottom: 10px; }";
  htmlContent += ".error-entry { background-color: white; padding: 10px; margin: 5px 0; border-radius: 5px; border-left: 4px solid #f44336; }";
  htmlContent += ".error-time { font-weight: bold; color: #f44336; }";
  htmlContent += ".error-code { color: #FF9800; font-family: monospace; }";
  htmlContent += ".error-message { color: #333; margin-top: 5px; }";
  htmlContent += ".back-button { padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; display: inline-block; margin-top: 20px; }";
  htmlContent += "pre { background-color: #f8f8f8; padding: 15px; border-radius: 5px; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }";
  htmlContent += "</style></head><body>";
  htmlContent += "<h1>Device Error Log</h1>";
  htmlContent += "<p>This shows up to the last 25 errors that have occurred on the device.  New errors are on top:</p>";
  

  // Read the file content
  uint8_t entryCount = 0;
  bool entryFound = true;
  ERROR_STRUCT LASTERROR;
  while (entryFound && entryCount < 25) {
    if (readErrorFromSD(LASTERROR, entryCount)) {
      entryCount++;
      htmlContent += "<div class=\"error-entry\">";
      htmlContent += "<span class=\"error-time\">" + (String) dateifyLocal(LASTERROR.errorTime) + "</span><br>";
      htmlContent += "<span class=\"error-code\">Error Code: " + String(LASTERROR.errorCode) + "</span><br>";
      htmlContent += "<div class=\"error-message\">" + (String) LASTERROR.errorMessage + "</div>";
      htmlContent += "</div>";      
    } else {
      entryFound = false;
    }
  }
  
  if (entryCount == 0) {
    htmlContent += "<p>No error entries found in the file.</p>";
  } else {
    htmlContent += "<p><strong>Total entries: " + String(entryCount) + "</strong></p>";
  }
  
  htmlContent += "<a href=\"/STATUS\" class=\"back-button\">Back to Status</a>";
  htmlContent += "</body></html>";
  
  server.send(200, "text/html", htmlContent);
  return;
  #else
  SerialPrint("No SD card found", true);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "No SD card found.");
  return;
  #endif
}

void handleREBOOT_DEBUG() {
  registerHTTPMessage("RebootDebug");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Reboot Debug");
  // Display reboot debug information
  WEBHTML = WEBHTML + "<div style=\"margin: 20px 0; padding: 20px; background-color: #f5f5f5; border-radius: 8px;\">";
  WEBHTML = WEBHTML + "<pre style=\"font-family: monospace; white-space: pre-wrap;\">";
  WEBHTML = WEBHTML + getRebootDebugInfo();
  WEBHTML = WEBHTML + "</pre>";
  WEBHTML = WEBHTML + "</div>";
  
  // Navigation links
  WEBHTML = WEBHTML + "<br><br><div style=\"text-align: center; padding: 20px;\">";
  WEBHTML = WEBHTML + "<h3>Navigation</h3>";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/CONFIG\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px;\">System Config</a> ";
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 10px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a>";
  WEBHTML = WEBHTML + "</div>";
  
  WEBHTML = WEBHTML + "</body></html>";
  
  serverTextClose(200, true);
}


// ==================== DEVICE VIEWER FUNCTIONS (root page) ====================

static void syncDeviceViewerNumberFromIndex() {
  uint8_t n = 0;
  for (int16_t i = 0; i < NUMDEVICES; i++) {
    if (!Sensors.isDeviceInit(i)) continue;
    if (i == CURRENT_DEVICEVIEWER_DEVINDEX) {
      CURRENT_DEVICEVIEWER_DEVNUMBER = n;
      return;
    }
    n++;
  }
}

static constexpr uint16_t LMK_HTTP_MAX_PLAINTEXT = 8192;
static constexpr uint16_t LMK_HTTP_MAX_FRAMED = LMK_HTTP_MAX_PLAINTEXT + 2;
static constexpr uint16_t LMK_HTTP_MAX_PADDED = ((LMK_HTTP_MAX_FRAMED + 15) / 16) * 16;
static constexpr uint16_t LMK_HTTP_MAX_CIPHER = LMK_HTTP_MAX_PADDED + 16;
// Ping JSON + framing fits in ~512 bytes; keep cipher buffers off the main task stack.
static constexpr uint16_t LMK_HTTP_PING_MAX_CIPHER = 640;

static constexpr uint16_t DEVICE_VIEWER_PING_TIMEOUT_MS = 5000;

enum JsonPingReplyVia : uint8_t {
  JSON_PING_REPLY_NONE = 0,
  JSON_PING_REPLY_HTTP_INLINE = 1,
  JSON_PING_REPLY_UDP = 2,
  JSON_PING_REPLY_HTTPS_INLINE = 3,
};

static uint8_t s_jsonPingReplyVia = JSON_PING_REPLY_NONE;
static IPAddress s_jsonPingReplyUdpIp;
static uint8_t s_jsonPingReplyViaDepth = 0;
static uint8_t s_jsonPingReplyViaStack[4];
static IPAddress s_jsonPingReplyUdpIpStack[4];
static String s_lastIncomingHttpMsgType;
static uint16_t s_snsAckWaitId = 0;
static bool s_snsAckOk = false;

static void pushJsonPingReplyContext(uint8_t via, IPAddress udpReplyIp = IPAddress(0, 0, 0, 0)) {
  if (s_jsonPingReplyViaDepth < (uint8_t)(sizeof(s_jsonPingReplyViaStack) / sizeof(s_jsonPingReplyViaStack[0]))) {
    s_jsonPingReplyViaStack[s_jsonPingReplyViaDepth] = via;
    s_jsonPingReplyUdpIpStack[s_jsonPingReplyViaDepth] = udpReplyIp;
    s_jsonPingReplyViaDepth++;
  }
  s_jsonPingReplyVia = via;
  s_jsonPingReplyUdpIp = udpReplyIp;
}

bool isJsonInlineHttpReply() {
  return s_jsonPingReplyVia == JSON_PING_REPLY_HTTP_INLINE
      || s_jsonPingReplyVia == JSON_PING_REPLY_HTTPS_INLINE;
}

static void popJsonPingReplyContext() {
  if (s_jsonPingReplyViaDepth == 0) {
    s_jsonPingReplyVia = JSON_PING_REPLY_NONE;
    return;
  }
  s_jsonPingReplyViaDepth--;
  if (s_jsonPingReplyViaDepth == 0) {
    s_jsonPingReplyVia = JSON_PING_REPLY_NONE;
    return;
  }
  uint8_t prev = s_jsonPingReplyViaDepth - 1;
  s_jsonPingReplyVia = s_jsonPingReplyViaStack[prev];
  s_jsonPingReplyUdpIp = s_jsonPingReplyUdpIpStack[prev];
}

static void truncateMsgTypeToBuffer(const String& msgType, char* out, size_t outLen) {
  if (outLen == 0) return;
  String mt = msgType;
  if (mt.length() == 0) mt = "snsData";
  if (mt.length() >= outLen) mt = mt.substring(0, outLen - 1);
  snprintf(out, outLen, "%s", mt.c_str());
}

static void parseJsonMsgType(const String& postData, char* out, size_t outLen) {
  if (outLen == 0) return;
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, postData) != DeserializationError::Ok) {
    snprintf(out, outLen, "JSON?");
    return;
  }
  truncateMsgTypeToBuffer(String(doc["msgType"] | "snsData"), out, outLen);
}

static void registerHttpMsgTypeFromJson(const String& postData) {
  char msgType[10];
  parseJsonMsgType(postData, msgType, sizeof(msgType));
  registerHTTPMessage(msgType);
}

static void registerUdpMsgTypeFromJson(const String& postData, IPAddress remoteIP) {
  char msgType[10];
  parseJsonMsgType(postData, msgType, sizeof(msgType));
  registerUDPMessage(remoteIP, msgType);
}

static bool jsonPingAckMatches(const String& jsonBody, uint64_t expectedMac) {
  if (jsonBody.length() == 0) return false;
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, jsonBody) != DeserializationError::Ok) return false;
  if (String(doc["msgType"] | "") != "ackPing") return false;
  if (!doc["senderDevice"]["mac"].is<const char*>()) return false;
  uint64_t mac = 0;
  if (!stringToUInt64(doc["senderDevice"]["mac"].as<const char*>(), &mac, true)) return false;
  return mac == expectedMac;
}

static volatile bool s_jsonPingActive = false;
static volatile bool s_jsonPingGotAck = false;
static uint64_t s_jsonPingExpectedMAC = 0;

static void noteJsonPingAck(uint64_t fromMAC) {
  if (!s_jsonPingActive) return;
  if (fromMAC != s_jsonPingExpectedMAC) return;
  s_jsonPingGotAck = true;
}

static bool sendHTTPPingWithInlineAck(ArborysDevType* device, uint32_t* rttMsOut) {
  if (rttMsOut) *rttMsOut = 0;
  if (!device) return false;
  if (!wifiReadyForNetwork()) return false;

  const uint32_t start = millis();
  char jsonBuffer[512];
  JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), true, false);
  if (jsonBuffer[0] == '\0') return false;

  static char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", device->IP.toString().c_str());

  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(jsonBuffer);
  if (!M.initPayload(512)) return false;

  if (!SendHTTPMessage(M)) {
    I.HTTP_OUTGOING_ERRORS++;
    return false;
  }
  registerHTTPSend(device->IP, "pingMsg");

  if (!M.payload || !M.payload.get()) return false;
  const bool ok = jsonPingAckMatches(String(M.payload.get()), device->MAC);
  if (ok && rttMsOut) *rttMsOut = millis() - start;
  return ok;
}

static bool decryptHttpCipherToPlain(const uint8_t* encBuf, uint16_t encLen, String& plainOut) {
  plainOut = "";
  if (!isValidLMKKey()) return false;
  if (encLen < 32 || encLen > LMK_HTTP_MAX_CIPHER || (encLen % 16) != 0) return false;

  uint16_t plainMax = encLen - 16;
  uint8_t* plain = (uint8_t*)malloc(plainMax);
  if (!plain) return false;

  if (BootSecure::decrypt((uint8_t*)encBuf, (char*)Prefs.KEYS.ESPNOW_KEY, plain, encLen, 16) != 1) {
    free(plain);
    return false;
  }

  uint16_t payloadLen = (uint16_t)plain[0] | ((uint16_t)plain[1] << 8);
  if (payloadLen == 0 || payloadLen + 2 > plainMax) {
    free(plain);
    return false;
  }

  plainOut.reserve(payloadLen + 1);
  for (uint16_t i = 0; i < payloadLen; i++) {
    plainOut += (char)plain[i + 2];
  }
  free(plain);
  return true;
}

static size_t readHttpEncResponseBody(HTTPClient& http, uint8_t* out, size_t outMax, uint32_t timeoutMs) {
  if (!out || outMax < 32) return 0;

  EncResponseCollector collector(out, outMax);
  esp_task_wdt_reset();
  int streamed = http.writeToStream(&collector);
  esp_task_wdt_reset();
  if (streamed > 0 && collector.written > 0) {
    return collector.written;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) return 0;

  int respLen = http.getSize();
  if (respLen > 0 && (size_t)respLen <= outMax) {
    size_t got = 0;
    uint32_t start = millis();
    while (got < (size_t)respLen && (millis() - start) < timeoutMs) {
      if (stream->available()) {
        int n = stream->read(out + got, respLen - (int)got);
        if (n > 0) got += (size_t)n;
      } else if (!stream->connected()) {
        break;
      } else {
        delay(1);
      }
    }
    return got;
  }

  size_t got = 0;
  uint32_t idleStart = millis();
  uint32_t start = millis();
  while (got < outMax && (millis() - start) < timeoutMs) {
    if (stream->available()) {
      int n = stream->read(out + got, outMax - got);
      if (n > 0) {
        got += (size_t)n;
        idleStart = millis();
      }
    } else if (!stream->connected()) {
      break;
    } else if (got > 0 && (millis() - idleStart) > 200) {
      break;
    } else {
      delay(1);
    }
  }
  if (got >= 32 && (got % 16) == 0) return got;
  return 0;
}

static bool sendHTTPSPingWithInlineAck(ArborysDevType* device, uint32_t* rttMsOut) {
  if (rttMsOut) *rttMsOut = 0;
  if (!device || !isValidLMKKey()) return false;
  if (!wifiReadyForNetwork()) return false;

  const uint32_t start = millis();
  char jsonBuffer[512];
  JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), false, false);
  if (jsonBuffer[0] == '\0') return false;

  uint16_t payloadLen = (uint16_t)strlen(jsonBuffer);
  const uint16_t framedLen = payloadLen + 2;
  uint8_t* framed = (uint8_t*)malloc(framedLen);
  if (!framed) return false;
  framed[0] = payloadLen & 0xFF;
  framed[1] = (payloadLen >> 8) & 0xFF;
  memcpy(framed + 2, jsonBuffer, payloadLen);

  uint8_t* encBuf = (uint8_t*)malloc(LMK_HTTP_PING_MAX_CIPHER);
  if (!encBuf) {
    free(framed);
    return false;
  }
  uint16_t encLen = 0;
  if (BootSecure::encrypt(framed, framedLen, (char*)Prefs.KEYS.ESPNOW_KEY, encBuf, &encLen, 16) != 1) {
    free(framed);
    free(encBuf);
    return false;
  }
  free(framed);

  static char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST_ENC", device->IP.toString().c_str());

  WiFiClient client;
  HTTPClient http;
  client.setTimeout(20000);
  http.begin(client, urlBuffer);
  http.setTimeout(20000);
  http.addHeader("Content-Type", "application/octet-stream");
  esp_task_wdt_reset();
  int httpCode = http.sendRequest("POST", encBuf, encLen);
  esp_task_wdt_reset();
  free(encBuf);

  bool ok = false;
  if (httpCode >= 200 && httpCode < 400) {
    registerHTTPSend(device->IP, "pingMsg");
    uint8_t* respEnc = (uint8_t*)malloc(LMK_HTTP_PING_MAX_CIPHER);
    if (respEnc) {
      size_t respLen = readHttpEncResponseBody(http, respEnc, LMK_HTTP_PING_MAX_CIPHER, DEVICE_VIEWER_PING_TIMEOUT_MS);
      if (respLen >= 32) {
        String plain;
        if (decryptHttpCipherToPlain(respEnc, (uint16_t)respLen, plain)) {
          ok = jsonPingAckMatches(plain, device->MAC);
        }
      }
      free(respEnc);
    }
  } else {
    I.HTTP_OUTGOING_ERRORS++;
  }

  http.end();
  if (ok && rttMsOut) *rttMsOut = millis() - start;
  return ok;
}

static bool sendBlockingJsonPing(ArborysDevType* device, bool viaHTTP, bool viaHTTPS, uint16_t blockMs, uint32_t* rttMsOut) {
  if (rttMsOut) *rttMsOut = 0;
  if (!device || !device->IsSet) return false;
  if (s_jsonPingActive) return false;
  if (blockMs == 0) blockMs = DEVICE_VIEWER_PING_TIMEOUT_MS;

  #ifdef _USE_HEADER_INFO_ALERT
  char pingBanner[16];
  char shortName[11];
  strncpy(shortName, device->devName, 10);
  shortName[10] = '\0';
  if (shortName[0] == '\0') {
    strncpy(shortName, "device", sizeof(shortName) - 1);
    shortName[sizeof(shortName) - 1] = '\0';
  }
  snprintf(pingBanner, sizeof(pingBanner), "Png %s", shortName);
  HeaderInfoAlertGuard headerAlert(pingBanner, TFT_YELLOW, TFT_BLACK, 60);
  #endif

  if (viaHTTPS) {
    if (!isValidLMKKey()) {
      storeError("Device viewer ping: LMK not configured for HTTPS");
      return false;
    }
    return sendHTTPSPingWithInlineAck(device, rttMsOut);
  }

  if (viaHTTP) {
    return sendHTTPPingWithInlineAck(device, rttMsOut);
  }

  s_jsonPingActive = true;
  s_jsonPingGotAck = false;
  s_jsonPingExpectedMAC = device->MAC;

  IPAddress ip = device->IP;
  int16_t sendResult = sendMSG_ping(ip, false);
  if (sendResult <= 0) {
    s_jsonPingActive = false;
    return false;
  }

  const uint32_t start = millis();
  while ((millis() - start) < blockMs) {
    esp_task_wdt_reset();
    if (s_jsonPingGotAck) {
      s_jsonPingActive = false;
      const uint32_t rttMs = millis() - start;
      if (rttMsOut) *rttMsOut = rttMs;
      SerialPrint("JSON ping ack from " + MACToString(device->MAC) +
          " in " + String(rttMs) + " ms", true);
      return true;
    }
    #ifdef _USEUDP
    delayWithNetwork(1, 1);
    #else
    delay(1);
    #endif
  }

  s_jsonPingActive = false;
  SerialPrint("JSON ping timeout to " + MACToString(device->MAC) +
      " after " + String(blockMs) + " ms", true);
  return false;
}

static void incPingCounter(uint8_t& counter) {
  if (counter < 255) counter++;
}

static void noteDevicePingAttempt(ArborysDevType* device, const char* channel, bool success) {
  if (!device) return;
  if (strcmp(channel, "ArborysMesh") == 0 || strcmp(channel, "ESPNow") == 0) {
    incPingCounter(device->ping_att_ESPNow);
    if (success) incPingCounter(device->ping_success_ESPNow);
  } else if (strcmp(channel, "UDP") == 0) {
    incPingCounter(device->ping_att_UDP);
    if (success) incPingCounter(device->ping_success_UDP);
  } else if (strcmp(channel, "HTTP") == 0) {
    incPingCounter(device->ping_att_HTTP);
    if (success) incPingCounter(device->ping_success_HTTP);
  }
}

static bool performDeviceViewerPing(ArborysDevType* device, const String& protocol, String& protocolLabel, uint32_t& rttMs) {
  rttMs = 0;
  if (!device) return false;

  String proto = protocol;
  proto.toLowerCase();

  if (proto == "esplan" || proto == "arborysmesh") {
    protocolLabel = "ArborysMesh";
    bool ok = meshBlockingAckCheck(device, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
    noteDevicePingAttempt(device, "ArborysMesh", ok);
    return ok;
  }
  if (proto == "udplan") {
    protocolLabel = "UDP";
    // Same mesh ACK_REQ; UDP twin used when espNowFailed inside mesh stack
    bool ok = meshBlockingAckCheck(device, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
    noteDevicePingAttempt(device, "UDP", ok);
    return ok;
  }
  if (proto == "udp") {
    protocolLabel = "UDP";
    bool ok = sendBlockingJsonPing(device, false, false, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
    noteDevicePingAttempt(device, "UDP", ok);
    return ok;
  }
  if (proto == "http") {
    protocolLabel = "HTTP";
    bool ok = sendBlockingJsonPing(device, true, false, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
    noteDevicePingAttempt(device, "HTTP", ok);
    return ok;
  }
  if (proto == "https") {
    protocolLabel = "HTTPS";
    bool ok = sendBlockingJsonPing(device, false, true, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
    noteDevicePingAttempt(device, "HTTP", ok);
    return ok;
  }

  protocolLabel = protocol;
  return false;
}

// Every ~10 minutes: ping each remote device via ESPNow and UDP; HTTP only if both fail.
// Processes one device per call so the main loop is not blocked for long.
void serviceDeviceConnectivityPings(bool startCycle) {
  static constexpr uint16_t METRICS_HTTP_PING_MS = 2000;
  static int16_t s_pingDevIndex = -1; // -1 = idle

  if (startCycle && s_pingDevIndex < 0) {
    s_pingDevIndex = 0;
  }
  if (s_pingDevIndex < 0) return;

  int16_t myIndex = Sensors.findMyDeviceIndex();
  while (s_pingDevIndex < NUMDEVICES) {
    int16_t di = s_pingDevIndex++;
    if (!Sensors.isDeviceInit(di)) continue;
    if (myIndex >= 0 && di == myIndex) continue;

    ArborysDevType* device = Sensors.getDeviceByDevIndex(di);
    if (!device || !device->IsSet) continue;

    esp_task_wdt_reset();
    bool espOk = meshBlockingAckCheck(device, meshParams().ackTimeoutMs, nullptr);
    noteDevicePingAttempt(device, "ArborysMesh", espOk);

    if (!espOk) {
      esp_task_wdt_reset();
      bool httpOk = sendBlockingJsonPing(device, true, false, METRICS_HTTP_PING_MS, nullptr);
      noteDevicePingAttempt(device, "HTTP", httpOk);
    }

    SerialPrint("Connectivity ping " + String(device->devName) +
      " ArborysMesh=" + String(espOk ? "ok" : "fail"), true);
    return; // one device per call
  }

  s_pingDevIndex = -1;
}

static bool sendJsonViaPreferredHttp(IPAddress ip, const char* rawJson, const char* msgType, uint16_t timeoutMs);

#if _IS_SERVER_HUB
// While a peripheral stays expired, ask again. One network attempt per call.
// Critical (bit 7 after override): HTTP/HTTPS every min(3 min, SendingInt).
// Others: UDP every min(5 min, 2×SendingInt), or plain HTTP if today's UDP
// ping fail rate is above 50%. The expired flag itself is the delivery clock.
static uint32_t s_expiryProbeUnix[NUMSENSORS];

static uint32_t criticalRecheckSec(uint32_t sendingInt) {
  if (sendingInt == 0 || sendingInt > 180u) return 180u;
  return sendingInt;
}

static uint32_t noncriticalRecheckSec(uint32_t sendingInt) {
  if (sendingInt == 0) return 300u;
  const uint64_t twice = (uint64_t)sendingInt * 2ull;
  if (twice > 300ull) return 300u;
  return (uint32_t)twice;
}

static bool deviceHasSensorPastExpiry(int16_t devIndex) {
  for (int16_t si = 0; si < NUMSENSORS; ++si) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(si);
    if (!S || !S->IsSet || S->deviceIndex != devIndex) continue;
    if (Sensors.isSensorPastExpiryThreshold(si)) return true;
  }
  return false;
}

static void labelPastThresholdSensorsExpired(int16_t devIndex) {
  ArborysDevType* device = Sensors.getDeviceByDevIndex(devIndex);
  if (!device) return;
  bool any = false;
  for (int16_t si = 0; si < NUMSENSORS; ++si) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(si);
    if (!S || !S->IsSet || S->deviceIndex != devIndex) continue;
    if (!Sensors.isSensorPastExpiryThreshold(si)) {
      S->expired = false;
      s_expiryProbeUnix[si] = 0;
      continue;
    }
    if (!S->expired && Sensors.isSensorFlagBitUsed(si, 7) && I.isExpired < 255) I.isExpired++;
    S->expired = true;
    any = true;
  }
  if (any) device->expired = true;
}

// A probe from the previous delivery does not count toward this miss.
static bool sensorRecheckDue(int16_t si, uint32_t now, bool critical) {
  ArborysSnsType* S = Sensors.snsIndexToPointer(si);
  if (!S || !Sensors.isSensorPastExpiryThreshold(si, (time_t)now)) return false;
  const uint32_t last = s_expiryProbeUnix[si];
  const uint32_t fresh = S->timeLogged;
  if (last == 0 || (fresh != 0 && last <= fresh)) return true;
  const uint32_t gap = critical ? criticalRecheckSec(S->SendingInt) : noncriticalRecheckSec(S->SendingInt);
  return now >= last + gap;
}

static void stampRecheckedSensors(int16_t devIndex, uint32_t now, bool includeCritical, bool includeOther) {
  for (int16_t si = 0; si < NUMSENSORS; ++si) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(si);
    if (!S || !S->IsSet || S->deviceIndex != devIndex) continue;
    if (!Sensors.isSensorPastExpiryThreshold(si, (time_t)now)) {
      s_expiryProbeUnix[si] = 0;
      continue;
    }
    const bool critical = Sensors.isSensorFlagBitUsed(si, 7);
    if ((critical && includeCritical) || (!critical && includeOther)) s_expiryProbeUnix[si] = now;
  }
}

// how: 0 HTTP/HTTPS, 1 UDP, 2 plain HTTP. Stamped by the caller either way.
static void sendExpiredProbe(ArborysDevType* device, uint8_t how) {
  const bool haveNet = wifiReadyForNetwork() && device->IP != IPAddress(0, 0, 0, 0);
  const char* via = "HTTP";
  if (how == 1) via = "UDP";
  else if (how == 0) via = isValidLMKKey() ? "HTTPS" : "HTTP";
  SerialPrint("snsReqExpired to " + String(device->devName) + " via " + String(via), true);
  if (!haveNet) return;

  char jsonBuffer[512];
  jsonBuffer[0] = '\0';
  JSONbuilder_DataRequestMSG(jsonBuffer, sizeof(jsonBuffer), false, -1, true);
  if (jsonBuffer[0] == '\0') return;
  device->dataSent = utcNow();
  esp_task_wdt_reset();
  if (how == 1) {
    sendUDPMessage((uint8_t*)jsonBuffer, device->IP, (uint16_t)strlen(jsonBuffer), "snsReqExpired");
    return;
  }
  if (how == 2) {
    String httpBody = String(jsonBuffer);
    JSONbuilder_encodeHTTP(httpBody);
    IPAddress ip = device->IP;
    sendHTTPJSON(ip, httpBody.c_str(), "snsReqExpired", 2500);
    return;
  }
  sendJsonViaPreferredHttp(device->IP, jsonBuffer, "snsReqExpired", 2500);
}

void serviceExpiredDeviceDataRequests(bool startCycle) {
  (void)startCycle;
  static int16_t s_scanIndex = 0;
  #ifdef _USE_HEADER_INFO_ALERT
  static bool s_headerActive = false;
  #endif

  if (s_scanIndex < 0 || s_scanIndex >= NUMDEVICES) s_scanIndex = 0;

  const int16_t myIndex = Sensors.findMyDeviceIndex();
  const uint32_t now = (uint32_t)utcNow();

  while (s_scanIndex < NUMDEVICES) {
    int16_t di = s_scanIndex++;
    if (di == myIndex) continue;
    ArborysDevType* device = Sensors.getDeviceByDevIndex(di);
    if (!device || !device->IsSet) continue;
    if (IS_SERVER_DEVICE_TYPE(device->devType)) continue;
    if (bitRead(device->Flags, 2)) continue; // low power: labeled on the clock, not asked
    if (Sensors.countSensors(-1, di) == 0) continue;

    if (!deviceHasSensorPastExpiry(di)) {
      for (int16_t si = 0; si < NUMSENSORS; ++si) {
        ArborysSnsType* S = Sensors.snsIndexToPointer(si);
        if (!S || !S->IsSet || S->deviceIndex != di) continue;
        S->expired = false;
        s_expiryProbeUnix[si] = 0;
      }
      device->expired = false;
      continue;
    }

    labelPastThresholdSensorsExpired(di);

    bool criticalDue = false;
    bool otherDue = false;
    for (int16_t si = 0; si < NUMSENSORS; ++si) {
      ArborysSnsType* S = Sensors.snsIndexToPointer(si);
      if (!S || !S->IsSet || S->deviceIndex != di) continue;
      if (!Sensors.isSensorPastExpiryThreshold(si, (time_t)now)) continue;
      if (Sensors.isSensorFlagBitUsed(si, 7)) {
        if (sensorRecheckDue(si, now, true)) criticalDue = true;
      } else if (sensorRecheckDue(si, now, false)) {
        otherDue = true;
      }
    }
    if (!criticalDue && !otherDue) continue;

    #ifdef _USE_HEADER_INFO_ALERT
    {
      char name10[11] = "";
      strncpy(name10, device->devName, 10);
      name10[10] = '\0';
      char banner[24];
      snprintf(banner, sizeof(banner), "req [%s]", name10[0] ? name10 : "device");
      HeaderInfoAlert(banner, TFT_YELLOW, TFT_BLACK, 120);
      s_headerActive = true;
    }
    #endif

    if (criticalDue) {
      // One request asks for every sensor, so both classes wait their next gap.
      sendExpiredProbe(device, 0);
      stampRecheckedSensors(di, now, true, true);
    } else {
      const bool udpWeak = udpPingSuccessRatePercent(device) < 50;
      sendExpiredProbe(device, udpWeak ? 2 : 1);
      stampRecheckedSensors(di, now, false, true);
    }
    return; // one device per call
  }

  #ifdef _USE_HEADER_INFO_ALERT
  if (s_headerActive) {
    HeaderInfoAlert("");
    s_headerActive = false;
  }
  #endif
  s_scanIndex = 0;
}
#endif

static void appendDeviceViewerPingStatus() {
  if (!server.hasArg("ping")) return;

  String pingStatus = server.arg("ping");
  String protocol = server.hasArg("pingproto") ? server.arg("pingproto") : "Unknown";
  String deviceName = server.hasArg("pingdevice") ? server.arg("pingdevice") : "device";

  if (pingStatus == "success") {
    WEBHTML = WEBHTML + "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">";
    WEBHTML = WEBHTML + "<strong>Success:</strong> Ping sent and Ack received from " + deviceName + " over " + protocol + ".";
    if (server.hasArg("pingrtt")) {
      WEBHTML = WEBHTML + " RTT: <strong>" + server.arg("pingrtt") + " ms</strong>.";
    }
    WEBHTML = WEBHTML + "</div>";
  } else if (pingStatus == "failed") {
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
    WEBHTML = WEBHTML + "Ping failed over " + protocol + ".";
    WEBHTML = WEBHTML + "</div>";
  }
}

#if _IS_SERVER_HUB
static void appendDeviceViewerLimitsStatus() {
  if (!server.hasArg("limits")) return;

  const String status = server.arg("limits");
  const String snsName = server.hasArg("limitsensor") ? server.arg("limitsensor") : "sensor";
  if (status == "success") {
    WEBHTML = WEBHTML + "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">";
    WEBHTML = WEBHTML + "<strong>Success:</strong> Limit and timing settings were received by " + snsName + ".";
    WEBHTML = WEBHTML + "</div>";
  } else if (status == "failed") {
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
    WEBHTML = WEBHTML + "<strong>Failed:</strong> Limit and timing settings were not received by " + snsName + ".";
    if (server.hasArg("limiterr")) {
      WEBHTML = WEBHTML + " (" + server.arg("limiterr") + ")";
    }
    WEBHTML = WEBHTML + "</div>";
  }
}
#endif

static void appendDeviceIndexOptions(int16_t selectedIndex) {
  bool deviceFlagged[NUMDEVICES];
  bool deviceExpired[NUMDEVICES];
  collectDeviceViewerNameMarks(deviceFlagged, deviceExpired);
  for (int16_t di = 0; di < NUMDEVICES; di++) {
    if (!Sensors.isDeviceInit(di)) continue;
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d) continue;
    String label = formatDeviceViewerName(d, deviceFlagged[di], deviceExpired[di], true);
    WEBHTML = WEBHTML + "<option value=\"" + String(di) + "\"";
    if (di == selectedIndex) WEBHTML = WEBHTML + " selected";
    WEBHTML = WEBHTML + ">" + label + "</option>";
    serverTextFlush(false);
  }
}

static void appendRemoteDevicePingLinks() {
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=arborysmesh\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">ArborysMesh</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=udplan\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">UDPLAN</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=udp\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #3F51B5; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">UDP</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=http\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">HTTP</a> ";
  WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=https\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #009688; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">HTTPS</a>";
}

void renderDeviceViewerPage() {
    WEBHTML = "";
    serverTextHeader("Main");
    serverTextStreamBegin(200, true);
    appendStandardPageNav();
    serverTextFlush(true);

    // Check for status messages from delete operations
    if (server.hasArg("delete")) {
        String deleteStatus = server.arg("delete");
        if (deleteStatus == "success") {
            String deviceName = server.hasArg("device") ? server.arg("device") : "Unknown";
            String sensorCount = server.hasArg("sensors") ? server.arg("sensors") : "0";
            WEBHTML = WEBHTML + "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Success:</strong> Device '" + deviceName + "' and " + sensorCount + " associated sensors have been deleted.";
            WEBHTML = WEBHTML + "</div>";
            CURRENT_DEVICEVIEWER_DEVINDEX = 0;
            CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
        }
    }

    if (server.hasArg("error")) {
        String errorType = server.arg("error");
        if (errorType == "no_devices") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; color: #856404; padding: 15px; margin: 10px 0; border: 1px solid #ffeaa7; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Warning:</strong> No devices available to ping.";
            WEBHTML = WEBHTML + "</div>";
        }
        else if (errorType == "device_not_found") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Error:</strong> Device not found.";
            WEBHTML = WEBHTML + "</div>";
        }
        else if (errorType == "cannot_delete_myself") {
            WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
            WEBHTML = WEBHTML + "<strong>Error:</strong> Cannot delete this device.";
            WEBHTML = WEBHTML + "</div>";
        }
    }
    serverTextFlush(true);

    #ifndef _USEGSHEET
    #if _IS_SERVER_HUB
    if (Sensors.getNumDevices() > 0) {
        appendAllDevicesFirmwareTable();
    }
    #endif
    #endif

    // Reset to first device if no devices exist
    if (Sensors.getNumDevices() == 0) {
        WEBHTML = WEBHTML + "<div style=\"background-color: #fff3cd; color: #856404; padding: 15px; margin: 10px 0; border: 1px solid #ffeaa7; border-radius: 4px;\">";
        WEBHTML = WEBHTML + "<strong>No devices found.</strong> No devices are currently registered in the system.";
        WEBHTML = WEBHTML + "</div>";
        WEBHTML = WEBHTML + "<br><a href=\"/\" style=\"display: inline-block; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
        WEBHTML = WEBHTML + "</body></html>";
        serverTextClose(200, true);
        return;
    }

    if (server.hasArg("devIndex")) {
        int16_t idx = server.arg("devIndex").toInt();
        if (idx >= 0 && idx < NUMDEVICES && Sensors.isDeviceInit(idx)) {
            CURRENT_DEVICEVIEWER_DEVINDEX = idx;
            syncDeviceViewerNumberFromIndex();
        }
    }

    uint16_t myDeviceIndex = Sensors.findMyDeviceIndex();
    if (myDeviceIndex == (uint16_t)-1) {
        WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
        WEBHTML = WEBHTML + "<strong>Error:</strong> Could not retrieve device information.";
        WEBHTML = WEBHTML + "</div>";
        WEBHTML = WEBHTML + "<br><a href=\"/\" style=\"display: inline-block; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
        WEBHTML = WEBHTML + "</body></html>";
        serverTextClose(200, true);
        return;
    }

    while (Sensors.isDeviceInit(CURRENT_DEVICEVIEWER_DEVINDEX)==false) {
        CURRENT_DEVICEVIEWER_DEVINDEX++;
        CURRENT_DEVICEVIEWER_DEVNUMBER++;
        if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES ) {
          CURRENT_DEVICEVIEWER_DEVINDEX = 0;
          CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
        }
    }

    ArborysDevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICEVIEWER_DEVINDEX);
    if (!device) {
        WEBHTML = WEBHTML + "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
        WEBHTML = WEBHTML + "<strong>Error:</strong> Could not retrieve device information.";
        WEBHTML = WEBHTML + "</div>";
        WEBHTML = WEBHTML + "<br><a href=\"/\" style=\"display: inline-block; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Back to Main</a>";
        WEBHTML = WEBHTML + "</body></html>";
        serverTextClose(200, true);
        return;
    }

    WEBHTML = WEBHTML + "<div style=\"background-color: #e8f5e8; color: #2e7d32; padding: 15px; margin: 10px 0; border: 1px solid #4caf50; border-radius: 4px;\">";
    bool isThisDevice = (CURRENT_DEVICEVIEWER_DEVINDEX == myDeviceIndex);
    WEBHTML = WEBHTML + "<form method=\"GET\" action=\"/\" style=\"margin: 0;\">";
    WEBHTML = WEBHTML + "<span style=\"font-size: 1.17em; font-weight: bold;\">Device </span>";
    WEBHTML = WEBHTML + "<select name=\"devIndex\" onchange=\"this.form.submit()\" style=\"font-size: 1em; padding: 4px 8px; margin: 0 4px; max-width: 70%;\">";
    appendDeviceIndexOptions(CURRENT_DEVICEVIEWER_DEVINDEX);
    WEBHTML = WEBHTML + "</select>";
    WEBHTML = WEBHTML + "<span style=\"font-size: 1.17em; font-weight: bold;\"> of " + String(Sensors.getNumDevices()) + "</span>";
    if (isThisDevice) WEBHTML = WEBHTML + "<span style=\"font-size: 1.17em; font-weight: bold;\"> (this device)</span>";
    WEBHTML = WEBHTML + "</form>";
    WEBHTML = WEBHTML + "<div style=\"margin-top: 10px; text-align: center;\">";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PREV\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">Prev</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_NEXT\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">Next</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_DELETE\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\" onclick=\"return confirm('Are you sure you want to delete this device and all its sensors? This action cannot be undone.');\">Delete</a> ";
    if (!isThisDevice) appendRemoteDevicePingLinks();
    WEBHTML = WEBHTML + "</div>";
    WEBHTML = WEBHTML + "</div>";
    serverTextFlush(true);

    appendDeviceViewerPingStatus();
    #if _IS_SERVER_HUB
    appendDeviceViewerLimitsStatus();
    #endif
    serverTextFlush(true);

    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h4>Device Information</h4>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 30%;\">Device Name:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devName) +  "</td></tr>";
    serverTextFlush(false);
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">MAC Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(device->MAC)) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">IP Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + device->IP.toString() + "\" target=\"_blank\">" + device->IP.toString() + "</a></td></tr>";
    serverTextFlush(false);
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Device Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devType) + "</td></tr>";
#if _IS_SERVER_HUB
    appendDeviceFirmwareInfoRow(device);
    serverTextFlush(false);
    if (isThisDevice) {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Alive Since:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ALIVESINCE ? dateifyLocal(I.ALIVESINCE, "mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
    } else {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Time Data Sent:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->dataSent ? dateifyLocal(device->dataSent, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
      serverTextFlush(false);
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Time Data Received:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->dataReceived ? dateifyLocal(device->dataReceived, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Sending Interval:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->SendingInt) + " seconds</td></tr>";
      serverTextFlush(false);
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Status:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->expired ? "Expired" : "Active") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">ArborysMesh Pings (today):</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->ping_success_ESPNow) + " / " + String(device->ping_att_ESPNow) + " success / attempts</td></tr>";
      serverTextFlush(false);
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Pings (today):</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->ping_success_UDP) + " / " + String(device->ping_att_UDP) + " success / attempts</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Pings (today):</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->ping_success_HTTP) + " / " + String(device->ping_att_HTTP) + " success / attempts</td></tr>";
    }
#else
    if (isThisDevice && I.ALIVESINCE) {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Alive Since:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(dateifyLocal(I.ALIVESINCE, "mm/dd/yyyy hh:nn:ss")) + "</td></tr>";
    }
    if (!isThisDevice) {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Preferred Communications:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(preferredCommunicationsLabel()) + "</td></tr>";
    }
#endif
    WEBHTML = WEBHTML + "</table>";
    WEBHTML = WEBHTML + "</div>";
    serverTextFlush(true);

    uint8_t sensorCount = 0;
    for (int16_t i = 0; i < NUMSENSORS; i++) {
        ArborysSnsType* sensor = Sensors.snsIndexToPointer(i);
        if (sensor && sensor->IsSet && sensor->deviceIndex == CURRENT_DEVICEVIEWER_DEVINDEX
            && sensor->snsID > 0 && sensor->snsType > 0) {
            sensorCount++;
        }
    }

    WEBHTML = WEBHTML + "<div style=\"background-color: #e3f2fd; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #2196f3;\">";
    WEBHTML = WEBHTML + "<h4>Sensors (" + String(sensorCount) + " total)</h4>";
    serverTextFlush(true);

    if (sensorCount == 0) {
        WEBHTML = WEBHTML + "<p>No sensors found for this device.</p>";
    } else {
        WEBHTML = WEBHTML + "<table id=\"DeviceSensors\" style=\"width: 100%; border-collapse: collapse; margin-top: 10px;\">";
        WEBHTML = WEBHTML + "<tr style=\"background-color: #2196f3; color: white;\">";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Sensor</th>";
#if _IS_SERVER_HUB
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">ID</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type Name</th>";
#endif
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Value</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Last Logged</th>";
        #if _IS_SERVER_HUB
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Flags</th>";
        #endif
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Flagged</th>";
        #if _HAS_LOCAL_SENSORS
        if (isThisDevice) {
          WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Critical</th>";
        } else
        #endif
        #if _IS_SERVER_HUB
        {
          WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">EXP</th>";
        }
        #elif _HAS_LOCAL_SENSORS
        {
          WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Critical</th>";
        }
        #endif
#if _IS_SERVER_HUB
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Plot Avg</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Plot Raw</th>";
#endif
        #if _HAS_LOCAL_SENSORS
        if (isThisDevice) {
          WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Config</th>";
        } else
        #endif
        #if _IS_SERVER_HUB
        {
          WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Override Config</th>";
        }
        #elif _HAS_LOCAL_SENSORS
        {
          WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Config</th>";
        }
        #endif
        WEBHTML = WEBHTML + "</tr>";
        serverTextFlush(true);

        for (int16_t i = 0; i < NUMSENSORS; i++) {
            ArborysSnsType* sensor = Sensors.snsIndexToPointer(i);
            if (sensor && sensor->IsSet && sensor->deviceIndex == CURRENT_DEVICEVIEWER_DEVINDEX
                && sensor->snsID > 0 && sensor->snsType > 0) {
                rootTableFill(i);
            }
        }
        WEBHTML = WEBHTML + "</table>";
    }
    WEBHTML = WEBHTML + "</div>";


    WEBHTML = WEBHTML + "</body></html>";
    serverTextClose(200, true);
}

void handleDeviceViewer() {
    registerHTTPMessage("DeviceViewer");
    String loc = "/";
    if (server.args() > 0) {
        loc += "?";
        for (uint8_t i = 0; i < server.args(); i++) {
            if (i > 0) loc += "&";
            loc += server.argName(i) + "=" + server.arg(i);
        }
    }
    server.sendHeader("Location", loc);
    server.send(302, "text/plain", "Redirecting to main page...");
}

void handleDeviceViewerNext() {
    registerHTTPMessage("DVNext");

    if (Sensors.getNumDevices() > 0) {

        CURRENT_DEVICEVIEWER_DEVINDEX++;
        CURRENT_DEVICEVIEWER_DEVNUMBER++;
        if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
          CURRENT_DEVICEVIEWER_DEVINDEX = 0;
          CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
        }
        while (Sensors.isDeviceInit(CURRENT_DEVICEVIEWER_DEVINDEX)==false) {
          CURRENT_DEVICEVIEWER_DEVINDEX++;
          if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES-1) {
            CURRENT_DEVICEVIEWER_DEVINDEX = 0;
            CURRENT_DEVICEVIEWER_DEVNUMBER = 0;
          }
        }
    }

    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Redirecting to next device...");
}

void handleDeviceViewerPrev() {
    registerHTTPMessage("DVPrev");

    if (Sensors.getNumDevices() == 0) {

      server.sendHeader("Location", "/?error=no_devices");
      server.send(302, "text/plain", "No devices available to view.");
    }

    if (CURRENT_DEVICEVIEWER_DEVINDEX == 0) {
      CURRENT_DEVICEVIEWER_DEVINDEX = NUMDEVICES - 1;
      CURRENT_DEVICEVIEWER_DEVNUMBER = Sensors.getNumDevices()-1;
    } else {
      CURRENT_DEVICEVIEWER_DEVINDEX--;
      CURRENT_DEVICEVIEWER_DEVNUMBER--;
    }

    while (Sensors.isDeviceInit(CURRENT_DEVICEVIEWER_DEVINDEX)==false) {
      if (CURRENT_DEVICEVIEWER_DEVINDEX <= 0) {
        CURRENT_DEVICEVIEWER_DEVINDEX = NUMDEVICES - 1;
        CURRENT_DEVICEVIEWER_DEVNUMBER = Sensors.getNumDevices()-1;
      } else {
        CURRENT_DEVICEVIEWER_DEVINDEX--;
      }
    }

    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Redirecting to previous device...");
}

void handleDeviceViewerPing() {
    registerHTTPMessage("DVPing");

    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/?error=no_devices");
        server.send(302, "text/plain", "No devices available to ping.");
        return;
    }

    if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
        CURRENT_DEVICEVIEWER_DEVINDEX = 0;
    }

    ArborysDevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICEVIEWER_DEVINDEX);
    if (!device) {
        server.sendHeader("Location", "/?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }

    int16_t myDeviceIndex = Sensors.findMyDeviceIndex();
    if (myDeviceIndex >= 0 && CURRENT_DEVICEVIEWER_DEVINDEX == (uint16_t)myDeviceIndex) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "Cannot ping this device.");
        return;
    }

    String protocol = server.hasArg("protocol") ? server.arg("protocol") : "esplan";
    String protocolLabel;
    uint32_t rttMs = 0;
    bool success = performDeviceViewerPing(device, protocol, protocolLabel, rttMs);
    if (protocolLabel.length() == 0) protocolLabel = protocol;

    String loc = "/?ping=" + String(success ? "success" : "failed")
        + "&pingproto=" + urlEncode(protocolLabel)
        + "&pingdevice=" + urlEncode(String(device->devName));
    if (success && rttMs > 0) {
      loc += "&pingrtt=" + String(rttMs);
    }

    server.sendHeader("Location", loc);
    server.send(302, "text/plain", success ? "Ping succeeded." : "Ping failed.");
}

void handleDeviceViewerDelete() {
    registerHTTPMessage("DVDelete");

    if (Sensors.getNumDevices() == 0) {
        server.sendHeader("Location", "/?error=no_devices");
        server.send(302, "text/plain", "No devices available to delete.");
        return;
    }

    if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
        CURRENT_DEVICEVIEWER_DEVINDEX = 0;
        server.sendHeader("Location", "/?error=no_devices");
        server.send(302, "text/plain", "Invalid device index.");
        return;
    }

    ArborysDevType* device = Sensors.getDeviceByDevIndex(CURRENT_DEVICEVIEWER_DEVINDEX);
    if (!device) {
        server.sendHeader("Location", "/?error=device_not_found");
        server.send(302, "text/plain", "Device not found.");
        return;
    }

    if (device->MAC == Sensors.getDeviceMACByDevIndex(Sensors.findMyDeviceIndex())) {
      server.sendHeader("Location", "/?error=cannot_delete_myself");
      server.send(302, "text/plain", "Cannot delete this device.");
      return;
    }

    String deviceName = String(device->devName);
    uint8_t deletedSensors = Sensors.initDevice(CURRENT_DEVICEVIEWER_DEVINDEX);

    if (CURRENT_DEVICEVIEWER_DEVINDEX >= NUMDEVICES) {
        CURRENT_DEVICEVIEWER_DEVINDEX = 0;
    }

    if (!device->IsSet) {
        server.sendHeader("Location", "/?delete=success&device=" + deviceName + "&sensors=" + String(deletedSensors));
        server.send(302, "text/plain", "Device and sensors deleted successfully.");
    } else {
        server.sendHeader("Location", "/?error=delete_failed");
        server.send(302, "text/plain", "Failed to delete device.");
    }
}

static String localIpSubnetPrefix() {
  IPAddress ip = WiFi.localIP();
  if (ip == IPAddress(0, 0, 0, 0)) return "";
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
}

static bool parseRegisterTargetIp(const String& raw, IPAddress& outIp, String& errorOut) {
  String trimmed = raw;
  trimmed.trim();
  if (trimmed.length() == 0) {
    errorOut = "IP address is required.";
    return false;
  }
  if (!outIp.fromString(trimmed)) {
    errorOut = "Invalid IP address format.";
    return false;
  }
  if (outIp == IPAddress(0, 0, 0, 0) || outIp == IPAddress(255, 255, 255, 255)) {
    errorOut = "IP address cannot be 0.0.0.0 or broadcast.";
    return false;
  }
  if (outIp == WiFi.localIP()) {
    errorOut = "Cannot register this device's own IP.";
    return false;
  }
  return true;
}

static bool hasFreeDeviceSlot() {
  for (int16_t i = 0; i < NUMDEVICES; i++) {
    if (!Sensors.isDeviceInit(i)) return true;
  }
  return false;
}

// Sends helloPing to target IP. Returns true when an HTTP response body is received (RTT set).
// ackJsonOut holds the raw body for further validation/registration.
static bool sendHelloPingToIp(IPAddress targetIP, String& ackJsonOut, uint32_t* rttMsOut, String& detailOut) {
  ackJsonOut = "";
  detailOut = "";
  if (rttMsOut) *rttMsOut = 0;
  if (!wifiReadyForNetwork()) {
    detailOut = "WiFi not ready.";
    return false;
  }

  const uint32_t start = millis();
  char jsonBuffer[512];
  JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), true, false);
  if (jsonBuffer[0] == '\0') {
    detailOut = "Failed to build helloPing.";
    return false;
  }

  char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", targetIP.toString().c_str());

  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(jsonBuffer);
  if (!M.initPayload(512)) {
    detailOut = "Failed to allocate HTTP payload buffer.";
    return false;
  }

  if (!SendHTTPMessage(M)) {
    I.HTTP_OUTGOING_ERRORS++;
    detailOut = "HTTP request failed (code " + String(M.httpCode) + ").";
    return false;
  }
  registerHTTPSend(targetIP, "pingMsg");

  if (!M.payload || !M.payload.get() || M.payload.get()[0] == '\0') {
    detailOut = "Empty HTTP response.";
    return false;
  }

  ackJsonOut = String(M.payload.get());
  if (rttMsOut) *rttMsOut = millis() - start;
  return true;
}

static String registerDeviceFromAckJson(const String& ackJson, IPAddress targetIP,
    String& outName, uint8_t& outDevType, IPAddress& outIp) {
  outName = "";
  outDevType = 0;
  outIp = targetIP;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, ackJson) != DeserializationError::Ok) {
    return "response error";
  }
  JsonObject sender = doc["senderDevice"];
  if (sender.isNull()) return "response error";

  uint64_t mac = 0;
  if (!sender["mac"].is<JsonVariantConst>() ||
      !stringToUInt64(sender["mac"].as<String>(), &mac, true) || mac == 0) {
    return "response error";
  }

  if (sender["name"].is<JsonVariantConst>()) outName = sender["name"].as<String>();
  if (sender["devType"].is<JsonVariantConst>()) outDevType = sender["devType"];
  if (sender["ip"].is<JsonVariantConst>()) {
    IPAddress parsed;
    if (parsed.fromString(sender["ip"].as<String>())) outIp = parsed;
  }
  if (outIp == IPAddress(0, 0, 0, 0)) outIp = targetIP;

#if _IS_SERVER_HUB
  if (!AggLinks_addDevice(mac, outIp, outName.c_str(), outDevType)) {
#if _HUB_REGISTERED_ONLY
    return "registry full";
#endif
  }
#endif

  FirmwareVersion fw;
  if (sender["firmware"].is<JsonVariantConst>()) {
    parseFirmwareFromJson(sender["firmware"], fw);
  }

  const int16_t existingByMac = Sensors.findDevice(mac);
  if (existingByMac >= 0) {
    Sensors.addDevice(mac, outIp, outName.c_str(), 0, 0, outDevType, &fw);
    ArborysDevType* d = Sensors.getDeviceByDevIndex(existingByMac);
    if (d) noteDevicePingAttempt(d, "HTTP", true);
    return "already known";
  }

#if !_IS_SERVER_HUB
  // Peripherals only store servers (devType 100�150); helloPing was still sent so remote can register us.
  if (IS_PERIPHERAL_DEVICE_TYPE(outDevType)) {
    return "no storage for peripherals";
  }
#endif

  if (!hasFreeDeviceSlot()) {
    return "memory full";
  }

  int16_t idx = Sensors.addDevice(mac, outIp, outName.c_str(), 0, 0, outDevType, &fw);
  if (idx < 0) {
#if !_IS_SERVER_HUB
    return "no storage for peripherals";
#else
    return "memory full";
#endif
  }

  ArborysDevType* d = Sensors.getDeviceByDevIndex(idx);
  if (d) noteDevicePingAttempt(d, "HTTP", true);
  return "registered";
}

static void renderRegisterDevicePage(const String& ipFieldValue,
    bool showResult, bool pingOk, uint32_t rttMs, const String& pingDetail,
    const String& regStatus, const String& respName, uint8_t respType, IPAddress respIp,
    const String& extraHtml = "") {
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Register Device");
  appendStandardPageNav();

  WEBHTML = WEBHTML + "<form method=\"POST\" action=\"/REGISTER_DEVICE\" style=\"max-width: 520px; margin: 20px 0;\">";
  WEBHTML = WEBHTML + "<label for=\"device_ip\" style=\"display: block; font-weight: bold; margin-bottom: 6px;\">Device IP address</label>";
  WEBHTML = WEBHTML + "<input type=\"text\" id=\"device_ip\" name=\"device_ip\" value=\"" + ipFieldValue + "\" "
      "maxlength=\"15\" required "
      "style=\"width: 100%; padding: 10px; border: 1px solid #ccc; border-radius: 4px; font-size: 1.1em;\">";
  WEBHTML = WEBHTML + "<div style=\"margin-top: 12px;\">";
  WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 10px 20px; background-color: #009688; color: white; border: none; border-radius: 4px; font-size: 1em; cursor: pointer;\">Register</button>";
  WEBHTML = WEBHTML + "</div></form>";

  if (showResult) {
    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 20px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h4>Registration Result</h4>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 35%;\">helloPing</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
        String(pingOk ? "success" : "failure") + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Round-trip time</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
        (rttMs > 0 ? String(rttMs) + " ms" : "n/a") + "</td></tr>";
    if (pingDetail.length() > 0) {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Ping detail</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
          pingDetail + "</td></tr>";
    }
    if (pingOk) {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Responder name</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
          (respName.length() ? respName : "(unknown)") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Responder type</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
          String(respType) + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Responder IP</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
          respIp.toString() + "</td></tr>";
    }
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Registration status</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" +
        regStatus + "</td></tr>";
    WEBHTML = WEBHTML + "</table></div>";
  }

  if (extraHtml.length()) WEBHTML += extraHtml;

  serverTextClose(200, true);
}

void handleREGISTER_DEVICE() {
  registerHTTPMessage("RegDev");
  renderRegisterDevicePage(localIpSubnetPrefix(), false, false, 0, "", "", "", 0, IPAddress(0, 0, 0, 0));
}

void handleREGISTER_DEVICE_POST() {
  registerHTTPMessage("RegDevPost");

  String ipRaw = server.hasArg("device_ip") ? server.arg("device_ip") : "";
  IPAddress targetIP;
  String validateError;
  if (!parseRegisterTargetIp(ipRaw, targetIP, validateError)) {
    renderRegisterDevicePage(ipRaw.length() ? ipRaw : localIpSubnetPrefix(), true, false, 0, validateError,
        "n/a", "", 0, IPAddress(0, 0, 0, 0));
    return;
  }

  String ackJson;
  String pingDetail;
  uint32_t rttMs = 0;
  const bool pingOk = sendHelloPingToIp(targetIP, ackJson, &rttMs, pingDetail);

  String respName;
  uint8_t respType = 0;
  IPAddress respIp = targetIP;
  String regStatus;

  if (!pingOk) {
    renderRegisterDevicePage(targetIP.toString(), true, false, rttMs, pingDetail, "n/a", "", 0, targetIP);
    return;
  }

  // Transport succeeded; validate ack JSON and apply local storage policy.
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, ackJson) != DeserializationError::Ok ||
      String(doc["msgType"] | "") != "ackPing" ||
      !doc["senderDevice"].is<JsonObject>()) {
    renderRegisterDevicePage(targetIP.toString(), true, true, rttMs, "Invalid or incomplete ackPing JSON.",
        "response error", "", 0, targetIP);
    return;
  }

  regStatus = registerDeviceFromAckJson(ackJson, targetIP, respName, respType, respIp);
  String extra;
#if _HAS_LOCAL_SENSORS && _IS_SERVER_HUB
  if (regStatus == "registered" || regStatus == "already known") {
    const int16_t idx = Sensors.findDevice(respIp);
    ArborysDevType* registered = Sensors.getDeviceByDevIndex(idx);
    if (registered) sendMSG_DataRequest(registered, -1, true);
    extra = aggregateRegisterNote();
  }
#endif
  renderRegisterDevicePage(targetIP.toString(), true, true, rttMs, "", regStatus, respName, respType, respIp, extra);
}

void setupServerRoutes() {
    // Main routes
    server.on("/", handleRoot);
    server.on("/POST", handlePost);
#ifdef _USE32
    server.on("/POST_ENC", HTTP_POST, handlePostEnc, handlePostEncRaw);
    server.on("/FIRMWARE_ENC", HTTP_POST, handleFirmwareEnc, handleFirmwareEncRaw);
    server.on("/FIRMWARE_BLOCK", HTTP_POST, handleFirmwareBlock);
#if defined(_USESDCARD) && _IS_SERVER_HUB
    server.on("/FIRMWARE_PUT", HTTP_POST, handleFirmwarePut, handleFirmwarePutRaw);
#endif
#else
    server.on("/POST_ENC", HTTP_POST, handlePostEnc);
    server.on("/FIRMWARE_ENC", HTTP_POST, handleFirmwareEnc);
    server.on("/FIRMWARE_BLOCK", HTTP_POST, handleFirmwareBlock);
#if defined(_USESDCARD) && _IS_SERVER_HUB
    server.on("/FIRMWARE_PUT", HTTP_POST, handleFirmwarePut);
#endif
#endif
    server.on("/REQUESTUPDATE", handleREQUESTUPDATE);
    server.on("/CLEARSENSOR", handleCLEARSENSOR);
    server.on("/TIMEUPDATE", handleTIMEUPDATE);
    server.on("/REQUESTWEATHER", handleREQUESTWEATHER);
#ifdef _USEWEATHER
    server.on("/WEATHERPKG", HTTP_GET, handleWEATHERPKG);
#endif
#ifdef _USEWEATHERLITE
    server.on("/WEATHERPKG", HTTP_POST, handleWEATHERPKG, handleWEATHERPKG_raw);
#endif
    server.on("/REBOOT", handleReboot);
    
    server.on("/STATUS", handleSTATUS);
#if _IS_SERVER_HUB
    server.on("/DEVICES", HTTP_GET, handleDEVICES);
#endif
    server.on("/MESH_SETTINGS", HTTP_GET, handleMESH_SETTINGS);
    server.on("/MESH_SETTINGS", HTTP_POST, handleMESH_SETTINGS);
#if _IS_SERVER_HUB
    server.on("/RETRIEVEDATA", handleRETRIEVEDATA);
    server.on("/RETRIEVEDATA_MOVINGAVERAGE", handleRETRIEVEDATA_MOVINGAVERAGE);
#endif
    
    // Configuration routes
    server.on("/CONFIG", HTTP_GET, handleCONFIG);
    server.on("/CONFIG", HTTP_POST, handleCONFIG_POST);
    server.on("/CONFIG_DELETE", HTTP_POST, handleCONFIG_DELETE);
    server.on("/CONFIG_OTA_SWITCH", HTTP_POST, handleCONFIG_OTA_SWITCH);
    #if _IS_SERVER_HUB
    server.on("/SENSOR_OVERRIDE_UPDATE", HTTP_POST, handleSENSOR_OVERRIDE_UPDATE);
    server.on("/SENSOR_LIMITS_UPDATE", HTTP_POST, handleSENSOR_LIMITS_UPDATE);
    #endif
    
    #if _HAS_LOCAL_SENSORS
    // Sensor configuration routes for peripheral devices
    server.on("/SENSOR_UPDATE", HTTP_POST, handleSENSOR_UPDATE_POST);
    server.on("/SENSOR_READ_SEND_NOW", HTTP_POST, handleSENSOR_READ_SEND_NOW);
    server.on("/SENSOR_SETUP", HTTP_GET, handleSensorSetup);
    server.on("/SNS_CALIBRATION", HTTP_POST, handleSNS_CALIBRATION);
#if _IS_SERVER_HUB
    server.on("/AGG_LINKS", HTTP_POST, handleAGG_LINKS);
    server.on("/AGG_PULL", HTTP_POST, handleAGG_PULL);
#endif
    server.on("/SWITCHSTATE", HTTP_GET, handleSWITCHSTATE);
    server.on("/SWITCHSTATE", HTTP_POST, handleSWITCHSTATE_POST);
    #endif
    
    #ifdef _USEGSHEET
    // Google Sheets routes
    server.on("/GSHEET", HTTP_GET, handleGSHEET);
    server.on("/GSHEET", HTTP_POST, handleGSHEET_POST);
    server.on("/GSHEET_UPLOAD_NOW", HTTP_POST, handleGSHEET_UPLOAD_NOW);
    server.on("/GSHEET_SHARE_ALL", HTTP_POST, handleGSHEET_SHARE_ALL);
    server.on("/GSHEET_DELETE_ALL", HTTP_POST, handleGSHEET_DELETE_ALL);
    #endif
    
    // LAN broadcast routes (presence / alive)
    server.on("/REQUEST_BROADCAST", HTTP_POST, handleREQUEST_BROADCAST);
    server.on("/REQUEST_BROADCAST_ESP", HTTP_POST, handleREQUEST_BROADCAST_ESP);
    server.on("/REQUEST_BROADCAST_UDP", HTTP_POST, handleREQUEST_BROADCAST_UDP);
    
    // New API routes for streamlined setup
    server.on("/api/wifi", HTTP_POST, apiConnectToWiFi);
    server.on("/api/wifi-scan", HTTP_GET, apiScanWiFi);
    server.on("/api/clear-wifi", HTTP_POST, apiClearWiFi);
    server.on("/api/location", HTTP_POST, apiLookupLocation);
    server.on("/api/timezone", HTTP_GET, apiDetectTimezone);
    server.on("/api/timezone/dst", HTTP_GET, apiDetectDST);
    server.on("/api/timezone", HTTP_POST, apiSaveTimezone);
    server.on("/api/setup-status", HTTP_GET, apiGetSetupStatus);
    server.on("/InitialSetup", HTTP_GET, handleInitialSetup);
    server.on("/api/complete-setup", HTTP_POST, handleApiCompleteSetup);
    server.on("/api/complete-setup", HTTP_GET, handleApiCompleteSetup);
    server.on("/api/SNS_READ_NOW", HTTP_GET, handleSNS_READ_NOW);
    #if _SUPABASE_RUNTIME
    server.on("/api/arborysnet/claim", HTTP_POST, apiSupabaseClaim);
    server.on("/api/arborysnet/quit", HTTP_POST, apiSupabaseQuit);
    server.on("/api/arborysnet/sites", HTTP_GET, apiSupabaseSites);
    server.on("/api/arborysnet/site", HTTP_POST, apiSupabaseSite);
    server.on("/api/arborysnet/upload", HTTP_POST, apiSupabaseUploadToggle);
    #if _IS_SERVER_HUB
    server.on("/api/arborysnet/site/create", HTTP_POST, apiSupabaseSiteCreate);
    server.on("/api/arborysnet/site/delete", HTTP_POST, apiSupabaseSiteDelete);
    server.on("/api/arborysnet/inventory", HTTP_POST, apiSupabaseInventory);
    server.on("/api/arborysnet/inventory", HTTP_GET, apiSupabaseInventory);
    #endif
    #endif
            
    #ifdef _USEWEATHER
    // Weather routes
    server.on("/WEATHER", HTTP_GET, handleWeather);
    server.on("/WEATHER", HTTP_POST, handleWeather_POST);
    server.on("/WeatherRefresh", HTTP_POST, handleWeatherRefresh);
    server.on("/WeatherZip", HTTP_POST, handleWeatherZip);
    server.on("/WeatherAddress", HTTP_POST, handleWeatherAddress);
    #endif
    
    #ifdef _USESDCARD
    // SD Card routes
    server.on("/SDCARD", HTTP_GET, handleSDCARD);
    server.on("/SDCARD", HTTP_POST, handleSDCARD_DIR);
    server.on("/SDCARD_DOWNLOAD", HTTP_GET, handleSDCARD_DOWNLOAD);
    server.on("/SDCARD_UPLOAD", HTTP_POST, handleSDCARD_UPLOAD, handleSDCARD_UPLOADFile);
    server.on("/SDCARD_DELETE_SENSORS", HTTP_POST, handleSDCARD_DELETE_SENSORS);
    server.on("/SDCARD_STORE_DEVICES", HTTP_POST, handleSDCARD_STORE_DEVICES);
    server.on("/SDCARD_SAVE_SCREENFLAGS", HTTP_POST, handleSDCARD_SAVE_SCREENFLAGS);
    server.on("/SDCARD_SAVE_WEATHERDATA", HTTP_POST, handleSDCARD_SAVE_WEATHERDATA);
    server.on("/SDCARD_SYSTEMLOG", HTTP_GET, handleSDCARD_SYSTEMLOG);
    server.on("/ERROR_LOG", HTTP_GET, handleERROR_LOG);
    #endif
    
    // Device viewer routes
    server.on("/DEVICEVIEWER", HTTP_GET, handleDeviceViewer);
    server.on("/DEVICEVIEWER_NEXT", HTTP_GET, handleDeviceViewerNext);
    server.on("/DEVICEVIEWER_PREV", HTTP_GET, handleDeviceViewerPrev);
    server.on("/DEVICEVIEWER_PING", HTTP_GET, handleDeviceViewerPing);
    server.on("/DEVICEVIEWER_DELETE", HTTP_GET, handleDeviceViewerDelete);

    server.on("/REGISTER_DEVICE", HTTP_GET, handleREGISTER_DEVICE);
    server.on("/REGISTER_DEVICE", HTTP_POST, handleREGISTER_DEVICE_POST);

    // 404 handler
    server.onNotFound(handleNotFound);
}

//___________________START OF JSON___________________
//json builders 
String JSONbuilder_device(ArborysDevType* device) {
  String deviceJSON = "\"senderDevice\":{\"mac\":\"";
  deviceJSON += MACToString(device->MAC, '\0', true);
  deviceJSON += "\",\"ip\":\"";
  deviceJSON += device->IP.toString();
  deviceJSON += "\",\"name\":\"";
  deviceJSON += String(device->devName);
  deviceJSON += "\",\"devType\":";
  deviceJSON += device->devType;
  deviceJSON += ",\"firmware\":";
  deviceJSON += firmwareJsonArray(device->firmware);
  deviceJSON += "}";
  return deviceJSON;
}

String JSONbuilder_sensorData(ArborysSnsType* S) {
  String sensorJSON = "\"sensorData\":" + JSONbuilder_sensorObject(S);
  return sensorJSON;
}

#if _HAS_LOCAL_SENSORS
/** Mirror Prefs alarm band and poll/send intervals onto a local sensor (Prefs remain canonical on peripherals). */
static void syncLocalSensorLimitsFromPrefs(int16_t snsIndex) {
  if (Sensors.isSensorIndexInvalid(snsIndex, false) != 0) return;
  if (!Sensors.isMySensor(snsIndex)) return;
  ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndex);
  if (!S) return;
  const int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(snsIndex);
  if (prefsIndex < 0 || prefsIndex >= _SENSORNUM) return;
  S->limitHigh = (float)Prefs.SNS_LIMIT_MAX[prefsIndex];
  S->limitLow = (float)Prefs.SNS_LIMIT_MIN[prefsIndex];
  S->PollingInt = Prefs.SNS_INTERVAL_POLL[prefsIndex];
  S->SendingInt = Prefs.SNS_INTERVAL_SEND[prefsIndex];
}
#endif

String JSONbuilder_sensorObject(ArborysSnsType* S) {
  if (!S) return "{}";
#if _HAS_LOCAL_SENSORS
  // Peripherals keep canonical limits in Prefs; mirror into the struct for this send.
  syncLocalSensorLimitsFromPrefs(Sensors.findSensorByPointer(S));
#endif

  String sensorJSON = "{\"type\":";
  sensorJSON += S->snsType;
  sensorJSON += ",\"id\":";
  sensorJSON += S->snsID;
  sensorJSON += ",\"name\":\"";
  sensorJSON += String(S->snsName);
  sensorJSON += "\",\"value\":";
  if (isnan(S->snsValue)) sensorJSON += "null";
  else sensorJSON += S->snsValue;
  sensorJSON += ",\"timeRead\":";
  sensorJSON += S->timeRead;
  sensorJSON += ",\"sendingInt\":";
  sensorJSON += S->SendingInt;
  sensorJSON += ",\"pollingInt\":";
  sensorJSON += S->PollingInt;
  sensorJSON += ",\"flags\":";
  sensorJSON += S->Flags;
  sensorJSON += ",\"limitHigh\":";
  if (isnan(S->limitHigh)) sensorJSON += "null";
  else sensorJSON += String(S->limitHigh, 4);
  sensorJSON += ",\"limitLow\":";
  if (isnan(S->limitLow)) sensorJSON += "null";
  else sensorJSON += String(S->limitLow, 4);
  sensorJSON += "}";
  return sensorJSON;
}

void JSONbuilder_sensorMSG(ArborysSnsType* S, char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP) {
  // Build full json sensor type message
  ArborysDevType* device = Sensors.getDeviceByDevIndex(S->deviceIndex);
  if (!device) {
    SerialPrint("JSONbuilder_sensorMSG: Device not found",true);
    storeError("JSONbuilder_sensorMSG: Device not found", ERROR_JSON_PARSE, true);
    return;
  } 

  String tempJSON = "{\"msgType\":\"snsData\"," + JSONbuilder_device(device) + "," + JSONbuilder_sensorData(S) + "}";

   // Build http body if needed
   if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);
   snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());

   return;
}

static String JSONbuildSensorMSGString(const int16_t* snsIndices, uint8_t count, bool forHTTP, bool ackReq, uint16_t ackId) {
  if (!snsIndices || count == 0) {
    return "";
  }

  int16_t mydeviceIndex = Sensors.findMyDeviceIndex();
  ArborysDevType* device = Sensors.getDeviceByDevIndex(mydeviceIndex);
  if (!device) {
    return "";
  }

  String ackPrefix = "";
  if (ackReq) {
    ackPrefix = "\"ackReq\":1,\"ackId\":" + String(ackId) + ",";
  }

  String tempJSON;
  if (count == 1) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndices[0]);
    if (!S) {
      return "";
    }
    tempJSON = "{\"msgType\":\"snsData\"," + ackPrefix + JSONbuilder_device(device) + "," + JSONbuilder_sensorData(S) + "}";
  } else {
    tempJSON.reserve(256 + (size_t)count * 96u);
    tempJSON = "{\"msgType\":\"snsData\"," + ackPrefix + JSONbuilder_device(device) + ",\"sensors\":[";
    bool first = true;
    for (uint8_t i = 0; i < count; ++i) {
      ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndices[i]);
      if (!S) {
        continue;
      }
      if (!first) {
        tempJSON += ",";
      }
      tempJSON += JSONbuilder_sensorObject(S);
      first = false;
    }
    tempJSON += "]}";
  }

  if (forHTTP) {
    JSONbuilder_encodeHTTP(tempJSON);
  }
  return tempJSON;
}

static bool sensorMSGFitsBuffer(const int16_t* snsIndices, uint8_t count, uint16_t jsonBufferSize, bool forHTTP, bool ackReq = false, uint16_t ackId = 0) {
  String tempJSON = JSONbuildSensorMSGString(snsIndices, count, forHTTP, ackReq, ackId);
  return tempJSON.length() > 0 && tempJSON.length() < jsonBufferSize;
}

bool JSONbuilder_sensorMSG_list(const int16_t* snsIndices, uint8_t count, char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP, bool ackReq, uint16_t ackId) {
  if (!snsIndices || count == 0 || !jsonBuffer || jsonBufferSize == 0) {
    return false;
  }

  String tempJSON = JSONbuildSensorMSGString(snsIndices, count, forHTTP, ackReq, ackId);
  if (tempJSON.isEmpty()) {
    SerialPrint("JSONbuilder_sensorMSG_list: buffer empty", true);
    storeError("JSONbuilder_sensorMSG_list: buffer empty", ERROR_JSON_PARSE, true);
    return false;
  }
  if (tempJSON.length() >= jsonBufferSize) {
    SerialPrint("JSONbuilder_sensorMSG_list: Buffer too small for JSON payload", true);
    storeError("JSONbuilder_sensorMSG_list: Buffer too small", ERROR_JSON_PARSE, true);
    return false;
  }

  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());
  return true;
}

void JSONbuilder_sensorMSG_all(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP) {
   
  int16_t mydeviceIndex = Sensors.findMyDeviceIndex();
  ArborysDevType* device = Sensors.getDeviceByDevIndex(mydeviceIndex);
  if (!device) {
    SerialPrint("JSONbuilder_sensorMSG_all: My device not found", true);
    storeError("JSONbuilder_sensorMSG_all: My device not found", ERROR_JSON_PARSE, true);
    return;
  }

  String tempJSON;
  tempJSON.reserve(256);
  tempJSON = "{\"msgType\":\"snsData\"," + JSONbuilder_device(device) + ",\"sensors\":[";

  bool first = true;
  for (int16_t i = 0; i < NUMSENSORS; ++i) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(i);
    if (!S || S->deviceIndex != mydeviceIndex) continue;

    if (!first) tempJSON += ",";
    tempJSON += JSONbuilder_sensorObject(S);
    first = false;
  }

  tempJSON += "]}";

  if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);

  if (tempJSON.isEmpty()) {
    SerialPrint("JSONbuilder_sensorMSG_all: buffer empty", true);
    storeError("JSONbuilder_sensorMSG_all: buffer empty", ERROR_JSON_PARSE, true);
    return;
  }
  if (tempJSON.length() >= jsonBufferSize) {
    SerialPrint("JSONbuilder_sensorMSG_all: Buffer too small for JSON payload", true);
    storeError("JSONbuilder_sensorMSG_all: Buffer too small", ERROR_JSON_PARSE, true);
    return;
  }

  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());
}

void JSONbuilder_DataRequestMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP, int16_t snsIndex, bool expiredRequest) {
  ArborysDevType* device = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  if (!device) {
    SerialPrint("JSONbuilder_DataRequestMSG: My Device not found",true);
    storeError("JSONbuilder_DataRequestMSG: My Device not found", ERROR_JSON_PARSE, true);
    return;
  }

  int16_t snsType = -1;
  int16_t snsID = -1;
  if (snsIndex >= 0) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndex);
    if (!S) {
      SerialPrint("JSONbuilder_DataRequestMSG: Sensor " + String(snsIndex) + " not found",true);
      storeError("JSONbuilder_DataRequestMSG: Sensor " + String(snsIndex) + " not found", ERROR_JSON_PARSE, true);
      return;
    }
    snsType = S->snsType;
    snsID = S->snsID;
  } else {
    //indicate all sensors for that device should be sent
    snsType = -1;
    snsID = -1;
  }

  const char* msgType = expiredRequest ? "snsReqExpired" : "sendSensorDataNow";
  String tempJSON = "{\"msgType\":\"" + String(msgType) + "\",\"snsType\":" + String(snsType) + ",\"snsID\":" + String(snsID) + "," + JSONbuilder_device(device) + "}";

  if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);
  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());

  return;
}

void JSONbuilder_pingMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP, bool isAck) {
  ArborysDevType* device = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  if (!device) {
    SerialPrint("JSONbuilder_pingMSG: Device not found",true);
    storeError("JSONbuilder_pingMSG: Device not found", ERROR_JSON_PARSE, true);
    return;
  }

  String tempJSON = "{\"msgType\":" + String(isAck ? "\"ackPing\"" : "\"helloPing\"") + "," + JSONbuilder_device(device) + "}";

  if (forHTTP) JSONbuilder_encodeHTTP(tempJSON);
  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());

  return;
}

uint16_t JSONbuilder_encodeHTTP(String& jsonBuffer) {
  String encodedJson = urlEncode(jsonBuffer);
  jsonBuffer = "JSON=" + encodedJson;
  return encodedJson.length();
}

//----------------------------- json handlers for receiving data -----------------------------
int16_t processJSONMessage_addDevice(JsonObject root, String& responseMsg);

static void formatMeshParamsJson(char* out, size_t outLen, const ArborysMeshParams& p, int ok) {
  snprintf(out, outLen,
      "{\"msgType\":\"meshGet\",\"ok\":%d,"
      "\"ttlN\":%u,\"ttlC\":%u,\"burst\":%u,"
      "\"gapMin\":%u,\"gapMax\":%u,\"supp\":%u,"
      "\"ackMs\":%u,\"ackR\":%u,"
      "\"rssiW\":%d,\"rssiS\":%d,"
      "\"dlyW\":%u,\"dlyS\":%u,\"jitter\":%u,"
      "\"critMin\":%u,\"critMax\":%u,\"reorder\":%u}",
      ok,
      (unsigned)p.ttlNormal, (unsigned)p.ttlCritical, (unsigned)p.criticalBurstCount,
      (unsigned)p.criticalBurstGapMinMs, (unsigned)p.criticalBurstGapMaxMs, (unsigned)p.relaySuppressCount,
      (unsigned)p.ackTimeoutMs, (unsigned)p.ackRetries,
      (int)p.relayRssiWeakDbm, (int)p.relayRssiStrongDbm,
      (unsigned)p.relayDelayWeakMs, (unsigned)p.relayDelayStrongMs, (unsigned)p.relayJitterMs,
      (unsigned)p.criticalRelayMinMs, (unsigned)p.criticalRelayMaxMs, (unsigned)p.originReorderWindow);
}

static bool jsonFieldInRange(JsonObject o, const char* key, long lo, long hi, long& out) {
  if (!o[key].is<int>()) return false;
  long v = o[key].as<int>();
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}

static bool meshParamsFromJson(JsonObject o, ArborysMeshParams& p) {
  long ttlN, ttlC, burst, gapMin, gapMax, supp, ackMs, ackR;
  long rssiW, rssiS, dlyW, dlyS, jitter, critMin, critMax, reorder;
  if (!jsonFieldInRange(o, "ttlN", 1, 15, ttlN)) return false;
  if (!jsonFieldInRange(o, "ttlC", 1, 15, ttlC)) return false;
  if (!jsonFieldInRange(o, "burst", 1, 8, burst)) return false;
  if (!jsonFieldInRange(o, "gapMin", 0, 1000, gapMin)) return false;
  if (!jsonFieldInRange(o, "gapMax", 0, 1000, gapMax)) return false;
  if (!jsonFieldInRange(o, "supp", 1, 255, supp)) return false;
  if (!jsonFieldInRange(o, "ackMs", 50, 10000, ackMs)) return false;
  if (!jsonFieldInRange(o, "ackR", 0, 8, ackR)) return false;
  if (!jsonFieldInRange(o, "rssiW", -100, -20, rssiW)) return false;
  if (!jsonFieldInRange(o, "rssiS", -100, -20, rssiS)) return false;
  if (!jsonFieldInRange(o, "dlyW", 0, 5000, dlyW)) return false;
  if (!jsonFieldInRange(o, "dlyS", 0, 5000, dlyS)) return false;
  if (!jsonFieldInRange(o, "jitter", 0, 1000, jitter)) return false;
  if (!jsonFieldInRange(o, "critMin", 0, 1000, critMin)) return false;
  if (!jsonFieldInRange(o, "critMax", 0, 1000, critMax)) return false;
  if (!jsonFieldInRange(o, "reorder", 1, 4096, reorder)) return false;
  p.ttlNormal = (uint8_t)ttlN;
  p.ttlCritical = (uint8_t)ttlC;
  p.criticalBurstCount = (uint8_t)burst;
  p.criticalBurstGapMinMs = (uint16_t)gapMin;
  p.criticalBurstGapMaxMs = (uint16_t)gapMax;
  p.relaySuppressCount = (uint8_t)supp;
  p.ackTimeoutMs = (uint16_t)ackMs;
  p.ackRetries = (uint8_t)ackR;
  p.relayRssiWeakDbm = (int8_t)rssiW;
  p.relayRssiStrongDbm = (int8_t)rssiS;
  p.relayDelayWeakMs = (uint16_t)dlyW;
  p.relayDelayStrongMs = (uint16_t)dlyS;
  p.relayJitterMs = (uint16_t)jitter;
  p.criticalRelayMinMs = (uint16_t)critMin;
  p.criticalRelayMaxMs = (uint16_t)critMax;
  p.originReorderWindow = (uint16_t)reorder;
  return meshParamsInRange(p);
}

static void processJSONMessage_meshParams(JsonObject root, String& responseMsg, bool doSet) {
  char json[480];
  int ok = 1;
  if (doSet) {
    ArborysMeshParams incoming;
    if (!meshParamsFromJson(root, incoming)) ok = 0;
    else meshSetParams(incoming);
  }
  formatMeshParamsJson(json, sizeof(json), meshParams(), ok);
  responseMsg = json;
}

int16_t processJSONMessage_addDevice(JsonObject root, String& responseMsg);

static void processJSONMessage_ExpiredDataRequest(JsonObject root, String& responseMsg) {
#ifdef _USELOWPOWER
  (void)root;
  responseMsg = "lowpower";
  return;
#else
  int16_t my = Sensors.findMyDeviceIndex();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(my);
  if (me && bitRead(me->Flags, 2)) {
    responseMsg = "lowpower";
    return;
  }
  int16_t senderIndex = processJSONMessage_addDevice(root, responseMsg);
  uint64_t mac = 0;
  if (senderIndex >= 0) {
    ArborysDevType* sender = Sensors.getDeviceByDevIndex(senderIndex);
    if (sender) mac = sender->MAC;
  }
  noteExpiredDataRequest(mac);
  responseMsg = "OK";
#endif
}

// Copy a JSON string into dest, dropping quotes and newlines so the log line stays one field.
static void copyErrorField(char* dest, size_t destLen, const char* src) {
  if (!dest || destLen == 0) return;
  dest[0] = '\0';
  if (!src) return;
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < destLen; ++i) {
    char c = src[i];
    if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = ' ';
    dest[j++] = c;
  }
  dest[j] = '\0';
}

// Shorter of "dev snsName" and "MAC snsType.snsID". Type is the numeric error code.
static void formatHubErrorMessage(char* out, size_t outLen, uint16_t code,
    const char* dev, const char* sns, const char* mac, int snsType, int snsID, const char* detail) {
  if (!out || outLen == 0) return;
  char byName[72];
  char byId[40];
  if (sns && sns[0]) snprintf(byName, sizeof(byName), "%s %s", dev ? dev : "", sns);
  else snprintf(byName, sizeof(byName), "%s", dev ? dev : "");
  if (mac && mac[0] && snsType >= 0 && snsID >= 0) snprintf(byId, sizeof(byId), "%s %d.%d", mac, snsType, snsID);
  else snprintf(byId, sizeof(byId), "%s", mac ? mac : "");

  const char* source = byName;
  if (byId[0] && (byName[0] == '\0' || strlen(byId) < strlen(byName))) source = byId;
  if (!source[0]) source = "?";

  if (detail && detail[0]) snprintf(out, outLen, "[%u] %s %s", (unsigned)code, source, detail);
  else snprintf(out, outLen, "[%u] %s", (unsigned)code, source);
}

static void processJSONMessage_errorLog(JsonObject root, String& responseMsg) {
#if !_IS_SERVER_HUB
  (void)root;
  responseMsg = "ignored";
  return;
#else
  const uint16_t code = root["code"].is<int>() ? (uint16_t)root["code"].as<int>() : (uint16_t)ERROR_UNDEFINED;
  char dev[31];
  char sns[31];
  char mac[17];
  char detail[100];
  copyErrorField(dev, sizeof(dev), root["dev"].as<const char*>());
  copyErrorField(sns, sizeof(sns), root["sns"].as<const char*>());
  copyErrorField(mac, sizeof(mac), root["mac"].as<const char*>());
  copyErrorField(detail, sizeof(detail), root["detail"].as<const char*>());
  const int snsType = root["snsType"].is<int>() ? root["snsType"].as<int>() : -1;
  const int snsID = root["snsID"].is<int>() ? root["snsID"].as<int>() : -1;

  char msg[100];
  // A peripheral storeError text is already the log line. Other reports get type and source here.
  if (detail[0] == '[') {
    strncpy(msg, detail, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
  } else {
    formatHubErrorMessage(msg, sizeof(msg), code, dev, sns, mac, snsType, snsID, detail);
  }
  storeError(msg, (ERRORCODES)code, true);
  SerialPrint(String("errorLog stored: ") + msg, true);
  responseMsg = "OK";
#endif
}

static void stripAckErrText(char* s, size_t cap) {
  if (!s || cap == 0) return;
  size_t j = 0;
  for (size_t i = 0; s[i] != '\0' && j + 1 < cap; ++i) {
    char c = s[i];
    if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = ' ';
    s[j++] = c;
  }
  s[j] = '\0';
}

static uint16_t ackIdFromRawJson(const String& raw) {
  int p = raw.indexOf("\"ackId\"");
  if (p < 0) return 0;
  p = raw.indexOf(':', p);
  if (p < 0) return 0;
  return (uint16_t)raw.substring(p + 1).toInt();
}

// Hub reply to snsData with ackReq. UDP goes back to the sender. HTTP/HTTPS is the POST body.
// A non-hub stays silent. No reply means the hub did not hear the packet.
static void replySnsDataAck(uint16_t ackId, bool ok, const char* err, String& responseMsg) {
#if !_IS_SERVER_HUB
  (void)ackId;
  (void)ok;
  (void)err;
  (void)responseMsg;
  return;
#else
  char errBuf[48];
  errBuf[0] = '\0';
  if (!ok && err) {
    strncpy(errBuf, err, sizeof(errBuf) - 1);
    errBuf[sizeof(errBuf) - 1] = '\0';
    stripAckErrText(errBuf, sizeof(errBuf));
  }
  char buf[160];
  if (ok) {
    snprintf(buf, sizeof(buf), "{\"msgType\":\"ackSnsData\",\"ackId\":%u,\"ok\":1}", (unsigned)ackId);
  } else {
    snprintf(buf, sizeof(buf), "{\"msgType\":\"ackSnsData\",\"ackId\":%u,\"ok\":0,\"err\":\"%s\"}", (unsigned)ackId, errBuf);
  }
  if (s_jsonPingReplyVia == JSON_PING_REPLY_UDP) {
    if (s_jsonPingReplyUdpIp != IPAddress(0, 0, 0, 0)) {
      sendUDPMessage((uint8_t*)buf, s_jsonPingReplyUdpIp, (uint16_t)strlen(buf), "ackSnsData");
    }
    return;
  }
  if (isJsonInlineHttpReply()) responseMsg = buf;
#endif
}

static void noteSnsDataAck(JsonObject root) {
  if (s_snsAckWaitId == 0) return;
  uint16_t id = (uint16_t)(root["ackId"] | 0);
  if (id != s_snsAckWaitId) return;
  if ((int)(root["ok"] | 0) == 0) return;
  s_snsAckOk = true;
}

//json handlers for receiving data
void processJSONMessage(String& postData, String& responseMsg) {
 //this is called when json data is received.

  SerialPrint("Processing sensor data JSON: " + postData,true); //default to level 0
   //process the sensor data JSON buffer

   StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, postData);
    if (err) {
      SerialPrint("Error deserializing JSON: " + String(err.c_str()),true);
      storeError("Error deserializing JSON: " + String(err.c_str()), ERROR_JSON_PARSE, true);
      responseMsg = "Error deserializing JSON: " + String(err.c_str());
      if (postData.indexOf("\"ackReq\"") >= 0) {
        replySnsDataAck(ackIdFromRawJson(postData), false, responseMsg.c_str(), responseMsg);
      }
      return;
    }

    String msgType = "NOTSET";
  // Backward compatibility: legacy packets have no msgtype
  if (!doc.containsKey("msgType") || doc["msgType"].isNull()) {
    msgType = "snsData";        // legacy ⇒ assume sensor update
  } else {
      msgType = doc["msgType"].as<String>();
  }
  s_lastIncomingHttpMsgType = msgType;
  JsonObject root = doc.as<JsonObject>();   

  if (msgType == "ackSnsData") {
    noteSnsDataAck(root);
    responseMsg = "OK";
  }
  else if (msgType == "snsData") {
    processJSONMessage_sensorData(root, responseMsg);
    if ((int)(root["ackReq"] | 0) != 0) {
      uint16_t ackId = (uint16_t)(root["ackId"] | 0);
      replySnsDataAck(ackId, responseMsg == "OK", responseMsg.c_str(), responseMsg);
    }
  } 
  else if (msgType == "sendSensorDataNow") {
    //we have received a request to send a single sensor
    processJSONMessage_DataRequest(root, responseMsg);
  }
  else if (msgType == "snsReqExpired") {
    processJSONMessage_ExpiredDataRequest(root, responseMsg);
  }
  else if (msgType == "errorLog") {
    processJSONMessage_errorLog(root, responseMsg);
  }
  else if (msgType == "helloPing" || msgType == "ackPing") {
    processJSONMessage_ping(root, responseMsg);
  }
  else if (msgType == "setFlagsReq") {
    //we have received a sensor request
    processJSONMessage_setFlagsReq(root, responseMsg);
  }
  else if (msgType == "setLimits") {
    processJSONMessage_setLimits(root, responseMsg);
  }
  else if (msgType == "FirmwareRequest") {
    processJSONMessage_FirmwareRequest(root, responseMsg);
  }
  else if (msgType == "FirmwareAvailable") {
    processJSONMessage_FirmwareAvailable(root, responseMsg);
  }
  else if (msgType == "FirmwareUnavailable") {
    processJSONMessage_FirmwareUnavailable(root, responseMsg);
  }
  else if (msgType == "FirmwareUpload") {
    processJSONMessage_FirmwareUpload(root, responseMsg);
  }
  else if (msgType == "networkStateReq") {
    processJSONMessage_networkStateReq(root, responseMsg);
  }
  else if (msgType == "alarmsReq") {
    processJSONMessage_alarmsReq(root, responseMsg);
  }
  else if (msgType == "sunReq") {
    processJSONMessage_sunReq(root, responseMsg);
  }
  else if (msgType == "meshGet" || msgType == "meshSet") {
    processJSONMessage_meshParams(root, responseMsg, msgType == "meshSet");
  }
  else if (msgType == "cloudAck") {
#if _IS_SERVER_HUB
    processJSONMessage_cloudAck(root, responseMsg);
#else
    responseMsg = "ignored";
#endif
  }
  else {
    SerialPrint("Unknown message type: " + msgType,true);
    SerialPrint("Erroneous Post data: " + postData,true);
    storeError("Unknown message type: " + msgType, ERROR_JSON_PARSE, true);
    responseMsg = "Unknown message type: " + msgType;
    return;
  }
}


int16_t processJSONMessage_addDevice(JsonObject root, String& responseMsg) {
  IPAddress devIP = IPAddress(0,0,0,0);
  uint64_t devMAC = 0;
  String devName = "Unknown";
  FirmwareVersion devFirmware;
  uint8_t devType = 0;

  int16_t senderIndex = -1;

  if (root["senderDevice"].is<JsonObject>()) {
    JsonObject senderDevice = root["senderDevice"];
    if (senderDevice["ip"].is<JsonVariantConst>())    devIP.fromString(senderDevice["ip"].as<String>());
    bool macParsed = false;
    if (senderDevice["mac"].is<JsonVariantConst>()) {
      macParsed = stringToUInt64(senderDevice["mac"].as<String>(), &devMAC, true);
      if (!macParsed) {
        SerialPrint("Failed to parse MAC address: " + senderDevice["mac"].as<String>(), true);
        responseMsg = "Failed to parse MAC address";
        storeError("Failed to parse MAC address: " + senderDevice["mac"].as<String>(), ERROR_JSON_PARSE, true);
        return -1;
      }
    } else {
      SerialPrint("Missing MAC address in senderDevice", true);
      responseMsg = "Missing MAC address in senderDevice";
      storeError("Missing MAC address in senderDevice", ERROR_JSON_PARSE, true);
      return -1;
    }
    if (senderDevice["name"].is<JsonVariantConst>())    devName = senderDevice["name"].as<String>();
    if (senderDevice["devType"].is<JsonVariantConst>())    devType = senderDevice["devType"];
    if (senderDevice["firmware"].is<JsonVariantConst>()) {
      parseFirmwareFromJson(senderDevice["firmware"], devFirmware);
    }
    //register the device sending me the request
    senderIndex = Sensors.addDevice(devMAC, devIP, devName.c_str(), 0, 0, devType, &devFirmware);
    noteServerHeard(devType);
  } else {
    SerialPrint("Missing senderDevice in JSON message", true);
    responseMsg = "Missing senderDevice in JSON message";
    storeError("Missing senderDevice in JSON message", ERROR_JSON_PARSE, true);
    return -1;
  }

  if (senderIndex < 0) {
    SerialPrint("Failed to register sender device: MAC=" + String(devMAC, HEX) + " Name=" + devName, true);
    responseMsg = "Failed to register sender device";
    storeError("Failed to register sender device: " + devName + " MAC=" + String(devMAC, HEX), ERROR_JSON_PARSE, true);
  }

  return senderIndex;

}


// Servers learned from the startup UDP presence exchange. A failed mesh ACK to any of
// them sticks sensor sends to UDP until the next boot. Midnight does not clear it.
static constexpr uint32_t BOOT_UDP_PRESENCE_WAIT_MS = 3000;
static uint8_t s_bootUdpPhase = 0; // 0 not sent, 1 collecting replies, 2 decided
static uint32_t s_bootUdpDeadlineMs = 0;
static bool s_udpSensorsUntilBoot = false;
static bool s_bootUdpServer[NUMDEVICES] = {};

static void noteUdpBootRegisteredServer(int16_t senderIndex) {
  if (s_bootUdpPhase != 1) return;
  if (s_jsonPingReplyVia != JSON_PING_REPLY_UDP) return;
  if (senderIndex < 0 || senderIndex >= NUMDEVICES) return;
  ArborysDevType* d = Sensors.getDeviceByDevIndex(senderIndex);
  if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) return;
  if (s_bootUdpServer[senderIndex]) return;
  s_bootUdpServer[senderIndex] = true;
  SerialPrint("Boot UDP: registered server " + String(d->devName), true);
}

void processJSONMessage_ping(JsonObject root, String& responseMsg) {
  responseMsg = "OK";

  String msgType = root["msgType"] | "";
  int16_t senderIndex = processJSONMessage_addDevice(root, responseMsg);

  if (senderIndex == -1) {
    return;
  }
  noteUdpBootRegisteredServer(senderIndex);

  if (msgType == "ackPing") {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(senderIndex);
    if (d) noteJsonPingAck(d->MAC);
    return;
  }

  if (msgType == "helloPing") {
    char jsonBuffer[512];
    JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), false, true);
    if (jsonBuffer[0] == '\0') {
      responseMsg = "Failed to build ping ack";
      storeError("Failed to build ping ack", ERROR_JSON_PARSE, true);
      return;
    }

    if (s_jsonPingReplyVia == JSON_PING_REPLY_HTTP_INLINE || s_jsonPingReplyVia == JSON_PING_REPLY_HTTPS_INLINE) {
      responseMsg = jsonBuffer;
      return;
    }

    if (s_jsonPingReplyVia == JSON_PING_REPLY_UDP) {
      if (!sendUDPMessage((uint8_t*)jsonBuffer, s_jsonPingReplyUdpIp, strlen(jsonBuffer), "pingAck")) {
        responseMsg = "Failed to return UDP ping";
        storeError("Failed to return UDP ping", ERROR_JSON_PARSE, true);
      }
      return;
    }
  }
  return;
}


struct PendingDataRequest {
  bool pending = false;
  int16_t senderIndex = -1;
  int16_t snsIndex = -1;
};
static PendingDataRequest s_pendingDataRequest;

static void queueDeferredDataRequest(int16_t senderIndex, int16_t snsIndex) {
  s_pendingDataRequest.senderIndex = senderIndex;
  s_pendingDataRequest.snsIndex = snsIndex;
  s_pendingDataRequest.pending = true;
}

void processJSONMessage_DataRequest(JsonObject root, String& responseMsg) {
  responseMsg = "OK";
  

  int16_t senderIndex = processJSONMessage_addDevice(root, responseMsg);
  if (senderIndex <0) {
    return;
  }

  int16_t snsType = root["snsType"].as<int16_t>();
  int16_t snsID = root["snsID"].as<int16_t>();


  if (snsType >= 0 && snsID >= 0) {
    int16_t sensorIndex = Sensors.findSensor(ESP.getEfuseMac(), snsType, snsID);
    if (sensorIndex >= 0) {
      queueDeferredDataRequest(senderIndex, sensorIndex);
    } else {
      SerialPrint("Sensor not found",true);
      responseMsg = "Sensor not found";
      storeError("Sensor not found", ERROR_JSON_PARSE, true);
      return;
    }
  } else {
    queueDeferredDataRequest(senderIndex, -1);
  }
  return;
}



void processJSONMessage_sensorData(JsonObject root, String& responseMsg) {
#if !_IS_SERVER_HUB
  processJSONMessage_addDevice(root, responseMsg);
  responseMsg = "OK";
  return;
#endif

  int16_t deviceIndex = processJSONMessage_addDevice(root, responseMsg);
  if (deviceIndex < 0) {
    SerialPrint("Failed to register sender device",true);
    responseMsg = "Failed to register sender device";
    storeError("snsData failed: " + responseMsg, ERROR_JSON_PARSE, true);
    return;
  }
  
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) {
    responseMsg = "Sender device not found";
    storeError("Sender device not found while processing sensor data", ERROR_JSON_PARSE, true);
    return;
  }

  
  responseMsg = "OK";

  // Legacy payload: single sensor object
  if (root["sensorData"].is<JsonObject>()) {
    JsonObject sensor = root["sensorData"];
    handleSingleSensor(d, sensor, responseMsg);
    return;
  }

  // New payload: array of sensors
  if (root["sensors"].is<JsonArray>()) {
      JsonArray sensors = root["sensors"];
      for (JsonObject sensor : sensors) {
          if (responseMsg != "OK") break;     // stop on first error
          handleSingleSensor(d, sensor, responseMsg);
      }
      return;
  }

  responseMsg = "Invalid JSON message format - missing sensor data";
}

void processJSONMessage_setFlagsReq(JsonObject root, String& responseMsg) {
  #if _HAS_LOCAL_SENSORS
  //attempt to register the sender device, not an error if it fails
  int16_t deviceIndex = processJSONMessage_addDevice(root, responseMsg);

  //check the message for flags keyword
  if (!root.containsKey("flags")) {
    responseMsg = "Flag set: Missing flags keyword";
    storeError("Flag set: Missing flags keyword", ERROR_JSON_PARSE, true);
    return;
  }
  //get the flags from the request
  uint8_t flags = root["flags"].as<uint8_t>();

  //check the message for the toDevice keyword
  if (!root.containsKey("toMAC")) {
    responseMsg = "Flag set: Missing toMAC keyword";
    storeError("Flag set: Missing toMAC keyword", ERROR_JSON_PARSE, true);
    return;
  }
  //get the toMAC from the request
  uint64_t toMAC = root["toMAC"].as<uint64_t>();
  //check if the toMAC is valid, and MY mac address
  if (toMAC != ESP.getEfuseMac()) {
    responseMsg = "Flag set: Not my MAC address";
    storeError("Flag set: Not my MAC address", ERROR_JSON_PARSE, true);
    return;
  }

  //check if there is a sensor Type in the message
  if (!root.containsKey("sensorType")) {
    responseMsg = "Flag set: Missing sensorType keyword";
    storeError("Flag set: Missing sensorType keyword", ERROR_JSON_PARSE, true);
    return;
  }
  //get the sensorType from the request
  uint8_t sensorType = root["sensorType"].as<uint8_t>();
  //check if the sensorType is valid
  if (sensorType > 255) {
    responseMsg = "Flag set: Invalid sensorType";
    storeError("Flag set: Invalid sensorType", ERROR_JSON_PARSE, true);
    return;
  }

  //check if there is a sensor ID in the message
  if (!root.containsKey("sensorID")) {
    responseMsg = "Flag set: Missing sensorID keyword";
    storeError("Flag set: Missing sensorID keyword", ERROR_JSON_PARSE, true);
    return;
  }
  //get the sensorID from the request
  uint8_t sensorID = root["sensorID"].as<uint8_t>();
  //check if the sensorID is valid
  if (sensorID > 255) {
    responseMsg = "Flag set: Invalid sensorID";
    storeError("Flag set: Invalid sensorID", ERROR_JSON_PARSE, true);
    return;
  }

  //now update the flags for the sensor
  int16_t sensorIndex = Sensors.findSensor(toMAC, sensorType, sensorID);
  if (Sensors.isSensorIndexInvalid(sensorIndex, false) > 0) {
    responseMsg = "Flag set: Sensor not found";
    storeError("Flag set: Sensor not found", ERROR_JSON_PARSE, true);
    return;
  }
  ArborysSnsType* S = Sensors.snsIndexToPointer(sensorIndex);
  if (!S) {
    responseMsg = "Flag set: Sensor not found";
    storeError("Flag set: Sensor not found", ERROR_JSON_PARSE, true);
    return;
  }
  S->Flags = flags;
  //now read and send this sensor
  SendData(sensorIndex, true, -1, true); //force send data to sensorindex using UDP

  #endif
  responseMsg = "OK";
  return;
}

void processJSONMessage_setLimits(JsonObject root, String& responseMsg) {
#if _HAS_LOCAL_SENSORS
  processJSONMessage_addDevice(root, responseMsg);

  if (!root.containsKey("toMAC")) {
    responseMsg = "Limit set: Missing toMAC";
    storeError("Limit set: Missing toMAC", ERROR_JSON_PARSE, true);
    return;
  }
  uint64_t toMAC = 0;
  if (!stringToUInt64(root["toMAC"].as<String>(), &toMAC, true) || toMAC == 0) {
    responseMsg = "Limit set: Invalid toMAC";
    storeError("Limit set: Invalid toMAC", ERROR_JSON_PARSE, true);
    return;
  }
  if (toMAC != ESP.getEfuseMac()) {
    responseMsg = "Limit set: Not my MAC address";
    storeError("Limit set: Not my MAC address", ERROR_JSON_PARSE, true);
    return;
  }

  if (!root.containsKey("snsType") || !root.containsKey("snsID")) {
    responseMsg = "Limit set: Missing snsType or snsID";
    storeError("Limit set: Missing snsType or snsID", ERROR_JSON_PARSE, true);
    return;
  }
  if (!root.containsKey("limitHigh") || !root.containsKey("limitLow") ||
      root["limitHigh"].isNull() || root["limitLow"].isNull()) {
    responseMsg = "Limit set: Missing limitHigh or limitLow";
    storeError("Limit set: Missing limitHigh or limitLow", ERROR_JSON_PARSE, true);
    return;
  }

  const uint8_t snsType = root["snsType"].as<uint8_t>();
  const uint8_t snsID = root["snsID"].as<uint8_t>();
  float limitHigh = root["limitHigh"].as<float>();
  float limitLow = root["limitLow"].as<float>();
  if (isnan(limitHigh) || isnan(limitLow) || isinf(limitHigh) || isinf(limitLow)) {
    responseMsg = "Limit set: Invalid limit values";
    storeError("Limit set: Invalid limit values", ERROR_JSON_PARSE, true);
    return;
  }

  const int16_t sensorIndex = Sensors.findSensor(toMAC, snsType, snsID);
  if (Sensors.isSensorIndexInvalid(sensorIndex, false) != 0) {
    responseMsg = "Limit set: Sensor not found";
    storeError("Limit set: Sensor not found", ERROR_JSON_PARSE, true);
    return;
  }
  ArborysSnsType* S = Sensors.snsIndexToPointer(sensorIndex);
  if (!S || !Sensors.isMySensor(sensorIndex)) {
    responseMsg = "Limit set: Sensor not found";
    storeError("Limit set: Sensor not found", ERROR_JSON_PARSE, true);
    return;
  }

  const int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(sensorIndex);
  if (prefsIndex < 0 || prefsIndex >= _SENSORNUM) {
    responseMsg = "Limit set: Prefs index not found";
    storeError("Limit set: Prefs index not found", ERROR_JSON_PARSE, true);
    return;
  }

  uint32_t pollingInt = 0;
  uint32_t sendingInt = 0;
  bool havePoll = false;
  bool haveSend = false;
  if (root.containsKey("pollingInt") && !root["pollingInt"].isNull()) {
    pollingInt = root["pollingInt"].as<uint32_t>();
    if (pollingInt < 1 || pollingInt > 65535) {
      responseMsg = "Limit set: Invalid interval values";
      storeError("Limit set: Invalid interval values", ERROR_JSON_PARSE, true);
      return;
    }
    havePoll = true;
  }
  if (root.containsKey("sendingInt") && !root["sendingInt"].isNull()) {
    sendingInt = root["sendingInt"].as<uint32_t>();
    if (sendingInt > 65535) {
      responseMsg = "Limit set: Invalid interval values";
      storeError("Limit set: Invalid interval values", ERROR_JSON_PARSE, true);
      return;
    }
    haveSend = true;
  }

  Prefs.SNS_LIMIT_MAX[prefsIndex] = limitHigh;
  Prefs.SNS_LIMIT_MIN[prefsIndex] = limitLow;
  if (IS_INTERRUPT_SENSOR_TYPE(S->snsType) &&
      normalizeHumanPresenceLimits(Prefs.SNS_LIMIT_MAX[prefsIndex], Prefs.SNS_LIMIT_MIN[prefsIndex])) {
    limitHigh = Prefs.SNS_LIMIT_MAX[prefsIndex];
    limitLow = Prefs.SNS_LIMIT_MIN[prefsIndex];
  }
  if (havePoll) {
    Prefs.SNS_INTERVAL_POLL[prefsIndex] = (uint16_t)pollingInt;
    S->PollingInt = pollingInt;
  }
  if (haveSend) {
    Prefs.SNS_INTERVAL_SEND[prefsIndex] = (uint16_t)sendingInt;
    S->SendingInt = sendingInt;
  }
  Prefs.isUpToDate = false;

  const uint8_t lastflag = S->Flags;
  S->limitHigh = limitHigh;
  S->limitLow = limitLow;
  applyAlarmFlags(S, limitHigh, limitLow, lastflag);
  I.isUpToDate = false;

#ifdef _USE_HEADER_INFO_ALERT
  showSensorLanMsgBanner(S->snsName);
#endif

  SerialPrint("Limit set: " + String(S->snsName) + " high=" + String(limitHigh, 4) +
      " low=" + String(limitLow, 4) + " poll=" + String(S->PollingInt) +
      " send=" + String(S->SendingInt), true);
  responseMsg = "OK";
  return;
#else
  (void)root;
  responseMsg = "Limit set: no local sensors";
  return;
#endif
}

#if _IS_SERVER_HUB
void processJSONMessage_cloudAck(JsonObject root, String& responseMsg) {
  // Peer hub notifies which sensors were uploaded: update timeCloudUpload; stub only if unknown.
  responseMsg = "OK";
  JsonArray arr = root["s"].as<JsonArray>();
  if (arr.isNull()) {
    responseMsg = "cloudAck: missing s";
    return;
  }

  for (JsonObject item : arr) {
    uint64_t mac = 0;
    if (!item["m"].is<JsonVariantConst>() ||
        !stringToUInt64(item["m"].as<String>(), &mac, true) || mac == 0) {
      continue;
    }
    const uint8_t snsType = (uint8_t)(item["t"] | 0);
    const uint8_t snsId = (uint8_t)(item["i"] | 0);
    const uint32_t tcu = (uint32_t)(item["u"] | 0);
    if (!snsType || !tcu) continue;

    IPAddress ip(0, 0, 0, 0);
    if (item["p"].is<JsonVariantConst>()) {
      ip.fromString(item["p"].as<String>());
    }

    int16_t di = Sensors.findDevice(mac);
    const bool deviceMissing = (di < 0);
    // Known device: never re-stub (avoids overwriting a real name with "?").
    if (deviceMissing) {
      di = Sensors.addDevice(mac, ip, "?", 300, 0, 0);
      if (di < 0) continue;
    }
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d) continue;
    if (ip != IPAddress(0, 0, 0, 0) && d->IP == IPAddress(0, 0, 0, 0)) {
      d->IP = ip;
    }

    int16_t si = Sensors.findSensor(mac, snsType, snsId);
    const bool sensorMissing = (si < 0);
    if (sensorMissing) {
      // Sensor stub only; use existing device name (or "?" for brand-new device stubs).
      si = Sensors.addSensor(mac, ip, snsType, snsId, "", 0.0, 0, 0, 300, 0,
                             d->devName[0] ? d->devName : "?",
                             d->devType);
    }
    if (si < 0) continue;
    ArborysSnsType* s = Sensors.snsIndexToPointer(si);
    if (!s) continue;
    // Never invent a newer timeRead; only advance cloud upload stamp.
    if (tcu > s->timeCloudUpload) {
      s->timeCloudUpload = tcu;
    }

    if ((deviceMissing || sensorMissing) && d->IP != IPAddress(0, 0, 0, 0)) {
      const bool useUdp = deviceUdpPingRateAbove50(d);
      sendMSG_DataRequest(d, -1, !useUdp);
    }
  }
  noteServerHeard(100); // peer hub message
}
#endif

void processJSONMessage_sunReq(JsonObject root, String& responseMsg) {
  (void)root;
#if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
  if (WeatherData.sunrise >= (uint32_t)TIMEZERO && WeatherData.sunset >= (uint32_t)TIMEZERO) {
    responseMsg = "{\"msgType\":\"sunAck\",\"sunrise\":";
    responseMsg += String(WeatherData.sunrise);
    responseMsg += ",\"sunset\":";
    responseMsg += String(WeatherData.sunset);
    responseMsg += '}';
    return;
  }
#endif
  responseMsg = "{\"msgType\":\"sunAck\",\"error\":\"noSun\"}";
}

void processJSONMessage_networkStateReq(JsonObject root, String& responseMsg) {
  (void)root;

#if !_IS_SERVER_HUB
  responseMsg = "{\"msgType\":\"networkState\",\"error\":\"notServer\"}";
  return;
#else
  uint8_t serverCount = 0;
  uint8_t peripheralCount = 0;
  String serverIPs;
  serverIPs.reserve(320);

  for (int16_t i = 0; i < NUMDEVICES; i++) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!d || !d->IsSet) continue;
    if (IS_SERVER_DEVICE_TYPE(d->devType)) {
      if (d->IP == IPAddress(0, 0, 0, 0)) continue;
      if (serverCount > 0) serverIPs += ',';
      serverIPs += '"';
      serverIPs += d->IP.toString();
      serverIPs += '"';
      serverCount++;
    } else {
      peripheralCount++;
    }
  }

  responseMsg.reserve(64 + serverIPs.length());
  responseMsg = "{\"msgType\":\"networkState\",\"serverCount\":";
  responseMsg += String(serverCount);
  responseMsg += ",\"serverIPs\":[";
  responseMsg += serverIPs;
  responseMsg += "],\"peripheralCount\":";
  responseMsg += String(peripheralCount);
  responseMsg += ",\"timestamp\":";
  responseMsg += String((uint32_t)utcNow());
  responseMsg += '}';
#endif
}

// Keep alarmsAck under LMK plaintext budget (~8 KB) when returned via POST_ENC.
static constexpr uint16_t ALARMS_ACK_MAX_JSON_BYTES = 7000;

static void appendAlarmedSensorJson(String& out, int16_t snsIndex, ArborysSnsType* S, ArborysDevType* D) {
  // Limits always come from ArborysSnsType:
  // - local sensors: refresh from Prefs first (canonical on the owning device)
  // - remote sensors (hub): use values stored from inbound JSON snsData (NaN if never received)
#if _HAS_LOCAL_SENSORS
  syncLocalSensorLimitsFromPrefs(snsIndex);
#endif

  out += "{\"name\":\"";
  out += String(S->snsName);
  out += "\",\"mac\":\"";
  out += MACToString(D->MAC, '\0', true);
  out += "\",\"type\":";
  out += String(S->snsType);
  out += ",\"id\":";
  out += String(S->snsID);
  out += ",\"value\":";
  if (isnan(S->snsValue)) out += "null";
  else out += String(S->snsValue, 4);
  out += ",\"limitHigh\":";
  if (isnan(S->limitHigh)) out += "null";
  else out += String(S->limitHigh, 4);
  out += ",\"limitLow\":";
  if (isnan(S->limitLow)) out += "null";
  else out += String(S->limitLow, 4);
  out += ",\"timeLogged\":";
  out += String((uint32_t)S->timeLogged);
  out += ",\"expired\":";
  out += S->expired ? "true" : "false";
  out += ",\"flags\":";
  out += String(S->Flags);
  out += '}';
}

void processJSONMessage_alarmsReq(JsonObject root, String& responseMsg) {
  // Register requester when present (same pattern as other JSON handlers).
  processJSONMessage_addDevice(root, responseMsg);

  ArborysDevType* me = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  if (!me) {
    responseMsg = "{\"msgType\":\"alarmsAck\",\"error\":\"noSelfDevice\"}";
    return;
  }

  const bool isHub = (_IS_SERVER_HUB != 0);
  String sensorsJson;
  sensorsJson.reserve(1024);
  sensorsJson = '[';

  uint16_t count = 0;
  bool truncated = false;
  bool first = true;
  // Leave headroom for msgType + senderDevice + count/truncated (+ optional error) wrapper.
  const uint16_t sensorsBudget = ALARMS_ACK_MAX_JSON_BYTES > 450
      ? (uint16_t)(ALARMS_ACK_MAX_JSON_BYTES - 450) : 512;

  for (int16_t i = 0; i < NUMSENSORS; ++i) {
    // Hubs: all alarmed sensors in the network DB. Peripherals: own sensors only.
    if (!isHub && !Sensors.isMySensor(i)) continue;
    if (!Sensors.matchesMainScreenAlert(i, true)) continue;
    ArborysSnsType* S = Sensors.snsIndexToPointer(i);
    ArborysDevType* D = Sensors.getDeviceBySnsIndex(i);
    if (!S || !D || !S->IsSet || !D->IsSet) continue;

    const unsigned int mark = sensorsJson.length();
    if (!first) sensorsJson += ',';
    appendAlarmedSensorJson(sensorsJson, i, S, D);
    if (sensorsJson.length() > sensorsBudget) {
      sensorsJson.remove(mark);
      truncated = true;
      break;
    }
    first = false;
    count++;
  }
  sensorsJson += ']';

  responseMsg.reserve(sensorsJson.length() + 288);
  responseMsg = "{\"msgType\":\"alarmsAck\",";
  responseMsg += JSONbuilder_device(me);
  if (!isHub) {
    responseMsg += ",\"error\":\"notServer\"";
  }
  responseMsg += ",\"count\":";
  responseMsg += String(count);
  responseMsg += ",\"truncated\":";
  responseMsg += truncated ? "true" : "false";
  responseMsg += ",\"sensors\":";
  responseMsg += sensorsJson;
  responseMsg += '}';

  SerialPrint(String("alarmsReq: returning ") + String(count) + " alarmed sensor(s)"
      + (isHub ? "" : " (peripheral/notServer)")
      + (truncated ? " (truncated)" : ""), true);
}

void handleSingleSensor(ArborysDevType* dev, JsonObject sensor, String& responseMsg) {
  if (!sensor.containsKey("type") || !sensor.containsKey("id") || !sensor.containsKey("name") || !sensor.containsKey("value")) {
      responseMsg = "Invalid sensor entry";
      return;
  }

  uint8_t snsType = sensor["type"];
  uint8_t snsID   = sensor["id"];
  String snsName  = sensor["name"].as<String>();
  double value = NAN;
  if (sensor.containsKey("value")) {
    (sensor["value"].isNull()) ? value = NAN : value =  sensor["value"].as<double>();
  }
  uint32_t timeRead   = sensor["timeRead"]   | (uint32_t)utcNow();
  uint32_t sendingInt = sensor["sendingInt"] | 3600;
  uint8_t flags       = sensor["flags"]      | 0;

  // Limits: key present → update (null clears to NaN). Missing key → leave prior value.
  const bool updateLimitHigh = sensor.containsKey("limitHigh");
  const bool updateLimitLow = sensor.containsKey("limitLow");
  float limitHigh = NAN;
  float limitLow = NAN;
  if (updateLimitHigh && !sensor["limitHigh"].isNull()) {
    limitHigh = sensor["limitHigh"].as<float>();
  }
  if (updateLimitLow && !sensor["limitLow"].isNull()) {
    limitLow = sensor["limitLow"].as<float>();
  }

  uint8_t ret = registerSensorData(
      dev->MAC, dev->IP, dev->devName, dev->devType, dev->Flags,
      snsType, snsID, snsName, value, timeRead, (uint32_t)utcNow(), sendingInt, flags,
      limitHigh, limitLow, updateLimitHigh, updateLimitLow
  );

  if (ret == 0) {
    responseMsg = "Failed to add sensor";
    return;
  }

  if (sensor.containsKey("pollingInt") && !sensor["pollingInt"].isNull()) {
    const uint32_t pollingInt = sensor["pollingInt"].as<uint32_t>();
    const int16_t si = Sensors.findSensor(dev->MAC, snsType, snsID);
    ArborysSnsType* S = Sensors.snsIndexToPointer(si);
    if (S) S->PollingInt = pollingInt;
  }

}

 uint8_t registerSensorData(uint64_t deviceMAC, IPAddress deviceIP, String devName, uint8_t devType, uint8_t devFlags, uint8_t snsType, uint8_t snsID, String snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, uint32_t sendingInt, uint8_t flags, float limitHigh, float limitLow, bool updateLimitHigh, bool updateLimitLow) {
   //returns 0 if failed to add sensor, 1 if sensor was added, 2 if sensor was already in the database and is updated
 
   uint8_t ret = 0;
   bool addToSD = false;
   int16_t sensorIndex = -1;
   if (deviceMAC!=0 && snsType!=0 ) {
     if (devName.length() == 0) devName = "Unknown";

#if _IS_SERVER_HUB
     // Peripheral climate I2C failure sentinel (-999) ? store NaN and log.
     if (snsValue <= -999.0 && snsValue > -10000.0 &&
         (Sensors.isSensorOfType(snsType, "temperature") ||
          Sensors.isSensorOfType(snsType, "humidity") ||
          Sensors.isSensorOfType(snsType, "pressure"))) {
       const String label = snsName.length() ? snsName : (String("sns ") + String(snsType) + "." + String(snsID));
       storeError(label + " has no I2c", ERROR_SENSOR_INVALID, true);
       SerialPrint(label + " has no I2c (value -999 ? NaN)", true);
       snsValue = NAN;
     }
#endif
 
     //is this sensor already in the database?
     sensorIndex = Sensors.findSensor(deviceMAC, snsType, snsID);
     if (sensorIndex >= 0) {
       ret = 2; //sensor was found in the database and is updated
       if (Sensors.getSensorFlag(sensorIndex) != flags) {
         SerialPrint("Sensor found, flags changed, adding to SD",true);
         addToSD = true;
       }
     } else {
       ret =1; //sensor was not found in the database and is added
       SerialPrint("Sensor not found, adding to Devices_Sensors class",true);
       addToSD = true;
     }   

     //is this a low power sensor?
     if (bitRead(flags,2) == 1) {
      ArborysSnsType* OldS = Sensors.snsIndexToPointer(Sensors.findSensor(deviceMAC, snsType, snsID));
      // low power sensors do not know if they switched flag status... get the last reading, which has not yet been updated
      if (OldS) {        
        if (OldS->Flags != flags && (bitRead(flags,0) !=  bitRead(flags,0))) { //flag status changed
          bitWrite(flags,6,1); //flag changed since last read
        }          
      }
     }

     // Add sensor to Devices_Sensors class
     sensorIndex = Sensors.addSensor(deviceMAC, deviceIP, snsType, snsID, 
                                           snsName.c_str(), snsValue, 
                                           timeRead, timeLogged, sendingInt, flags, devName.c_str(), devType,
                                           -9999, -9999, limitHigh, limitLow, updateLimitHigh, updateLimitLow);
 
     if (sensorIndex < 0) {
       SerialPrint("Failed to add sensor",true);
       return 0; //failed to add sensor
     } 
   }
 
   #ifdef _USESDCARD
   if (sensorIndex >= 0 && addToSD) {
     //message was successfully added to Devices_Sensors class
     //save to SD card if addToSD is true [flag was changed]. Otherwise it will save hourly
     if (storeSensorDataSD(sensorIndex)) {
       SerialPrint("Individual sensor data saved to SD: " + snsName + " (index " + String(sensorIndex) + ")", true);
     } else {
       SerialPrint("Warning: Failed to save individual sensor data to SD: " + snsName + " (index " + String(sensorIndex) + ")", true);
     }
   }
   #endif
  

  
   return ret;
 }

 //___________________END OF JSON handlers___________________
 
 void handlePost() {
    String responseMsg = "OK";

    if (server.hasArg("JSON")) {
        String postData = server.arg("JSON");
        registerHttpMsgTypeFromJson(postData);
        pushJsonPingReplyContext(JSON_PING_REPLY_HTTP_INLINE);
        processJSONMessage(postData, responseMsg);
        popJsonPingReplyContext();
    } else {
      registerHTTPMessage("POST");
      SerialPrint("No JSON data received",true);
      responseMsg = "No JSON data received";
    }

    if (responseMsg.startsWith("{")) {
      server.send(200, "application/json", responseMsg);
    } else if (responseMsg == "OK") {
      server.send(200, "text/plain", responseMsg);
    } else {
      SerialPrint("Failed to add sensor with response message: " + responseMsg + "\n");
      server.send(500, "text/plain", responseMsg);
    }
    return;
 
 }

static bool decryptAndProcessHttpPayload(uint8_t* encBuf, uint16_t encLen, String& responseMsg) {
  if (!isValidLMKKey()) {
    responseMsg = "LMK not configured";
    return false;
  }
  if (encLen < 32 || encLen > LMK_HTTP_MAX_CIPHER || (encLen % 16) != 0) {
    responseMsg = "Invalid cipher length";
    return false;
  }

  uint16_t plainMax = encLen - 16;
  uint8_t* plain = (uint8_t*)malloc(plainMax);
  if (!plain) {
    responseMsg = "Out of memory";
    return false;
  }

  if (BootSecure::decrypt(encBuf, (char*)Prefs.KEYS.ESPNOW_KEY, plain, encLen, 16) != 1) {
    free(plain);
    responseMsg = "Decrypt failed";
    return false;
  }

  uint16_t payloadLen = (uint16_t)plain[0] | ((uint16_t)plain[1] << 8);
  if (payloadLen == 0 || payloadLen + 2 > plainMax) {
    free(plain);
    responseMsg = "Invalid payload length";
    return false;
  }

  uint8_t* payload = plain + 2;
  if (payload[0] == '{' || payload[0] == '[') {
    String json;
    json.reserve(payloadLen + 1);
    for (uint16_t i = 0; i < payloadLen; i++) {
      json += (char)payload[i];
    }
    registerHttpMsgTypeFromJson(json);
    processJSONMessage(json, responseMsg);
  } else {
    SerialPrint("Encrypted HTTP binary payload received: " + String(payloadLen) + " bytes", true);
    responseMsg = "OK";
  }

  free(plain);
  return true;
}

static bool serverSendEncryptedPlaintext(const String& plaintext) {
  if (!isValidLMKKey()) return false;
  uint16_t payloadLen = (uint16_t)plaintext.length();
  if (payloadLen == 0 || payloadLen > LMK_HTTP_MAX_PLAINTEXT) return false;

  const uint16_t framedLen = payloadLen + 2;
  uint8_t* framed = (uint8_t*)malloc(framedLen);
  if (!framed) return false;
  framed[0] = payloadLen & 0xFF;
  framed[1] = (payloadLen >> 8) & 0xFF;
  memcpy(framed + 2, plaintext.c_str(), payloadLen);

  uint8_t* encBuf = (uint8_t*)malloc(LMK_HTTP_MAX_CIPHER);
  if (!encBuf) {
    free(framed);
    return false;
  }

  uint16_t encLen = 0;
  if (BootSecure::encrypt(framed, framedLen, (char*)Prefs.KEYS.ESPNOW_KEY, encBuf, &encLen, 16) != 1) {
    free(framed);
    free(encBuf);
    return false;
  }
  free(framed);

  server.setContentLength(encLen);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char*)encBuf, encLen);
  server.client().flush();
  free(encBuf);
  return true;
}

#ifdef _USE32
struct BinaryPostRawBody {
  uint8_t* data = nullptr;
  size_t len = 0;
  size_t cap = 0;
};

static BinaryPostRawBody s_binaryPostRawBody;

static void resetBinaryPostRawBody(BinaryPostRawBody& body) {
  if (body.data) free(body.data);
  body.data = nullptr;
  body.len = 0;
  body.cap = 0;
}

void absorbBinaryPostRaw(size_t maxLen) {
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START) {
    resetBinaryPostRawBody(s_binaryPostRawBody);
    int cl = server.clientContentLength();
    if (cl < 1 || (size_t)cl > maxLen) return;
    s_binaryPostRawBody.cap = (size_t)cl;
    s_binaryPostRawBody.data = (uint8_t*)malloc(s_binaryPostRawBody.cap);
    if (!s_binaryPostRawBody.data) s_binaryPostRawBody.cap = 0;
    return;
  }
  if (raw.status == RAW_WRITE) {
    if (!s_binaryPostRawBody.data || s_binaryPostRawBody.len + raw.currentSize > s_binaryPostRawBody.cap) return;
    memcpy(s_binaryPostRawBody.data + s_binaryPostRawBody.len, raw.buf, raw.currentSize);
    s_binaryPostRawBody.len += raw.currentSize;
    return;
  }
  if (raw.status == RAW_ABORTED) {
    resetBinaryPostRawBody(s_binaryPostRawBody);
  }
}

bool takeBinaryPostBody(uint8_t** out, size_t* outLen, size_t minLen, size_t maxLen) {
  *out = s_binaryPostRawBody.data;
  *outLen = s_binaryPostRawBody.len;
  s_binaryPostRawBody.data = nullptr;
  s_binaryPostRawBody.len = 0;
  s_binaryPostRawBody.cap = 0;
  if (!*out || *outLen < minLen || *outLen > maxLen) {
    if (*out) free(*out);
    *out = nullptr;
    *outLen = 0;
    return false;
  }
  return true;
}

void handlePostEncRaw() {
  absorbBinaryPostRaw(LMK_HTTP_MAX_CIPHER);
}
#endif

#ifndef _USE32
static bool readLegacyBinaryPostBody(uint8_t** out, size_t* outLen, size_t minLen, size_t maxLen, uint32_t timeoutMs) {
  *out = nullptr;
  *outLen = 0;
  size_t encLen = 0;
  if (server.hasHeader("Content-Length")) {
    encLen = (size_t)server.header("Content-Length").toInt();
  }
  if (encLen < minLen || encLen > maxLen) return false;

  uint8_t* encBuf = (uint8_t*)malloc(encLen);
  if (!encBuf) return false;

  WiFiClient client = server.client();
  size_t got = 0;
  uint32_t startMs = millis();
  while (got < encLen && (millis() - startMs) < timeoutMs) {
    int avail = client.available();
    if (avail > 0) {
      int chunk = client.read(encBuf + got, encLen - got);
      if (chunk > 0) got += (size_t)chunk;
    } else {
      delay(1);
    }
  }
  if (got != encLen) {
    free(encBuf);
    return false;
  }
  *out = encBuf;
  *outLen = encLen;
  return true;
}
#endif

void handlePostEnc() {
  String responseMsg = "OK";
  s_lastIncomingHttpMsgType = "";

  uint8_t* encBuf = nullptr;
  size_t encLen = 0;
#ifdef _USE32
  if (!takeBinaryPostBody(&encBuf, &encLen, 32, LMK_HTTP_MAX_CIPHER)) {
#else
  if (!readLegacyBinaryPostBody(&encBuf, &encLen, 32, LMK_HTTP_MAX_CIPHER, 5000)) {
#endif
    registerHTTPMessage("badReq");
    server.send(400, "text/plain", "Invalid or missing body");
    return;
  }

  pushJsonPingReplyContext(JSON_PING_REPLY_HTTPS_INLINE);
  if (!decryptAndProcessHttpPayload(encBuf, (uint16_t)encLen, responseMsg)) {
    popJsonPingReplyContext();
    free(encBuf);
    if (s_lastIncomingHttpMsgType.length() == 0) {
      registerHTTPMessage("decFail");
    }
    server.send(500, "text/plain", responseMsg);
    return;
  }
  popJsonPingReplyContext();

  free(encBuf);

  // POST_ENC ping: always return encrypted ackPing inline (do not rely on ping reply context).
  if (s_lastIncomingHttpMsgType == "helloPing") {
    char jsonBuffer[512];
    JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), false, true);
    if (jsonBuffer[0] == '\0') {
      server.send(500, "text/plain", "Failed to build ping ack");
      return;
    }
    if (serverSendEncryptedPlaintext(String(jsonBuffer))) {
      return;
    }
    server.send(500, "text/plain", "Encrypt failed");
    return;
  }

  if (responseMsg.startsWith("{")) {
    if (!serverSendEncryptedPlaintext(responseMsg)) {
      server.send(500, "text/plain", "Encrypt failed");
    }
  } else if (responseMsg == "OK") {
    server.send(200, "text/plain", responseMsg);
  } else {
    server.send(500, "text/plain", responseMsg);
  }
}
 


//handlers for sending data
bool checkThisSensorTime(ArborysSnsType* Si) {
  // return true if it is time to send this sensor
  if (!Si || Si->deviceIndex != I.MY_DEVICE_INDEX) return false;
#if _HAS_LOCAL_SENSORS
  // Do not send the registration 0 or a boot read that produced no sample.
  if (!localSensorReadyToSend(Si)) return false;
#endif
  const bool monitored = bitRead(Si->Flags, 1);
  const bool critical = bitRead(Si->Flags, 7);
  const bool changed = bitRead(Si->Flags, 6) == 1;
#if _HAS_LOCAL_SENSORS
  // A critical limit cross does not wait for SendingInt, including interrupt rate limits.
  if (critical && criticalLimitCrossPending(Si)) return true;
#endif
#if _USEINTERRUPT
  if (IS_INTERRUPT_SENSOR_TYPE(Si->snsType)) {
    // Unmonitored: bit 6 is only a bounds or expiry edge (activity does not latch it).
    if (!monitored && critical && changed) return true;
    const time_t now = utcNow();
    if (Si->SendingInt > 0 && Si->timeLogged != 0 && now >= (time_t)Si->timeLogged
        && (uint32_t)(now - (time_t)Si->timeLogged) < Si->SendingInt) {
      return false; // max send rate even if interrupts keep firing
    }
    if (!monitored) return false;
    if (changed) return true;
    if (Si->timeLogged == 0 && Si->timeRead != 0) return true;
    if (Si->SendingInt == 0) return false;
    return true;
  }
#endif
  // Bit 7 latches bit 6 only for a bounds cross or an expiry-status change,
  // in either direction. That send does not wait for the interval.
  if (critical && changed) return true;
  // Bit 1: interval, and any other normal send (interrupt, actuator, first read).
  // A bounds or expiry edge with bit 7 clear does not latch bit 6.
  if (!monitored) return false;
  if (changed) return true;
  if (Si->SendingInt == 0) {
    // Event-only: also send once after the first successful read so hubs can register the sensor.
    if (Si->timeLogged == 0 && Si->timeRead != 0) return true;
    return false;
  }
  if (Si->timeLogged != 0 && Si->timeLogged < utcNow() && utcNow() - Si->timeLogged < 60*60*24) {
    if (Si->timeLogged + Si->SendingInt > utcNow()) return false; //not time
  }
  return true;
}

bool isSensorSendTime(int16_t snsIndex) {
  // When snsIndex == -1, check all sensors and return true if ANY sensor is due
  if (snsIndex < 0) {
    for (int16_t i = 0; i < NUMSENSORS; i++) {
      ArborysSnsType* Si = Sensors.snsIndexToPointer(i);
      if (checkThisSensorTime(Si)) return true;
    }
    return false;
  }

  // Single sensor path
  ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndex);
  if (checkThisSensorTime(S)) return true;
  return false;
}

bool isDeviceSendTime(ArborysDevType* D, bool forceSend) {
  
  //basic check - does this device exist?
  if (!D || !D->IsSet) return false;
  if (D->IP==WiFi.localIP()) return false; //do not send to myself
  if (forceSend) return true;
  if (!IS_SERVER_DEVICE_TYPE(D->devType)) return false; //not a server

  return true;
}

//___________________START OF HTTP SEND HANDLERS___________________

void wrapupSendData(ArborysSnsType* S) {
  if (!S) {
    //special case, all sensors sent
    for (int16_t i = 0; i < NUMSENSORS; i++) {
      ArborysSnsType* S = Sensors.snsIndexToPointer(i);
      if (!S) continue;
      if (S->deviceIndex != I.MY_DEVICE_INDEX) continue; //don't send others sensors
      bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I sent the value so this is the new baseline. Set bit 6 (change in flag) to zero
#if _HAS_LOCAL_SENSORS
      clearMonitoredLimitCross(i);
      clearCriticalLimitCross(i);
#endif
      S->timeLogged = utcNow();
      // Leave expired as-is. Clearing it here looks like an expired → fresh
      // edge and would send a critical sensor again on the next pass.
    }
    return;
  }
  bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I sent the value so this is the new baseline. Set bit 6 (change in flag) to zero
#if _HAS_LOCAL_SENSORS
  {
    const int16_t sentIndex = Sensors.findSensor(S->deviceIndex, S->snsType, S->snsID);
    clearMonitoredLimitCross(sentIndex);
    clearCriticalLimitCross(sentIndex);
  }
#endif
  S->timeLogged = utcNow();
}

void wrapupSendDataList(const int16_t* snsIndices, uint8_t count) {
  if (!snsIndices) {
    return;
  }
  for (uint8_t i = 0; i < count; ++i) {
    wrapupSendData(Sensors.snsIndexToPointer(snsIndices[i]));
  }
}

int16_t sendHTTPJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType, uint16_t timeoutMs) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002; //device not found
  return sendHTTPJSON(d->IP, jsonBuffer, msgType, timeoutMs);
}

int16_t sendHTTPJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType, uint16_t timeoutMs) {
  //http method

  if (!wifiReadyForNetwork()) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1001; //not connected to wifi
  }

  static char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", ip.toString().c_str());

  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(jsonBuffer);
  if (timeoutMs > 0) M.timeout = timeoutMs;
  if (SendHTTPMessage(M)) {
    registerHTTPSend(ip, msgType);
    return M.httpCode;
  }
  I.HTTP_OUTGOING_ERRORS++;
  return -1000; //failed to send message
}

static int16_t sendHTTPEncryptedPayload(IPAddress& ip, const uint8_t* payload, uint16_t payloadLen, const char* msgType, uint16_t timeoutMs = 0, String* replyOut = nullptr) {
  if (!payload || payloadLen == 0) return -1002;
  if (payloadLen > LMK_HTTP_MAX_PLAINTEXT) return -1005;
  if (!isValidLMKKey()) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1003;
  }
  if (!wifiReadyForNetwork()) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1001;
  }

  const uint16_t framedLen = payloadLen + 2;
  uint8_t* framed = (uint8_t*)malloc(framedLen);
  if (!framed) return -1006;
  framed[0] = payloadLen & 0xFF;
  framed[1] = (payloadLen >> 8) & 0xFF;
  memcpy(framed + 2, payload, payloadLen);

  uint8_t* encBuf = (uint8_t*)malloc(LMK_HTTP_MAX_CIPHER);
  if (!encBuf) {
    free(framed);
    return -1006;
  }

  uint16_t encLen = 0;
  if (BootSecure::encrypt(framed, framedLen, (char*)Prefs.KEYS.ESPNOW_KEY, encBuf, &encLen, 16) != 1) {
    free(framed);
    free(encBuf);
    I.HTTP_OUTGOING_ERRORS++;
    return -1004;
  }
  free(framed);

  static char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST_ENC", ip.toString().c_str());

  const uint32_t clientTimeoutMs = (timeoutMs > 0) ? timeoutMs : 20000;

  WiFiClient client;
  HTTPClient http;
  client.setTimeout(clientTimeoutMs);
  http.begin(client, urlBuffer);
  http.setTimeout(clientTimeoutMs);
  http.addHeader("Content-Type", "application/octet-stream");
  esp_task_wdt_reset();
  int httpCode = http.POST(encBuf, encLen);
  esp_task_wdt_reset();

  if (httpCode >= 200 && httpCode < 400) {
    registerHTTPSend(ip, msgType);
    if (replyOut) {
      *replyOut = "";
      uint8_t* respEnc = (uint8_t*)malloc(512);
      if (respEnc) {
        size_t respLen = readHttpEncResponseBody(http, respEnc, 512, clientTimeoutMs);
        if (respLen >= 32 && respLen <= 512) {
          decryptHttpCipherToPlain(respEnc, (uint16_t)respLen, *replyOut);
        }
        free(respEnc);
      }
    }
    http.end();
    free(encBuf);
    return httpCode;
  }

  String errDetail;
  WiFiClient* stream = http.getStreamPtr();
  if (stream && stream->available() > 0) {
    char errBuf[64];
    int n = stream->readBytes(errBuf, sizeof(errBuf) - 1);
    if (n > 0) {
      errBuf[n] = '\0';
      errDetail = String(errBuf);
    }
  }
  http.end();
  free(encBuf);
  I.HTTP_OUTGOING_ERRORS++;
  if (errDetail.length() > 0) {
    storeError("POST_ENC to " + ip.toString() + " httpCode=" + String(httpCode) + " " + errDetail, ERROR_HTTP_RESPONSE, true);
  }
  return (int16_t)httpCode;
}

int16_t sendHTTPSJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType, uint16_t timeoutMs) {
  if (!jsonBuffer) return -1002;
  uint16_t len = (uint16_t)strlen(jsonBuffer);
  return sendHTTPEncryptedPayload(ip, (const uint8_t*)jsonBuffer, len, msgType, timeoutMs);
}

int16_t sendHTTPSJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType, uint16_t timeoutMs) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002;
  return sendHTTPSJSON(d->IP, jsonBuffer, msgType, timeoutMs);
}

int16_t sendHTTPSBinary(IPAddress& ip, const uint8_t* data, uint16_t dataLen, const char* msgType, uint16_t timeoutMs) {
  return sendHTTPEncryptedPayload(ip, data, dataLen, msgType, timeoutMs);
}

int16_t sendHTTPSBinary(int16_t deviceIndex, const uint8_t* data, uint16_t dataLen, const char* msgType, uint16_t timeoutMs) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002;
  return sendHTTPSBinary(d->IP, data, dataLen, msgType, timeoutMs);
}

// Prefer encrypted POST_ENC when LMK is available; otherwise (or on failure) plain HTTP.
static bool sendJsonViaPreferredHttp(IPAddress ip, const char* rawJson, const char* msgType, uint16_t timeoutMs = 0) {
  if (!rawJson || rawJson[0] == '\0') return false;

  if (isValidLMKKey()) {
    int16_t code = sendHTTPSJSON(ip, rawJson, msgType, timeoutMs);
    if (code >= 200 && code < 400) return true;
  }

  String httpBody = String(rawJson);
  JSONbuilder_encodeHTTP(httpBody);
  return sendHTTPJSON(ip, httpBody.c_str(), msgType, timeoutMs) == 200;
}

#if _IS_SERVER_HUB
static bool postMeshJsonReadReply(IPAddress ip, const char* rawJson, String& reply) {
  reply = "";
  if (!rawJson || !rawJson[0] || !wifiReadyForNetwork()) return false;
  if (ip == IPAddress(0, 0, 0, 0)) return false;

  if (isValidLMKKey()) {
    uint16_t payloadLen = (uint16_t)strlen(rawJson);
    const uint16_t framedLen = payloadLen + 2;
    uint8_t* framed = (uint8_t*)malloc(framedLen);
    uint8_t* encBuf = framed ? (uint8_t*)malloc(LMK_HTTP_PING_MAX_CIPHER) : nullptr;
    if (framed && encBuf) {
      framed[0] = payloadLen & 0xFF;
      framed[1] = (payloadLen >> 8) & 0xFF;
      memcpy(framed + 2, rawJson, payloadLen);
      uint16_t encLen = 0;
      const bool encOk = BootSecure::encrypt(framed, framedLen, (char*)Prefs.KEYS.ESPNOW_KEY, encBuf, &encLen, 16) == 1
          && encLen > 0 && encLen <= LMK_HTTP_PING_MAX_CIPHER;
      free(framed);
      framed = nullptr;
      if (encOk) {
        static char urlBuffer[64];
        snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST_ENC", ip.toString().c_str());
        WiFiClient client;
        HTTPClient http;
        client.setTimeout(4000);
        http.begin(client, urlBuffer);
        http.setTimeout(4000);
        http.addHeader("Content-Type", "application/octet-stream");
        esp_task_wdt_reset();
        int httpCode = http.sendRequest("POST", encBuf, encLen);
        esp_task_wdt_reset();
        free(encBuf);
        encBuf = nullptr;
        if (httpCode >= 200 && httpCode < 400) {
          uint8_t* respEnc = (uint8_t*)malloc(LMK_HTTP_PING_MAX_CIPHER);
          if (respEnc) {
            size_t respLen = readHttpEncResponseBody(http, respEnc, LMK_HTTP_PING_MAX_CIPHER, 4000);
            if (respLen >= 32) decryptHttpCipherToPlain(respEnc, (uint16_t)respLen, reply);
            free(respEnc);
          }
        }
        http.end();
        if (reply.length() > 0) return true;
      }
    }
    if (framed) free(framed);
    if (encBuf) free(encBuf);
  }

  String body = rawJson;
  JSONbuilder_encodeHTTP(body);
  static char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", ip.toString().c_str());
  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(body.c_str());
  M.timeout = 4000;
  if (!M.initPayload(640)) return false;
  if (!SendHTTPMessage(M) || !M.payload || !M.payload.get()) return false;
  reply = M.payload.get();
  return reply.length() > 0;
}

static bool parseMeshParamsReply(const String& reply, ArborysMeshParams& out, bool* accepted) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, reply) != DeserializationError::Ok) return false;
  if (String(doc["msgType"] | "") != "meshGet") return false;
  if (!meshParamsFromJson(doc.as<JsonObject>(), out)) return false;
  if (accepted) *accepted = (int)(doc["ok"] | 0) != 0;
  return true;
}

static bool hubExchangeMeshParams(ArborysDevType* device, bool doSet, const ArborysMeshParams* send, ArborysMeshParams& got, bool* accepted) {
  if (accepted) *accepted = false;
  if (!device) return false;
  char json[480];
  if (!doSet) {
    snprintf(json, sizeof(json), "{\"msgType\":\"meshGet\"}");
  } else {
    if (!send || !meshParamsInRange(*send)) return false;
    formatMeshParamsJson(json, sizeof(json), *send, 1);
    // formatMeshParamsJson writes a meshGet reply. The set request uses meshSet.
    char* type = strstr(json, "meshGet");
    if (!type) return false;
    memcpy(type, "meshSet", 7);
  }
  String reply;
  if (!postMeshJsonReadReply(device->IP, json, reply)) return false;
  return parseMeshParamsReply(reply, got, accepted);
}
#endif

static bool readWebLong(const char* key, long& out) {
  if (!server.hasArg(key)) return false;
  out = server.arg(key).toInt();
  return true;
}

static bool meshParamsFromWeb(ArborysMeshParams& p) {
  long ttlN, ttlC, burst, gapMin, gapMax, supp, ackMs, ackR;
  long rssiW, rssiS, dlyW, dlyS, jitter, critMin, critMax, reorder;
  if (!readWebLong("ttlN", ttlN) || !readWebLong("ttlC", ttlC) || !readWebLong("burst", burst)) return false;
  if (!readWebLong("gapMin", gapMin) || !readWebLong("gapMax", gapMax) || !readWebLong("supp", supp)) return false;
  if (!readWebLong("ackMs", ackMs) || !readWebLong("ackR", ackR)) return false;
  if (!readWebLong("rssiW", rssiW) || !readWebLong("rssiS", rssiS)) return false;
  if (!readWebLong("dlyW", dlyW) || !readWebLong("dlyS", dlyS) || !readWebLong("jitter", jitter)) return false;
  if (!readWebLong("critMin", critMin) || !readWebLong("critMax", critMax) || !readWebLong("reorder", reorder)) return false;
  if (ttlN < 0 || ttlN > 255 || ttlC < 0 || ttlC > 255 || burst < 0 || burst > 255) return false;
  if (supp < 0 || supp > 255 || ackR < 0 || ackR > 255) return false;
  if (rssiW < -128 || rssiW > 127 || rssiS < -128 || rssiS > 127) return false;
  if (gapMin < 0 || gapMin > 65535 || gapMax < 0 || gapMax > 65535) return false;
  if (ackMs < 0 || ackMs > 65535 || dlyW < 0 || dlyW > 65535 || dlyS < 0 || dlyS > 65535) return false;
  if (jitter < 0 || jitter > 65535 || critMin < 0 || critMin > 65535 || critMax < 0 || critMax > 65535) return false;
  if (reorder < 0 || reorder > 65535) return false;
  p.ttlNormal = (uint8_t)ttlN;
  p.ttlCritical = (uint8_t)ttlC;
  p.criticalBurstCount = (uint8_t)burst;
  p.criticalBurstGapMinMs = (uint16_t)gapMin;
  p.criticalBurstGapMaxMs = (uint16_t)gapMax;
  p.relaySuppressCount = (uint8_t)supp;
  p.ackTimeoutMs = (uint16_t)ackMs;
  p.ackRetries = (uint8_t)ackR;
  p.relayRssiWeakDbm = (int8_t)rssiW;
  p.relayRssiStrongDbm = (int8_t)rssiS;
  p.relayDelayWeakMs = (uint16_t)dlyW;
  p.relayDelayStrongMs = (uint16_t)dlyS;
  p.relayJitterMs = (uint16_t)jitter;
  p.criticalRelayMinMs = (uint16_t)critMin;
  p.criticalRelayMaxMs = (uint16_t)critMax;
  p.originReorderWindow = (uint16_t)reorder;
  return meshParamsInRange(p);
}

static void appendMeshNumberRow(const char* label, const char* name, bool known, long value) {
  WEBHTML += "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">";
  WEBHTML += label;
  WEBHTML += "</td><td style=\"padding: 8px; border: 1px solid #ddd;\">";
  if (!known) {
    WEBHTML += "?";
  } else {
    WEBHTML += "<input type=\"number\" name=\"";
    WEBHTML += name;
    WEBHTML += "\" value=\"";
    WEBHTML += String(value);
    WEBHTML += "\" required style=\"width:8em;\">";
  }
  WEBHTML += "</td></tr>";
}

static void appendMeshSettingsTable(const ArborysMeshParams* p) {
  const bool known = p != nullptr;
  const ArborysMeshParams blank = {};
  const ArborysMeshParams& v = known ? *p : blank;
  WEBHTML += "<table style=\"width:100%; border-collapse:collapse; max-width:640px;\">";
  appendMeshNumberRow("Normal TTL (1-15)", "ttlN", known, v.ttlNormal);
  appendMeshNumberRow("Critical TTL (1-15)", "ttlC", known, v.ttlCritical);
  appendMeshNumberRow("Critical burst count (1-8)", "burst", known, v.criticalBurstCount);
  appendMeshNumberRow("Burst gap min ms (0-1000)", "gapMin", known, v.criticalBurstGapMinMs);
  appendMeshNumberRow("Burst gap max ms (0-1000)", "gapMax", known, v.criticalBurstGapMaxMs);
  appendMeshNumberRow("Relay suppress count (1-255)", "supp", known, v.relaySuppressCount);
  appendMeshNumberRow("ACK timeout ms (50-10000)", "ackMs", known, v.ackTimeoutMs);
  appendMeshNumberRow("ACK retries (0-8)", "ackR", known, v.ackRetries);
  appendMeshNumberRow("Weak RSSI dBm (-100 to -20)", "rssiW", known, v.relayRssiWeakDbm);
  appendMeshNumberRow("Strong RSSI dBm (-100 to -20)", "rssiS", known, v.relayRssiStrongDbm);
  appendMeshNumberRow("Weak relay delay ms (0-5000)", "dlyW", known, v.relayDelayWeakMs);
  appendMeshNumberRow("Strong relay delay ms (0-5000)", "dlyS", known, v.relayDelayStrongMs);
  appendMeshNumberRow("Relay jitter ms (0-1000)", "jitter", known, v.relayJitterMs);
  appendMeshNumberRow("Critical relay min ms (0-1000)", "critMin", known, v.criticalRelayMinMs);
  appendMeshNumberRow("Critical relay max ms (0-1000)", "critMax", known, v.criticalRelayMaxMs);
  appendMeshNumberRow("Origin reorder window (1-4096)", "reorder", known, v.originReorderWindow);
  WEBHTML += "</table>";
  WEBHTML += "<p>Strong RSSI must be above weak RSSI. Each maximum must be at least its matching minimum.</p>";
}

static void renderMeshSettingsPage(const char* notice, bool noticeOk, bool known, const ArborysMeshParams* params, int16_t devIndex, bool isThisDevice) {
  WEBHTML = "";
  serverTextHeader("Mesh Settings");
  serverTextStreamBegin(200, true);
  appendStandardPageNav();
  if (notice && notice[0]) {
    WEBHTML += noticeOk
        ? "<div style=\"background-color: #d4edda; color: #155724; padding: 15px; margin: 10px 0; border: 1px solid #c3e6cb; border-radius: 4px;\">"
        : "<div style=\"background-color: #f8d7da; color: #721c24; padding: 15px; margin: 10px 0; border: 1px solid #f5c6cb; border-radius: 4px;\">";
    WEBHTML += notice;
    WEBHTML += "</div>";
  }
  serverTextFlush(true);

#if _IS_SERVER_HUB
  WEBHTML += "<div style=\"background-color: #e8f5e8; color: #2e7d32; padding: 15px; margin: 10px 0; border: 1px solid #4caf50; border-radius: 4px;\">";
  WEBHTML += "<form method=\"GET\" action=\"/MESH_SETTINGS\" style=\"margin: 0;\">";
  WEBHTML += "<span style=\"font-size: 1.17em; font-weight: bold;\">Device </span>";
  WEBHTML += "<select name=\"devIndex\" onchange=\"this.form.submit()\" style=\"font-size: 1em; padding: 4px 8px; margin: 0 4px; max-width: 70%;\">";
  appendDeviceIndexOptions(devIndex);
  WEBHTML += "</select>";
  WEBHTML += "<span style=\"font-size: 1.17em; font-weight: bold;\"> of " + String(Sensors.getNumDevices()) + "</span>";
  if (isThisDevice) WEBHTML += "<span style=\"font-size: 1.17em; font-weight: bold;\"> (this device)</span>";
  WEBHTML += "</form>";
  if (!isThisDevice) {
    WEBHTML += "<div style=\"margin-top: 10px; text-align: center;\">";
    appendRemoteDevicePingLinks();
    WEBHTML += "</div>";
  }
  WEBHTML += "</div>";
  serverTextFlush(true);
#else
  (void)devIndex;
  (void)isThisDevice;
#endif

  WEBHTML += "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
  WEBHTML += "<h4>ArborysMesh Settings</h4>";
  WEBHTML += "<form method=\"POST\" action=\"/MESH_SETTINGS\">";
#if _IS_SERVER_HUB
  WEBHTML += "<input type=\"hidden\" name=\"devIndex\" value=\"" + String(devIndex) + "\">";
#endif
  appendMeshSettingsTable(known ? params : nullptr);
  const char* dis = known ? "" : " disabled";
  WEBHTML += "<button type=\"submit\" name=\"action\" value=\"apply\"";
  WEBHTML += dis;
  WEBHTML += " style=\"margin: 8px 8px 0 0; padding: 8px 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px;\">Update</button>";
  WEBHTML += "<button type=\"submit\" name=\"action\" value=\"defaults\"";
  WEBHTML += dis;
  WEBHTML += " style=\"margin-top: 8px; padding: 8px 16px; background-color: #FF9800; color: white; border: none; border-radius: 4px;\">Defaults</button>";
  WEBHTML += "</form></div></body></html>";
  serverTextClose(200, true);
}

void handleMESH_SETTINGS() {
  registerHTTPMessage("MeshSet");
  const char* notice = "";
  bool noticeOk = false;

  int16_t devIndex = CURRENT_DEVICEVIEWER_DEVINDEX;
  if (server.hasArg("devIndex")) {
    int16_t idx = server.arg("devIndex").toInt();
    if (idx >= 0 && idx < NUMDEVICES && Sensors.isDeviceInit(idx)) {
      devIndex = idx;
      CURRENT_DEVICEVIEWER_DEVINDEX = idx;
    }
  }
  const int16_t myIndex = (int16_t)Sensors.findMyDeviceIndex();
  const bool isThisDevice = myIndex >= 0 && devIndex == myIndex;
  ArborysDevType* device = Sensors.getDeviceByDevIndex(devIndex);

  ArborysMeshParams shown = meshParams();
  bool known = false;

#if _IS_SERVER_HUB
  auto loadShown = [&]() {
    if (!device) { known = false; return; }
    if (isThisDevice) {
      shown = meshParams();
      known = true;
      return;
    }
    bool accepted = false;
    known = hubExchangeMeshParams(device, false, nullptr, shown, &accepted);
  };
#else
  auto loadShown = [&]() {
    shown = meshParams();
    known = true;
  };
#endif

  if (server.method() == HTTP_POST && device) {
    const bool wantDefaults = server.hasArg("action") && server.arg("action") == "defaults";
    ArborysMeshParams next = {};
    bool valuesOk = wantDefaults || meshParamsFromWeb(next);
    if (!valuesOk) {
      notice = "A value is outside the allowed range. Settings were not changed.";
      noticeOk = false;
      loadShown();
    } else {
#if _IS_SERVER_HUB
      if (isThisDevice) {
        meshSetParams(next);
        shown = meshParams();
        known = true;
        notice = wantDefaults ? "Default mesh settings restored." : "Mesh settings updated.";
        noticeOk = true;
      } else {
        bool accepted = false;
        known = hubExchangeMeshParams(device, true, &next, shown, &accepted);
        if (!known) {
          notice = "Settings could not be sent to this device.";
          noticeOk = false;
        } else if (!accepted) {
          notice = "This device rejected the settings.";
          noticeOk = false;
        } else {
          notice = wantDefaults ? "Default mesh settings were sent." : "Mesh settings were sent.";
          noticeOk = true;
        }
      }
#else
      meshSetParams(next);
      shown = meshParams();
      known = true;
      notice = wantDefaults ? "Default mesh settings restored." : "Mesh settings updated.";
      noticeOk = true;
#endif
    }
  } else {
    loadShown();
    if (!known) notice = "Settings could not be read from this device.";
  }

  renderMeshSettingsPage(notice, noticeOk, known, known ? &shown : nullptr, devIndex, isThisDevice);
}

uint8_t sendAllSensors(bool forceSend, int16_t sendToDeviceIndex, bool useUDP) {
  //can use -1 to send to broadcast (if using HTTP, then send to all servers), or a specific device index to send to a specific device
  
  return SendData(-1,forceSend,sendToDeviceIndex,useUDP);

  
}

// Counted expiry requests today. 0–2: mesh only. 3–5: mesh + UDP. 6+: mesh + HTTP to each hub.
static uint8_t s_expiredReqCount = 0;
static uint32_t s_expiredReqLastCounted = 0;
static bool s_expiredSendPending = false;

static uint32_t peripheralSendIntervalSec() {
  int16_t my = Sensors.findMyDeviceIndex();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(my);
  if (me && me->SendingInt) return me->SendingInt;
  return 300;
}

static uint8_t expiredSendLadder() {
  if (s_expiredReqCount >= 6) return 2;
  if (s_expiredReqCount >= 3) return 1;
  return 0;
}

static const char* preferredCommunicationsLabel() {
  // A failed boot mesh ACK keeps UDP for the rest of this boot, including past midnight.
  if (s_udpSensorsUntilBoot) return "UDP";
  // Hubs ask again when readings are missing. Repeated asks move this node off mesh.
  if (expiredSendLadder() >= 2) return isValidLMKKey() ? "HTTPS" : "HTTP";
  if (expiredSendLadder() >= 1) return "UDP";
  return "arborysmesh";
}

void resetExpiredRequestLadder() {
  // Day change only. s_udpSensorsUntilBoot stays set until reboot.
  s_expiredReqCount = 0;
  s_expiredReqLastCounted = 0;
  s_expiredSendPending = false;
}

void noteExpiredDataRequest(uint64_t hubMac) {
  (void)hubMac;
#ifdef _USELOWPOWER
  return;
#else
  int16_t my = Sensors.findMyDeviceIndex();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(my);
  if (me && bitRead(me->Flags, 2)) return;

  const uint32_t now = (uint32_t)utcNow();
  const uint32_t window = peripheralSendIntervalSec() * 2u;
  if (s_expiredReqLastCounted != 0 && now <= s_expiredReqLastCounted + window) {
    SerialPrint("Expiry request inside 2x send interval; not counted (count " + String(s_expiredReqCount) + ")", true);
  } else {
    if (s_expiredReqCount < 255) s_expiredReqCount++;
    s_expiredReqLastCounted = now;
    SerialPrint("Expiry request counted " + String(s_expiredReqCount) + " today", true);
  }
  s_expiredSendPending = true;
#endif
}

static bool localSensorReadingIsStale(const ArborysSnsType* S) {
  if (!S) return false;
  const uint32_t poll = S->PollingInt ? S->PollingInt : S->SendingInt;
  if (poll == 0) return false; // not a scheduled reading
  if (S->timeRead == 0) return true;
  const uint32_t now = (uint32_t)utcNow();
  if (now <= S->timeRead) return false;
  return (now - S->timeRead) > poll;
}

static void stripErrorText(char* s) {
  if (!s) return;
  for (char* p = s; *p; ++p) {
    if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r') *p = ' ';
  }
}

void sendErrorLogToHubs(uint16_t code, const ArborysSnsType* sensor, const char* detail) {
  const int16_t my = Sensors.findMyDeviceIndex();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(my);

  char dev[31] = "";
  char sns[31] = "";
  char mac[17] = "";
  char detailBuf[41] = "";
  if (me) strncpy(dev, me->devName, sizeof(dev) - 1);
  if (sensor) strncpy(sns, sensor->snsName, sizeof(sns) - 1);
  if (me) {
    String macStr = MACToString(me->MAC, '\0', true);
    strncpy(mac, macStr.c_str(), sizeof(mac) - 1);
  }
  if (detail) strncpy(detailBuf, detail, sizeof(detailBuf) - 1);
  stripErrorText(dev);
  stripErrorText(sns);
  stripErrorText(mac);
  stripErrorText(detailBuf);

  const int snsType = sensor ? (int)sensor->snsType : -1;
  const int snsID = sensor ? (int)sensor->snsID : -1;
  char msg[100];
  formatHubErrorMessage(msg, sizeof(msg), code, dev, sns, mac, snsType, snsID, detailBuf);
  // storeError forwards this line to hubs on a peripheral.
  storeError(msg, (ERRORCODES)code, true);
}

void forwardPeripheralErrorToHubs(uint16_t code, const char* message) {
  if (!message || !message[0] || !wifiReadyForNetwork()) return;

  const int16_t my = Sensors.findMyDeviceIndex();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(my);
  if (!me) return;

  char dev[31] = "";
  char mac[17] = "";
  char detail[100] = "";
  strncpy(dev, me->devName, sizeof(dev) - 1);
  String macStr = MACToString(me->MAC, '\0', true);
  strncpy(mac, macStr.c_str(), sizeof(mac) - 1);
  strncpy(detail, message, sizeof(detail) - 1);
  stripErrorText(dev);
  stripErrorText(mac);
  stripErrorText(detail);

  char json[512];
  snprintf(json, sizeof(json),
      "{\"msgType\":\"errorLog\",\"code\":%u,\"dev\":\"%s\",\"sns\":\"\",\"mac\":\"%s\",\"snsType\":-1,\"snsID\":-1,\"detail\":\"%s\"}",
      (unsigned)code, dev, mac, detail);

  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) continue;
    if (d->IP == IPAddress(0, 0, 0, 0) || d->IP == WiFi.localIP()) continue;
    sendJsonViaPreferredHttp(d->IP, json, "errorLog", 2500);
    delay(10);
  }
}

static void processDeferredExpiredDataRequest() {
  if (!s_expiredSendPending) return;
  s_expiredSendPending = false;

  const int16_t my = Sensors.findMyDeviceIndex();
  for (int16_t i = 0; i < NUMSENSORS; ++i) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(i);
    if (!S || !S->IsSet || S->deviceIndex != my) continue;
    if (!localSensorReadingIsStale(S)) continue;
    sendErrorLogToHubs((uint16_t)ERROR_SENSOR_READ, S, "scheduled read failed");
  }
  SendData(-1, true, -1, false);
}

void processDeferredDataRequest() {
  processDeferredExpiredDataRequest();
  if (!s_pendingDataRequest.pending) return;
  int16_t senderIndex = s_pendingDataRequest.senderIndex;
  int16_t snsIndex = s_pendingDataRequest.snsIndex;
  s_pendingDataRequest.pending = false;
  if (senderIndex < 0) return;
  if (snsIndex >= 0) {
    SendData(snsIndex, true, senderIndex, false);
  } else {
    sendAllSensors(true, senderIndex, false);
  }
}


static void markAllServersDataSent() {
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (d && d->IsSet && IS_SERVER_DEVICE_TYPE(d->devType)) {
      d->dataSent = utcNow();
    }
  }
}

static void backoffAllServersDataSent() {
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (d && d->IsSet && IS_SERVER_DEVICE_TYPE(d->devType)) {
      d->dataSent = utcNow() - d->SendingInt + 10 * 60;
    }
  }
}

// ArborysMesh: one TELEMETRY frame per sensor (device+sensor packed).
static bool sendSensorDataMeshBundle(const int16_t* snsIndices, uint8_t count, bool alsoUdp) {
  if (!snsIndices || count == 0) return false;
  int16_t myIdx = Sensors.findMyDeviceIndex();
  ArborysDevType* me = Sensors.getDeviceByDevIndex(myIdx);
  if (!me) return false;
  bool ok = false;
  for (uint8_t i = 0; i < count; ++i) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndices[i]);
    if (!S || !S->IsSet) continue;
    if (meshSendTelemetry(me, S, alsoUdp)) ok = true;
  }
  return ok;
}

static void pauseBetweenSensorJsonMessages() {
  for (uint8_t sec = 0; sec < 10; ++sec) {
    delay(1000);
    esp_task_wdt_reset();
  }
}

// Prefix of list that fits in the JSON buffer. Sensor text length varies, so this builds the real string.
static uint8_t countSensorsThatFitJson(const int16_t* list, uint8_t count, uint16_t jsonBufferSize, bool ackReq = false, uint16_t ackId = 0) {
  uint8_t n = 0;
  while (n < count && sensorMSGFitsBuffer(list, (uint8_t)(n + 1), jsonBufferSize, false, ackReq, ackId)) {
    n++;
  }
  return n;
}

static bool postSensorJsonToTargets(const char* jsonBuffer, bool forceSend, ArborysDevType* oneDevice) {
  if (oneDevice) {
    if (!sendJsonViaPreferredHttp(oneDevice->IP, jsonBuffer, "snsMsg")) return false;
    oneDevice->dataSent = utcNow();
    return true;
  }
  bool any = false;
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!isDeviceSendTime(d, forceSend)) continue;
    if (!IS_SERVER_DEVICE_TYPE(d->devType)) continue;
    if (d->IP == IPAddress(0, 0, 0, 0)) continue;
    if (sendJsonViaPreferredHttp(d->IP, jsonBuffer, "snsMsg")) {
      d->dataSent = utcNow();
      any = true;
    }
    delay(10);
  }
  return any;
}

// oneDevice null: each known hub. Otherwise that device only.
// Successful chunks are marked sent. A sensor that cannot fit alone is logged and skipped.
static bool sendSensorJsonInFittingMessages(const int16_t* sendList, uint8_t sendCount, bool forceSend,
    char* jsonBuffer, uint16_t jsonBufferSize, ArborysDevType* oneDevice, bool markSent = true) {
  if (!sendList || sendCount == 0 || !jsonBuffer || jsonBufferSize == 0) return false;
  uint8_t offset = 0;
  bool any = false;
  bool sentOne = false;
  while (offset < sendCount) {
    const uint8_t n = countSensorsThatFitJson(sendList + offset, (uint8_t)(sendCount - offset), jsonBufferSize);
    if (n == 0) {
      JSONbuilder_sensorMSG_list(sendList + offset, 1, jsonBuffer, jsonBufferSize, false);
      offset++;
      continue;
    }
    if (sentOne) pauseBetweenSensorJsonMessages();
    if (n < (uint8_t)(sendCount - offset) || offset > 0) {
      SerialPrint("SendData: JSON message " + String(n) + " sensor(s), " + String(sendCount - offset - n) + " after this", true);
    }
    if (!JSONbuilder_sensorMSG_list(sendList + offset, n, jsonBuffer, jsonBufferSize, false)) {
      offset++;
      continue;
    }
    if (postSensorJsonToTargets(jsonBuffer, forceSend, oneDevice)) {
      if (markSent) wrapupSendDataList(sendList + offset, n);
      any = true;
    }
    sentOne = true;
    offset += n;
  }
  return any;
}

static bool sendListedSensorsHttpToHubs(const int16_t* sendList, uint8_t sendCount, bool forceSend, char* jsonBuffer, uint16_t jsonBufferSize, bool markSent = true) {
  return sendSensorJsonInFittingMessages(sendList, sendCount, forceSend, jsonBuffer, jsonBufferSize, nullptr, markSent);
}

// ip 0.0.0.0 is the presence multicast. Chunks that fit the JSON buffer go out back to back.
static bool sendSensorJsonViaUdp(const int16_t* sendList, uint8_t sendCount,
    char* jsonBuffer, uint16_t jsonBufferSize, IPAddress ip, bool markSent = true) {
  if (!sendList || sendCount == 0 || !jsonBuffer || jsonBufferSize == 0) return false;
  uint8_t offset = 0;
  bool any = false;
  while (offset < sendCount) {
    const uint8_t n = countSensorsThatFitJson(sendList + offset, (uint8_t)(sendCount - offset), jsonBufferSize);
    if (n == 0) {
      offset++;
      continue;
    }
    if (!JSONbuilder_sensorMSG_list(sendList + offset, n, jsonBuffer, jsonBufferSize, false)) {
      offset++;
      continue;
    }
    if (sendUDPMessage((uint8_t*)jsonBuffer, ip, (uint16_t)strlen(jsonBuffer), "snsData")) {
      if (markSent) wrapupSendDataList(sendList + offset, n);
      any = true;
    }
    offset += n;
  }
  return any;
}

static bool broadcastUdpPresence() {
  char jsonBuffer[512];
  jsonBuffer[0] = '\0';
  JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), false, false);
  if (jsonBuffer[0] == '\0') return false;
  return sendUDPMessage((uint8_t*)jsonBuffer, IPAddress(0, 0, 0, 0), (uint16_t)strlen(jsonBuffer), "helloPing");
}

static void decideBootUdpTransport() {
  s_bootUdpPhase = 2;
  bool any = false;
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    if (!s_bootUdpServer[i]) continue;
    any = true;
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (!d || !d->IsSet) {
      s_udpSensorsUntilBoot = true;
      SerialPrint("Boot UDP: registered server missing; UDP until reboot", true);
      return;
    }
    SerialPrint("Boot UDP: ArborysMesh ACK to " + String(d->devName), true);
    uint16_t waitMs = meshParams().ackTimeoutMs;
    if (waitMs < 2000) waitMs = 2000;
    if (!meshBlockingAckCheck(d, waitMs, nullptr)) {
      s_udpSensorsUntilBoot = true;
      SerialPrint("Boot UDP: ACK failed for " + String(d->devName) + "; UDP until reboot", true);
      return;
    }
  }
  if (!any) {
    SerialPrint("Boot UDP: no server registered; ArborysMesh stays primary", true);
  } else {
    SerialPrint("Boot UDP: ArborysMesh ACK ok; existing send protocol", true);
  }
}

void serviceBootUdpPresence(bool blockUntilDecided) {
  if (s_bootUdpPhase == 2) return;
  if (!wifiReadyForNetwork()) return;
#ifdef _USEUDP
  if (s_bootUdpPhase == 0) {
    if (!connectUDP()) return;
    if (!broadcastUdpPresence()) return;
    s_bootUdpPhase = 1;
    s_bootUdpDeadlineMs = millis() + BOOT_UDP_PRESENCE_WAIT_MS;
    SerialPrint("Boot UDP: presence broadcast", true);
  }
  if (s_bootUdpPhase != 1) return;
  if (blockUntilDecided) {
    while ((int32_t)(millis() - s_bootUdpDeadlineMs) < 0) {
      receiveUDPMessage();
      delay(20);
      esp_task_wdt_reset();
    }
  } else if ((int32_t)(millis() - s_bootUdpDeadlineMs) < 0) {
    return;
  }
  decideBootUdpTransport();
#else
  (void)blockUntilDecided;
  s_bootUdpPhase = 2;
#endif
}

static bool destinationNeedsLAN(const ArborysDevType* d, bool haveWifi) {
  if (!d) return true;
  return !haveWifi || d->IP == IPAddress(0, 0, 0, 0);
}

static bool isSensorMonitored(const ArborysSnsType* S) {
  return S && bitRead(S->Flags, 1);
}

static bool sensorHasFreshUnreadData(const ArborysSnsType* S) {
  if (!S || !isTimeValid(S->timeRead)) {
    return false;
  }
  return S->timeLogged == 0 || S->timeRead > S->timeLogged;
}

static bool isIndexAlreadyInList(int16_t idx, const int16_t* list, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    if (list[i] == idx) {
      return true;
    }
  }
  return false;
}

// Due sensors are always included; monitored sensors with unread reads are added if they fit in the JSON buffer.
static uint8_t packSensorsForSend(int16_t* outIndices, uint8_t maxOut, bool forceSend, int16_t triggerSnsIndex,
    bool applyJsonLimit, uint16_t jsonBufferSize, bool forHTTP) {
  const int16_t myIdx = Sensors.findMyDeviceIndex();
  int16_t dueIndices[NUMSENSORS];
  uint8_t dueCount = 0;
  int16_t freshIndices[NUMSENSORS];
  uint8_t freshCount = 0;

  for (int16_t i = 0; i < NUMSENSORS; ++i) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(i);
    if (!S || !S->IsSet || S->deviceIndex != myIdx) continue;
#if _HAS_LOCAL_SENSORS
    // forceSend still omits a sensor that has no real sample yet.
    if (!localSensorReadyToSend(S)) continue;
#endif
    const bool named = forceSend && triggerSnsIndex == i;
    const bool monitored = bitRead(S->Flags, 1);
    const bool criticalEdge = bitRead(S->Flags, 7) && bitRead(S->Flags, 6);
    // Interval and ordinary sends need Monitored. A send-all does not pull an
    // unmonitored sensor. Critical still goes out on a bounds or expiry edge.
    if (!named && !monitored && !criticalEdge) continue;

    bool isDue = false;
    if (forceSend) {
      if (named || criticalEdge) isDue = true;
      else if (triggerSnsIndex < 0 && monitored) isDue = true;
    } else {
      isDue = checkThisSensorTime(S);
    }

    if (isDue) {
      dueIndices[dueCount++] = i;
    } else if (monitored && S->SendingInt != 0 && sensorHasFreshUnreadData(S)) {
      freshIndices[freshCount++] = i;
    }
  }

  if (dueCount == 0) {
    return 0;
  }

  const uint8_t requestedTotal = dueCount + freshCount;
  uint8_t outCount = 0;

  if (applyJsonLimit) {
    for (uint8_t d = 0; d < dueCount && outCount < maxOut; ++d) {
      int16_t trial[NUMSENSORS];
      memcpy(trial, outIndices, outCount * sizeof(int16_t));
      trial[outCount] = dueIndices[d];
      if (sensorMSGFitsBuffer(trial, outCount + 1, jsonBufferSize, forHTTP)) {
        outIndices[outCount++] = dueIndices[d];
      }
    }

    if (outCount == 0) {
      storeError("SendData: JSON buffer too small for any due sensor payload", ERROR_JSON_PARSE, true);
      return 0;
    }

    for (uint8_t f = 0; f < freshCount && outCount < maxOut; ++f) {
      int16_t candidate = freshIndices[f];
      if (isIndexAlreadyInList(candidate, outIndices, outCount)) {
        continue;
      }

      int16_t trial[NUMSENSORS];
      memcpy(trial, outIndices, outCount * sizeof(int16_t));
      trial[outCount] = candidate;
      if (sensorMSGFitsBuffer(trial, outCount + 1, jsonBufferSize, forHTTP)) {
        outIndices[outCount++] = candidate;
      }
    }

    if (outCount < requestedTotal) {
      SerialPrint("SendData: JSON buffer limit - sent " + String(outCount) + " of " + String(requestedTotal) + " requested sensors", true);
    }
  } else {
    for (uint8_t d = 0; d < dueCount && outCount < maxOut; ++d) {
      outIndices[outCount++] = dueIndices[d];
    }
    for (uint8_t f = 0; f < freshCount && outCount < maxOut; ++f) {
      if (!isIndexAlreadyInList(freshIndices[f], outIndices, outCount)) {
        outIndices[outCount++] = freshIndices[f];
      }
    }
  }

  return outCount;
}

#if _I_AM_PERIPHERAL && _HAS_LOCAL_SENSORS
static bool sensorIsAckComponent(int16_t idx) {
  ArborysSnsType* S = Sensors.snsIndexToPointer(idx);
  if (!S) return false;
  if (bitRead(S->Flags, 7)) return true;
  return bitRead(S->Flags, 1) && monitoredLimitCrossPending(idx);
}

static bool peripheralSendNeedsAck(const int16_t* list, uint8_t count) {
  if (!list) return false;
  for (uint8_t i = 0; i < count; ++i) {
    if (sensorIsAckComponent(list[i])) return true;
  }
  return false;
}

static bool snsAckBodyOk(const String& reply, uint16_t ackId) {
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, reply) != DeserializationError::Ok) return false;
  if (String(doc["msgType"] | "") != "ackSnsData") return false;
  if ((uint16_t)(doc["ackId"] | 0) != ackId) return false;
  return (int)(doc["ok"] | 0) != 0;
}

static uint16_t nextSnsAckId() {
  static uint16_t s_next = 1;
  uint16_t id = s_next++;
  if (s_next == 0) s_next = 1;
  return id;
}

static bool waitUdpSnsAck(uint16_t ackId) {
  s_snsAckWaitId = ackId;
  s_snsAckOk = false;
  uint16_t waitMs = meshParams().ackTimeoutMs;
  if (waitMs < 2000) waitMs = 2000;
  const uint32_t t0 = millis();
  while ((int32_t)(millis() - (t0 + waitMs)) < 0) {
    receiveUDPMessage();
    if (s_snsAckOk) {
      s_snsAckWaitId = 0;
      return true;
    }
    delay(20);
    esp_task_wdt_reset();
  }
  s_snsAckWaitId = 0;
  return false;
}

static bool httpsAckOneServer(IPAddress ip, const char* json, uint16_t ackId) {
  if (!json || !isValidLMKKey()) return false;
  String reply;
  int16_t code = sendHTTPEncryptedPayload(ip, (const uint8_t*)json, (uint16_t)strlen(json), "snsDataAck", 4000, &reply);
  if (code < 200 || code >= 400) return false;
  return snsAckBodyOk(reply, ackId);
}

// After the normal send: multicast UDP snsData with ackReq. One hub ack is enough.
// Otherwise HTTPS the same packet to each known server, and each must ack.
static bool deliverCriticalSensorAck(const int16_t* sendList, uint8_t sendCount, char* jsonBuffer, uint16_t jsonBufferSize) {
  int16_t ordered[NUMSENSORS];
  uint8_t nOrd = 0;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < sendCount; ++i) {
      const bool crit = sensorIsAckComponent(sendList[i]);
      if ((pass == 0 && crit) || (pass == 1 && !crit)) ordered[nOrd++] = sendList[i];
    }
  }

  struct AckSlice { uint8_t off; uint8_t n; uint16_t ackId; };
  AckSlice failed[8];
  uint8_t nFail = 0;
  bool udpOk = true;
  uint8_t offset = 0;
  while (offset < nOrd && nFail < 8) {
    const uint16_t ackId = nextSnsAckId();
    const uint8_t n = countSensorsThatFitJson(ordered + offset, (uint8_t)(nOrd - offset), jsonBufferSize, true, ackId);
    if (n == 0) {
      udpOk = false;
      break;
    }
    bool hasCrit = false;
    for (uint8_t i = 0; i < n; ++i) {
      if (sensorIsAckComponent(ordered[offset + i])) hasCrit = true;
    }
    if (!hasCrit) break;
    if (!JSONbuilder_sensorMSG_list(ordered + offset, n, jsonBuffer, jsonBufferSize, false, true, ackId)) {
      udpOk = false;
      break;
    }
    SerialPrint("SendData: UDP snsData ackId " + String(ackId), true);
    const bool sent = sendUDPMessage((uint8_t*)jsonBuffer, IPAddress(0, 0, 0, 0), (uint16_t)strlen(jsonBuffer), "snsDataAck");
    if (sent && waitUdpSnsAck(ackId)) {
      wrapupSendDataList(ordered + offset, n);
    } else {
      if (nFail < 8) failed[nFail++] = AckSlice{offset, n, ackId};
      udpOk = false;
    }
    offset += n;
  }
  if (udpOk) return true;

  SerialPrint("SendData: UDP snsData ack missed; HTTPS to each server", true);
  if (nFail == 0) return false;
  bool anyServer = false;
  bool allHttps = true;
  for (int16_t di = 0; di < NUMDEVICES; ++di) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
    if (!d || !d->IsSet || !IS_SERVER_DEVICE_TYPE(d->devType)) continue;
    if (d->MAC == ESP.getEfuseMac()) continue;
    if (d->IP == IPAddress(0, 0, 0, 0) || d->IP == WiFi.localIP()) continue;
    anyServer = true;
    for (uint8_t f = 0; f < nFail; ++f) {
      esp_task_wdt_reset();
      if (!JSONbuilder_sensorMSG_list(ordered + failed[f].off, failed[f].n, jsonBuffer, jsonBufferSize, false, true, failed[f].ackId)) {
        allHttps = false;
        continue;
      }
      SerialPrint("SendData: HTTPS snsData ack to " + String(d->devName), true);
      if (!httpsAckOneServer(d->IP, jsonBuffer, failed[f].ackId)) allHttps = false;
    }
  }
  if (anyServer && allHttps) {
    for (uint8_t f = 0; f < nFail; ++f) {
      wrapupSendDataList(ordered + failed[f].off, failed[f].n);
    }
  }
  return anyServer && allHttps;
}
#endif

static bool finishSensorSend(bool normalOk, bool holdWrap, bool haveWifi,
    const int16_t* sendList, uint8_t sendCount, char* jsonBuffer) {
#if _I_AM_PERIPHERAL && _HAS_LOCAL_SENSORS
  if (holdWrap) {
    bool acked = false;
    if (haveWifi) {
      acked = deliverCriticalSensorAck(sendList, sendCount, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE);
    }
    if (acked && normalOk) wrapupSendDataList(sendList, sendCount);
    else if (!acked) I.makeBroadcast = true;
    return acked;
  }
#else
  (void)holdWrap;
  (void)haveWifi;
  (void)sendList;
  (void)sendCount;
  (void)jsonBuffer;
#endif
  return normalOk;
}

bool SendData(int16_t snsIndex, bool forceSend, int16_t sendToDeviceIndex, bool useUDP) {
  (void)useUDP;
  if (hardwareFaultBlocksLocalSensors()) return false;
  if (snsIndex >= 0 && !forceSend && !isSensorSendTime(snsIndex)) {
    return false;
  }
  if (snsIndex < 0 && !forceSend && !isSensorSendTime(-1)) {
    return false;
  }

  const bool haveWifi = wifiReadyForNetwork();
  // Finish the startup UDP exchange before the first sensor send chooses a transport.
  serviceBootUdpPresence(true);
  static char jsonBuffer[SNSDATA_JSON_BUFFER_SIZE];
  int16_t sendList[NUMSENSORS];
  // Pack without JSON limit first for mesh; apply JSON limit only for HTTP fallback.
  const uint8_t sendCount = packSensorsForSend(sendList, NUMSENSORS, forceSend, snsIndex,
      false, SNSDATA_JSON_BUFFER_SIZE, false);
  if (sendCount == 0) {
    return false;
  }

#if _I_AM_PERIPHERAL && _HAS_LOCAL_SENSORS
  const bool holdWrap = peripheralSendNeedsAck(sendList, sendCount);
#else
  const bool holdWrap = false;
#endif
  const bool markSent = !holdWrap;
  bool isGood = false;

  // Mesh ACK failed at boot: JSON over UDP until reboot. No mesh, and midnight does not undo this.
  if (s_udpSensorsUntilBoot && haveWifi) {
    IPAddress dest(0, 0, 0, 0);
    if (sendToDeviceIndex >= 0) {
      ArborysDevType* d = Sensors.getDeviceByDevIndex(sendToDeviceIndex);
      if (!d) return false;
      if (!isDeviceSendTime(d, forceSend)) return false;
      if (d->IP != IPAddress(0, 0, 0, 0)) dest = d->IP;
    }
    SerialPrint("SendData: UDP (ArborysMesh skipped until reboot)", true);
    isGood = sendSensorJsonViaUdp(sendList, sendCount, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, dest, markSent);
    if (isGood) {
      if (sendToDeviceIndex >= 0) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(sendToDeviceIndex);
        if (d) d->dataSent = utcNow();
      } else {
#ifndef _USELOWPOWER
        markAllServersDataSent();
#endif
      }
    } else if (!holdWrap) {
      I.makeBroadcast = true;
    }
    return finishSensorSend(isGood, holdWrap, haveWifi, sendList, sendCount, jsonBuffer);
  }

  // Directed HTTP response to one device (data-request)
  if (sendToDeviceIndex >= 0) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(sendToDeviceIndex);
    if (!d) return false;
    if (!isDeviceSendTime(d, forceSend)) return false;

    if (destinationNeedsLAN(d, haveWifi) || !haveWifi) {
      isGood = sendSensorDataMeshBundle(sendList, sendCount, haveWifi && expiredSendLadder() == 1);
      if (isGood) {
        d->dataSent = utcNow();
        if (markSent) wrapupSendDataList(sendList, sendCount);
      }
    } else {
      isGood = sendSensorJsonInFittingMessages(sendList, sendCount, forceSend, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, d, markSent);
    }
    if (!isGood && !holdWrap) {
      I.makeBroadcast = true;
    }
    return finishSensorSend(isGood, holdWrap, haveWifi, sendList, sendCount, jsonBuffer);
  }

  // 0: mesh broadcast. 1: mesh + UDP broadcast. 2: mesh + HTTP to each hub.
  // No WiFi: mesh only. Mesh success does not skip the added leg.
  const uint8_t ladder = expiredSendLadder();
  const bool alsoUdp = haveWifi && ladder == 1;
  SerialPrint(String("SendData: ArborysMesh TELEMETRY")
      + (alsoUdp ? " + UDP" : "")
      + ((ladder == 2 && haveWifi) ? " + HTTP hubs" : ""), true);
  bool meshOk = sendSensorDataMeshBundle(sendList, sendCount, alsoUdp);
  if (meshOk) {
#ifndef _USELOWPOWER
    markAllServersDataSent();
#endif
  }

  bool httpOk = false;
  if (ladder == 2 && haveWifi) {
    httpOk = sendListedSensorsHttpToHubs(sendList, sendCount, forceSend, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, markSent);
  } else if (!meshOk && haveWifi && ladder == 0) {
    // Radio send failed before the daily ladder has added a second path.
    httpOk = sendListedSensorsHttpToHubs(sendList, sendCount, forceSend, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, markSent);
  }

  // Mesh already carried every sensor. HTTP marks only the chunks that were posted.
  if (meshOk && markSent) {
    wrapupSendDataList(sendList, sendCount);
  } else if (!meshOk && !httpOk && !holdWrap) {
    I.makeBroadcast = true;
  }
  return finishSensorSend(meshOk || httpOk, holdWrap, haveWifi, sendList, sendCount, jsonBuffer);
}


int16_t sendMSG_ping(IPAddress& ip, bool viaHTTP) {
  char jsonBuffer[512];
  JSONbuilder_pingMSG(jsonBuffer, sizeof(jsonBuffer), viaHTTP, false);
  if (viaHTTP) {
    return sendHTTPJSON(ip, jsonBuffer, "pingMsg");
  } else {
    return sendUDPMessage((uint8_t*)jsonBuffer, ip, strlen(jsonBuffer), "pingMsg");
  }
}

int16_t sendMSG_networkStateReq(IPAddress& serverIP, uint16_t timeoutMs) {
  if (serverIP == IPAddress(0, 0, 0, 0) || !wifiReadyForNetwork()) return -1;

  String httpBody = "{\"msgType\":\"networkStateReq\"}";
  JSONbuilder_encodeHTTP(httpBody);

  char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", serverIP.toString().c_str());

  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(httpBody.c_str());
  M.timeout = timeoutMs;
  if (!M.initPayload(768)) return -1;

  if (!SendHTTPMessage(M)) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1;
  }
  registerHTTPSend(serverIP, "netState");

  if (!M.payload || !M.payload.get() || M.payload.get()[0] != '{') return -1;

  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, M.payload.get()) != DeserializationError::Ok) return -1;
  if (strcmp(doc["msgType"] | "", "networkState") != 0) return -1;
  if (doc["error"].is<const char*>()) return -2;

  int16_t registered = 0;
  JsonArray ips = doc["serverIPs"].as<JsonArray>();
  if (!ips.isNull()) {
    for (JsonVariant v : ips) {
      IPAddress ip;
      if (!ip.fromString(v.as<const char*>())) continue;
      if (Sensors.addServerPlaceholder(ip) >= 0) registered++;
    }
  }
  const int count = doc["serverCount"] | registered;
  return (int16_t)count;
}

int16_t sendMSG_sunReq(IPAddress& serverIP, uint32_t* sunriseUtc, uint32_t* sunsetUtc, uint16_t timeoutMs) {
  if (sunriseUtc) *sunriseUtc = 0;
  if (sunsetUtc) *sunsetUtc = 0;
  if (serverIP == IPAddress(0, 0, 0, 0) || !wifiReadyForNetwork()) return -1;

  String httpBody = "{\"msgType\":\"sunReq\"}";
  JSONbuilder_encodeHTTP(httpBody);

  char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", serverIP.toString().c_str());

  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(httpBody.c_str());
  M.timeout = timeoutMs;
  if (!M.initPayload(256)) return -1;

  if (!SendHTTPMessage(M)) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1;
  }
  registerHTTPSend(serverIP, "sunReq");

  if (!M.payload || !M.payload.get() || M.payload.get()[0] != '{') return -1;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, M.payload.get()) != DeserializationError::Ok) return -3;
  if (strcmp(doc["msgType"] | "", "sunAck") != 0) return -3;
  if (doc["error"].is<const char*>()) return -2;

  const uint32_t rise = doc["sunrise"] | 0u;
  const uint32_t set = doc["sunset"] | 0u;
  if (rise < (uint32_t)TIMEZERO || set < (uint32_t)TIMEZERO) return -2;
  if (sunriseUtc) *sunriseUtc = rise;
  if (sunsetUtc) *sunsetUtc = set;
  return 0;
}

int16_t requestSunTimesFromType100(uint32_t& sunriseUtc, uint32_t& sunsetUtc) {
  sunriseUtc = 0;
  sunsetUtc = 0;
  bool haveServer = false;
  int16_t idx = Sensors.nextServerIndex(0, true);
  while (idx >= 0) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(idx);
    if (d && d->IsSet && !d->expired && d->IP != IPAddress(0, 0, 0, 0)) {
      haveServer = true;
      if (sendMSG_sunReq(d->IP, &sunriseUtc, &sunsetUtc) == 0) return 1;
    }
    idx = Sensors.nextServerIndex(idx + 1, true);
  }
  return haveServer ? (int16_t)-1 : (int16_t)0;
}

int16_t sendMSG_alarmsReq(IPAddress& serverIP, String& responseOut, uint16_t timeoutMs) {
  responseOut = "";
  if (serverIP == IPAddress(0, 0, 0, 0) || !wifiReadyForNetwork()) return -1;

  ArborysDevType* me = Sensors.getDeviceByMAC(ESP.getEfuseMac());
  String httpBody;
  if (me) {
    httpBody = "{\"msgType\":\"alarmsReq\",";
    httpBody += JSONbuilder_device(me);
    httpBody += '}';
  } else {
    httpBody = "{\"msgType\":\"alarmsReq\"}";
  }
  JSONbuilder_encodeHTTP(httpBody);

  char urlBuffer[64];
  snprintf(urlBuffer, sizeof(urlBuffer), "http://%s/POST", serverIP.toString().c_str());

  HTTPMessage M;
  M.setUrl(urlBuffer);
  M.setMethod("POST");
  M.setContentType("application/x-www-form-urlencoded");
  M.setBody(httpBody.c_str());
  M.timeout = timeoutMs;
  // Alarms payloads can be large; allocate enough for a full alarmsAck.
  if (!M.initPayload(ALARMS_ACK_MAX_JSON_BYTES + 256)) return -1;

  if (!SendHTTPMessage(M)) {
    I.HTTP_OUTGOING_ERRORS++;
    return -1;
  }
  registerHTTPSend(serverIP, "alarmsReq");

  if (!M.payload || !M.payload.get() || M.payload.get()[0] != '{') return -1;
  responseOut = String(M.payload.get());

  // Light parse for count / error (avoid loading the full sensor array into a JsonDocument).
  if (responseOut.indexOf("\"msgType\":\"alarmsAck\"") < 0) {
    responseOut = "";
    return -3;
  }
  // Peripherals include error:"notServer" but still return their own alarmed sensors.
  // Only treat other errors as hard failures.
  const bool notServer = (responseOut.indexOf("\"error\":\"notServer\"") >= 0);
  if (!notServer && responseOut.indexOf("\"error\"") >= 0) {
    return -2;
  }

  int count = 0;
  const int countKey = responseOut.indexOf("\"count\":");
  if (countKey >= 0) {
    count = responseOut.substring(countKey + 8).toInt();
  }
  return (int16_t)count;
}

int16_t sendMSG_DataRequest(int16_t deviceIndex, int16_t snsIndex, bool viaHTTP) {
  //send a data request to a specific device and sensor, or use snsIndex = -1 to request all sensors
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);  
  return sendMSG_DataRequest(d, snsIndex, viaHTTP);
}

// Async HTTP/HTTPS snsReq: queue to a worker so the main loop never waits on SendHTTPMessage.
#if defined(_USE32)
static constexpr uint16_t SNSREQ_HTTP_TIMEOUT_MS = 2500;
static constexpr UBaseType_t SNSREQ_QUEUE_DEPTH = 4;

struct SnsReqHttpJob {
  uint32_t ipAddr;
  char json[512];
};

static QueueHandle_t s_snsReqQueue = nullptr;
static TaskHandle_t s_snsReqTask = nullptr;

static void snsReqHttpWorkerTask(void* /*arg*/) {
  SnsReqHttpJob job;
  for (;;) {
    if (xQueueReceive(s_snsReqQueue, &job, portMAX_DELAY) != pdTRUE) continue;
    IPAddress ip(job.ipAddr);
    SerialPrint("snsReq worker: HTTP/HTTPS to " + ip.toString(), true);
    sendJsonViaPreferredHttp(ip, job.json, "snsReq", SNSREQ_HTTP_TIMEOUT_MS);
  }
}

static bool ensureSnsReqHttpWorker() {
  if (s_snsReqQueue) return true;
  s_snsReqQueue = xQueueCreate(SNSREQ_QUEUE_DEPTH, sizeof(SnsReqHttpJob));
  if (!s_snsReqQueue) return false;
  // Pin to Arduino core (1) at low priority so WiFi/HTTP stay coherent with the main loop.
  BaseType_t ok = xTaskCreatePinnedToCore(
      snsReqHttpWorkerTask, "snsReqHttp", 12288, nullptr, 1, &s_snsReqTask, 1);
  if (ok != pdPASS) {
    vQueueDelete(s_snsReqQueue);
    s_snsReqQueue = nullptr;
    s_snsReqTask = nullptr;
    return false;
  }
  return true;
}

static bool queueSnsReqHttp(IPAddress ip, const char* json) {
  if (!json || json[0] == '\0') return false;
  if (!ensureSnsReqHttpWorker()) return false;
  SnsReqHttpJob job = {};
  job.ipAddr = (uint32_t)ip;
  strncpy(job.json, json, sizeof(job.json) - 1);
  job.json[sizeof(job.json) - 1] = '\0';
  if (xQueueSend(s_snsReqQueue, &job, 0) != pdTRUE) {
    SerialPrint("snsReq worker: queue full, dropping request to " + ip.toString(), true);
    return false;
  }
  return true;
}
#endif

int16_t sendMSG_DataRequest(ArborysDevType* d, int16_t snsIndex, bool viaHTTP) { //snsindex is which sensor we want, or -1 for all sensors
  if (!d) {
    SerialPrint("sendMSG_DataRequest: Device not found", true);
    return -1002; //device not found
  }

  if (bitRead(d->Flags,2) == 1) {
    SerialPrint("sendMSG_DataRequest: Device is low power and will not send data", true);
    return -1003; //device is low power and will not see this data request
  }

  char jsonBuffer[512];
  // Build raw JSON; HTTP form-encoding is applied only for plain HTTP fallback.
  JSONbuilder_DataRequestMSG(jsonBuffer, sizeof(jsonBuffer), false, snsIndex);
  SerialPrint("sendMSG_DataRequest: " + String(jsonBuffer), true);
  SerialPrint("sendMSG_DataRequest sent to: " + String(d->IP.toString()), true);
  d->dataSent = utcNow();
  if (viaHTTP) {
    #if defined(_USE32)
    if (queueSnsReqHttp(d->IP, jsonBuffer)) {
      return 1; // queued async
    }
    // Fallback: short sync send if the worker/queue is unavailable.
    if (sendJsonViaPreferredHttp(d->IP, jsonBuffer, "snsReq", SNSREQ_HTTP_TIMEOUT_MS)) {
      return 200;
    }
    return -1000;
    #else
    if (sendJsonViaPreferredHttp(d->IP, jsonBuffer, "snsReq", 2500)) {
      return 200;
    }
    return -1000;
    #endif
  }
  return sendUDPMessage((uint8_t*)jsonBuffer, d->IP, strlen(jsonBuffer), "snsReq") ? 1 : -1000;
}

//___________________END OF HTTP SEND HANDLERS___________________

String getWiFiModeString() {
  //possible modes are WIFI_MODE_OFF, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA
  switch (WiFi.getMode()) {
    case WIFI_MODE_NULL:
      return "WIFI_MODE_OFF";
    case WIFI_MODE_STA:
      return "WIFI_MODE_STA";
    case WIFI_MODE_AP:
      return "WIFI_MODE_AP";
    case WIFI_MODE_APSTA:
      return "WIFI_MODE_APSTA";
  }
  return "UNKNOWN";
}



// Complete address to coordinates conversion using US Census Bureau Geocoding API
bool getCoordinatesFromAddress(const String& zipCode, const String& street, const String& city, const String& state) {

  double lat = 0, lon = 0;

  // Validate ZIP code format (5 digits)
  if (zipCode.length() != 5) {
      SerialPrint("Invalid ZIP code format. Must be 5 digits.", true);
      return false;
  }
  
  for (int i = 0; i < 5; i++) {
      if (!isdigit(zipCode.charAt(i))) {
          SerialPrint("Invalid ZIP code format. Must contain only digits.", true);
          return false;
      }
  }
  
  // Validate state format (2 letters)
  if (state.length() != 2) {
      SerialPrint("Invalid state format. Must be 2 letters.", true);
      return false;
  }
  
  // Build the URL for the Census Bureau Geocoding API
  String url = "https://geocoding.geo.census.gov/geocoder/locations/address?";
  url += "street=" + urlEncode(street);
  url += "&city=" + urlEncode(city);
  url += "&state=" + urlEncode(state);
  url += "&zip=" + zipCode;
  url += "&benchmark=Public_AR_Current&format=json";
  
  JsonDocument doc;
  
  SerialPrint(("Fetching coordinates for address: " + street + ", " + city + ", " + state + " " + zipCode).c_str(), true);
  SerialPrint(("API URL: " + url).c_str(), true);


  HTTPMessage M;
  M.setUrl(url.c_str());
  M.setMethod("GET");
  M.setContentType("application/json");
  M.timeout = 5000; // 5 second timeout for geocoding
  M.usePSRAM = false;
  M.responseDoc = &doc;
  if (SendHTTPMessage(M)) {
      // Check if we have address matches
      if (doc["result"]["addressMatches"].is<JsonArray>()) {
        JsonArray addressMatches = doc["result"]["addressMatches"];
        
        if (addressMatches.size() > 0) {
            // Get the first (best) match
            JsonObject match = addressMatches[0];
            
            if (match["coordinates"]["x"].is<double>() && match["coordinates"]["y"].is<double>()) {
                lon = match["coordinates"]["x"].as<double>();
                lat = match["coordinates"]["y"].as<double>();
                
                SerialPrint(("Coordinates found: " + String(lat, 6) + ", " + String(lon, 6)).c_str(), true);
                Prefs.LATITUDE = lat;
                Prefs.LONGITUDE = lon;
                Prefs.isUpToDate = false;
                // Log the matched address for verification
                if (match["matchedAddress"].is<String>()) {
                    String matchedAddress = match["matchedAddress"].as<String>();
                    SerialPrint(("Matched Address: " + matchedAddress).c_str(), true);
                }
                
                return true;
            }
        }
      } 
  }
  SerialPrint("Falling back to ZIP code only method", true);
  return getCoordinatesFromZipCode(zipCode);
}


// Fallback method using a simple geocoding service
bool getCoordinatesFromZipCode(const String& zipCode) {
  // Use a simple geocoding service (example with a free API)
  // Note: This is a simplified approach. In production, you might want to use
  // a more reliable service like Google Geocoding API (requires API key)
  
  SerialPrint(("Using fallback geocoding service for ZIP: " + zipCode).c_str(), true);

  double lat = 0, lon = 0;
  String url = "http://api.zippopotam.us/US/" + zipCode;
  
  JsonDocument doc;
  HTTPMessage M;
  M.setUrl(url.c_str());
  M.setMethod("GET");
  M.setContentType("application/json");
  M.timeout = 5000; // 5 second timeout for geocoding
  M.usePSRAM = false;
  M.responseDoc = &doc;
  if (SendHTTPMessage(M)) {
    if (doc["places"][0]["latitude"].is<String>() && doc["places"][0]["longitude"].is<String>()) {
      lat = doc["places"][0]["latitude"].as<double>();
      lon = doc["places"][0]["longitude"].as<double>();
      Prefs.LATITUDE = lat;
      Prefs.LONGITUDE = lon;
      Prefs.isUpToDate = false;

      // Save to NVS now that we have the coordinates
      BootSecure bootSecure;
      int8_t ret = bootSecure.setPrefs();
      if (ret < 0) {
        SerialPrint("getCoordinatesFromZipCode: Failed to save Prefs to NVS (error " + String(ret) + ")", true);
      }
      
      return true;
    }
    SerialPrint("Fallback geocoding failed due to no coordinates found", true);
    storeError("Fallback geocoding failed due to no coordinates found", ERROR_JSON_GEOCODING,true);

    return false;
  } else {
    SerialPrint(("Fallback geocoding failed with HTTP code: " + String(M.httpCode)).c_str(), true);
    storeError("Fallback geocoding failed with HTTP code: " + String(M.httpCode), ERROR_JSON_GEOCODING,true);
    return false;
  }
return false;
} 

#ifdef _USEUDP
//In addition to ESPnow, I will also use UDP for all of the above message types
//In addition to ESPnow, I will also use UDP for all of the above message types

bool closeUDP(bool returnStatus) {
  #ifdef _USEUDP
  LAN_UDP.clear(); //clear the buffer to avoid reading the same message twice
  if (returnStatus) {
    I.UDP_LAST_INCOMINGMSG_TIME = utcNow();
    I.UDP_RECEIVES++;
  }

  return returnStatus;
  #else
  return false;
  #endif
}

bool receiveUDPMessage() {
  //receive a message via UDP
  //return true if message is received, false if no message is received
  #ifdef _USEUDP

  int packetSize = LAN_UDP.parsePacket(); //>0 if message received!

  if (packetSize > 0) {
    if (packetSize > 8192) {
      SerialPrint("UDP message is too large: " + String(packetSize) + " bytes (max: 8192)", true);
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "TooLarge");
      return closeUDP(false);
    }

    IPAddress remoteIP = LAN_UDP.remoteIP();
    if (remoteIP == WiFi.localIP()) {
      return closeUDP(false);  // ignore self-sent packets
    }

    SerialPrint("UDP message from: " + remoteIP.toString(), true);
    registerUDPMessage(remoteIP, 0);

    char* buffer = (char*)malloc(packetSize + 1);
    if (!buffer) {
      SerialPrint("UDP message: Failed to allocate buffer for " + String(packetSize) + " bytes", true);
      I.UDP_INCOMING_ERRORS++;
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "AllocFail");
      return closeUDP(false);
    }
    size_t bytesRead = LAN_UDP.read((uint8_t*)buffer, packetSize);

    if (bytesRead != packetSize) {
      SerialPrint("UDP message: Read " + String(bytesRead) + " bytes, expected " + String(packetSize), true);
      free(buffer);
      I.UDP_INCOMING_ERRORS++;
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "ReadFail");
      return closeUDP(false);
    }

    // ArborysMesh binary (network_id 103) vs JSON
    const bool isMesh = (packetSize >= ARBORYS_MESH_HEADER_SIZE &&
                         (uint8_t)buffer[0] == ARBORYS_MESH_NETWORK_ID);

    if (isMesh) {
      meshOnRawFrame((const uint8_t*)buffer, (uint16_t)packetSize, true);
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "ArborysMesh");
    } else {
      #if !_HAS_LOCAL_SENSORS && _IS_SERVER_HUB
      if (_I_AM_PERIPHERAL) {
        free(buffer);
        return closeUDP(true);
      }
      #endif
      buffer[packetSize] = '\0';
      String responseMsg = "OK";
      String postData = (String)buffer;
      registerUdpMsgTypeFromJson(postData, remoteIP);
      pushJsonPingReplyContext(JSON_PING_REPLY_UDP, remoteIP);
      processJSONMessage(postData, responseMsg);
      popJsonPingReplyContext();
    }

    free(buffer);
    return closeUDP(true);

  }
  #endif
  return false;
}

bool sendUDPMessage(const uint8_t* buffer,  IPAddress ip, uint16_t bufferSize, const char* msgType) {
  //send the provided jsonbuffer via UDP, including directed broadcast
  //broadcasts are 192.168.68.255 or multicast ip, but I will accept 0.0.0.0 or 255.255.255.255 as well
  //return true if successful, false if failed
  #ifdef _USEUDP
  SerialPrint("Buffer contains: " + String((char*)buffer),true);
  // No inter-send delay: UDP is fire-and-forget; callers that need pacing do so themselves
  // (e.g. serviceExpiredDeviceDataRequests: one device per second).
  
  // Calculate buffer size if not provided
  if (bufferSize == 0) {
    // Only use strlen if buffer is guaranteed to be null-terminated string
    // For binary data, bufferSize must be provided explicitly
    bufferSize = strlen((const char*)buffer);
    if (bufferSize == 0) {
      SerialPrint("sendUDPMessage: Invalid buffer size (0)", true);
      I.UDP_OUTGOING_ERRORS++;
      return false;
    }
  }

  // Normalize broadcast addresses to multicast group
  if (ip == IPAddress(0,0,0,0) || ip == IPAddress(255,255,255,255)) {
    ip = IPAddress(_USEUDP_MULTICAST); //broadcast to multicast group 
  }

  SerialPrint("Sending UDP message to " + ip.toString() + ", size: " + String(bufferSize) + " bytes", true);
  
  // beginPacket returns void, so we can't check for errors here
  LAN_UDP.beginPacket(ip, _USEUDP);
  
  // Write data and check if all bytes were written
  size_t bytesWritten = LAN_UDP.write(buffer, bufferSize);
  if (bytesWritten != bufferSize) {
    SerialPrint("sendUDPMessage: write failed - wrote " + String(bytesWritten) + " of " + String(bufferSize) + " bytes", true);
    I.UDP_OUTGOING_ERRORS++;
    return false;
  }
  
  // endPacket returns size_t (bytes sent), 0 indicates failure
  size_t bytesSent = LAN_UDP.endPacket();
  if (bytesSent == 0) {
    SerialPrint("sendUDPMessage: endPacket failed (returned 0)", true);
    I.UDP_OUTGOING_ERRORS++;
    return false;
  }
  
  // Success - register the send
  registerUDPSend(ip, msgType);
  return true;
  #else
  I.UDP_OUTGOING_ERRORS++;
  return false;
  #endif
}

void registerUDPMessage(IPAddress ip, const char* messageType) {
  I.UDP_LAST_INCOMINGMSG_FROM_IP = ip;
  if (messageType != 0)   snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), messageType);
  //I.UDP_LAST_INCOMINGMSG_TIME = utcNow();
  //I.UDP_RECEIVES++;
}

void registerUDPSend(IPAddress ip, const char* messageType) {
  I.UDP_LAST_OUTGOINGMSG_TO_IP = ip;
  snprintf(I.UDP_LAST_OUTGOINGMSG_TYPE, sizeof(I.UDP_LAST_OUTGOINGMSG_TYPE), messageType);
  I.UDP_LAST_OUTGOINGMSG_TIME = utcNow();  
  I.UDP_SENDS++;
}

void registerHTTPMessage(const char* messageType) {
  if (isHttpUiBrowseMessage(messageType)) return;
  I.HTTP_LAST_INCOMINGMSG_TIME = utcNow();
  snprintf(I.HTTP_LAST_INCOMINGMSG_TYPE, sizeof(I.HTTP_LAST_INCOMINGMSG_TYPE), "%s", messageType);
  I.HTTP_LAST_INCOMINGMSG_FROM_IP = server.client().remoteIP();
  I.HTTP_RECEIVES++;
}

void registerHTTPSend(IPAddress ip, const char* messageType) {
  I.HTTP_LAST_OUTGOINGMSG_TO_IP = ip;
  I.HTTP_LAST_OUTGOINGMSG_TIME = utcNow();
  I.HTTP_SENDS++;
  snprintf(I.HTTP_LAST_OUTGOINGMSG_TYPE, sizeof(I.HTTP_LAST_OUTGOINGMSG_TYPE), messageType);
}
void delayWithNetwork(uint16_t delayTime, uint8_t maxChecks) {
//do not delay a highspeed device, such as a TFLuna, as it will lock out wifi
  for (uint8_t i=0; i<maxChecks; i++) {
    delay(delayTime);
    serviceESPNOWRecvQueue();
    receiveUDPMessage();
    server.handleClient();
  }
}
#endif

