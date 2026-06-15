#include <catch2/catch_test_macros.hpp>
#include "HlvHeader.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace hlplayer::crypto;

TEST_CASE("HlvHeader serialize/deserialize round-trip", "[hlv_header]") {
    HlvHeader original;
    original.version = HLV_VERSION;
    original.keyMode = KeyMode::Password;
    original.algorithm = Algorithm::AES256CTR;
    original.flags = 0;
    std::memset(original.salt, 0xAA, HLV_SALT_SIZE);
    std::memset(original.nonce, 0xBB, HLV_NONCE_SIZE);
    original.pbkdf2Iterations = 600000;
    original.originalSize = 12345678;
    std::memcpy(original.originalExt, "mp4\0\0\0\0\0", HLV_ORIG_EXT_SIZE);
    std::memset(original.reserved, 0, HLV_RESERVED_SIZE);
    std::memset(original.headerHmac, 0xCC, HLV_HMAC_SIZE);

    auto data = original.serialize();
    REQUIRE(data.size() == HLV_HEADER_SIZE);

    auto restored = HlvHeader::deserialize(data.data(), data.size());

    REQUIRE(restored.version == original.version);
    REQUIRE(restored.keyMode == original.keyMode);
    REQUIRE(restored.algorithm == original.algorithm);
    REQUIRE(restored.flags == original.flags);
    REQUIRE(std::memcmp(restored.salt, original.salt, HLV_SALT_SIZE) == 0);
    REQUIRE(std::memcmp(restored.nonce, original.nonce, HLV_NONCE_SIZE) == 0);
    REQUIRE(restored.pbkdf2Iterations == original.pbkdf2Iterations);
    REQUIRE(restored.originalSize == original.originalSize);
    REQUIRE(std::memcmp(restored.originalExt, original.originalExt, HLV_ORIG_EXT_SIZE) == 0);
    REQUIRE(std::memcmp(restored.reserved, original.reserved, HLV_RESERVED_SIZE) == 0);
    REQUIRE(std::memcmp(restored.headerHmac, original.headerHmac, HLV_HMAC_SIZE) == 0);
}

TEST_CASE("Serialized size is exactly 112 bytes", "[hlv_header]") {
    HlvHeader header{};
    auto data = header.serialize();
    REQUIRE(data.size() == 112);
}

TEST_CASE("Magic bytes at correct offset in serialized data", "[hlv_header]") {
    HlvHeader header{};
    auto data = header.serialize();
    // First 8 bytes should be "HLPENC\0\0"
    REQUIRE(std::memcmp(data.data(), HLV_MAGIC, 8) == 0);
}

TEST_CASE("Field offsets are correct", "[hlv_header]") {
    HlvHeader header;
    header.version = 0x1234;
    header.keyMode = KeyMode::Password;
    header.algorithm = Algorithm::AES256CTR;
    header.pbkdf2Iterations = 0xDEADBEEF;
    header.originalSize = 0x0102030405060708ULL;

    auto data = header.serialize();

    // Magic at offset 0x00
    REQUIRE(std::memcmp(data.data(), HLV_MAGIC, 8) == 0);
    // Version at offset 0x08 (LE)
    REQUIRE(data[8] == 0x34);
    REQUIRE(data[9] == 0x12);
    // key_mode at offset 0x0A
    REQUIRE(data[0x0A] == 0x01);
    // algorithm at offset 0x0B
    REQUIRE(data[0x0B] == 0x01);
    // pbkdf2 at offset 0x2C (LE)
    REQUIRE(data[0x2C] == 0xEF);
    // original_size at offset 0x30 (LE)
    REQUIRE(data[0x30] == 0x08);
    REQUIRE(data[0x37] == 0x01);
}

TEST_CASE("validateMagic rejects wrong magic", "[hlv_header]") {
    HlvHeader header;
    auto data = header.serialize();
    // Corrupt the magic
    data[0] = 'X';
    REQUIRE(header.validateMagic(data.data()) == false);
}

TEST_CASE("validateMagic accepts correct magic", "[hlv_header]") {
    HlvHeader header;
    auto data = header.serialize();
    REQUIRE(header.validateMagic(data.data()) == true);
}

TEST_CASE("validateMagic rejects nullptr", "[hlv_header]") {
    HlvHeader header;
    REQUIRE(header.validateMagic(nullptr) == false);
}

TEST_CASE("getClampedIterations clamps to max", "[hlv_header]") {
    HlvHeader header;
    header.pbkdf2Iterations = 0xFFFFFFFF;
    REQUIRE(header.getClampedIterations() == HLV_MAX_ITERATIONS);
}

TEST_CASE("getClampedIterations passes through valid values", "[hlv_header]") {
    HlvHeader header;
    header.pbkdf2Iterations = 600000;
    REQUIRE(header.getClampedIterations() == 600000);
}

TEST_CASE("getClampedIterations passes zero", "[hlv_header]") {
    HlvHeader header;
    header.pbkdf2Iterations = 0;
    REQUIRE(header.getClampedIterations() == 0);
}

TEST_CASE("hasHlvExtension returns true for .hlv files", "[hlv_header]") {
    REQUIRE(hasHlvExtension("video.hlv") == true);
    REQUIRE(hasHlvExtension("VIDEO.HLV") == true);
    REQUIRE(hasHlvExtension("test.HlV") == true);
}

TEST_CASE("hasHlvExtension returns false for non-hlv files", "[hlv_header]") {
    REQUIRE(hasHlvExtension("video.mp4") == false);
    REQUIRE(hasHlvExtension("video.hlvx") == false);
    REQUIRE(hasHlvExtension("video") == false);
    REQUIRE(hasHlvExtension("") == false);
    REQUIRE(hasHlvExtension(".hlv") == true);
}

TEST_CASE("isHlvFile detects magic bytes", "[hlv_header]") {
    // Create temp file with HLV magic
    const char* tmpName = std::tmpnam(nullptr);
    std::string path(tmpName);
    path += ".tmp";

    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        // Write 8 bytes of magic + some padding
        uint8_t buf[16];
        std::memcpy(buf, HLV_MAGIC, 8);
        std::memset(buf + 8, 0, 8);
        std::fwrite(buf, 1, 16, f);
        std::fclose(f);
    }

    REQUIRE(isHlvFile(path) == true);
    std::remove(path.c_str());
}

TEST_CASE("isHlvFile rejects wrong magic", "[hlv_header]") {
    const char* tmpName = std::tmpnam(nullptr);
    std::string path(tmpName);
    path += ".tmp";

    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        uint8_t buf[8] = {'N', 'O', 'T', 'H', 'L', 'V', 0, 0};
        std::fwrite(buf, 1, 8, f);
        std::fclose(f);
    }

    REQUIRE(isHlvFile(path) == false);
    std::remove(path.c_str());
}

TEST_CASE("isHlvFile returns false for nonexistent file", "[hlv_header]") {
    REQUIRE(isHlvFile("nonexistent_file_xyz123.xyz") == false);
}

TEST_CASE("isValid accepts valid header", "[hlv_header]") {
    HlvHeader header{};
    header.version = HLV_VERSION;
    header.keyMode = KeyMode::Password;
    header.algorithm = Algorithm::AES256CTR;
    header.pbkdf2Iterations = 600000;
    REQUIRE(header.isValid() == true);
}

TEST_CASE("isValid rejects bad version", "[hlv_header]") {
    HlvHeader header{};
    header.version = 999;
    header.keyMode = KeyMode::Password;
    header.algorithm = Algorithm::AES256CTR;
    REQUIRE(header.isValid() == false);
}

TEST_CASE("isValid rejects bad key mode", "[hlv_header]") {
    HlvHeader header{};
    header.version = HLV_VERSION;
    header.keyMode = static_cast<KeyMode>(0xFF);
    header.algorithm = Algorithm::AES256CTR;
    REQUIRE(header.isValid() == false);
}

TEST_CASE("isValid rejects bad algorithm", "[hlv_header]") {
    HlvHeader header{};
    header.version = HLV_VERSION;
    header.keyMode = KeyMode::Password;
    header.algorithm = static_cast<Algorithm>(0xFF);
    REQUIRE(header.isValid() == false);
}

TEST_CASE("isValid rejects non-zero reserved fields", "[hlv_header]") {
    HlvHeader header{};
    header.version = HLV_VERSION;
    header.keyMode = KeyMode::Password;
    header.algorithm = Algorithm::AES256CTR;
    header.reserved[5] = 0x01;
    REQUIRE(header.isValid() == false);
}

TEST_CASE("isValid for raw key mode requires zero iterations", "[hlv_header]") {
    HlvHeader header{};
    header.version = HLV_VERSION;
    header.keyMode = KeyMode::RawKey;
    header.algorithm = Algorithm::AES256CTR;
    header.pbkdf2Iterations = 600000; // Should be 0 for raw key
    REQUIRE(header.isValid() == false);

    header.pbkdf2Iterations = 0;
    REQUIRE(header.isValid() == true);
}
