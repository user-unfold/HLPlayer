#ifndef HLPLAYER_FILE_ENCRYPTOR_H
#define HLPLAYER_FILE_ENCRYPTOR_H

#include "CryptoExport.h"
#include "SecureAllocator.h"
#include "HlvHeader.h"
#include <functional>
#include <string>
#include <atomic>

namespace hlplayer::crypto {

enum class EncryptError {
    None = 0,
    InputNotFound,
    OutputExists,
    DiskFull,
    IoError,
    Cancelled,
    AlreadyEncrypted,
    Unknown
};

struct EncryptConfig {
    std::string inputPath;        // source .mp4/.mkv (MUST NOT be .hlv)
    std::string outputPath;       // target .hlv
    KeyMode keyMode;              // Password or RawKey
    std::string password;         // if Password mode (empty if RawKey)
    SecureBytes rawKey;           // if RawKey mode (32 bytes, empty if Password)
    std::string originalExt;      // "mp4", "mkv", etc. (max 7 chars)
};

struct EncryptResult {
    bool success = false;
    EncryptError error = EncryptError::None;
    std::string errorMessage;
    SecureBytes generatedKey;     // populated if RawKey mode (the generated key)
};

class HLPLAYER_CRYPTO_API FileEncryptor {
public:
    FileEncryptor();
    ~FileEncryptor();

    EncryptResult encrypt(const EncryptConfig& config,
                          std::function<void(double progress)> onProgress = nullptr);

    void cancel();

private:
    std::atomic<bool> shouldCancel_{false};

    // Helper: check disk space
    bool checkDiskSpace(const std::string& path, uint64_t requiredBytes);

    // Helper: generate random salt and nonce
    void generateRandomBytes(uint8_t* buf, size_t len);
};

} // namespace hlplayer::crypto

#endif // HLPLAYER_FILE_ENCRYPTOR_H