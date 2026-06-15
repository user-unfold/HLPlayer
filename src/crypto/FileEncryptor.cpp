#include "FileEncryptor.h"
#include "AesCtr256.h"
#include "HmacSha256.h"
#include "KeyManager.h"
#include "constant_time.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

extern "C" {
#include <libavutil/random_seed.h>
}

namespace hlplayer::crypto {

#ifdef _WIN32
static std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}
#endif

FileEncryptor::FileEncryptor() = default;

FileEncryptor::~FileEncryptor() = default;

void FileEncryptor::cancel() {
    shouldCancel_.store(true, std::memory_order_release);
}

bool FileEncryptor::checkDiskSpace(const std::string& path, uint64_t requiredBytes) {
#ifdef _WIN32
    ULARGE_INTEGER freeBytesAvailable;
    ULARGE_INTEGER totalBytes;
    ULARGE_INTEGER totalFreeBytes;

    // Get the disk root from the path
    std::wstring wpath = utf8ToWide(path);
    if (wpath.length() < 3 || wpath[1] != L':') {
        // Not a full path, assume current drive
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        wpath = cwd;
    }

    // Extract drive letter and colon
    std::wstring rootPath = wpath.substr(0, 3);

    if (GetDiskFreeSpaceExW(rootPath.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        return freeBytesAvailable.QuadPart >= requiredBytes;
    }
    return false; // Assume disk full on error
#else
    // Unix: use statvfs
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) == 0) {
        uint64_t freeSpace = static_cast<uint64_t>(stat.f_bsize) * stat.f_bavail;
        return freeSpace >= requiredBytes;
    }
    return false;
#endif
}

void FileEncryptor::generateRandomBytes(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        uint32_t rand = av_get_random_seed();
        size_t copyLen = (len - i < 4) ? (len - i) : 4;
        std::memcpy(buf + i, &rand, copyLen);
    }
}

EncryptResult FileEncryptor::encrypt(const EncryptConfig& config,
                                      std::function<void(double progress)> onProgress) {
    EncryptResult result;
    shouldCancel_.store(false, std::memory_order_release);

    // 1. Validate input
    if (hasHlvExtension(config.inputPath)) {
        result.error = EncryptError::AlreadyEncrypted;
        result.errorMessage = "文件已加密 (.hlv 文件) / File is already encrypted (.hlv file)";
        return result;
    }

    // Check if input file exists and is readable
    FILE* inputFile = nullptr;
#ifdef _WIN32
    std::wstring wInputPath = utf8ToWide(config.inputPath);
    inputFile = _wfopen(wInputPath.c_str(), L"rb");
#else
    inputFile = std::fopen(config.inputPath.c_str(), "rb");
#endif

    if (inputFile == nullptr) {
        result.error = EncryptError::InputNotFound;
        result.errorMessage = "输入文件不存在或无法读取 / Input file not found or cannot be read";
        return result;
    }

    // Get input file size
#ifdef _WIN32
    _fseeki64(inputFile, 0, SEEK_END);
    int64_t fileSize = _ftelli64(inputFile);
    _fseeki64(inputFile, 0, SEEK_SET);
#else
    fseeko(inputFile, 0, SEEK_END);
    int64_t fileSize = ftello(inputFile);
    fseeko(inputFile, 0, SEEK_SET);
#endif

    if (fileSize < 0) {
        std::fclose(inputFile);
        result.error = EncryptError::IoError;
        result.errorMessage = "无法获取输入文件大小 / Failed to get input file size";
        return result;
    }

    // Check if output file already exists
    FILE* outputFile = nullptr;
#ifdef _WIN32
    std::wstring wOutputCheckPath = utf8ToWide(config.outputPath);
    outputFile = _wfopen(wOutputCheckPath.c_str(), L"rb");
#else
    outputFile = std::fopen(config.outputPath.c_str(), "rb");
#endif

    if (outputFile != nullptr) {
        std::fclose(outputFile);
        std::fclose(inputFile);
        result.error = EncryptError::OutputExists;
        result.errorMessage = "输出文件已存在 / Output file already exists";
        return result;
    }

    // 2. Check disk space (require file size + 1MB overhead for header + slack)
    uint64_t requiredSpace = static_cast<uint64_t>(fileSize) + HLV_HEADER_SIZE + 1024 * 1024;
    if (!checkDiskSpace(config.outputPath, requiredSpace)) {
        std::fclose(inputFile);
        result.error = EncryptError::DiskFull;
        result.errorMessage = "磁盘空间不足 / Insufficient disk space";
        return result;
    }

    // 3. Generate random salt (16 bytes) and nonce (12 bytes)
    uint8_t salt[HLV_SALT_SIZE];
    uint8_t nonce[HLV_NONCE_SIZE];
    generateRandomBytes(salt, HLV_SALT_SIZE);
    generateRandomBytes(nonce, HLV_NONCE_SIZE);

    // 4. Derive keys
    uint8_t aesKey[32];
    uint8_t hmacKey[32];

    if (config.keyMode == KeyMode::Password) {
        auto derived = KeyManager::deriveFromPassword(config.password, salt, HLV_DEFAULT_ITERATIONS);
        std::memcpy(aesKey, derived.aesKey.data(), 32);
        std::memcpy(hmacKey, derived.hmacKey.data(), 32);
    } else {
        SecureBytes keyInput;
        if (config.rawKey.empty() || config.rawKey.size() != 32) {
            keyInput = KeyManager::generateRawKey();
            result.generatedKey = keyInput;
        } else {
            keyInput = config.rawKey;
        }
        auto derived = KeyManager::deriveFromRawKey(keyInput);
        std::memcpy(aesKey, derived.aesKey.data(), 32);
        std::memcpy(hmacKey, derived.hmacKey.data(), 32);
    }

    // 5. Build HlvHeader
    HlvHeader header;
    header.version = HLV_VERSION;
    header.keyMode = config.keyMode;
    header.algorithm = Algorithm::AES256CTR;
    header.flags = 0;
    std::memcpy(header.salt, salt, HLV_SALT_SIZE);
    std::memcpy(header.nonce, nonce, HLV_NONCE_SIZE);
    header.pbkdf2Iterations = (config.keyMode == KeyMode::Password) ? HLV_DEFAULT_ITERATIONS : 0;
    header.originalSize = static_cast<uint64_t>(fileSize);

    // Copy original extension (null-padded)
    std::memset(header.originalExt, 0, HLV_ORIG_EXT_SIZE);
    size_t extLen = config.originalExt.length();
    if (extLen > 7) {
        extLen = 7;
    }
    std::memcpy(header.originalExt, config.originalExt.c_str(), extLen);

    std::memset(header.reserved, 0, HLV_RESERVED_SIZE);
    std::memset(header.headerHmac, 0, HLV_HMAC_SIZE);

    // 6. Open output as .hlv.tmp with exclusive lock
    std::string tmpPath = config.outputPath + ".tmp";

#ifdef _WIN32
    std::wstring wtmpPath = utf8ToWide(tmpPath);
    HANDLE hFile = CreateFileW(
        wtmpPath.c_str(),
        GENERIC_WRITE,
        0,  // No sharing (exclusive lock)
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::fclose(inputFile);
        DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            result.error = EncryptError::IoError;  // No FileLocked in EncryptError enum
            result.errorMessage = "文件被其他程序占用 / File is locked by another process";
        } else {
            result.error = EncryptError::IoError;
            result.errorMessage = "无法创建临时输出文件 / Failed to create temporary output file";
        }
        return result;
    }

    // RAII wrapper for HANDLE
    struct HandleCloser {
        void operator()(HANDLE h) const { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    };
    std::unique_ptr<void, HandleCloser> fileGuard(hFile);
#else
    // Unix: create with O_EXCL for exclusive access
    int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        std::fclose(inputFile);
        int error = errno;
        if (error == EACCES || error == EPERM) {
            result.error = EncryptError::IoError;  // No FileLocked in EncryptError enum
            result.errorMessage = "文件被其他程序占用 / File is locked by another process";
        } else {
            result.error = EncryptError::IoError;
            result.errorMessage = "无法创建临时输出文件 / Failed to create temporary output file";
        }
        return result;
    }
    FILE* tmpFile = fdopen(fd, "wb");
    if (tmpFile == nullptr) {
        close(fd);
        std::fclose(inputFile);
        result.error = EncryptError::IoError;
        result.errorMessage = "无法打开临时输出文件 / Failed to open temporary output file";
        return result;
    }
    std::unique_ptr<FILE, decltype(&std::fclose)> tmpFileGuard(tmpFile, &std::fclose);
#endif

    // 7. Write header placeholder (112 zero bytes) to .tmp file
    uint8_t headerPlaceholder[HLV_HEADER_SIZE] = {0};
#ifdef _WIN32
    DWORD bytesWritten = 0;
    if (!WriteFile(hFile, headerPlaceholder, HLV_HEADER_SIZE, &bytesWritten, nullptr) ||
        bytesWritten != HLV_HEADER_SIZE) {
        result.error = EncryptError::IoError;
        result.errorMessage = "无法写入头部占位符 / Failed to write header placeholder";
        return result;
    }
#else
    if (std::fwrite(headerPlaceholder, 1, HLV_HEADER_SIZE, tmpFile) != HLV_HEADER_SIZE) {
        result.error = EncryptError::IoError;
        result.errorMessage = "无法写入头部占位符 / Failed to write header placeholder";
        return result;
    }
#endif

    // 8. Encrypt data in chunks
    constexpr size_t CHUNK_SIZE = 512 * 1024;  // 512KB chunks
    std::vector<uint8_t> plaintext(CHUNK_SIZE);
    std::vector<uint8_t> ciphertext(CHUNK_SIZE);

    AesCtr256 aes;
    if (!aes.init(aesKey, nonce)) {
        result.error = EncryptError::Unknown;
        result.errorMessage = "无法初始化 AES-CTR / Failed to initialize AES-CTR";
        return result;
    }

    int64_t bytesProcessed = 0;
    double lastReportedProgress = 0.0;
    auto lastReportTime = std::chrono::steady_clock::now();
    constexpr double kMinProgressDelta = 0.01;   // 1% minimum change
    constexpr auto kMinReportInterval = std::chrono::milliseconds(50);

    while (bytesProcessed < fileSize) {
        // Check for cancellation
        if (shouldCancel_.load(std::memory_order_acquire)) {
            // Clean up: delete .tmp file
#ifdef _WIN32
            fileGuard.reset();  // Close handle
            DeleteFileW(wtmpPath.c_str());
#else
            tmpFileGuard.reset();  // Close file
            std::remove(tmpPath.c_str());
#endif
            result.error = EncryptError::Cancelled;
            result.errorMessage = "操作已取消 / Operation cancelled";
            return result;
        }

        // Read chunk
        size_t bytesToRead = CHUNK_SIZE;
        if (static_cast<int64_t>(bytesToRead) > (fileSize - bytesProcessed)) {
            bytesToRead = static_cast<size_t>(fileSize - bytesProcessed);
        }

        size_t bytesRead = std::fread(plaintext.data(), 1, bytesToRead, inputFile);
        if (bytesRead != bytesToRead) {
#ifdef _WIN32
            fileGuard.reset();
            DeleteFileW(wtmpPath.c_str());
#else
            tmpFileGuard.reset();
            std::remove(tmpPath.c_str());
#endif
            result.error = EncryptError::IoError;
            result.errorMessage = "无法读取输入文件 / Failed to read input file";
            return result;
        }

        // Encrypt chunk
        aes.process(plaintext.data(), ciphertext.data(), bytesRead);

        // Write encrypted chunk
#ifdef _WIN32
        bytesWritten = 0;
        if (!WriteFile(hFile, ciphertext.data(), static_cast<DWORD>(bytesRead), &bytesWritten, nullptr) ||
            static_cast<size_t>(bytesWritten) != bytesRead) {
            fileGuard.reset();
            DeleteFileW(wtmpPath.c_str());
            result.error = EncryptError::IoError;
            result.errorMessage = "无法写入加密数据 / Failed to write encrypted data";
            return result;
        }
#else
        if (std::fwrite(ciphertext.data(), 1, bytesRead, tmpFile) != bytesRead) {
            tmpFileGuard.reset();
            std::remove(tmpPath.c_str());
            result.error = EncryptError::IoError;
            result.errorMessage = "无法写入加密数据 / Failed to write encrypted data";
            return result;
        }
#endif

        bytesProcessed += bytesRead;

        // Report progress (throttled: max ~100 updates, min 50ms apart)
        if (onProgress) {
            double progress = static_cast<double>(bytesProcessed) / static_cast<double>(fileSize);
            bool isLastChunk = (bytesProcessed >= fileSize);
            auto now = std::chrono::steady_clock::now();
            if (isLastChunk ||
                (progress - lastReportedProgress) >= kMinProgressDelta ||
                (now - lastReportTime) >= kMinReportInterval) {
                onProgress(progress);
                lastReportedProgress = progress;
                lastReportTime = now;
            }
        }
    }

    std::fclose(inputFile);

    // 9. Compute header HMAC
    // Serialize header with all fields set (headerHmac = zeros)
    auto serializedHeader = header.serialize();
    // HMAC covers bytes 0x00-0x4F (80 bytes)
    HmacSha256::compute(hmacKey, 32, serializedHeader.data(), 80, header.headerHmac);

    // Serialize again with the HMAC set
    serializedHeader = header.serialize();

    // 10. Write full serialized header (112 bytes) at offset 0 in .tmp file
#ifdef _WIN32
    LARGE_INTEGER offset;
    offset.QuadPart = 0;
    if (!SetFilePointerEx(hFile, offset, nullptr, FILE_BEGIN)) {
        fileGuard.reset();
        DeleteFileW(wtmpPath.c_str());
        result.error = EncryptError::IoError;
        result.errorMessage = "无法定位到头部偏移量 / Failed to seek to header offset";
        return result;
    }

    bytesWritten = 0;
    if (!WriteFile(hFile, serializedHeader.data(), HLV_HEADER_SIZE, &bytesWritten, nullptr) ||
        bytesWritten != HLV_HEADER_SIZE) {
        fileGuard.reset();
        DeleteFileW(wtmpPath.c_str());
        result.error = EncryptError::IoError;
        result.errorMessage = "无法写入头部 / Failed to write header";
        return result;
    }

    fileGuard.reset();
#else
    if (std::fseek(tmpFile, 0, SEEK_SET) != 0) {
        tmpFileGuard.reset();
        std::remove(tmpPath.c_str());
        result.error = EncryptError::IoError;
        result.errorMessage = "无法定位到头部偏移量 / Failed to seek to header offset";
        return result;
    }

    if (std::fwrite(serializedHeader.data(), 1, HLV_HEADER_SIZE, tmpFile) != HLV_HEADER_SIZE) {
        tmpFileGuard.reset();
        std::remove(tmpPath.c_str());
        result.error = EncryptError::IoError;
        result.errorMessage = "无法写入头部 / Failed to write header";
        return result;
    }

    tmpFileGuard.reset();
#endif

    // 11. Atomic rename .tmp to .hlv
#ifdef _WIN32
    std::wstring woutputPath = utf8ToWide(config.outputPath);
    if (!MoveFileExW(wtmpPath.c_str(), woutputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        DeleteFileW(wtmpPath.c_str());
        result.error = EncryptError::IoError;
        result.errorMessage = "无法重命名临时文件到最终输出 / Failed to rename temporary file to final output";
        return result;
    }
#else
    if (std::rename(tmpPath.c_str(), config.outputPath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        result.error = EncryptError::IoError;
        result.errorMessage = "无法重命名临时文件到最终输出 / Failed to rename temporary file to final output";
        return result;
    }
#endif

    result.success = true;
    result.error = EncryptError::None;
    return result;
}

} // namespace hlplayer::crypto