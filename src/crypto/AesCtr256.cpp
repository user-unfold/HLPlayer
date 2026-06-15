#include "AesCtr256.h"

#include <cstring>

extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}

namespace hlplayer::crypto {

AesCtr256::AesCtr256()
    : m_aesCtx(nullptr)
    , m_counter(0)
    , m_keystreamPos(0)
    , m_initialized(false) {
    std::memset(m_nonce, 0, sizeof(m_nonce));
    std::memset(m_keystream, 0, sizeof(m_keystream));
}

AesCtr256::~AesCtr256() {
    if (m_aesCtx) {
        av_freep(&m_aesCtx);
    }
}

AesCtr256::AesCtr256(AesCtr256&& other) noexcept
    : m_aesCtx(other.m_aesCtx)
    , m_counter(other.m_counter)
    , m_keystreamPos(other.m_keystreamPos)
    , m_initialized(other.m_initialized) {
    std::memcpy(m_nonce, other.m_nonce, sizeof(m_nonce));
    std::memcpy(m_keystream, other.m_keystream, sizeof(m_keystream));
    other.m_aesCtx = nullptr;
    other.m_initialized = false;
    other.m_counter = 0;
    other.m_keystreamPos = 0;
}

AesCtr256& AesCtr256::operator=(AesCtr256&& other) noexcept {
    if (this != &other) {
        if (m_aesCtx) {
            av_freep(&m_aesCtx);
        }
        m_aesCtx = other.m_aesCtx;
        m_counter = other.m_counter;
        m_keystreamPos = other.m_keystreamPos;
        m_initialized = other.m_initialized;
        std::memcpy(m_nonce, other.m_nonce, sizeof(m_nonce));
        std::memcpy(m_keystream, other.m_keystream, sizeof(m_keystream));
        other.m_aesCtx = nullptr;
        other.m_initialized = false;
        other.m_counter = 0;
        other.m_keystreamPos = 0;
    }
    return *this;
}

bool AesCtr256::init(const uint8_t key[32], const uint8_t nonce[12]) {
    m_aesCtx = av_aes_alloc();
    if (!m_aesCtx) {
        return false;
    }

    std::memcpy(m_nonce, nonce, 12);
    m_counter = 0;
    m_keystreamPos = 16; // Trigger keystream generation on first process() call

    int ret = av_aes_init(m_aesCtx, key, 256, 0);
    if (ret != 0) {
        av_freep(&m_aesCtx);
        return false;
    }

    m_initialized = true;
    return true;
}

void AesCtr256::uint32ToBe(uint32_t val, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(val & 0xFF);
}

void AesCtr256::generateKeystreamBlock() {
    // Build counter block: nonce[12] || counter[4] big-endian
    uint8_t counterBlock[16];
    std::memcpy(counterBlock, m_nonce, 12);
    uint32ToBe(m_counter, counterBlock + 12);

    // ECB encrypt the counter block to produce keystream
    // iv=NULL → ECB mode, count=1 block, decrypt=0 (encrypt)
    av_aes_crypt(m_aesCtx, m_keystream, counterBlock, 1, nullptr, 0);

    // Increment counter
    ++m_counter;
    m_keystreamPos = 0;
}

void AesCtr256::process(const uint8_t* input, uint8_t* output, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        if (m_keystreamPos == 16) {
            generateKeystreamBlock();
        }

        int remaining = 16 - m_keystreamPos;
        size_t chunk = length - offset;
        if (static_cast<size_t>(remaining) < chunk) {
            chunk = static_cast<size_t>(remaining);
        }

        // XOR input with keystream
        for (size_t i = 0; i < chunk; ++i) {
            output[offset + i] = input[offset + i] ^ m_keystream[m_keystreamPos + i];
        }

        m_keystreamPos += static_cast<int>(chunk);
        offset += chunk;
    }
}

void AesCtr256::seek(uint64_t absoluteOffset) {
    uint64_t blockIndex = absoluteOffset / 16;
    int blockSkip = static_cast<int>(absoluteOffset % 16);

    m_counter = static_cast<uint32_t>(blockIndex);
    generateKeystreamBlock();
    m_keystreamPos = blockSkip;
}

} // namespace hlplayer::crypto
