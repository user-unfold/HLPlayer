#ifndef HLPLAYER_GPUFRAMECONTRACT_H
#define HLPLAYER_GPUFRAMECONTRACT_H

#include <cstdint>
#include <vector>

namespace hlplayer {

enum class PixelFormat : uint8_t {
    Unknown = 0,
    NV12,
    P010,
    RGBA8,
    RGBA16F,
    Vulkan
};

enum class ColorSpace : uint8_t {
    BT601 = 0,
    BT709,
    BT2020,
    sRGB
};

enum class ColorRange : uint8_t {
    Limited = 0,
    Full
};

struct GpuFrameHandle {
    void* nativeHandle = nullptr;
    void* auxiliaryHandle = nullptr;
    uint32_t apiType = 0;
};

struct GpuFrame {
    GpuFrameHandle handle{};
    PixelFormat format = PixelFormat::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    ColorSpace colorSpace = ColorSpace::BT709;
    ColorRange colorRange = ColorRange::Limited;
    double timestamp = 0.0;
    bool deviceLost = false;
    int seekSerial = 0;                           // Seek generation this frame belongs to
    std::vector<uint8_t> cpuData; // Optional CPU-readable RGBA pixel data (empty = GPU-only)
};

class GpuFramePool {
public:
    virtual ~GpuFramePool() = default;

    virtual GpuFrame allocate(uint32_t width, uint32_t height, PixelFormat format) = 0;
    virtual void recycle(GpuFrame&& frame) = 0;
    virtual void reset() = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_GPUFRAMECONTRACT_H
