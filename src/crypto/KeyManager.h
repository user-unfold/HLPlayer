#ifndef HLPLAYER_KEY_MANAGER_H
#define HLPLAYER_KEY_MANAGER_H

#include <cstdint>
#include <string>

#include "CryptoExport.h"
#include "SecureAllocator.h"

// Forward declare for Result<T> usage
namespace hlplayer {
template<typename T> class Result;
enum class PlayerError : int32_t;
} // namespace hlplayer

namespace hlplayer::crypto {

/// Derived AES and HMAC keys from a single source (password or raw key).
struct DerivedKeys {
    SecureBytes aesKey;   // 32 bytes
    SecureBytes hmacKey;  // 32 bytes
};

/// Key management for encrypted video: derivation, generation, formatting.
///
/// Supports two key modes:
/// - Password-based: PBKDF2-HMAC-SHA256 with salt and iterations
/// - Raw key-based: HKDF-SHA256 (RFC 5869) extract-and-expand
class HLPLAYER_CRYPTO_API KeyManager {
public:
    /// Derive AES + HMAC keys from a password using PBKDF2-HMAC-SHA256.
    /// Derives 64 bytes: first 32 → aesKey, second 32 → hmacKey.
    /// Iterations are clamped to HLV_MAX_ITERATIONS (2000000).
    static DerivedKeys deriveFromPassword(const std::string& password,
                                           const uint8_t salt[16],
                                           uint32_t iterations);

    /// Derive AES + HMAC keys from a raw key using HKDF-SHA256.
    /// Extract: PRK = HMAC-SHA256(salt="", IKM=rawKey)
    /// Expand aesKey: HMAC-SHA256(PRK, "HLPlayer-v1-enc" || 0x01)
    /// Expand hmacKey: HMAC-SHA256(PRK, "HLPlayer-v1-mac" || 0x01)
    static DerivedKeys deriveFromRawKey(const SecureBytes& rawKey);

    /// Generate a cryptographically random 32-byte key.
    /// Uses av_get_random_seed() (32-bit) called 8 times.
    static SecureBytes generateRawKey();

    /// Format a 32-byte key as a human-readable base32 string.
    /// Format: "XXXX-XXXX-...-XXXX" (13 groups of 4 chars, no padding).
    /// Base32 alphabet: A-Z 2-7 (RFC 4648).
    static std::string formatKeyString(const SecureBytes& key);

    /// Parse a base32 key string back into a 32-byte key.
    /// Returns error if format is invalid (wrong length, invalid chars).
    static ::hlplayer::Result<SecureBytes> parseKeyString(const std::string& str);
};

} // namespace hlplayer::crypto

#endif // HLPLAYER_KEY_MANAGER_H
