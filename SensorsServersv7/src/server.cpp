#include "globals.hpp"
#include "server.hpp"
#include "Devices.hpp"
#include "SDCard.hpp"
#ifdef _USENETWORKMONITOR
#if _USENETWORKMONITOR > 0
#include "NetworkMonitor.hpp"
#endif
#endif
#ifdef _USETFT
  #ifdef _ISCLOCK480X480
    #include "Clock480X480.hpp"
  #else
    #include "graphics.hpp"
  #endif
  extern LGFX tft;
  extern STRUCT_GRAPHICS GRAPHICS;
#endif

#ifdef _USELEDMATRIX
#include "LEDMatrix.hpp"
#endif

#include "BootSecure.hpp"
#include "AddESPNOW.hpp"
#include "firmwareUpdate.hpp"

#include <ssl_client.h> // Ensure this is at the top of server.cpp


#include <string.h> // For memset
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>


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

  if (isTimeValid(I.currentTime) && isTimeValid(I.UDP_LAST_INCOMINGMSG_TIME)
      && I.currentTime - I.UDP_LAST_INCOMINGMSG_TIME > IGMP_STALE_UDP_SEC) {
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
  if (!isTimeValid(I.currentTime)) {
    return;
  }
  if (s_lastIgmpRefreshTime != 0
      && I.currentTime - s_lastIgmpRefreshTime < IGMP_REFRESH_INTERVAL_SEC) {
    return;
  }
  s_lastIgmpRefreshTime = I.currentTime;
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
  return (t > 0) ? dateify(t, "mm/dd/yyyy hh:nn:ss") : String("???");
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

static void appendCommunicationsSection() {
  WEBHTML += "<h3>Communications</h3>";
  appendBroadcastForm("/REQUEST_BROADCAST", "Broadcast Now (ESPLan + UDPLan)");

  WEBHTML += "<h4>ESPLan</h4>";
  appendBroadcastForm("/REQUEST_BROADCAST_ESP", "Broadcast ESPLan", "#4CAF50");
  WEBHTML += "<table style=\"width: 100%; border-collapse: collapse;\">";
  {
    String sender = MACToString(I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC);
    if (I.ESPNOW_LAST_INCOMINGMSG_FROM_IP != IPAddress(0, 0, 0, 0)) {
      sender += " / " + I.ESPNOW_LAST_INCOMINGMSG_FROM_IP.toString();
    }
    appendCommTableRow("Last incoming time", formatCommTime(I.ESPNOW_LAST_INCOMINGMSG_TIME));
    appendCommTableRow("Last incoming type", String(I.ESPNOW_LAST_INCOMINGMSG_TYPE));
    appendCommTableRow("Last incoming sender", sender);
    appendCommTableRow("Total incoming today", String(I.ESPNOW_RECEIVES));
    appendCommTableRow("Last outgoing time", formatCommTime(I.ESPNOW_LAST_OUTGOINGMSG_TIME));
    appendCommTableRow("Last outgoing type", String(I.ESPNOW_LAST_OUTGOINGMSG_TYPE));
    appendCommTableRow("Last outgoing target", MACToString(I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC));
    appendCommTableRow("Total outgoing today", String(I.ESPNOW_SENDS));
  }
  WEBHTML += "</table>";

  #ifdef _USEUDP
  WEBHTML += "<h4>UDPLan</h4>";
  appendBroadcastForm("/REQUEST_BROADCAST_UDP", "Broadcast UDPLan", "#FF9800");
  WEBHTML += "<table style=\"width: 100%; border-collapse: collapse;\">";
  appendCommTableRow("Last incoming time", formatCommTime(I.UDP_LAST_INCOMINGMSG_TIME));
  appendCommTableRow("Last incoming type", String(I.UDP_LAST_INCOMINGMSG_TYPE));
  appendCommTableRow("Last incoming sender IP", I.UDP_LAST_INCOMINGMSG_FROM_IP.toString());
  appendCommTableRow("Total incoming today", String(I.UDP_RECEIVES));
  appendCommTableRow("Last outgoing time", formatCommTime(I.UDP_LAST_OUTGOINGMSG_TIME));
  appendCommTableRow("Last outgoing type", String(I.UDP_LAST_OUTGOINGMSG_TYPE));
  appendCommTableRow("Last outgoing target IP", I.UDP_LAST_OUTGOINGMSG_TO_IP.toString());
  appendCommTableRow("Total outgoing today", String(I.UDP_SENDS));
  WEBHTML += "</table>";
  #endif

  WEBHTML += "<h4>HTTP</h4>";
  WEBHTML += "<table style=\"width: 100%; border-collapse: collapse;\">";
  appendCommTableRow("Last incoming time", formatCommTime(I.HTTP_LAST_INCOMINGMSG_TIME));
  appendCommTableRow("Last incoming type", String(I.HTTP_LAST_INCOMINGMSG_TYPE));
  appendCommTableRow("Last incoming sender IP", I.HTTP_LAST_INCOMINGMSG_FROM_IP.toString());
  appendCommTableRow("Total incoming today", String(I.HTTP_RECEIVES));
  appendCommTableRow("Last outgoing time", formatCommTime(I.HTTP_LAST_OUTGOINGMSG_TIME));
  appendCommTableRow("Last outgoing type", String(I.HTTP_LAST_OUTGOINGMSG_TYPE));
  appendCommTableRow("Last outgoing target IP", I.HTTP_LAST_OUTGOINGMSG_TO_IP.toString());
  appendCommTableRow("Total outgoing today", String(I.HTTP_SENDS));
  WEBHTML += "</table>";
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
    "404", "Broadcast", "STATUS",
    "SnsOvrd", "SnsUpd", "ReadReq", "API_SNS_READ_NOW",
  };
  for (const char* uiType : kUiBrowseTypes) {
    if (strcmp(messageType, uiType) == 0) return true;
  }
  return false;
}

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
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      logSystemEvent("STA disconnected from AP", EVENT_WIFI_DISCONNECTED);
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
    storeError("SendHTTPMessage: Failed with code: " + String(M.httpCode) + " for " + String(M.url.get()), errType, true);
    M.success = false;
    http.end();

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
    if (!M.success) {
      storeError("SendHTTPMessage: Empty body for " + String(M.url.get()), ERROR_JSON_PARSE, true);
    }
    http.end();
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
        storeError("SendHTTPMessage: Failed to initialize payload for " + String(M.url.get()), ERROR_HTTP_RESPONSE,true);
        M.success = false;
        http.end();
        return false;
      }
    } else {
      if (M.payloadSize < serverSize) {
        if (!M.resizePayload(serverSize + 1)) {
          SerialPrint("SendHTTPMessage: Failed to resize payload for " + String(M.url.get()) + " with size " + String(serverSize + 1), true);
          storeError("SendHTTPMessage: Failed to resize payload for " + String(M.url.get()) + " with size " + String(serverSize + 1), ERROR_HTTP_RESPONSE,true);
          M.success = false;
          http.end();
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
  //2 - connected by all measures, but wifi.status() != WL_CONNECTED
  //1 - valid status (WL_CONNECTED)
  //0 - unknown status
  //-1 - no valid IP address
  //-2 - no valid RSSI range
  //-3 - no valid SSID
  //-4 - no valid gateway

  int32_t rssi = WiFi.RSSI();

  if (WiFi.status() == WL_CONNECTED) {
    I.WiFiStatus = 1;
    return 1;
  }
  I.WiFiStatus = 0;

  if (WiFi.SSID().length() <= 0) {
    I.WiFiStatus = -3;
  } else if (WiFi.gatewayIP() == IPAddress(0, 0, 0, 0)) {
    I.WiFiStatus = -4;
  } else if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    I.WiFiStatus = -1;
  } else if (!(rssi < 0 && rssi > -150)) {
    I.WiFiStatus = -2;
  } else {
    I.WiFiStatus = 2;
  }
  return I.WiFiStatus;
}

void updateRSSI(bool forceUpdate) {
  constexpr time_t RSSI_POLL_INTERVAL_SEC = 5;
  if (!forceUpdate && I.lastRSSItime != 0 && I.currentTime - I.lastRSSItime < RSSI_POLL_INTERVAL_SEC) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    I.RSSIcurrent = -999;
    I.lastRSSItime = I.currentTime;
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
  I.lastRSSItime = I.currentTime;
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
  if (connected) {
    I.wifiDownSince = 0;
    I.wifiFailCount = 0;
    return;
  }
  if (I.wifiDownSince == 0 && isTimeValid(I.currentTime)) {
    I.wifiDownSince = I.currentTime;
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

// Scan for Prefs.WIFISSID and return the strongest AP. Does not persist the BSSID.
static bool findBestApForConfiguredSsid(WifiApCandidate& out) {
  out = WifiApCandidate{};
  if (Prefs.WIFISSID[0] == '\0') return false;

  esp_task_wdt_reset();
  const int numNetworks = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  esp_task_wdt_reset();
  if (numNetworks <= 0) {
    WiFi.scanDelete();
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
  return out.found;
}

// Begin STA using the strongest BSSID for the configured SSID when a scan finds one.
// Falls back to SSID-only begin() if the scan finds nothing. BSSID is not stored in Prefs.
static void beginWifiPreferBestBssid() {
  WifiApCandidate best;
  if (findBestApForConfiguredSsid(best) && best.channel > 0) {
    SerialPrint("WiFi: joining strongest BSSID " + bssidToString(best.bssid) +
        " ch=" + String(best.channel) + " rssi=" + String(best.rssi) +
        " for SSID " + String(Prefs.WIFISSID), true);
    WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD, best.channel, best.bssid, true);
    return;
  }

  SerialPrint("WiFi: no BSSID scan match for " + String(Prefs.WIFISSID) +
      "; joining by SSID only", true);
  WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD);
}

static bool initialSetupRequirementsMet() {
  if (!haveWifiCredentials()) return false;
  if (Prefs.TimeZoneOffset > 50400) return false;
  #ifdef _USEWEATHER
  if (Prefs.LATITUDE == 0 && Prefs.LONGITUDE == 0) return false;
  #endif
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

int8_t CheckWifiStatus(WifiCheckMode mode) {
  const int8_t linkStatus = measureWifiLinkStatus();
  const bool connected = (linkStatus >= 1);

  syncWifiDownFlags(connected);
  syncDeviceIPFromWifi();

  if (connected) {
    maybeExitAPStationMode();
    return linkStatus;
  }

  if (mode == WIFI_CHECK_BOOT) {
    if (!haveWifiCredentials()) {
      enterAPStationMode();
      return linkStatus;
    }
    for (uint8_t attempt = 1; attempt <= WIFI_BOOT_RETRY_LIMIT; ++attempt) {
      #ifdef _USETFT
      tftPrint("WiFi attempt " + String(attempt) + "/" + String(WIFI_BOOT_RETRY_LIMIT) + ", please wait",
          true, TFT_WHITE, 2, 1, false, -1, -1);
      #endif
      SerialPrint("Boot WiFi attempt " + String(attempt) + "/" + String(WIFI_BOOT_RETRY_LIMIT), true);
      if (tryWifi(WIFI_BOOT_TRY_MS, true) == 1) {
        syncWifiDownFlags(true);
        syncDeviceIPFromWifi();
        return measureWifiLinkStatus();
      }
    }
    SerialPrint("Boot WiFi failed after " + String(WIFI_BOOT_RETRY_LIMIT) + " attempts; entering AP mode", true);
    enterAPStationMode();
    return linkStatus;
  }

  // Runtime: ESP-IDF auto-reconnect handles brief STA drops. After WIFI_DOWN_AP_THRESHOLD_SEC
  // of continuous failure, open soft-AP so credentials can be updated; keep AP up until STA recovers.
  if (!haveWifiCredentials()) {
    if (!softApRunning()) {
      enterAPStationMode();
    }
    return linkStatus;
  }

  if (I.wifiDownSince && isTimeValid(I.currentTime)
      && (I.currentTime - I.wifiDownSince >= WIFI_DOWN_AP_THRESHOLD_SEC)) {
    if (!softApRunning()) {
      SerialPrint("WiFi down > " + String(WIFI_DOWN_AP_THRESHOLD_SEC) + "s; entering AP mode", true);
      enterAPStationMode();
    }
  }

  return linkStatus;
}

bool wifiReadyForNetwork() {
  return measureWifiLinkStatus() == 1;
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
  WiFi.persistent(true);
  
  // Set WiFi security preferences for WPA2/WPA3 compatibility
  // WIFI_AUTH_WPA2_PSK = WPA2 only
  // WIFI_AUTH_WPA3_PSK = WPA3 only  
  // WIFI_AUTH_WPA2_WPA3_PSK = WPA2/WPA3 mixed (transition mode) - preferred for compatibility
  // Note: The WiFi.begin() call will automatically negotiate, but we can set preferences
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
      tftPrint("Failed WiFi", true);

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
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  #endif
  // Preserve AP+STA if soft-AP is already up; never force a mode flip here.
  if (softApRunning()) {
    if (WiFi.getMode() != WIFI_MODE_APSTA) {
      WiFi.mode(WIFI_MODE_APSTA);
    }
  } else if (WiFi.getMode() != WIFI_MODE_STA && WiFi.getMode() != WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_MODE_STA);
  }
  beginWifiPreferBestBssid();
  WiFi.setSleep(WIFI_PS_NONE);
  s_lastBeginMs = nowMs;
  SerialPrint("startWifiConnectAsync: non-blocking STA reconnect started", true);
}

void maybeOptimizeWifiBssid() {
  static time_t s_lastOptimizeTime = 0;

  if (!haveWifiCredentials()) return;
  if (!wifiReadyForNetwork()) return;
  if (!isTimeValid(I.currentTime)) return;

  // Start the 30-minute clock on first eligible call; do not rescan immediately after boot connect.
  if (s_lastOptimizeTime == 0) {
    s_lastOptimizeTime = I.currentTime;
    return;
  }
  if (I.currentTime >= s_lastOptimizeTime
      && (I.currentTime - s_lastOptimizeTime) < WIFI_BSSID_OPTIMIZE_INTERVAL_SEC) {
    return;
  }
  s_lastOptimizeTime = I.currentTime;

  const uint8_t* currentBssid = WiFi.BSSID();
  if (!currentBssid) {
    SerialPrint("WiFi BSSID optimize: no current BSSID; skipping", true);
    return;
  }

  uint8_t currentCopy[6];
  memcpy(currentCopy, currentBssid, 6);
  const int32_t currentRssi = WiFi.RSSI();

  WifiApCandidate best;
  if (!findBestApForConfiguredSsid(best)) {
    SerialPrint("WiFi BSSID optimize: scan found no APs for " + String(Prefs.WIFISSID), true);
    return;
  }

  const bool sameAp = (memcmp(best.bssid, currentCopy, 6) == 0);
  if (sameAp) {
    SerialPrint("WiFi BSSID optimize: already on strongest AP " +
        bssidToString(best.bssid) + " rssi=" + String(best.rssi), true);
    return;
  }

  const int32_t improvement = best.rssi - currentRssi;
  if (improvement < WIFI_BSSID_ROAM_MIN_IMPROVEMENT_DB) {
    SerialPrint("WiFi BSSID optimize: stronger AP " + bssidToString(best.bssid) +
        " rssi=" + String(best.rssi) + " only +" + String(improvement) +
        " dB vs current " + bssidToString(currentCopy) +
        " rssi=" + String(currentRssi) + "; keeping current", true);
    return;
  }

  SerialPrint("WiFi BSSID optimize: roaming " + bssidToString(currentCopy) +
      " rssi=" + String(currentRssi) + " -> " + bssidToString(best.bssid) +
      " rssi=" + String(best.rssi) + " ch=" + String(best.channel), true);
  WiFi.begin((char*)Prefs.WIFISSID, (char*)Prefs.WIFIPWD, best.channel, best.bssid, true);
  WiFi.setSleep(WIFI_PS_NONE);
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
  volatile bool s_apChannelScanListen = false;
  volatile bool s_apChannelScanGotResponse = false;

  bool apEspNowStaleFor(uint32_t seconds) {
    if (!isTimeValid(I.ESPNOW_LAST_INCOMINGMSG_TIME)) return true;
    if (!isTimeValid(I.currentTime)) {
      return (millis() - s_apEnterMillis) >= (seconds * 1000UL);
    }
    return (I.currentTime - I.ESPNOW_LAST_INCOMINGMSG_TIME) >= seconds;
  }

  bool apClientIdleFor(uint32_t seconds) {
    if (isTimeValid(I.apLastClientActivity) && isTimeValid(I.currentTime)) {
      return (I.currentTime - I.apLastClientActivity) >= seconds;
    }
    if (isTimeValid(I.apModeEnteredTime) && isTimeValid(I.currentTime)) {
      return (I.currentTime - I.apModeEnteredTime) >= seconds;
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

    if (!found && _MYTYPE >= 100) {
      SerialPrint("AP mode: channel scan failed, server defaulting to channel 1", true);
      setWifiRfChannel(1);
    }

    s_apLastChannelScanMillis = millis();
    if (isTimeValid(I.currentTime)) {
      I.apLastChannelScanTime = I.currentTime;
    }

    SerialPrint(String("AP mode: channel scan ") + (found ? "found server" : "no server"), true);
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

    if (_MYTYPE >= 100) {
      return s_apLastChannelScanMillis == 0;
    }

    if (s_apLastChannelScanMillis == 0) return true;
    if (isTimeValid(I.apLastChannelScanTime) && isTimeValid(I.currentTime)) {
      return (I.currentTime - I.apLastChannelScanTime) >= AP_CHANNEL_SCAN_IDLE_SEC;
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

  #ifdef _USELEDMATRIX
  Matrix_Init();
  Matrix_Draw(false, "WiFi?");
  #endif

  connectSoftAP(&wifiID, &wifiPWD, &apIP);
  if (!softApRunning()) {
    SerialPrint("Failed to start soft AP", true);
    return;
  }

  if (initESPNOW() == 1) {
    SerialPrint("ESPNow initialized in AP mode", true);
  } else {
    SerialPrint("ESPNow init failed in AP mode", true);
  }

  server.begin();

  I.apLastClientActivity = 0;
  I.apLastChannelScanTime = 0;
  s_apLastChannelScanMillis = 0;
  s_apEnterMillis = millis();
  if (isTimeValid(I.currentTime)) {
    I.apModeEnteredTime = I.currentTime;
    // Defer first STA reconnect so soft-AP can stabilize (WiFi.begin can bounce AP briefly).
    I.apLastReconnectCheckTime = I.currentTime;
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
  if (!softApRunning()) return;
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
  if (softApRunning()) {
    WiFi.softAPdisconnect(true);
    SerialPrint("exitAPStationMode: soft AP stopped, STA active", true);
  }

  I.apLastReconnectCheckTime = 0;
  I.apLastClientActivity = 0;
  I.apModeEnteredTime = 0;
  I.apLastChannelScanTime = 0;
  s_apLastChannelScanMillis = 0;
  s_apChannelScanListen = false;
  s_apChannelScanGotResponse = false;

  updateWifiChannel();

  #ifdef _USEUDP
  if (wifiReadyForNetwork()) {
    connectUDP();
  }
  #endif
}

void serviceAPStationMode() {
  if (!softApRunning()) return;

  maybeExitAPStationMode();
  if (!softApRunning()) return;

  static uint32_t lastHttpActivitySeen = 0;
  if (I.HTTP_LAST_INCOMINGMSG_TIME != lastHttpActivitySeen) {
    lastHttpActivitySeen = I.HTTP_LAST_INCOMINGMSG_TIME;
    if (isTimeValid(I.currentTime)) {
      I.apLastClientActivity = I.currentTime;
    }
  }

  if (shouldRunApChannelScan()) {
    runApModeChannelScan();
  }

  if (!haveWifiCredentials()) return;

  const bool firstCheck = (I.apLastReconnectCheckTime == 0);
  const bool due = firstCheck
      || (isTimeValid(I.currentTime)
          && I.currentTime - I.apLastReconnectCheckTime >= WIFI_AP_STA_RECONNECT_SEC);
  if (!due) return;

  const bool clientActiveSinceLastCheck = !firstCheck
      && isTimeValid(I.apLastClientActivity)
      && I.apLastClientActivity >= I.apLastReconnectCheckTime;

  if (isTimeValid(I.currentTime)) {
    I.apLastReconnectCheckTime = I.currentTime;
  }

  // Retry known credentials periodically; keep soft-AP up until STA recovers.
  if (!clientActiveSinceLastCheck) {
    startWifiConnectAsync();
  }
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
  
  // ESP32 cannot scan while connected to a network in STA mode
  // We need to temporarily disconnect or switch to AP+STA mode
  // IMPORTANT: Don't disconnect if client might be connected via WiFi - send response first!
  #ifdef _USE32
    bool wasConnected = (WiFi.status() == WL_CONNECTED);
    String savedSSID = "";
    String savedPassword = "";
    wifi_mode_t originalMode = WiFi.getMode();
    bool needToDisconnect = false;
    
    if (wasConnected) {
      // Save current connection info
      savedSSID = WiFi.SSID();
      savedPassword = WiFi.psk();
      
      // Switch to AP+STA mode to allow scanning while maintaining AP
      if (originalMode != WIFI_MODE_APSTA) {
        WiFi.mode(WIFI_MODE_APSTA);
        delay(100);
        // In AP+STA mode, we can scan without disconnecting
        // The scan will work even if STA is connected
        needToDisconnect = false;
      } else {
        // Already in AP+STA mode - can scan without disconnecting
        needToDisconnect = false;
      }
    } else {
      // Not connected, but ensure we're in a mode that allows scanning
      if (originalMode == WIFI_MODE_AP) {
        WiFi.mode(WIFI_MODE_APSTA);
        delay(100);
      }
      needToDisconnect = false;
    }
    
    // Note: We don't disconnect here to avoid breaking HTTP connections
    // Scanning works in AP+STA mode even when STA is connected
  #endif
  
  // Perform WiFi scan
  int numNetworks = WiFi.scanNetworks(false, true); // async=false, show_hidden=true
  
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
  
  // Clean up scan results
  WiFi.scanDelete();
  
  // Send response BEFORE reconnecting/disconnecting to avoid breaking HTTP connection
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
  
  // Now reconnect/disconnect AFTER sending the response
  #ifdef IGNOREME //don't reconnect to original network
    // Reconnect to original network if we were connected
    if (wasConnected && savedSSID.length() > 0) {
      SerialPrint("Reconnecting to " + savedSSID, true);
      WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
      // Restore original mode if it wasn't APSTA
      if (originalMode != WIFI_MODE_APSTA) {
        delay(1000); // Give time for connection attempt
        WiFi.mode(originalMode);
      }
    } else if (!wasConnected && originalMode != WIFI_MODE_APSTA) {
      // Restore original mode if we changed it
      WiFi.mode(originalMode);
    }
  #endif
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
  
  WEBHTML += R"===(
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
<div class="setup-container">
  <p>Welcome! Let's configure your system.</p>
  )===";
  serverTextFlush(true);

  //insert what is missing/ why we got to initial setup
  //possible choices include no wifi, no timezone, no location (if using weather), or user reset

  //is there no wifi?
  CheckWifiStatus(WIFI_CHECK_NORMAL);
  WEBHTML += "<p>";
  WEBHTML += "Credentials present: " + String(Prefs.HAVECREDENTIALS ? "true" : "false") + "<br>";    
  WEBHTML += "WiFi connection status: "; 
  if (I.WiFiStatus == 2) {
    WEBHTML += "Connected by all measures, but wifi.status() != WL_CONNECTED";
  } else if (I.WiFiStatus == 1) {
    WEBHTML += "Connected";
  } else if (I.WiFiStatus == 0) {
    WEBHTML += "Unknown";
  } else if (I.WiFiStatus == -1) {
    WEBHTML += "No valid IP address";
  } else if (I.WiFiStatus == -2) {
    WEBHTML += "No valid RSSI range";
  } else if (I.WiFiStatus == -3) {
    WEBHTML += "No valid SSID";
  } else if (I.WiFiStatus == -4) {
    WEBHTML += "No valid gateway";
  } else {
    WEBHTML += "Unknown";
  }
  WEBHTML += "<br>";
  if (I.WiFiStatus >= 1) {
    WEBHTML += "Currently connected to WiFi network: " + WiFi.SSID() + "<br>";
    WEBHTML += "Current IP address: " + WiFi.localIP().toString() + "<br>";
    WEBHTML += "Current RSSI: " + formatRssiHtml(WiFi.RSSI()) + "<br>";
  } else {
    WEBHTML += "Not connected to WiFi<br>";
  }
  WEBHTML += "Last WiFi event: " + WiFiEventtoString(I.WiFiLastEvent) + "<br>";
  WEBHTML += "</p>";
  WEBHTML += R"===(<button class="btn btn-secondary" onclick="clearWiFiCredentials()" style="margin-top: 10px; padding: 8px 16px; font-size: 14px; background-color: #dc3545; color: white; border: none; border-radius: 4px; cursor: pointer;">Clear WiFi Credentials</button>)===";

  //is there no timezone?
  if (Prefs.TimeZoneOffset > 50400) { //UTC cannot be greater than this
    WEBHTML += R"===(
    <p>Timezone not found and required to continue.</p>
    )===";
  } else {
    WEBHTML += "<p>Timezone: " + String(Prefs.TimeZoneOffset) + "</p>";
  }

  #ifdef _USEWEATHER
  //is there no location?
  if (Prefs.LATITUDE == 0 || Prefs.LONGITUDE == 0) {
    WEBHTML += R"===(
    <p>Location not found and required for weather data (leave blank if not using weather).</p>
    )===";
  } else {
    WEBHTML += "<p>LAT, LON: " + String(Prefs.LATITUDE, 6) + ", " + String(Prefs.LONGITUDE, 6) + "</p>";
  }
  #endif

  WEBHTML += R"===(  
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
        <input type="text" id="lmk_key" maxlength="16" placeholder="Optional - for ESPNow encryption">
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
  timezoneConfigured: false
};

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
  if (stepNum < 3) {
    const nextStep = document.getElementById('step' + (stepNum + 1));
    nextStep.classList.add('active');
  }
  checkSetupComplete();
}

// Check if setup is complete
function checkSetupComplete() {
  if (setupState.wifiConfigured && setupState.locationConfigured && setupState.timezoneConfigured) {
    document.getElementById('complete-btn').disabled = false;
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
)===";
  
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


void handleREQUESTWEATHER() {
  #ifdef _USEWEATHER
  registerHTTPMessage("WTHRREQ");
//if no parameters passed, return current temp, max, min, today weather ID, pop, and snow amount
//otherwise, return the index value for the requested value

  int8_t dailyT[2];

  WEBHTML = "";
  if (server.args()==0) {
        WEBHTML += (String) WeatherData.getTemperature(I.currentTime) + ";"; //current temp
        WeatherData.getDailyTemp(0,dailyT);
        WEBHTML += (String) dailyT[0] + ";"; //dailymax
        WEBHTML += (String) dailyT[1] + ";"; //dailymin
        WEBHTML += (String) WeatherData.getDailyWeatherID(0) + ";"; //dailyID
        WEBHTML += (String) WeatherData.getDailyPoP(0) + ";"; //POP
        WEBHTML += (String) WeatherData.flag_snow + ";"; 
        WEBHTML += (String) WeatherData.sunrise + ";"; 
        WEBHTML += (String) WeatherData.sunset + ";"; 
  } else {
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i)=="hourly_temp") WEBHTML += (String) WeatherData.getTemperature(I.currentTime + server.arg(i).toInt()*3600,false,false) + ";";
      WeatherData.getDailyTemp(server.arg(i).toInt(),dailyT);
        if (server.argName(i)=="daily_tempMax") WEBHTML += (String) dailyT[0] + ";";
      if (server.argName(i)=="daily_tempMin") WEBHTML += (String) dailyT[1] + ";";
      if (server.argName(i)=="daily_weatherID") WEBHTML += (String) WeatherData.getDailyWeatherID(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="hourly_weatherID") WEBHTML += (String) WeatherData.getWeatherID(I.currentTime + server.arg(i).toInt()*3600) + ";";
      if (server.argName(i)=="daily_pop") WEBHTML += (String) WeatherData.getDailyPoP(server.arg(i).toInt(),true) + ";";
      if (server.argName(i)=="daily_snow") WEBHTML += (String) WeatherData.getDailySnow(server.arg(i).toInt()) + ";";
      if (server.argName(i)=="hourly_pop") WEBHTML += (String) WeatherData.getPoP(I.currentTime + server.arg(i).toInt()*3600) + ";";
      if (server.argName(i)=="hourly_snow") WEBHTML += (String) WeatherData.getSnow(I.currentTime + server.arg(i).toInt()*3600) + ";";      //note snow is returned in mm!!
      if (server.argName(i)=="sunrise") WEBHTML += (String) WeatherData.sunrise + ";";
      if (server.argName(i)=="sunset") WEBHTML += (String) WeatherData.sunset + ";";
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


void handleSTATUS() {
  //registerHTTPMessage("STATUS");
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Status");
  serverTextStreamBegin(200, true);

  WEBHTML = WEBHTML + "<body>";  

  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush();

  #ifdef _USE8266
  WEBHTML = WEBHTML + "Free Stack Memory: " + ESP.getFreeContStack() + "<br>";  
  #endif

  #ifdef _USE32
  WEBHTML = WEBHTML + "Free Internal Heap Memory: " + esp_get_free_internal_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "Free Total Heap Memory: " + esp_get_free_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "Minimum Free Heap: " + esp_get_minimum_free_heap_size() + "<br>";  
  WEBHTML = WEBHTML + "PSRAM Size: " + (String) ESP.getFreePsram() + " / " + (String) ESP.getPsramSize() + "<br>"; 
  
  #endif

  WEBHTML += "Number of devices/sensors: " + (String) Sensors.getNumDevices() + " / " + (String) Sensors.getNumSensors() + "<br>";
  WEBHTML = WEBHTML + "Alive since: " + dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss") + "<br>";
  WEBHTML = WEBHTML + "Number of reboots today: " + (String) I.rebootsToday + "<br>";;
  WEBHTML = WEBHTML + "Last error: " + (String) I.lastError + " @" + (String) (I.lastErrorTime ? dateify(I.lastErrorTime,"mm/dd/yyyy hh:nn:ss") : "???");
  #ifdef _USESDCARD
  WEBHTML = WEBHTML + " <a href=\"/ERROR_LOG\" target=\"_blank\" style=\"display: inline-block; padding: 5px 10px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; cursor: pointer; font-size: 12px;\">Error Log</a>";
  WEBHTML = WEBHTML + " <a href=\"/SDCARD_SYSTEMLOG\" target=\"_blank\" style=\"display: inline-block; padding: 5px 10px; background-color: #3F51B5; color: white; text-decoration: none; border-radius: 4px; cursor: pointer; font-size: 12px;\">System Log</a>";
  #endif
  WEBHTML = WEBHTML + "<br>";
  WEBHTML = WEBHTML + "Last known reset: " + (String) lastReset2String()  + " ";
  WEBHTML = WEBHTML + "<a href=\"/REBOOT_DEBUG\" target=\"_blank\" style=\"display: inline-block; padding: 5px 10px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px; cursor: pointer; font-size: 12px;\">Debug</a><br>";
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML = WEBHTML + "<p><strong>WiFi Status:</strong> " + (wifiReadyForNetwork() ? "Connected" : "Disconnected") + "<br>";
  WEBHTML = WEBHTML + "<strong>WiFi Mode:</strong> " + getWiFiModeString() + "<br>";

  wifi_mode_t t = WiFi.getMode();

  if (t == WIFI_MODE_APSTA) {
    WEBHTML = WEBHTML + "<strong>Connected to:</strong> " + WiFi.SSID() + "<br>";
    WEBHTML = WEBHTML + "APIP: " + WiFi.softAPIP().toString() + ", Station IP: " + WiFi.localIP().toString() + "<br>";
    WEBHTML = WEBHTML + "Stations connected to me: " + (String) WiFi.softAPgetStationNum() + "<br>";
    
  } else if (t == WIFI_MODE_STA) {
    WEBHTML = WEBHTML + "<strong>Station IP:</strong> " + WiFi.localIP().toString() + "<br>";
    WEBHTML = WEBHTML + "<strong>Connected to:</strong> " + WiFi.SSID() + "<br>";
  
  } else if (t == WIFI_MODE_AP) {
    WEBHTML = WEBHTML + "APIP: " + WiFi.softAPIP().toString() + "<br>";
    WEBHTML = WEBHTML + "Stations connected to me: " + (String) WiFi.softAPgetStationNum() + "<br>";
  }

  {
    String bssidStr = WiFi.BSSIDstr();
    if (bssidStr.length() == 0 || bssidStr == "00:00:00:00:00:00") bssidStr = "N/A";
    WEBHTML = WEBHTML + "<strong>BSSID:</strong> " + bssidStr + "</p>";
  }
  #if defined(_USENETWORKMONITOR) && (_USENETWORKMONITOR > 0)
  auto nmTime = [](time_t t) -> String {
    return t ? dateify(t, "mm/dd/yyyy hh:nn:ss") : String("???");
  };
  WEBHTML = WEBHTML + "<strong>RSSI:</strong> current " + formatRssiHtml(I.RSSIcurrent)
      + " @" + (String) (I.lastRSSItime ? dateify(I.lastRSSItime,"mm/dd/yyyy hh:nn:ss") : "???")
      + ", best " + formatRssiHtml(I.RSSIhigh) + ", worst " + formatRssiHtml(I.RSSIlow) + "<br>";
  WEBHTML = WEBHTML + "AP switches: " + String(NetworkMonitor.bssid.changeCount)
      + " @" + nmTime(NetworkMonitor.bssid.lastAttemptTime) + "<br>";
  WEBHTML = WEBHTML + "LocalIP switches: " + String(NetworkMonitor.localIp.changeCount)
      + " @" + nmTime(NetworkMonitor.localIp.lastAttemptTime) + "<br>";
  WEBHTML = WEBHTML + "DNS Resolution Time: " + String(NetworkMonitor.dns.resolutionMs) + " ms"
      + " @" + nmTime(NetworkMonitor.dns.lastAttemptTime) + "<br>";
  WEBHTML = WEBHTML + "Tx Failures: " + String(NetworkMonitor.txFailures.failureCount)
      + " @" + nmTime(NetworkMonitor.txFailures.lastAttemptTime) + "<br>";
  WEBHTML = WEBHTML + "Gateway Latency: " + String(NetworkMonitor.gatewayLatency.ping.avgRttMs) + " ms avg, "
      + String(NetworkMonitor.gatewayLatency.ping.packetsLost) + "/" + String(NetworkMonitor.gatewayLatency.ping.packetsSent) + " lost, jitter "
      + String(NetworkMonitor.gatewayLatency.ping.jitterMs) + " ms @"
      + nmTime(NetworkMonitor.gatewayLatency.lastAttemptTime) + "<br>";
  WEBHTML = WEBHTML + "External Ping: " + String(NetworkMonitor.externalPing.wan.avgRttMs) + " ms avg, "
      + String(NetworkMonitor.externalPing.wan.packetsLost) + "/" + String(NetworkMonitor.externalPing.wan.packetsSent) + " lost, jitter "
      + String(NetworkMonitor.externalPing.wan.jitterMs) + " ms (WAN)";
  if (NetworkMonitor.externalPing.gatewayChecked) {
    WEBHTML = WEBHTML + "; gateway " + String(NetworkMonitor.externalPing.gateway.avgRttMs) + " ms avg, "
        + String(NetworkMonitor.externalPing.gateway.packetsLost) + "/" + String(NetworkMonitor.externalPing.gateway.packetsSent) + " lost, jitter "
        + String(NetworkMonitor.externalPing.gateway.jitterMs) + " ms (LAN)";
  }
  WEBHTML = WEBHTML + " @" + nmTime(NetworkMonitor.externalPing.lastAttemptTime) + "<br>";
  {
    double speedMbps = -1;
    NetworkMonitor.readSensorValue(89, speedMbps);
    WEBHTML = WEBHTML + "Speedtest: ";
    if (speedMbps < 0) {
      WEBHTML = WEBHTML + "failed";
    } else {
      WEBHTML = WEBHTML + String(speedMbps, 2) + " Mbps (" + String(NetworkMonitor.download.durationMs) + " ms, "
          + String(NetworkMonitor.download.downloadBytes) + " bytes)";
    }
    WEBHTML = WEBHTML + " from " + NetworkMonitor.download.sourceIP.toString() + " @"
        + nmTime(NetworkMonitor.download.lastRunTime) + "<br>";
  }
  #else
  WEBHTML = WEBHTML + "<strong>RSSI:</strong> current " + formatRssiHtml(I.RSSIcurrent)
      + " @" + (String) (I.lastRSSItime ? dateify(I.lastRSSItime,"mm/dd/yyyy hh:nn:ss") : "???")
      + ", best " + formatRssiHtml(I.RSSIhigh) + ", worst " + formatRssiHtml(I.RSSIlow) + "<br>";
  #endif

  WEBHTML = WEBHTML + "---------------------<br>";
  appendCommunicationsSection();
  WEBHTML = WEBHTML + "---------------------<br>";
  #ifdef _USEWEATHER
  WEBHTML = WEBHTML + "---------------------<br>";      
  WEBHTML += "Weather last retrieved at: " + (String) (WeatherData.lastUpdateT ? dateify(WeatherData.lastUpdateT,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "Weather last failure at: " + (String) (WeatherData.lastUpdateError ? dateify(WeatherData.lastUpdateError,"mm/dd/yyyy hh:nn:ss") : "???") + "<br>";
  WEBHTML += "NOAA address: " + WeatherData.getGrid(0) + "<br>";
  #endif
  WEBHTML = WEBHTML + "---------------------<br>";      


  serverTextClose(200);
}


void handleRoot(void) {
  registerHTTPMessage("MainPage");
  if (!haveWifiCredentials()) {
    SerialPrint("No stored WiFi credentials, redirecting root to InitialSetup", true);
    server.sendHeader("Location", "/InitialSetup");
    server.send(302, "text/plain", "Redirecting to setup...");
    return;
  }
  renderDeviceViewerPage();
}

void appendStandardPageNav(bool includeWiFiConfig) {
  WEBHTML = WEBHTML + "<div style=\"text-align: center; padding: 20px; background-color: #f0f0f0; margin-bottom: 20px;\">";
  WEBHTML = WEBHTML + "<a href=\"/\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Main</a> ";
  WEBHTML = WEBHTML + "<a href=\"/STATUS\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px;\">Status</a> ";
  WEBHTML = WEBHTML + "<a href=\"/REGISTER_DEVICE\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #009688; color: white; text-decoration: none; border-radius: 4px;\">Register Device</a> ";
  #ifdef _USESDCARD
  WEBHTML = WEBHTML + "<a href=\"/SDCARD\" style=\"display: inline-block; margin: 5px; padding: 10px 20px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px;\">SD Card</a> ";
  #endif
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
}

static String formatArborysDeviceFirmware(const ArborysDevType* device) {
  if (!device) return "0.0.0";
  char buf[16];
  device->firmware.toChar(buf, sizeof(buf));
  return String(buf);
}

static void appendDeviceFirmwareInfoRow(const ArborysDevType* device) {
  WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Firmware Version:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + formatArborysDeviceFirmware(device) + "</td></tr>";
}

#ifndef _USEGSHEET
static void appendAllDevicesFirmwareTable() {
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
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"/?devIndex=" + String(di) + "\">" + String(d->devName) + "</a></td>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + d->IP.toString() + "\" target=\"_blank\">" + d->IP.toString() + "</a></td>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(d->devType) + "</td>";
    WEBHTML = WEBHTML + "<td style=\"padding: 8px; border: 1px solid #ddd;\">" + formatArborysDeviceFirmware(d) + "</td>";
    WEBHTML = WEBHTML + "</tr>";
    serverTextFlush();
  }
  WEBHTML = WEBHTML + "</table></div>";
  serverTextFlush();
}
#endif

#ifndef CONTENT_LENGTH_UNKNOWN
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#endif

namespace {
  bool s_webStreamActive = false;
  constexpr size_t WEBHTML_CHUNK_BYTES = 2048;

  void serverTextSendBufferedChunks(const char* contentType, int htmlcode) {
    // Stream an already-built buffer in slices so send() does not hold a second full copy.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(htmlcode, contentType, "");
    const size_t len = WEBHTML.length();
    const char* data = WEBHTML.c_str();
    for (size_t off = 0; off < len; off += WEBHTML_CHUNK_BYTES) {
      const size_t n = (len - off > WEBHTML_CHUNK_BYTES) ? WEBHTML_CHUNK_BYTES : (len - off);
      server.sendContent(data + off, n);
      yield();
      esp_task_wdt_reset();
    }
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
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(htmlcode, asHTML ? "text/html" : "text/plain", "");
  s_webStreamActive = true;
  if (WEBHTML.length() > 0) {
    server.sendContent(WEBHTML);
    WEBHTML = "";
    WEBHTML.reserve(WEBHTML_CHUNK_BYTES);
  }
}

void serverTextFlush(bool force) {
  if (!s_webStreamActive || WEBHTML.length() == 0) return;
  if (!force && WEBHTML.length() < WEBHTML_CHUNK_BYTES) return;
  server.sendContent(WEBHTML);
  WEBHTML = "";
  WEBHTML.reserve(WEBHTML_CHUNK_BYTES);
  yield();
  esp_task_wdt_reset();
}

void serverTextClose(int htmlcode, bool asHTML) {
  if (asHTML) {
    WEBHTML += "</body></html>\n";
  }

  if (s_webStreamActive) {
    if (WEBHTML.length() > 0) {
      server.sendContent(WEBHTML);
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
  WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA_MOVINGAVERAGE?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&windowSize=1800&numPointsX=48\" target=\"_blank\" rel=\"noopener noreferrer\">AvgHx</a></td>";
  WEBHTML = WEBHTML + "<td><a href=\"/RETRIEVEDATA?MAC=" + (String) Sensors.getDeviceMACBySnsIndex(j) + "&type=" + (String) sensor->snsType + "&id=" + (String) sensor->snsID + "&starttime=0&endtime=0&N=50\" target=\"_blank\" rel=\"noopener noreferrer\">History</a></td>";
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
  const char* overrideFlagNames[] = {"Flag Status", "Monitored", "Outside", "Derived/Calc", "Predictive", "High/Low", "Changed", "Critical"};
  for (int i = 0; i < 8; i++) {
    WEBHTML = WEBHTML + "<label style=\"display: flex; align-items: center; gap: 4px;\">";
    WEBHTML = WEBHTML + "<input type=\"checkbox\" name=\"override_flag_bit" + String(i) + "\" value=\"1\"";
    if (bitRead(currentOverrideFlags, i)) WEBHTML = WEBHTML + " checked";
    WEBHTML = WEBHTML + ">";
    WEBHTML = WEBHTML + "<span style=\"font-size: 11px;\">" + String(i) + ":" + String(overrideFlagNames[i]) + "</span>";
    WEBHTML = WEBHTML + "</label>";
  }
  WEBHTML = WEBHTML + "</div></div>";
  WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 6px 12px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Update Override</button>";
  WEBHTML = WEBHTML + "</form></details></td>";
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
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Max Limit:</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" name=\"limitMax\" value=\"" + String(Prefs.SNS_LIMIT_MAX[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Min Limit:</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" step=\"any\" name=\"limitMin\" value=\"" + String(Prefs.SNS_LIMIT_MIN[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Poll Int (s):</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" name=\"intervalPoll\" value=\"" + String(Prefs.SNS_INTERVAL_POLL[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
      WEBHTML = WEBHTML + "<div style=\"margin-bottom: 8px;\">";
      WEBHTML = WEBHTML + "<label style=\"display: inline-block; width: 120px; font-weight: bold;\">Send Int (s):</label>";
      WEBHTML = WEBHTML + "<input type=\"number\" name=\"intervalSend\" value=\"" + String(Prefs.SNS_INTERVAL_SEND[prefsIndex]) + "\" style=\"width: 100px; padding: 4px;\">";
      WEBHTML = WEBHTML + "</div>";
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
      }
      WEBHTML = WEBHTML + "</div></div>";
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
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsType + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsID + "</td>";
  WEBHTML = WEBHTML + "<td>" + Sensors.sensorIsOfType(sensor) + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) sensor->snsValue + "</td>";
  WEBHTML = WEBHTML + "<td>" + (String) (sensor->timeLogged ? dateify(sensor->timeLogged,"mm/dd hh:nn:ss") : "???") + "</td>";
  appendSensorTableFlagsCell(sensor);
  appendSensorTableFlaggedCell(sensor);
  appendSensorTableExpiredCell(j, sensor);
  appendSensorTablePlotCells(j, sensor);
  appendSensorTableConfigCell(j, sensor);
  WEBHTML = WEBHTML + "</tr>";
  serverTextFlush();
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

  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<h1>" + (String) Prefs.DEVICENAME + "</h1>";
  WEBHTML = WEBHTML + "<br>";
  WEBHTML = WEBHTML + "<h2>" + dateify(I.currentTime,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br>\n";

  WEBHTML = WEBHTML + "<p>";

  if (sensorIndex<0)   WEBHTML += "WARNING!! Device: " + String(deviceMAC, HEX) + " sensor type: " + (String) snsType + " id: " + (String) snsID + " was NOT found in the active list, though I did find an associated file. <br>";
  else {
    WEBHTML += "Request for Device: " + String(deviceMAC, HEX) + " sensor: " + (String) sensor->snsName + " type: " + (String) sensor->snsType + " id: " + (String) sensor->snsID + "<br>";
  }

  WEBHTML += "Start time: " + (String) dateify(t[0],"mm/dd/yyyy hh:nn:ss") + " to " + (String) dateify(t[N-1],"mm/dd/yyyy hh:nn:ss")  +  "<br>";
  
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
      serverTextFlush();
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
    serverTextFlush();
  }
  
  serverTextClose(200);
}



void handleCONFIG() {
  registerHTTPMessage("CONFIG");
  
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("System Configuration");
  serverTextStreamBegin(200, true);
//  SerialPrint("config: filename is " + (strlen(GSheetInfo.GsheetName) > 0 ? String(GSheetInfo.GsheetName) : "N/A"),true);

  WEBHTML = WEBHTML + "<body>";

  // Navigation buttons (WiFi Config only on this page)
  appendStandardPageNav(true);
  serverTextFlush();

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

  //can add a div here if "show these flags" becomes editable
  
  
  WEBHTML = WEBHTML + "</div>";
  
  // Submit button
  WEBHTML = WEBHTML + "<br><input type=\"submit\" value=\"Update Configuration\" style=\"padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
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
  
  WEBHTML = WEBHTML + "</body>";
  WEBHTML = WEBHTML + "<script>";
  WEBHTML = WEBHTML + R"===(
function isValidTimezoneOffset(offset) {
  return offset !== undefined && offset >= -50400 && offset <= 50400 && offset !== 90000;
}

function fillDstFields(data) {
  document.getElementById('dst_enabled').value = data.dst_enabled;
  document.getElementById('dst_start_date').value = data.dst_start_date || '';
  document.getElementById('dst_end_date').value = data.dst_end_date || '';
  document.getElementById('dst_offset').value = data.dst_offset !== undefined ? data.dst_offset : '';
}

// Auto-detect standard UTC offset only
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

// Auto-detect DST rules only (start/end/offset/active flag)
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
  </script>
)===";


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


  WEBHTML = WEBHTML + "<body>";

  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush();

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
  WEBHTML = WEBHTML + "<div style=\"padding: 12px; border: 1px solid #ddd;\">" + (String) ((GSheetInfo.lastErrorTime) ? dateify(GSheetInfo.lastErrorTime) : "N/A") + "</div>";

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
  WEBHTML = WEBHTML + "<body>";
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
  SerialPrint("Broadcast (ESPLan+UDPLan): " + String(result ? "Success" : "Failed"), true);
  server.sendHeader("Location", "/STATUS");
  server.send(302, "text/plain", result ? "Success" : "Failed");
}

void handleREQUEST_BROADCAST_ESP() {
  bool result = broadcastServerPresence(true, 0);
  SerialPrint("Broadcast ESPLan: " + String(result ? "Success" : "Failed"), true);
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
  
  if (!WiFi.softAP(wifiID->c_str(), wifiPWD->c_str())) {
    SerialPrint("Failed to start AP", true);
    return;
  }
  SerialPrint("AP starting (please wait)", true);
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

#if _IS_SERVER_HUB
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
  
  // Update flags - reconstruct from individual bits
  uint16_t flags = 0;
  for (int i = 0; i < 8; i++) {
    String flagName = "flag_bit" + String(i);
    if (server.hasArg(flagName)) {
      bitSet(flags, i);
    }
  }
  Prefs.SNS_FLAGS[prefsIndex] = flags;
  
  // Also update the sensor's flags in the Sensors array
  if (sensor) {
    sensor->Flags = flags;
    sensor->SendingInt = Prefs.SNS_INTERVAL_SEND[prefsIndex];
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
  WEBHTML = WEBHTML + "<body>";
  WEBHTML = WEBHTML + "<p><a href=\"/\">Back to Main</a></p>";
  serverTextFlush();

  // Calibration is available for every sensor. The values live in Prefs (the source of truth);
  // if no live reading is available for this sensor it simply shows NaN and the user can still
  // type the min/max manually.
  int16_t prefsIndex = SensorHistory.getSensorHistoryIndex(snsIndex);
  if (prefsIndex < 0) {
    WEBHTML = WEBHTML + "<p>Calibration unavailable: this sensor has no Prefs storage slot.</p>";
  } else {
    bool isSoil = (snsType == 33 || snsType == 3);
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
    WEBHTML = WEBHTML + "<button type=\"submit\" style=\"padding: 8px 16px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer;\">Apply Calibration</button>";
    WEBHTML = WEBHTML + "</form>";
  }

  WEBHTML = WEBHTML + "</body></html>";
  server.send(200, "text/html", WEBHTML.c_str());
}

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



void handleWeather() {
  registerHTTPMessage("Weather");
  WEBHTML = "";
  serverTextHeader("Weather Data");
  serverTextStreamBegin(200, true);
  WEBHTML = WEBHTML + "<body>";

  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush();

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
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateT) ? dateify(WeatherData.lastUpdateT) : "???") + "</td></tr>";

  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Last Update Attempt Time</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.lastUpdateAttempt) ? dateify(WeatherData.lastUpdateAttempt) : "???") + "</td></tr>";


  // Basic weather data
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Latitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Prefs.LATITUDE, 6) + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Longitude</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String(Prefs.LONGITUDE, 6) + "</td></tr>";
    
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Sunrise</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.sunrise) ? dateify(WeatherData.sunrise) : "???") + "</td></tr>";
  
  WEBHTML = WEBHTML + "<tr><td style=\"border: 1px solid #ddd; padding: 8px;\"><strong>Sunset</strong></td>";
  WEBHTML = WEBHTML + "<td style=\"border: 1px solid #ddd; padding: 8px;\">" + String((WeatherData.sunset) ? dateify(WeatherData.sunset) : "???") + "</td></tr>";
  
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
  
  // Force immediate weather update
  bool updateResult = WeatherData.updateWeather(0);
  
  if (updateResult) {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather data refreshed successfully.");
  } else {
    server.sendHeader("Location", "/WEATHER");
    server.send(302, "text/plain", "Weather update failed. Please try again.");
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

static void appendSdCardDirectoryManageForms(const String& currentPath) {
  String ep = "/SDCARD?path=" + urlEncode(currentPath);
  const bool firmwareDir = sdIsFirmwareRoot(currentPath);

  WEBHTML = WEBHTML + "<h3>Directory Management</h3>";
  if (firmwareDir) {
    WEBHTML = WEBHTML + "<p><strong>/Firmware</strong> accepts firmware uploads and file deletes. "
      "Use <code>&lt;devicename&gt;-&lt;x.x.x&gt;.bin</code> (version after the last hyphen). "
      "Delete legacy subfolders below if migrating from the old per-device layout.</p>";
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
  WEBHTML = WEBHTML + "<p>Upload to <strong>" + currentPath + "</strong>. Existing files are replaced. Folders may contain up to <strong>50 files</strong> (uploaded one at a time).</p>";
  if (currentPath.equalsIgnoreCase("/Firmware")) {
    WEBHTML = WEBHTML + "<p>Firmware files: name each binary <code>&lt;devicename&gt;-&lt;x.x.x&gt;.bin</code> (e.g. <code>PleasantB-9.0.0.bin</code>). The version is the segment after the <strong>last</strong> hyphen.</p>";
  }
  WEBHTML = WEBHTML + "<form id=\"sd-upload-form\" method=\"POST\" action=\"/SDCARD_UPLOAD?path=" + urlEncode(currentPath) + "\" enctype=\"multipart/form-data\">";
  WEBHTML = WEBHTML + "<div id=\"sd-drop-zone\" style=\"border: 2px dashed #999; padding: 24px; margin: 10px 0; text-align: center; cursor: pointer; background-color: #fafafa;\">";
  WEBHTML = WEBHTML + "Drag and drop a file or folder here";
  WEBHTML = WEBHTML + "<input type=\"file\" name=\"upload\" id=\"sd-file-input\" style=\"display:none;\">";
  WEBHTML = WEBHTML + "<input type=\"file\" id=\"sd-folder-input\" webkitdirectory directory multiple style=\"display:none;\">";
  WEBHTML = WEBHTML + "</div>";
  WEBHTML = WEBHTML + "<p id=\"sd-upload-name\" style=\"color:#555;\"></p>";
  WEBHTML = WEBHTML + "<button type=\"button\" id=\"sd-choose-file\" style=\"padding: 8px 16px; margin-right: 8px; background-color: #607D8B; color: white; border: none; border-radius: 4px; cursor: pointer;\">Choose File</button>";
  WEBHTML = WEBHTML + "<button type=\"button\" id=\"sd-choose-folder\" style=\"padding: 8px 16px; margin-right: 8px; background-color: #607D8B; color: white; border: none; border-radius: 4px; cursor: pointer;\">Choose Folder</button>";
  WEBHTML = WEBHTML + "<input type=\"submit\" value=\"Upload File to SD Card\" style=\"padding: 10px 20px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;\">";
  WEBHTML = WEBHTML + "</form>";
  WEBHTML = WEBHTML + "<script>\n";
  WEBHTML = WEBHTML + "const SD_BASE_PATH = \"" + jsPath + "\";\n";
  WEBHTML = WEBHTML + "const SD_MAX_FOLDER_FILES = 50;\n";
  WEBHTML = WEBHTML + R"===(
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
    for (let i = 0; i < fileEntries.length; i++) {
      const rel = (fileEntries[i].relativePath || fileEntries[i].file.name).replace(/\\/g, '/');
      const parts = rel.split('/');
      const fileName = parts.pop();
      const subDir = parts.join('/');
      const targetDir = joinPath(SD_BASE_PATH, subDir);
      showSelected('Uploading ' + (i + 1) + '/' + fileEntries.length + ': ' + rel);
      try {
        const uploadFile = fileEntries[i].file;
        await uploadOneFile(uploadFile, targetDir);
        ok++;
      } catch (err) {
        fail++;
        lastError = err.message || 'Upload failed';
      }
    }

    let redirect = '/SDCARD?path=' + encodeURIComponent(SD_BASE_PATH);
    if (fail === 0) {
      redirect += '&upload=ok&msg=' + encodeURIComponent('Folder upload complete: ' + ok + ' file(s)');
    } else {
      redirect += '&upload=fail&msg=' + encodeURIComponent('Folder upload: ' + ok + ' ok, ' + fail + ' failed. ' + lastError);
    }
    window.location.href = redirect;
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
      input.files = e.dataTransfer.files;
      showSelected('Selected file: ' + e.dataTransfer.files[0].name);
    }
  });

  input.addEventListener('change', function() {
    if (input.files && input.files.length > 0) {
      showSelected('Selected file: ' + input.files[0].name);
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
</script>)==="
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
    appendSdCardUploadForm(currentPath);
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
      if (fileTime > 0) lastUpdated = String(dateify(fileTime, "mm/dd/yyyy hh:nn:ss"));
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
  appendSdCardUploadForm(currentPath);
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

  WEBHTML = WEBHTML + "<body>";

  // Navigation buttons
  appendStandardPageNav();
  serverTextFlush();
  
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
      htmlContent += "<div class=\"log-entry\">" + htmlEscapeText(lines[i]) + "</div>";
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
      htmlContent += "<span class=\"error-time\">" + (String) dateify(LASTERROR.errorTime) + "</span><br>";
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

  WEBHTML = WEBHTML + "<body>";
  
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

static void pushJsonPingReplyContext(uint8_t via, IPAddress udpReplyIp = IPAddress(0, 0, 0, 0)) {
  if (s_jsonPingReplyViaDepth < (uint8_t)(sizeof(s_jsonPingReplyViaStack) / sizeof(s_jsonPingReplyViaStack[0]))) {
    s_jsonPingReplyViaStack[s_jsonPingReplyViaDepth] = via;
    s_jsonPingReplyUdpIpStack[s_jsonPingReplyViaDepth] = udpReplyIp;
    s_jsonPingReplyViaDepth++;
  }
  s_jsonPingReplyVia = via;
  s_jsonPingReplyUdpIp = udpReplyIp;
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
  HeaderInfoAlertGuard headerAlert("Pinging...", TFT_YELLOW, TFT_BLACK, 60);
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
  if (strcmp(channel, "ESPNow") == 0) {
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

  if (proto == "esplan") {
    protocolLabel = "ESPLAN";
    bool ok = sendLANBlockingPing(device, 1, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
    noteDevicePingAttempt(device, "ESPNow", ok);
    return ok;
  }
  if (proto == "udplan") {
    protocolLabel = "UDPLAN";
    bool ok = sendLANBlockingPing(device, 2, DEVICE_VIEWER_PING_TIMEOUT_MS, &rttMs);
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
  static constexpr uint16_t METRICS_LAN_PING_MS = ESPNOW_BLOCKING_PING_DEFAULT_MS;
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
    bool espOk = sendLANBlockingPing(device, 1, METRICS_LAN_PING_MS, nullptr);
    noteDevicePingAttempt(device, "ESPNow", espOk);

    esp_task_wdt_reset();
    bool udpOk = sendLANBlockingPing(device, 2, METRICS_LAN_PING_MS, nullptr);
    noteDevicePingAttempt(device, "UDP", udpOk);

    if (!espOk && !udpOk) {
      esp_task_wdt_reset();
      bool httpOk = sendBlockingJsonPing(device, true, false, METRICS_HTTP_PING_MS, nullptr);
      noteDevicePingAttempt(device, "HTTP", httpOk);
    }

    SerialPrint("Connectivity ping " + String(device->devName) +
      " ESPNow=" + String(espOk ? "ok" : "fail") +
      " UDP=" + String(udpOk ? "ok" : "fail") +
      ((!espOk && !udpOk) ? " HTTP=fallback" : ""), true);
    return; // one device per call
  }

  s_pingDevIndex = -1;
}

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

void renderDeviceViewerPage() {
    WEBHTML = "";
    serverTextHeader("Main");
    serverTextStreamBegin(200, true);

    WEBHTML = WEBHTML + "<body>";

    appendStandardPageNav();
    serverTextFlush();

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

    #ifndef _USEGSHEET
    if (Sensors.getNumDevices() > 0) {
        appendAllDevicesFirmwareTable();
    }
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
    bool deviceFlagged[NUMDEVICES];
    for (int16_t di = 0; di < NUMDEVICES; di++) deviceFlagged[di] = false;
    for (int16_t si = 0; si < NUMSENSORS; si++) {
      ArborysSnsType* sensor = Sensors.snsIndexToPointer(si);
      if (sensor && sensor->IsSet && sensor->deviceIndex >= 0 && sensor->deviceIndex < NUMDEVICES && bitRead(sensor->Flags, 0)) {
        deviceFlagged[sensor->deviceIndex] = true;
      }
    }
    for (int16_t di = 0; di < NUMDEVICES; di++) {
      if (!Sensors.isDeviceInit(di)) continue;
      ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
      if (!d) continue;
      String label = String(d->devName) + " v" + formatArborysDeviceFirmware(d);
      if (deviceFlagged[di]) label += "*";
      WEBHTML = WEBHTML + "<option value=\"" + String(di) + "\"";
      if (di == CURRENT_DEVICEVIEWER_DEVINDEX) WEBHTML = WEBHTML + " selected";
      WEBHTML = WEBHTML + ">" + label + "</option>";
    }
    WEBHTML = WEBHTML + "</select>";
    WEBHTML = WEBHTML + "<span style=\"font-size: 1.17em; font-weight: bold;\"> of " + String(Sensors.getNumDevices()) + "</span>";
    if (isThisDevice) WEBHTML = WEBHTML + "<span style=\"font-size: 1.17em; font-weight: bold;\"> (this device)</span>";
    WEBHTML = WEBHTML + "</form>";
    WEBHTML = WEBHTML + "<div style=\"margin-top: 10px; text-align: center;\">";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PREV\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #FF9800; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">Prev</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_NEXT\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">Next</a> ";
    WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_DELETE\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #f44336; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\" onclick=\"return confirm('Are you sure you want to delete this device and all its sensors? This action cannot be undone.');\">Delete</a> ";
    if (!isThisDevice) {
      WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=esplan\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #9C27B0; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">ESPLAN</a> ";
      WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=udplan\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #673AB7; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">UDPLAN</a> ";
      WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=udp\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #3F51B5; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">UDP</a> ";
      WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=http\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #2196F3; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">HTTP</a> ";
      WEBHTML = WEBHTML + "<a href=\"/DEVICEVIEWER_PING?protocol=https\" style=\"display: inline-block; margin: 3px 2px; padding: 6px 10px; background-color: #009688; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em;\">HTTPS</a>";
    }
    WEBHTML = WEBHTML + "</div>";
    WEBHTML = WEBHTML + "</div>";

    appendDeviceViewerPingStatus();

    WEBHTML = WEBHTML + "<div style=\"background-color: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 4px; border: 1px solid #dee2e6;\">";
    WEBHTML = WEBHTML + "<h4>Device Information</h4>";
    WEBHTML = WEBHTML + "<table style=\"width: 100%; border-collapse: collapse;\">";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold; width: 30%;\">Device Name:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devName) +  "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">MAC Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(MACToString(device->MAC)) + "</td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">IP Address:</td><td style=\"padding: 8px; border: 1px solid #ddd;\"><a href=\"http://" + device->IP.toString() + "\" target=\"_blank\">" + device->IP.toString() + "</a></td></tr>";
    WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Device Type:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->devType) + "</td></tr>";
    appendDeviceFirmwareInfoRow(device);
    if (isThisDevice) {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Alive Since:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(I.ALIVESINCE ? dateify(I.ALIVESINCE, "mm/dd/yyyy hh:nn:ss") : "???") + "</td></tr>";
    } else {
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Time Data Sent:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->dataSent ? dateify(device->dataSent, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Last Time Data Received:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->dataReceived ? dateify(device->dataReceived, "mm/dd/yyyy hh:nn:ss") : "Never") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Sending Interval:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->SendingInt) + " seconds</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">Status:</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->expired ? "Expired" : "Active") + "</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">ESPNow Pings (today):</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->ping_success_ESPNow) + " / " + String(device->ping_att_ESPNow) + " success / attempts</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">UDP Pings (today):</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->ping_success_UDP) + " / " + String(device->ping_att_UDP) + " success / attempts</td></tr>";
      WEBHTML = WEBHTML + "<tr><td style=\"padding: 8px; border: 1px solid #ddd; background-color: #f8f9fa; font-weight: bold;\">HTTP Pings (today):</td><td style=\"padding: 8px; border: 1px solid #ddd;\">" + String(device->ping_success_HTTP) + " / " + String(device->ping_att_HTTP) + " success / attempts</td></tr>";
    }
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
    serverTextFlush();

    if (sensorCount == 0) {
        WEBHTML = WEBHTML + "<p>No sensors found for this device.</p>";
    } else {
        WEBHTML = WEBHTML + "<table id=\"DeviceSensors\" style=\"width: 100%; border-collapse: collapse; margin-top: 10px;\">";
        WEBHTML = WEBHTML + "<tr style=\"background-color: #2196f3; color: white;\">";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Sensor</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">ID</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Type Name</th>";
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
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Plot Avg</th>";
        WEBHTML = WEBHTML + "<th style=\"padding: 8px; border: 1px solid #ddd; text-align: left;\">Plot Raw</th>";
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
  // Peripherals only store servers (devType >= 100); helloPing was still sent so remote can register us.
  if (outDevType < 100) {
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
    const String& regStatus, const String& respName, uint8_t respType, IPAddress respIp) {
  WEBHTML.clear();
  WEBHTML = "";
  serverTextHeader("Register Device");
  WEBHTML = WEBHTML + "<body>";
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
  renderRegisterDevicePage(targetIP.toString(), true, true, rttMs, "", regStatus, respName, respType, respIp);
}

void setupServerRoutes() {
    // Main routes
    server.on("/", handleRoot);
    server.on("/POST", handlePost);
#ifdef _USE32
    server.on("/POST_ENC", HTTP_POST, handlePostEnc, handlePostEncRaw);
    server.on("/FIRMWARE_ENC", HTTP_POST, handleFirmwareEnc, handleFirmwareEncRaw);
    server.on("/FIRMWARE_BLOCK", HTTP_POST, handleFirmwareBlock);
#else
    server.on("/POST_ENC", HTTP_POST, handlePostEnc);
    server.on("/FIRMWARE_ENC", HTTP_POST, handleFirmwareEnc);
    server.on("/FIRMWARE_BLOCK", HTTP_POST, handleFirmwareBlock);
#endif
    server.on("/REQUESTUPDATE", handleREQUESTUPDATE);
    server.on("/CLEARSENSOR", handleCLEARSENSOR);
    server.on("/TIMEUPDATE", handleTIMEUPDATE);
    server.on("/REQUESTWEATHER", handleREQUESTWEATHER);
    server.on("/REBOOT", handleReboot);
    
    server.on("/STATUS", handleSTATUS);
    server.on("/RETRIEVEDATA", handleRETRIEVEDATA);
    server.on("/RETRIEVEDATA_MOVINGAVERAGE", handleRETRIEVEDATA_MOVINGAVERAGE);
    
    // Configuration routes
    server.on("/CONFIG", HTTP_GET, handleCONFIG);
    server.on("/CONFIG", HTTP_POST, handleCONFIG_POST);
    server.on("/CONFIG_DELETE", HTTP_POST, handleCONFIG_DELETE);
    server.on("/CONFIG_OTA_SWITCH", HTTP_POST, handleCONFIG_OTA_SWITCH);
    #if _IS_SERVER_HUB
    server.on("/SENSOR_OVERRIDE_UPDATE", HTTP_POST, handleSENSOR_OVERRIDE_UPDATE);
    #endif
    
    #if _HAS_LOCAL_SENSORS
    // Sensor configuration routes for peripheral devices
    server.on("/SENSOR_UPDATE", HTTP_POST, handleSENSOR_UPDATE_POST);
    server.on("/SENSOR_READ_SEND_NOW", HTTP_POST, handleSENSOR_READ_SEND_NOW);
    server.on("/SENSOR_SETUP", HTTP_GET, handleSensorSetup);
    server.on("/SNS_CALIBRATION", HTTP_POST, handleSNS_CALIBRATION);
    #endif
    
    // Google Sheets routes
    server.on("/GSHEET", HTTP_GET, handleGSHEET);
    server.on("/GSHEET", HTTP_POST, handleGSHEET_POST);
    server.on("/GSHEET_UPLOAD_NOW", HTTP_POST, handleGSHEET_UPLOAD_NOW);
    server.on("/GSHEET_SHARE_ALL", HTTP_POST, handleGSHEET_SHARE_ALL);
    server.on("/GSHEET_DELETE_ALL", HTTP_POST, handleGSHEET_DELETE_ALL);
    
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
            
    // Weather routes
    server.on("/WEATHER", HTTP_GET, handleWeather);
    server.on("/WEATHER", HTTP_POST, handleWeather_POST);
    server.on("/WeatherRefresh", HTTP_POST, handleWeatherRefresh);
    server.on("/WeatherZip", HTTP_POST, handleWeatherZip);
    server.on("/WeatherAddress", HTTP_POST, handleWeatherAddress);
    
    // SD Card routes
    server.on("/SDCARD", HTTP_GET, handleSDCARD);
    server.on("/SDCARD", HTTP_POST, handleSDCARD_DIR);
    server.on("/SDCARD_DOWNLOAD", HTTP_GET, handleSDCARD_DOWNLOAD);
    #ifdef _USESDCARD
    server.on("/SDCARD_UPLOAD", HTTP_POST, handleSDCARD_UPLOAD, handleSDCARD_UPLOADFile);
    #endif
    server.on("/SDCARD_DELETE_SENSORS", HTTP_POST, handleSDCARD_DELETE_SENSORS);
    server.on("/SDCARD_STORE_DEVICES", HTTP_POST, handleSDCARD_STORE_DEVICES);
    server.on("/SDCARD_SAVE_SCREENFLAGS", HTTP_POST, handleSDCARD_SAVE_SCREENFLAGS);
    server.on("/SDCARD_SAVE_WEATHERDATA", HTTP_POST, handleSDCARD_SAVE_WEATHERDATA);
    server.on("/SDCARD_SYSTEMLOG", HTTP_GET, handleSDCARD_SYSTEMLOG);
    server.on("/ERROR_LOG", HTTP_GET, handleERROR_LOG);
    server.on("/REBOOT_DEBUG", HTTP_GET, handleREBOOT_DEBUG);
    
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

String JSONbuilder_sensorObject(ArborysSnsType* S) {
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
  sensorJSON += ",\"flags\":";
  sensorJSON += S->Flags;
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

static String JSONbuildSensorMSGString(const int16_t* snsIndices, uint8_t count, bool forHTTP) {
  if (!snsIndices || count == 0) {
    return "";
  }

  int16_t mydeviceIndex = Sensors.findMyDeviceIndex();
  ArborysDevType* device = Sensors.getDeviceByDevIndex(mydeviceIndex);
  if (!device) {
    return "";
  }

  String tempJSON;
  if (count == 1) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndices[0]);
    if (!S) {
      return "";
    }
    tempJSON = "{\"msgType\":\"snsData\"," + JSONbuilder_device(device) + "," + JSONbuilder_sensorData(S) + "}";
  } else {
    tempJSON.reserve(256 + (size_t)count * 96u);
    tempJSON = "{\"msgType\":\"snsData\"," + JSONbuilder_device(device) + ",\"sensors\":[";
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

static bool sensorMSGFitsBuffer(const int16_t* snsIndices, uint8_t count, uint16_t jsonBufferSize, bool forHTTP) {
  String tempJSON = JSONbuildSensorMSGString(snsIndices, count, forHTTP);
  return tempJSON.length() > 0 && tempJSON.length() < jsonBufferSize;
}

bool JSONbuilder_sensorMSG_list(const int16_t* snsIndices, uint8_t count, char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP) {
  if (!snsIndices || count == 0 || !jsonBuffer || jsonBufferSize == 0) {
    return false;
  }

  String tempJSON = JSONbuildSensorMSGString(snsIndices, count, forHTTP);
  if (tempJSON.isEmpty() || tempJSON.length() >= jsonBufferSize) {
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

  if (tempJSON.length() >= jsonBufferSize) {
    SerialPrint("JSONbuilder_sensorMSG_all: Buffer too small for JSON payload", true);
    storeError("JSONbuilder_sensorMSG_all: Buffer too small", ERROR_JSON_PARSE, true);
    return;
  }

  snprintf(jsonBuffer, jsonBufferSize, "%s", tempJSON.c_str());
}

void JSONbuilder_DataRequestMSG(char* jsonBuffer, uint16_t jsonBufferSize, bool forHTTP, int16_t snsIndex) {
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

  String tempJSON = "{\"msgType\":\"sendSensorDataNow\",\"snsType\":" + String(snsType) + ",\"snsID\":" + String(snsID) + "," + JSONbuilder_device(device) + "}";

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

  if (msgType == "snsData") {
    //we have received sensor data
    processJSONMessage_sensorData(root, responseMsg);
  } 
  else if (msgType == "sendSensorDataNow") {
    //we have received a request to send a single sensor
    processJSONMessage_DataRequest(root, responseMsg);
  }
  else if (msgType == "helloPing" || msgType == "ackPing") {
    processJSONMessage_ping(root, responseMsg);
  }
  else if (msgType == "setFlagsReq") {
    //we have received a sensor request
    processJSONMessage_setFlagsReq(root, responseMsg);
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


void processJSONMessage_ping(JsonObject root, String& responseMsg) {
  responseMsg = "OK";

  String msgType = root["msgType"] | "";
  int16_t senderIndex = processJSONMessage_addDevice(root, responseMsg);

  if (senderIndex == -1) {
    return;
  }

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
  uint32_t timeRead   = sensor["timeRead"]   | I.currentTime;
  uint32_t sendingInt = sensor["sendingInt"] | 3600;
  uint8_t flags       = sensor["flags"]      | 0;

  uint8_t ret = registerSensorData(
      dev->MAC, dev->IP, dev->devName, dev->devType, dev->Flags,
      snsType, snsID, snsName, value, timeRead, I.currentTime, sendingInt, flags
  );

  if (ret == 0) responseMsg = "Failed to add sensor";

}

 uint8_t registerSensorData(uint64_t deviceMAC, IPAddress deviceIP, String devName, uint8_t devType, uint8_t devFlags, uint8_t snsType, uint8_t snsID, String snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, uint32_t sendingInt, uint8_t flags) {
   //returns 0 if failed to add sensor, 1 if sensor was added, 2 if sensor was already in the database and is updated
 
   uint8_t ret = 0;
   bool addToSD = false;
   int16_t sensorIndex = -1;
   if (deviceMAC!=0 && snsType!=0 ) {
     if (devName.length() == 0) devName = "Unknown";
 
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
                                           timeRead, timeLogged, sendingInt, flags, devName.c_str(), devType);
 
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
  //return true if it is time to send this sensor
  if (!Si || Si->deviceIndex != I.MY_DEVICE_INDEX) return false;
  if (Si->timeLogged != 0 && Si->timeLogged < I.currentTime && I.currentTime - Si->timeLogged < 60*60*24 && bitRead(Si->Flags, 6) == 0 ) {
    if (Si->timeLogged + Si->SendingInt > I.currentTime) return false; //not time
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
  if (D->devType < 100) return false; //not a server

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
      S->timeLogged = I.currentTime;
      S->expired = false;
    }
    return;
  }
  bitWrite(S->Flags,6,0); //even if there was no change in the flag status, I sent the value so this is the new baseline. Set bit 6 (change in flag) to zero
  S->timeLogged = I.currentTime;
  S->expired = false;
}

void wrapupSendDataList(const int16_t* snsIndices, uint8_t count) {
  if (!snsIndices) {
    return;
  }
  for (uint8_t i = 0; i < count; ++i) {
    wrapupSendData(Sensors.snsIndexToPointer(snsIndices[i]));
  }
}

int16_t sendHTTPJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002; //device not found
  return sendHTTPJSON(d->IP, jsonBuffer, msgType);
}

int16_t sendHTTPJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType) {
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
  if (SendHTTPMessage(M)) {
    registerHTTPSend(ip, msgType);
    return M.httpCode;
  }
  I.HTTP_OUTGOING_ERRORS++;
  return -1000; //failed to send message
}

static int16_t sendHTTPEncryptedPayload(IPAddress& ip, const uint8_t* payload, uint16_t payloadLen, const char* msgType) {
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

  WiFiClient client;
  HTTPClient http;
  client.setTimeout(20000);
  http.begin(client, urlBuffer);
  http.setTimeout(20000);
  http.addHeader("Content-Type", "application/octet-stream");
  esp_task_wdt_reset();
  int httpCode = http.POST(encBuf, encLen);
  esp_task_wdt_reset();

  if (httpCode >= 200 && httpCode < 400) {
    registerHTTPSend(ip, msgType);
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

int16_t sendHTTPSJSON(IPAddress& ip, const char* jsonBuffer, const char* msgType) {
  if (!jsonBuffer) return -1002;
  uint16_t len = (uint16_t)strlen(jsonBuffer);
  return sendHTTPEncryptedPayload(ip, (const uint8_t*)jsonBuffer, len, msgType);
}

int16_t sendHTTPSJSON(int16_t deviceIndex, const char* jsonBuffer, const char* msgType) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002;
  return sendHTTPSJSON(d->IP, jsonBuffer, msgType);
}

int16_t sendHTTPSBinary(IPAddress& ip, const uint8_t* data, uint16_t dataLen, const char* msgType) {
  return sendHTTPEncryptedPayload(ip, data, dataLen, msgType);
}

int16_t sendHTTPSBinary(int16_t deviceIndex, const uint8_t* data, uint16_t dataLen, const char* msgType) {
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);
  if (!d) return -1002;
  return sendHTTPSBinary(d->IP, data, dataLen, msgType);
}

// Prefer encrypted POST_ENC when LMK is available; otherwise (or on failure) plain HTTP.
static bool sendJsonViaPreferredHttp(IPAddress ip, const char* rawJson, const char* msgType) {
  if (!rawJson || rawJson[0] == '\0') return false;

  if (isValidLMKKey()) {
    int16_t code = sendHTTPSJSON(ip, rawJson, msgType);
    if (code >= 200 && code < 400) return true;
  }

  String httpBody = String(rawJson);
  JSONbuilder_encodeHTTP(httpBody);
  return sendHTTPJSON(ip, httpBody.c_str(), msgType) == 200;
}

uint8_t sendAllSensors(bool forceSend, int16_t sendToDeviceIndex, bool useUDP) {
  //can use -1 to send to broadcast (if using HTTP, then send to all servers), or a specific device index to send to a specific device
  
  return SendData(-1,forceSend,sendToDeviceIndex,useUDP);

  
}

void processDeferredDataRequest() {
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


static constexpr uint64_t LAN_SENSOR_BROADCAST_MAC = 0xFFFFFFFFFFFFULL;

static void markAllServersDataSent() {
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (d && d->IsSet && d->devType >= 100) {
      d->dataSent = I.currentTime;
    }
  }
}

static void backoffAllServersDataSent() {
  for (int16_t i = 0; i < NUMDEVICES; ++i) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
    if (d && d->IsSet && d->devType >= 100) {
      d->dataSent = I.currentTime - d->SendingInt + 10 * 60;
    }
  }
}

// ESP-NOW type 12: one frame per sensor (bundle sends each included sensor).
static bool sendSensorDataLANBundle(ArborysDevType* d, const int16_t* snsIndices, uint8_t count) {
  if (!d || !d->IsSet || !snsIndices || count == 0) {
    return false;
  }
  const uint8_t tier = 1;
  bool ok = false;
  for (uint8_t i = 0; i < count; ++i) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndices[i]);
    if (!S || !S->IsSet) {
      continue;
    }
    if (sendLANSensorData(S, d->MAC, d->IP, tier)) {
      ok = true;
    }
  }
  return ok;
}

static bool sendSensorDataLANBroadcastBundle(const int16_t* snsIndices, uint8_t count) {
  const uint8_t tier = 1;
  const IPAddress noIp(0, 0, 0, 0);
  if (!snsIndices || count == 0) {
    return false;
  }
  bool ok = false;
  for (uint8_t i = 0; i < count; ++i) {
    ArborysSnsType* S = Sensors.snsIndexToPointer(snsIndices[i]);
    if (!S || !S->IsSet) {
      continue;
    }
    if (sendLANSensorData(S, LAN_SENSOR_BROADCAST_MAC, noIp, tier)) {
      ok = true;
    }
  }
  if (ok) {
    markAllServersDataSent();
  }
  return ok;
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
    if (!S || !S->IsSet || S->deviceIndex != myIdx) {
      continue;
    }

    bool isDue = false;
    if (forceSend) {
      isDue = (triggerSnsIndex < 0) || (i == triggerSnsIndex);
    } else {
      isDue = checkThisSensorTime(S);
    }

    if (isDue) {
      dueIndices[dueCount++] = i;
    } else if (isSensorMonitored(S) && sensorHasFreshUnreadData(S)) {
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

bool SendData(int16_t snsIndex, bool forceSend, int16_t sendToDeviceIndex, bool useUDP) {
  if (snsIndex >= 0 && !forceSend && !isSensorSendTime(snsIndex)) {
    return false;
  }
  if (snsIndex < 0 && !forceSend && !isSensorSendTime(-1)) {
    return false;
  }

  const bool haveWifi = wifiReadyForNetwork();
  // Pack against HTTP form size (larger); raw JSON is used for UDP/HTTPS.
  static char jsonBuffer[SNSDATA_JSON_BUFFER_SIZE];
  int16_t sendList[NUMSENSORS];
  const uint8_t sendCount = packSensorsForSend(sendList, NUMSENSORS, forceSend, snsIndex,
      haveWifi, SNSDATA_JSON_BUFFER_SIZE, true);
  if (sendCount == 0) {
    return false;
  }

  bool isGood = false;

  // Directed send to one device (e.g. data-request response)
  if (sendToDeviceIndex >= 0) {
    ArborysDevType* d = Sensors.getDeviceByDevIndex(sendToDeviceIndex);
    if (!d) {
      return false;
    }
    if (!isDeviceSendTime(d, forceSend)) {
      return false;
    }

    if (destinationNeedsLAN(d, haveWifi)) {
      isGood = sendSensorDataLANBundle(d, sendList, sendCount);
    } else if (useUDP) {
      if (!JSONbuilder_sensorMSG_list(sendList, sendCount, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, false)) {
        return false;
      }
      isGood = sendUDPMessage((uint8_t*)jsonBuffer, d->IP, strlen(jsonBuffer), "snsMsg");
    } else {
      if (!JSONbuilder_sensorMSG_list(sendList, sendCount, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, false)) {
        return false;
      }
      isGood = sendJsonViaPreferredHttp(d->IP, jsonBuffer, "snsMsg");
    }
    if (isGood) {
      d->dataSent = I.currentTime;
      wrapupSendDataList(sendList, sendCount);
    } else {
      I.makeBroadcast = true;
    }
    return isGood;
  }

  // Send to all servers: always UDP broadcast, then HTTP/HTTPS only for servers
  // whose UDP ping success rate is not above 50%.
  if (haveWifi) {
    if (!JSONbuilder_sensorMSG_list(sendList, sendCount, jsonBuffer, SNSDATA_JSON_BUFFER_SIZE, false)) {
      return false;
    }

    SerialPrint("SendData: UDP broadcast bundled sensor data", true);
    if (sendUDPMessage((uint8_t*)jsonBuffer, IPAddress(0, 0, 0, 0), strlen(jsonBuffer), "snsBrdcst")) {
      isGood = true;
      #ifndef _USELOWPOWER
      markAllServersDataSent();
      #endif
    }

    for (int16_t i = 0; i < NUMDEVICES; ++i) {
      ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
      if (!isDeviceSendTime(d, forceSend)) {
        continue;
      }
      if (deviceUdpPingRateAbove50(d)) {
        continue; // UDP broadcast is sufficient for this server
      }

      if (destinationNeedsLAN(d, haveWifi)) {
        SerialPrint("SendData: LAN/ESP-NOW unicast to low-UDP-rate server " + String(d->devName), true);
        if (sendSensorDataLANBundle(d, sendList, sendCount)) {
          d->dataSent = I.currentTime;
          isGood = true;
        } else {
          d->dataSent = I.currentTime - d->SendingInt + 10 * 60;
        }
      } else {
        SerialPrint("SendData: HTTP/HTTPS fallback to " + String(d->devName) +
          " (UDP rate " + String(udpPingSuccessRatePercent(d)) + "%)", true);
        if (sendJsonViaPreferredHttp(d->IP, jsonBuffer, "snsMsg")) {
          d->dataSent = I.currentTime;
          isGood = true;
        } else {
          d->dataSent = I.currentTime - d->SendingInt + 10 * 60;
        }
      }
      delay(10);
    }
  } else {
    bool anyServer = false;
    for (int16_t i = 0; i < NUMDEVICES; ++i) {
      ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
      if (d && d->IsSet && d->devType >= 100 && isDeviceSendTime(d, false)) {
        anyServer = true;
        break;
      }
    }
    if (anyServer) {
      SerialPrint("SendData: no WiFi; ESP-NOW broadcast bundled sensor data to servers", true);
      isGood = sendSensorDataLANBroadcastBundle(sendList, sendCount);
      if (!isGood) {
        backoffAllServersDataSent();
      }
    }
  }

  if (isGood) {
    wrapupSendDataList(sendList, sendCount);
  } else {
    I.makeBroadcast = true;
  }
  return isGood;
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

int16_t sendMSG_DataRequest(int16_t deviceIndex, int16_t snsIndex, bool viaHTTP) {
  //send a data request to a specific device and sensor, or use snsIndex = -1 to request all sensors
  ArborysDevType* d = Sensors.getDeviceByDevIndex(deviceIndex);  
  return sendMSG_DataRequest(d, snsIndex, viaHTTP);
}

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
  d->dataSent = I.currentTime;
  if (viaHTTP) {
    if (sendJsonViaPreferredHttp(d->IP, jsonBuffer, "snsReq")) {
      return 200;
    }
    return -1000;
  }
  return sendUDPMessage((uint8_t*)jsonBuffer, d->IP, strlen(jsonBuffer), "snsReq");
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
    I.UDP_LAST_INCOMINGMSG_TIME = I.currentTime;
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

    bool isESPNOW = false;

    if (packetSize != sizeof(ESPNOW_type)) {
      // JSON UDP: local-sensor nodes always accept; hub-only builds require _MYTYPE >= 100
      #if !_HAS_LOCAL_SENSORS && _IS_SERVER_HUB
      if (_MYTYPE < 100) {
        return closeUDP(true);
      }
      #endif

    } else {
      isESPNOW = true;
    } 
    
    // Allocate buffer dynamically based on actual packet size
    // This allows multi-sensor messages that exceed SNSDATA_JSON_BUFFER_SIZE
    char* buffer = (char*)malloc(packetSize + 1);
    if (!buffer) {
      SerialPrint("UDP message: Failed to allocate buffer for " + String(packetSize) + " bytes", true);
      I.UDP_INCOMING_ERRORS++;
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "AllocFail");
      return closeUDP(false);
    }
    // Read packet data and verify all bytes were read
    size_t bytesRead = LAN_UDP.read((uint8_t*)buffer, packetSize);

    if (bytesRead != packetSize) {
      SerialPrint("UDP message: Read " + String(bytesRead) + " bytes, expected " + String(packetSize), true);
      free(buffer);
      I.UDP_INCOMING_ERRORS++;
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "ReadFail");
      return closeUDP(false);
    }

    if (isESPNOW) {
      ESPNOW_type msg;
      memcpy(&msg, buffer, sizeof(ESPNOW_type));
      processLANMessage(&msg);
      snprintf(I.UDP_LAST_INCOMINGMSG_TYPE, sizeof(I.UDP_LAST_INCOMINGMSG_TYPE), "ESP type:%d", msg.msgType);
      
    } else {
      //process the json buffer
      buffer[packetSize] = '\0';  // ensure null termination for String
      String responseMsg = "OK";
      String postData = (String)buffer;
      registerUdpMsgTypeFromJson(postData, remoteIP);
      pushJsonPingReplyContext(JSON_PING_REPLY_UDP, remoteIP);
      processJSONMessage(postData, responseMsg);
      popJsonPingReplyContext();
    }

  
    free(buffer);  // must free to avoid memory leak

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
  if (I.UDP_LAST_OUTGOINGMSG_TIME == I.currentTime) delay(500); //wait if the last message was sent within the last second
  
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
  //I.UDP_LAST_INCOMINGMSG_TIME = I.currentTime;
  //I.UDP_RECEIVES++;
}

void registerUDPSend(IPAddress ip, const char* messageType) {
  I.UDP_LAST_OUTGOINGMSG_TO_IP = ip;
  snprintf(I.UDP_LAST_OUTGOINGMSG_TYPE, sizeof(I.UDP_LAST_OUTGOINGMSG_TYPE), messageType);
  I.UDP_LAST_OUTGOINGMSG_TIME = I.currentTime;  
  I.UDP_SENDS++;
}

void registerHTTPMessage(const char* messageType) {
  if (isHttpUiBrowseMessage(messageType)) return;
  I.HTTP_LAST_INCOMINGMSG_TIME = I.currentTime;
  snprintf(I.HTTP_LAST_INCOMINGMSG_TYPE, sizeof(I.HTTP_LAST_INCOMINGMSG_TYPE), "%s", messageType);
  I.HTTP_LAST_INCOMINGMSG_FROM_IP = server.client().remoteIP();
  I.HTTP_RECEIVES++;
}

void registerHTTPSend(IPAddress ip, const char* messageType) {
  I.HTTP_LAST_OUTGOINGMSG_TO_IP = ip;
  I.HTTP_LAST_OUTGOINGMSG_TIME = I.currentTime;
  I.HTTP_SENDS++;
  snprintf(I.HTTP_LAST_OUTGOINGMSG_TYPE, sizeof(I.HTTP_LAST_OUTGOINGMSG_TYPE), messageType);
}
void delayWithNetwork(uint16_t delayTime, uint8_t maxChecks) {
//do not delay a highspeed device, such as a TFLuna, as it will lock out wifi
  for (uint8_t i=0; i<maxChecks; i++) {
    delay(delayTime);
    receiveUDPMessage();
    server.handleClient();
  }
}
#endif

