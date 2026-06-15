#include <catch2/catch_test_macros.hpp>
#include "FileEncryptor.h"
#include "HlvHeader.h"
#include "DecryptingAVIOContext.h"
#include "KeyManager.h"
#include "HmacSha256.h"
#include "constant_time.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <random>

extern "C" {
#include <libavformat/avio.h>
}

using namespace hlplayer::crypto;

namespace {

// Helper: create a test file with patterned data
static std::string createTestFile(size_t size) {
    const char* tmpName = std::tmpnam(nullptr);
    std::string path(tmpName);
    path += ".test";

    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif

    REQUIRE(f != nullptr);

    // Write patterned data (byte value = position % 256)
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = static_cast<uint8_t>(i % 256);
        std::fwrite(&byte, 1, 1, f);
    }

    std::fclose(f);
    return path;
}

// Helper: read entire file into vector
static std::vector<uint8_t> readFileContents(const std::string& path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif

    REQUIRE(f != nullptr);

    // Get file size
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> data(size);
    size_t bytesRead = std::fread(data.data(), 1, size, f);
    REQUIRE(bytesRead == static_cast<size_t>(size));

    std::fclose(f);
    return data;
}

// Helper: delete file if exists
static void deleteFile(const std::string& path) {
    std::remove(path.c_str());
}

} // anonymous namespace

// =============================================================================
// SCENARIO 1: Full Encrypt → Decrypt Round-Trip
// =============================================================================
TEST_CASE("Scenario 1: Full encrypt-decrypt round-trip with password", "[crypto][integration][scenario1]") {
    const size_t fileSize = 1024; // 1KB
    const std::string password = "testpassword123";

    // Step 1: Create test file with patterned data
    std::string inputPath = createTestFile(fileSize);
    std::vector<uint8_t> originalData = readFileContents(inputPath);

    // Step 2: Encrypt to .hlv
    std::string outputPath = inputPath + ".hlv";
    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = password;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);

    REQUIRE(encryptResult.success == true);
    REQUIRE(encryptResult.error == EncryptError::None);

    // Step 3: Read .hlv header to extract salt, nonce, iterations, originalSize
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    REQUIRE(hlvData.size() >= HLV_HEADER_SIZE);

    // Verify magic
    REQUIRE(std::memcmp(hlvData.data(), HLV_MAGIC, 8) == 0);

    // Parse header
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);

    // Step 4: Derive keys using KeyManager::deriveFromPassword
    auto keys = KeyManager::deriveFromPassword(password, header.salt, header.pbkdf2Iterations);

    REQUIRE(keys.aesKey.size() == 32);
    REQUIRE(keys.hmacKey.size() == 32);

    // Step 5: Create DecryptingAVIOContext with the correct keys
    DecryptConfig decryptConfig;
    decryptConfig.filePath = outputPath;
    decryptConfig.aesKey = keys.aesKey;
    std::memcpy(decryptConfig.nonce, header.nonce, HLV_NONCE_SIZE);
    decryptConfig.originalSize = header.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(decryptConfig);
    REQUIRE(ctx != nullptr);

    // Step 6: Read all data through the AVIOContext
    std::vector<uint8_t> decryptedData(fileSize);
    size_t totalRead = 0;
    while (totalRead < fileSize) {
        int ret = avio_read(ctx, decryptedData.data() + totalRead,
                            static_cast<int>(fileSize - totalRead));
        if (ret <= 0) break;
        totalRead += static_cast<size_t>(ret);
    }

    REQUIRE(totalRead == fileSize);

    // Step 7: Compare with original — should be byte-identical
    bool dataMatch = std::memcmp(decryptedData.data(), originalData.data(), fileSize) == 0;
    REQUIRE(dataMatch == true);

    DecryptingAVIOContext::destroy(ctx);
    deleteFile(inputPath);
    deleteFile(outputPath);
}

// =============================================================================
// SCENARIO 2: Wrong Password Rejection
// =============================================================================
TEST_CASE("Scenario 2: Wrong password is rejected via HMAC", "[crypto][integration][scenario2]") {
    const size_t fileSize = 1024;
    const std::string correctPassword = "correct_password";
    const std::string wrongPassword = "wrong_password";

    // Step 1: Encrypt a file with correct password
    std::string inputPath = createTestFile(fileSize);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = correctPassword;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);
    REQUIRE(encryptResult.success == true);

    // Step 2: Read header
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);

    // Step 3: Try to derive keys with wrong password
    auto wrongKeys = KeyManager::deriveFromPassword(wrongPassword, header.salt, header.pbkdf2Iterations);

    REQUIRE(wrongKeys.aesKey.size() == 32);
    REQUIRE(wrongKeys.hmacKey.size() == 32);

    // Step 4: Compute HMAC with wrong hmacKey and compare with stored header HMAC
    // The header HMAC covers bytes 0x00-0x4F (80 bytes)
    std::vector<uint8_t> headerPayload(hlvData.begin(), hlvData.begin() + 0x50);
    uint8_t computedHmac[32];
    HmacSha256::compute(wrongKeys.hmacKey.data(), wrongKeys.hmacKey.size(),
                        headerPayload.data(), headerPayload.size(), computedHmac);

    // Step 5: Verify they DON'T match
    bool hmacMatch = constant_time_compare(computedHmac, header.headerHmac, HLV_HMAC_SIZE);
    REQUIRE(hmacMatch == false);

    deleteFile(inputPath);
    deleteFile(outputPath);
}

// =============================================================================
// SCENARIO 3: Seek in Encrypted Data
// =============================================================================
TEST_CASE("Scenario 3: Non-block-aligned seek and read", "[crypto][integration][scenario3]") {
    const size_t fileSize = 64 * 1024; // 64KB
    const std::string password = "testpassword123";

    // Step 1: Encrypt a 64KB test file
    std::string inputPath = createTestFile(fileSize);
    std::vector<uint8_t> originalData = readFileContents(inputPath);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = password;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);
    REQUIRE(encryptResult.success == true);

    // Step 2: Read header and derive keys
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);
    auto keys = KeyManager::deriveFromPassword(password, header.salt, header.pbkdf2Iterations);

    // Step 3: Create DecryptingAVIOContext
    DecryptConfig decryptConfig;
    decryptConfig.filePath = outputPath;
    decryptConfig.aesKey = keys.aesKey;
    std::memcpy(decryptConfig.nonce, header.nonce, HLV_NONCE_SIZE);
    decryptConfig.originalSize = header.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(decryptConfig);
    REQUIRE(ctx != nullptr);

    // Step 4: Seek to offset 5000 (non-block-aligned: 5000 % 16 = 8)
    int64_t ret = avio_seek(ctx, 5000, SEEK_SET);
    REQUIRE(ret == 5000);

    // Step 5: Read 100 bytes
    std::vector<uint8_t> buf1(100);
    int nread = avio_read(ctx, buf1.data(), 100);
    REQUIRE(nread == 100);

    // Step 6: Compare with original[5000:5100]
    bool match1 = std::memcmp(buf1.data(), originalData.data() + 5000, 100) == 0;
    REQUIRE(match1 == true);

    // Step 7: Seek to offset 32000 (block-aligned: 32000 % 16 = 0)
    ret = avio_seek(ctx, 32000, SEEK_SET);
    REQUIRE(ret == 32000);

    // Step 8: Read 100 bytes
    std::vector<uint8_t> buf2(100);
    nread = avio_read(ctx, buf2.data(), 100);
    REQUIRE(nread == 100);

    // Step 9: Compare with original[32000:32100]
    bool match2 = std::memcmp(buf2.data(), originalData.data() + 32000, 100) == 0;
    REQUIRE(match2 == true);

    DecryptingAVIOContext::destroy(ctx);
    deleteFile(inputPath);
    deleteFile(outputPath);
}

// =============================================================================
// SCENARIO 4: Raw Key Mode Round-Trip
// =============================================================================
TEST_CASE("Scenario 4: Raw key mode encrypt-decrypt round-trip", "[crypto][integration][scenario4]") {
    const size_t fileSize = 2048;
    const std::string password = "testpassword123";

    // Step 1: Generate a raw key
    SecureBytes rawKey = KeyManager::generateRawKey();
    REQUIRE(rawKey.size() == 32);

    // Step 2: Format it
    std::string formattedKey = KeyManager::formatKeyString(rawKey);
    REQUIRE(formattedKey.size() == 64); // 52 base32 chars + 12 hyphens

    // Step 3: Parse it back
    auto parsedResult = KeyManager::parseKeyString(formattedKey);
    REQUIRE(parsedResult.hasValue());
    SecureBytes parsedKey = parsedResult.value();

    // Step 4: Verify round-trip preserves bytes
    bool roundTripMatch = constant_time_compare(rawKey.data(), parsedKey.data(), 32);
    REQUIRE(roundTripMatch == true);

    // Step 5: Create and encrypt a file with this raw key
    std::string inputPath = createTestFile(fileSize);
    std::vector<uint8_t> originalData = readFileContents(inputPath);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig encryptConfig;
    encryptConfig.inputPath = inputPath;
    encryptConfig.outputPath = outputPath;
    encryptConfig.keyMode = KeyMode::RawKey;
    encryptConfig.rawKey = rawKey;
    encryptConfig.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(encryptConfig);
    REQUIRE(encryptResult.success == true);

    // Step 6: Decrypt using the same raw key (deriveFromRawKey)
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);

    auto keys = KeyManager::deriveFromRawKey(rawKey);
    REQUIRE(keys.aesKey.size() == 32);
    REQUIRE(keys.hmacKey.size() == 32);

    DecryptConfig decryptConfig;
    decryptConfig.filePath = outputPath;
    decryptConfig.aesKey = keys.aesKey;
    std::memcpy(decryptConfig.nonce, header.nonce, HLV_NONCE_SIZE);
    decryptConfig.originalSize = header.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(decryptConfig);
    REQUIRE(ctx != nullptr);

    // Step 7: Read all decrypted data
    std::vector<uint8_t> decryptedData(fileSize);
    size_t totalRead = 0;
    while (totalRead < fileSize) {
        int ret = avio_read(ctx, decryptedData.data() + totalRead,
                            static_cast<int>(fileSize - totalRead));
        if (ret <= 0) break;
        totalRead += static_cast<size_t>(ret);
    }

    REQUIRE(totalRead == fileSize);

    // Step 8: Compare with original
    bool dataMatch = std::memcmp(decryptedData.data(), originalData.data(), fileSize) == 0;
    REQUIRE(dataMatch == true);

    DecryptingAVIOContext::destroy(ctx);
    deleteFile(inputPath);
    deleteFile(outputPath);
}

// =============================================================================
// SCENARIO 5: .hlv File Structure Validation
// =============================================================================
TEST_CASE("Scenario 5: .hlv file structure and HMAC validation", "[crypto][integration][scenario5]") {
    const size_t fileSize = 1024;
    const std::string password = "testpassword123";

    // Step 1: Encrypt a file
    std::string inputPath = createTestFile(fileSize);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = password;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);
    REQUIRE(encryptResult.success == true);

    // Step 2: Read the first 8 bytes — verify magic = "HLPENC\0\0"
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    REQUIRE(hlvData.size() >= 8);
    REQUIRE(std::memcmp(hlvData.data(), HLV_MAGIC, 8) == 0);

    // Step 3: Parse header to get keys for HMAC verification
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);
    auto keys = KeyManager::deriveFromPassword(password, header.salt, header.pbkdf2Iterations);

    // Step 4: Read bytes 0x00-0x4F (80 bytes) and compute HMAC with derived hmacKey
    std::vector<uint8_t> headerPayload(hlvData.begin(), hlvData.begin() + 0x50);
    uint8_t computedHmac[32];
    HmacSha256::compute(keys.hmacKey.data(), keys.hmacKey.size(),
                        headerPayload.data(), headerPayload.size(), computedHmac);

    // Step 5: Read bytes 0x50-0x6F (32 bytes) and compare with computed HMAC
    std::vector<uint8_t> storedHmac(hlvData.begin() + 0x50, hlvData.begin() + 0x70);

    bool hmacMatch = constant_time_compare(computedHmac, storedHmac.data(), HLV_HMAC_SIZE);
    REQUIRE(hmacMatch == true);

    deleteFile(inputPath);
    deleteFile(outputPath);
}

// =============================================================================
// SCENARIO 6: Non-.hlv File Rejection
// =============================================================================
TEST_CASE("Scenario 6: Extension and file type validation", "[crypto][integration][scenario6]") {
    // Step 1: hasHlvExtension("test.mp4") → should return false
    REQUIRE(hasHlvExtension("test.mp4") == false);
    REQUIRE(hasHlvExtension("video.MKV") == false);
    REQUIRE(hasHlvExtension("movie.avi") == false);

    // Step 2: hasHlvExtension("test.hlv") → should return true
    REQUIRE(hasHlvExtension("test.hlv") == true);
    REQUIRE(hasHlvExtension("video.HLV") == true);
    REQUIRE(hasHlvExtension("movie.Hlv") == true);

    // Step 3: Create a regular file
    std::string regularFile = createTestFile(512);
    std::string hlvFile = regularFile + ".hlv";

    // Step 4: isHlvFile on a regular .mp4 → should return false
    REQUIRE(isHlvFile(regularFile) == false);

    // Step 5: Encrypt it to create a real .hlv file
    EncryptConfig config;
    config.inputPath = regularFile;
    config.outputPath = hlvFile;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);
    REQUIRE(result.success == true);

    // Step 6: isHlvFile on a .hlv file → should return true
    REQUIRE(isHlvFile(hlvFile) == true);

    deleteFile(regularFile);
    deleteFile(hlvFile);
}

// =============================================================================
// Additional Edge Cases
// =============================================================================

TEST_CASE("Edge case: Encrypt and decrypt empty file", "[crypto][integration][edge_case]") {
    const std::string password = "testpassword123";

    // Create empty file
    const char* tmpName = std::tmpnam(nullptr);
    std::string inputPath(tmpName);
    inputPath += ".test";

    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, inputPath.c_str(), "wb");
#else
    f = std::fopen(inputPath.c_str(), "wb");
#endif
    REQUIRE(f != nullptr);
    std::fclose(f);

    std::string outputPath = inputPath + ".hlv";

    // Encrypt
    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = password;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);
    REQUIRE(encryptResult.success == true);

    // Decrypt
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);
    auto keys = KeyManager::deriveFromPassword(password, header.salt, header.pbkdf2Iterations);

    DecryptConfig decryptConfig;
    decryptConfig.filePath = outputPath;
    decryptConfig.aesKey = keys.aesKey;
    std::memcpy(decryptConfig.nonce, header.nonce, HLV_NONCE_SIZE);
    decryptConfig.originalSize = header.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(decryptConfig);
    REQUIRE(ctx != nullptr);

    // Read should return EOF immediately
    uint8_t buf[10];
    int nread = avio_read(ctx, buf, 10);
    REQUIRE(nread == AVERROR_EOF);

    DecryptingAVIOContext::destroy(ctx);
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Edge case: Very small file (less than one AES block)", "[crypto][integration][edge_case]") {
    const size_t fileSize = 10; // Less than 16 bytes (one AES block)
    const std::string password = "testpassword123";

    std::string inputPath = createTestFile(fileSize);
    std::vector<uint8_t> originalData = readFileContents(inputPath);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = password;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);
    REQUIRE(encryptResult.success == true);

    // Decrypt
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);
    auto keys = KeyManager::deriveFromPassword(password, header.salt, header.pbkdf2Iterations);

    DecryptConfig decryptConfig;
    decryptConfig.filePath = outputPath;
    decryptConfig.aesKey = keys.aesKey;
    std::memcpy(decryptConfig.nonce, header.nonce, HLV_NONCE_SIZE);
    decryptConfig.originalSize = header.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(decryptConfig);
    REQUIRE(ctx != nullptr);

    // Read all data
    std::vector<uint8_t> decryptedData(fileSize);
    int nread = avio_read(ctx, decryptedData.data(), static_cast<int>(fileSize));
    REQUIRE(nread == static_cast<int>(fileSize));

    // Verify data matches
    bool dataMatch = std::memcmp(decryptedData.data(), originalData.data(), fileSize) == 0;
    REQUIRE(dataMatch == true);

    DecryptingAVIOContext::destroy(ctx);
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Edge case: Multiple consecutive seeks", "[crypto][integration][edge_case]") {
    const size_t fileSize = 1024;
    const std::string password = "testpassword123";

    std::string inputPath = createTestFile(fileSize);
    std::vector<uint8_t> originalData = readFileContents(inputPath);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = password;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto encryptResult = encryptor.encrypt(config);
    REQUIRE(encryptResult.success == true);

    // Decrypt
    std::vector<uint8_t> hlvData = readFileContents(outputPath);
    HlvHeader header = HlvHeader::deserialize(hlvData.data(), HLV_HEADER_SIZE);
    auto keys = KeyManager::deriveFromPassword(password, header.salt, header.pbkdf2Iterations);

    DecryptConfig decryptConfig;
    decryptConfig.filePath = outputPath;
    decryptConfig.aesKey = keys.aesKey;
    std::memcpy(decryptConfig.nonce, header.nonce, HLV_NONCE_SIZE);
    decryptConfig.originalSize = header.originalSize;

    AVIOContext* ctx = DecryptingAVIOContext::create(decryptConfig);
    REQUIRE(ctx != nullptr);

    // Multiple seeks to various positions
    std::vector<std::pair<int64_t, int>> seekReadOps = {
        {0, 50},
        {100, 50},
        {17, 100},  // Non-block-aligned
        {512, 200},
        {1023, 1}   // Last byte
    };

    for (const auto& [offset, readSize] : seekReadOps) {
        int64_t ret = avio_seek(ctx, offset, SEEK_SET);
        REQUIRE(ret == offset);

        std::vector<uint8_t> buf(readSize);
        int nread = avio_read(ctx, buf.data(), readSize);
        REQUIRE(nread == readSize);

        bool match = std::memcmp(buf.data(), originalData.data() + offset, readSize) == 0;
        REQUIRE(match == true);
    }

    DecryptingAVIOContext::destroy(ctx);
    deleteFile(inputPath);
    deleteFile(outputPath);
}