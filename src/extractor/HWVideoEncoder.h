#ifndef HLPLAYER_HW_VIDEO_ENCODER_H
#define HLPLAYER_HW_VIDEO_ENCODER_H

#include <hlplayer/IVideoEncoder.h>
#include "FFmpegRAII.h"

#include <atomic>
#include <mutex>
#include <string>
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

/// Supported hardware encoding backends.
enum class EncoderBackend : uint8_t {
    NVENC = 0,
    QSV,
    AMF,
    Software
};

/// Information about a detected encoder backend.
struct EncoderBackendInfo {
    EncoderBackend backend;
    std::string name;          // FFmpeg encoder name, e.g. "h264_nvenc"
    bool available = false;
};

/// Hardware-accelerated video encoder using FFmpeg.
/// Supports NVENC, QSV, AMF backends with auto-detection.
/// Accepts GPU frames directly for zero-copy encoding path.
class HLPLAYER_EXTRACTOR_API HWVideoEncoder : public IVideoEncoder {
public:
    HWVideoEncoder();
    ~HWVideoEncoder() override;

    HWVideoEncoder(const HWVideoEncoder&) = delete;
    HWVideoEncoder& operator=(const HWVideoEncoder&) = delete;

    /// Open and configure the encoder with hardware acceleration.
    Result<void> open(const EncoderConfig& config) override;

    /// Encode a GPU frame (zero-copy if GPU texture is available).
    Result<EncodedPacket> encode(const GpuFrame& frame) override;

    /// Flush remaining buffered frames and return final packets.
    Result<std::vector<EncodedPacket>> flush() override;

    /// Close the encoder and release all FFmpeg resources.
    void close() override;

    /// Get the current encoder configuration.
    EncoderConfig getConfig() const override;

    /// Check if encoder is open and ready.
    bool isOpen() const override;

    // -----------------------------------------------------------------------
    // Static utilities
    // -----------------------------------------------------------------------

    /// Probe FFmpeg for all available hardware encoder backends.
    /// Returns a list of backends that are actually usable on this system.
    static std::vector<EncoderBackendInfo> detectAvailableBackends();

    /// Map a (backend, codec) pair to the FFmpeg encoder name.
    /// Returns empty string if the combination is unsupported.
    static std::string getEncoderName(EncoderBackend backend, Codec codec);

    /// Resolve HwAccelMode to the AVHWDeviceType FFmpeg understands.
    static AVHWDeviceType hwAccelModeToAVType(HwAccelMode mode);

    /// Map PixelFormat to AVPixelFormat for encoding.
    static AVPixelFormat pixelFormatToAV(PixelFormat fmt);

private:
    /// Select the best available backend for the requested HwAccelMode and codec.
    Result<EncoderBackend> selectBackend(Codec codec, HwAccelMode mode);

    /// Create and initialize the FFmpeg codec context for the chosen backend.
    Result<void> initEncoder(EncoderBackend backend, const EncoderConfig& config);

    /// Set up hardware frame context for zero-copy GPU texture input.
    Result<void> setupHWFramesContext(AVHWDeviceType deviceType,
                                       uint32_t width, uint32_t height,
                                       AVPixelFormat swFormat);

    /// Map an AVPacket to an EncodedPacket.
    EncodedPacket convertPacket(AVPacket* pkt);

    /// Build an AVFrame from a GpuFrame for GPU texture input path.
    Result<AVFramePtr> frameFromGpuTexture(const GpuFrame& frame);

    /// Build an AVFrame from CPU pixel data fallback path.
    Result<AVFramePtr> frameFromCpuData(const GpuFrame& frame);

    // FFmpeg resources (RAII managed)
    AVCodecContextPtr codecCtx_;
    AVBufferRefPtr hwDeviceCtx_;
    AVBufferRefPtr hwFramesCtx_;

    // State
    EncoderConfig config_;
    EncoderBackend activeBackend_ = EncoderBackend::Software;
    std::atomic<bool> isOpen_{false};
    double timeBase_ = 0.0;
    int64_t frameIndex_ = 0;

    // Thread safety
    mutable std::mutex mutex_;
};

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_HW_VIDEO_ENCODER_H
