#include "SessionKeyCache.h"
#include "HmacSha256.h"
#include "constant_time.h"
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstring>
#endif

namespace hlplayer::crypto {

SessionKeyCache::SessionKeyCache() = default;

SessionKeyCache::~SessionKeyCache() {
    clear();
}

std::optional<SecureBytes> SessionKeyCache::tryFindKey(const uint8_t* headerBytes80,
                                                         const uint8_t* headerHmac32) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& entry : cache_) {
        // Compute HMAC-SHA256 of header with cached hmac_key
        uint8_t computedHmac[32];
        HmacSha256::compute(entry.hmacKey.data(), 32, headerBytes80, 80, computedHmac);

        // Constant-time compare
        if (constant_time_compare(computedHmac, headerHmac32, 32)) {
            // Update lastUsed timestamp
            entry.lastUsed = std::chrono::steady_clock::now();
            return entry.aesKey;
        }
    }

    return std::nullopt;
}

void SessionKeyCache::put(const std::string& filePath,
                          const SecureBytes& aesKey,
                          const SecureBytes& hmacKey) {
    std::lock_guard<std::mutex> lock(mutex_);

    evictIfNeeded();

    Entry entry;
    entry.filePath = filePath;
    entry.aesKey = aesKey;
    entry.hmacKey = hmacKey;
    entry.lastUsed = std::chrono::steady_clock::now();

    cache_.push_back(std::move(entry));
}

void SessionKeyCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& entry : cache_) {
        // Securely zero key material before deallocation
#ifdef _WIN32
        ::SecureZeroMemory(entry.aesKey.data(), entry.aesKey.size());
        ::SecureZeroMemory(entry.hmacKey.data(), entry.hmacKey.size());
#else
        // Use volatile to prevent compiler from optimizing away the zero loop
        volatile uint8_t* aesPtr = reinterpret_cast<volatile uint8_t*>(entry.aesKey.data());
        for (size_t i = 0; i < entry.aesKey.size(); ++i) {
            aesPtr[i] = 0;
        }
        volatile uint8_t* hmacPtr = reinterpret_cast<volatile uint8_t*>(entry.hmacKey.data());
        for (size_t i = 0; i < entry.hmacKey.size(); ++i) {
            hmacPtr[i] = 0;
        }
#endif
    }

    cache_.clear();
}

void SessionKeyCache::evictIfNeeded() {
    while (cache_.size() >= MAX_ENTRIES) {
        // Find entry with oldest lastUsed
        auto oldestIt = std::min_element(cache_.begin(), cache_.end(),
            [](const Entry& a, const Entry& b) {
                return a.lastUsed < b.lastUsed;
            });

        // Securely zero key material before erasing
#ifdef _WIN32
        ::SecureZeroMemory(oldestIt->aesKey.data(), oldestIt->aesKey.size());
        ::SecureZeroMemory(oldestIt->hmacKey.data(), oldestIt->hmacKey.size());
#else
        volatile uint8_t* aesPtr = reinterpret_cast<volatile uint8_t*>(oldestIt->aesKey.data());
        for (size_t i = 0; i < oldestIt->aesKey.size(); ++i) {
            aesPtr[i] = 0;
        }
        volatile uint8_t* hmacPtr = reinterpret_cast<volatile uint8_t*>(oldestIt->hmacKey.data());
        for (size_t i = 0; i < oldestIt->hmacKey.size(); ++i) {
            hmacPtr[i] = 0;
        }
#endif

        cache_.erase(oldestIt);
    }
}

} // namespace hlplayer::crypto