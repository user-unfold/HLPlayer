#ifndef HLPLAYER_HMAC_SHA256_H
#define HLPLAYER_HMAC_SHA256_H

#include <cstddef>
#include <cstdint>

#include "CryptoExport.h"

typedef struct AVHMAC AVHMAC;

namespace hlplayer::crypto {

/// HMAC-SHA256 wrapper built on FFmpeg's av_hmac API.
class HLPLAYER_CRYPTO_API HmacSha256 {
public:
    HmacSha256();
    ~HmacSha256();

    HmacSha256(const HmacSha256&) = delete;
    HmacSha256& operator=(const HmacSha256&) = delete;

    /// Initialize with key.
    void init(const uint8_t* key, size_t keyLen);

    /// Feed data into HMAC.
    void update(const uint8_t* data, size_t len);

    /// Finalize and write 32-byte digest to out.
    /// Returns bytes written (32 on success).
    int final(uint8_t out[32]);

    /// One-shot: compute HMAC-SHA256(key, data) → out[32].
    static void compute(const uint8_t* key, size_t keyLen,
                        const uint8_t* data, size_t dataLen,
                        uint8_t out[32]);

private:
    AVHMAC* m_ctx;
};

} // namespace hlplayer::crypto

#endif // HLPLAYER_HMAC_SHA256_H
