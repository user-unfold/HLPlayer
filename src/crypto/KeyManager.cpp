#include "KeyManager.h"

#include <algorithm>
#include <cstring>

#include "HmacSha256.h"
#include "HlvHeader.h"
#include "constant_time.h"
#include "pbkdf2.h"

#include <hlplayer/Result.h>

extern "C" {
#include <libavutil/random_seed.h>
}

namespace hlplayer::crypto {

// ---------------------------------------------------------------------------
// Base32 (RFC 4648) helpers — internal, no export attribute
// ---------------------------------------------------------------------------

static constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static bool base32CharToValue(char c, uint8_t& out) {
    if (c >= 'A' && c <= 'Z') { out = static_cast<uint8_t>(c - 'A'); return true; }
    if (c >= '2' && c <= '7') { out = static_cast<uint8_t>(c - '2' + 26); return true; }
    return false;
}

/// Encode 32 bytes → 52 base32 chars (no padding).
static std::string base32Encode(const uint8_t* data, size_t len) {
    // 32 bytes = 256 bits → ceil(256/5) = 52 chars
    std::string out;
    out.reserve(52);

    size_t bits = 0;
    uint32_t buffer = 0;

    for (size_t i = 0; i < len; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(kBase32Alphabet[(buffer >> bits) & 0x1F]);
        }
    }
    // Emit final partial group: remaining bits (1-4) left-aligned + zero padding
    if (bits > 0) {
        out.push_back(kBase32Alphabet[(buffer << (5 - bits)) & 0x1F]);
    }
    return out;
}

/// Decode base32 string (no padding) → bytes.
/// Returns true on success. outputLen is set to number of bytes written.
static bool base32Decode(const std::string& encoded, uint8_t* output, size_t outputCapacity, size_t& outputLen) {
    // Filter valid base32 chars
    size_t validChars = 0;
    for (char c : encoded) {
        uint8_t val;
        if (base32CharToValue(c, val)) {
            ++validChars;
        } else {
            return false; // invalid character
        }
    }

    if (validChars < 2) return false;

    size_t bits = 0;
    uint32_t buffer = 0;
    outputLen = 0;

    for (char c : encoded) {
        uint8_t val;
        if (!base32CharToValue(c, val)) continue;

        buffer = (buffer << 5) | val;
        bits += 5;

        while (bits >= 8) {
            bits -= 8;
            if (outputLen >= outputCapacity) return false;
            output[outputLen++] = static_cast<uint8_t>((buffer >> bits) & 0xFF);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// KeyManager implementation
// ---------------------------------------------------------------------------

DerivedKeys KeyManager::deriveFromPassword(const std::string& password,
                                           const uint8_t salt[16],
                                           uint32_t iterations) {
    uint32_t clamped = std::min(iterations, HLV_MAX_ITERATIONS);

    DerivedKeys keys;
    keys.aesKey.resize(32);
    keys.hmacKey.resize(32);

    // Derive 64 bytes: first 32 → aesKey, second 32 → hmacKey
    SecureBytes derived(64);
    pbkdf2_hmac_sha256(
        reinterpret_cast<const uint8_t*>(password.data()), password.size(),
        salt, 16,
        clamped,
        derived.data(), 64);

    std::memcpy(keys.aesKey.data(), derived.data(), 32);
    std::memcpy(keys.hmacKey.data(), derived.data() + 32, 32);

    return keys;
}

DerivedKeys KeyManager::deriveFromRawKey(const SecureBytes& rawKey) {
    DerivedKeys keys;

    // HKDF-SHA256 Extract: PRK = HMAC-SHA256(key=salt="", data=IKM=rawKey)
    uint8_t prk[32];
    HmacSha256::compute(nullptr, 0,
                         rawKey.data(), rawKey.size(),
                         prk);

    // HKDF-SHA256 Expand aesKey: OKM = HMAC-SHA256(PRK, "HLPlayer-v1-enc" || 0x01)
    keys.aesKey.resize(32);
    {
        static const char encInfo[] = "HLPlayer-v1-enc"; // 15 chars
        constexpr size_t infoLen = sizeof(encInfo) - 1;  // 15
        uint8_t info[16];
        std::memcpy(info, encInfo, infoLen);
        info[infoLen] = 0x01;
        HmacSha256::compute(prk, 32, info, infoLen + 1, keys.aesKey.data());
    }

    // HKDF-SHA256 Expand hmacKey: OKM = HMAC-SHA256(PRK, "HLPlayer-v1-mac" || 0x01)
    keys.hmacKey.resize(32);
    {
        static const char macInfo[] = "HLPlayer-v1-mac"; // 15 chars
        constexpr size_t infoLen = sizeof(macInfo) - 1;  // 15
        uint8_t info[16];
        std::memcpy(info, macInfo, infoLen);
        info[infoLen] = 0x01;
        HmacSha256::compute(prk, 32, info, infoLen + 1, keys.hmacKey.data());
    }

    return keys;
}

SecureBytes KeyManager::generateRawKey() {
    SecureBytes key(32);

    // Use av_get_random_seed() (returns 32 bits) called 8 times for 32 bytes
    for (int i = 0; i < 8; ++i) {
        uint32_t seed = av_get_random_seed();
        std::memcpy(key.data() + i * 4, &seed, 4);
    }

    // Ensure not all zeros (astronomically unlikely but defensive)
    bool allZero = true;
    for (size_t i = 0; i < 32; ++i) {
        if (key[i] != 0) { allZero = false; break; }
    }
    if (allZero) {
        // Fallback: fill with av_get_random_seed() one more time per byte
        for (size_t i = 0; i < 32; ++i) {
            key[i] = static_cast<uint8_t>(av_get_random_seed() & 0xFF);
            if (key[i] != 0) allZero = false;
            if (!allZero) break;
        }
    }

    return key;
}

std::string KeyManager::formatKeyString(const SecureBytes& key) {
    // Encode all 32 bytes as base32 (no padding) → 52 chars
    std::string b32 = base32Encode(key.data(), key.size());

    // Split into groups of 4 chars separated by '-': 52/4 = 13 groups
    std::string result;
    result.reserve(52 + 12); // 52 chars + 12 hyphens
    for (size_t i = 0; i < 52; ++i) {
        if (i > 0 && i % 4 == 0) {
            result.push_back('-');
        }
        result.push_back(b32[i]);
    }
    return result;
}

::hlplayer::Result<SecureBytes> KeyManager::parseKeyString(const std::string& str) {
    // Remove hyphens
    std::string cleaned;
    cleaned.reserve(str.size());
    for (char c : str) {
        if (c != '-') {
            cleaned.push_back(c);
        }
    }

    // Validate: must be exactly 52 base32 chars
    if (cleaned.size() != 52) {
        return ::hlplayer::Result<SecureBytes>::error(::hlplayer::PlayerError::InvalidState);
    }

    // Validate all characters are valid base32
    for (char c : cleaned) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'))) {
            return ::hlplayer::Result<SecureBytes>::error(::hlplayer::PlayerError::InvalidState);
        }
    }

    // Decode base32
    SecureBytes key(32);
    size_t decodedLen = 0;
    if (!base32Decode(cleaned, key.data(), 32, decodedLen) || decodedLen != 32) {
        return ::hlplayer::Result<SecureBytes>::error(::hlplayer::PlayerError::InvalidState);
    }

    return ::hlplayer::Result<SecureBytes>::success(std::move(key));
}

} // namespace hlplayer::crypto
