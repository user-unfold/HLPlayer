#ifndef HLPLAYER_FFMPEG_VIDEO_DECODER_H
#define HLPLAYER_FFMPEG_VIDEO_DECODER_H

#include <hlplayer/HWDecoder.h>
#include <hlplayer/GpuFrameContract.h>
#include "FFmpegRAII.h"
#include <hlplayer/EventBus.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifdef HLPLAYER_EXTRACTOR_EXPORTS
        #define HLPLAYER_EXTRACTOR_API __declspec(dllexport)
    #else
        #define HLPLAYER_EXTRACTOR_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_EXTRACTOR_API
#endif

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

/// Hardware acceleration backend preference
enum class HWAccelBackend {
    Auto = 0,
    D3D11VA,
    DXVA2,
    VideoToolbox,
    VAAPI,
    CUDA,
    None
};

/// FFmpeg video decoder configuration
struct FFmpegDecoderConfig {
    HWAccelBackend hwBackend = HWAccelBackend::Auto;
    void* externalDevice = nullptr;
    uint32_t threadCount = 0;
    bool enableLowDelay = true;
    bool enableZeroCopy = true;
    bool allowCpuFallback = true;
    PixelFormat outputFormat = PixelFormat::NV12;

    FFmpegDecoderConfig() = default;
};

/// Callback for decoded GPU frames
using GpuFrameCallback = std::function<void(GpuFrame)>;

/// FFmpeg-based video decoder with hardware acceleration
class HLPLAYER_EXTRACTOR_API FFmpegVideoDecoder : public hlplayer::IHWDecoder {
public:
    explicit FFmpegVideoDecoder(EventBus* eventBus = nullptr);
    ~FFmpegVideoDecoder() override;

    FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
    FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

    Result<void> open(const DecoderConfig& config) override;
    Result<GpuFrame> decode(const uint8_t* data, size_t size, double pts) override;
    Result<std::vector<GpuFrame>> flush() override;
    void close() override;

    DecodeBackend getBackend() const override;
    bool supportsCodec(Codec codec) const override;

    void setFrameCallback(GpuFrameCallback callback);

    Result<void> setExternalDevice(void* device);

private:
    Result<void> setupHardwareContext();
    Result<void> createCodecContext(const AVCodec* codec);
    GpuFrame convertToGpuFrame(AVFrame* frame);
    PixelFormat avPixelFormatToGpuFormat(AVPixelFormat fmt) const;
    ColorSpace getFrameColorSpace(AVFrame* frame) const;

    AVBufferRefPtr hwDeviceCtx_;
    AVBufferRefPtr hwFramesCtx_;
    AVCodecContextPtr codecCtx_;
    const AVCodec* codec_ = nullptr;

    DecoderConfig config_;
    FFmpegDecoderConfig ffmpegConfig_;

    std::atomic<DecodeBackend> activeBackend_{DecodeBackend::CPU};
    std::atomic<bool> isOpen_{false};

    int consecutiveDecodeFailures_ = 0;
    static constexpr int MAX_CONSECUTIVE_FAILURES = 5;

    std::queue<GpuFrame> frameQueue_;
    std::mutex queueMutex_;

    std::function<void(GpuFrame)> frameCallback_;
    EventBus* eventBus_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    AVPixelFormat swPixelFormat_ = AV_PIX_FMT_NONE;

    // Cached SwsContext for color conversion (avoid recreating per-frame)
    SwsContextPtr cachedSwsCtx_;
    int swsSrcW_ = 0;
    int swsSrcH_ = 0;
    AVPixelFormat swsSrcFmt_ = AV_PIX_FMT_NONE;
};

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_FFMPEG_VIDEO_DECODER_H
