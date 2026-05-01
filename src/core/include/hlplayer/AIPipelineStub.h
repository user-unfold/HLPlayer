#ifndef HLPLAYER_AIPIPELINESTUB_H
#define HLPLAYER_AIPIPELINESTUB_H

#include <hlplayer/IAIPipeline.h>

#include <unordered_set>
#include <mutex>

namespace hlplayer {

class HLPLAYER_CORE_API AIPipelineStub : public IAIPipeline {
public:
    AIPipelineStub();
    ~AIPipelineStub() override = default;

    bool hasCapability(AICapability cap) const override;
    Result<GpuFrame> processFrame(const GpuFrame& frame, uint32_t capabilities) override;
    Result<void> loadModel(const std::string& path, AICapability cap) override;

private:
    std::unordered_set<AICapability> loadedCapabilities_;
    mutable std::mutex mutex_;
};

} // namespace hlplayer

#endif // HLPLAYER_AIPIPELINESTUB_H
