#ifndef HARDWARE_FAULT_HPP
#define HARDWARE_FAULT_HPP

#include "device_roles.hpp"
#include <stdint.h>
#include <stddef.h>

// Sticky boot-time faults for optional local hardware (I2C sensors, displays, SPI/SD).
enum HardwareFaultBit : uint8_t {
  HW_FAULT_I2C_SENSOR = 1 << 0,
  HW_FAULT_TFLUNA     = 1 << 1,
  HW_FAULT_OLED       = 1 << 2,
  HW_FAULT_LED_MATRIX = 1 << 3,
  HW_FAULT_SPI_SD     = 1 << 4,
  HW_FAULT_TFT        = 1 << 5,
};

void hardwareFaultSet(uint8_t maskBits);
uint8_t hardwareFaultMask();

/** True when the device must not read or uplink local sensor data. */
bool hardwareFaultBlocksLocalSensors();

/** Human-readable list for hub errorLog detail (comma-separated). */
void hardwareFaultSummary(char* out, size_t outLen);

/** Forward a single consolidated error to hubs (peripherals only). */
void hardwareFaultAnnounceToHubs(bool force);

/** Periodic re-announce (5 min) and minimal loop work while blocked. Returns true if caller should skip normal sensor loop body. */
bool serviceHardwareFaultMode();

#if _HAS_LOCAL_SENSORS
/** Call after initHardwareSensors(); sets fault bits from init results. */
void auditLocalHardwareInit();
#endif

#endif
