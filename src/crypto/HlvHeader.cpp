#include "HlvHeader.h"
#include <cstring>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>

namespace {
std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}
}
#endif

namespace hlplayer::crypto {

std::vector<uint8_t> HlvHeader::serialize() const {
    std::vector<uint8_t> data(HLV_HEADER_SIZE);
    uint8_t* ptr = data.data();

    // Offset 0x00: magic[8] - "HLPENC\0\0"
    memcpy(ptr, HLV_MAGIC, 8);
    ptr += 8;

    // Offset 0x08: version (uint16 LE)
    memcpy(ptr, &version, 2);
    ptr += 2;

    // Offset 0x0A: key_mode (uint8)
    uint8_t keyModeVal = static_cast<uint8_t>(keyMode);
    memcpy(ptr, &keyModeVal, 1);
    ptr += 1;

    // Offset 0x0B: algorithm (uint8)
    uint8_t algoVal = static_cast<uint8_t>(algorithm);
    memcpy(ptr, &algoVal, 1);
    ptr += 1;

    // Offset 0x0C: flags (uint32 LE)
    memcpy(ptr, &flags, 4);
    ptr += 4;

    // Offset 0x10: salt[16]
    memcpy(ptr, salt, HLV_SALT_SIZE);
    ptr += HLV_SALT_SIZE;

    // Offset 0x20: nonce[12]
    memcpy(ptr, nonce, HLV_NONCE_SIZE);
    ptr += HLV_NONCE_SIZE;

    // Offset 0x2C: pbkdf2_iterations (uint32 LE)
    memcpy(ptr, &pbkdf2Iterations, 4);
    ptr += 4;

    // Offset 0x30: original_size (uint64 LE)
    memcpy(ptr, &originalSize, 8);
    ptr += 8;

    // Offset 0x38: original_ext[8]
    memcpy(ptr, originalExt, HLV_ORIG_EXT_SIZE);
    ptr += HLV_ORIG_EXT_SIZE;

    // Offset 0x40: reserved[16]
    memcpy(ptr, reserved, HLV_RESERVED_SIZE);
    ptr += HLV_RESERVED_SIZE;

    // Offset 0x50: header_hmac[32]
    memcpy(ptr, headerHmac, HLV_HMAC_SIZE);
    // ptr += HLV_HMAC_SIZE; // Last field, no need to advance

    return data;
}

HlvHeader HlvHeader::deserialize(const uint8_t* data, size_t len) {
    HlvHeader header{};
    const uint8_t* ptr = data;

    // Validate minimum length
    if (len < HLV_HEADER_SIZE) {
        return header; // Return default-constructed header on error
    }

    // Offset 0x08: version (uint16 LE)
    memcpy(&header.version, ptr + 8, 2);

    // Offset 0x0A: key_mode (uint8)
    header.keyMode = static_cast<KeyMode>(ptr[0x0A]);

    // Offset 0x0B: algorithm (uint8)
    header.algorithm = static_cast<Algorithm>(ptr[0x0B]);

    // Offset 0x0C: flags (uint32 LE)
    memcpy(&header.flags, ptr + 0x0C, 4);

    // Offset 0x10: salt[16]
    memcpy(header.salt, ptr + 0x10, HLV_SALT_SIZE);

    // Offset 0x20: nonce[12]
    memcpy(header.nonce, ptr + 0x20, HLV_NONCE_SIZE);

    // Offset 0x2C: pbkdf2_iterations (uint32 LE)
    memcpy(&header.pbkdf2Iterations, ptr + 0x2C, 4);

    // Offset 0x30: original_size (uint64 LE)
    memcpy(&header.originalSize, ptr + 0x30, 8);

    // Offset 0x38: original_ext[8]
    memcpy(header.originalExt, ptr + 0x38, HLV_ORIG_EXT_SIZE);

    // Offset 0x40: reserved[16]
    memcpy(header.reserved, ptr + 0x40, HLV_RESERVED_SIZE);

    // Offset 0x50: header_hmac[32]
    memcpy(header.headerHmac, ptr + 0x50, HLV_HMAC_SIZE);

    return header;
}

bool HlvHeader::validateMagic(const uint8_t* serializedData) const {
    if (!serializedData) {
        return false;
    }
    return memcmp(serializedData, HLV_MAGIC, 8) == 0;
}

uint32_t HlvHeader::getClampedIterations() const {
    return std::min(pbkdf2Iterations, HLV_MAX_ITERATIONS);
}

bool HlvHeader::isValid() const {
    // Check version is supported
    if (version != HLV_VERSION) {
        return false;
    }

    // Check key mode is valid
    if (keyMode != KeyMode::Password && keyMode != KeyMode::RawKey) {
        return false;
    }

    // Check algorithm is valid
    if (algorithm != Algorithm::AES256CTR) {
        return false;
    }

    // Check PBKDF2 iterations are within valid range
    if (keyMode == KeyMode::Password) {
        if (pbkdf2Iterations < 1 || pbkdf2Iterations > HLV_MAX_ITERATIONS) {
            return false;
        }
    } else {
        // Raw key mode should have 0 iterations
        if (pbkdf2Iterations != 0) {
            return false;
        }
    }

    // Check reserved fields are all zero
    for (size_t i = 0; i < HLV_RESERVED_SIZE; ++i) {
        if (reserved[i] != 0) {
            return false;
        }
    }

    return true;
}

bool isHlvFile(const std::string& filePath) {
    FILE* f = nullptr;
#ifdef _WIN32
    std::wstring wPath = utf8ToWide(filePath);
    f = _wfopen(wPath.c_str(), L"rb");
#else
    f = fopen(filePath.c_str(), "rb");
#endif
    if (!f) return false;

    uint8_t magic[8];
    size_t read = fread(magic, 1, 8, f);
    fclose(f);

    if (read != 8) return false;
    return memcmp(magic, HLV_MAGIC, 8) == 0;
}

bool hasHlvExtension(const std::string& filePath) {
    if (filePath.empty()) {
        return false;
    }

    size_t pos = filePath.find_last_of('.');
    if (pos == std::string::npos) {
        return false;
    }

    std::string ext = filePath.substr(pos);
    if (ext.length() != 4) { // ".hlv" is 4 chars
        return false;
    }

    // Case-insensitive comparison
    return std::tolower(ext[1]) == 'h' &&
           std::tolower(ext[2]) == 'l' &&
           std::tolower(ext[3]) == 'v';
}

} // namespace hlplayer::crypto