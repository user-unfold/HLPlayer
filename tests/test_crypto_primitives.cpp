#include <catch2/catch_test_macros.hpp>
#include "AesCtr256.h"
#include "HmacSha256.h"
#include "pbkdf2.h"
#include "constant_time.h"
#include "SecureAllocator.h"
#include <cstring>
#include <vector>

using namespace hlplayer::crypto;

// Helper: convert hex string to bytes
static std::vector<uint8_t> hexToBytes(const char* hex) {
    size_t len = std::strlen(hex) / 2;
    std::vector<uint8_t> out(len);
    for (size_t i = 0; i < len; ++i) {
        char byteStr[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        out[i] = static_cast<uint8_t>(std::strtoul(byteStr, nullptr, 16));
    }
    return out;
}

TEST_CASE("AES-256-CTR encrypt-decrypt round-trip", "[crypto][aes]") {
    constexpr size_t DATA_SIZE = 1024;
    std::vector<uint8_t> plaintext(DATA_SIZE);
    for (size_t i = 0; i < DATA_SIZE; ++i) {
        plaintext[i] = static_cast<uint8_t>(i % 256);
    }

    uint8_t key[32] = {};
    uint8_t nonce[12] = {};

    // Encrypt
    AesCtr256 enc;
    REQUIRE(enc.init(key, nonce));
    std::vector<uint8_t> ciphertext(DATA_SIZE);
    enc.process(plaintext.data(), ciphertext.data(), DATA_SIZE);

    // Decrypt (new instance, same key/nonce)
    AesCtr256 dec;
    REQUIRE(dec.init(key, nonce));
    std::vector<uint8_t> decrypted(DATA_SIZE);
    dec.process(ciphertext.data(), decrypted.data(), DATA_SIZE);

    REQUIRE(std::memcmp(plaintext.data(), decrypted.data(), DATA_SIZE) == 0);
}

TEST_CASE("AES-256-CTR seek to non-aligned offset", "[crypto][aes]") {
    constexpr size_t DATA_SIZE = 10000;
    std::vector<uint8_t> plaintext(DATA_SIZE);
    for (size_t i = 0; i < DATA_SIZE; ++i) {
        plaintext[i] = static_cast<uint8_t>(i % 256);
    }

    uint8_t key[32] = {};
    uint8_t nonce[12] = {};

    // Encrypt full data
    AesCtr256 enc;
    REQUIRE(enc.init(key, nonce));
    std::vector<uint8_t> ciphertext(DATA_SIZE);
    enc.process(plaintext.data(), ciphertext.data(), DATA_SIZE);

    // Decrypt from offset 5000 only
    constexpr size_t SEEK_OFFSET = 5000;
    AesCtr256 dec;
    REQUIRE(dec.init(key, nonce));
    dec.seek(SEEK_OFFSET);

    size_t remaining = DATA_SIZE - SEEK_OFFSET;
    std::vector<uint8_t> decrypted(remaining);
    dec.process(ciphertext.data() + SEEK_OFFSET, decrypted.data(), remaining);

    REQUIRE(std::memcmp(plaintext.data() + SEEK_OFFSET, decrypted.data(), remaining) == 0);
}

TEST_CASE("AES-256-CTR seek to block boundary", "[crypto][aes]") {
    constexpr size_t DATA_SIZE = 1024;
    std::vector<uint8_t> plaintext(DATA_SIZE);
    for (size_t i = 0; i < DATA_SIZE; ++i) {
        plaintext[i] = static_cast<uint8_t>(i % 256);
    }

    uint8_t key[32] = {};
    uint8_t nonce[12] = {};

    AesCtr256 enc;
    REQUIRE(enc.init(key, nonce));
    std::vector<uint8_t> ciphertext(DATA_SIZE);
    enc.process(plaintext.data(), ciphertext.data(), DATA_SIZE);

    // Seek to exactly block boundary (256)
    constexpr size_t SEEK_OFFSET = 256; // 256 % 16 == 0
    AesCtr256 dec;
    REQUIRE(dec.init(key, nonce));
    dec.seek(SEEK_OFFSET);

    size_t remaining = DATA_SIZE - SEEK_OFFSET;
    std::vector<uint8_t> decrypted(remaining);
    dec.process(ciphertext.data() + SEEK_OFFSET, decrypted.data(), remaining);

    REQUIRE(std::memcmp(plaintext.data() + SEEK_OFFSET, decrypted.data(), remaining) == 0);
}

TEST_CASE("AES-256-CTR multiple blocks continuity", "[crypto][aes]") {
    // Verify that encrypting 160 bytes in one call matches
    // encrypting 3 chunks of different sizes
    constexpr size_t DATA_SIZE = 160;
    std::vector<uint8_t> plaintext(DATA_SIZE);
    for (size_t i = 0; i < DATA_SIZE; ++i) {
        plaintext[i] = static_cast<uint8_t>(i % 256);
    }

    uint8_t key[32] = {};
    uint8_t nonce[12] = {};

    // Encrypt all at once
    AesCtr256 enc1;
    REQUIRE(enc1.init(key, nonce));
    std::vector<uint8_t> ciphertext1(DATA_SIZE);
    enc1.process(plaintext.data(), ciphertext1.data(), DATA_SIZE);

    // Encrypt in chunks
    AesCtr256 enc2;
    REQUIRE(enc2.init(key, nonce));
    std::vector<uint8_t> ciphertext2(DATA_SIZE);
    // Chunk 1: 30 bytes
    enc2.process(plaintext.data(), ciphertext2.data(), 30);
    // Chunk 2: 80 bytes
    enc2.process(plaintext.data() + 30, ciphertext2.data() + 30, 80);
    // Chunk 3: 50 bytes
    enc2.process(plaintext.data() + 110, ciphertext2.data() + 110, 50);

    REQUIRE(std::memcmp(ciphertext1.data(), ciphertext2.data(), DATA_SIZE) == 0);
}

TEST_CASE("HMAC-SHA256 RFC 4231 Test Case 1", "[crypto][hmac]") {
    // Key: 0x0b repeated 20 times
    uint8_t key[20];
    std::memset(key, 0x0b, 20);

    // Data: "Hi There"
    const uint8_t data[] = "Hi There";
    size_t dataLen = 8;

    // Expected: b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
    auto expected = hexToBytes("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    uint8_t result[32];
    HmacSha256::compute(key, 20, data, dataLen, result);

    REQUIRE(std::memcmp(result, expected.data(), 32) == 0);
}

TEST_CASE("HMAC-SHA256 RFC 4231 Test Case 2", "[crypto][hmac]") {
    // Key: "Jefe"
    const uint8_t key[] = "Jefe";
    size_t keyLen = 4;

    // Data: "what do ya want for nothing?"
    const uint8_t data[] = "what do ya want for nothing?";
    size_t dataLen = 28;

    // Expected: 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
    auto expected = hexToBytes("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    uint8_t result[32];
    HmacSha256::compute(key, keyLen, data, dataLen, result);

    REQUIRE(std::memcmp(result, expected.data(), 32) == 0);
}

TEST_CASE("HMAC-SHA256 streaming matches one-shot", "[crypto][hmac]") {
    uint8_t key[32];
    std::memset(key, 0xAA, 32);
    const uint8_t data[] = "test data for streaming HMAC verification";
    size_t dataLen = std::strlen(reinterpret_cast<const char*>(data));

    // One-shot
    uint8_t oneShot[32];
    HmacSha256::compute(key, 32, data, dataLen, oneShot);

    // Streaming: split data
    HmacSha256 hmac;
    hmac.init(key, 32);
    hmac.update(data, 10);        // First 10 bytes
    hmac.update(data + 10, dataLen - 10); // Rest
    uint8_t streaming[32];
    hmac.final(streaming);

    REQUIRE(std::memcmp(oneShot, streaming, 32) == 0);
}

TEST_CASE("PBKDF2-HMAC-SHA256 RFC 8018 Test Vector c=1", "[crypto][pbkdf2]") {
    const uint8_t password[] = "password";
    const uint8_t salt[] = "salt";

    // Expected: 120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b
    auto expected = hexToBytes("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    uint8_t result[32];
    pbkdf2_hmac_sha256(password, 8, salt, 4, 1, result, 32);

    REQUIRE(std::memcmp(result, expected.data(), 32) == 0);
}

TEST_CASE("PBKDF2-HMAC-SHA256 RFC 8018 Test Vector c=2", "[crypto][pbkdf2]") {
    const uint8_t password[] = "password";
    const uint8_t salt[] = "salt";

    // Expected: ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43
    auto expected = hexToBytes("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");

    uint8_t result[32];
    pbkdf2_hmac_sha256(password, 8, salt, 4, 2, result, 32);

    REQUIRE(std::memcmp(result, expected.data(), 32) == 0);
}

TEST_CASE("PBKDF2-HMAC-SHA256 determinism", "[crypto][pbkdf2]") {
    const uint8_t password[] = "test_password";
    const uint8_t salt[] = "test_salt_value";

    uint8_t result1[64];
    uint8_t result2[64];
    pbkdf2_hmac_sha256(password, 13, salt, 15, 100, result1, 64);
    pbkdf2_hmac_sha256(password, 13, salt, 15, 100, result2, 64);

    REQUIRE(std::memcmp(result1, result2, 64) == 0);
}

TEST_CASE("PBKDF2-HMAC-SHA256 different passwords produce different keys", "[crypto][pbkdf2]") {
    const uint8_t salt[] = "salt";
    uint8_t result1[32];
    uint8_t result2[32];

    const uint8_t pass1[] = "password1";
    const uint8_t pass2[] = "password2";
    pbkdf2_hmac_sha256(pass1, 9, salt, 4, 100, result1, 32);
    pbkdf2_hmac_sha256(pass2, 9, salt, 4, 100, result2, 32);

    bool different = false;
    for (int i = 0; i < 32; ++i) {
        if (result1[i] != result2[i]) { different = true; break; }
    }
    REQUIRE(different);
}

TEST_CASE("PBKDF2-HMAC-SHA256 variable output length", "[crypto][pbkdf2]") {
    const uint8_t password[] = "password";
    const uint8_t salt[] = "salt";

    // Derive 64 bytes (our use case: 32 AES + 32 HMAC)
    uint8_t result[64];
    pbkdf2_hmac_sha256(password, 8, salt, 4, 1, result, 64);

    // First 32 bytes should match c=1 test vector
    auto expected32 = hexToBytes("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    REQUIRE(std::memcmp(result, expected32.data(), 32) == 0);

    // Second 32 bytes (c=1, block 2) should be different from first
    bool different = false;
    for (int i = 0; i < 32; ++i) {
        if (result[i] != result[i + 32]) { different = true; break; }
    }
    REQUIRE(different);
}

TEST_CASE("Constant-time compare equal", "[crypto][constant_time]") {
    uint8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t b[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    REQUIRE(constant_time_compare(a, b, 16) == true);
}

TEST_CASE("Constant-time compare different by one byte", "[crypto][constant_time]") {
    uint8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t b[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99};
    REQUIRE(constant_time_compare(a, b, 16) == false);
}

TEST_CASE("Constant-time compare empty", "[crypto][constant_time]") {
    REQUIRE(constant_time_compare(nullptr, nullptr, 0) == true);
}

TEST_CASE("Constant-time compare single byte different", "[crypto][constant_time]") {
    uint8_t a[1] = {42};
    uint8_t b[1] = {43};
    REQUIRE(constant_time_compare(a, b, 1) == false);
}

TEST_CASE("Constant-time compare completely different", "[crypto][constant_time]") {
    uint8_t a[32];
    uint8_t b[32];
    std::memset(a, 0, 32);
    std::memset(b, 0xFF, 32);
    REQUIRE(constant_time_compare(a, b, 32) == false);
}

TEST_CASE("SecureBytes basic usage", "[crypto][secure]") {
    SecureBytes data;
    data.resize(32);
    std::memset(data.data(), 0x42, 32);
    REQUIRE(data.size() == 32);
    REQUIRE(data[0] == 0x42);
    REQUIRE(data[31] == 0x42);
}

TEST_CASE("SecureBytes resize preserves data", "[crypto][secure]") {
    SecureBytes data;
    data.resize(16);
    std::memset(data.data(), 0xAB, 16);

    data.resize(32);
    // First 16 bytes should be preserved
    for (size_t i = 0; i < 16; ++i) {
        REQUIRE(data[i] == 0xAB);
    }
}

TEST_CASE("SecureBytes move construction", "[crypto][secure]") {
    SecureBytes data;
    data.resize(32);
    std::memset(data.data(), 0x55, 32);

    SecureBytes moved(std::move(data));
    REQUIRE(moved.size() == 32);
    REQUIRE(moved[0] == 0x55);
    REQUIRE(data.size() == 0); // Moved-from should be empty
}
