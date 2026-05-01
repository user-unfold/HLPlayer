#include <hlplayer/AIPipelineStub.h>
#include <hlplayer/logger.h>

#include <string>

namespace hlplayer {

AIPipelineStub::AIPipelineStub() = default;

bool AIPipelineStub::hasCapability(AICapability cap) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadedCapabilities_.count(cap) > 0;
}

Result<GpuFrame> AIPipelineStub::processFrame(const GpuFrame& frame, uint32_t capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);

    bool anyMatch = false;
    for (const auto& loaded : loadedCapabilities_) {
        if (static_cast<uint32_t>(loaded) & capabilities) {
            anyMatch = true;
            break;
        }
    }

    if (!anyMatch) {
        LOG_WARN("AIPipelineStub::processFrame called with no matching model loaded (caps=0x{:08x})", capabilities);
        return Result<GpuFrame>::error(PlayerError::UnsupportedFormat);
    }

    LOG_WARN("AIPipelineStub::processFrame — stub pass-through, no actual AI processing");
    return Result<GpuFrame>::success(frame);
}

Result<void> AIPipelineStub::loadModel(const std::string& path, AICapability cap) {
    if (path.empty()) {
        LOG_WARN("AIPipelineStub::loadModel called with empty path");
        return Result<void>::error(PlayerError::InvalidState);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    loadedCapabilities_.insert(cap);
    LOG_INFO("AIPipelineStub::loadModel — stub loaded capability {} from {}", static_cast<uint32_t>(cap), path);
    return Result<void>::success();
}

} // namespace hlplayer
