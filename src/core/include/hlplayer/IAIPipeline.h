#ifndef HLPLAYER_IAIPIPELINE_H
#define HLPLAYER_IAIPIPELINE_H

#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <cstdint>
#include <string>

namespace hlplayer {

enum class AICapability : uint32_t {
    None = 0,
    SuperResolution = 1u << 0,
    FrameInterpolation = 1u << 1,
    HDR = 1u << 2,
    ToneMapping = 1u << 3,
    NoiseReduction = 1u << 4
};

class HLPLAYER_CORE_API IAIPipeline {
public:
    virtual ~IAIPipeline() = default;

    virtual bool hasCapability(AICapability cap) const = 0;

    virtual Result<GpuFrame> processFrame(const GpuFrame& frame, uint32_t capabilities) = 0;

    virtual Result<void> loadModel(const std::string& path, AICapability cap) = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IAIPIPELINE_H
