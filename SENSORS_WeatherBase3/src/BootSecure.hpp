#ifndef BootSecure_HPP
    #define BootSecure_HPP


    #include <Preferences.h>
    #include <esp_now.h>
    #include "globals.hpp"
    #include "utility.hpp"


    extern WiFi_type WIFI_INFO;
    extern Screen I;

    bool initESPNOW();
    esp_err_t addESPNOWPeer(byte* macad,bool doEncrypt);
    esp_err_t delESPNOWPeer(byte* macad);

    bool sendESPNOW(byte* MAC = nullptr, struct WiFi_type *w = nullptr);
    void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);

    bool getWiFiCredentials();
    bool putWiFiCredentials();
    void initCreds(struct WiFi_type *w);
    uint16_t CRCCalculator(uint8_t * data, uint16_t length);

    //#define _USEENCRYPTION

    #ifdef _USEENCRYPTION
        #include <mbedtls/aes.h>
        #include <mbedtls/cipher.h>
        void encrypt(char* input, char* key, unsigned char* output, unsigned char* iv);
        void decrypt(unsigned char* input, char* key, unsigned char* output, unsigned char* iv);
    #endif

#endif