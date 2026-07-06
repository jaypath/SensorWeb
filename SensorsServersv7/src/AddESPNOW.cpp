/*ESPNow general structure...

All messages will be sent as a struct, which may or may not contain sensitive info (if not, then the sensitive fields will be zeroed)

Every X minutes, 
--a Server will broadcast it's presence. This is a type 1 message
  --a type message will include at minimum:
    --server ID (MAC) 
    --Server IP
--a sensor will accept/update server addresses and add to their internal list to send data
--no response is required

At any time, a sensor can broadcast a ping (a type 2 message). This may be optional (the sensor could simply send data via TCP/IP instead)
--all server types will register the device 
--no response is required, but if a server did not have that sensor registered then a type 1 message should be sent

At any time, a broadcase message can be sent that REQUIRES a private response from the intended recipient (a type 3 message). This broadcast message will indicate who the intended recipient is.

At any time, a private message can be sent (a type 10 message). Such a message will be encrypted with the vendor PMK and the user's LMK
--The recipient will not be required to respond.


At any time, a private message can be sent, asking for a response (a type 11 message). Such a message will be encrypted with the vendor PMK and the user's LMK
--The recipient will respond with wifi info


Messages will use the ESPNOW_type struct for their transmission


*/

#include "AddESPNOW.hpp"
#include "globals.hpp"  // Add this include to access Prefs.KEYS.ESPNOW_KEY
#include "utility.hpp"
#include "Devices.hpp"

uint8_t registerSensorData(uint64_t deviceMAC, IPAddress deviceIP, String devName, uint8_t devType, uint8_t devFlags, uint8_t snsType, uint8_t snsID, String snsName, double snsValue, uint32_t timeRead, uint32_t timeLogged, uint32_t sendingInt, uint8_t flags);


#ifdef _USEUDP
WiFiUDP LAN_UDP;
#endif


namespace {
    constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    IPAddress readLanMetaIP(const uint8_t* payload) {
        uint32_t ipRaw = 0;
        memcpy(&ipRaw, payload + LAN_META_IP_OFF, sizeof(ipRaw));
        return IPAddress(ipRaw);
    }

    uint8_t readLanMetaType(const uint8_t* payload) {
        return payload[LAN_META_TYPE_OFF];
    }

    uint32_t readLanMetaTime(const uint8_t* payload) {
        uint32_t t = 0;
        memcpy(&t, payload + LAN_META_TIME_OFF, sizeof(t));
        return t;
    }

    void readLanMetaFirmware(FirmwareVersion& fw, const uint8_t* payload) {
        memcpy(fw.v, payload + LAN_META_FW_OFF, 3);
    }

    void packLanMeta(uint8_t* payload, uint32_t timeOrId, bool includeFirmware = true) {
        memset(payload, 0, LAN_META_SIZE);
        memcpy(payload + LAN_META_TIME_OFF, &timeOrId, sizeof(timeOrId));
        uint32_t ipRaw = (uint32_t)WiFi.localIP();
        memcpy(payload + LAN_META_IP_OFF, &ipRaw, sizeof(ipRaw));
        payload[LAN_META_TYPE_OFF] = _MYTYPE;
        if (includeFirmware) {
            FirmwareVersion fw;
            getLocalFirmware(fw);
            memcpy(payload + LAN_META_FW_OFF, fw.v, sizeof(fw.v));
        }
    }

    void packLanMetaName(uint8_t* payload, const char* devName) {
        strncpy((char*)payload + LAN_DEVNAME_OFF, devName, LAN_DEVNAME_LEN);
        payload[LAN_DEVNAME_OFF + LAN_DEVNAME_LEN - 1] = '\0';
    }

    const char* lanPayloadDevName(const ESPNOW_type* msg, char* nameBuf, size_t nameBufLen) {
        uint8_t base = lanMsgBaseType(msg->msgType);
        if (base == ESPNOW_MSG_WIFI_PW_REQUEST || base == ESPNOW_MSG_WIFI_PW_RESPONSE) {
            if (msg->payload[LAN_WIFI_NAME_OFF] != 0) {
                strncpy(nameBuf, (const char*)msg->payload + LAN_WIFI_NAME_OFF, LAN_WIFI_NAME_LEN);
                nameBuf[LAN_WIFI_NAME_LEN] = '\0';
                return nameBuf;
            }
            return "";
        }
        if (msg->payload[LAN_DEVNAME_OFF] != 0) {
            strncpy(nameBuf, (const char*)msg->payload + LAN_DEVNAME_OFF, LAN_DEVNAME_LEN);
            nameBuf[LAN_DEVNAME_LEN] = '\0';
            return nameBuf;
        }
        return "";
    }

    void applyLanSenderToDevices(const ESPNOW_type* msg, const char* devNameOverride = nullptr) {
        IPAddress ip = readLanMetaIP(msg->payload);
        uint8_t devType = readLanMetaType(msg->payload);
        FirmwareVersion fw;
        readLanMetaFirmware(fw, msg->payload);
        const FirmwareVersion* fwPtr = fw.isUnset() ? nullptr : &fw;

        char nameBuf[LAN_DEVNAME_LEN + 1] = {0};
        const char* useName = devNameOverride;
        if (!useName || useName[0] == '\0') {
            useName = lanPayloadDevName(msg, nameBuf, sizeof(nameBuf));
        }

        IPAddress regIP = (ip != IPAddress(0, 0, 0, 0)) ? ip : IPAddress(0, 0, 0, 0);
        Sensors.addDevice(MACToUint64(msg->senderMAC), regIP, useName, 0, 0, devType, fwPtr);
    }

    void packLANSensorPayload(uint8_t* payload, ArborysSnsType* S) {
        LAN_sensor_payload_t pl = {};
        pl.timeStamp = isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : 0;
        uint32_t ipRaw = (uint32_t)WiFi.localIP();
        memcpy(pl.senderIP, &ipRaw, sizeof(pl.senderIP));
        pl.senderType = _MYTYPE;
        FirmwareVersion fw;
        getLocalFirmware(fw);
        memcpy(pl.firmware, fw.v, sizeof(pl.firmware));
        pl.snsType = S->snsType;
        pl.snsID = S->snsID;
        pl.snsValue = (float)S->snsValue;
        pl.timeRead = S->timeRead;
        pl.flags = S->Flags;
        pl.SendingInt = S->SendingInt;
        strncpy(pl.snsName, S->snsName, LAN_SENSOR_SNSNAME_LEN);
        strncpy(pl.devName, Prefs.DEVICENAME, LAN_SENSOR_DEVNAME_LEN);
        memcpy(payload, &pl, sizeof(pl));
    }

    void packDeviceRegPayload(uint8_t* payload, uint8_t devType, uint8_t devFlags, uint32_t sendingInt) {
        LAN_device_reg_payload_t pl = {};
        pl.timestamp = isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : 0;
        uint32_t ipRaw = (uint32_t)WiFi.localIP();
        memcpy(pl.senderIP, &ipRaw, sizeof(pl.senderIP));
        pl.devType = devType;
        pl.devFlags = devFlags;
        pl.wifiChannel = I.WifiChannel;
        pl.sendingInt = sendingInt;
        strncpy(pl.devName, Prefs.DEVICENAME, LAN_DEVICE_REG_DEVNAME_LEN);
        pl.devName[LAN_DEVICE_REG_DEVNAME_LEN - 1] = '\0';
        FirmwareVersion fw;
        getLocalFirmware(fw);
        memcpy(pl.firmware, fw.v, sizeof(pl.firmware));
        memcpy(payload, &pl, sizeof(pl));
    }

    void registerDeviceFromLanPayload(const ESPNOW_type* msg, const LAN_device_reg_payload_t& pl) {
        char nameBuf[LAN_DEVICE_REG_DEVNAME_LEN + 1] = {0};
        memcpy(nameBuf, pl.devName, LAN_DEVICE_REG_DEVNAME_LEN);
        const uint8_t devType = pl.devType ? pl.devType : readLanMetaType((const uint8_t*)&pl);
        FirmwareVersion fw;
        memcpy(fw.v, pl.firmware, sizeof(fw.v));
        const FirmwareVersion* fwPtr = fw.isUnset() ? nullptr : &fw;
        IPAddress ip = readLanMetaIP((const uint8_t*)&pl);
        IPAddress regIP = (ip != IPAddress(0, 0, 0, 0)) ? ip : IPAddress(0, 0, 0, 0);
        Sensors.addDevice(MACToUint64(msg->senderMAC), regIP, nameBuf, pl.sendingInt, pl.devFlags, devType, fwPtr);
    }

    #if _HAS_LOCAL_SENSORS
    void respondLANSensorDataRequest(ESPNOW_type* msg) {
        uint8_t tier = (msg->msgType == UDP_MSG_REQUEST_SENSOR_DATA) ? 2 : 1;
        uint64_t targetMAC = MACToUint64(msg->senderMAC);
        IPAddress targetIP = readLanMetaIP(msg->payload);
        int16_t myIdx = Sensors.findMyDeviceIndex();
        bool sent = false;

        for (int16_t i = 0; i < NUMSENSORS; ++i) {
            ArborysSnsType* S = Sensors.snsIndexToPointer(i);
            if (!S || !S->IsSet || S->deviceIndex != myIdx) continue;
            if (sendLANSensorData(S, targetMAC, targetIP, tier)) sent = true;
            delay(random(10, 100));
        }
        if (!sent) {
            storeError("LAN: No sensors to send in data request response");
        }
    }
    #endif
}

// Store last sent MAC for callback (workaround for ESP32 core 3.3.5 API changes)
static uint8_t lastSentTargetMAC[6] = {0};
static bool s_espNowInitialized = false;

// One in-flight blocking ping (type 8) at a time; completed by type 9 in processLANMessage.
static volatile bool s_blockingPingActive = false;
static volatile bool s_blockingPingGotResponse = false;
static uint64_t s_blockingPingExpectedFromMAC = 0;
static uint32_t s_blockingPingRequestId = 0;
static uint8_t s_blockingPingTier = 0; // 1=ESP-NOW only, 2=UDP only, 3=either

static volatile bool s_serverPingTimeSyncActive = false;
static volatile bool s_serverPingTimeSyncReady = false;
static volatile uint32_t s_serverPingSyncedLocalTime = 0;

static void noteBlockingPingResponse(uint64_t fromMAC, uint32_t requestId, bool viaUDP) {
    if (!s_blockingPingActive) return;
    if (fromMAC != s_blockingPingExpectedFromMAC) return;
    if (requestId != s_blockingPingRequestId) return;
    if (s_blockingPingTier == 1 && viaUDP) return;
    if (s_blockingPingTier == 2 && !viaUDP) return;
    s_blockingPingGotResponse = true;
}

static void makeESPNowBlockingPingMsg(ESPNOW_type& msg, uint64_t targetMAC, uint32_t requestId, bool useUDP) {
    uint8_t tempMAC[6] = {0};
    uint64ToMAC(targetMAC, tempMAC);
    memcpy(msg.targetMAC, tempMAC, 6);
    msg.msgType = useUDP ? UDP_MSG_BLOCKING_PING : ESPNOW_MSG_BLOCKING_PING;
    packLanMeta(msg.payload, requestId, true);
    packLanMetaName(msg.payload, Prefs.DEVICENAME);
}

static bool sendBlockingPingResponse(ESPNOW_type* req, uint32_t requestId, bool useUDP) {
    ESPNOW_type resp = {};
    memcpy(resp.targetMAC, req->senderMAC, 6);
    resp.msgType = useUDP ? UDP_MSG_BLOCKING_PING_RESPONSE : ESPNOW_MSG_BLOCKING_PING_RESPONSE;
    packLanMeta(resp.payload, requestId, true);
    packLanMetaName(resp.payload, Prefs.DEVICENAME);
    snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Blocking ping response to %s via %s",
        MACToString(req->senderMAC).c_str(), useUDP ? "UDP" : "ESPNow");

    if (useUDP) {
        return sendLANmsg_UDP(resp, readLanMetaIP(req->payload), false);
    }
    return sendLANmsg_ESPNOW(resp, false);
}

// ESP-NOW needs the WiFi radio on (STA/AP/APSTA), not a router association.
static bool ensureWiFiRadioForESPNOW() {
    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(WIFI_PS_NONE);
    }
    return WiFi.getMode() != WIFI_MODE_NULL;
}

// --- Utility: Check if LMK is configured ---
bool isLMKConfigured() {
    for (int i = 0; i < 16; i++) {
        if (Prefs.KEYS.ESPNOW_KEY[i] != 0) {
            return true;
        }
    }
    return false;
}

// --- Utility: Validate LMK key format ---
bool isValidLMKKey() {
    if (!isLMKConfigured()) {
        return false;
    }
   

    //key can contain any characters, so we don't need to check for printable ASCII characters
    if (false) {
        // Check if key contains only printable ASCII characters
        for (int i = 0; i < 16; i++) {
            if (Prefs.KEYS.ESPNOW_KEY[i] != 0 && (Prefs.KEYS.ESPNOW_KEY[i] < 32 || Prefs.KEYS.ESPNOW_KEY[i] > 126)) {
                return false;
            }
        }
    }

    return true;
}


// --- ESPNow Initialization ---
int8_t initESPNOW() {
    if (!ensureWiFiRadioForESPNOW()) {
        s_espNowInitialized = false;
        storeError("ESPNow: WiFi radio not available");
        return -1;
    }
    esp_now_deinit(); //call this to reset the espnow state prior to reinitialization
    esp_err_t result = esp_now_init();
    if (result != ESP_OK) {
        s_espNowInitialized = false;
        storeError("ESPNow: initialize failed " + ESPNowError(result));
        return -2;
    }
    uint8_t MACB[6] = {0};
    memset(MACB,255,6);
    
    if (!esp_now_is_peer_exist(MACB)) {
        // Register broadcast address (no encryption, I'll handle that separately)
        addESPNOWPeer(MACB);
    }

    esp_now_register_send_cb(OnESPNOWDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    s_espNowInitialized = true;
    return 1;
}

bool ensureESPNOW() {
    if (s_espNowInitialized) return true;
    return initESPNOW() == 1;
}

// --- Peer Management ---
esp_err_t addESPNOWPeer(uint8_t* macad) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macad, 6);
    
//all ESPNOW messages are unencrypted, and the channel is set to 0, which means auto-channel selection
//encryption is handled separately, and is not required for all messages
//the LMK is used to encrypt private messages, and the PMK is used to encrypt broadcast messages and provisioning LMK

    peerInfo.channel = 0;  // use the wifi channel, and 0 defines auto-channel selection
    peerInfo.encrypt = false;
            
    
    esp_err_t result = esp_now_add_peer(&peerInfo);
    
    if (result != ESP_OK) {
        String errormsg = "Failed to add ESPNow peer: " + MACToString(macad) + " Error: " + String(result);
        SerialPrint(errormsg, true,4);
        storeError(errormsg.c_str(),ERROR_ESPNOW_GENERAL);
    }
    
    return result;
}

esp_err_t delESPNOWPeer(uint8_t* macad) {
    return esp_now_del_peer(macad);
}
esp_err_t delESPNOWPeer(uint64_t macad) {
    uint8_t mac[6] = {0};
    uint64ToMAC(macad, mac);
    return esp_now_del_peer(mac);
}



// --- Send ESPNow Message ---
bool sendLANmsg_ESPNOW(ESPNOW_type msg,bool forceencrypt) {
    if (!ensureESPNOW()) {
        storeError("ESPNow: not initialized", ERROR_ESPNOW_GENERAL);
        return false;
    }

    bool isBroadcast = true;
    bool espnowSuccess = false;

    uint64ToMAC(Prefs.PROCID, msg.senderMAC);

    #ifndef _USELOWPOWER
    I.ESPNOW_LAST_OUTGOINGMSG_TIME = I.currentTime;
    #endif

    if (!isBroadcastMAC(msg.targetMAC)) {

      isBroadcast = false;
      
        // Add peer 
        addESPNOWPeer(msg.targetMAC);
    }

    if (lanMsgIsEncrypted(msg.msgType) || forceencrypt) {
        if (encryptLANMessage(msg, LAN_ENCRYPT_MSG_LEN)==false) {
            I.ESPNOW_OUTGOING_ERRORS++;
            return false;
        }
    }

    // Store target MAC for callback (ESP32 core 3.3.5 workaround)
    memcpy(lastSentTargetMAC, msg.targetMAC, 6);
    
    esp_err_t result = esp_now_send(msg.targetMAC, (const uint8_t*) &msg, sizeof(ESPNOW_type));
    if (result != ESP_OK) {
        String error = "SendESPNow: Error" + (String) result + ", to " + MACToString(msg.targetMAC);
        SerialPrint(error,true,4);
        storeError(error.c_str(),ERROR_ESPNOW_SEND);
        espnowSuccess = false;
    } else {
        espnowSuccess = true;
        I.ESPNOW_LAST_OUTGOINGMSG_TYPE = msg.msgType;
        I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC = MACToUint64(msg.targetMAC);
        SerialPrint((String) "ESPNow sent to " + MACToString(msg.targetMAC) + " result: " + result,true,1);
    }

    return espnowSuccess;
}


String ESPNowError(esp_err_t result) {
    if (result == ESP_OK) return "OK";
    
    switch (result) {
        case ESP_ERR_NO_MEM: return "ESPNow out of memory";
        case ESP_ERR_INVALID_ARG: return "ESPNow: Invalid argument";
        case ESP_ERR_INVALID_STATE: return "ESPNow: Invalid state";
        case ESP_ERR_INVALID_SIZE: return "ESPNow: Invalid size";
        case ESP_ERR_NOT_FOUND: return "ESPNow: Resource not found";
        case ESP_ERR_TIMEOUT: return "ESPNow: timeout";
        case ESP_ERR_INVALID_RESPONSE: return "ESPNow: invalid response";
        case ESP_ERR_INVALID_CRC: return "ESPNow: invalid CRC";
        case ESP_ERR_INVALID_MAC: return "ESPNow: invalid MAC";
        case ESP_ERR_ESPNOW_NOT_FOUND: return "ESPNow: MAC recipient not found";
        case ESP_ERR_ESPNOW_ARG: return "ESPNow: Invalid argument";
        default: return "ESPNow: Error undefined";
        break;
    }

}

// --- ESPNow Send Callback ---
void OnESPNOWDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        I.ESPNOW_LAST_OUTGOINGMSG_TIME = I.currentTime;
        // Use stored MAC (workaround for ESP32 core 3.3.5 - wifi_tx_info_t field structure unclear)
        I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC = MACToUint64(lastSentTargetMAC);        
        I.ESPNOW_SENDS++;
        I.isUpToDate = false;
    } else {
        I.ESPNOW_OUTGOING_ERRORS++;
        I.ESPNOW_LAST_OUTGOINGMSG_TIME = I.currentTime;
        String err = "ESPNow: Failed to send data to " + MACToString(lastSentTargetMAC);
        SerialPrint(err, true, 4);
        storeError(err.c_str(), ERROR_ESPNOW_SEND);
    }

    // Transient unicast peers must outlive the async send; remove after callback.
    if (!isBroadcastMAC(lastSentTargetMAC)) {
        delESPNOWPeer(lastSentTargetMAC);
    }
}

// --- ESPNow Receive Callback ---
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    I.ESPNOW_LAST_INCOMINGMSG_TIME = I.currentTime;
    if (len < static_cast<int>(sizeof(ESPNOW_type))) {
        storeError("ESPNow: Received message too short");
        I.ESPNOW_INCOMING_ERRORS++;
        return;
    }
    ESPNOW_type msg;

    memcpy(&msg, incomingData, sizeof(msg));
    I.ESPNOW_LAST_INCOMINGMSG_FROM_MAC = MACToUint64(msg.senderMAC);
    I.ESPNOW_LAST_INCOMINGMSG_TYPE = msg.msgType;
    I.ESPNOW_RECEIVES++;
    I.isUpToDate = false;

    processLANMessage(&msg);
}

uint8_t registerLANSensorData(ESPNOW_type* msg) {
    if (!msg) return 0;

    LAN_sensor_payload_t pl = {};
    memcpy(&pl, msg->payload, sizeof(pl));

    char devNameBuf[LAN_SENSOR_DEVNAME_LEN + 1] = {0};
    char snsNameBuf[LAN_SENSOR_SNSNAME_LEN + 1] = {0};
    memcpy(devNameBuf, pl.devName, LAN_SENSOR_DEVNAME_LEN);
    memcpy(snsNameBuf, pl.snsName, LAN_SENSOR_SNSNAME_LEN);

    IPAddress ip = readLanMetaIP(msg->payload);
    IPAddress regIP = (ip != IPAddress(0, 0, 0, 0)) ? ip : IPAddress(0, 0, 0, 0);
    FirmwareVersion fw;
    readLanMetaFirmware(fw, msg->payload);
    const FirmwareVersion* fwPtr = fw.isUnset() ? nullptr : &fw;
    Sensors.addDevice(MACToUint64(msg->senderMAC), regIP, devNameBuf, pl.SendingInt, 0, pl.senderType, fwPtr);

    return registerSensorData(
        MACToUint64(msg->senderMAC),
        regIP,
        String(devNameBuf),
        pl.senderType,
        0,
        pl.snsType,
        pl.snsID,
        String(snsNameBuf),
        (double)pl.snsValue,
        pl.timeRead,
        I.currentTime,
        pl.SendingInt,
        pl.flags);
}

void processLANMessage(ESPNOW_type* msg) {

    if (lanMsgIsEncrypted(msg->msgType)) {
        if (!decryptLANMessage(*msg, LAN_ENCRYPT_MSG_LEN)) {
            I.ESPNOW_INCOMING_ERRORS++;
            return;
        }
    }

    I.ESPNOW_LAST_INCOMINGMSG_FROM_IP = readLanMetaIP(msg->payload);
    I.ESPNOW_LAST_INCOMINGMSG_FROM_TYPE = readLanMetaType(msg->payload);

    if (msg->msgType == ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED || msg->msgType == UDP_MSG_BROADCAST_ALIVE_ENCRYPTED) {
        applyLanSenderToDevices(msg);

        char nameBuf[LAN_DEVNAME_LEN + 1] = {0};
        const char* devName = lanPayloadDevName(msg, nameBuf, sizeof(nameBuf));
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Broadcast: %s", devName);
        I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63] = 0;

        #if _IS_SERVER_HUB && !defined(_USELOWPOWER)
        //am I a server and did I get a message from a non server? If so, broadcast my presence
        if (_MYTYPE >= 100 && readLanMetaType(msg->payload) < 100) {
            //delay a random number of milliseconds between 500 and 5000
            delay(random(1, 10)*100); //this is to prevent all servers from broadcasting at the same time
            I.makeBroadcast = true;
        }
        #endif

        return;
    }

    if (msg->msgType == ESPNOW_MSG_BROADCAST_SERVER_PING || msg->msgType == UDP_MSG_BROADCAST_SERVER_PING) {
        if (_MYTYPE < 100) {
            return;
        }
        if (!isBroadcastMAC(msg->targetMAC)) {
            return;
        }

        LAN_device_reg_payload_t pl = {};
        memcpy(&pl, msg->payload, sizeof(pl));
        registerDeviceFromLanPayload(msg, pl);

        delay(random(10, 200));

        ESPNOW_type resp = {};
        memcpy(resp.targetMAC, msg->senderMAC, 6);
        packDeviceRegPayload(resp.payload, _MYTYPE, 0, 0);

        bool ok = false;
        if (msg->msgType == ESPNOW_MSG_BROADCAST_SERVER_PING) {
            resp.msgType = ESPNOW_MSG_SERVER_PING_RESPONSE;
            ok = sendLANmsg_ESPNOW(resp, false);
        } else {
            resp.msgType = UDP_MSG_SERVER_PING_RESPONSE;
            ok = sendLANmsg_UDP(resp, readLanMetaIP(msg->payload), false);
        }

        if (!ok) {
            storeError("LAN: Failed to send server ping response");
            I.ESPNOW_OUTGOING_ERRORS++;
            return;
        }

        snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Server ping response to %s",
            MACToString(msg->senderMAC).c_str());
        return;
    }

    if (msg->msgType == ESPNOW_MSG_SERVER_PING_RESPONSE || msg->msgType == UDP_MSG_SERVER_PING_RESPONSE) {
        LAN_device_reg_payload_t pl = {};
        memcpy(&pl, msg->payload, sizeof(pl));
        const uint8_t devType = pl.devType ? pl.devType : readLanMetaType(msg->payload);
        if (devType < 100) {
            return;
        }

        registerDeviceFromLanPayload(msg, pl);
        if (s_serverPingTimeSyncActive && !s_serverPingTimeSyncReady
            && pl.timestamp >= TIMEZERO) {
            s_serverPingSyncedLocalTime = pl.timestamp;
            s_serverPingTimeSyncReady = true;
        }
        if (softApRunning() && !wifiReadyForNetwork()) {
            if (pl.wifiChannel >= AP_WIFI_CHANNEL_MIN && pl.wifiChannel <= AP_WIFI_CHANNEL_MAX) {
                setWifiRfChannel(pl.wifiChannel);
            }
            noteApModeServerPingResponse(pl.wifiChannel);
        }
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Server ping response: %s @ %lu",
            pl.devName, (unsigned long)pl.timestamp);
        I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63] = 0;
        return;
    }

    
    if (msg->msgType== ESPNOW_MSG_WIFI_PW_REQUEST) {
        if (_MYTYPE<100) {
            storeError("ESPNow: peripheral cannot process WiFi password request");
            return; //only servers can provide WiFi password
        }
        applyLanSenderToDevices(msg);
        ESPNOW_type resp = {};
        uint64ToMAC(Prefs.PROCID, resp.senderMAC);
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "WiFi request from %s", MACToString(MACToUint64(msg->senderMAC)).c_str());
        memcpy(resp.targetMAC, msg->senderMAC, 6);
        resp.msgType = ESPNOW_MSG_WIFI_PW_RESPONSE;
        packLanMeta(resp.payload, isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : 0, true);
        uint8_t encrypted[48] = {0};
        uint16_t outlen = 0;
        BootSecure::encryptWithIV((const unsigned char*)Prefs.WIFIPWD, 32, (char*)msg->payload + LAN_WIFI_KEY_OFF,
            msg->payload + LAN_WIFI_IV_OFF, encrypted, &outlen);
        memcpy(resp.payload + LAN_WIFI_KEY_OFF, encrypted, (outlen < 32) ? outlen : 32);
        memcpy(resp.payload + LAN_WIFI_NONCE_OFF, msg->payload + LAN_WIFI_NONCE_OFF, 8);
        
        if (!sendLANmsg_ESPNOW(resp,false)) { 
            storeError("ESPNow: Failed to send WiFi password response");            
            return;
        }
    }
    else if (msg->msgType== ESPNOW_MSG_WIFI_PW_RESPONSE) {
        applyLanSenderToDevices(msg);
        uint32_t nowt = (uint32_t)time(nullptr);
        bool valid = false;
        if (I.TEMP_AES_TIME != 0 && (nowt - I.TEMP_AES_TIME) <= 300) {
            for (int i = 0; i < 32; ++i) if (I.TEMP_AES[i] != 0) valid = true;
        }
        bool macMatch = true;
        if (I.TEMP_AES_MAC != MACToUint64(msg->senderMAC)) macMatch = false;
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 50, "WiFi credentials received");

        bool nonceMatch = true;
        for (int i = 0; i < 8; ++i) {
            if (I.WIFI_RECOVERY_NONCE[i] != msg->payload[LAN_WIFI_NONCE_OFF + i]) nonceMatch = false;
        }
        if (!valid || !macMatch || !nonceMatch) {
            storeError("WiFi PW response: No valid TEMP_AES, expired, MAC mismatch, or nonce mismatch");
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.TEMP_AES_MAC = 0;
            memset(I.WIFI_RECOVERY_NONCE, 0, 8);
            I.ESPNOW_INCOMING_ERRORS++;
            storeError("WiFi PW response: No valid TEMP_AES, expired, MAC mismatch, or nonce mismatch");
            return;
        }
        // Decrypt using the TEMP_AES and IV
        char key[17]; memcpy(key, I.TEMP_AES, 16); key[16] = 0;
        uint8_t* iv = I.TEMP_AES + 16;
        uint8_t decrypted[65] = {0};
        int8_t decres = BootSecure::decryptWithIV((unsigned char*)msg->payload + LAN_WIFI_KEY_OFF, key, iv, decrypted, 32);
        if (decres != 0) {
            storeError("WiFi PW response: Decryption failed");
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.TEMP_AES_MAC = 0;
            memset(I.WIFI_RECOVERY_NONCE, 0, 8);
            I.isUpToDate = false;
            I.ESPNOW_INCOMING_ERRORS++;
            storeError("WiFi PW response: Decryption failed");
            return;
        }
        
        if (strcmp((char*)decrypted, (char*)Prefs.WIFIPWD) == 0) {
            //wifi password matches, so maybe WiFi is down. Proceed along typical routine
            #if defined(_USETFT) 
            screenWiFiDown();
            #endif

            enterAPStationMode();
            
            return;
        }
        memcpy(Prefs.WIFIPWD, decrypted, 64);
        Prefs.WIFIPWD[64] = 0;
        Prefs.HAVECREDENTIALS = true;
        Prefs.isUpToDate = false;
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        I.TEMP_AES_MAC = 0;
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
  
        BootSecure bs;
        bs.setPrefs();

        // Log success
        #ifdef _DEBUG
        Serial.println("[ESPNow] WiFi password updated from server response.");
        #endif
    }
    else if (msg->msgType== ESPNOW_MSG_WIFI_KEY_REQUIRED) {
        applyLanSenderToDevices(msg);
        uint32_t nowt = (uint32_t)time(nullptr);
        bool valid = false;
        if (I.TEMP_AES_TIME != 0 && (nowt - I.TEMP_AES_TIME) <= 300) {
            valid = true;
        }
        bool macMatch = true;
        if (I.TEMP_AES_MAC != MACToUint64(msg->senderMAC)) macMatch = false;
        
        if (!valid || !macMatch) {
            storeError("WiFi KEY REQUIRED: MAC mismatch or expired");
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.TEMP_AES_MAC = 0;
            memset(I.WIFI_RECOVERY_NONCE, 0, 8);
            I.isUpToDate = false;
            storeError("WiFi KEY REQUIRED: MAC mismatch or expired");
            return;
        }
        // Zero out old TEMP_AES and TEMP_AES_TIME
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
        I.isUpToDate = false;
        // Send a new type 2 message to the same MAC
        uint8_t mac[6] = {0};
        uint64ToMAC(I.TEMP_AES_MAC, mac);
        requestWiFiPassword(mac, nullptr);
    }
    else if (msg->msgType== ESPNOW_MSG_PING_RESPONSE_REQUIRED || msg->msgType== UDP_MSG_PING_RESPONSE_REQUIRED) {
        applyLanSenderToDevices(msg);

        char nameBuf[LAN_DEVNAME_LEN + 1] = {0};
        const char* devName = lanPayloadDevName(msg, nameBuf, sizeof(nameBuf));
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Ping recv: %s", devName);
        I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63] = 0;

        ESPNOW_type resp = {};
        memcpy(resp.targetMAC, msg->senderMAC, 6);
        resp.msgType = ESPNOW_MSG_PING_RESPONSE_SUCCESS;
        packLanMeta(resp.payload, isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : 0, true);
        packLanMetaName(resp.payload, Prefs.DEVICENAME);
        snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Ping response to %s", devName);

        bool ok = false;
        if (msg->msgType== ESPNOW_MSG_PING_RESPONSE_REQUIRED) ok = sendLANmsg_ESPNOW(resp,false);
        else {
            resp.msgType = UDP_MSG_PING_RESPONSE_SUCCESS;
            ok = sendLANmsg_UDP(resp, readLanMetaIP(msg->payload), false);
        }
        if (!ok) {
            storeError("ESPNow: Failed to send ping response");
            I.ESPNOW_OUTGOING_ERRORS++;
            return;
        } 
        #ifdef _DEBUG
        SerialPrint("ESPNow: Sent ping response to " + MACToString(msg->senderMAC), true,2);
        #endif
    }
    else if (msg->msgType== ESPNOW_MSG_PING_RESPONSE_SUCCESS || msg->msgType== UDP_MSG_PING_RESPONSE_SUCCESS) {
        applyLanSenderToDevices(msg);
        uint32_t responseTime = readLanMetaTime(msg->payload);
        char nameBuf[LAN_DEVNAME_LEN + 1] = {0};
        const char* devName = lanPayloadDevName(msg, nameBuf, sizeof(nameBuf));
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Ping response: %s", devName);
        I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63] = 0;
        
        #ifdef _DEBUG
        SerialPrint("ESPNow: Received ping response from " + MACToString(msg->senderMAC) + " at time " + String(responseTime), true,2);
        #endif
    }
    else if (msg->msgType == ESPNOW_MSG_BLOCKING_PING || msg->msgType == UDP_MSG_BLOCKING_PING) {
        if (MACToUint64(msg->targetMAC) != (uint64_t)(ESP.getEfuseMac())) {
            return;
        }
        applyLanSenderToDevices(msg);
        uint32_t requestId = readLanMetaTime(msg->payload);
        bool useUDP = (msg->msgType == UDP_MSG_BLOCKING_PING);

        if (!sendBlockingPingResponse(msg, requestId, useUDP)) {
            storeError("ESPNow: Failed to send blocking ping response");
            I.ESPNOW_INCOMING_ERRORS++;
        }
        return;
    }
    else if (msg->msgType == ESPNOW_MSG_BLOCKING_PING_RESPONSE || msg->msgType == UDP_MSG_BLOCKING_PING_RESPONSE) {
        if (!s_blockingPingActive) return;
        uint32_t requestId = readLanMetaTime(msg->payload);
        bool viaUDP = (msg->msgType == UDP_MSG_BLOCKING_PING_RESPONSE);
        noteBlockingPingResponse(MACToUint64(msg->senderMAC), requestId, viaUDP);
        if (!s_blockingPingGotResponse) return;
        applyLanSenderToDevices(msg);
        char nameBuf[LAN_DEVNAME_LEN + 1] = {0};
        const char* devName = lanPayloadDevName(msg, nameBuf, sizeof(nameBuf));
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Blocking ping response: %s", devName);
        I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63] = 0;
        return;
    }
    else if (msg->msgType== ESPNOW_MSG_REQUEST_SENSOR_DATA || msg->msgType== UDP_MSG_REQUEST_SENSOR_DATA) {
        applyLanSenderToDevices(msg);
        if (MACToUint64(msg->targetMAC) != (uint64_t)(ESP.getEfuseMac())) {
            return;
        }
        #if _HAS_LOCAL_SENSORS
        delay(random(10, 2000));
        respondLANSensorDataRequest(msg);
        #endif
        return;
    }
    else if (msg->msgType == ESPNOW_MSG_SENSOR_DATA || msg->msgType == UDP_MSG_SENSOR_DATA) {
        #if _IS_SERVER_HUB
        if (registerLANSensorData(msg) == 0) {
            storeError("LAN: Failed to register sensor data from " + MACToString(msg->senderMAC));
            I.ESPNOW_INCOMING_ERRORS++;
        } else {
            LAN_sensor_payload_t pl = {};
            memcpy(&pl, msg->payload, sizeof(pl));
            snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "LAN sensor: %s", pl.snsName);
            I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD[63] = 0;
        }
        #endif
        return;
    }

    else if (msg->msgType== ESPNOW_MSG_TERMINATE || msg->msgType== UDP_MSG_TERMINATE) {
        applyLanSenderToDevices(msg);
        // Terminate communication, delete peer if needed
        delESPNOWPeer(msg->senderMAC);
        // Zero TEMP_AES and TEMP_AES_TIME for security
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        I.TEMP_AES_MAC = 0;
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
        I.isUpToDate = false;
        snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "MSG TERMINATED for %s", MACToString(msg->senderMAC).c_str());
        
    }
    return ;
}

//at present I do not offer a type 0 message, so all messages are encrypted
// --- Broadcast Server Presence (Type 1) ---
bool broadcastServerPresence(bool broadcastPeripheral, uint8_t method) {
    //methods are 0 = ESPNow, 1 = UDP, 2 = both

    if (_MYTYPE<100 && broadcastPeripheral==false) return false; //only servers broadcast, except under certain circumstances when peripherals specifically requested to do so
    
    ESPNOW_type msg = {};

    makeBroadcastMAC(msg.targetMAC);
    // Always start with ESPNOW message type - then do udp
    msg.msgType = ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED;
    memset(msg.payload, 0, LAN_ENCRYPT_MSG_LEN);
    packLanMeta(msg.payload, isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : 0, true);
    packLanMetaName(msg.payload, Prefs.DEVICENAME);
    
    #ifndef _USELOWPOWER
    snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Broadcast server presence to all");
    I.makeBroadcast = false;
    #endif

    // Send via ESPNow with ESPNOW message type (will be encrypted since msgType > 0)
    msg.msgType = ESPNOW_MSG_BROADCAST_ALIVE_ENCRYPTED;
    bool ok = sendLANmsg_ESPNOW(msg, false);

    if (wifiReadyForNetwork()) {
      msg.msgType = UDP_MSG_BROADCAST_ALIVE_ENCRYPTED;
      ok |= sendLANmsg_UDP(msg, IPAddress(0, 0, 0, 0), false);
    }
    return ok;
}

void beginServerPingTimeSync() {
    s_serverPingTimeSyncActive = true;
    s_serverPingTimeSyncReady = false;
    s_serverPingSyncedLocalTime = 0;
}

void endServerPingTimeSync() {
    s_serverPingTimeSyncActive = false;
    s_serverPingTimeSyncReady = false;
    s_serverPingSyncedLocalTime = 0;
}

bool takeServerPingTimeSync(uint32_t& outLocalTime) {
    if (!s_serverPingTimeSyncReady) return false;
    outLocalTime = s_serverPingSyncedLocalTime;
    s_serverPingTimeSyncReady = false;
    return (outLocalTime >= TIMEZERO);
}

bool broadcastServerPing(uint8_t tier) {
    if (tier < 1 || tier > 3) return false;

    ESPNOW_type msg = {};
    makeBroadcastMAC(msg.targetMAC);
    packDeviceRegPayload(msg.payload, _MYTYPE, 0, 0);

    bool ok = false;
    if (tier == 1 || tier == 3) {
        msg.msgType = ESPNOW_MSG_BROADCAST_SERVER_PING;
        ok |= sendLANmsg_ESPNOW(msg, false);
    }
    if (tier == 2 || tier == 3) {
        msg.msgType = UDP_MSG_BROADCAST_SERVER_PING;
        ok |= sendLANmsg_UDP(msg, IPAddress(0, 0, 0, 0), false);
    }

    if (ok) {
        snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Broadcast server ping tier %u", tier);
    } else {
        storeError("LAN: Failed to send broadcast server ping");
    }
    return ok;
}

// --- Request WiFi Password via ESPNow (with nonce) ---
bool requestWiFiPassword(const uint8_t* serverMAC, const uint8_t* nonce) {
    // Generate random 16-byte key and 16-byte IV
    uint8_t key[16], iv[16];
    esp_fill_random(key, 16);
    esp_fill_random(iv, 16);
    // Store in Prefs
    memcpy(I.TEMP_AES, key, 16);
    memcpy(I.TEMP_AES + 16, iv, 16);
    I.TEMP_AES_TIME = (uint32_t)time(nullptr);
    // Prepare ESPNow message
    ESPNOW_type msg = {};


    msg.msgType = ESPNOW_MSG_WIFI_PW_REQUEST;
    memset(msg.payload, 0, LAN_ENCRYPT_MSG_LEN);
    packLanMeta(msg.payload, (uint32_t)time(nullptr), true);
    memcpy(msg.payload + LAN_WIFI_KEY_OFF, key, 16);
    memcpy(msg.payload + LAN_WIFI_IV_OFF, iv, 16);
    if (nonce) {
        memcpy(msg.payload + LAN_WIFI_NONCE_OFF, nonce, 8);
        memcpy(I.WIFI_RECOVERY_NONCE, nonce, 8);
    } else {
        memset(msg.payload + LAN_WIFI_NONCE_OFF, 0, 8);
        memset(I.WIFI_RECOVERY_NONCE, 0, 8);
    }
    strncpy((char*)msg.payload + LAN_WIFI_NAME_OFF, Prefs.DEVICENAME, LAN_WIFI_NAME_LEN);
    msg.payload[LAN_WIFI_NAME_OFF + LAN_WIFI_NAME_LEN - 1] = '\0';

    // Find server MAC if not provided
    uint8_t destMAC[6] = {0};
    if (serverMAC) {
        memcpy(destMAC, serverMAC, 6);
    } else {
        bool found = false;
        for (int16_t i = 0; i < Sensors.getNumDevices(); ++i) {
            ArborysDevType* dev = Sensors.getDeviceByDevIndex(i);
            if (dev && dev->IsSet && dev->devType >= 100) {
                for (int j = 0; j < 6; ++j)
                    destMAC[5-j] = (dev->MAC >> (8*j)) & 0xFF;
                found = true;
                break;
            }
        }
        if (!found) {
            storeError("No server found for WiFi PW request");
            memset(I.TEMP_AES, 0, 32);
            I.TEMP_AES_TIME = 0;
            I.TEMP_AES_MAC = 0;
            return false;
        }
    }
    memcpy(msg.targetMAC, destMAC, 6);
    I.TEMP_AES_MAC = MACToUint64(destMAC); // Store expected server MAC

    bool ok = sendLANmsg_ESPNOW(msg, false);
    if (!ok) {
        storeError("Failed to send WiFi PW request");
        memset(I.TEMP_AES, 0, 32);
        I.TEMP_AES_TIME = 0;
        I.TEMP_AES_MAC = 0;
        snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "WiFi PW failed to  %s", MACToString(destMAC).c_str());
    }
    snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "WiFi PW request to %s", MACToString(destMAC).c_str());
    return ok;
}

// --- LAN Ping Request (Type 5 / 105) ---
void makeLanPingMsg(ESPNOW_type& msg, uint64_t targetMAC, uint8_t tier) {
    // tier 1 = ESP-NOW, tier 2 = UDP
    byte tempMAC[6] = {0};
    uint64ToMAC(targetMAC, tempMAC);
    memcpy(msg.targetMAC, tempMAC, 6);
    if (tier == 2) {
        msg.msgType = UDP_MSG_PING_RESPONSE_REQUIRED;
    } else {
        msg.msgType = ESPNOW_MSG_PING_RESPONSE_REQUIRED;
    }
    packLanMeta(msg.payload, isTimeValid(I.currentTime) ? (uint32_t)I.currentTime : 0, true);
    packLanMetaName(msg.payload, Prefs.DEVICENAME);
}


bool sendLANSensorDataRequest(ArborysDevType* targetDevice, uint8_t tier) {
    // type 7 / 107 / both; recipient responds with type 12 / 112 per transport
    return sendLANPingRequest(targetDevice, tier, true);
}

bool sendLANSensorData(ArborysSnsType* sensor, uint64_t targetMAC, IPAddress targetIP, uint8_t tier) {
    if (!sensor || !sensor->IsSet) return false;
    if (tier < 1 || tier > 3) return false;

    ESPNOW_type msg = {};
    uint8_t tempMAC[6] = {0};
    uint64ToMAC(targetMAC, tempMAC);
    memcpy(msg.targetMAC, tempMAC, 6);
    packLANSensorPayload(msg.payload, sensor);

    bool ok = false;
    if (tier == 2 || tier == 3) {
        msg.msgType = UDP_MSG_SENSOR_DATA;
        ok |= sendLANmsg_UDP(msg, targetIP, false);
    }
    if (tier == 1 || tier == 3) {
        msg.msgType = ESPNOW_MSG_SENSOR_DATA;
        ok |= sendLANmsg_ESPNOW(msg, false);
    }
    return ok;
}

bool sendLANSensorDataAll(uint64_t targetMAC, IPAddress targetIP, uint8_t tier) {
    int16_t myIdx = Sensors.findMyDeviceIndex();
    bool ok = false;
    for (int16_t i = 0; i < NUMSENSORS; ++i) {
        ArborysSnsType* S = Sensors.snsIndexToPointer(i);
        if (!S || !S->IsSet || S->deviceIndex != myIdx) continue;
        ok |= sendLANSensorData(S, targetMAC, targetIP, tier);
    }
    return ok;
}

bool sendLANPingRequest(ArborysDevType* targetDevice, uint8_t tier, bool dataRequest) {
    // Prepare ping request message (type 5) with current unix timestamp
    //tier is 1 = ESPNow directed ping, tier 2 = directed UDP, tier 3 = both ESP and UDP Directed ping (for HTTP or UDP use http specific code) 
    ESPNOW_type msg = {};
        
    bool ok = false;
    if (tier == 2 || tier == 3) {
        makeLanPingMsg(msg, targetDevice->MAC, 2);
        if (dataRequest) {
            msg.msgType = UDP_MSG_REQUEST_SENSOR_DATA;            
        } else {
            msg.msgType = UDP_MSG_PING_RESPONSE_REQUIRED;
        }
        
        ok |= sendLANmsg_UDP(msg, targetDevice->IP, false);
    } 
    if (tier == 1 || tier == 3) {
        makeLanPingMsg(msg, targetDevice->MAC, 1);

        if (dataRequest) {
            msg.msgType = ESPNOW_MSG_REQUEST_SENSOR_DATA;
        } else {
            msg.msgType = ESPNOW_MSG_PING_RESPONSE_REQUIRED;
        }
        
        ok |= sendLANmsg_ESPNOW(msg, false);
    }
    
    if (!ok) {
        if (tier == 2 || tier == 3) {
            snprintf(I.UDP_LAST_OUTGOINGMSG_TYPE, 10, "PingFail");
        } 
        if (tier == 1 || tier == 3) {
            snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Ping request failed to %s", MACToString(targetDevice->MAC).c_str());
        }
        storeError("ESPNow: Failed to send ping request");
    } else {
        if (tier == 2 || tier == 3) {
            snprintf(I.UDP_LAST_OUTGOINGMSG_TYPE, 10, "PingSent");
        } 
        if (tier == 1 || tier == 3) {
            snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Ping request to %s", MACToString(targetDevice->MAC).c_str());
        }
    }
    return ok;
}

bool sendLANBlockingPing(ArborysDevType* targetDevice, uint8_t tier, uint16_t blockTimeMs, uint32_t* rttMsOut) {
    // tier: 1 = ESP-NOW, 2 = UDP, 3 = both (success if either responds)
    if (rttMsOut) *rttMsOut = 0;
    if (!targetDevice || !targetDevice->IsSet) return false;
    if (tier < 1 || tier > 3) return false;
    if (s_blockingPingActive) {
        storeError("ESPNow: blocking ping already in progress");
        return false;
    }
    if (blockTimeMs == 0) blockTimeMs = ESPNOW_BLOCKING_PING_DEFAULT_MS;

    uint32_t requestId = millis();
    ESPNOW_type msg = {};
    bool sent = false;

    s_blockingPingActive = true;
    s_blockingPingGotResponse = false;
    s_blockingPingExpectedFromMAC = targetDevice->MAC;
    s_blockingPingRequestId = requestId;
    s_blockingPingTier = tier;

    if (tier == 2 || tier == 3) {
        makeESPNowBlockingPingMsg(msg, targetDevice->MAC, requestId, true);
        sent |= sendLANmsg_UDP(msg, targetDevice->IP, false);
    }
    if (tier == 1 || tier == 3) {
        makeESPNowBlockingPingMsg(msg, targetDevice->MAC, requestId, false);
        sent |= sendLANmsg_ESPNOW(msg, false);
    }

    if (!sent) {
        s_blockingPingActive = false;
        snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Blocking ping send failed to %s",
            MACToString(targetDevice->MAC).c_str());
        storeError("ESPNow: Failed to send blocking ping");
        return false;
    }

    snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, 64, "Blocking ping to %s id=%lu tier=%u",
        MACToString(targetDevice->MAC).c_str(), (unsigned long)requestId, tier);

    const uint32_t start = millis();
    while ((millis() - start) < blockTimeMs) {
        esp_task_wdt_reset();
        if (s_blockingPingGotResponse) {
            s_blockingPingActive = false;
            const uint32_t rttMs = millis() - start;
            if (rttMsOut) *rttMsOut = rttMs;
            snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Blocking ping OK from %s",
                MACToString(targetDevice->MAC).c_str());
            SerialPrint("LAN: Blocking ping success from " + MACToString(targetDevice->MAC) +
                " in " + String(rttMs) + " ms", true);
            return true;
        }
        #ifdef _USEUDP
        if (tier == 1) {
          delay(1);
        } else {
          delayWithNetwork(1, 1);
        }
        #else
        delay(1);
        #endif
    }

    s_blockingPingActive = false;
    snprintf(I.ESPNOW_LAST_INCOMINGMSG_PAYLOAD, 64, "Blocking ping timeout %s",
        MACToString(targetDevice->MAC).c_str());
    SerialPrint("LAN: Blocking ping timeout to " + MACToString(targetDevice->MAC) +
        " after " + String(blockTimeMs) + " ms", true);
    storeError("ESPNow: Blocking ping timeout");
    return false;
}

bool encryptLANMessage(ESPNOW_type& msg, byte msglen) {
    // Encrypt payload with ESPNOW_KEY. Plaintext max is msglen - 16 (AES-CBC IV prefix).
    // LAN_ENCRYPT_MSG_LEN (96) allows 80-byte plaintext.

    if (!lanMsgIsEncrypted(msg.msgType)) return false;

    if (!isValidLMKKey()) {
        storeError("ESPNow: LMK not valid, message cannot be encrypted");
        return false;
    }
    
    uint8_t encrypted[msglen] = {0};
    uint16_t outlen = 0;

    int8_t ret = BootSecure::encrypt((const unsigned char*)msg.payload, msglen-16, (char*)Prefs.KEYS.ESPNOW_KEY, encrypted, &outlen,16);
    if (ret == -2) {
        storeError("ESPNow: Invalid key");
        return false;
    }
    if (ret == -3) {
        storeError("ESPNow: Failed to encrypt message");
        return false;
    }
    memcpy(msg.payload, encrypted, (outlen < msglen) ? outlen : msglen);

    return true;
}

bool decryptLANMessage(ESPNOW_type& msg, byte msglen) {
    uint8_t decrypted[msglen] = {0};

    int8_t ret = BootSecure::decrypt((unsigned char*)msg.payload, (char*)Prefs.KEYS.ESPNOW_KEY, decrypted, msglen, 16);
    if (ret == -2) {
        storeError("ESPNow: Invalid key");
        return false;
    }
    if (ret == -3) {
        storeError("ESPNow: Failed to decrypt message");
        return false;
    }

    uint16_t actualDecryptedLen = msglen - 16;
    memcpy(msg.payload, decrypted, actualDecryptedLen);
    if (actualDecryptedLen < sizeof(msg.payload)) {
        msg.payload[actualDecryptedLen] = 0;
    }
    return true;
}

void makeBroadcastMAC(uint8_t* mac) {
    memset(mac, 0xFF, 6);
}

bool isBroadcastMAC(uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) {
            return false;
        }
    }
    return true;
}
//---------------------------------


bool sendLANmsg_UDP(ESPNOW_type msg, IPAddress targetIP, bool forceencrypt) {
    //send the message via UDP
    //return true if successful, false if failed
    //the message is already encrypted, so we don't need to encrypt it again
    //the message is already in the payload field of the ESPNOW_type struct
    //the message is already in the length field of the ESPNOW_type struct
    //the targetMAC field of the ESPNOW_type struct has the recipient MAC address
    #ifdef _USEUDP

    //since this is ESPNow type, will use esp tracking variables from I
    I.ESPNOW_LAST_OUTGOINGMSG_TYPE = msg.msgType;
    I.ESPNOW_LAST_OUTGOINGMSG_TO_MAC = MACToUint64(msg.targetMAC);
    I.ESPNOW_LAST_OUTGOINGMSG_TIME = I.currentTime;
    snprintf(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD, sizeof(I.ESPNOW_LAST_OUTGOINGMSG_PAYLOAD), (char*)msg.payload);


    uint64ToMAC(Prefs.PROCID, msg.senderMAC);

    if (forceencrypt || lanMsgIsEncrypted(msg.msgType)) {
        if (encryptLANMessage(msg, LAN_ENCRYPT_MSG_LEN)==false) return false;
    }

// Check if this is a broadcast message (all 0xFF in targetMAC)
    bool isBroadcast = false;
    if (isBroadcastMAC(msg.targetMAC)) isBroadcast = true;

    
    if (isBroadcast) {
        targetIP = IPAddress(0,0,0,0); //generic broadcast address for arborys UDP messages
    } else {
        if (targetIP == IPAddress(255,255,255,255) || targetIP == IPAddress(0,0,0,0) || targetIP == IPAddress(_USEUDP_MULTICAST) || targetIP == IPAddress(239, 1, 2, 3) || targetIP == IPAddress(192,168,68,255) || targetIP == IPAddress(239, 255, 255, 250) ) { //broadcast addresses specified, but this is not a broadcast,  check if we need to find the IP address of the device
            ArborysDevType* d = Sensors.getDeviceByMAC(MACToUint64(msg.targetMAC));
            if (d) {
                targetIP = d->IP;
            } else {
                storeError("No device found for non-broadcast UDP message");
                return false;
            }
        } 
    } 
    String msgType = "LAN:" + String(msg.msgType);
    bool sendResult = sendUDPMessage((uint8_t*)&msg, targetIP, sizeof(ESPNOW_type), msgType.c_str());
    
    if (!sendResult) {
        storeError("sendLANmsg_UDP: sendUDPMessage failed", ERROR_ESPNOW_SEND);
        return false;
    }
    
    return true;
    #else
    return false;
    #endif
}

