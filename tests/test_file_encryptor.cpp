#include <catch2/catch_test_macros.hpp>
#include "FileEncryptor.h"
#include "HlvHeader.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

using namespace hlplayer::crypto;

// Helper: create a test file with random data
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

    // Write random data
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = static_cast<uint8_t>(i % 256);
        std::fwrite(&byte, 1, 1, f);
    }

    std::fclose(f);
    return path;
}

// Helper: check if file exists
static bool fileExists(const std::string& path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif

    if (f != nullptr) {
        std::fclose(f);
        return true;
    }
    return false;
}

// Helper: read first N bytes of file
static std::vector<uint8_t> readFileHeader(const std::string& path, size_t n) {
    std::vector<uint8_t> data(n);
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif

    REQUIRE(f != nullptr);
    size_t bytesRead = std::fread(data.data(), 1, n, f);
    std::fclose(f);

    if (bytesRead < n) {
        data.resize(bytesRead);
    }
    return data;
}

// Helper: delete file if exists
static void deleteFile(const std::string& path) {
    std::remove(path.c_str());
}

TEST_CASE("Encrypt small test file with password", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);  // 1KB test file
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test_password";
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == true);
    REQUIRE(result.error == EncryptError::None);
    REQUIRE(fileExists(outputPath) == true);

    // Verify .hlv.tmp does NOT exist
    REQUIRE(fileExists(outputPath + ".tmp") == false);

    // Verify header magic
    auto header = readFileHeader(outputPath, 8);
    REQUIRE(std::memcmp(header.data(), HLV_MAGIC, 8) == 0);

    // Verify header_hmac is populated (not all zeros)
    auto headerBytes = readFileHeader(outputPath, HLV_HEADER_SIZE);
    bool hmacPopulated = false;
    for (size_t i = 0x50; i < 0x70; ++i) {
        if (headerBytes[i] != 0) {
            hmacPopulated = true;
            break;
        }
    }
    REQUIRE(hmacPopulated == true);

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Encrypt with raw key mode generates key", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::RawKey;
    config.originalExt = "mkv";
    // rawKey is empty, should generate one

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == true);
    REQUIRE(result.generatedKey.size() == 32);
    REQUIRE(result.generatedKey[0] != 0);  // Should be non-zero

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Encrypt .hlv file rejected with AlreadyEncrypted error", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);
    std::string outputPath = inputPath + ".hlv";

    // First, create an .hlv file
    {
        EncryptConfig config;
        config.inputPath = inputPath;
        config.outputPath = outputPath;
        config.keyMode = KeyMode::Password;
        config.password = "test";
        config.originalExt = "mp4";

        FileEncryptor encryptor;
        auto result = encryptor.encrypt(config);
        REQUIRE(result.success == true);
    }

    // Now try to encrypt the .hlv file itself
    std::string outputPath2 = outputPath + ".2.hlv";
    EncryptConfig config;
    config.inputPath = outputPath;
    config.outputPath = outputPath2;
    config.keyMode = KeyMode::Password;
    config.password = "test2";
    config.originalExt = "hlv";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == false);
    REQUIRE(result.error == EncryptError::AlreadyEncrypted);

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
    deleteFile(outputPath2);
}

TEST_CASE("Cancel mid-encrypt deletes .tmp and no .hlv created", "[file_encryptor]") {
    // Create a large enough file so we can cancel mid-operation
    std::string inputPath = createTestFile(1024 * 256);  // 256KB
    std::string outputPath = inputPath + ".hlv";

    bool cancelled = false;
    std::thread cancelThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        FileEncryptor encryptor;
        encryptor.cancel();
        cancelled = true;
    });

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    cancelThread.join();

    REQUIRE(result.success == false);
    REQUIRE(result.error == EncryptError::Cancelled);
    REQUIRE(fileExists(outputPath) == false);
    REQUIRE(fileExists(outputPath + ".tmp") == false);
    REQUIRE(cancelled == true);

    // Cleanup
    deleteFile(inputPath);
}

TEST_CASE("Two encryptions produce different salts and nonces", "[file_encryptor]") {
    std::string inputPath = createTestFile(512);
    std::string outputPath1 = inputPath + ".1.hlv";
    std::string outputPath2 = inputPath + ".2.hlv";

    EncryptConfig config1;
    config1.inputPath = inputPath;
    config1.outputPath = outputPath1;
    config1.keyMode = KeyMode::Password;
    config1.password = "same_password";
    config1.originalExt = "mp4";

    EncryptConfig config2 = config1;
    config2.outputPath = outputPath2;

    FileEncryptor encryptor;
    auto result1 = encryptor.encrypt(config1);
    auto result2 = encryptor.encrypt(config2);

    REQUIRE(result1.success == true);
    REQUIRE(result2.success == true);

    // Read headers and compare salt/nonce
    auto header1 = readFileHeader(outputPath1, HLV_HEADER_SIZE);
    auto header2 = readFileHeader(outputPath2, HLV_HEADER_SIZE);

    // Salt is at offset 0x0C (12)
    bool saltDifferent = false;
    for (size_t i = 0; i < HLV_SALT_SIZE; ++i) {
        if (header1[0x0C + i] != header2[0x0C + i]) {
            saltDifferent = true;
            break;
        }
    }
    REQUIRE(saltDifferent == true);

    // Nonce is at offset 0x1C (28)
    bool nonceDifferent = false;
    for (size_t i = 0; i < HLV_NONCE_SIZE; ++i) {
        if (header1[0x1C + i] != header2[0x1C + i]) {
            nonceDifferent = true;
            break;
        }
    }
    REQUIRE(nonceDifferent == true);

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath1);
    deleteFile(outputPath2);
}

TEST_CASE("Encrypt with progress callback", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024 * 256);  // 256KB
    std::string outputPath = inputPath + ".hlv";

    std::vector<double> progressValues;
    auto onProgress = [&progressValues](double progress) {
        progressValues.push_back(progress);
    };

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config, onProgress);

    REQUIRE(result.success == true);
    REQUIRE(progressValues.size() > 0);
    REQUIRE(progressValues.back() == 1.0);  // Final progress should be 100%

    // Check that progress is monotonically increasing
    for (size_t i = 1; i < progressValues.size(); ++i) {
        REQUIRE(progressValues[i] >= progressValues[i - 1]);
    }

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Encrypt with existing output file fails", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);
    std::string outputPath = inputPath + ".hlv";

    // Create output file first
    {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, outputPath.c_str(), "wb");
#else
        f = std::fopen(outputPath.c_str(), "wb");
#endif
        REQUIRE(f != nullptr);
        std::fclose(f);
    }

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == false);
    REQUIRE(result.error == EncryptError::OutputExists);

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Encrypt with non-existent input file fails", "[file_encryptor]") {
    std::string inputPath = "nonexistent_file_xyz123.xyz";
    std::string outputPath = "output.hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == false);
    REQUIRE(result.error == EncryptError::InputNotFound);
}

TEST_CASE("Encrypt with provided raw key uses that key", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);
    std::string outputPath = inputPath + ".hlv";

    SecureBytes rawKey(32);
    for (size_t i = 0; i < 32; ++i) {
        rawKey[i] = static_cast<uint8_t>(i);
    }

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::RawKey;
    config.rawKey = rawKey;
    config.originalExt = "mp4";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == true);
    REQUIRE(result.generatedKey.empty());  // Should not generate new key

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Encrypt preserves original extension in header", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "mkv";

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == true);

    // Read header and check originalExt field
    auto header = readFileHeader(outputPath, HLV_HEADER_SIZE);
    // originalExt is at offset 0x38 (56)
    char extBuf[8];
    std::memcpy(extBuf, header.data() + 0x38, 8);
    REQUIRE(std::memcmp(extBuf, "mkv\0\0\0\0\0", 8) == 0);

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}

TEST_CASE("Encrypt truncates extension to max 7 chars", "[file_encryptor]") {
    std::string inputPath = createTestFile(1024);
    std::string outputPath = inputPath + ".hlv";

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.keyMode = KeyMode::Password;
    config.password = "test";
    config.originalExt = "verylongext";  // > 7 chars

    FileEncryptor encryptor;
    auto result = encryptor.encrypt(config);

    REQUIRE(result.success == true);

    // Read header and check originalExt field
    auto header = readFileHeader(outputPath, HLV_HEADER_SIZE);
    char extBuf[8];
    std::memcpy(extBuf, header.data() + 0x38, 8);
    // Should be "verylon\0"
    REQUIRE(std::memcmp(extBuf, "verylon\0", 8) == 0);

    // Cleanup
    deleteFile(inputPath);
    deleteFile(outputPath);
}