#ifndef HLPLAYER_AES_CTR_256_H
#define HLPLAYER_AES_CTR_256_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "CryptoExport.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
// MinGW's bcrypt.h may lack this constant
#ifndef BCRYPT_CHAINING_MODE_CTR
#define BCRYPT_CHAINING_MODE_CTR L"ChainingModeCTR"
#endif
#endif

namespace hlplayer::crypto {

/// AES-256-CTR stream cipher via Windows BCrypt (hardware AES-NI).
///
/// Counter block layout (16 bytes): nonce[12] || counter[4] (big-endian).
/// BCrypt handles CTR counter increment in hardware; bulk data passes
/// through in a single API call without manual keystream management.
class HLPLAYER_CRYPTO_API AesCtr256 {
public:
    AesCtr256();
    ~AesCtr256();

    // Non-copyable, movable
    AesCtr256(const AesCtr256&) = delete;
    AesCtr256& operator=(const AesCtr256&) = delete;
    AesCtr256(AesCtr256&& other) noexcept;
    AesCtr256& operator=(AesCtr256&& other) noexcept;

    /// Initialize with 32-byte key and 12-byte nonce.
    /// Returns true on success.
    bool init(const uint8_t key[32], const uint8_t nonce[12]);

    /// Encrypt/decrypt data (identical operation in CTR mode).
    /// Advances internal counter state.
    void process(const uint8_t* input, uint8_t* output, size_t length);

    /// Seek to absolute byte offset. Resets counter and handles
    /// partial-block alignment.
    void seek(uint64_t absoluteOffset);

private:
#ifdef _WIN32
    BCRYPT_ALG_HANDLE m_hAlg;
    BCRYPT_KEY_HANDLE m_hKey;
    std::vector<UCHAR> m_keyObj;
#endif
    uint8_t m_nonce[12];
    uint8_t m_counterBlock[16];
    uint32_t m_counter;
    int m_partialSkip;        // bytes to skip in next process() due to seek
    bool m_initialized;

    static void uint32ToBe(uint32_t val, uint8_t out[4]);
    void buildCounterBlock();
};

} // namespace hlplayer::crypto

#endif // HLPLAYER_AES_CTR_256_H
