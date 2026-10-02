#ifndef ARBORYS_MESH_HPP
#define ARBORYS_MESH_HPP

#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>
#include <string.h>

struct ArborysDevType;
struct ArborysSnsType;

// --- Frame limits ---
constexpr uint8_t  ARBORYS_MESH_NETWORK_ID = 103;
constexpr uint16_t ARBORYS_MESH_MAX_FRAME = 250;
constexpr uint8_t  ARBORYS_MESH_HEADER_SIZE = 35; // includes trailing crc16
constexpr uint8_t  ARBORYS_MESH_MAX_CIPHER = 208; // 13 * 16 <= 250-35
constexpr uint8_t  ARBORYS_MESH_DEDUP_DEPTH = 128; // buffer depth; not a runtime tune

// Airtime policy. Copy, edit fields, then meshSetParams(). Wire format and buffer
// sizes stay compile-time. Hub-directed updates can call meshSetParams() later.
struct ArborysMeshParams {
    uint8_t  ttlNormal = 3;                  // hops for ordinary frames (header stores 4 bits, 1..15)
    uint8_t  ttlCritical = 6;                // hops for hub sweep and other long-lived floods
    uint8_t  criticalBurstCount = 3;         // copies sent back-to-back for a critical frame
    uint16_t criticalBurstGapMinMs = 10;
    uint16_t criticalBurstGapMaxMs = 25;
    uint8_t  relaySuppressCount = 2;         // drop a queued non-critical relay after this many copies
    uint16_t ackTimeoutMs = 750;             // wait for ACK, and gap between link-check resends
    uint8_t  ackRetries = 2;

    // Far/weak neighbors relay first. Near/strong neighbors wait, then are usually suppressed.
    int8_t   relayRssiWeakDbm = -90;         // at or below this: relayDelayWeakMs
    int8_t   relayRssiStrongDbm = -40;       // at or above this: relayDelayStrongMs
    uint16_t relayDelayWeakMs = 20;
    uint16_t relayDelayStrongMs = 120;       // hold before relay when the heard signal is strong
    uint16_t relayJitterMs = 30;             // added as a random 0..jitter on every non-critical relay
    uint16_t criticalRelayMinMs = 5;         // critical relays skip the RSSI curve
    uint16_t criticalRelayMaxMs = 20;

    uint16_t originReorderWindow = 64;       // accept a seq this far behind or ahead of the highest

    // Hold before this node rebroadcasts. rssi 0 (unknown) uses the midpoint of the curve.
    uint16_t relayHoldMs(int8_t rssi, bool critical) const;
};

const ArborysMeshParams& meshParams();
void meshSetParams(const ArborysMeshParams& params);
/** False when a field is outside the airtime bounds, including a max below its min. */
bool meshParamsInRange(const ArborysMeshParams& params);

#ifndef NUMDEVICES
#define NUMDEVICES 16
#endif
constexpr uint8_t ARBORYS_MESH_ORIGIN_SLOTS =
    (NUMDEVICES > 64) ? (uint8_t)NUMDEVICES : (uint8_t)64;

#pragma pack(push, 1)
struct ArborysMeshHeader {
    uint8_t  network_id;
    uint8_t  origin_mac[6];
    uint8_t  dest_mac[6];
    uint16_t seq_num;       // LE
    uint8_t  ttl_type;      // (ttl << 4) | msgType
    uint8_t  cipher_len;    // ciphertext bytes (0 = empty payload)
    uint8_t  iv[16];
    uint16_t crc16;         // LE: CRC over header with crc16=0, then ciphertext
};
#pragma pack(pop)
static_assert(sizeof(ArborysMeshHeader) == ARBORYS_MESH_HEADER_SIZE, "ArborysMeshHeader must be 35 bytes");

inline uint8_t meshHdrTtl(uint8_t ttl_type) { return (uint8_t)((ttl_type >> 4) & 0x0F); }
inline uint8_t meshHdrMsgType(uint8_t ttl_type) { return (uint8_t)(ttl_type & 0x0F); }
inline uint8_t meshMakeTtlType(uint8_t ttl, uint8_t msgType) {
    return (uint8_t)(((ttl & 0x0F) << 4) | (msgType & 0x0F));
}

enum MeshMsgType : uint8_t {
    MSG_BROADCAST_NO_ACK = 0x0,
    MSG_UNICAST_NO_ACK   = 0x1,
    MSG_TELEMETRY        = 0x2,
    MSG_LOG_EVENT        = 0x3,
    MSG_UNICAST_ACK_REQ  = 0x4,
    MSG_ACK              = 0x5,
    MSG_NACK_RETRY       = 0x6,
    MSG_CRITICAL_EVENT   = 0x7,
    MSG_CMD_UNICAST      = 0x8,
    MSG_CMD_BROADCAST    = 0x9,
    MSG_CMD_RESPONSE     = 0xA,
    MSG_PING_PONG        = 0xB,
    MSG_BEACON_SYNC      = 0xC,
    MSG_NODE_ANNOUNCE    = 0xD,
    MSG_CONFIG_UPDATE    = 0xE,
    MSG_RESERVED_EXPANSION = 0xF
};

enum MessageBcstSubtype : uint8_t {
    MSG_BCST_HUB_SWEEP          = 0,
    MSG_BCST_DELINQUENT         = 1,   // subtype + N*6 MAC bytes; this hub's own delinquent set
    MSG_BCST_CRITICAL_NET_ERROR = 200
};

enum MessageUniSubtype : uint8_t {
    MSG_UNI_RESERVED         = 0,
    MSG_UNI_HUB_SENSOR_REQ   = 1,
    MSG_UNI_HUB_SENSOR_REPLY = 2,
    MSG_UNI_ERR_NOT_FOUND    = 100,
    MSG_UNI_ERR_EXPIRED      = 101
};

enum MessageUniAckSubtype : uint8_t {
    MSG_UNIACK_LINK_CHECK = 0,
    MSG_UNIACK_SNS_REQ_EXPIRED = 1  // hub → peripheral: readings are missing; ACK required
};

#pragma pack(push, 1)
struct ArborysMeshDevWire {
    uint64_t MAC;
    uint32_t IP;              // IPv4 as uint32
    uint8_t  devType;
    char     devName[30];
    uint8_t  firmware[3];
    uint8_t  Flags;
    uint32_t SendingInt;
};
struct ArborysMeshSnsWire {
    uint8_t  snsType;
    uint8_t  snsID;
    char     snsName[30];
    float    snsValue;
    uint32_t timeRead;
    uint32_t timeLogged;
    uint8_t  Flags;
    uint32_t SendingInt;
    uint32_t PollingInt;
    int16_t  snsPin;
    int16_t  powerPin;
    uint8_t  OverrideFlags;
    float    limitHigh;
    float    limitLow;
};
#pragma pack(pop)

static_assert(sizeof(ArborysMeshDevWire) == 51, "DevWire size");
static_assert(sizeof(ArborysMeshSnsWire) == 66, "SnsWire size");
static_assert(sizeof(ArborysMeshDevWire) + sizeof(ArborysMeshSnsWire) <= ARBORYS_MESH_MAX_CIPHER,
              "telemetry plaintext must fit cipher budget");

struct MeshStats {
    uint32_t rxDropped;        // ESP-NOW frames lost because the RX queue was full
    uint32_t relaysSent;
    uint32_t relaysSuppressed; // queued relays dropped after hearing enough copies
    int8_t   lastRxRssi;       // dBm of the most recent ESP-NOW mesh frame (0 = none yet)
    uint64_t lastRxFromMac;    // transmitter of that frame (origin or relay)
};
const MeshStats& meshStats();

// --- Lifecycle ---
int8_t meshInit();
bool meshEnsure();
void meshService(); // relay timers, ACK timeouts, hourly sweep / link-check
void meshOnRawFrame(const uint8_t* data, uint16_t len, bool viaUdp);

// --- LMK ---
bool isLMKConfigured();
bool isValidLMKKey();

// --- Pack / unpack ---
void meshPackDev(ArborysMeshDevWire& out, const ArborysDevType* d);
void meshPackSns(ArborysMeshSnsWire& out, const ArborysSnsType* s);
bool meshUnpackDevToSensors(const ArborysMeshDevWire& w);
bool meshUnpackTelemetryToSensors(const ArborysMeshDevWire& d, const ArborysMeshSnsWire& s);

// --- Send helpers ---
bool meshSendRaw(uint8_t msgType, const uint8_t destMac[6], const uint8_t* plain, uint8_t plainLen,
                 uint8_t ttl, uint8_t burstK, bool alsoUdpIfFailed);
bool meshSendTelemetry(const ArborysDevType* d, const ArborysSnsType* s, bool alsoUdp = false);
bool meshSendNodeAnnounce();
bool meshSendBeaconSync(); // hubs: on-channel clock + hub device
bool meshSendHubSweep();   // hubs: ch1 sweep subtype 0
bool meshSendUniAckReqToHubs(); // peripherals: link check
bool meshSendAck(const uint8_t destMac[6], uint16_t echoSeq);
/** Blocking link check to one device (ACK_REQ / ACK). */
bool meshBlockingAckCheck(ArborysDevType* target, uint16_t timeoutMs, uint32_t* rttMsOut);
/** Blocking snsReqExpired. True only when the peripheral ACKs. */
bool meshSendSnsReqExpired(ArborysDevType* target, uint16_t timeoutMs = 2000);

// Compatibility aliases used across the tree (legacy names)
inline int8_t initESPNOW() { return meshInit(); }
inline bool ensureESPNOW() { return meshEnsure(); }
inline void serviceESPNOWRecvQueue() { meshService(); }
bool broadcastServerPresence(bool broadcastPeripheral = false, uint8_t method = 2);
bool broadcastServerPing(uint8_t tier = 1);
void beginServerPingTimeSync();
void endServerPingTimeSync();
bool takeServerPingTimeSync(uint32_t& outUtcTime);

#endif
