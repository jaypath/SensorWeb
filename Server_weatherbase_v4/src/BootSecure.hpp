#ifndef BootSecure_HPP
    #define BootSecure_HPP

    //#define _USEENCRYPTION


    #include <Preferences.h>
    #include "globals.hpp"
    #include "utility.hpp"
    #include "server.hpp"
    #include <mbedtls/aes.h>
    #include <mbedtls/cipher.h>
    #include "esp_random.h"


    #define BOOTKEY "YfMVDR2qtzxJdD9yNhN6IDGPwgpyMjk2" //must be 128, 192, or 256 bits long - used for decrypting/encrypting boot parameters
        
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


    


    extern STRUCT_PrefsH Prefs;

    
    uint16_t CRCCalculator(uint8_t * data, uint16_t length);

    int8_t decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength, uint8_t keylength=16);

    int8_t encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output,  uint16_t* outputlength, uint8_t keylength=16);
    //bool getWiFiCredentials();
    //bool putWiFiCredentials();
    void initCreds(struct STRUCT_PrefsH *w);
    int8_t setPrefs(bool forceUpdate = false); // Standalone function to save Prefs to encrypted storage
    
class BootSecure {
public:
    BootSecure();
    int8_t setup(); // Call once at boot. Returns 1 if secure, 0 if not, -1 if error no prefs found, -2 if PROCID mismatch
    bool isSecure() const { return secure; }
    int8_t setPrefs(bool forceUpdate = false); // Save Prefs to encrypted storage
    int8_t flushPrefs(void);
    // Optionally expose CRC and encryption helpers if needed elsewhere
    static uint16_t CRCCalculator(uint8_t * data, uint16_t length);
    static int8_t encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output,  uint16_t* outputlength, uint8_t keylength=16);
    static int8_t decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength, uint8_t keylength=16);
    // --- New: AES-CBC with provided IV (does not prepend IV to output) ---
    static int8_t encryptWithIV(const unsigned char* input, uint16_t inputlength, char* key, uint8_t* iv, unsigned char* output, uint16_t* outputlength, uint8_t keylength=16);
    static int8_t decryptWithIV(unsigned char* input, char* key, uint8_t* iv, unsigned char* output, uint16_t datalength, uint8_t keylength=16);
    static void zeroize(void* buf, size_t len);

private:
    bool secure = false;
    int8_t getPrefs();
    bool checkDeviceID();

};

#endif