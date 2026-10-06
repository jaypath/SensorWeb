#include "ArborysMesh.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include "utility.hpp"
#include "BootSecure.hpp"
#include "server.hpp"
#include "device_roles.hpp"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>

#ifdef _USEUDP
WiFiUDP LAN_UDP;
#endif

namespace {

constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

ArborysMeshParams s_params = {};

struct DedupEntry {
    uint32_t truncMac;
    uint16_t seq;
};
DedupEntry s_dedup[ARBORYS_MESH_DEDUP_DEPTH];
uint8_t s_dedupHead = 0;

struct OriginTrack {
    uint32_t truncMac;
    uint16_t highestSeq;
    bool used;
};
OriginTrack s_origins[ARBORYS_MESH_ORIGIN_SLOTS];

struct RelayItem {
    uint8_t frame[ARBORYS_MESH_MAX_FRAME];
    uint16_t len;
    uint32_t sendAtMs;
    uint32_t truncMac;
    uint16_t seq;
    uint8_t dupCount;   // copies heard from other relays while waiting
    bool noSuppress;    // critical or addressed to one node: always relay
    bool rebootAfter;   // critical net error: reboot after this copy is sent
    bool active;
};
constexpr uint8_t RELAY_SLOTS = 6;
RelayItem s_relay[RELAY_SLOTS];

struct PendingAck {
    uint8_t hubMac[6];
    uint16_t seq;
    uint32_t deadlineMs;
    uint8_t retriesLeft;
    bool active;
};
PendingAck s_pendingAck = {};

constexpr uint8_t DELINQUENT_NEIGHBOR_SLOTS = 10;
constexpr uint8_t DELINQUENT_PING_MIN_ATTEMPTS = 4;
constexpr uint32_t DELINQUENT_SILENCE_SEC = 3600;

struct DelinquentNeighbor {
    uint8_t mac[6];
    int8_t rssi; // 0 = named by a hub, not heard as a link-layer transmitter
    bool used;
};
DelinquentNeighbor s_delinq[DELINQUENT_NEIGHBOR_SLOTS] = {};
uint32_t s_lastOriginBroadcastUtc = 0;

bool s_espNowReady = false;
uint16_t s_nextSeq = 0; // randomized in meshInit so a reboot doesn't reuse seqs neighbors still remember
QueueHandle_t s_rxQueue = nullptr;
constexpr uint8_t RX_QUEUE_DEPTH = 16; // frames are drained once per loop; blocking HTTP calls stall that

struct RxItem {
    uint16_t len;
    bool viaUdp;
    int8_t rssi;        // dBm; 0 = unknown (UDP)
    uint8_t srcMac[6];  // link-layer transmitter (relay or origin)
    uint8_t data[ARBORYS_MESH_MAX_FRAME];
};

MeshStats s_stats = {};

// Time sync from BEACON_SYNC (replaces old server ping sync)
bool s_timeSyncActive = false;
bool s_timeSyncReady = false;
uint32_t s_timeSyncUtc = 0;

uint8_t s_linkCheckMinute = 255;
uint8_t s_sweepMinute = 255;
uint8_t s_lastHourHandled = 255;

uint32_t truncMacFrom(const uint8_t mac[6]) {
    uint32_t t = 0;
    memcpy(&t, mac + 2, 4);
    return t;
}

/** CRC16 over header with crc16 zeroed, then ciphertext. Uses BootSecure Fletcher-like CRC. */
uint16_t meshComputeCrc(const uint8_t* frame, uint16_t frameLen) {
    if (frameLen < ARBORYS_MESH_HEADER_SIZE) return 0;
    uint8_t tmp[ARBORYS_MESH_MAX_FRAME];
    memcpy(tmp, frame, frameLen);
    ArborysMeshHeader* h = (ArborysMeshHeader*)tmp;
    h->crc16 = 0;
    return BootSecure::CRCCalculator(tmp, frameLen);
}

void meshStampCrc(uint8_t* frame, uint16_t frameLen) {
    if (frameLen < ARBORYS_MESH_HEADER_SIZE) return;
    ArborysMeshHeader* h = (ArborysMeshHeader*)frame;
    h->crc16 = 0;
    h->crc16 = meshComputeCrc(frame, frameLen);
}

bool meshVerifyCrc(const uint8_t* frame, uint16_t frameLen) {
    if (frameLen < ARBORYS_MESH_HEADER_SIZE) return false;
    ArborysMeshHeader hdr;
    memcpy(&hdr, frame, sizeof(hdr));
    const uint16_t got = hdr.crc16;
    return got == meshComputeCrc(frame, frameLen);
}

bool macEq(const uint8_t a[6], const uint8_t b[6]) {
    return memcmp(a, b, 6) == 0;
}

bool isBcast(const uint8_t mac[6]) {
    return macEq(mac, BROADCAST_MAC);
}

void myMacBytes(uint8_t out[6]) {
    uint64ToMAC(ESP.getEfuseMac(), out);
}

bool wifiRadioUp() {
    return WiFi.getMode() != WIFI_MODE_NULL;
}

uint16_t randRange(uint16_t lo, uint16_t hi) {
    if (hi <= lo) return lo;
    return (uint16_t)(lo + (esp_random() % (uint32_t)(hi - lo + 1)));
}

bool dedupSeen(uint32_t trunc, uint16_t seq) {
    for (uint8_t i = 0; i < ARBORYS_MESH_DEDUP_DEPTH; ++i) {
        if (s_dedup[i].truncMac == trunc && s_dedup[i].seq == seq) return true;
    }
    return false;
}

void dedupAdd(uint32_t trunc, uint16_t seq) {
    s_dedup[s_dedupHead].truncMac = trunc;
    s_dedup[s_dedupHead].seq = seq;
    s_dedupHead = (uint8_t)((s_dedupHead + 1) % ARBORYS_MESH_DEDUP_DEPTH);
}

// Wrap-aware per-origin tracking. Relays add random delays, so frames from one origin can arrive
// out of order; unseen frames behind the highest seq are accepted (dedup already rejected repeats).
bool originAccept(uint32_t trunc, uint16_t seq) {
    int freeSlot = -1;
    for (uint8_t i = 0; i < ARBORYS_MESH_ORIGIN_SLOTS; ++i) {
        if (!s_origins[i].used) {
            if (freeSlot < 0) freeSlot = (int)i;
            continue;
        }
        if (s_origins[i].truncMac != trunc) continue;
        const uint16_t hi = s_origins[i].highestSeq;
        const uint16_t delta = (uint16_t)(seq - hi);
        if (seq == hi) return false; // exact replay of highest
        const uint16_t window = s_params.originReorderWindow;
        if (delta > 0 && delta < window) {
            s_origins[i].highestSeq = seq;
            return true;
        }
        if ((uint16_t)(hi - seq) < window) return true; // late arrival within window
        // large jump / wrap reboot — accept and reset
        s_origins[i].highestSeq = seq;
        return true;
    }
    if (freeSlot < 0) freeSlot = 0; // overwrite slot 0 if full
    s_origins[freeSlot].used = true;
    s_origins[freeSlot].truncMac = trunc;
    s_origins[freeSlot].highestSeq = seq;
    return true;
}

// Counter-based suppression: a copy heard while our relay is pending means a neighbor already covered it.
void noteDuplicate(uint32_t trunc, uint16_t seq) {
    for (uint8_t i = 0; i < RELAY_SLOTS; ++i) {
        if (s_relay[i].active && s_relay[i].truncMac == trunc && s_relay[i].seq == seq) {
            if (s_relay[i].dupCount < 255) s_relay[i].dupCount++;
        }
    }
}

bool macUsable(const uint8_t mac[6]) {
    if (!mac) return false;
    bool any = false;
    bool allFf = true;
    for (uint8_t i = 0; i < 6; ++i) {
        if (mac[i] != 0) any = true;
        if (mac[i] != 0xFF) allFf = false;
    }
    return any && !allFf;
}

int delinquentSlot(const uint8_t mac[6]) {
    if (!macUsable(mac)) return -1;
    for (uint8_t i = 0; i < DELINQUENT_NEIGHBOR_SLOTS; ++i) {
        if (s_delinq[i].used && macEq(s_delinq[i].mac, mac)) return (int)i;
    }
    return -1;
}

// Real RSSI only. A zero means the hub named this MAC and this radio has not heard it.
bool delinquentHeard(const uint8_t mac[6]) {
    const int slot = delinquentSlot(mac);
    return slot >= 0 && s_delinq[slot].rssi < 0;
}

void rememberDelinquentMac(const uint8_t mac[6]) {
    if (!macUsable(mac)) return;
    if (delinquentSlot(mac) >= 0) return;
    int empty = -1;
    int unseen = -1;
    for (uint8_t i = 0; i < DELINQUENT_NEIGHBOR_SLOTS; ++i) {
        if (!s_delinq[i].used) {
            if (empty < 0) empty = (int)i;
            continue;
        }
        if (s_delinq[i].rssi == 0 && unseen < 0) unseen = (int)i;
    }
    const int slot = (empty >= 0) ? empty : unseen;
    if (slot < 0) return; // every slot is a neighbor this node has heard
    memcpy(s_delinq[slot].mac, mac, 6);
    s_delinq[slot].rssi = 0;
    s_delinq[slot].used = true;
}

void noteDelinquentRssi(const uint8_t mac[6], int8_t rssi) {
    if (rssi >= 0) return;
    const int slot = delinquentSlot(mac);
    if (slot < 0) return;
    s_delinq[slot].rssi = rssi;
}

void noteOriginBroadcast() {
    const uint32_t now = (uint32_t)utcNow();
    if (isTimeValid(now)) s_lastOriginBroadcastUtc = now;
}

// This hub's own view. A MAC learned from another hub's broadcast is not copied in.
bool hubDeviceIsDelinquent(const ArborysDevType* d) {
    if (!d || !d->IsSet || d->MAC == 0) return false;
    if (d->MAC == ESP.getEfuseMac()) return false;
    if (Devices_Sensors::isServerPlaceholderMac(d->MAC)) return false;
    const uint32_t now = (uint32_t)utcNow();
    uint32_t silence = 0xFFFFFFFFu;
    if (isTimeValid(now) && d->dataReceived != 0 && now >= d->dataReceived) silence = now - d->dataReceived;
    const bool unheard = silence >= DELINQUENT_SILENCE_SEC;
    const bool lowPing = d->ping_att_ESPNow >= DELINQUENT_PING_MIN_ATTEMPTS
        && pingSuccessRatePercent(d->ping_success_ESPNow, d->ping_att_ESPNow) < 50;
    return unheard || lowPing;
}

bool queueRelay(const uint8_t* frame, uint16_t len, uint32_t trunc, uint16_t seq, bool critical, bool directed,
                int8_t rssi, bool rebootAfter, const uint8_t* linkSrc, const uint8_t* destMac) {
    int slot = -1;
    for (uint8_t i = 0; i < RELAY_SLOTS; ++i) {
        if (!s_relay[i].active) { slot = (int)i; break; }
    }
    if (slot < 0) return false;
    if (len > ARBORYS_MESH_MAX_FRAME) return false;
    memcpy(s_relay[slot].frame, frame, len);
    s_relay[slot].len = len;
    s_relay[slot].truncMac = trunc;
    s_relay[slot].seq = seq;
    s_relay[slot].dupCount = 0;
    // Directed frames (pings, ACKs, sensor requests) are rare; suppressing them strands the one relay
    // that can reach the destination when other relays heard nearby cover a different area.
    s_relay[slot].noSuppress = critical || directed;
    s_relay[slot].rebootAfter = rebootAfter;
    s_relay[slot].active = true;
    // Short hold when this radio has heard the destination, or heard the transmitter of a broadcast.
    // RSSI 0 (named, never heard) does not qualify. Every other relay keeps the previous-hop curve.
    const bool prefer = (destMac && delinquentHeard(destMac))
        || (destMac && isBcast(destMac) && linkSrc && delinquentHeard(linkSrc));
    s_relay[slot].sendAtMs = millis() + s_params.relayHoldMs(rssi, critical || prefer);
    return true;
}

bool addBroadcastPeer() {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_is_peer_exist(BROADCAST_MAC)) return true;
    return esp_now_add_peer(&peer) == ESP_OK;
}

void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!s_rxQueue || len < (int)ARBORYS_MESH_HEADER_SIZE || len > (int)ARBORYS_MESH_MAX_FRAME) return;
    if (data[0] != ARBORYS_MESH_NETWORK_ID) return;
    RxItem item = {};
    item.len = (uint16_t)len;
    item.viaUdp = false;
    item.rssi = (info && info->rx_ctrl) ? (int8_t)info->rx_ctrl->rssi : 0;
    if (info && info->src_addr) memcpy(item.srcMac, info->src_addr, 6);
    memcpy(item.data, data, (size_t)len);
    if (xQueueSend(s_rxQueue, &item, 0) != pdTRUE) s_stats.rxDropped++;
}

// Every mesh frame goes out as an 802.11 broadcast; the mesh destination lives in hdr.dest_mac.
// An ESP-NOW unicast is dropped by every radio except the addressee, so relays would never hear it.
bool sendEspNowBytes(const uint8_t* frame, uint16_t len) {
    if (!meshEnsure()) return false;
    if (!addBroadcastPeer()) return false;
    esp_err_t r = esp_now_send(BROADCAST_MAC, frame, len);
    if (r == ESP_OK) {
        I.MESH_SENDS++;
        I.MESH_LAST_OUTGOINGMSG_TIME = utcNow();
        if (len >= ARBORYS_MESH_HEADER_SIZE) {
            ArborysMeshHeader hdr;
            memcpy(&hdr, frame, sizeof(hdr));
            I.MESH_LAST_OUTGOINGMSG_TO_MAC = MACToUint64(hdr.dest_mac);
            I.MESH_LAST_OUTGOINGMSG_TYPE = meshHdrMsgType(hdr.ttl_type);
        }
        return true;
    }
    I.MESH_OUTGOING_ERRORS++;
    return false;
}

bool sendUdpBytes(const uint8_t* frame, uint16_t len) {
#ifdef _USEUDP
    return sendUDPMessage(frame, IPAddress(0, 0, 0, 0), len, "ArborysMesh");
#else
    (void)frame; (void)len;
    return false;
#endif
}

bool buildAndSend(uint8_t msgType, const uint8_t destMac[6], const uint8_t* plain, uint8_t plainLen,
                  uint8_t ttl, uint16_t* seqOut, bool forceUdpAlso) {
    if (!isValidLMKKey() && plainLen > 0) return false;

    ArborysMeshHeader hdr = {};
    hdr.network_id = ARBORYS_MESH_NETWORK_ID;
    myMacBytes(hdr.origin_mac);
    memcpy(hdr.dest_mac, destMac, 6);
    while (s_nextSeq == 0) s_nextSeq = (uint16_t)esp_random();
    hdr.seq_num = s_nextSeq++;
    if (s_nextSeq == 0) s_nextSeq = 1;
    hdr.ttl_type = meshMakeTtlType(ttl, msgType);
    if (seqOut) *seqOut = hdr.seq_num;

    uint8_t frame[ARBORYS_MESH_MAX_FRAME];
    uint16_t frameLen = ARBORYS_MESH_HEADER_SIZE;

    if (plainLen == 0) {
        hdr.cipher_len = 0;
        memset(hdr.iv, 0, 16);
        hdr.crc16 = 0;
        memcpy(frame, &hdr, sizeof(hdr));
        meshStampCrc(frame, frameLen);
    } else {
        esp_fill_random(hdr.iv, 16);
        uint8_t cipher[ARBORYS_MESH_MAX_CIPHER];
        uint16_t outLen = 0;
        if (BootSecure::encryptWithIV(plain, plainLen, (char*)Prefs.KEYS.ESPNOW_KEY, hdr.iv, cipher, &outLen, 16) != 1) {
            return false;
        }
        if (outLen > ARBORYS_MESH_MAX_CIPHER || outLen > 255) return false;
        hdr.cipher_len = (uint8_t)outLen;
        hdr.crc16 = 0;
        memcpy(frame, &hdr, sizeof(hdr));
        memcpy(frame + ARBORYS_MESH_HEADER_SIZE, cipher, outLen);
        frameLen = (uint16_t)(ARBORYS_MESH_HEADER_SIZE + outLen);
        meshStampCrc(frame, frameLen);
    }

    const bool critical = (msgType == MSG_CRITICAL_EVENT) ||
                          (msgType == MSG_BROADCAST_NO_ACK && plainLen > 0 && plain[0] == MSG_BCST_CRITICAL_NET_ERROR);
    const uint8_t k = critical ? s_params.criticalBurstCount : 1;

    bool ok = false;
    for (uint8_t i = 0; i < k; ++i) {
        if (i > 0) delay(randRange(s_params.criticalBurstGapMinMs, s_params.criticalBurstGapMaxMs));
        ok |= sendEspNowBytes(frame, frameLen);
    }
    if (forceUdpAlso || I.espNowFailed) {
        ok |= sendUdpBytes(frame, frameLen);
    }

    dedupAdd(truncMacFrom(hdr.origin_mac), hdr.seq_num);
    return ok;
}

bool decryptPayload(const ArborysMeshHeader& hdr, const uint8_t* cipher, uint8_t* plainOut, uint8_t* plainLenOut) {
    if (hdr.cipher_len == 0) {
        *plainLenOut = 0;
        return true;
    }
    if (!isValidLMKKey()) return false;
    uint8_t tmp[ARBORYS_MESH_MAX_CIPHER];
    if (BootSecure::decryptWithIV((uint8_t*)cipher, (char*)Prefs.KEYS.ESPNOW_KEY, (uint8_t*)hdr.iv, tmp, hdr.cipher_len, 16) != 1) {
        return false;
    }
    // Zero-padded CBC: trim trailing zeros for convenience (payload structs are fixed-size when used)
    uint8_t len = hdr.cipher_len;
    while (len > 0 && tmp[len - 1] == 0) len--;
    // If payload was exact multiple of 16 with trailing zero data, callers use fixed sizes — restore full padded for struct copies
    // Prefer returning full cipher_len for memcpy of known structs; trim only for subtype-first variable messages.
    memcpy(plainOut, tmp, hdr.cipher_len);
    *plainLenOut = hdr.cipher_len;
    (void)len;
    return true;
}

void applyBeaconClock(uint32_t utc, int32_t tzOff, int16_t dstOff, uint8_t dstState) {
    if (wifiReadyForNetwork()) return; // NTP/WiFi preferred
    if (utc < TIMEZERO) return;
    I.UTCTime = utc;
    Prefs.TimeZoneOffset = tzOff;
    Prefs.DSTOffset = dstOff;
    Prefs.DST = dstState;
    setTime(utc);
    I.currentTime = I.UTCTime + Prefs.TimeZoneOffset + (Prefs.DST > 0 ? (Prefs.DST - 1) * Prefs.DSTOffset : 0);
    if (s_timeSyncActive) {
        s_timeSyncUtc = utc;
        s_timeSyncReady = true;
    }
}

void handlePlain(const ArborysMeshHeader& hdr, const uint8_t* plain, uint8_t plainLen, bool viaUdp) {
    const uint8_t msgType = meshHdrMsgType(hdr.ttl_type);
    uint8_t my[6];
    myMacBytes(my);
    const bool forMe = isBcast(hdr.dest_mac) || macEq(hdr.dest_mac, my);

    // Transport-specific RX accounting: ESP-NOW → MESH_*; UDP → already counted in closeUDP / UDP_*
    if (!viaUdp) {
        I.MESH_RECEIVES++;
        I.MESH_LAST_INCOMINGMSG_TIME = utcNow();
        I.MESH_LAST_INCOMINGMSG_FROM_MAC = MACToUint64(hdr.origin_mac);
        I.MESH_LAST_INCOMINGMSG_TYPE = msgType;
    } else {
        I.UDP_LAST_INCOMINGMSG_TIME = utcNow();
        // type string set by UDP demux ("Mesh"); keep numeric mirror unused on UDP path
    }

    if (msgType == MSG_ACK && forMe) {
        uint16_t echo = hdr.seq_num; // pre-11.0.4 ACKs reuse the request seq in the header
        if (plainLen >= 2) memcpy(&echo, plain, 2);
        if (s_pendingAck.active && s_pendingAck.seq == echo) {
            s_pendingAck.active = false;
            I.espNowFailed = false;
        } else if (s_pendingAck.active && macEq(s_pendingAck.hubMac, hdr.origin_mac)) {
            // some stacks echo differently — also clear if ACK from awaited hub
            s_pendingAck.active = false;
            I.espNowFailed = false;
        }
        return;
    }

    if (msgType == MSG_UNICAST_ACK_REQ && forMe && plainLen >= 1) {
        if (plain[0] == MSG_UNIACK_LINK_CHECK) {
            meshSendAck(hdr.origin_mac, hdr.seq_num);
        } else if (plain[0] == MSG_UNIACK_SNS_REQ_EXPIRED) {
#if defined(_USELOWPOWER)
            // Low-power nodes stay silent. No ACK, so the hub labels them expired.
            return;
#else
            int16_t my = Sensors.findMyDeviceIndex();
            ArborysDevType* me = Sensors.getDeviceByDevIndex(my);
            if (me && bitRead(me->Flags, 2)) return;
            meshSendAck(hdr.origin_mac, hdr.seq_num);
            noteExpiredDataRequest(MACToUint64(hdr.origin_mac));
#endif
        }
        return;
    }

    if (msgType == MSG_TELEMETRY) {
        if (viaUdp && !_IS_SERVER_HUB) return; // peripherals ignore UDP telemetry
        if (!_IS_SERVER_HUB && !IS_SERVER_DEVICE_TYPE(_MYTYPE)) {
            // non-hub peripheral: still may hear; only hubs store remote sensors
            if (!_IS_SERVER_HUB) return;
        }
        if (plainLen < sizeof(ArborysMeshDevWire) + sizeof(ArborysMeshSnsWire)) return;
        ArborysMeshDevWire dw;
        ArborysMeshSnsWire sw;
        memcpy(&dw, plain, sizeof(dw));
        memcpy(&sw, plain + sizeof(dw), sizeof(sw));
        meshUnpackTelemetryToSensors(dw, sw);
        return;
    }

    if (msgType == MSG_NODE_ANNOUNCE) {
        if (!_IS_SERVER_HUB) return;
        if (plainLen < sizeof(ArborysMeshDevWire)) return;
        ArborysMeshDevWire dw;
        memcpy(&dw, plain, sizeof(dw));
        meshUnpackDevToSensors(dw);
        return;
    }

    if (msgType == MSG_BEACON_SYNC) {
        // utc(4)+tz(4)+dstOff(2)+dst(1)+dev
        constexpr uint8_t kMeta = 4 + 4 + 2 + 1;
        if (plainLen < kMeta + sizeof(ArborysMeshDevWire)) return;
        uint32_t utc = 0;
        int32_t tz = 0;
        int16_t dstOff = 0;
        uint8_t dst = 0;
        memcpy(&utc, plain, 4);
        memcpy(&tz, plain + 4, 4);
        memcpy(&dstOff, plain + 8, 2);
        dst = plain[10];
        ArborysMeshDevWire dw;
        memcpy(&dw, plain + kMeta, sizeof(dw));
        meshUnpackDevToSensors(dw);
        applyBeaconClock(utc, tz, dstOff, dst);
        noteServerHeard(dw.devType);
        return;
    }

    if (msgType == MSG_BROADCAST_NO_ACK && plainLen >= 1) {
        const uint8_t sub = plain[0];
        if (sub == MSG_BCST_HUB_SWEEP) {
            // subtype + DevWire + channel + ssid[33]
            const uint8_t* p = plain + 1;
            if (plainLen < 1 + sizeof(ArborysMeshDevWire) + 1 + 1) return;
            ArborysMeshDevWire dw;
            memcpy(&dw, p, sizeof(dw));
            p += sizeof(dw);
            uint8_t ch = *p++;
            char ssid[33] = {};
            uint8_t slen = *p++;
            if (slen > 32) slen = 32;
            if (plainLen < (uint8_t)(p - plain) + slen) return;
            memcpy(ssid, p, slen);
            meshUnpackDevToSensors(dw);
            if (ch >= 1 && ch <= 14) {
                // Orphans / not-on-router: retune. Healthy STA already on router — skip if associated.
                if (WiFi.status() != WL_CONNECTED) {
                    setWifiRfChannel(ch);
                    I.WifiChannel = ch;
                }
            }
            (void)ssid; // SSID available for future join assist; password still from setup
            return;
        }
        if (sub == MSG_BCST_DELINQUENT) {
            const uint8_t* p = plain + 1;
            uint8_t left = (uint8_t)(plainLen - 1);
            while (left >= 6) {
                rememberDelinquentMac(p);
                p += 6;
                left = (uint8_t)(left - 6);
            }
            return;
        }
        if (sub == MSG_BCST_CRITICAL_NET_ERROR) {
            // Still relayable: processFrame queues the rebroadcast and serviceRelays reboots after sending.
            if (meshHdrTtl(hdr.ttl_type) > 1) return;
            delay(100);
            recordRebootIssue(RESET_UNKNOWN);
            ESP.restart();
            return;
        }
        return;
    }

    if (msgType == MSG_UNICAST_NO_ACK && forMe && plainLen >= 1 && _IS_SERVER_HUB) {
        const uint8_t sub = plain[0];
        if (sub == MSG_UNI_HUB_SENSOR_REQ && plainLen >= 1 + 6 + 2) {
            uint64_t mac = 0;
            memcpy(&mac, plain + 1, 6);
            // MAC stored as 6 bytes on wire — reconstruct like MACToUint64
            uint8_t m6[6];
            memcpy(m6, plain + 1, 6);
            mac = MACToUint64(m6);
            uint8_t snsType = plain[7];
            uint8_t snsID = plain[8];
            int16_t di = Sensors.findDevice(mac);
            int16_t si = (di >= 0) ? Sensors.findSensor(di, snsType, snsID) : -1;
            uint8_t reply[1 + sizeof(ArborysMeshDevWire) + sizeof(ArborysMeshSnsWire)];
            if (si < 0) {
                uint8_t err[1 + 6 + 2];
                err[0] = MSG_UNI_ERR_NOT_FOUND;
                memcpy(err + 1, m6, 6);
                err[7] = snsType;
                err[8] = snsID;
                meshSendRaw(MSG_UNICAST_NO_ACK, hdr.origin_mac, err, sizeof(err), s_params.ttlNormal, 1, false);
                return;
            }
            ArborysSnsType* S = Sensors.snsIndexToPointer(si);
            ArborysDevType* D = Sensors.getDeviceByDevIndex(S->deviceIndex);
            if (!D) return;
            if (S->expired) {
                uint8_t err[1 + 6 + 2];
                err[0] = MSG_UNI_ERR_EXPIRED;
                memcpy(err + 1, m6, 6);
                err[7] = snsType;
                err[8] = snsID;
                meshSendRaw(MSG_UNICAST_NO_ACK, hdr.origin_mac, err, sizeof(err), s_params.ttlNormal, 1, false);
                return;
            }
            reply[0] = MSG_UNI_HUB_SENSOR_REPLY;
            ArborysMeshDevWire dw;
            ArborysMeshSnsWire sw;
            meshPackDev(dw, D);
            meshPackSns(sw, S);
            memcpy(reply + 1, &dw, sizeof(dw));
            memcpy(reply + 1 + sizeof(dw), &sw, sizeof(sw));
            meshSendRaw(MSG_UNICAST_NO_ACK, hdr.origin_mac, reply, sizeof(reply), s_params.ttlNormal, 1, false);
            return;
        }
        if (sub == MSG_UNI_HUB_SENSOR_REPLY && plainLen >= 1 + sizeof(ArborysMeshDevWire) + sizeof(ArborysMeshSnsWire)) {
            ArborysMeshDevWire dw;
            ArborysMeshSnsWire sw;
            memcpy(&dw, plain + 1, sizeof(dw));
            memcpy(&sw, plain + 1 + sizeof(dw), sizeof(sw));
            meshUnpackTelemetryToSensors(dw, sw);
            return;
        }
        return;
    }

    if (msgType == MSG_RESERVED_EXPANSION) {
        // stub
        return;
    }
}

// Broadcast dest is not "for me" by itself. Only types this node acts on are opened.
// Telemetry and announces are hub state; other nodes relay that ciphertext unchanged.
bool nodeConsumesBroadcast(uint8_t msgType) {
    switch (msgType) {
        case MSG_BEACON_SYNC:       // clock and hub identity live in the payload
        case MSG_BROADCAST_NO_ACK:  // sweep vs critical-net-error subtype lives in the payload
            return true;
        case MSG_TELEMETRY:
        case MSG_NODE_ANNOUNCE:
            return _IS_SERVER_HUB;
        default:
            return false;
    }
}

void processFrame(const uint8_t* data, uint16_t len, bool viaUdp, int8_t rssi, const uint8_t* linkSrc) {
    if (len < ARBORYS_MESH_HEADER_SIZE) return;
    ArborysMeshHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.network_id != ARBORYS_MESH_NETWORK_ID) return;
    if ((uint16_t)(ARBORYS_MESH_HEADER_SIZE + hdr.cipher_len) != len) return;

    // CRC first — fail aborts all further processing for every message type
    if (!meshVerifyCrc(data, len)) {
        if (viaUdp) I.UDP_INCOMING_ERRORS++;
        else I.MESH_INCOMING_ERRORS++;
        return;
    }

    const uint32_t trunc = truncMacFrom(hdr.origin_mac);
    if (dedupSeen(trunc, hdr.seq_num)) {
        noteDuplicate(trunc, hdr.seq_num);
        return;
    }
    if (!originAccept(trunc, hdr.seq_num)) return;
    dedupAdd(trunc, hdr.seq_num);

    uint8_t my[6];
    myMacBytes(my);
    const uint8_t ttl = meshHdrTtl(hdr.ttl_type);
    const uint8_t msgType = meshHdrMsgType(hdr.ttl_type);
    const bool critical = (msgType == MSG_CRITICAL_EVENT);
    const bool addressedToMe = macEq(hdr.dest_mac, my);
    const bool bcast = isBcast(hdr.dest_mac);
    // Header fields (dest, type, ttl, seq) are cleartext. Open the payload only when this
    // node acts on it. A frame for someone else is rebroadcast from the header alone.
    const bool consume = addressedToMe || (bcast && nodeConsumesBroadcast(msgType));

    uint8_t plain[ARBORYS_MESH_MAX_CIPHER];
    uint8_t plainLen = 0;
    bool netErr = false;

    if (consume) {
        if (hdr.cipher_len == 0) {
            handlePlain(hdr, plain, 0, viaUdp);
        } else if (!decryptPayload(hdr, data + ARBORYS_MESH_HEADER_SIZE, plain, &plainLen)) {
            if (viaUdp) I.UDP_INCOMING_ERRORS++;
            else I.MESH_INCOMING_ERRORS++;
            if (addressedToMe) return;
        } else {
            if (msgType == MSG_BROADCAST_NO_ACK && plainLen >= 1 && plain[0] == MSG_BCST_CRITICAL_NET_ERROR) {
                netErr = true;
            }
            handlePlain(hdr, plain, plainLen, viaUdp);
        }
    }

    // Unicast to this node stops here. Any other dest may be relayed while TTL remains.
    if (addressedToMe) return;
    if (ttl <= 1) return;

    uint8_t outFrame[ARBORYS_MESH_MAX_FRAME];
    memcpy(outFrame, data, len);
    ArborysMeshHeader* oh = (ArborysMeshHeader*)outFrame;
    oh->ttl_type = meshMakeTtlType((uint8_t)(ttl - 1), msgType);
    meshStampCrc(outFrame, len); // TTL change invalidates prior CRC

    const bool critFwd = critical || netErr;
    if (!queueRelay(outFrame, len, trunc, hdr.seq_num, critFwd, !bcast, rssi, netErr, linkSrc, hdr.dest_mac) && netErr) {
        // No relay slot: rebroadcast now, then reboot as the handler would have.
        sendEspNowBytes(outFrame, len);
        delay(50);
        recordRebootIssue(RESET_UNKNOWN);
        ESP.restart();
    }
}

void serviceRelays() {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < RELAY_SLOTS; ++i) {
        if (!s_relay[i].active) continue;
        if ((int32_t)(now - s_relay[i].sendAtMs) < 0) continue;
        if (!s_relay[i].noSuppress && s_relay[i].dupCount >= s_params.relaySuppressCount) {
            s_relay[i].active = false;
            s_stats.relaysSuppressed++;
            continue;
        }
        if (sendEspNowBytes(s_relay[i].frame, s_relay[i].len)) s_stats.relaysSent++;
        // Critical net error was identified when the frame was queued (payload already open).
        if (s_relay[i].rebootAfter) {
            s_relay[i].active = false;
            delay(50);
            recordRebootIssue(RESET_UNKNOWN);
            ESP.restart();
        }
        s_relay[i].active = false;
    }
}

void servicePendingAck() {
    if (!s_pendingAck.active) return;
    if ((int32_t)(millis() - s_pendingAck.deadlineMs) < 0) return;
    if (s_pendingAck.retriesLeft > 0) {
        s_pendingAck.retriesLeft--;
        uint8_t payload = MSG_UNIACK_LINK_CHECK;
        uint16_t seq = 0;
        buildAndSend(MSG_UNICAST_ACK_REQ, s_pendingAck.hubMac, &payload, 1, s_params.ttlNormal, &seq, false);
        s_pendingAck.seq = seq;
        s_pendingAck.deadlineMs = millis() + s_params.ackTimeoutMs;
        return;
    }
    s_pendingAck.active = false;
    I.espNowFailed = true;
}

void pickHourlyMinutes() {
    if (s_linkCheckMinute == 255) s_linkCheckMinute = (uint8_t)(esp_random() % 60);
    if (s_sweepMinute == 255) s_sweepMinute = (uint8_t)(esp_random() % 60);
}

#if _IS_SERVER_HUB
bool meshSendDelinquentList() {
    struct Cand {
        uint64_t mac;
        uint32_t silence;
        uint8_t rate;
        bool unheard;
    };
    Cand cand[NUMDEVICES];
    uint8_t n = 0;
    const uint32_t now = (uint32_t)utcNow();
    for (int16_t i = 0; i < NUMDEVICES && n < NUMDEVICES; ++i) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
        if (!hubDeviceIsDelinquent(d)) continue;
        uint32_t silence = 0xFFFFFFFFu;
        if (isTimeValid(now) && d->dataReceived != 0 && now >= d->dataReceived) silence = now - d->dataReceived;
        cand[n].mac = d->MAC;
        cand[n].silence = silence;
        cand[n].unheard = silence >= DELINQUENT_SILENCE_SEC;
        cand[n].rate = (d->ping_att_ESPNow >= DELINQUENT_PING_MIN_ATTEMPTS)
            ? pingSuccessRatePercent(d->ping_success_ESPNow, d->ping_att_ESPNow) : 100;
        n++;
    }
    for (uint8_t a = 1; a < n; ++a) {
        Cand key = cand[a];
        int b = (int)a - 1;
        while (b >= 0) {
            const Cand& earlier = cand[b];
            const bool keyMore = (key.unheard != earlier.unheard) ? key.unheard
                : (key.unheard ? key.silence > earlier.silence : key.rate < earlier.rate);
            if (!keyMore) break;
            cand[b + 1] = earlier;
            b--;
        }
        cand[b + 1] = key;
    }

    constexpr uint8_t kFit = (uint8_t)((ARBORYS_MESH_MAX_CIPHER - 1) / 6);
    if (n > kFit) n = kFit;
    if (n == 0) return false;

    uint8_t plain[ARBORYS_MESH_MAX_CIPHER];
    plain[0] = MSG_BCST_DELINQUENT;
    for (uint8_t i = 0; i < n; ++i) uint64ToMAC(cand[i].mac, plain + 1 + i * 6);
    const uint8_t plainLen = (uint8_t)(1 + n * 6);
    return buildAndSend(MSG_BROADCAST_NO_ACK, BROADCAST_MAC, plain, plainLen, s_params.ttlNormal, nullptr, false);
}
#endif

void serviceHourly() {
    pickHourlyMinutes();
    if (!isTimeValid((uint32_t)utcNow())) return;
    const uint8_t h = hour();
    const uint8_t m = minute();
    if (h == s_lastHourHandled) return;

#if !_IS_SERVER_HUB
    if (m == s_linkCheckMinute) {
        const uint32_t now = (uint32_t)utcNow();
        const bool spoke = s_lastOriginBroadcastUtc != 0 && now >= s_lastOriginBroadcastUtc
            && (now - s_lastOriginBroadcastUtc) < DELINQUENT_SILENCE_SEC;
        if (!spoke) meshSendNodeAnnounce();
        meshSendUniAckReqToHubs();
        s_lastHourHandled = h;
    }
#else
    if (m == s_sweepMinute) {
        meshSendDelinquentList();
        meshSendHubSweep();
        s_lastHourHandled = h;
    }
#endif
}

uint8_t clampU8(int v, int lo, int hi) {
    if (v < lo) return (uint8_t)lo;
    if (v > hi) return (uint8_t)hi;
    return (uint8_t)v;
}

uint16_t clampU16(int v, int lo, int hi) {
    if (v < lo) return (uint16_t)lo;
    if (v > hi) return (uint16_t)hi;
    return (uint16_t)v;
}

int8_t clampRssi(int v) {
    if (v < -100) return -100;
    if (v > -20) return -20;
    return (int8_t)v;
}

ArborysMeshParams sanitizeMeshParams(ArborysMeshParams p) {
    p.ttlNormal = clampU8(p.ttlNormal, 1, 15);
    p.ttlCritical = clampU8(p.ttlCritical, 1, 15);
    p.criticalBurstCount = clampU8(p.criticalBurstCount, 1, 8);
    p.criticalBurstGapMinMs = clampU16(p.criticalBurstGapMinMs, 0, 1000);
    p.criticalBurstGapMaxMs = clampU16(p.criticalBurstGapMaxMs, p.criticalBurstGapMinMs, 1000);
    p.relaySuppressCount = clampU8(p.relaySuppressCount, 1, 255);
    p.ackTimeoutMs = clampU16(p.ackTimeoutMs, 50, 10000);
    p.ackRetries = clampU8(p.ackRetries, 0, 8);

    p.relayRssiWeakDbm = clampRssi(p.relayRssiWeakDbm);
    p.relayRssiStrongDbm = clampRssi(p.relayRssiStrongDbm);
    if (p.relayRssiStrongDbm <= p.relayRssiWeakDbm) {
        p.relayRssiWeakDbm = -90;
        p.relayRssiStrongDbm = -40;
    }
    p.relayDelayWeakMs = clampU16(p.relayDelayWeakMs, 0, 5000);
    p.relayDelayStrongMs = clampU16(p.relayDelayStrongMs, p.relayDelayWeakMs, 5000);
    p.relayJitterMs = clampU16(p.relayJitterMs, 0, 1000);
    p.criticalRelayMinMs = clampU16(p.criticalRelayMinMs, 0, 1000);
    p.criticalRelayMaxMs = clampU16(p.criticalRelayMaxMs, p.criticalRelayMinMs, 1000);
    p.originReorderWindow = clampU16(p.originReorderWindow, 1, 4096);
    return p;
}

} // namespace

uint16_t ArborysMeshParams::relayHoldMs(int8_t rssi, bool critical) const {
    if (critical) return randRange(criticalRelayMinMs, criticalRelayMaxMs);

    int32_t frac256 = 128; // rssi 0, or any non-negative value, is "unknown" (UDP)
    const int32_t span = (int32_t)relayRssiStrongDbm - (int32_t)relayRssiWeakDbm;
    if (rssi < 0 && span > 0) {
        int32_t r = rssi;
        if (r < relayRssiWeakDbm) r = relayRssiWeakDbm;
        if (r > relayRssiStrongDbm) r = relayRssiStrongDbm;
        frac256 = ((r - relayRssiWeakDbm) * 256) / span;
    }
    int32_t weak = relayDelayWeakMs;
    int32_t strong = relayDelayStrongMs;
    if (strong < weak) strong = weak;
    const int32_t base = weak + ((strong - weak) * frac256) / 256;
    return (uint16_t)((base < 0 ? 0 : base) + randRange(0, relayJitterMs));
}

const ArborysMeshParams& meshParams() {
    return s_params;
}

void meshSetParams(const ArborysMeshParams& params) {
    s_params = sanitizeMeshParams(params);
}

static bool inClosedRange(long v, long lo, long hi) {
    return v >= lo && v <= hi;
}

bool meshParamsInRange(const ArborysMeshParams& p) {
    if (!inClosedRange(p.ttlNormal, 1, 15)) return false;
    if (!inClosedRange(p.ttlCritical, 1, 15)) return false;
    if (!inClosedRange(p.criticalBurstCount, 1, 8)) return false;
    if (!inClosedRange(p.criticalBurstGapMinMs, 0, 1000)) return false;
    if (!inClosedRange(p.criticalBurstGapMaxMs, p.criticalBurstGapMinMs, 1000)) return false;
    if (!inClosedRange(p.relaySuppressCount, 1, 255)) return false;
    if (!inClosedRange(p.ackTimeoutMs, 50, 10000)) return false;
    if (!inClosedRange(p.ackRetries, 0, 8)) return false;
    if (!inClosedRange(p.relayRssiWeakDbm, -100, -20)) return false;
    if (!inClosedRange(p.relayRssiStrongDbm, -100, -20)) return false;
    if (p.relayRssiStrongDbm <= p.relayRssiWeakDbm) return false;
    if (!inClosedRange(p.relayDelayWeakMs, 0, 5000)) return false;
    if (!inClosedRange(p.relayDelayStrongMs, p.relayDelayWeakMs, 5000)) return false;
    if (!inClosedRange(p.relayJitterMs, 0, 1000)) return false;
    if (!inClosedRange(p.criticalRelayMinMs, 0, 1000)) return false;
    if (!inClosedRange(p.criticalRelayMaxMs, p.criticalRelayMinMs, 1000)) return false;
    if (!inClosedRange(p.originReorderWindow, 1, 4096)) return false;
    return true;
}

const MeshStats& meshStats() {
    return s_stats;
}

bool isLMKConfigured() {
    for (int i = 0; i < 16; i++) {
        if (Prefs.KEYS.ESPNOW_KEY[i] != 0) return true;
    }
    return false;
}

bool isValidLMKKey() {
    return isLMKConfigured();
}

void meshPackDev(ArborysMeshDevWire& out, const ArborysDevType* d) {
    memset(&out, 0, sizeof(out));
    if (!d) return;
    out.MAC = d->MAC;
    out.IP = (uint32_t)d->IP;
    out.devType = d->devType;
    strncpy(out.devName, d->devName, sizeof(out.devName) - 1);
    memcpy(out.firmware, d->firmware.v, 3);
    out.Flags = d->Flags;
    out.SendingInt = d->SendingInt;
}

void meshPackSns(ArborysMeshSnsWire& out, const ArborysSnsType* s) {
    memset(&out, 0, sizeof(out));
    if (!s) return;
    out.snsType = s->snsType;
    out.snsID = s->snsID;
    strncpy(out.snsName, s->snsName, sizeof(out.snsName) - 1);
    out.snsValue = (float)s->snsValue;
    out.timeRead = s->timeRead;
    out.timeLogged = s->timeLogged;
    out.Flags = s->Flags;
    out.SendingInt = s->SendingInt;
    out.PollingInt = s->PollingInt;
    out.snsPin = s->snsPin;
    out.powerPin = s->powerPin;
    out.OverrideFlags = s->OverrideFlags;
    out.limitHigh = s->limitHigh;
    out.limitLow = s->limitLow;
}

bool meshUnpackDevToSensors(const ArborysMeshDevWire& w) {
    FirmwareVersion fw;
    memcpy(fw.v, w.firmware, 3);
    IPAddress ip(w.IP);
    int16_t idx = Sensors.addDevice(w.MAC, ip, w.devName, w.SendingInt, w.Flags, w.devType, &fw);
    return idx >= 0;
}

bool meshUnpackTelemetryToSensors(const ArborysMeshDevWire& d, const ArborysMeshSnsWire& s) {
    if (!meshUnpackDevToSensors(d)) return false;
    IPAddress ip(d.IP);
    int16_t si = Sensors.addSensor(d.MAC, ip, s.snsType, s.snsID, s.snsName, (double)s.snsValue,
                                   s.timeRead, s.timeLogged ? s.timeLogged : (uint32_t)utcNow(),
                                   s.SendingInt, s.Flags, d.devName, d.devType,
                                   s.snsPin, s.powerPin, s.limitHigh, s.limitLow, true, true);
    if (si < 0) return false;
    ArborysSnsType* S = Sensors.snsIndexToPointer(si);
    if (S) {
        S->PollingInt = s.PollingInt;
        // OverrideFlags are this hub's choice. The sender's value is not applied,
        // so a first-registration default and later user edits both stay.
        S->timeLogged = (uint32_t)utcNow();
    }
    return true;
}

int8_t meshInit() {
    if (!wifiRadioUp()) {
        s_espNowReady = false;
        return -1;
    }
    if (s_espNowReady) return 1;
    while (s_nextSeq == 0) s_nextSeq = (uint16_t)esp_random();
    if (esp_now_init() != ESP_OK) {
        s_espNowReady = false;
        return -2;
    }
    if (!s_rxQueue) {
        s_rxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxItem));
    }
    esp_now_register_recv_cb(onEspNowRecv);
    addBroadcastPeer();
    s_espNowReady = true;
    pickHourlyMinutes();
    return 1;
}

bool meshEnsure() {
    if (s_espNowReady) return true;
    return meshInit() == 1;
}

void meshOnRawFrame(const uint8_t* data, uint16_t len, bool viaUdp) {
    processFrame(data, len, viaUdp, 0, nullptr);
}

void meshService() {
    if (s_rxQueue) {
        RxItem item;
        while (xQueueReceive(s_rxQueue, &item, 0) == pdTRUE) {
            if (!item.viaUdp) {
                s_stats.lastRxRssi = item.rssi;
                s_stats.lastRxFromMac = MACToUint64(item.srcMac);
                noteDelinquentRssi(item.srcMac, item.rssi);
            }
            processFrame(item.data, item.len, item.viaUdp, item.rssi, item.viaUdp ? nullptr : item.srcMac);
        }
    }
    serviceRelays();
    servicePendingAck();
    serviceHourly();
}

bool meshSendRaw(uint8_t msgType, const uint8_t destMac[6], const uint8_t* plain, uint8_t plainLen,
                 uint8_t ttl, uint8_t burstK, bool alsoUdpIfFailed) {
    (void)burstK;
    return buildAndSend(msgType, destMac, plain, plainLen, ttl, nullptr, alsoUdpIfFailed);
}

bool meshSendTelemetry(const ArborysDevType* d, const ArborysSnsType* s, bool alsoUdp) {
    if (!d || !s) return false;
    uint8_t plain[sizeof(ArborysMeshDevWire) + sizeof(ArborysMeshSnsWire)];
    ArborysMeshDevWire dw;
    ArborysMeshSnsWire sw;
    meshPackDev(dw, d);
    meshPackSns(sw, s);
    memcpy(plain, &dw, sizeof(dw));
    memcpy(plain + sizeof(dw), &sw, sizeof(sw));
    if (!buildAndSend(MSG_TELEMETRY, BROADCAST_MAC, plain, sizeof(plain), s_params.ttlNormal, nullptr, alsoUdp || I.espNowFailed)) {
        return false;
    }
    noteOriginBroadcast();
    return true;
}

bool meshSendNodeAnnounce() {
    int16_t my = Sensors.findMyDeviceIndex();
    ArborysDevType* d = Sensors.getDeviceByDevIndex(my);
    if (!d) return false;
    ArborysMeshDevWire dw;
    meshPackDev(dw, d);
    if (!buildAndSend(MSG_NODE_ANNOUNCE, BROADCAST_MAC, (uint8_t*)&dw, sizeof(dw), s_params.ttlNormal, nullptr, false)) {
        return false;
    }
    noteOriginBroadcast();
    return true;
}

bool meshSendBeaconSync() {
#if !_IS_SERVER_HUB
    return false;
#else
    int16_t my = Sensors.findMyDeviceIndex();
    ArborysDevType* d = Sensors.getDeviceByDevIndex(my);
    if (!d) return false;
    uint8_t plain[4 + 4 + 2 + 1 + sizeof(ArborysMeshDevWire)];
    uint32_t utc = (uint32_t)utcNow();
    int32_t tz = Prefs.TimeZoneOffset;
    int16_t dstOff = Prefs.DSTOffset;
    uint8_t dst = Prefs.DST;
    memcpy(plain, &utc, 4);
    memcpy(plain + 4, &tz, 4);
    memcpy(plain + 8, &dstOff, 2);
    plain[10] = dst;
    ArborysMeshDevWire dw;
    meshPackDev(dw, d);
    memcpy(plain + 11, &dw, sizeof(dw));
    return buildAndSend(MSG_BEACON_SYNC, BROADCAST_MAC, plain, sizeof(plain), s_params.ttlNormal, nullptr, false);
#endif
}

bool meshSendHubSweep() {
#if !_IS_SERVER_HUB
    return false;
#else
    int16_t my = Sensors.findMyDeviceIndex();
    ArborysDevType* d = Sensors.getDeviceByDevIndex(my);
    if (!d) return false;

    // Read the associated radio before the hop. I.WifiChannel can be a leftover scan channel.
    uint8_t homeCh = 0;
    uint8_t homeBssid[6] = {0};
    bool haveBssid = false;
    const bool wasConnected = (WiFi.status() == WL_CONNECTED);
    if (wasConnected) {
        const int liveCh = WiFi.channel();
        const uint8_t* liveBssid = WiFi.BSSID();
        if (liveCh >= 1 && liveCh <= 14 && liveBssid) {
            homeCh = (uint8_t)liveCh;
            memcpy(homeBssid, liveBssid, 6);
            haveBssid = true;
        }
    }
    if (homeCh == 0 && I.WifiChannel >= 1 && I.WifiChannel <= 14) homeCh = I.WifiChannel;

    // Brief hop to channel 1. Rejoin must name the channel and BSSID; a bare begin() scans
    // and leaves the radio on whatever channel the scan finished on.
    if (wasConnected) WiFi.disconnect(false, false);
    delay(20);
    setWifiRfChannel(1);
    meshEnsure();

    uint8_t plain[1 + sizeof(ArborysMeshDevWire) + 1 + 1 + 32];
    plain[0] = MSG_BCST_HUB_SWEEP;
    ArborysMeshDevWire dw;
    meshPackDev(dw, d);
    memcpy(plain + 1, &dw, sizeof(dw));
    uint8_t* p = plain + 1 + sizeof(dw);
    *p++ = homeCh ? homeCh : 1;
    const char* ssid = Prefs.WIFISSID;
    uint8_t slen = (uint8_t)strnlen(ssid, 32);
    *p++ = slen;
    memcpy(p, ssid, slen);
    const uint8_t plainLen = (uint8_t)(1 + sizeof(dw) + 1 + 1 + slen);

    bool ok = buildAndSend(MSG_BROADCAST_NO_ACK, BROADCAST_MAC, plain, plainLen, s_params.ttlCritical, nullptr, false);

    if (haveBssid) {
        WiFi.begin(Prefs.WIFISSID, Prefs.WIFIPWD, homeCh, homeBssid, true);
        WiFi.setSleep(WIFI_PS_NONE);
    } else if (homeCh >= 1 && homeCh <= 14) {
        setWifiRfChannel(homeCh);
    }
    return ok;
#endif
}

bool meshSendUniAckReqToHubs() {
#if _IS_SERVER_HUB && !_HAS_LOCAL_SENSORS
    return false;
#else
    bool any = false;
    uint8_t payload = MSG_UNIACK_LINK_CHECK;
    for (int16_t i = 0; i < NUMDEVICES; ++i) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(i);
        if (!d || !d->IsSet) continue;
        if (!IS_SERVER_DEVICE_TYPE(d->devType)) continue;
        if (d->MAC == ESP.getEfuseMac()) continue;
        uint8_t dest[6];
        uint64ToMAC(d->MAC, dest);
        uint16_t seq = 0;
        if (buildAndSend(MSG_UNICAST_ACK_REQ, dest, &payload, 1, s_params.ttlNormal, &seq, false)) {
            s_pendingAck.active = true;
            memcpy(s_pendingAck.hubMac, dest, 6);
            s_pendingAck.seq = seq;
            s_pendingAck.retriesLeft = s_params.ackRetries;
            s_pendingAck.deadlineMs = millis() + s_params.ackTimeoutMs;
            any = true;
            break; // one hub at a time; success = any ACK
        }
    }
    return any;
#endif
}

// The ACK carries its own seq (dedup/replay tracking is per origin); the request seq is echoed in the payload.
bool meshSendAck(const uint8_t destMac[6], uint16_t echoSeq) {
    uint8_t payload[2];
    memcpy(payload, &echoSeq, 2);
    return buildAndSend(MSG_ACK, destMac, payload, sizeof(payload), s_params.ttlNormal, nullptr, false);
}

bool meshBlockingAckCheck(ArborysDevType* target, uint16_t timeoutMs, uint32_t* rttMsOut) {
    if (rttMsOut) *rttMsOut = 0;
    if (!target || !target->IsSet) return false;
    uint8_t dest[6];
    uint64ToMAC(target->MAC, dest);
    uint8_t payload = MSG_UNIACK_LINK_CHECK;
    uint16_t seq = 0;
    const uint32_t t0 = millis();
    const uint8_t ttl = hubDeviceIsDelinquent(target) ? s_params.ttlCritical : s_params.ttlNormal;
    if (!buildAndSend(MSG_UNICAST_ACK_REQ, dest, &payload, 1, ttl, &seq, false)) {
        return false;
    }
    s_pendingAck.active = true;
    memcpy(s_pendingAck.hubMac, dest, 6);
    s_pendingAck.seq = seq;
    s_pendingAck.retriesLeft = 0;
    // Resends are driven here; keep servicePendingAck from expiring the request mid-loop.
    s_pendingAck.deadlineMs = t0 + timeoutMs + 60000UL;
    // Broadcast frames get no MAC-layer retries, so resend until the timeout.
    uint32_t nextResend = t0 + s_params.ackTimeoutMs;
    while ((int32_t)(millis() - (t0 + timeoutMs)) < 0) {
        meshService();
        if (!s_pendingAck.active) {
            if (rttMsOut) *rttMsOut = millis() - t0;
            return true;
        }
        if ((int32_t)(millis() - nextResend) >= 0) {
            if (buildAndSend(MSG_UNICAST_ACK_REQ, dest, &payload, 1, ttl, &seq, false)) {
                s_pendingAck.seq = seq;
            }
            nextResend = millis() + s_params.ackTimeoutMs;
        }
        delay(10);
        esp_task_wdt_reset();
    }
    s_pendingAck.active = false;
    return false;
}

bool meshSendSnsReqExpired(ArborysDevType* target, uint16_t timeoutMs) {
    if (!target || !target->IsSet || target->MAC == 0) return false;
    uint8_t dest[6];
    uint64ToMAC(target->MAC, dest);
    uint8_t payload = MSG_UNIACK_SNS_REQ_EXPIRED;
    uint16_t seq = 0;
    const uint32_t t0 = millis();
    const uint8_t ttl = s_params.ttlNormal;
    if (!buildAndSend(MSG_UNICAST_ACK_REQ, dest, &payload, 1, ttl, &seq, false)) {
        return false;
    }
    s_pendingAck.active = true;
    memcpy(s_pendingAck.hubMac, dest, 6);
    s_pendingAck.seq = seq;
    s_pendingAck.retriesLeft = 0;
    s_pendingAck.deadlineMs = t0 + timeoutMs + 60000UL;
    uint32_t nextResend = t0 + s_params.ackTimeoutMs;
    while ((int32_t)(millis() - (t0 + timeoutMs)) < 0) {
        meshService();
        if (!s_pendingAck.active) return true;
        if ((int32_t)(millis() - nextResend) >= 0) {
            if (buildAndSend(MSG_UNICAST_ACK_REQ, dest, &payload, 1, ttl, &seq, false)) {
                s_pendingAck.seq = seq;
            }
            nextResend = millis() + s_params.ackTimeoutMs;
        }
        delay(10);
        esp_task_wdt_reset();
    }
    s_pendingAck.active = false;
    return false;
}

bool broadcastServerPresence(bool broadcastPeripheral, uint8_t method) {
    (void)method;
#if _I_AM_PERIPHERAL
    if (!broadcastPeripheral) return false;
#endif
    bool ok = meshSendNodeAnnounce();
#if _IS_SERVER_HUB
    ok |= meshSendBeaconSync();
#endif
    return ok;
}

bool broadcastServerPing(uint8_t tier) {
    (void)tier;
    // Used for time sync / discovery: announce ourselves; hubs reply with BEACON_SYNC on their schedule.
    // Actively request presence by announcing; time sync listens for BEACON_SYNC.
    return meshSendNodeAnnounce();
}

void beginServerPingTimeSync() {
    s_timeSyncActive = true;
    s_timeSyncReady = false;
    s_timeSyncUtc = 0;
}

void endServerPingTimeSync() {
    s_timeSyncActive = false;
}

bool takeServerPingTimeSync(uint32_t& outUtcTime) {
    if (!s_timeSyncReady) return false;
    outUtcTime = s_timeSyncUtc;
    s_timeSyncReady = false;
    return outUtcTime >= TIMEZERO;
}
