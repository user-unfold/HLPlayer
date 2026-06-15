#include "pbkdf2.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>

// Manually declare constants and function missing from MinGW's bcrypt.h.
// These are standard Windows CNG APIs available in bcrypt.dll (Win8+).
#ifndef BCRYPT_SHA256_ALGORITHM
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#endif
#ifndef BCRYPT_CHAIN_MODE_HMAC
#define BCRYPT_CHAIN_MODE_HMAC L"ChainingModeHMAC"
#endif

extern "C" {
NTSTATUS WINAPI BCryptDeriveKeyPBKDF2(
    BCRYPT_ALG_HANDLE hPrf,
    PUCHAR pbPassword, ULONG cbPassword,
    PUCHAR pbSalt, ULONG cbSalt,
    ULONGLONG cIterations,
    PUCHAR pbDerivedKey, ULONG cbDerivedKey,
    ULONG dwFlags);
}
#else
#include "HmacSha256.h"
#include <cstring>
#endif

namespace hlplayer::crypto {

#ifdef _WIN32

void pbkdf2_hmac_sha256(const uint8_t* password, size_t passwordLen,
                        const uint8_t* salt, size_t saltLen,
                        uint32_t iterations,
                        uint8_t* out, size_t outLen) {
    // Cache the SHA-256 algorithm provider handle (thread-safe: opened once)
    static BCRYPT_ALG_HANDLE hSha256 = nullptr;
    if (!hSha256) {
        NTSTATUS s = BCryptOpenAlgorithmProvider(&hSha256, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(s)) {
            fprintf(stderr, "pbkdf2: BCryptOpenAlgorithmProvider(SHA256) failed: 0x%08lx\n", s);
            hSha256 = nullptr;
            return;
        }
        // Set HMAC mode (PBKDF2 uses HMAC internally)
        s = BCryptSetProperty(hSha256, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_HMAC, sizeof(BCRYPT_CHAIN_MODE_HMAC), 0);
        if (!BCRYPT_SUCCESS(s)) {
            fprintf(stderr, "pbkdf2: HMAC mode failed: 0x%08lx\n", s);
            BCryptCloseAlgorithmProvider(hSha256, 0);
            hSha256 = nullptr;
            return;
        }
    }

    NTSTATUS status = BCryptDeriveKeyPBKDF2(
        hSha256,
        (PUCHAR)password, static_cast<ULONG>(passwordLen),
        (PUCHAR)salt, static_cast<ULONG>(saltLen),
        iterations,
        out, static_cast<ULONG>(outLen),
        0);

    if (!BCRYPT_SUCCESS(status)) {
        fprintf(stderr, "pbkdf2: BCryptDeriveKeyPBKDF2 failed: 0x%08lx\n", status);
    }
}

#else

namespace {
void uint32ToBe(uint32_t val, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(val & 0xFF);
}
void xorBlock(uint8_t* dst, const uint8_t* src, size_t len) {
    for (size_t i = 0; i < len; ++i) dst[i] ^= src[i];
}
} // anonymous namespace

void pbkdf2_hmac_sha256(const uint8_t* password, size_t passwordLen,
                        const uint8_t* salt, size_t saltLen,
                        uint32_t iterations,
                        uint8_t* out, size_t outLen) {
    constexpr size_t HASH_LEN = 32;
    size_t blockCount = (outLen + HASH_LEN - 1) / HASH_LEN;

    for (size_t blockIndex = 1; blockIndex <= blockCount; ++blockIndex) {
        uint8_t u[HASH_LEN], t[HASH_LEN];
        std::memset(u, 0, HASH_LEN);
        std::memset(t, 0, HASH_LEN);

        {
            size_t msgLen = saltLen + 4;
            uint8_t msg[256];
            if (msgLen <= sizeof(msg)) {
                std::memcpy(msg, salt, saltLen);
                uint32ToBe(static_cast<uint32_t>(blockIndex), msg + saltLen);
                HmacSha256::compute(password, passwordLen, msg, msgLen, u);
            } else {
                auto msgBuf = new uint8_t[msgLen];
                std::memcpy(msgBuf, salt, saltLen);
                uint32ToBe(static_cast<uint32_t>(blockIndex), msgBuf + saltLen);
                HmacSha256::compute(password, passwordLen, msgBuf, msgLen, u);
                delete[] msgBuf;
            }
        }
        std::memcpy(t, u, HASH_LEN);

        for (uint32_t j = 1; j < iterations; ++j) {
            uint8_t newU[HASH_LEN];
            HmacSha256::compute(password, passwordLen, u, HASH_LEN, newU);
            std::memcpy(u, newU, HASH_LEN);
            xorBlock(t, u, HASH_LEN);
        }

        size_t offset = (blockIndex - 1) * HASH_LEN;
        size_t copyLen = outLen - offset;
        if (copyLen > HASH_LEN) copyLen = HASH_LEN;
        std::memcpy(out + offset, t, copyLen);
    }
}

#endif

} // namespace hlplayer::crypto
