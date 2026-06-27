// Override the weak esp_app_desc from Arduino-ESP32 prebuilt libs.
// Version string: CONFIG_APP_PROJECT_VER in platformio.ini build_flags (ESP-IDF naming).
#include "esp_app_desc.h"

#ifndef CONFIG_APP_PROJECT_VER
#define CONFIG_APP_PROJECT_VER "0.0.0"
#endif

#if defined(__APPLE__) && defined(CONFIG_IDF_TARGET_LINUX) && CONFIG_IDF_TARGET_LINUX
const __attribute__((weak)) __attribute__((section("__RODATA_DESC,.rodata_desc"))) esp_app_desc_t esp_app_desc = {
#else
const __attribute__((weak)) __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
#endif
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = CONFIG_APP_PROJECT_VER,
    .project_name = "ArborysSensorWeb",
    .time = __TIME__,
    .date = __DATE__,
    .idf_ver = IDF_VER,
};
