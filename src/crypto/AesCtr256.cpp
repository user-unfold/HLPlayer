#include "AesCtr256.h"

#include <cstring>
#include <memory>

namespace hlplayer::crypto {

#ifdef _WIN32

AesCtr256::AesCtr256()
    : m_hAlg(nullptr)
    , m_hKey(nullptr)
    , m_counter(0)
    , m_partialSkip(0)
    , m_initialized(false) {
    std::memset(m_nonce, 0, sizeof(m_nonce));
    std::memset(m_counterBlock, 0, sizeof(m_counterBlock));
}

AesCtr256::~AesCtr256() {
    if (m_hKey) {
        BCryptDestroyKey(m_hKey);
        m_hKey = nullptr;
    }
    if (m_hAlg) {
        BCryptCloseAlgorithmProvider(m_hAlg, 0);
        m_hAlg = nullptr;
    }
    // SecureZeroMemory equivalent for key object
    if (!m_keyObj.empty()) {
        SecureZeroMemory(m_keyObj.data(), m_keyObj.size());
    }
}

AesCtr256::AesCtr256(AesCtr256&& other) noexcept
    : m_hAlg(other.m_hAlg)
    , m_hKey(other.m_hKey)
    , m_keyObj(std::move(other.m_keyObj))
    , m_counter(other.m_counter)
    , m_partialSkip(other.m_partialSkip)
    , m_initialized(other.m_initialized) {
    std::memcpy(m_nonce, other.m_nonce, sizeof(m_nonce));
    std::memcpy(m_counterBlock, other.m_counterBlock, sizeof(m_counterBlock));
    other.m_hAlg = nullptr;
    other.m_hKey = nullptr;
    other.m_initialized = false;
    other.m_counter = 0;
    other.m_partialSkip = 0;
}

AesCtr256& AesCtr256::operator=(AesCtr256&& other) noexcept {
    if (this != &other) {
        if (m_hKey) {
            BCryptDestroyKey(m_hKey);
        }
        if (m_hAlg) {
            BCryptCloseAlgorithmProvider(m_hAlg, 0);
        }
        if (!m_keyObj.empty()) {
            SecureZeroMemory(m_keyObj.data(), m_keyObj.size());
        }

        m_hAlg = other.m_hAlg;
        m_hKey = other.m_hKey;
        m_keyObj = std::move(other.m_keyObj);
        m_counter = other.m_counter;
        m_partialSkip = other.m_partialSkip;
        m_initialized = other.m_initialized;
        std::memcpy(m_nonce, other.m_nonce, sizeof(m_nonce));
        std::memcpy(m_counterBlock, other.m_counterBlock, sizeof(m_counterBlock));

        other.m_hAlg = nullptr;
        other.m_hKey = nullptr;
        other.m_initialized = false;
        other.m_counter = 0;
        other.m_partialSkip = 0;
    }
    return *this;
}

bool AesCtr256::init(const uint8_t key[32], const uint8_t nonce[12]) {
    NTSTATUS status;

    // Open AES algorithm provider
    status = BCryptOpenAlgorithmProvider(&m_hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return false;
    }

    // Set CTR chaining mode
    status = BCryptSetProperty(m_hAlg, BCRYPT_CHAINING_MODE,
        (PUCHAR)BCRYPT_CHAINING_MODE_CTR,
        sizeof(BCRYPT_CHAINING_MODE_CTR), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(m_hAlg, 0);
        m_hAlg = nullptr;
        return false;
    }

    // Allocate key object buffer
    ULONG keyObjSize = 0;
    ULONG cbResult = 0;
    status = BCryptGetProperty(m_hAlg, BCRYPT_OBJECT_LENGTH,
        (PUCHAR)&keyObjSize, sizeof(keyObjSize), &cbResult, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(m_hAlg, 0);
        m_hAlg = nullptr;
        return false;
    }
    m_keyObj.resize(keyObjSize);

    // Import AES-256 key
    status = BCryptGenerateSymmetricKey(m_hAlg, &m_hKey,
        m_keyObj.data(), keyObjSize,
        (PUCHAR)key, 32, 0);
    if (!BCRYPT_SUCCESS(status)) {
        m_keyObj.clear();
        BCryptCloseAlgorithmProvider(m_hAlg, 0);
        m_hAlg = nullptr;
        return false;
    }

    std::memcpy(m_nonce, nonce, 12);
    m_counter = 0;
    m_partialSkip = 0;
    buildCounterBlock();

    m_initialized = true;
    return true;
}

void AesCtr256::buildCounterBlock() {
    std::memcpy(m_counterBlock, m_nonce, 12);
    uint32ToBe(m_counter, m_counterBlock + 12);
}

void AesCtr256::uint32ToBe(uint32_t val, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(val & 0xFF);
}

void AesCtr256::process(const uint8_t* input, uint8_t* output, size_t length) {
    if (length == 0) return;

    ULONG cbResult;
    size_t offset = 0;

    // Handle partial block from a previous seek()
    if (m_partialSkip > 0) {
        // Generate keystream for the current counter block
        uint8_t keystream[16];
        uint8_t zero[16] = {};
        // BCryptEncrypt in CTR mode updates the IV. Save/restore to avoid
        // advancing counter for the partial skip.
        uint8_t ivBackup[16];
        std::memcpy(ivBackup, m_counterBlock, 16);
        BCryptEncrypt(m_hKey, zero, 16, nullptr, ivBackup, 16, keystream, 16, &cbResult, 0);

        size_t skip = static_cast<size_t>(m_partialSkip);
        size_t avail = 16 - skip;
        size_t chunk = (length < avail) ? length : avail;

        for (size_t i = 0; i < chunk; ++i) {
            output[i] = input[i] ^ keystream[skip + i];
        }
        offset += chunk;
        length -= chunk;

        if (chunk == avail) {
            m_partialSkip = 0;
            ++m_counter;
            buildCounterBlock();
        } else {
            m_partialSkip = static_cast<int>(skip + chunk);
        }
    }

    // Bulk encrypt remaining data via BCrypt (hardware AES-NI)
    if (length > 0) {
        BCryptEncrypt(m_hKey,
            (PUCHAR)(input + offset), static_cast<ULONG>(length),
            nullptr,                      // no padding info
            m_counterBlock, 16,           // IV (counter block, updated in-place)
            (PUCHAR)(output + offset), static_cast<ULONG>(length),
            &cbResult, 0);

        // Reconstruct m_counter from BCrypt-updated counterBlock
        m_counter = (static_cast<uint32_t>(m_counterBlock[12]) << 24) |
                    (static_cast<uint32_t>(m_counterBlock[13]) << 16) |
                    (static_cast<uint32_t>(m_counterBlock[14]) << 8) |
                    (static_cast<uint32_t>(m_counterBlock[15]));
    }
}

void AesCtr256::seek(uint64_t absoluteOffset) {
    uint64_t blockIndex = absoluteOffset / 16;
    int byteInBlock = static_cast<int>(absoluteOffset % 16);

    m_counter = static_cast<uint32_t>(blockIndex);
    m_partialSkip = byteInBlock;
    buildCounterBlock();
}

#else // !_WIN32 — fallback not implemented

AesCtr256::AesCtr256() : m_counter(0), m_partialSkip(0), m_initialized(false) {
    std::memset(m_nonce, 0, sizeof(m_nonce));
    std::memset(m_counterBlock, 0, sizeof(m_counterBlock));
}
AesCtr256::~AesCtr256() = default;
AesCtr256::AesCtr256(AesCtr256&&) noexcept = default;
AesCtr256& AesCtr256::operator=(AesCtr256&&) noexcept = default;
bool AesCtr256::init(const uint8_t[32], const uint8_t[12]) { return false; }
void AesCtr256::process(const uint8_t*, uint8_t*, size_t) {}
void AesCtr256::seek(uint64_t) {}
void AesCtr256::buildCounterBlock() {}
void AesCtr256::uint32ToBe(uint32_t, uint8_t[4]) {}

#endif

} // namespace hlplayer::crypto
