#include <hlplayer/NcnnAIPipeline.h>
#include <hlplayer/logger.h>

#include <chrono>

namespace hlplayer {

NcnnAIPipeline::NcnnAIPipeline()
    : ncnnInterop_(std::make_unique<NCNNInterop>()) {
    LOG_INFO("NcnnAIPipeline::NcnnAIPipeline - created");
}

NcnnAIPipeline::~NcnnAIPipeline() {
    shutdown();
}

Result<void> NcnnAIPipeline::initialize(const NCNNDeviceConfig& config) {
    std::lock_guard<std::mutex> lock(capabilitiesMutex_);

    if (initialized_) {
        LOG_WARN("NcnnAIPipeline::initialize - already initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }

    LOG_INFO("NcnnAIPipeline::initialize - initializing with Vulkan device sharing");

    auto result = ncnnInterop_->initialize(config);
    if (result.hasError()) {
        LOG_ERROR("NcnnAIPipeline::initialize - NCNN interop initialization failed");
        return Result<void>::error(result.error());
    }

    LOG_INFO("NcnnAIPipeline::initialize - Vulkan context established successfully");
    LOG_INFO("  VkInstance: {}", config.vulkanInstance);
    LOG_INFO("  VkPhysicalDevice: {}", config.vulkanPhysicalDevice);
    LOG_INFO("  VkDevice: {}", config.vulkanDevice);
    LOG_INFO("  QueueFamilyIndex: {}", config.queueFamilyIndex);

    initialized_ = true;

    return Result<void>::success();
}

void NcnnAIPipeline::shutdown() {
    if (shutdownRequested_.exchange(true)) {
        return;
    }

    LOG_INFO("NcnnAIPipeline::shutdown - shutting down");

    queueCondition_.notify_all();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    ncnnInterop_->shutdown();
    initialized_ = false;
    shutdownRequested_ = false;

    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!taskQueue_.empty()) {
        taskQueue_.pop();
    }

    LOG_INFO("NcnnAIPipeline::shutdown - shutdown complete");
}

bool NcnnAIPipeline::hasCapability(AICapability cap) const {
    std::lock_guard<std::mutex> lock(capabilitiesMutex_);
    return loadedCapabilities_.count(cap) > 0;
}

Result<GpuFrame> NcnnAIPipeline::processFrame(const GpuFrame& frame, uint32_t capabilities) {
    if (!initialized_) {
        LOG_WARN("NcnnAIPipeline::processFrame - pipeline not initialized");
        return Result<GpuFrame>::error(PlayerError::InvalidState);
    }

    if (frame.deviceLost) {
        LOG_ERROR("NcnnAIPipeline::processFrame - device lost detected");
        return Result<GpuFrame>::error(PlayerError::DeviceLost);
    }

    std::lock_guard<std::mutex> lock(capabilitiesMutex_);

    bool anyMatch = false;
    for (const auto& loaded : loadedCapabilities_) {
        if (static_cast<uint32_t>(loaded) & capabilities) {
            anyMatch = true;
            break;
        }
    }

    if (!anyMatch && capabilities != 0) {
        LOG_WARN("NcnnAIPipeline::processFrame - no matching model loaded (caps=0x{:08x})", capabilities);
        return Result<GpuFrame>::error(PlayerError::UnsupportedFormat);
    }

    auto processingResult = applyDummyProcessing(frame);
    if (processingResult.hasError()) {
        return Result<GpuFrame>::error(processingResult.error());
    }

    return processingResult;
}

Result<void> NcnnAIPipeline::loadModel(const std::string& path, AICapability cap) {
    if (path.empty()) {
        LOG_WARN("NcnnAIPipeline::loadModel - empty path provided");
        return Result<void>::error(PlayerError::InvalidState);
    }

    std::lock_guard<std::mutex> lock(capabilitiesMutex_);

    loadedCapabilities_.insert(cap);

    LOG_INFO("NcnnAIPipeline::loadModel - loaded capability {} from {} (stub, no actual model loaded yet)",
             static_cast<uint32_t>(cap), path);

    return Result<void>::success();
}

size_t NcnnAIPipeline::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return taskQueue_.size();
}

void NcnnAIPipeline::workerThread() {
    LOG_INFO("NcnnAIPipeline::workerThread - started");

    while (!shutdownRequested_) {
        ProcessingTask task;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this]() {
                return shutdownRequested_ || !taskQueue_.empty();
            });

            if (shutdownRequested_) {
                break;
            }

            if (taskQueue_.empty()) {
                continue;
            }

            task = std::move(taskQueue_.front());
            taskQueue_.pop();
        }

        auto result = processFrameInternal(task.frame, task.capabilities);

        if (task.callback) {
            task.callback(result);
        }
    }

    LOG_INFO("NcnnAIPipeline::workerThread - stopped");
}

Result<GpuFrame> NcnnAIPipeline::processFrameInternal(const GpuFrame& frame, uint32_t capabilities) {
    (void)capabilities;
    return applyDummyProcessing(frame);
}

Result<GpuFrame> NcnnAIPipeline::applyDummyProcessing(const GpuFrame& frame) {
    GpuFrame result = frame;

    switch (processingMode_) {
        case ProcessingMode::PassThrough:
            break;

        case ProcessingMode::ColorInvert:
            LOG_DEBUG("NcnnAIPipeline::applyDummyProcessing - color invert mode (stub)");
            break;
    }

    return Result<GpuFrame>::success(result);
}

} // namespace hlplayer
