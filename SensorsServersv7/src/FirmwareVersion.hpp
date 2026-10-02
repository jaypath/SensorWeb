#ifndef FIRMWARE_VERSION_HPP
#define FIRMWARE_VERSION_HPP

#include <stdint.h>
#include <stddef.h>

// Firmware major.minor.patch — each element 0-255; compare byte 0, then 1, then 2.
struct FirmwareVersion {
    uint8_t v[3];

    void clear();
    bool isUnset() const;
    bool fromText(const char* text);
    void toChar(char* out, size_t outLen) const;
    void toBinPathSegment(char* out, size_t outLen) const;
    int compare(const uint8_t other[3]) const;
    int compare(const FirmwareVersion& other) const;
    static int compareBytes(const uint8_t a[3], const uint8_t b[3]);
};

#endif
