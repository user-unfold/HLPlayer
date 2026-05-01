#ifndef HLPLAYER_HWDECODER_H
#define HLPLAYER_HWDECODER_H

#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace hlplayer {

enum class DecodeBackend : uint8_t {
    Auto = 0,
    Vulkan,
    CUDA,
    D3D11,
    CPU
};

enum class Codec : uint8_t {
    Unknown = 0,
    H264,
    HEVC,
    AV1
};

struct DecoderConfig {
    DecodeBackend backend = DecodeBackend::Auto;
    Codec codec = Codec::Unknown;
    void* gpuDevice = nullptr;      // VkPhysicalDevice or ID3D11Device
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat outputPixelFormat = PixelFormat::NV12;
    std::vector<uint8_t> extradata;
};

class HLPLAYER_CORE_API IHWDecoder {
public:
    virtual ~IHWDecoder() = default;

    virtual Result<void> open(const DecoderConfig& config) = 0;
    virtual Result<GpuFrame> decode(const uint8_t* data, size_t size, double pts) = 0;
    virtual Result<std::vector<GpuFrame>> flush() = 0;
    virtual void close() = 0;

    virtual DecodeBackend getBackend() const = 0;
    virtual bool supportsCodec(Codec codec) const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_HWDECODER_H
