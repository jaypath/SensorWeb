#include "globals.hpp"
#include "BootSecure.hpp"

byte prefs_set = 0;

// Encrypted NVS blob length for a given plaintext prefs size (AES-CBC padding + 16-byte IV).
static uint16_t prefsEncryptedLength(size_t plainSize) {
    uint8_t padding = 0;
    if (plainSize % 16 != 0) {
        padding = (uint8_t)(16 - (plainSize % 16));
    }
    return (uint16_t)(plainSize + padding + 16);
}

static int8_t decryptPrefsBlob(uint8_t* blob, uint8_t* decoded, uint16_t blobLen, size_t plainSize) {
    if (blobLen != prefsEncryptedLength(plainSize)) {
        return -1;
    }
    BootSecure::decrypt(blob, (char*)BOOTKEY, decoded, blobLen, 32);
    memset(&Prefs, 0, sizeof(Prefs));
    memcpy(&Prefs, decoded, plainSize);
    if (Prefs.PROCID != ESP.getEfuseMac()) {
        return -2;
    }
    Prefs.isUpToDate = true;
    return 1;
}


BootSecure::BootSecure() {}

int8_t BootSecure::setup() {
    int8_t prefs_status = getPrefs();

    if (prefs_status <= 0) { //no prefs found or error
        #ifdef SETSECURE
                return prefs_status;
        #else

                Prefs.isUpToDate = false;
        #endif
    }


    if (!checkDeviceID()) {
        #ifdef SETSECURE
                return -2; //error, PROCID mismatch
        #else
            if (Prefs.PROCID != ESP.getEfuseMac()) {
                Prefs.PROCID = ESP.getEfuseMac();
                Prefs.isUpToDate=false;
            }

        #endif
    }

    #ifdef _MYDEVICENAME
    if (Prefs.DEVICENAME[0] == '\0') {
        strncpy(Prefs.DEVICENAME, _MYDEVICENAME, sizeof(Prefs.DEVICENAME));
        Prefs.DEVICENAME[sizeof(Prefs.DEVICENAME) - 1] = '\0';
        Prefs.isUpToDate = false;
    }
    #endif

    return prefs_status;
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

int8_t BootSecure::flushPrefs(void) {
    //returns 1 if successful, -1 if could not open prefs, -2 if could not find key
    if (nvs_flash_erase() != ESP_OK) return -1;
    return 1;
}

int8_t BootSecure::getPrefs() {
    Preferences p;
    if (!p.begin("STARTUP", false)) return -1;
    if (!p.isKey("Boot")) {
        p.end();
        return 0; // no prefs found
    }

    const uint16_t storedLen = p.getBytesLength("Boot");
    const uint16_t currentLen = prefsEncryptedLength(sizeof(STRUCT_PrefsH));

    if (storedLen != currentLen) {
        storeError("BootSecure::getPrefs: Boot length mismatch, prefs failed to load", ERROR_FAILED_PREFS, false);
        p.remove("Boot");
        p.end();
        memset(&Prefs, 0, sizeof(Prefs));
        Prefs.isUpToDate = false;
        return -1;
    }

    uint8_t blob[currentLen];
    uint8_t decoded[currentLen];
    memset(blob, 0, currentLen);
    memset(decoded, 0, currentLen);
    p.getBytes("Boot", blob, storedLen);
    p.end();

    int8_t loadStatus = decryptPrefsBlob(blob, decoded, storedLen, sizeof(STRUCT_PrefsH));
    BootSecure::zeroize(blob, currentLen);
    BootSecure::zeroize(decoded, currentLen);

    if (loadStatus != 1) {
        storeError("BootSecure::getPrefs: Boot decrypt failed, prefs failed to load", ERROR_FAILED_PREFS, false);
        Preferences p2;
        if (p2.begin("STARTUP", false)) {
            p2.remove("Boot");
            p2.end();
        }
        memset(&Prefs, 0, sizeof(Prefs));
        Prefs.isUpToDate = false;
        return (loadStatus == -2) ? -2 : -1;
    }

    return 1;
}

int8_t BootSecure::setPrefs(bool forceUpdate) {
    //returns 0 if no update required, -1 if encryption failed, -10 if setsecure failed, -100 if prefs could not create (for example no space), and 1 if update
    if (Prefs.isUpToDate == true && forceUpdate==false ) return 0; //don't need to update
    if (Prefs.PROCID != ESP.getEfuseMac()) {
        #ifdef SETSECURE
                return -10;
        #else
                Prefs.PROCID = ESP.getEfuseMac();
                Prefs.isUpToDate = false;
        #endif
    }
    Preferences p;
    if (!p.begin("STARTUP", false)) return 0;
    uint8_t padding = 0;
    if (sizeof(STRUCT_PrefsH) % 16 != 0) {
        padding = 16 - (sizeof(STRUCT_PrefsH) % 16);
    }
    uint16_t p_length = sizeof(STRUCT_PrefsH) + padding + 16; // padding + iv
    uint8_t tempPrefs[p_length];
    memset(tempPrefs, 0, p_length);
    uint16_t outlen = 0;
    Prefs.isUpToDate = true;
    int8_t ret = BootSecure::encrypt((const unsigned char*)&Prefs, sizeof(STRUCT_PrefsH), (char*)BOOTKEY, tempPrefs, &outlen, 32);
    if (ret < 0) { //negative return codes are errors
        SerialPrint("Failed to encrypt Prefs with error code: " + String(ret), true,5);
        p.end();
        return -1;
    }
    p.putBytes("Boot", tempPrefs, outlen);
    p.end();
    BootSecure::zeroize(tempPrefs, p_length);
    
    return 1;

}

//encrypt message and return the encrypted message with embedded iv
//return 0 if successful, -2 if key is invalid, -3 if encryption failed
int8_t BootSecure::encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output, uint16_t* outputlength, uint8_t keylength) {
    //key must be 16 bytes
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
    int ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, keylength * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, localinput, output + 16);
    mbedtls_aes_free(&aes);
    BootSecure::zeroize(localinput, paddedLen);
    if (ret != 0) return -3;
    return 1;
}

int8_t BootSecure::decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength, uint8_t keylength) {
    uint8_t iv[16];
    memcpy(iv, input, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key, keylength * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, datalength - 16, iv, input + 16, output);
    mbedtls_aes_free(&aes);
    if (ret != 0) return -3;
    return 1;
}

// --- AES-CBC with provided IV (no IV prepended/extracted) ---
int8_t BootSecure::encryptWithIV(const unsigned char* input, uint16_t inputlength, char* key, uint8_t* iv, unsigned char* output, uint16_t* outputlength, uint8_t keylength) {
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
    int ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, keylength * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16); // mbedtls_aes_crypt_cbc updates IV
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, localinput, output);
    mbedtls_aes_free(&aes);
    BootSecure::zeroize(localinput, paddedLen);
    if (ret != 0) return -3;
    return 1;
}

int8_t BootSecure::decryptWithIV(unsigned char* input, char* key, uint8_t* iv, unsigned char* output, uint16_t datalength, uint8_t keylength) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key, keylength * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -2;
    }
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, datalength, iv_copy, input, output);
    mbedtls_aes_free(&aes);
    if (ret != 0) return -3;
    return 1;
}

void BootSecure::zeroize(void* buf, size_t len) {
    volatile uint8_t* p = (volatile uint8_t*)buf;
    while (len--) *p++ = 0;
}

void initCreds(STRUCT_PrefsH *w) {
    for (byte j=0;j<33;j++) w->WIFISSID[j]=0;
    for (byte j=0;j<65;j++) w->WIFIPWD[j]=0;
}


/*
// --- Secure WiFi Credentials Storage ---
bool putWiFiCredentials() {
    bool isGood = false;
    uint16_t sCRC = BootSecure::CRCCalculator((uint8_t*) Prefs.WIFISSID, 32);
    uint16_t pCRC = BootSecure::CRCCalculator((uint8_t*) Prefs.WIFIPWD, 64);
    Preferences p;
    p.begin("credentials", false);
    p.clear();

    // If SSID is blank, delete credentials
    if (Prefs.WIFISSID[0] == '\0' || sCRC == 0) {
        initCreds(&Prefs);
        Prefs.isUpToDate = false;
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
    snprintf(key, sizeof(key), "%s%08llX", BOOTKEY, mac & 0xFFFFFFFFFFFF); //if bootkey is short, this will add extra bytes to the key. if too long, this will truncate the key.

    // Encrypt SSID
    uint8_t encryptedSSID[48] = {0};
    uint16_t ssidLen = 0;
    BootSecure::encrypt((const unsigned char*)Prefs.WIFISSID, 33, key, encryptedSSID, &ssidLen, 32);
    p.putBytes("SSID", encryptedSSID, ssidLen);
    p.putUInt("SSIDCRC", sCRC);
    BootSecure::zeroize(encryptedSSID, sizeof(encryptedSSID));

    // Encrypt PWD
    uint8_t encryptedPWD[80] = {0};
    uint16_t pwdLen = 0;
    BootSecure::encrypt((const unsigned char*)Prefs.WIFIPWD, 65, key, encryptedPWD, &pwdLen, 32);
    p.putBytes("PWD", encryptedPWD, pwdLen);
    p.putUInt("PWDCRC", pCRC);
    BootSecure::zeroize(encryptedPWD, sizeof(encryptedPWD));

    BootSecure::zeroize(key, sizeof(key));
    p.end();
    return isGood;
}

bool getWiFiCredentials() {
    initCreds(&Prefs);
    Preferences p;
    p.begin("credentials", true);
    uint16_t sCRC = p.getUInt("SSIDCRC", 0);
    uint16_t pCRC = p.getUInt("PWDCRC", 0);
    Prefs.HAVECREDENTIALS = false;
    Prefs.isUpToDate = false;
    if (sCRC == 0 && pCRC == 0) {
        p.end();
        return false;
    }
    // Use device-unique key
    uint64_t mac = ESP.getEfuseMac();
    char key[33];
    snprintf(key, sizeof(key), "%s%08llX", BOOTKEY, mac & 0xFFFFFFFFFFFF);

    // Decrypt SSID
    uint8_t encryptedSSID[48] = {0};
    size_t ssidLen = p.getBytesLength("SSID");
    if (ssidLen > 0 && ssidLen <= sizeof(encryptedSSID)) {
        p.getBytes("SSID", encryptedSSID, ssidLen);
        BootSecure::decrypt(encryptedSSID, key, (unsigned char*)Prefs.WIFISSID, ssidLen, 32);
    }
    BootSecure::zeroize(encryptedSSID, sizeof(encryptedSSID));

    // Decrypt PWD
    uint8_t encryptedPWD[80] = {0};
    size_t pwdLen = p.getBytesLength("PWD");
    if (pwdLen > 0 && pwdLen <= sizeof(encryptedPWD)) {
        p.getBytes("PWD", encryptedPWD, pwdLen);
        BootSecure::decrypt(encryptedPWD, key, (unsigned char*)Prefs.WIFIPWD, pwdLen, 32);
    }
    BootSecure::zeroize(encryptedPWD, sizeof(encryptedPWD));
    BootSecure::zeroize(key, sizeof(key));
    p.end();
    // CRC check
    if (sCRC != BootSecure::CRCCalculator((uint8_t*)Prefs.WIFISSID, 32)) return false;
    if (pCRC != BootSecure::CRCCalculator((uint8_t*)Prefs.WIFIPWD, 64)) return false;
    Prefs.HAVECREDENTIALS = true;
    Prefs.isUpToDate = false;
    return true;
}
  
*/