#include "hardware_fault.hpp"
#include "globals.hpp"
#include "server.hpp"
#include "utility.hpp"
#include "device_roles.hpp"
#include "ble_provision.hpp"
#include <esp_task_wdt.h>
#include <string.h>

#if _HAS_LOCAL_SENSORS
#include "sensors.hpp"
#endif

static uint8_t s_hwFaultMask = 0;
static uint32_t s_lastHubAnnounceUtc = 0;
static constexpr uint32_t kHardwareFaultAnnounceSec = 300;

void hardwareFaultSet(uint8_t maskBits) {
  s_hwFaultMask |= maskBits;
}

uint8_t hardwareFaultMask() {
  return s_hwFaultMask;
}

bool hardwareFaultBlocksLocalSensors() {
#if _HAS_LOCAL_SENSORS
  if (!_I_AM_PERIPHERAL) return false;
  return s_hwFaultMask != 0;
#else
  return false;
#endif
}

void hardwareFaultSummary(char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';
  bool namedChip = false;
  auto append = [&](const char* label) {
    if (!label || !label[0]) return;
    if (out[0]) {
      strncat(out, ", ", outLen - strlen(out) - 1);
    }
    strncat(out, label, outLen - strlen(out) - 1);
  };
#if _HAS_LOCAL_SENSORS
#ifdef _USETFLUNA
  if (hardwareInitFailedForI2cAddr((uint8_t)_USETFLUNA)) {
    append("TF-Luna");
    namedChip = true;
  }
#endif
#ifdef _USEAHT
  #if defined(AHTXX_ADDRESS_X38)
    if (hardwareInitFailedForI2cAddr(AHTXX_ADDRESS_X38)) { append("AHT"); namedChip = true; }
  #else
    if (hardwareInitFailedForI2cAddr(0x38)) { append("AHT"); namedChip = true; }
  #endif
#endif
#ifdef _USEAHTADA
  if (hardwareInitFailedForI2cAddr(0x38)) { append("AHT"); namedChip = true; }
#endif
#ifdef _USEBMP
  if (hardwareInitFailedForI2cAddr(0x76) && hardwareInitFailedForI2cAddr(0x77)) {
    append("BMP");
    namedChip = true;
  }
#endif
#ifdef _USEBME
  if (hardwareInitFailedForI2cAddr(0x76) && hardwareInitFailedForI2cAddr(0x77)) {
    append("BME");
    namedChip = true;
  }
#endif
#ifdef _USEBME680
  if (hardwareInitFailedForI2cAddr(0x76) && hardwareInitFailedForI2cAddr(0x77)) {
    append("BME680");
    namedChip = true;
  }
#endif
#ifdef _USEADS1115
  if (hardwareInitFailedForI2cAddr((uint8_t)_USEADS1115)) {
    append("ADS1115");
    namedChip = true;
  }
#endif
#endif
  if ((s_hwFaultMask & HW_FAULT_I2C_SENSOR) && !namedChip) append("I2C");
  if (s_hwFaultMask & HW_FAULT_OLED) append("OLED");
  if (s_hwFaultMask & HW_FAULT_LED_MATRIX) append("LED matrix");
  if (s_hwFaultMask & HW_FAULT_SPI_SD) append("SPI/SD");
  if (s_hwFaultMask & HW_FAULT_TFT) append("SPI screen");
  if (!out[0]) strncpy(out, "hardware", outLen - 1);
  out[outLen - 1] = '\0';
}

void hardwareFaultAnnounceToHubs(bool force) {
  if (!hardwareFaultBlocksLocalSensors()) return;
  const uint32_t now = (uint32_t)utcNow();
  if (!force && s_lastHubAnnounceUtc != 0 && now >= s_lastHubAnnounceUtc
      && now - s_lastHubAnnounceUtc < kHardwareFaultAnnounceSec) {
    return;
  }
  s_lastHubAnnounceUtc = now;

  char detail[80];
  hardwareFaultSummary(detail, sizeof(detail));
  char msg[120];
  snprintf(msg, sizeof(msg), "Device hardware fault — no sensor data (%s)", detail);
  storeError(msg, ERROR_SENSOR_INVALID, true);
  SerialPrint(String("Hardware fault mode: ") + detail, true);
}

bool serviceHardwareFaultMode() {
  if (!hardwareFaultBlocksLocalSensors()) return false;

  systemHousekeeping(false);
  // This path returns before loop() reaches the minute CheckWifiStatus.
  // BLE advertises only while the soft AP is up, so a dropped AP otherwise
  // leaves both radios dark for the rest of the boot.
  if (!wifiReadyForNetwork() && !softApRunning()) {
    CheckWifiStatus(WIFI_CHECK_BOOT);
    bleProvisionService();
  }
  hardwareFaultAnnounceToHubs(false);
  esp_task_wdt_reset();
  delay(50);
  return true;
}

#if _HAS_LOCAL_SENSORS
void auditLocalHardwareInit() {
  if (!_I_AM_PERIPHERAL) return;

#ifdef _USETFLUNA
  {
    const uint8_t tfAddr = (uint8_t)_USETFLUNA;
    if (hardwareInitFailedForI2cAddr(tfAddr)) {
      hardwareFaultSet(HW_FAULT_TFLUNA | HW_FAULT_I2C_SENSOR);
    }
  }
#endif

#ifdef _USEAHT
  #if defined(AHTXX_ADDRESS_X38)
    const byte ahtAddr = AHTXX_ADDRESS_X38;
  #else
    const byte ahtAddr = 0x38;
  #endif
  if (hardwareInitFailedForI2cAddr(ahtAddr)) hardwareFaultSet(HW_FAULT_I2C_SENSOR);
#endif
#ifdef _USEAHTADA
  if (hardwareInitFailedForI2cAddr(0x38)) hardwareFaultSet(HW_FAULT_I2C_SENSOR);
#endif
#ifdef _USEBMP
  if (hardwareInitFailedForI2cAddr(0x76) && hardwareInitFailedForI2cAddr(0x77)) {
    hardwareFaultSet(HW_FAULT_I2C_SENSOR);
  }
#endif
#ifdef _USEBME
  if (hardwareInitFailedForI2cAddr(0x76) && hardwareInitFailedForI2cAddr(0x77)) {
    hardwareFaultSet(HW_FAULT_I2C_SENSOR);
  }
#endif
#ifdef _USEBME680
  if (hardwareInitFailedForI2cAddr(0x76) && hardwareInitFailedForI2cAddr(0x77)) {
    hardwareFaultSet(HW_FAULT_I2C_SENSOR);
  }
#endif
#ifdef _USEADS1115
  if (hardwareInitFailedForI2cAddr((uint8_t)_USEADS1115)) hardwareFaultSet(HW_FAULT_I2C_SENSOR);
#endif

  // Any local sensor slot encoded as I2C address (400–599).
  {
    int16_t snsPinsInit[] = _SNSPINS;
    for (byte i = 0; i < _SENSORNUM; i++) {
      int8_t corrected = -1;
      const uint8_t pt = getPinType(snsPinsInit[i], &corrected);
      if ((pt == 9 || pt == 10) && corrected >= 0) {
        if (hardwareInitFailedForI2cAddr((uint8_t)corrected)) {
          hardwareFaultSet(HW_FAULT_I2C_SENSOR);
        }
      }
    }
  }

  if (s_hwFaultMask != 0) {
    char detail[80];
    hardwareFaultSummary(detail, sizeof(detail));
    SerialPrint(String("Local hardware audit failed: ") + detail, true);
  }
}
#endif
