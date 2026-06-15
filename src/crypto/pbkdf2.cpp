#include "pbkdf2.h"
#include "HmacSha256.h"

#include <cstring>

namespace hlplayer::crypto {

namespace {

/// Write a uint32_t as 4 big-endian bytes.
void uint32ToBe(uint32_t val, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(val & 0xFF);
}

/// XOR block of 32 bytes in place.
void xorBlock(uint8_t* dst, const uint8_t* src, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        dst[i] ^= src[i];
    }
}

} // anonymous namespace

void pbkdf2_hmac_sha256(const uint8_t* password, size_t passwordLen,
                        const uint8_t* salt, size_t saltLen,
                        uint32_t iterations,
                        uint8_t* out, size_t outLen) {
    constexpr size_t HASH_LEN = 32;

    // Number of 32-byte blocks needed
    size_t blockCount = (outLen + HASH_LEN - 1) / HASH_LEN;

    for (size_t blockIndex = 1; blockIndex <= blockCount; ++blockIndex) {
        uint8_t u[HASH_LEN];
        uint8_t t[HASH_LEN];
        std::memset(u, 0, HASH_LEN);
        std::memset(t, 0, HASH_LEN);

        // U_1 = HMAC(password, salt || INT_32_BE(blockIndex))
        {
            // Build: salt || INT_32_BE(i)
            size_t msgLen = saltLen + 4;
            // Use stack buffer for small salt+4 (typical case)
            uint8_t msg[256];
            if (msgLen <= sizeof(msg)) {
                std::memcpy(msg, salt, saltLen);
                uint32ToBe(static_cast<uint32_t>(blockIndex), msg + saltLen);
                HmacSha256::compute(password, passwordLen, msg, msgLen, u);
            } else {
                // Large salt: heap allocate
                auto msgBuf = new uint8_t[msgLen];
                std::memcpy(msgBuf, salt, saltLen);
                uint32ToBe(static_cast<uint32_t>(blockIndex), msgBuf + saltLen);
                HmacSha256::compute(password, passwordLen, msgBuf, msgLen, u);
                delete[] msgBuf;
            }
        }

        std::memcpy(t, u, HASH_LEN);

        // U_2 .. U_c: U_j = HMAC(password, U_{j-1})
        for (uint32_t j = 1; j < iterations; ++j) {
            uint8_t newU[HASH_LEN];
            HmacSha256::compute(password, passwordLen, u, HASH_LEN, newU);
            std::memcpy(u, newU, HASH_LEN);
            xorBlock(t, u, HASH_LEN);
        }

        // Copy result bytes
        size_t offset = (blockIndex - 1) * HASH_LEN;
        size_t copyLen = outLen - offset;
        if (copyLen > HASH_LEN) {
            copyLen = HASH_LEN;
        }
        std::memcpy(out + offset, t, copyLen);
    }
}

} // namespace hlplayer::crypto
