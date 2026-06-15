#include "DecryptingAVIOContext.h"
#include "AesCtr256.h"
#include "HlvHeader.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <cerrno>
#endif

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
}

namespace hlplayer::crypto {

namespace {

// Internal state passed as opaque to AVIOContext callbacks
struct DecryptState {
    FILE* file;                // physical file handle
    uint64_t headerSize;       // 112 bytes
    uint64_t originalSize;     // logical stream size (excludes header)
    AesCtr256 aes;             // AES-CTR engine
    uint64_t logicalPos;       // current position in logical (decrypted) stream
    uint64_t aesLogicalPos;    // position the AES engine is synced to
    uint8_t* avioBuffer;       // FFmpeg AVIOContext internal buffer (64KB)
};

    // Read callback for AVIOContext
int readPacket(void* opaque, uint8_t* buf, int bufSize) {
    auto* state = static_cast<DecryptState*>(opaque);

    // Calculate how much we can read
    uint64_t remaining = state->originalSize - state->logicalPos;
    if (remaining == 0) {
        return AVERROR_EOF;
    }

    int toRead = static_cast<int>(std::min(static_cast<uint64_t>(bufSize), remaining));

    // Ensure AES engine is synced to our current logical position
    if (state->logicalPos != state->aesLogicalPos) {
        state->aes.seek(state->logicalPos);
        state->aesLogicalPos = state->logicalPos;
    }

    // Read encrypted data from physical file at headerSize + logicalPos
#ifdef _WIN32
    _fseeki64(state->file, static_cast<__int64>(state->headerSize + state->logicalPos), SEEK_SET);
#else
    fseeko(state->file, static_cast<off_t>(state->headerSize + state->logicalPos), SEEK_SET);
#endif

    // Temporary buffer for encrypted data
    auto* encBuf = new uint8_t[toRead];
    size_t nread = std::fread(encBuf, 1, static_cast<size_t>(toRead), state->file);
    int bytesRead = static_cast<int>(nread);

    if (bytesRead > 0) {
        // Decrypt in-place into the output buffer
        state->aes.process(encBuf, buf, static_cast<size_t>(bytesRead));
    }

    delete[] encBuf;

    state->logicalPos += static_cast<uint64_t>(bytesRead);
    state->aesLogicalPos = state->logicalPos;

    return bytesRead > 0 ? bytesRead : AVERROR_EOF;
}

// Seek callback for AVIOContext
int64_t seek(void* opaque, int64_t offset, int whence) {
    auto* state = static_cast<DecryptState*>(opaque);

    // AVSEEK_SIZE: return the logical stream size
    if ((whence & AVSEEK_SIZE) != 0) {
        return static_cast<int64_t>(state->originalSize);
    }

    // Strip AVSEEK_FORCE flag
    int directive = whence & ~AVSEEK_FORCE;

    // Normalize to absolute logical offset
    int64_t absolute = 0;
    switch (directive) {
        case SEEK_SET:
            absolute = offset;
            break;
        case SEEK_CUR:
            absolute = static_cast<int64_t>(state->logicalPos) + offset;
            break;
        case SEEK_END:
            absolute = static_cast<int64_t>(state->originalSize) + offset;
            break;
        default:
            return AVERROR(EINVAL);
    }

    // Bounds check
    if (absolute < 0) {
        return AVERROR(EINVAL);
    }
    if (static_cast<uint64_t>(absolute) > state->originalSize) {
        return AVERROR(EINVAL);
    }

    // Reset AES engine to the new position
    state->aes.seek(static_cast<uint64_t>(absolute));
    state->logicalPos = static_cast<uint64_t>(absolute);
    state->aesLogicalPos = state->logicalPos;

    return absolute;
}

} // anonymous namespace

AVIOContext* DecryptingAVIOContext::create(const DecryptConfig& config) {
    // Validate key size
    if (config.aesKey.size() != 32) {
        return nullptr;
    }

    // Open file in binary mode
    FILE* file = nullptr;
#ifdef _WIN32
    // Convert UTF-8 path to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, config.filePath.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        return nullptr;
    }
    auto* wpath = new wchar_t[static_cast<size_t>(wlen)];
    MultiByteToWideChar(CP_UTF8, 0, config.filePath.c_str(), -1, wpath, wlen);
    file = _wfopen(wpath, L"rb");
    delete[] wpath;
#else
    file = std::fopen(config.filePath.c_str(), "rb");
#endif

    if (!file) {
        return nullptr;
    }

    // Verify file is at least 112 bytes (header size)
#ifdef _WIN32
    _fseeki64(file, 0, SEEK_END);
    __int64 fileSize = _ftelli64(file);
    _fseeki64(file, 0, SEEK_SET);
#else
    fseeko(file, 0, SEEK_END);
    off_t fileSize = ftello(file);
    fseeko(file, 0, SEEK_SET);
#endif

    if (fileSize < static_cast<decltype(fileSize)>(HLV_HEADER_SIZE)) {
        std::fclose(file);
        return nullptr;
    }

    // Allocate state
    auto* state = new DecryptState();
    state->file = file;
    state->headerSize = HLV_HEADER_SIZE;
    state->originalSize = config.originalSize;
    state->logicalPos = 0;
    state->aesLogicalPos = 0;
    state->avioBuffer = nullptr;

    // Initialize AES engine
    if (!state->aes.init(config.aesKey.data(), config.nonce)) {
        std::fclose(file);
        delete state;
        return nullptr;
    }

    // Allocate AVIO buffer using av_malloc (required by FFmpeg)
    state->avioBuffer = static_cast<uint8_t*>(av_malloc(65536));
    if (!state->avioBuffer) {
        std::fclose(file);
        delete state;
        return nullptr;
    }

    // Create AVIOContext (read-only: writeFlag=0, writePacket=NULL)
    AVIOContext* ctx = avio_alloc_context(
        state->avioBuffer, 65536, 0,
        state, readPacket, nullptr, seek);

    if (!ctx) {
        av_free(state->avioBuffer);
        std::fclose(file);
        delete state;
        return nullptr;
    }

    // Mark as seekable
    ctx->seekable = AVIO_SEEKABLE_NORMAL;

    return ctx;
}

void DecryptingAVIOContext::destroy(AVIOContext* ctx) {
    if (!ctx) {
        return;
    }

    auto* state = static_cast<DecryptState*>(ctx->opaque);
    if (state) {
        if (state->file) {
            std::fclose(state->file);
        }
        // avioBuffer is freed by avio_context_free via ctx->buffer
        delete state;
    }

    // avio_context_free frees ctx->buffer (our avioBuffer) and the AVIOContext struct
    avio_context_free(&ctx);
}

} // namespace hlplayer::crypto
