#pragma once

#include <Arduino.h>

/**
 * @brief Refresh IGMP membership for the configured UDP multicast group.
 */
void NetworkMonitor_IGMP(); // IGMPv2 Membership Report refresh. Target every 60 seconds.
void NetworkMonitor_lowRSSI(); // lowest RSSI of current AP. Target every 10 seconds.
void NetworkMonitor_BSSIDChanges(); // AP switches counter. Target every 60 seconds.
void NetworkMonitor_LocalIPChanges(); // local IP change counter. Target every 600 seconds.
void NetworkMonitor_DNSResolutionTime(); // DNS resolution duration. Target every 600 seconds.
void NetworkMonitor_TxFailures(); // transmission failure count. Target every 60 seconds.
void NetworkMonitor_GatewayLatency(); // gateway ping latency. Target every 600 seconds.


struct NetworkMonitor_type {
    String NetworkTest[7] = {"IGMP", "lowest RSSI", "AP switches", "LocalIP switches", "DNS Resolution Time", "Tx Failures", "Gateway Latency"};
    void (*NetworkTestFunction[7])() = {NetworkMonitor_IGMP, NetworkMonitor_lowRSSI, NetworkMonitor_BSSIDChanges, NetworkMonitor_LocalIPChanges, NetworkMonitor_DNSResolutionTime, NetworkMonitor_TxFailures, NetworkMonitor_GatewayLatency};
    int16_t NetworkTestValue[7] = {0};
    uint16_t PollInterval[7] = {60, 10, 60, 600, 600, 60, 600};    
    time_t lastPollTime[7] = {0};
    time_t lastRSSILowTime = 0;
    uint32_t oldBSSID = 0;
    IPAddress oldIP;
    void init();
    void update();
} ;

extern NetworkMonitor_type NetworkMonitor;
