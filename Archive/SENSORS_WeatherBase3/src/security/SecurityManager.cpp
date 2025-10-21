#include "SecurityManager.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <esp_random.h>
#include <esp_system.h>

namespace WeatherStation {

// Static member initialization
SecurityManager* SecurityManager::instance_ = nullptr;
std::mutex SecurityManager::mutex_;

SecurityManager::SecurityManager() 
    : initialized_(false)
    , encryption_enabled_(false)
    , auth_enabled_(false)
    , key_rotation_interval_(3600) // 1 hour default
    , last_key_rotation_(0) {
    
    // Initialize mbedTLS structures
    mbedtls_aes_init(&aes_context_);
    mbedtls_sha256_init(&sha256_context_);
    mbedtls_entropy_init(&entropy_context_);
    mbedtls_ctr_drbg_init(&ctr_drbg_context_);
    
    // Clear sensitive data
    memset(encryption_key_, 0, sizeof(encryption_key_));
    memset(auth_key_, 0, sizeof(auth_key_));
    memset(session_token_, 0, sizeof(session_token_));
}

SecurityManager::~SecurityManager() {
    // Clean up mbedTLS structures
    mbedtls_aes_free(&aes_context_);
    mbedtls_sha256_free(&sha256_context_);
    mbedtls_entropy_free(&entropy_context_);
    mbedtls_ctr_drbg_free(&ctr_drbg_context_);
    
    // Clear sensitive data from memory
    secure_wipe(encryption_key_, sizeof(encryption_key_));
    secure_wipe(auth_key_, sizeof(auth_key_));
    secure_wipe(session_token_, sizeof(session_token_));
}

SecurityManager* SecurityManager::getInstance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_ == nullptr) {
        instance_ = new SecurityManager();
    }
    return instance_;
}

void SecurityManager::destroyInstance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_ != nullptr) {
        delete instance_;
        instance_ = nullptr;
    }
}

ErrorCode SecurityManager::initialize() {
    if (initialized_) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        // Initialize entropy source
        int ret = mbedtls_entropy_add_source(&entropy_context_, 
                                           mbedtls_platform_entropy_poll, 
                                           nullptr, 
                                           32, 
                                           MBEDTLS_ENTROPY_SOURCE_STRONG);
        if (ret != 0) {
            return error_handler.logError(ErrorCode::SECURITY_INIT_FAILED, 
                                        "Failed to add entropy source: " + std::to_string(ret));
        }
        
        // Seed the random number generator
        ret = mbedtls_ctr_drbg_seed(&ctr_drbg_context_, 
                                   mbedtls_entropy_func, 
                                   &entropy_context_, 
                                   nullptr, 
                                   0);
        if (ret != 0) {
            return error_handler.logError(ErrorCode::SECURITY_INIT_FAILED, 
                                        "Failed to seed RNG: " + std::to_string(ret));
        }
        
        // Generate initial encryption key
        if (!generateEncryptionKey()) {
            return error_handler.logError(ErrorCode::SECURITY_INIT_FAILED, 
                                        "Failed to generate encryption key");
        }
        
        // Generate authentication key
        if (!generateAuthKey()) {
            return error_handler.logError(ErrorCode::SECURITY_INIT_FAILED, 
                                        "Failed to generate auth key");
        }
        
        // Generate session token
        if (!generateSessionToken()) {
            return error_handler.logError(ErrorCode::SECURITY_INIT_FAILED, 
                                        "Failed to generate session token");
        }
        
        initialized_ = true;
        encryption_enabled_ = true;
        auth_enabled_ = true;
        
        error_handler.logInfo("SecurityManager initialized successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::SECURITY_INIT_FAILED, 
                                    "Security initialization exception: " + std::string(e.what()));
    }
}

void SecurityManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Clear all sensitive data
    secure_wipe(encryption_key_, sizeof(encryption_key_));
    secure_wipe(auth_key_, sizeof(auth_key_));
    secure_wipe(session_token_, sizeof(session_token_));
    
    // Reset state
    initialized_ = false;
    encryption_enabled_ = false;
    auth_enabled_ = false;
    
    ErrorHandler::getInstance().logInfo("SecurityManager shutdown complete");
}

bool SecurityManager::generateEncryptionKey() {
    if (!initialized_) {
        return false;
    }
    
    // Generate 256-bit (32-byte) encryption key
    int ret = mbedtls_ctr_drbg_random(&ctr_drbg_context_, 
                                     encryption_key_, 
                                     sizeof(encryption_key_));
    if (ret != 0) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_KEY_GENERATION_FAILED, 
                                           "Failed to generate encryption key");
        return false;
    }
    
    // Set up AES context with the new key
    ret = mbedtls_aes_setkey_enc(&aes_context_, encryption_key_, 256);
    if (ret != 0) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_KEY_GENERATION_FAILED, 
                                           "Failed to set AES key");
        return false;
    }
    
    last_key_rotation_ = esp_timer_get_time() / 1000000; // Convert to seconds
    return true;
}

bool SecurityManager::generateAuthKey() {
    if (!initialized_) {
        return false;
    }
    
    // Generate 256-bit authentication key
    int ret = mbedtls_ctr_drbg_random(&ctr_drbg_context_, 
                                     auth_key_, 
                                     sizeof(auth_key_));
    if (ret != 0) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_KEY_GENERATION_FAILED, 
                                           "Failed to generate auth key");
        return false;
    }
    
    return true;
}

bool SecurityManager::generateSessionToken() {
    if (!initialized_) {
        return false;
    }
    
    // Generate 128-bit session token
    int ret = mbedtls_ctr_drbg_random(&ctr_drbg_context_, 
                                     session_token_, 
                                     sizeof(session_token_));
    if (ret != 0) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_KEY_GENERATION_FAILED, 
                                           "Failed to generate session token");
        return false;
    }
    
    return true;
}

std::string SecurityManager::encryptData(const std::string& plaintext) {
    if (!initialized_ || !encryption_enabled_) {
        return plaintext; // Return unencrypted if security is disabled
    }
    
    if (plaintext.empty()) {
        return "";
    }
    
    try {
        // Calculate required buffer size (plaintext + padding + IV)
        size_t padded_size = ((plaintext.length() + 15) / 16) * 16; // AES block size alignment
        size_t total_size = padded_size + 16; // +16 for IV
        
        std::vector<unsigned char> buffer(total_size);
        
        // Generate random IV
        int ret = mbedtls_ctr_drbg_random(&ctr_drbg_context_, 
                                         buffer.data(), 
                                         16);
        if (ret != 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_ENCRYPTION_FAILED, 
                                               "Failed to generate IV");
            return "";
        }
        
        // Copy plaintext and pad
        memcpy(buffer.data() + 16, plaintext.c_str(), plaintext.length());
        
        // Pad the remaining bytes
        size_t padding = padded_size - plaintext.length();
        for (size_t i = 0; i < padding; i++) {
            buffer[16 + plaintext.length() + i] = padding;
        }
        
        // Encrypt in CBC mode
        unsigned char iv[16];
        memcpy(iv, buffer.data(), 16);
        
        ret = mbedtls_aes_crypt_cbc(&aes_context_, 
                                   MBEDTLS_AES_ENCRYPT, 
                                   padded_size, 
                                   iv, 
                                   buffer.data() + 16, 
                                   buffer.data() + 16);
        if (ret != 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_ENCRYPTION_FAILED, 
                                               "AES encryption failed");
            return "";
        }
        
        // Convert to base64
        return StringUtils::base64Encode(buffer.data(), buffer.size());
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_ENCRYPTION_FAILED, 
                                           "Encryption exception: " + std::string(e.what()));
        return "";
    }
}

std::string SecurityManager::decryptData(const std::string& ciphertext) {
    if (!initialized_ || !encryption_enabled_) {
        return ciphertext; // Return as-is if security is disabled
    }
    
    if (ciphertext.empty()) {
        return "";
    }
    
    try {
        // Decode from base64
        std::vector<unsigned char> buffer = StringUtils::base64Decode(ciphertext);
        if (buffer.size() < 32) { // Minimum size: IV(16) + 1 block(16)
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_DECRYPTION_FAILED, 
                                               "Invalid ciphertext size");
            return "";
        }
        
        // Extract IV
        unsigned char iv[16];
        memcpy(iv, buffer.data(), 16);
        
        // Decrypt
        size_t data_size = buffer.size() - 16;
        int ret = mbedtls_aes_crypt_cbc(&aes_context_, 
                                       MBEDTLS_AES_DECRYPT, 
                                       data_size, 
                                       iv, 
                                       buffer.data() + 16, 
                                       buffer.data() + 16);
        if (ret != 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_DECRYPTION_FAILED, 
                                               "AES decryption failed");
            return "";
        }
        
        // Remove padding
        unsigned char padding = buffer[buffer.size() - 1];
        if (padding > 16 || padding == 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_DECRYPTION_FAILED, 
                                               "Invalid padding");
            return "";
        }
        
        size_t plaintext_size = data_size - padding;
        return std::string(reinterpret_cast<char*>(buffer.data() + 16), plaintext_size);
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_DECRYPTION_FAILED, 
                                           "Decryption exception: " + std::string(e.what()));
        return "";
    }
}

std::string SecurityManager::generateHash(const std::string& data) {
    if (!initialized_) {
        return "";
    }
    
    try {
        unsigned char hash[32]; // SHA-256 produces 32 bytes
        
        int ret = mbedtls_sha256_starts(&sha256_context_, 0); // 0 = SHA-256
        if (ret != 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_HASH_FAILED, 
                                               "Failed to start SHA-256");
            return "";
        }
        
        ret = mbedtls_sha256_update(&sha256_context_, 
                                   reinterpret_cast<const unsigned char*>(data.c_str()), 
                                   data.length());
        if (ret != 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_HASH_FAILED, 
                                               "Failed to update SHA-256");
            return "";
        }
        
        ret = mbedtls_sha256_finish(&sha256_context_, hash);
        if (ret != 0) {
            ErrorHandler::getInstance().logError(ErrorCode::SECURITY_HASH_FAILED, 
                                               "Failed to finish SHA-256");
            return "";
        }
        
        return StringUtils::bytesToHex(hash, 32);
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_HASH_FAILED, 
                                           "Hash generation exception: " + std::string(e.what()));
        return "";
    }
}

std::string SecurityManager::generateHMAC(const std::string& data) {
    if (!initialized_) {
        return "";
    }
    
    try {
        // Simple HMAC-SHA256 implementation
        std::string key_str(reinterpret_cast<const char*>(auth_key_), sizeof(auth_key_));
        
        // Outer padding
        std::string outer_pad = key_str;
        for (size_t i = 0; i < outer_pad.length(); i++) {
            outer_pad[i] ^= 0x5c;
        }
        
        // Inner padding
        std::string inner_pad = key_str;
        for (size_t i = 0; i < inner_pad.length(); i++) {
            inner_pad[i] ^= 0x36;
        }
        
        // Inner hash
        std::string inner_data = inner_pad + data;
        std::string inner_hash = generateHash(inner_data);
        
        // Outer hash
        std::string outer_data = outer_pad + StringUtils::hexToBytes(inner_hash);
        return generateHash(outer_data);
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_HMAC_FAILED, 
                                           "HMAC generation exception: " + std::string(e.what()));
        return "";
    }
}

bool SecurityManager::verifyHMAC(const std::string& data, const std::string& expected_hmac) {
    std::string calculated_hmac = generateHMAC(data);
    return calculated_hmac == expected_hmac;
}

std::string SecurityManager::generateRandomString(size_t length) {
    if (!initialized_) {
        return "";
    }
    
    try {
        const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::string result;
        result.reserve(length);
        
        for (size_t i = 0; i < length; i++) {
            unsigned char random_byte;
            int ret = mbedtls_ctr_drbg_random(&ctr_drbg_context_, &random_byte, 1);
            if (ret != 0) {
                ErrorHandler::getInstance().logError(ErrorCode::SECURITY_RANDOM_FAILED, 
                                                   "Failed to generate random byte");
                return "";
            }
            result += charset[random_byte % (sizeof(charset) - 1)];
        }
        
        return result;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::SECURITY_RANDOM_FAILED, 
                                           "Random string generation exception: " + std::string(e.what()));
        return "";
    }
}

std::string SecurityManager::getSessionToken() const {
    if (!initialized_) {
        return "";
    }
    return StringUtils::bytesToHex(session_token_, sizeof(session_token_));
}

bool SecurityManager::validateSessionToken(const std::string& token) const {
    if (!initialized_ || token.empty()) {
        return false;
    }
    
    std::string current_token = StringUtils::bytesToHex(session_token_, sizeof(session_token_));
    return token == current_token;
}

void SecurityManager::rotateKeys() {
    if (!initialized_) {
        return;
    }
    
    uint64_t current_time = esp_timer_get_time() / 1000000; // Convert to seconds
    
    if (current_time - last_key_rotation_ >= key_rotation_interval_) {
        ErrorHandler::getInstance().logInfo("Rotating security keys");
        
        generateEncryptionKey();
        generateAuthKey();
        generateSessionToken();
        
        last_key_rotation_ = current_time;
    }
}

void SecurityManager::secure_wipe(void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    
    // Overwrite with random data
    volatile unsigned char* ptr = static_cast<volatile unsigned char*>(data);
    for (size_t i = 0; i < size; i++) {
        ptr[i] = esp_random() & 0xFF;
    }
    
    // Overwrite with zeros
    memset(data, 0, size);
    
    // Ensure compiler doesn't optimize away the writes
    volatile unsigned char dummy = 0;
    for (size_t i = 0; i < size; i++) {
        dummy ^= ptr[i];
    }
    (void)dummy; // Suppress unused variable warning
}

SecurityConfig SecurityManager::getConfig() const {
    return {
        .encryption_enabled = encryption_enabled_,
        .auth_enabled = auth_enabled_,
        .key_rotation_interval = key_rotation_interval_,
        .last_key_rotation = last_key_rotation_
    };
}

void SecurityManager::setConfig(const SecurityConfig& config) {
    encryption_enabled_ = config.encryption_enabled;
    auth_enabled_ = config.auth_enabled;
    key_rotation_interval_ = config.key_rotation_interval;
    
    ErrorHandler::getInstance().logInfo("Security configuration updated");
}

} // namespace WeatherStation 