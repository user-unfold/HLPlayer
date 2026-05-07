#ifndef HLPLAYER_IVIDEOENCODER_H
#define HLPLAYER_IVIDEOENCODER_H

#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/HWDecoder.h>
extern "C" {
#include <libavutil/avutil.h>
}
#include <cstdint>
#include <string>
#include <vector>

namespace hlplayer {

/// Hardware acceleration options for encoding.
enum class HwAccelMode : uint8_t {
    None = 0,
    Auto,
    D3D11,
    CUDA,
    Vulkan
};

/// Configuration for video encoder.
struct EncoderConfig {
    Codec codec = Codec::H264;
    uint32_t width = 0;
    uint32_t height = 0;
    double frameRate = 30.0;
    uint32_t bitrate = 0;
    uint32_t crf = 23;
    std::string preset = "medium";
    HwAccelMode hwAccel = HwAccelMode::Auto;
    PixelFormat inputFormat = PixelFormat::NV12;
};

/// Encoded packet output from encoder.
/// pts/dts/duration are in the encoder's time_base units (raw int64_t).
/// Callers must rescale to the output stream time_base via av_packet_rescale_ts.
struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = AV_NOPTS_VALUE;
    int64_t dts = AV_NOPTS_VALUE;
    int64_t duration = 0;
    bool isKeyFrame = false;
    uint32_t streamIndex = 0;
};

/// Interface for video encoding.
/// Encodes GPU frames to compressed video packets in H.264/HEVC/AV1 formats.
class HLPLAYER_CORE_API IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    /// Open and configure the encoder.
    /// @param config Encoder configuration parameters.
    /// @return Result<void>::success() on success, or an error code on failure.
    virtual Result<void> open(const EncoderConfig& config) = 0;

    /// Encode a GPU frame.
    /// @param frame Input GPU frame to encode.
    /// @return Result<EncodedPacket> containing the encoded packet, or an error.
    virtual Result<EncodedPacket> encode(const GpuFrame& frame) = 0;

    /// Flush any remaining buffered frames and return final packets.
    /// @return Result<std::vector<EncodedPacket>> containing remaining packets, or an error.
    virtual Result<std::vector<EncodedPacket>> flush() = 0;

    /// Close the encoder and release resources.
    virtual void close() = 0;

    /// Get the current encoder configuration.
    /// @return Current encoder configuration.
    virtual EncoderConfig getConfig() const = 0;

    /// Check if encoder is ready for encoding.
    /// @return true if encoder is open and ready, false otherwise.
    virtual bool isOpen() const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IVIDEOENCODER_H
