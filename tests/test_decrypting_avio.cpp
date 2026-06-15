#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "DecryptingAVIOContext.h"
#include "AesCtr256.h"
#include "HlvHeader.h"
#include "SecureAllocator.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

using namespace hlplayer::crypto;

namespace {

// Helper: create a .hlv temp file with encrypted data
struct HlvTempFile {
    std::string path;
    std::vector<uint8_t> originalData;
    uint8_t key[32];
    uint8_t nonce[12];
    uint64_t originalSize;

    ~HlvTempFile() {
        if (!path.empty()) {
            std::remove(path.c_str());
        }
    }
};

HlvTempFile createHlvTempFile(size_t dataSize) {
    HlvTempFile hlv;

    // Generate random original data
    hlv.originalData.resize(dataSize);
    std::mt19937 rng(42); // deterministic seed for reproducibility
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    for (size_t i = 0; i < dataSize; ++i) {
        hlv.originalData[i] = static_cast<uint8_t>(dist(rng));
    }

    // Generate random key and nonce
    for (int i = 0; i < 32; ++i) {
        hlv.key[i] = static_cast<uint8_t>(dist(rng));
    }
    for (int i = 0; i < 12; ++i) {
        hlv.nonce[i] = static_cast<uint8_t>(dist(rng));
    }

    hlv.originalSize = dataSize;

    // Build HLV header
    HlvHeader header;
    header.version = HLV_VERSION;
    header.keyMode = KeyMode::RawKey;
    header.algorithm = Algorithm::AES256CTR;
    header.flags = 0;
    std::memcpy(header.salt, hlv.nonce, HLV_SALT_SIZE); // repurpose salt
    std::memcpy(header.nonce, hlv.nonce, HLV_NONCE_SIZE);
    header.pbkdf2Iterations = 0;
    header.originalSize = dataSize;
    std::memset(header.originalExt, 0, HLV_ORIG_EXT_SIZE);
    std::memcpy(header.originalExt, ".mp4", 4);
    std::memset(header.reserved, 0, HLV_RESERVED_SIZE);
    std::memset(header.headerHmac, 0, HLV_HMAC_SIZE);

    auto headerBytes = header.serialize();

    // Encrypt the data
    AesCtr256 aes;
    aes.init(hlv.key, hlv.nonce);
    std::vector<uint8_t> ciphertext(dataSize);
    aes.process(hlv.originalData.data(), ciphertext.data(), dataSize);

    // Write to temp file
    hlv.path = "test_decrypting_avio_tmp.hlv";
    FILE* f = std::fopen(hlv.path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(headerBytes.data(), 1, headerBytes.size(), f);
    std::fwrite(ciphertext.data(), 1, ciphertext.size(), f);
    std::fclose(f);

    return hlv;
}

} // anonymous namespace

TEST_CASE("DecryptingAVIOContext read all data", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 1024 * 1024; // 1MB
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // Read all data
    std::vector<uint8_t> readData(DATA_SIZE);
    size_t totalRead = 0;
    while (totalRead < DATA_SIZE) {
        int ret = avio_read(ctx, readData.data() + totalRead,
                            static_cast<int>(DATA_SIZE - totalRead));
        if (ret <= 0) break;
        totalRead += static_cast<size_t>(ret);
    }

    REQUIRE(totalRead == DATA_SIZE);
    REQUIRE(std::memcmp(readData.data(), hlv.originalData.data(), DATA_SIZE) == 0);

    DecryptingAVIOContext::destroy(ctx);
}

TEST_CASE("DecryptingAVIOContext seek to non-block-aligned offset", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 10240;
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // Seek to offset 5000
    int64_t ret = avio_seek(ctx, 5000, SEEK_SET);
    REQUIRE(ret == 5000);

    // Read 100 bytes
    uint8_t buf[100];
    int nread = avio_read(ctx, buf, 100);
    REQUIRE(nread == 100);
    REQUIRE(std::memcmp(buf, hlv.originalData.data() + 5000, 100) == 0);

    DecryptingAVIOContext::destroy(ctx);
}

TEST_CASE("DecryptingAVIOContext seek to block boundary", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 1024;
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // Seek to offset 256 (block boundary: 256/16 = 16 blocks)
    int64_t ret = avio_seek(ctx, 256, SEEK_SET);
    REQUIRE(ret == 256);

    // Read 100 bytes
    uint8_t buf[100];
    int nread = avio_read(ctx, buf, 100);
    REQUIRE(nread == 100);
    REQUIRE(std::memcmp(buf, hlv.originalData.data() + 256, 100) == 0);

    DecryptingAVIOContext::destroy(ctx);
}

TEST_CASE("DecryptingAVIOContext seek to end returns EOF", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 1024;
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // Seek to originalSize (end of logical stream)
    int64_t ret = avio_seek(ctx, static_cast<int64_t>(hlv.originalSize), SEEK_SET);
    REQUIRE(ret == static_cast<int64_t>(hlv.originalSize));

    // Read should return EOF
    uint8_t buf[10];
    int nread = avio_read(ctx, buf, 10);
    REQUIRE(nread == AVERROR_EOF);

    DecryptingAVIOContext::destroy(ctx);
}

TEST_CASE("DecryptingAVIOContext seek to negative returns EINVAL", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 1024;
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // Seek to -1 should fail
    int64_t ret = avio_seek(ctx, -1, SEEK_SET);
    REQUIRE(ret == AVERROR(EINVAL));

    DecryptingAVIOContext::destroy(ctx);
}

TEST_CASE("DecryptingAVIOContext AVSEEK_SIZE returns originalSize", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 2048;
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // AVSEEK_SIZE should return the original logical size
    int64_t size = avio_seek(ctx, 0, AVSEEK_SIZE);
    REQUIRE(size == static_cast<int64_t>(hlv.originalSize));

    DecryptingAVIOContext::destroy(ctx);
}

TEST_CASE("DecryptingAVIOContext SEEK_CUR after read", "[decrypting_avio]") {
    constexpr size_t DATA_SIZE = 1024;
    auto hlv = createHlvTempFile(DATA_SIZE);

    DecryptConfig config;
    config.filePath = hlv.path;
    config.aesKey.assign(hlv.key, hlv.key + 32);
    std::memcpy(config.nonce, hlv.nonce, 12);
    config.originalSize = hlv.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(config);
    REQUIRE(ctx != nullptr);

    // Read 100 bytes (position is now 100)
    uint8_t buf[100];
    int nread = avio_read(ctx, buf, 100);
    REQUIRE(nread == 100);

    // SEEK_CUR with offset 50 → position should be 150
    int64_t ret = avio_seek(ctx, 50, SEEK_CUR);
    REQUIRE(ret == 150);

    // Read 50 bytes and verify they match original[150:200]
    uint8_t buf2[50];
    nread = avio_read(ctx, buf2, 50);
    REQUIRE(nread == 50);
    REQUIRE(std::memcmp(buf2, hlv.originalData.data() + 150, 50) == 0);

    DecryptingAVIOContext::destroy(ctx);
}
