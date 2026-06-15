#ifndef HLPLAYER_SESSION_KEY_CACHE_H
#define HLPLAYER_SESSION_KEY_CACHE_H

#include "CryptoExport.h"
#include "SecureAllocator.h"
#include <cstdint>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace hlplayer::crypto {

class HLPLAYER_CRYPTO_API SessionKeyCache {
public:
    SessionKeyCache();
    ~SessionKeyCache();

    SessionKeyCache(const SessionKeyCache&) = delete;
    SessionKeyCache& operator=(const SessionKeyCache&) = delete;

    // Try to find a matching key for the given header without prompting user.
    // Extracts header bytes 0x00-0x4F (80 bytes), computes HMAC-SHA256 with each
    // cached hmac_key, constant-time compares with headerHmac.
    // Returns aesKey on match, nullopt on no match.
    std::optional<SecureBytes> tryFindKey(const uint8_t* headerBytes80,
                                          const uint8_t* headerHmac32);

    // Store a key after user successfully enters it.
    void put(const std::string& filePath,
             const SecureBytes& aesKey,
             const SecureBytes& hmacKey);

    // Clear all cached keys (SecureZeroMemory all key material before deallocation).
    void clear();

private:
    struct Entry {
        std::string filePath;
        SecureBytes aesKey;
        SecureBytes hmacKey;
        std::chrono::steady_clock::time_point lastUsed;
    };

    std::vector<Entry> cache_;
    mutable std::mutex mutex_;
    static constexpr size_t MAX_ENTRIES = 8;

    void evictIfNeeded();
};

} // namespace hlplayer::crypto

#endif // HLPLAYER_SESSION_KEY_CACHE_H