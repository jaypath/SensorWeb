#pragma once

#include <Arduino.h>

#ifndef _USENETWORKMONITOR
#define _USENETWORKMONITOR 0
#endif

#ifndef _NETWORKMONITOR_DOWNLOAD_KB
#define _NETWORKMONITOR_DOWNLOAD_KB 100
#endif

#if _USENETWORKMONITOR > 0

constexpr uint8_t NETWORK_MONITOR_TEST_COUNT = 7;
constexpr uint8_t NM_PING_BURST_COUNT = 5;
constexpr uint16_t NM_PING_BURST_INTERVAL_MS = 200;
constexpr uint32_t NM_PING_MAX_DURATION_MS = 30000;
constexpr uint32_t NM_DOWNLOAD_MIN_INTERVAL_SEC = 120;

// Local sensors: snsType 81-89. Multiple snsTypes may share one underlying test (85/86 gateway, 87/88 external, 89 download).
enum NetworkMonitorTestIndex : uint8_t {
    NM_TEST_BSSID = 0,
    NM_TEST_LOCAL_IP = 1,
    NM_TEST_DNS = 2,
    NM_TEST_TX_FAILURES = 3,
    NM_TEST_GATEWAY_LATENCY = 4,
    NM_TEST_EXTERNAL_PING = 5,
    NM_TEST_DOWNLOAD = 6,
};

struct NmPingResult {
    uint8_t packetsSent = 0;
    uint8_t packetsLost = 0;
    int16_t rttMs[NM_PING_BURST_COUNT] = {-1, -1, -1, -1, -1};
    int16_t avgRttMs = -1;
    int16_t jitterMs = -1;
};

struct NmBssidTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    int16_t changeCount = 0;
    uint32_t trackedBssid = 0;
};

struct NmLocalIpTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    int16_t changeCount = 0;
    IPAddress oldIP;
};

struct NmDnsTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    int16_t resolutionMs = -1;
};

struct NmTxFailuresTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    uint8_t failureCount = 0;
};

struct NmGatewayLatencyTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    NmPingResult ping;
};

struct NmExternalPingTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    NmPingResult wan;
    NmPingResult gateway;
    bool gatewayChecked = false;
    IPAddress pingTarget = IPAddress(8, 8, 8, 8);
    IPAddress pingTargetBackup = IPAddress(1, 1, 1, 1);
};

struct NmDownloadTest {
    time_t lastAttemptTime = 0;
    bool success = false;
    uint32_t downloadBytes = 0;
    uint32_t durationMs = 0;
    IPAddress sourceIP;
    time_t lastRunTime = 0;
};

void NetworkMonitor_BSSIDChanges();
void NetworkMonitor_LocalIPChanges();
void NetworkMonitor_DNSResolutionTime();
void NetworkMonitor_TxFailures();
void NetworkMonitor_GatewayLatency();
void NetworkMonitor_PingTest();
void NetworkMonitor_DownloadTest();

struct NetworkMonitor_type {
    NmBssidTest bssid;
    NmLocalIpTest localIp;
    NmDnsTest dns;
    NmTxFailuresTest txFailures;
    NmGatewayLatencyTest gatewayLatency;
    NmExternalPingTest externalPing;
    NmDownloadTest download;

    static bool isSensorType(uint8_t snsType);
    static int8_t runTestIndexFromSensorType(uint8_t snsType);
    bool runTest(uint8_t testIndex);
    bool readSensorValue(uint8_t snsType, double& value) const;
    time_t readSensorTime(uint8_t snsType) const;
    static bool isSensorValueInvalid(uint8_t snsType, double value);
    void init();
    void initRuntime();
};

extern NetworkMonitor_type NetworkMonitor;

#endif // _USENETWORKMONITOR > 0
