#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "CryptoExport.h"

namespace hlplayer::crypto {

// Key derivation modes
enum class KeyMode : uint8_t {
    Password = 0x01,
    RawKey   = 0x02,
};

// Encryption algorithms
enum class Algorithm : uint8_t {
    AES256CTR = 0x01,
};

// Header constants
static constexpr size_t HLV_HEADER_SIZE = 112;
static constexpr uint16_t HLV_VERSION = 1;
static constexpr uint32_t HLV_MAX_ITERATIONS = 2000000;
static constexpr uint32_t HLV_DEFAULT_ITERATIONS = 600000;

// Magic bytes
static constexpr uint8_t HLV_MAGIC[8] = {'H', 'L', 'P', 'E', 'N', 'C', '\0', '\0'};

// Salt and nonce sizes
static constexpr size_t HLV_SALT_SIZE = 16;
static constexpr size_t HLV_NONCE_SIZE = 12;
static constexpr size_t HLV_HMAC_SIZE = 32;
static constexpr size_t HLV_ORIG_EXT_SIZE = 8;
static constexpr size_t HLV_RESERVED_SIZE = 16;

struct HLPLAYER_CRYPTO_API HlvHeader {
    uint16_t version;
    KeyMode keyMode;
    Algorithm algorithm;
    uint32_t flags;
    uint8_t salt[HLV_SALT_SIZE];
    uint8_t nonce[HLV_NONCE_SIZE];
    uint32_t pbkdf2Iterations;
    uint64_t originalSize;
    char originalExt[HLV_ORIG_EXT_SIZE];
    uint8_t reserved[HLV_RESERVED_SIZE];
    uint8_t headerHmac[HLV_HMAC_SIZE];

    // Serialize to 112-byte little-endian array
    std::vector<uint8_t> serialize() const;

    // Deserialize from byte array (must be exactly 112 bytes)
    static HlvHeader deserialize(const uint8_t* data, size_t len);

    // Validate magic bytes (magic is not stored in the struct, it's implicit)
    // NOTE: This requires the serialized form to validate; the struct itself
    // doesn't store the magic bytes since they're implicit in the file format
    bool validateMagic(const uint8_t* serializedData) const;

    // Return clamped iteration count (min(iterations, HLV_MAX_ITERATIONS))
    uint32_t getClampedIterations() const;

    // Check if this is a valid header with expected defaults
    bool isValid() const;
};

// File-level helpers

// Check if a file starts with the HLV magic bytes
// Opens file, reads first 8 bytes, compares with HLV_MAGIC
// Returns true if file is an encrypted .hlv file
HLPLAYER_CRYPTO_API bool isHlvFile(const std::string& filePath);

// Check if a path has .hlv extension (case-insensitive)
HLPLAYER_CRYPTO_API bool hasHlvExtension(const std::string& filePath);

} // namespace hlplayer::crypto