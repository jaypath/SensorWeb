#include "BootSecure.hpp"
#include "esp_random.h"

BootSecure::BootSecure() {}

bool BootSecure::setup() {
    secure = false;
    if (!checkDeviceID()) {
#ifdef SETSECURE
        return false;
#else
        Prefs.PROCID = ESP.getEfuseMac();
#endif
    }
    if (!getPrefs()) {
#ifdef SETSECURE
        return false;
#else
        return setPrefs();
#endif
    }
    secure = true;
    return true;
}

bool BootSecure::checkDeviceID() {
    return Prefs.PROCID == ESP.getEfuseMac();
}

uint16_t BootSecure::CRCCalculator(uint8_t * data, uint16_t length) {
    uint16_t curr_crc = 0x0000;
    uint8_t sum1 = (uint8_t) curr_crc;
    uint8_t sum2 = (uint8_t) (curr_crc >> 8);
    for(int index = 0; index < length; index++) {
        sum1 = (sum1 + data[index]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

bool BootSecure::getPrefs() {
    Preferences p;
    p.begin("STARTUP", true);
    if (p.isKey("Boot")) {
        uint16_t p_length = sizeof(STRUCT_PrefsH) + (16 - (sizeof(STRUCT_PrefsH) % 16)) + 16; // padding + iv
        uint8_t tempPrefs[p_length];
        memset(tempPrefs, 0, p_length);
        p.getBytes("Boot", tempPrefs, p_length);
        unsigned char decodedPrefs[p_length];
        memset(decodedPrefs, 0, p_length);
        decrypt(tempPrefs, (char*)AESKEY, decodedPrefs, p_length);
        memcpy(&Prefs, decodedPrefs, sizeof(STRUCT_PrefsH));
        zeroize(decodedPrefs, p_length);
        p.end();
    } else {
        p.end();
        return false;
    }
    return true;
}

bool BootSecure::setPrefs() {
    if (Prefs.PROCID != ESP.getEfuseMac()) {
#ifdef SETSECURE
        return false;
#else
        Prefs.PROCID = ESP.getEfuseMac();
#endif
    }
    Preferences p;
    if (!p.begin("ORIGIN", false)) return false;
    uint16_t p_length = sizeof(STRUCT_PrefsH) + (16 - (sizeof(STRUCT_PrefsH) % 16)) + 16; // padding + iv
    uint8_t tempPrefs[p_length];
    memset(tempPrefs, 0, p_length);
    uint16_t outlen = 0;
    if (encrypt((const unsigned char*)&Prefs, sizeof(STRUCT_PrefsH), (char*)AESKEY, tempPrefs, &outlen) != 0) {
        p.end();
        return false;
    }
    p.putBytes("Boot", tempPrefs, outlen);
    p.end();
    zeroize(tempPrefs, p_length);
    return true;
}

int8_t BootSecure::encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output, uint16_t* outputlength) {
    uint16_t paddedLen = inputlength;
    if (inputlength == 0 || inputlength % 16 != 0) {
        paddedLen += 16 - (inputlength % 16);
    }
    uint8_t localinput[paddedLen];
    memset(localinput, 0, paddedLen);
    memcpy(localinput, input, inputlength);
    *outputlength = paddedLen + 16; // IV + encrypted data
    uint8_t iv[16];
    esp_fill_random(iv, 16);
    memcpy(output, iv, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, localinput, output + 16);
    mbedtls_aes_free(&aes);
    zeroize(localinput, paddedLen);
    if (ret != 0) return -3;
    return 0;
}

int8_t BootSecure::decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength) {
    uint8_t iv[16];
    memcpy(iv, input, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key, strlen(key) * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, datalength - 16, iv, input + 16, output);
    mbedtls_aes_free(&aes);
    if (ret != 0) return -3;
    return 0;
}

// --- AES-CBC with provided IV (no IV prepended/extracted) ---
int8_t BootSecure::encryptWithIV(const unsigned char* input, uint16_t inputlength, char* key, uint8_t* iv, unsigned char* output, uint16_t* outputlength) {
    uint16_t paddedLen = inputlength;
    if (inputlength == 0 || inputlength % 16 != 0) {
        paddedLen += 16 - (inputlength % 16);
    }
    uint8_t localinput[paddedLen];
    memset(localinput, 0, paddedLen);
    memcpy(localinput, input, inputlength);
    *outputlength = paddedLen; // No IV prepended
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16); // mbedtls_aes_crypt_cbc updates IV
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, localinput, output);
    mbedtls_aes_free(&aes);
    zeroize(localinput, paddedLen);
    if (ret != 0) return -3;
    return 0;
}

int8_t BootSecure::decryptWithIV(unsigned char* input, char* key, uint8_t* iv, unsigned char* output, uint16_t datalength) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key, strlen(key) * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, datalength, iv_copy, input, output);
    mbedtls_aes_free(&aes);
    if (ret != 0) return -3;
    return 0;
}

void BootSecure::zeroize(void* buf, size_t len) {
    volatile uint8_t* p = (volatile uint8_t*)buf;
    while (len--) *p++ = 0;
}

void initCreds(WiFi_type *w) {
    for (byte j=0;j<33;j++) w->SSID[j]=0;
    for (byte j=0;j<65;j++) w->PWD[j]=0;
}

// --- Secure WiFi Credentials Storage ---
bool putWiFiCredentials() {
    bool isGood = false;
    uint16_t sCRC = BootSecure::CRCCalculator((uint8_t*) WIFI_INFO.SSID, 32);
    uint16_t pCRC = BootSecure::CRCCalculator((uint8_t*) WIFI_INFO.PWD, 64);
    Preferences p;
    p.begin("credentials", false);
    p.clear();

    // If SSID is blank, delete credentials
    if (WIFI_INFO.SSID[0] == '\0' || sCRC == 0) {
        initCreds(&WIFI_INFO);
        sCRC = 0;
        pCRC = 0;
        p.end();
        return false;
    } else {
        isGood = true;
    }

    // Use device-unique key
    uint64_t mac = ESP.getEfuseMac();
    char key[33];
    snprintf(key, sizeof(key), "%s%08llX", AESKEY, mac & 0xFFFFFFFFFFFF);

    // Encrypt SSID
    uint8_t encryptedSSID[48] = {0};
    uint16_t ssidLen = 0;
    BootSecure::encrypt((const unsigned char*)WIFI_INFO.SSID, 33, key, encryptedSSID, &ssidLen);
    p.putBytes("SSID", encryptedSSID, ssidLen);
    p.putUInt("SSIDCRC", sCRC);
    BootSecure::zeroize(encryptedSSID, sizeof(encryptedSSID));

    // Encrypt PWD
    uint8_t encryptedPWD[80] = {0};
    uint16_t pwdLen = 0;
    BootSecure::encrypt((const unsigned char*)WIFI_INFO.PWD, 65, key, encryptedPWD, &pwdLen);
    p.putBytes("PWD", encryptedPWD, pwdLen);
    p.putUInt("PWDCRC", pCRC);
    BootSecure::zeroize(encryptedPWD, sizeof(encryptedPWD));

    BootSecure::zeroize(key, sizeof(key));
    p.end();
    return isGood;
}

bool getWiFiCredentials() {
    initCreds(&WIFI_INFO);
    Preferences p;
    p.begin("credentials", true);
    uint16_t sCRC = p.getUInt("SSIDCRC", 0);
    uint16_t pCRC = p.getUInt("PWDCRC", 0);
    WIFI_INFO.HAVECREDENTIALS = false;
    if (sCRC == 0 && pCRC == 0) {
        p.end();
        return false;
    }
    // Use device-unique key
    uint64_t mac = ESP.getEfuseMac();
    char key[33];
    snprintf(key, sizeof(key), "%s%08llX", AESKEY, mac & 0xFFFFFFFFFFFF);

    // Decrypt SSID
    uint8_t encryptedSSID[48] = {0};
    size_t ssidLen = p.getBytesLength("SSID");
    if (ssidLen > 0 && ssidLen <= sizeof(encryptedSSID)) {
        p.getBytes("SSID", encryptedSSID, ssidLen);
        BootSecure::decrypt(encryptedSSID, key, (unsigned char*)WIFI_INFO.SSID, ssidLen);
    }
    BootSecure::zeroize(encryptedSSID, sizeof(encryptedSSID));

    // Decrypt PWD
    uint8_t encryptedPWD[80] = {0};
    size_t pwdLen = p.getBytesLength("PWD");
    if (pwdLen > 0 && pwdLen <= sizeof(encryptedPWD)) {
        p.getBytes("PWD", encryptedPWD, pwdLen);
        BootSecure::decrypt(encryptedPWD, key, (unsigned char*)WIFI_INFO.PWD, pwdLen);
    }
    BootSecure::zeroize(encryptedPWD, sizeof(encryptedPWD));
    BootSecure::zeroize(key, sizeof(key));
    p.end();
    // CRC check
    if (sCRC != BootSecure::CRCCalculator((uint8_t*)WIFI_INFO.SSID, 32)) return false;
    if (pCRC != BootSecure::CRCCalculator((uint8_t*)WIFI_INFO.PWD, 64)) return false;
    WIFI_INFO.HAVECREDENTIALS = true;
    return true;
}
  