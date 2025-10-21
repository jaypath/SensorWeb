/**
 * @file StringUtils.hpp
 * @brief Comprehensive string manipulation utilities for SENSORS_WeatherBase3
 * @author System Refactoring Team
 * @date 2024
 * 
 * This module provides centralized string utilities to eliminate code duplication
 * and provide consistent string handling across the entire codebase.
 */

#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <Arduino.h>
#include <vector>

/**
 * @brief Comprehensive string utilities class
 * 
 * This class provides common string operations used throughout the system:
 * - String parsing and tokenization
 * - Formatting and conversion
 * - Validation and sanitization
 * - Memory-efficient operations
 */
class StringUtils {
public:
    /**
     * @brief Split a string by delimiter
     * @param input Input string to split
     * @param delimiter Delimiter character
     * @return Vector of substrings
     */
    static std::vector<String> split(const String& input, char delimiter);
    
    /**
     * @brief Split a string by delimiter with maximum count
     * @param input Input string to split
     * @param delimiter Delimiter character
     * @param maxCount Maximum number of splits
     * @return Vector of substrings
     */
    static std::vector<String> split(const String& input, char delimiter, size_t maxCount);
    
    /**
     * @brief Split a string by delimiter string
     * @param input Input string to split
     * @param delimiter Delimiter string
     * @return Vector of substrings
     */
    static std::vector<String> split(const String& input, const String& delimiter);
    
    /**
     * @brief Join vector of strings with delimiter
     * @param strings Vector of strings to join
     * @param delimiter Delimiter string
     * @return Joined string
     */
    static String join(const std::vector<String>& strings, const String& delimiter);
    
    /**
     * @brief Trim whitespace from both ends of string
     * @param input Input string
     * @return Trimmed string
     */
    static String trim(const String& input);
    
    /**
     * @brief Trim whitespace from left side of string
     * @param input Input string
     * @return Left-trimmed string
     */
    static String trimLeft(const String& input);
    
    /**
     * @brief Trim whitespace from right side of string
     * @param input Input string
     * @return Right-trimmed string
     */
    static String trimRight(const String& input);
    
    /**
     * @brief Convert string to lowercase
     * @param input Input string
     * @return Lowercase string
     */
    static String toLowerCase(const String& input);
    
    /**
     * @brief Convert string to uppercase
     * @param input Input string
     * @return Uppercase string
     */
    static String toUpperCase(const String& input);
    
    /**
     * @brief Check if string starts with prefix
     * @param input Input string
     * @param prefix Prefix to check
     * @param caseSensitive Whether to perform case-sensitive comparison
     * @return true if string starts with prefix, false otherwise
     */
    static bool startsWith(const String& input, const String& prefix, bool caseSensitive = true);
    
    /**
     * @brief Check if string ends with suffix
     * @param input Input string
     * @param suffix Suffix to check
     * @param caseSensitive Whether to perform case-sensitive comparison
     * @return true if string ends with suffix, false otherwise
     */
    static bool endsWith(const String& input, const String& suffix, bool caseSensitive = true);
    
    /**
     * @brief Check if string contains substring
     * @param input Input string
     * @param substring Substring to search for
     * @param caseSensitive Whether to perform case-sensitive comparison
     * @return true if string contains substring, false otherwise
     */
    static bool contains(const String& input, const String& substring, bool caseSensitive = true);
    
    /**
     * @brief Replace all occurrences of substring
     * @param input Input string
     * @param search Substring to search for
     * @param replace Substring to replace with
     * @return String with replacements
     */
    static String replace(const String& input, const String& search, const String& replace);
    
    /**
     * @brief Replace first occurrence of substring
     * @param input Input string
     * @param search Substring to search for
     * @param replace Substring to replace with
     * @return String with replacement
     */
    static String replaceFirst(const String& input, const String& search, const String& replace);
    
    /**
     * @brief Pad string to specified length
     * @param input Input string
     * @param length Target length
     * @param padChar Character to pad with
     * @param padLeft Whether to pad on left (true) or right (false)
     * @return Padded string
     */
    static String pad(const String& input, size_t length, char padChar = ' ', bool padLeft = true);
    
    /**
     * @brief Format string with printf-style formatting
     * @param format Format string
     * @param ... Variable arguments
     * @return Formatted string
     */
    static String format(const char* format, ...);
    
    /**
     * @brief Convert integer to string with base
     * @param value Integer value
     * @param base Base for conversion (2, 8, 10, 16)
     * @return String representation
     */
    static String toString(int value, int base = 10);
    
    /**
     * @brief Convert unsigned integer to string with base
     * @param value Unsigned integer value
     * @param base Base for conversion (2, 8, 10, 16)
     * @return String representation
     */
    static String toString(unsigned int value, int base = 10);
    
    /**
     * @brief Convert long integer to string with base
     * @param value Long integer value
     * @param base Base for conversion (2, 8, 10, 16)
     * @return String representation
     */
    static String toString(long value, int base = 10);
    
    /**
     * @brief Convert unsigned long integer to string with base
     * @param value Unsigned long integer value
     * @param base Base for conversion (2, 8, 10, 16)
     * @return String representation
     */
    static String toString(unsigned long value, int base = 10);
    
    /**
     * @brief Convert float to string with precision
     * @param value Float value
     * @param precision Number of decimal places
     * @return String representation
     */
    static String toString(float value, int precision = 2);
    
    /**
     * @brief Convert double to string with precision
     * @param value Double value
     * @param precision Number of decimal places
     * @return String representation
     */
    static String toString(double value, int precision = 2);
    
    /**
     * @brief Parse string to integer
     * @param input Input string
     * @param defaultValue Default value if parsing fails
     * @return Parsed integer value
     */
    static int toInt(const String& input, int defaultValue = 0);
    
    /**
     * @brief Parse string to unsigned integer
     * @param input Input string
     * @param defaultValue Default value if parsing fails
     * @return Parsed unsigned integer value
     */
    static unsigned int toUInt(const String& input, unsigned int defaultValue = 0);
    
    /**
     * @brief Parse string to long integer
     * @param input Input string
     * @param defaultValue Default value if parsing fails
     * @return Parsed long integer value
     */
    static long toLong(const String& input, long defaultValue = 0);
    
    /**
     * @brief Parse string to unsigned long integer
     * @param input Input string
     * @param defaultValue Default value if parsing fails
     * @return Parsed unsigned long integer value
     */
    static unsigned long toULong(const String& input, unsigned long defaultValue = 0);
    
    /**
     * @brief Parse string to float
     * @param input Input string
     * @param defaultValue Default value if parsing fails
     * @return Parsed float value
     */
    static float toFloat(const String& input, float defaultValue = 0.0f);
    
    /**
     * @brief Parse string to double
     * @param input Input string
     * @param defaultValue Default value if parsing fails
     * @return Parsed double value
     */
    static double toDouble(const String& input, double defaultValue = 0.0);
    
    /**
     * @brief Check if string is numeric
     * @param input Input string
     * @return true if string is numeric, false otherwise
     */
    static bool isNumeric(const String& input);
    
    /**
     * @brief Check if string is integer
     * @param input Input string
     * @return true if string is integer, false otherwise
     */
    static bool isInteger(const String& input);
    
    /**
     * @brief Check if string is hexadecimal
     * @param input Input string
     * @return true if string is hexadecimal, false otherwise
     */
    static bool isHexadecimal(const String& input);
    
    /**
     * @brief Escape special characters in string
     * @param input Input string
     * @return Escaped string
     */
    static String escape(const String& input);
    
    /**
     * @brief Unescape special characters in string
     * @param input Input string
     * @return Unescaped string
     */
    static String unescape(const String& input);
    
    /**
     * @brief Convert string to URL-encoded format
     * @param input Input string
     * @return URL-encoded string
     */
    static String urlEncode(const String& input);
    
    /**
     * @brief Convert URL-encoded string to normal format
     * @param input URL-encoded string
     * @return Decoded string
     */
    static String urlDecode(const String& input);
    
    /**
     * @brief Convert string to base64 encoding
     * @param input Input string
     * @return Base64 encoded string
     */
    static String toBase64(const String& input);
    
    /**
     * @brief Convert base64 string to normal format
     * @param input Base64 encoded string
     * @return Decoded string
     */
    static String fromBase64(const String& input);
    
    /**
     * @brief Generate random string
     * @param length Length of random string
     * @param charset Character set to use (default: alphanumeric)
     * @return Random string
     */
    static String random(size_t length, const String& charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    
    /**
     * @brief Generate UUID v4 string
     * @return UUID string
     */
    static String generateUUID();
    
    /**
     * @brief Check if string is valid UUID
     * @param input Input string
     * @return true if valid UUID, false otherwise
     */
    static bool isValidUUID(const String& input);
    
    /**
     * @brief Extract filename from path
     * @param path File path
     * @return Filename
     */
    static String getFilename(const String& path);
    
    /**
     * @brief Extract directory from path
     * @param path File path
     * @return Directory path
     */
    static String getDirectory(const String& path);
    
    /**
     * @brief Get file extension
     * @param path File path
     * @return File extension (without dot)
     */
    static String getExtension(const String& path);
    
    /**
     * @brief Normalize path separators
     * @param path File path
     * @return Normalized path
     */
    static String normalizePath(const String& path);
    
    /**
     * @brief Check if string matches wildcard pattern
     * @param input Input string
     * @param pattern Wildcard pattern (* and ? supported)
     * @param caseSensitive Whether to perform case-sensitive comparison
     * @return true if string matches pattern, false otherwise
     */
    static bool matchesPattern(const String& input, const String& pattern, bool caseSensitive = true);
    
    /**
     * @brief Count occurrences of substring
     * @param input Input string
     * @param substring Substring to count
     * @param caseSensitive Whether to perform case-sensitive comparison
     * @return Number of occurrences
     */
    static size_t countOccurrences(const String& input, const String& substring, bool caseSensitive = true);
    
    /**
     * @brief Remove all occurrences of substring
     * @param input Input string
     * @param substring Substring to remove
     * @return String with substrings removed
     */
    static String remove(const String& input, const String& substring);
    
    /**
     * @brief Remove all occurrences of character
     * @param input Input string
     * @param character Character to remove
     * @return String with characters removed
     */
    static String remove(const String& input, char character);
    
    /**
     * @brief Extract substring between two delimiters
     * @param input Input string
     * @param startDelim Start delimiter
     * @param endDelim End delimiter
     * @param startPos Starting position to search from
     * @return Extracted substring or empty string if not found
     */
    static String extractBetween(const String& input, const String& startDelim, const String& endDelim, size_t startPos = 0);
    
    /**
     * @brief Convert byte array to hex string
     * @param data Byte array
     * @param length Length of array
     * @param separator Separator between bytes (empty for no separator)
     * @return Hex string
     */
    static String bytesToHex(const uint8_t* data, size_t length, const String& separator = "");
    
    /**
     * @brief Convert hex string to byte array
     * @param hexString Hex string
     * @param output Output byte array
     * @param maxLength Maximum length of output array
     * @return Number of bytes written
     */
    static size_t hexToBytes(const String& hexString, uint8_t* output, size_t maxLength);
    
    /**
     * @brief Convert MAC address to string
     * @param mac MAC address as byte array
     * @param separator Separator between bytes (default: ":")
     * @return MAC address string
     */
    static String macToString(const uint8_t* mac, const String& separator = ":");
    
    /**
     * @brief Convert string to MAC address
     * @param macString MAC address string
     * @param mac Output MAC address as byte array
     * @return true if conversion successful, false otherwise
     */
    static bool stringToMac(const String& macString, uint8_t* mac);
    
    /**
     * @brief Convert IP address to string
     * @param ip IP address as uint32_t
     * @return IP address string
     */
    static String ipToString(uint32_t ip);
    
    /**
     * @brief Convert string to IP address
     * @param ipString IP address string
     * @return IP address as uint32_t (0 if invalid)
     */
    static uint32_t stringToIp(const String& ipString);

private:
    /**
     * @brief Check if character is whitespace
     * @param c Character to check
     * @return true if whitespace, false otherwise
     */
    static bool isWhitespace(char c);
    
    /**
     * @brief Check if character is hexadecimal digit
     * @param c Character to check
     * @return true if hexadecimal digit, false otherwise
     */
    static bool isHexDigit(char c);
    
    /**
     * @brief Convert hex digit to value
     * @param c Hex digit character
     * @return Numeric value (0-15)
     */
    static uint8_t hexDigitToValue(char c);
    
    /**
     * @brief Convert value to hex digit
     * @param value Numeric value (0-15)
     * @return Hex digit character
     */
    static char valueToHexDigit(uint8_t value);
};

// Convenience macros for common operations
#define STR_SPLIT(input, delimiter) StringUtils::split(input, delimiter)
#define STR_TRIM(input) StringUtils::trim(input)
#define STR_TO_LOWER(input) StringUtils::toLowerCase(input)
#define STR_TO_UPPER(input) StringUtils::toUpperCase(input)
#define STR_STARTS_WITH(input, prefix) StringUtils::startsWith(input, prefix)
#define STR_ENDS_WITH(input, suffix) StringUtils::endsWith(input, suffix)
#define STR_CONTAINS(input, substring) StringUtils::contains(input, substring)
#define STR_REPLACE(input, search, replace) StringUtils::replace(input, search, replace)
#define STR_TO_INT(input) StringUtils::toInt(input)
#define STR_TO_FLOAT(input) StringUtils::toFloat(input)
#define STR_IS_NUMERIC(input) StringUtils::isNumeric(input)

#endif // STRING_UTILS_HPP 