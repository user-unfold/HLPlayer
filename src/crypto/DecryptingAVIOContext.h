#pragma once
#include "CryptoExport.h"
#include "SecureAllocator.h"
#include <cstdint>
#include <string>

// Forward declarations to avoid exposing FFmpeg types in header
struct AVIOContext;

namespace hlplayer::crypto {

struct DecryptConfig {
    std::string filePath;        // path to .hlv file
    SecureBytes aesKey;          // 32 bytes
    uint8_t nonce[12];           // from header
    uint64_t originalSize;       // from header (logical stream size, excludes 112-byte header)
};

class HLPLAYER_CRYPTO_API DecryptingAVIOContext {
public:
    /// Create a custom AVIOContext that transparently decrypts .hlv data.
    /// Returns nullptr on failure.
    /// The returned AVIOContext must be destroyed with destroy().
    static AVIOContext* create(const DecryptConfig& config);

    /// Destroy a previously created AVIOContext.
    /// Safe to call with nullptr.
    static void destroy(AVIOContext* ctx);
};

} // namespace hlplayer::crypto
