#ifndef HLPLAYER_NCNNAIPIPELINE_H
#define HLPLAYER_NCNNAIPIPELINE_H

#include <hlplayer/IAIPipeline.h>
#include <hlplayer/NCNNInterop.h>
#include <hlplayer/GpuFrameContract.h>

#include <memory>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>

namespace hlplayer {

/// NCNN-based AI Pipeline implementation using Vulkan for zero-copy GPU processing.
/// Runs AI inference on a separate compute queue for thread-safety.
class HLPLAYER_CORE_API NcnnAIPipeline : public IAIPipeline {
public:
    NcnnAIPipeline();
    ~NcnnAIPipeline() override;

    // Disable copy and move
    NcnnAIPipeline(const NcnnAIPipeline&) = delete;
    NcnnAIPipeline& operator=(const NcnnAIPipeline&) = delete;
    NcnnAIPipeline(NcnnAIPipeline&&) = delete;
    NcnnAIPipeline& operator=(NcnnAIPipeline&&) = delete;

    /// Initialize Vulkan device context for NCNN
    /// Allows external VkInstance/VkDevice sharing for zero-copy
    Result<void> initialize(const NCNNDeviceConfig& config);

    /// Shutdown the pipeline and release resources
    void shutdown();

    /// Check if a specific AI capability is available
    bool hasCapability(AICapability cap) const override;

    /// Process a GPU frame with the requested AI capabilities
    /// This queues the frame for processing on a separate thread
    Result<GpuFrame> processFrame(const GpuFrame& frame, uint32_t capabilities) override;

    /// Load an AI model from a file path
    Result<void> loadModel(const std::string& path, AICapability cap) override;

    /// Check if the pipeline is initialized and ready
    bool isReady() const { return initialized_; }

    /// Get the number of frames currently queued for processing
    size_t getQueueSize() const;

private:
    struct ProcessingTask {
        GpuFrame frame;
        uint32_t capabilities;
        std::function<void(Result<GpuFrame>)> callback;
    };

    /// Worker thread function that processes frames from the queue
    void workerThread();

    /// Process a single frame (actual AI computation)
    Result<GpuFrame> processFrameInternal(const GpuFrame& frame, uint32_t capabilities);

    /// Simple pass-through or color invert for v1 (scaffold)
    Result<GpuFrame> applyDummyProcessing(const GpuFrame& frame);

    // Thread-safe queue management
    mutable std::mutex queueMutex_;
    mutable std::mutex capabilitiesMutex_;
    std::queue<ProcessingTask> taskQueue_;
    std::condition_variable queueCondition_;

    // Worker thread
    std::thread workerThread_;
    std::atomic<bool> shutdownRequested_{false};

    // NCNN interop
    std::unique_ptr<NCNNInterop> ncnnInterop_;
    std::atomic<bool> initialized_{false};

    // Loaded capabilities
    std::unordered_set<AICapability> loadedCapabilities_;

    // Processing mode for v1 scaffolding
    enum class ProcessingMode {
        PassThrough,
        ColorInvert
    };
    ProcessingMode processingMode_{ProcessingMode::PassThrough};
};

} // namespace hlplayer

#endif // HLPLAYER_NCNNAIPIPELINE_H
