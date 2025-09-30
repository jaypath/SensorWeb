#ifndef BootSecure_HPP
#define BootSecure_HPP

// Replicated secure preferences storage for generic sensors

#if defined(ESP32)
#define _USE32 1
#endif

#ifdef _USE32
  #include <Preferences.h>
  #include <mbedtls/aes.h>
  #include <mbedtls/cipher.h>
  #include "esp_random.h"
  #include <WiFi.h>
#endif

#include "../../GLOBAL_LIBRARY/globals.hpp"
#include "../../GLOBAL_LIBRARY/utility.hpp"

#define AESKEY "YfMVDR2qtzxJdD9yNhN6IDGPwgpyMjk2"

// Global-style helpers (kept for API parity with weatherbase server)
uint16_t CRCCalculator(uint8_t * data, uint16_t length);
int8_t decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength);
int8_t encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output,  uint16_t* outputlength);
bool getWiFiCredentials();
bool putWiFiCredentials();
void initCreds(struct STRUCT_PrefsH *w);
bool setPrefs();

class BootSecure {
public:
    BootSecure();
    int8_t setup(); // Returns 1 if loaded, 0 if none, -1 error, -2 PROCID mismatch (if enforced)
    bool isSecure() const { return secure; }
    static bool setPrefs();

    static uint16_t CRCCalculator(uint8_t * data, uint16_t length);
    static int8_t encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output,  uint16_t* outputlength);
    static int8_t decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength);
    static int8_t encryptWithIV(const unsigned char* input, uint16_t inputlength, char* key, uint8_t* iv, unsigned char* output, uint16_t* outputlength);
    static int8_t decryptWithIV(unsigned char* input, char* key, uint8_t* iv, unsigned char* output, uint16_t datalength);
    static void zeroize(void* buf, size_t len);

private:
    bool secure = false;
    int8_t getPrefs();
    bool checkDeviceID();
};

#endif

