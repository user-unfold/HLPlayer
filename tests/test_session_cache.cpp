#include <catch2/catch_test_macros.hpp>
#include "SessionKeyCache.h"
#include "HmacSha256.h"
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>

using namespace hlplayer::crypto;

// Helper: create test header and compute HMAC
static void createTestHeaderAndHmac(const uint8_t* hmacKey, uint8_t* headerBytes80, uint8_t* headerHmac32) {
    std::memset(headerBytes80, 0xAA, 80);
    HmacSha256::compute(hmacKey, 32, headerBytes80, 80, headerHmac32);
}

// Helper: create test keys
static SecureBytes createTestAesKey(uint8_t pattern) {
    SecureBytes key(32);
    std::memset(key.data(), pattern, 32);
    return key;
}

static SecureBytes createTestHmacKey(uint8_t pattern) {
    SecureBytes key(32);
    std::memset(key.data(), pattern, 32);
    return key;
}

TEST_CASE("tryFindKey returns cached key on match", "[session_cache]") {
    SessionKeyCache cache;

    SecureBytes aesKey = createTestAesKey(0x42);
    SecureBytes hmacKey = createTestHmacKey(0x43);

    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];
    createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

    cache.put("test.hlvideo", aesKey, hmacKey);

    auto found = cache.tryFindKey(headerBytes80, headerHmac32);
    REQUIRE(found.has_value());
    REQUIRE(found->size() == 32);
    REQUIRE(std::memcmp(found->data(), aesKey.data(), 32) == 0);
}

TEST_CASE("tryFindKey returns nullopt on non-matching header", "[session_cache]") {
    SessionKeyCache cache;

    SecureBytes aesKey = createTestAesKey(0x42);
    SecureBytes hmacKey = createTestHmacKey(0x43);

    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];
    createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

    cache.put("test.hlvideo", aesKey, hmacKey);

    // Create a different header/HMAC pair
    uint8_t differentHmacKey[32];
    std::memset(differentHmacKey, 0xFF, 32);
    uint8_t differentHeaderBytes80[80];
    uint8_t differentHeaderHmac32[32];
    createTestHeaderAndHmac(differentHmacKey, differentHeaderBytes80, differentHeaderHmac32);

    auto found = cache.tryFindKey(differentHeaderBytes80, differentHeaderHmac32);
    REQUIRE(!found.has_value());
}

TEST_CASE("tryFindKey returns nullopt on empty cache", "[session_cache]") {
    SessionKeyCache cache;

    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];
    uint8_t hmacKey[32];
    std::memset(hmacKey, 0x42, 32);
    createTestHeaderAndHmac(hmacKey, headerBytes80, headerHmac32);

    auto found = cache.tryFindKey(headerBytes80, headerHmac32);
    REQUIRE(!found.has_value());
}

TEST_CASE("LRU eviction keeps only 8 entries", "[session_cache]") {
    SessionKeyCache cache;

    // Add 10 entries
    for (int i = 0; i < 10; ++i) {
        SecureBytes aesKey = createTestAesKey(i);
        SecureBytes hmacKey = createTestHmacKey(i);

        uint8_t headerBytes80[80];
        uint8_t headerHmac32[32];
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

        cache.put("file" + std::to_string(i) + ".hlvideo", aesKey, hmacKey);

        // Small delay to ensure different timestamps
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // First 2 entries should be evicted
    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];

    // Entry 0 should be evicted
    {
        SecureBytes hmacKey = createTestHmacKey(0);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(!found.has_value());
    }

    // Entry 1 should be evicted
    {
        SecureBytes hmacKey = createTestHmacKey(1);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(!found.has_value());
    }

    // Entries 2-9 should be present
    for (int i = 2; i < 10; ++i) {
        SecureBytes hmacKey = createTestHmacKey(i);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(found.has_value());
    }
}

TEST_CASE("LRU eviction reorders entries by use", "[session_cache]") {
    SessionKeyCache cache;

    // Add 8 entries (fill cache)
    for (int i = 0; i < 8; ++i) {
        SecureBytes aesKey = createTestAesKey(i);
        SecureBytes hmacKey = createTestHmacKey(i);

        uint8_t headerBytes80[80];
        uint8_t headerHmac32[32];
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

        cache.put("file" + std::to_string(i) + ".hlvideo", aesKey, hmacKey);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Access entry 0 (should make it most recently used)
    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];
    {
        SecureBytes hmacKey = createTestHmacKey(0);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(found.has_value());
    }

    // Add 9th entry (should evict entry 1, not entry 0)
    {
        SecureBytes aesKey = createTestAesKey(8);
        SecureBytes hmacKey = createTestHmacKey(8);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        cache.put("file8.hlvideo", aesKey, hmacKey);
    }

    // Entry 0 should still be present
    {
        SecureBytes hmacKey = createTestHmacKey(0);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(found.has_value());
    }

    // Entry 1 should be evicted
    {
        SecureBytes hmacKey = createTestHmacKey(1);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(!found.has_value());
    }
}

TEST_CASE("Concurrent put and tryFindKey is thread-safe", "[session_cache]") {
    SessionKeyCache cache;
    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 100;

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&cache, t, ITERATIONS]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                SecureBytes aesKey = createTestAesKey((t * ITERATIONS + i) % 256);
                SecureBytes hmacKey = createTestHmacKey((t * ITERATIONS + i) % 256);

                uint8_t headerBytes80[80];
                uint8_t headerHmac32[32];
                createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

                cache.put("file" + std::to_string(t) + "_" + std::to_string(i) + ".hlvideo", aesKey, hmacKey);

                auto found = cache.tryFindKey(headerBytes80, headerHmac32);
                // May or may not find depending on eviction, but should not crash
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

TEST_CASE("clear removes all entries", "[session_cache]") {
    SessionKeyCache cache;

    // Add several entries
    for (int i = 0; i < 5; ++i) {
        SecureBytes aesKey = createTestAesKey(i);
        SecureBytes hmacKey = createTestHmacKey(i);

        uint8_t headerBytes80[80];
        uint8_t headerHmac32[32];
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

        cache.put("file" + std::to_string(i) + ".hlvideo", aesKey, hmacKey);
    }

    cache.clear();

    // All entries should be gone
    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];

    for (int i = 0; i < 5; ++i) {
        SecureBytes hmacKey = createTestHmacKey(i);
        createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);
        auto found = cache.tryFindKey(headerBytes80, headerHmac32);
        REQUIRE(!found.has_value());
    }
}

TEST_CASE("Multiple files with same key can all be found", "[session_cache]") {
    SessionKeyCache cache;

    SecureBytes aesKey = createTestAesKey(0x42);
    SecureBytes hmacKey = createTestHmacKey(0x43);

    uint8_t headerBytes80[80];
    uint8_t headerHmac32[32];
    createTestHeaderAndHmac(hmacKey.data(), headerBytes80, headerHmac32);

    // Same key used for multiple files
    cache.put("file1.hlvideo", aesKey, hmacKey);
    cache.put("file2.hlvideo", aesKey, hmacKey);
    cache.put("file3.hlvideo", aesKey, hmacKey);

    // Should find the key for the header
    auto found = cache.tryFindKey(headerBytes80, headerHmac32);
    REQUIRE(found.has_value());
    REQUIRE(std::memcmp(found->data(), aesKey.data(), 32) == 0);
}

TEST_CASE("Different keys in cache don't interfere", "[session_cache]") {
    SessionKeyCache cache;

    SecureBytes aesKey1 = createTestAesKey(0x42);
    SecureBytes hmacKey1 = createTestHmacKey(0x43);

    SecureBytes aesKey2 = createTestAesKey(0x84);
    SecureBytes hmacKey2 = createTestHmacKey(0x86);

    // Header bytes must differ between files so HMAC with wrong key doesn't match
    uint8_t headerBytes1[80];
    std::memset(headerBytes1, 0xAA, 80);
    uint8_t headerHmac1[32];
    HmacSha256::compute(hmacKey1.data(), 32, headerBytes1, 80, headerHmac1);

    uint8_t headerBytes2[80];
    std::memset(headerBytes2, 0xBB, 80);
    uint8_t headerHmac2[32];
    HmacSha256::compute(hmacKey2.data(), 32, headerBytes2, 80, headerHmac2);

    cache.put("file1.hlvideo", aesKey1, hmacKey1);
    cache.put("file2.hlvideo", aesKey2, hmacKey2);

    // Find first key
    auto found1 = cache.tryFindKey(headerBytes1, headerHmac1);
    REQUIRE(found1.has_value());
    REQUIRE(std::memcmp(found1->data(), aesKey1.data(), 32) == 0);

    // Find second key
    auto found2 = cache.tryFindKey(headerBytes2, headerHmac2);
    REQUIRE(found2.has_value());
    REQUIRE(std::memcmp(found2->data(), aesKey2.data(), 32) == 0);

    // Wrong combination should not match
    auto foundWrong = cache.tryFindKey(headerBytes1, headerHmac2);
    REQUIRE(!foundWrong.has_value());
}