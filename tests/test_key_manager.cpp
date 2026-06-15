#include <catch2/catch_test_macros.hpp>

#include "KeyManager.h"
#include "constant_time.h"
#include "hlplayer/Result.h"

#include <cstring>

using namespace hlplayer;
using namespace hlplayer::crypto;

TEST_CASE("Password mode: same inputs produce same DerivedKeys", "[key_manager]") {
    const std::string password = "test_password_123";
    uint8_t salt[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint32_t iterations = 1000;

    auto keys1 = KeyManager::deriveFromPassword(password, salt, iterations);
    auto keys2 = KeyManager::deriveFromPassword(password, salt, iterations);

    REQUIRE(keys1.aesKey.size() == 32);
    REQUIRE(keys1.hmacKey.size() == 32);
    REQUIRE(constant_time_compare(keys1.aesKey.data(), keys2.aesKey.data(), 32));
    REQUIRE(constant_time_compare(keys1.hmacKey.data(), keys2.hmacKey.data(), 32));
}

TEST_CASE("Password mode: different passwords produce different keys", "[key_manager]") {
    uint8_t salt[16] = {};
    uint32_t iterations = 100;

    auto keys1 = KeyManager::deriveFromPassword("password_alpha", salt, iterations);
    auto keys2 = KeyManager::deriveFromPassword("password_beta", salt, iterations);

    bool aesDiffers = !constant_time_compare(keys1.aesKey.data(), keys2.aesKey.data(), 32);
    bool hmacDiffers = !constant_time_compare(keys1.hmacKey.data(), keys2.hmacKey.data(), 32);
    REQUIRE(aesDiffers);
    REQUIRE(hmacDiffers);
}

TEST_CASE("Password mode: AES and HMAC keys are always different", "[key_manager]") {
    uint8_t salt[16] = {};
    auto keys = KeyManager::deriveFromPassword("any_password", salt, 100);

    bool same = constant_time_compare(keys.aesKey.data(), keys.hmacKey.data(), 32);
    REQUIRE_FALSE(same);
}

TEST_CASE("Raw key mode: same rawKey produces same DerivedKeys", "[key_manager]") {
    SecureBytes rawKey(32);
    for (size_t i = 0; i < 32; ++i) rawKey[i] = static_cast<uint8_t>(i);

    auto keys1 = KeyManager::deriveFromRawKey(rawKey);
    auto keys2 = KeyManager::deriveFromRawKey(rawKey);

    REQUIRE(keys1.aesKey.size() == 32);
    REQUIRE(keys1.hmacKey.size() == 32);
    REQUIRE(constant_time_compare(keys1.aesKey.data(), keys2.aesKey.data(), 32));
    REQUIRE(constant_time_compare(keys1.hmacKey.data(), keys2.hmacKey.data(), 32));
}

TEST_CASE("Raw key mode: different rawKeys produce different keys", "[key_manager]") {
    SecureBytes rawKey1(32, 0x11);
    SecureBytes rawKey2(32, 0x22);

    auto keys1 = KeyManager::deriveFromRawKey(rawKey1);
    auto keys2 = KeyManager::deriveFromRawKey(rawKey2);

    REQUIRE_FALSE(constant_time_compare(keys1.aesKey.data(), keys2.aesKey.data(), 32));
    REQUIRE_FALSE(constant_time_compare(keys1.hmacKey.data(), keys2.hmacKey.data(), 32));
}

TEST_CASE("Raw key mode: AES and HMAC keys are always different", "[key_manager]") {
    SecureBytes rawKey(32, 0x42);
    auto keys = KeyManager::deriveFromRawKey(rawKey);

    REQUIRE_FALSE(constant_time_compare(keys.aesKey.data(), keys.hmacKey.data(), 32));
}

TEST_CASE("formatKeyString and parseKeyString round-trip preserves all 32 bytes", "[key_manager]") {
    SecureBytes original(32);
    for (size_t i = 0; i < 32; ++i) original[i] = static_cast<uint8_t>(i * 7 + 3);

    std::string formatted = KeyManager::formatKeyString(original);
    auto parsed = KeyManager::parseKeyString(formatted);

    REQUIRE(parsed.hasValue());
    REQUIRE(parsed.value().size() == 32);
    REQUIRE(constant_time_compare(original.data(), parsed.value().data(), 32));
}

TEST_CASE("formatKeyString produces expected format with 13 groups", "[key_manager]") {
    SecureBytes key(32, 0x00);
    std::string formatted = KeyManager::formatKeyString(key);

    // 52 base32 chars + 12 hyphens = 64 chars
    REQUIRE(formatted.size() == 64);

    // Count hyphens
    int hyphenCount = 0;
    for (char c : formatted) {
        if (c == '-') ++hyphenCount;
    }
    REQUIRE(hyphenCount == 12);

    // All chars must be A-Z, 2-7, or hyphen
    for (char c : formatted) {
        bool valid = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7') || c == '-';
        REQUIRE(valid);
    }

    // Must not contain lowercase or padding
    for (char c : formatted) {
        REQUIRE(c != '=');
        REQUIRE(!(c >= 'a' && c <= 'z'));
    }
}

TEST_CASE("parseKeyString rejects invalid format: wrong length", "[key_manager]") {
    auto result = KeyManager::parseKeyString("AAAA-BBBB");
    REQUIRE(result.hasError());
}

TEST_CASE("parseKeyString rejects invalid characters", "[key_manager]") {
    // Valid length but contains '1' and '0' which aren't base32
    std::string invalid(64, 'A');
    invalid[0] = '1';
    invalid[1] = '0';

    auto result = KeyManager::parseKeyString(invalid);
    REQUIRE(result.hasError());
}

TEST_CASE("parseKeyString rejects lowercase characters", "[key_manager]") {
    std::string invalid(64, 'a');
    auto result = KeyManager::parseKeyString(invalid);
    REQUIRE(result.hasError());
}

TEST_CASE("parseKeyString rejects empty string", "[key_manager]") {
    auto result = KeyManager::parseKeyString("");
    REQUIRE(result.hasError());
}

TEST_CASE("generateRawKey produces 32 bytes", "[key_manager]") {
    auto key = KeyManager::generateRawKey();
    REQUIRE(key.size() == 32);
}

TEST_CASE("generateRawKey is not all zeros", "[key_manager]") {
    auto key = KeyManager::generateRawKey();
    bool allZero = true;
    for (size_t i = 0; i < 32; ++i) {
        if (key[i] != 0) { allZero = false; break; }
    }
    REQUIRE_FALSE(allZero);
}

TEST_CASE("Two generateRawKey calls produce different keys", "[key_manager]") {
    auto key1 = KeyManager::generateRawKey();
    auto key2 = KeyManager::generateRawKey();
    REQUIRE_FALSE(constant_time_compare(key1.data(), key2.data(), 32));
}

TEST_CASE("Password iterations clamped to HLV_MAX_ITERATIONS", "[key_manager]") {
    uint8_t salt[16] = {};
    uint32_t huge = 5000000; // above max of 2000000

    auto keys1 = KeyManager::deriveFromPassword("password", salt, huge);
    auto keys2 = KeyManager::deriveFromPassword("password", salt, 2000000);

    // Both should produce same result (clamped to max)
    REQUIRE(constant_time_compare(keys1.aesKey.data(), keys2.aesKey.data(), 32));
    REQUIRE(constant_time_compare(keys1.hmacKey.data(), keys2.hmacKey.data(), 32));
}

TEST_CASE("Raw key derivation is deterministic (HKDF)", "[key_manager]") {
    SecureBytes rawKey(32);
    for (size_t i = 0; i < 32; ++i) rawKey[i] = static_cast<uint8_t>(i ^ 0xAA);

    auto keys1 = KeyManager::deriveFromRawKey(rawKey);
    auto keys2 = KeyManager::deriveFromRawKey(rawKey);
    auto keys3 = KeyManager::deriveFromRawKey(rawKey);

    REQUIRE(constant_time_compare(keys1.aesKey.data(), keys3.aesKey.data(), 32));
    REQUIRE(constant_time_compare(keys1.hmacKey.data(), keys3.hmacKey.data(), 32));
}
