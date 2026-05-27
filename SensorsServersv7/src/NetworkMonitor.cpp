#include <Arduino.h>
#include <WiFi.h>
#include <lwip/icmp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet_chksum.h>
#include <lwip/igmp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include "globals.hpp"
#include "sensors.hpp"
#include "NetworkMonitor.hpp"

IPAddress MULTICAST_TARGET_GROUP = IPAddress(_USEUDP_MULTICAST);

// Forward declaration of the struct to avoid deep nesting header dependencies


NetworkMonitor_type NetworkMonitor;

void NetworkMonitor_type::init() {
    oldIP = WiFi.localIP();
    oldBSSID = 0;
    lastRSSILowTime = 0;
    for (byte i = 0; i < 7; i++) {
        NetworkTestValue[i] = 0;
        lastPollTime[i] = 0;
    }
}

void NetworkMonitor_type::update() {
    for (byte i = 0; i < 7; i++) {
        if (I.currentTime - lastPollTime[i] > PollInterval[i]) {
            lastPollTime[i] = I.currentTime;
            NetworkTestFunction[i]();
        }
    }
}

/**
 * @brief Refresh IGMP membership for the UDP multicast group.
 * Sends proper IGMPv2 Membership Reports via lwIP so IGMP-snooping routers/APs
 * (Deco, XB10, etc.) keep forwarding 239.x traffic to this station.
 */
void NetworkMonitor_IGMP() {
    if (WiFi.status() != WL_CONNECTED) {
        NetworkMonitor.NetworkTestValue[0] = 0;
        return;
    }


    struct netif *netif = netif_default;
    if (netif == NULL) {
        SerialPrint("NetMonitor: No default netif for IGMP report", true);
        NetworkMonitor.NetworkTestValue[0] = -1;
        return;
    }

    ip4_addr_t groupaddr;
    IP4_ADDR(&groupaddr,
             MULTICAST_TARGET_GROUP[0],
             MULTICAST_TARGET_GROUP[1],
             MULTICAST_TARGET_GROUP[2],
             MULTICAST_TARGET_GROUP[3]);


    // IGMP internals schedule lwIP timers and must run while TCPIP core is locked.
    // Without this, ESP-IDF asserts in sys_timeout() with:
    // "Required to lock TCPIP core functionality!"
    LOCK_TCPIP_CORE();

    // If no UDP broadcast seen recently, force a leave/rejoin recovery.
    // Keep this inside the lwIP core lock to avoid sys_timeout() assert.
    if (I.currentTime - I.UDP_LAST_INCOMINGMSG_TIME > 120) {
        igmp_leavegroup_netif(netif, &groupaddr);
        igmp_joingroup_netif(netif, &groupaddr);
    }

    if (igmp_lookfor_group(netif, &groupaddr) == NULL) {
        // Not joined yet (e.g. connectUDP not run) — join sends an initial Report.
        err_t err = igmp_joingroup_netif(netif, &groupaddr);
        UNLOCK_TCPIP_CORE();

        if (err != ERR_OK) {
            SerialPrint("NetMonitor: igmp_joingroup failed, err=" + String((int)err), true);
            NetworkMonitor.NetworkTestValue[0] = -2;
            return;
        }
        SerialPrint("NetMonitor: Joined multicast group " + MULTICAST_TARGET_GROUP.toString(), true);
        NetworkMonitor.NetworkTestValue[0] = 1;
        return;
    }

    // Re-send IGMPv2 Membership Reports for all groups joined on this interface.
    igmp_report_groups(netif);
    UNLOCK_TCPIP_CORE();

    SerialPrint("NetMonitor: IGMP Membership Report sent for " + MULTICAST_TARGET_GROUP.toString(), true);
    NetworkMonitor.NetworkTestValue[0] = 1;
}
/**
 * @brief Measures the lowest Received Signal Strength Indicator (RSSI) - needs to be reset periodically.
 */
void NetworkMonitor_lowRSSI() {
    if (WiFi.status() != WL_CONNECTED) {
        NetworkMonitor.NetworkTestValue[1] = -999;
        return;
    }
    int16_t rssi = WiFi.RSSI();
    if (rssi < NetworkMonitor.NetworkTestValue[1] || NetworkMonitor.NetworkTestValue[1] == -999 || NetworkMonitor.NetworkTestValue[1] == 0 || NetworkMonitor.lastRSSILowTime == 0) {
        (rssi == 0) ? NetworkMonitor.NetworkTestValue[1] = -999 : NetworkMonitor.NetworkTestValue[1] = rssi;
        NetworkMonitor.lastRSSILowTime = I.currentTime;
    } 
    
}

/**
 * @brief Evaluates the current connected Access Point's MAC address (BSSID).
 */
void NetworkMonitor_BSSIDChanges() {        
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    uint8_t* bssid = WiFi.BSSID();
    uint32_t trackedBytes = (bssid[3] << 16) | (bssid[4] << 8) | bssid[5];
    if (trackedBytes != NetworkMonitor.oldBSSID) {
        NetworkMonitor.NetworkTestValue[2]++;
        NetworkMonitor.oldBSSID = trackedBytes;
    }
}

/**
 * @brief Tracks a single specific octet of the allocated IP address.
 */
void NetworkMonitor_LocalIPChanges() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }


    IPAddress local = WiFi.localIP();
    if (local != NetworkMonitor.oldIP) {
        NetworkMonitor.NetworkTestValue[3]++;
        NetworkMonitor.oldIP = local;
    } 
}


/**
 * @brief Evaluates DNS resolution round-trip duration.
 */
void NetworkMonitor_DNSResolutionTime() {
    if (WiFi.status() != WL_CONNECTED) {
        NetworkMonitor.NetworkTestValue[4] = -1;
        return;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    uint32_t startTime = millis();
    int err = lwip_getaddrinfo("example.com", NULL, &hints, &res);
    uint32_t endTime = millis();

    if (err == 0) {
        freeaddrinfo(res); 
        NetworkMonitor.NetworkTestValue[4] = (int16_t)(endTime - startTime); 
    } else {
        NetworkMonitor.NetworkTestValue[4] = -4; 
    }
}

/**
 * @brief Tracks the total number of transmission failures registered since boot up.
 */
void NetworkMonitor_TxFailures() {
    NetworkMonitor.NetworkTestValue[5] = I.HTTP_OUTGOING_ERRORS;
}

/**
 * @brief Measures ICMP loopback latency directly to the local default gateway.
 */
void NetworkMonitor_GatewayLatency() {
    if (WiFi.status() != WL_CONNECTED) {
        NetworkMonitor.NetworkTestValue[6] = -1; 
        return;
    }

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        NetworkMonitor.NetworkTestValue[6] = -2;
        return;
    }

    struct timeval tv_out;
    tv_out.tv_sec = 0;
    tv_out.tv_usec = 150000; 
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv_out, sizeof(tv_out));

    struct sockaddr_in target;
    target.sin_family = AF_INET;
    target.sin_port = 0;
    target.sin_addr.s_addr = inet_addr("10.0.0.1"); 

    uint8_t ping_pkt[8] = {0};
    ping_pkt[0] = ICMP_ECHO; 
    ping_pkt[1] = 0;        
    ping_pkt[4] = 0xAA;     
    ping_pkt[5] = 0xBB;
    
    uint16_t chk = inet_chksum(ping_pkt, sizeof(ping_pkt));
    memcpy(&ping_pkt[2], &chk, 2);

    uint32_t startTime = millis();
    if (sendto(sock, ping_pkt, sizeof(ping_pkt), 0, (struct sockaddr*)&target, sizeof(target)) < 0) {
        close(sock);
        NetworkMonitor.NetworkTestValue[6] = -3; 
        return;
    }

    uint8_t rcv_buf[64];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    int res = recvfrom(sock, rcv_buf, sizeof(rcv_buf), 0, (struct sockaddr*)&from, &from_len);
    uint32_t endTime = millis();
    close(sock);

    if (res < 0) {
        NetworkMonitor.NetworkTestValue[6] = 999; 
    } else {
        NetworkMonitor.NetworkTestValue[6] = (int16_t)(endTime - startTime); 
    }
}