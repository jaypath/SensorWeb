#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <lwip/netdb.h>
#include <esp_task_wdt.h>
#include "globals.hpp"
#include "sensors.hpp"
#include "NetworkMonitor.hpp"
#ifdef _USEUDP
#include "server.hpp"
#endif

#if _USENETWORKMONITOR > 0

NetworkMonitor_type NetworkMonitor;

static void finalizePingResult(NmPingResult& result) {
    uint32_t rttSum = 0;
    uint8_t successCount = 0;
    int16_t prevRtt = -1;
    uint32_t jitterSum = 0;
    uint8_t jitterPairs = 0;

    for (uint8_t i = 0; i < NM_PING_BURST_COUNT; i++) {
        if (result.rttMs[i] < 0) {
            continue;
        }
        rttSum += (uint32_t)result.rttMs[i];
        successCount++;
        if (prevRtt >= 0) {
            int16_t diff = result.rttMs[i] - prevRtt;
            if (diff < 0) {
                diff = -diff;
            }
            jitterSum += (uint32_t)diff;
            jitterPairs++;
        }
        prevRtt = result.rttMs[i];
    }

    result.avgRttMs = (successCount > 0) ? (int16_t)(rttSum / successCount) : -1;
    result.jitterMs = (jitterPairs > 0) ? (int16_t)(jitterSum / jitterPairs) : -1;
}

static void runPingBurst(IPAddress target, NmPingResult& result, uint32_t deadlineMs) {
    result.packetsSent = NM_PING_BURST_COUNT;
    result.packetsLost = 0;
    for (uint8_t i = 0; i < NM_PING_BURST_COUNT; i++) {
        result.rttMs[i] = -1;
        if (millis() >= deadlineMs) {
            result.packetsLost = NM_PING_BURST_COUNT - i;
            result.packetsSent = i;
            finalizePingResult(result);
            return;
        }
        if (i > 0) {
#ifdef _USEUDP
            delayWithNetwork(NM_PING_BURST_INTERVAL_MS / 10, 10);
#else
            delay(NM_PING_BURST_INTERVAL_MS);
#endif
        }
        if (Ping.ping(target, 1)) {
            result.rttMs[i] = (int16_t)Ping.averageTime();
        } else {
            result.packetsLost++;
        }
    }
    finalizePingResult(result);
}

static bool runLocalPing(NmPingResult& result, uint32_t deadlineMs) {
    result = NmPingResult();
    if (WiFi.status() != WL_CONNECTED) {
        result.packetsLost = NM_PING_BURST_COUNT;
        return false;
    }
    IPAddress gateway = WiFi.gatewayIP();
    if (gateway == IPAddress(0, 0, 0, 0)) {
        result.packetsLost = NM_PING_BURST_COUNT;
        return false;
    }
    runPingBurst(gateway, result, deadlineMs);
    return true;
}

static void runExternalPing(NmPingResult& result, IPAddress primary, IPAddress backup,
    uint8_t& primaryLoss, uint8_t& backupLoss, bool& triedBackup, uint32_t deadlineMs) {
    triedBackup = false;
    backupLoss = 255;
    runPingBurst(primary, result, deadlineMs);
    primaryLoss = result.packetsLost;

    if (result.packetsLost == NM_PING_BURST_COUNT && millis() < deadlineMs) {
        triedBackup = true;
        runPingBurst(backup, result, deadlineMs);
        backupLoss = result.packetsLost;
    }
}

static bool externalPingNeedsLocalCheck(uint8_t primaryLoss, uint8_t backupLoss, bool triedBackup) {
    const uint8_t triggerLoss = NM_PING_BURST_COUNT - 1;
    if (primaryLoss >= triggerLoss) {
        return true;
    }
    return triedBackup && backupLoss >= triggerLoss;
}

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#if defined(_USE_CERT_BUNDLE)
extern "C" {
    extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
    extern const uint8_t x509_crt_imported_bundle_bin_end[] asm("_binary_x509_crt_bundle_end");
}
#endif

static const uint16_t DOWNLOAD_HTTP_TIMEOUT_MS = 45000;
static const uint16_t DOWNLOAD_DISCARD_CHUNK_BYTES = 512;

class NetworkMonitorDiscardStream : public Stream {
public:
    size_t totalWritten = 0;

    size_t write(const uint8_t* buffer, size_t size) override {
        totalWritten += size;
        if ((totalWritten % 16384) < size) {
            esp_task_wdt_reset();
        }
        return size;
    }

    size_t write(uint8_t c) override {
        totalWritten++;
        return 1;
    }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

static size_t drainHttpResponseBody(HTTPClient& http, uint32_t timeoutMs) {
    NetworkMonitorDiscardStream discard;
    esp_task_wdt_reset();
    int streamed = http.writeToStream(&discard);
    esp_task_wdt_reset();
    if (streamed > 0 && discard.totalWritten > 0) {
        return discard.totalWritten;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        return 0;
    }

    uint8_t chunk[DOWNLOAD_DISCARD_CHUNK_BYTES];
    size_t total = 0;
    uint32_t start = millis();
    uint32_t idleStart = millis();

    while ((millis() - start) < timeoutMs) {
        if (stream->available()) {
            int n = stream->read(chunk, sizeof(chunk));
            if (n > 0) {
                total += (size_t)n;
                idleStart = millis();
                if ((total % 16384) < (size_t)n) {
                    esp_task_wdt_reset();
                }
            } else if (n == 0 && !stream->connected()) {
                break;
            }
        } else if (!stream->connected()) {
            break;
        } else if ((millis() - idleStart) > 5000) {
            break;
        } else {
            delay(1);
        }
    }

    return total;
}

static bool resolveHostIPv4(const char* host, IPAddress& out) {
    struct addrinfo hints;
    struct addrinfo* res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (lwip_getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) {
        return false;
    }
    struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
    out = IPAddress(addr->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

bool NetworkMonitor_type::isSensorType(uint8_t snsType) {
    return runTestIndexFromSensorType(snsType) >= 0;
}

int8_t NetworkMonitor_type::runTestIndexFromSensorType(uint8_t snsType) {
    switch (snsType) {
        case 81: return NM_TEST_BSSID;
        case 82: return NM_TEST_LOCAL_IP;
        case 83: return NM_TEST_DNS;
        case 84: return NM_TEST_TX_FAILURES;
        case 85:
        case 86: return NM_TEST_GATEWAY_LATENCY;
        case 87:
        case 88: return NM_TEST_EXTERNAL_PING;
        case 89: return NM_TEST_DOWNLOAD;
        default: return -1;
    }
}

bool NetworkMonitor_type::runTest(uint8_t testIndex) {
    updateRSSI(true);

    if (testIndex >= NETWORK_MONITOR_TEST_COUNT) {
        return false;
    }

    switch (testIndex) {
        case NM_TEST_BSSID:
            NetworkMonitor_BSSIDChanges();
            return true;
        case NM_TEST_LOCAL_IP:
            NetworkMonitor_LocalIPChanges();
            return true;
        case NM_TEST_DNS:
            NetworkMonitor_DNSResolutionTime();
            return true;
        case NM_TEST_TX_FAILURES:
            NetworkMonitor_TxFailures();
            return true;
        case NM_TEST_GATEWAY_LATENCY:
            NetworkMonitor_GatewayLatency();
            return true;
        case NM_TEST_EXTERNAL_PING:
            NetworkMonitor_PingTest();
            return true;
        case NM_TEST_DOWNLOAD:
            NetworkMonitor_DownloadTest();
            return true;
        default:
            return false;
    }
}

bool NetworkMonitor_type::readSensorValue(uint8_t snsType, double& value) const {
    switch (snsType) {
        case 81:
            value = bssid.changeCount;
            return true;
        case 82:
            value = localIp.changeCount;
            return true;
        case 83:
            value = dns.resolutionMs;
            return true;
        case 84:
            value = txFailures.failureCount;
            return true;
        case 85:
            value = gatewayLatency.ping.avgRttMs;
            return true;
        case 86:
            value = gatewayLatency.ping.jitterMs;
            return true;
        case 87:
            value = externalPing.wan.avgRttMs;
            return true;
        case 88:
            value = externalPing.wan.jitterMs;
            return true;
        case 89:
            if (!download.success || download.durationMs == 0) {
                value = -1;
                return true;
            }
            value = (double)download.downloadBytes * 8.0 / (double)download.durationMs / 1000.0;
            return true;
        default:
            return false;
    }
}

time_t NetworkMonitor_type::readSensorTime(uint8_t snsType) const {
    switch (snsType) {
        case 81:
            return bssid.lastAttemptTime;
        case 82:
            return localIp.lastAttemptTime;
        case 83:
            return dns.lastAttemptTime;
        case 84:
            return txFailures.lastAttemptTime;
        case 85:
        case 86:
            return gatewayLatency.lastAttemptTime;
        case 87:
        case 88:
            return externalPing.lastAttemptTime;
        case 89:
            return download.lastRunTime != 0 ? download.lastRunTime : download.lastAttemptTime;
        default:
            return 0;
    }
}

bool NetworkMonitor_type::isSensorValueInvalid(uint8_t snsType, double value) {
    switch (snsType) {
        case 83:
        case 85:
        case 86:
        case 87:
        case 88:
            return value < 0;
        case 89:
            return value < 0;
        default:
            return false;
    }
}

void NetworkMonitor_type::init() {
    initRuntime();
}

void NetworkMonitor_type::initRuntime() {
    bssid = NmBssidTest();
    localIp = NmLocalIpTest();
    dns = NmDnsTest();
    txFailures = NmTxFailuresTest();
    gatewayLatency = NmGatewayLatencyTest();
    externalPing = NmExternalPingTest();
    download = NmDownloadTest();

    localIp.oldIP = WiFi.localIP();
}

void NetworkMonitor_BSSIDChanges() {
    NetworkMonitor.bssid.lastAttemptTime = I.currentTime;
    NetworkMonitor.bssid.success = false;

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    uint8_t* bssid = WiFi.BSSID();
    if (!bssid) {
        return;
    }

    uint32_t trackedBytes = (bssid[3] << 16) | (bssid[4] << 8) | bssid[5];
    if (NetworkMonitor.bssid.trackedBssid == 0) {
        NetworkMonitor.bssid.trackedBssid = trackedBytes;
        NetworkMonitor.bssid.success = true;
        return;
    }

    if (trackedBytes != NetworkMonitor.bssid.trackedBssid) {
        NetworkMonitor.bssid.changeCount++;
        NetworkMonitor.bssid.trackedBssid = trackedBytes;
    }
    NetworkMonitor.bssid.success = true;
}

void NetworkMonitor_LocalIPChanges() {
    NetworkMonitor.localIp.lastAttemptTime = I.currentTime;
    NetworkMonitor.localIp.success = false;

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    IPAddress local = WiFi.localIP();
    if (local != NetworkMonitor.localIp.oldIP) {
        if (NetworkMonitor.localIp.oldIP != IPAddress(0, 0, 0, 0)) {
            NetworkMonitor.localIp.changeCount++;
        }
        NetworkMonitor.localIp.oldIP = local;
    }
    NetworkMonitor.localIp.success = true;
}

void NetworkMonitor_DNSResolutionTime() {
    NetworkMonitor.dns.lastAttemptTime = I.currentTime;
    NetworkMonitor.dns.success = false;
    NetworkMonitor.dns.resolutionMs = -1;

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    struct addrinfo hints;
    struct addrinfo* res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    uint32_t startTime = millis();
    int err = lwip_getaddrinfo("example.com", nullptr, &hints, &res);
    uint32_t endTime = millis();

    if (err == 0) {
        freeaddrinfo(res);
        NetworkMonitor.dns.resolutionMs = (int16_t)(endTime - startTime);
        NetworkMonitor.dns.success = true;
    } else {
        NetworkMonitor.dns.resolutionMs = -4;
    }
}

void NetworkMonitor_TxFailures() {
    NetworkMonitor.txFailures.lastAttemptTime = I.currentTime;
    NetworkMonitor.txFailures.failureCount = I.HTTP_OUTGOING_ERRORS;
    NetworkMonitor.txFailures.success = true;
}

void NetworkMonitor_GatewayLatency() {
    NetworkMonitor.gatewayLatency.lastAttemptTime = I.currentTime;
    NetworkMonitor.gatewayLatency.success = false;
    NetworkMonitor.gatewayLatency.ping = NmPingResult();

    const uint32_t deadlineMs = millis() + NM_PING_MAX_DURATION_MS;
    if (!runLocalPing(NetworkMonitor.gatewayLatency.ping, deadlineMs)) {
        NetworkMonitor.gatewayLatency.ping.packetsLost = NM_PING_BURST_COUNT;
        return;
    }

    NetworkMonitor.gatewayLatency.success =
        NetworkMonitor.gatewayLatency.ping.packetsLost < NM_PING_BURST_COUNT;
}

void NetworkMonitor_PingTest() {
    NetworkMonitor.externalPing.lastAttemptTime = I.currentTime;
    NetworkMonitor.externalPing.success = false;
    NetworkMonitor.externalPing.wan = NmPingResult();
    NetworkMonitor.externalPing.gateway = NmPingResult();
    NetworkMonitor.externalPing.gatewayChecked = false;

    if (WiFi.status() != WL_CONNECTED) {
        NetworkMonitor.externalPing.wan.packetsLost = NM_PING_BURST_COUNT;
        return;
    }

    const uint32_t deadlineMs = millis() + NM_PING_MAX_DURATION_MS;
    uint8_t primaryLoss = 0;
    uint8_t backupLoss = 255;
    bool triedBackup = false;

    runExternalPing(NetworkMonitor.externalPing.wan,
        NetworkMonitor.externalPing.pingTarget,
        NetworkMonitor.externalPing.pingTargetBackup,
        primaryLoss, backupLoss, triedBackup, deadlineMs);

    if (externalPingNeedsLocalCheck(primaryLoss, backupLoss, triedBackup) && millis() < deadlineMs) {
        NetworkMonitor.externalPing.gatewayChecked = true;
        runLocalPing(NetworkMonitor.externalPing.gateway, deadlineMs);
    }

    NetworkMonitor.externalPing.success =
        NetworkMonitor.externalPing.wan.packetsLost < NM_PING_BURST_COUNT;
}

void NetworkMonitor_DownloadTest() {
    if (NetworkMonitor.download.lastRunTime != 0
        && I.currentTime - NetworkMonitor.download.lastRunTime < (time_t)NM_DOWNLOAD_MIN_INTERVAL_SEC) {
        return;
    }

    NetworkMonitor.download.lastAttemptTime = I.currentTime;
    NetworkMonitor.download.success = false;
    NetworkMonitor.download.downloadBytes = 0;
    NetworkMonitor.download.durationMs = 0;
    NetworkMonitor.download.sourceIP = IPAddress();

    if (WiFi.status() != WL_CONNECTED) {
        NetworkMonitor.download.lastRunTime = I.currentTime;
        return;
    }

#if !defined(_USE_CERT_BUNDLE)
    NetworkMonitor.download.lastRunTime = I.currentTime;
    return;
#else
    if (!resolveHostIPv4("speed.cloudflare.com", NetworkMonitor.download.sourceIP)) {
        NetworkMonitor.download.lastRunTime = I.currentTime;
        return;
    }

    const uint32_t downloadBytesTarget = (uint32_t)_NETWORKMONITOR_DOWNLOAD_KB * 1024u;

    char url[96];
    snprintf(url, sizeof(url),
        "https://speed.cloudflare.com/__down?bytes=%lu",
        (unsigned long)downloadBytesTarget);

    WiFiClientSecure client;
    client.setTimeout(DOWNLOAD_HTTP_TIMEOUT_MS);
    client.setHandshakeTimeout(DOWNLOAD_HTTP_TIMEOUT_MS);
    client.setCACertBundle(
        x509_crt_imported_bundle_bin_start,
        (size_t)(x509_crt_imported_bundle_bin_end - x509_crt_imported_bundle_bin_start));

    HTTPClient http;
    if (!http.begin(client, url)) {
        NetworkMonitor.download.lastRunTime = I.currentTime;
        return;
    }

    http.setTimeout(DOWNLOAD_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    uint32_t startMs = millis();
    esp_task_wdt_reset();
    int httpCode = http.GET();
    esp_task_wdt_reset();

    if (httpCode != HTTP_CODE_OK) {
        http.end();
        NetworkMonitor.download.lastRunTime = I.currentTime;
        return;
    }

    size_t bytesRead = drainHttpResponseBody(http, DOWNLOAD_HTTP_TIMEOUT_MS);
    uint32_t elapsedMs = millis() - startMs;
    http.end();

    NetworkMonitor.download.downloadBytes = (uint32_t)bytesRead;
    NetworkMonitor.download.lastRunTime = I.currentTime;

    uint32_t minBytes = (downloadBytesTarget * 95u) / 100u;
    if (bytesRead >= minBytes) {
        NetworkMonitor.download.durationMs = elapsedMs;
        NetworkMonitor.download.success = true;
    }
#endif
}

#endif // _USENETWORKMONITOR > 0
