#ifdef _USELOWPOWER
#include "LowPower.hpp"
#include "firmwareUpdate.hpp"
#include "server.hpp"
#include "AddESPNOW.hpp"
#include "sensors.hpp"
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <math.h>

// Stay-alive budgets (dedicated LP loop — not main loop()).
static constexpr uint32_t LP_APSTA_IDLE_MS = 300000UL;       // 5 min idle in APSTA
static constexpr uint32_t LP_NO_SERVER_MS = 300000UL;        // 5 min firmware path with no server
static constexpr uint32_t LP_FW_DOWNLOAD_MAX_MS = 600000UL;  // 10 min download cap
static constexpr uint32_t LP_SERVER_PING_INTERVAL_MS = 60000UL;
static constexpr uint32_t LP_FW_CHECK_WINDOW_SEC = 21600UL;  // 6 hours
static constexpr double LP_FW_CHECK_CUMULATIVE_P = 0.75;     // ≤75% chance of ≥1 check / window
// One device slot is local; evaluate/register at most this many servers.
static constexpr uint8_t LP_MAX_SERVERS = (NUMDEVICES > 1) ? (uint8_t)(NUMDEVICES - 1) : 1;

static void LOWPOWER_enableWdt() {
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
}

static void LOWPOWER_pump() {
    if (wifiReadyForNetwork()) {
        CheckWifiStatus(WIFI_CHECK_NORMAL);
    }
    if (softApRunning()) {
        serviceAPStationMode();
    }
    server.handleClient();
    receiveUDPMessage();
    processChunkFirmwareTick();
    esp_task_wdt_reset();
}

static void LOWPOWER_doSensorCycle() {
    if (!wifiReadyForNetwork()) {
        SerialPrint("LP: WiFi not ready — skip sensors", true);
        return;
    }
    byte snscount = readAllSensors(true);
    if (snscount > 0) {
        sendAllSensors(true, -1, true);
    } else {
        SerialPrint("LP: No sensors to send", true);
    }
}

/** Per-wake check probability so P(≥1 check in 6h) ≤ 75% given sleep interval. */
static uint8_t LOWPOWER_firmwareCheckThreshold() {
    const double sleepSec = (double)(_USELOWPOWER / 1000000ULL);
    if (sleepSec <= 0) return 1;
    // 1 - (1-p)^n ≤ C  =>  p ≤ 1 - (1-C)^(1/n) = 1 - (1-C)^(S/T)
    const double pMax = 1.0 - pow(1.0 - LP_FW_CHECK_CUMULATIVE_P, sleepSec / (double)LP_FW_CHECK_WINDOW_SEC);
    int x = (int)(100.0 * pMax); // floor
    if (x < 1) x = 1;
    if (x > 100) x = 100;
    return (uint8_t)x;
}

static bool LOWPOWER_shouldEnterFirmwarePath() {
    const uint8_t x = LOWPOWER_firmwareCheckThreshold();
    // Uniform 1..100; enter when roll < x  => P ≈ (x-1)/100.
    // With x from floor(100*pMax), use roll <= x so P = x/100 matches the bound.
    const uint8_t roll = (uint8_t)(1U + (esp_random() % 100U));
    const bool enter = (roll <= x);
    SerialPrint("LP FW gate: roll=" + String(roll) + " x=" + String(x)
        + " sleepSec=" + String((uint32_t)(_USELOWPOWER / 1000000ULL))
        + (enter ? " → firmware path" : " → fast path"), true);
    return enter;
}

/** APSTA recovery: stay until 5 min idle (activity resets idle timer), then sleep. */
static void LOWPOWER_stayAliveApsta() {
    if (!softApRunning()) {
        enterAPStationMode();
    }
    uint32_t idleAnchor = getApStationEnterMillis();
    if (idleAnchor == 0) idleAnchor = millis();
    SerialPrint("LP: APSTA stay-alive (5 min idle)", true);

    while (true) {
        LOWPOWER_pump();
        if (wifiReadyForNetwork()) {
            SerialPrint("LP: STA recovered during APSTA stay-alive", true);
            maybeExitAPStationMode();
            return;
        }
        if (apStationUserActive()) {
            idleAnchor = millis();
        } else if ((millis() - idleAnchor) >= LP_APSTA_IDLE_MS) {
            SerialPrint("LP: APSTA idle timeout — sleeping", true);
            return;
        }
        delay(20);
    }
}

static bool LOWPOWER_fwSaidNo(const IPAddress& ip, const IPAddress* list, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        if (list[i] == ip) return true;
    }
    return false;
}

static void LOWPOWER_fwRememberNo(IPAddress ip, IPAddress* list, uint8_t& count) {
    if (count >= LP_MAX_SERVERS) return;
    if (LOWPOWER_fwSaidNo(ip, list, count)) return;
    list[count++] = ip;
}

/** Walk known servers in table order, capped at LP_MAX_SERVERS. */
static uint8_t LOWPOWER_collectServers(ArborysDevType** out, uint8_t outMax) {
    uint8_t n = 0;
    for (int16_t di = Sensors.nextServerIndex(0, false); di >= 0 && n < outMax;
         di = Sensors.nextServerIndex(di + 1, false)) {
        ArborysDevType* d = Sensors.getDeviceByDevIndex(di);
        if (!d || !d->IsSet || d->IP == IPAddress(0, 0, 0, 0)) continue;
        out[n++] = d;
    }
    return n;
}

/**
 * Firmware path: discover servers, networkState, FirmwareRequest, sensors, bounded stay-alive.
 * Server list is RAM-only for this wake (not persisted to NVS).
 */
static void LOWPOWER_firmwarePathStayAlive() {
    const uint32_t pathStart = millis();
    uint32_t lastServerPingMs = 0;
    uint32_t downloadStartMs = 0;
    bool opsDone = false;
    bool networkStateDone = false;
    bool sawServer = false;
    bool fwTransportFailed = false;
    IPAddress fwNoList[LP_MAX_SERVERS];
    uint8_t fwNoCount = 0;

    SerialPrint("LP: entering firmware check path (max servers=" + String(LP_MAX_SERVERS) + ")", true);
    broadcastServerPing(3); // ESP-NOW + UDP
    lastServerPingMs = millis();

    while (true) {
        LOWPOWER_pump();

        if (!wifiReadyForNetwork()) {
            SerialPrint("LP FW path: lost WiFi — falling back to APSTA", true);
            LOWPOWER_stayAliveApsta();
            return;
        }

        // Normal sensor ops once WiFi is up (independent of FW progress).
        if (!opsDone) {
            LOWPOWER_doSensorCycle();
            opsDone = true;
        }

        ArborysDevType* servers[LP_MAX_SERVERS];
        const uint8_t serverCount = LOWPOWER_collectServers(servers, LP_MAX_SERVERS);
        if (serverCount > 0) sawServer = true;

        // Broadcast for servers every minute until at least one is known.
        if (serverCount == 0
            && (millis() - lastServerPingMs) >= LP_SERVER_PING_INTERVAL_MS) {
            broadcastServerPing(3);
            lastServerPingMs = millis();
            SerialPrint("LP: server ping broadcast", true);
        }

        // After first server: request network state once (fills placeholders up to LP_MAX_SERVERS).
        if (serverCount > 0 && !networkStateDone) {
            IPAddress sip = servers[0]->IP;
            int16_t n = sendMSG_networkStateReq(sip, 5000);
            SerialPrint("LP: networkStateReq → " + String(n), true);
            networkStateDone = true; // one shot even on failure; servers may still self-register
        }

        // Firmware requests: first LP_MAX_SERVERS only; transport error ends this wake's FW attempts.
        if (!fwTransportFailed && !isFirmwareChunkSessionActive() && serverCount > 0) {
            uint8_t askedNo = 0;
            for (uint8_t i = 0; i < serverCount; i++) {
                IPAddress ip = servers[i]->IP;
                if (LOWPOWER_fwSaidNo(ip, fwNoList, fwNoCount)) {
                    askedNo++;
                    continue;
                }
                int8_t rc = sendMSG_FirmwareRequest(ip, true, 4000);
                if (rc == 1) {
                    downloadStartMs = millis();
                    SerialPrint("LP: firmware available — downloading", true);
                    break;
                }
                if (rc == 0) {
                    LOWPOWER_fwRememberNo(ip, fwNoList, fwNoCount);
                    askedNo++;
                    SerialPrint("LP: firmware unavailable from " + ip.toString(), true);
                    continue;
                }
                // Transport error: defer to next random FW cycle (do not burn the window retrying).
                fwTransportFailed = true;
                SerialPrint("LP: firmware request transport error from " + ip.toString()
                    + " — defer to next cycle", true);
                break;
            }
            if (fwTransportFailed && opsDone && !isFirmwareChunkSessionActive()) {
                SerialPrint("LP: FW transport failed + ops done — sleep", true);
                return;
            }
            if (!isFirmwareChunkSessionActive() && opsDone && askedNo >= serverCount) {
                SerialPrint("LP: all evaluated servers report no firmware + ops done — sleep", true);
                return;
            }
        }

        if (isFirmwareChunkSessionActive()) {
            if (downloadStartMs == 0) downloadStartMs = millis();
            if ((millis() - downloadStartMs) >= LP_FW_DOWNLOAD_MAX_MS) {
                SerialPrint("LP: firmware download timeout — sleep", true);
                return;
            }
        } else if (downloadStartMs != 0 && opsDone) {
            SerialPrint("LP: firmware download ended + ops done — sleep", true);
            return;
        }

        if (!sawServer && (millis() - pathStart) >= LP_NO_SERVER_MS) {
            SerialPrint("LP: no server for 5 min — sleep", true);
            return;
        }

        delay(20);
    }
}

void LOWPOWER_readAndSend() {
    LOWPOWER_enableWdt();

    // Brief wait if STA is still associating after boot budget.
    uint32_t start_time = millis();
    while (!wifiReadyForNetwork() && !softApRunning()
           && (millis() - start_time) < 10000UL) {
        delay(50);
        esp_task_wdt_reset();
    }

    if (!wifiReadyForNetwork()) {
        SerialPrint("LP: no WiFi — APSTA stay-alive", true);
        LOWPOWER_stayAliveApsta();
        // If STA recovered, optionally run a normal cycle before sleep.
        if (wifiReadyForNetwork()) {
            if (LOWPOWER_shouldEnterFirmwarePath()) {
                LOWPOWER_firmwarePathStayAlive();
            } else {
                LOWPOWER_doSensorCycle();
            }
        }
        LOWPOWER_sleep();
        return;
    }

    if (LOWPOWER_shouldEnterFirmwarePath()) {
        LOWPOWER_firmwarePathStayAlive();
    } else {
        LOWPOWER_doSensorCycle();
    }
    LOWPOWER_sleep();
}

void LOWPOWER_sleep(uint64_t sleepTime) {
    SerialPrint("Sleeping for " + String(sleepTime / 1000000ULL) + " seconds...", true);

    int8_t PowerPins[_SENSORNUM] = _POWERPINS;
    for (byte i = 0; i < _SENSORNUM; i++) {
        if (PowerPins[i] >= 0) {
            pinMode(PowerPins[i], OUTPUT);
            digitalWrite(PowerPins[i], LOW);
        }
    }

    esp_sleep_enable_timer_wakeup(sleepTime);
    delay(100);
    esp_deep_sleep_start();
}

void LOWPOWER_Initialize() {
    SerialPrint("Initializing low power mode...", true);

    int8_t PowerPins[_SENSORNUM] = _POWERPINS;
    for (byte i = 0; i < _SENSORNUM; i++) {
        if (PowerPins[i] >= 0) {
            pinMode(PowerPins[i], OUTPUT);
            digitalWrite(PowerPins[i], HIGH);
        }
    }
}
#endif
