#ifndef BootSecure_HPP
    #define BootSecure_HPP

    //#define _USEENCRYPTION


    #include <Preferences.h>
    #include <esp_now.h>
    #include "globals.hpp"
    #include "utility.hpp"

    #ifdef _USEENCRYPTION
        #include <mbedtls/aes.h>
        #include <mbedtls/cipher.h>

        #define AESKEY "YfMVDR2qtzxJdD9yNhN6IDGPwgpyMjk2"
        
        /**
 * \brief  This function performs an AES-CBC encryption or decryption operation
 *         on full blocks.
 *
 *         It performs the operation defined in the \p mode
 *         parameter (encrypt/decrypt), on the input data buffer defined in
 *         the \p input parameter.
 *
 *         It can be called as many times as needed, until all the input
 *         data is processed. mbedtls_aes_init(), and either
 *         mbedtls_aes_setkey_enc() or mbedtls_aes_setkey_dec() must be called
 *         before the first call to this API with the same context.
 *
 * \note   This function operates on full blocks, that is, the input size
 *         must be a multiple of the AES block size of \c 16 Bytes.
 *
 * \note   Upon exit, the content of the IV is updated so that you can
 *         call the same function again on the next
 *         block(s) of data and get the same result as if it was
 *         encrypted in one call. This allows a "streaming" usage.
 *         If you need to retain the contents of the IV, you should
 *         either save it manually or use the cipher module instead.
 *
 *
 * \param ctx      The AES context to use for encryption or decryption.
 *                 It must be initialized and bound to a key.
 * \param mode     The AES operation: #MBEDTLS_AES_ENCRYPT or
 *                 #MBEDTLS_AES_DECRYPT.
 * \param length   The length of the input data in Bytes. This must be a
 *                 multiple of the block size (\c 16 Bytes).
 * \param iv       Initialization vector (updated after use).
 *                 It must be a readable and writeable buffer of \c 16 Bytes.
 * \param input    The buffer holding the input data.
 *                 It must be readable and of size \p length Bytes.
 * \param output   The buffer holding the output data.
 *                 It must be writeable and of size \p length Bytes.
 *
 * \return         \c 0 on success.
 * \return         #MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH
 *                 on failure.
 */
    #endif


    extern WiFi_type WIFI_INFO;
    extern Screen I;


    char PMK_KEY_STR[17] = "KiKa.yaas1anni!~"; //note this is not stored in prefs

    byte prefs_set = 0;

    struct STRUCT_PrefsH {
        char LMK_KEY_STR[17];
        byte LMK_isSet = 0;
        byte WIFISSID[33];
        byte WIFIPWD[65];
        uint32_t SSIDCRC;
        uint32_t PWDCRC;
        uint8_t PROCID[6];
    };


    STRUCT_PrefsH PrefsH;

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
        byte encrypt(char* input, char* key, unsigned char* output, uint16_t datalength);
        byte decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength);
    #endif

#endif