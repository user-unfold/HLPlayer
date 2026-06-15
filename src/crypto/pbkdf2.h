#ifndef HLPLAYER_PBKDF2_H
#define HLPLAYER_PBKDF2_H

#include <cstddef>
#include <cstdint>

#include "CryptoExport.h"

namespace hlplayer::crypto {

/// PBKDF2-HMAC-SHA256 key derivation per RFC 8018.
/// Derives outLen bytes from password + salt + iterations.
HLPLAYER_CRYPTO_API
void pbkdf2_hmac_sha256(const uint8_t* password, size_t passwordLen,
                        const uint8_t* salt, size_t saltLen,
                        uint32_t iterations,
                        uint8_t* out, size_t outLen);

} // namespace hlplayer::crypto

#endif // HLPLAYER_PBKDF2_H
