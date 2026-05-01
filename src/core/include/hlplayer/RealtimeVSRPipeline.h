#ifndef HLPLAYER_REALTIMEVSRPIPELINE_H
#define HLPLAYER_REALTIMEVSRPIPELINE_H

#include <hlplayer/Export.h>
#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/Result.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace hlplayer {

class IHWDecoder;
class IPipelineNode;
class IVRAMBudgetManager;
class SyncClock;
class EventBus;
class PacketQueue;
class VideoFrameQueue;

namespace render {
class VSRRenderBridge;
}

// ============================================================================
// VSR Circuit Breaker
// ============================================================================

/// Circuit breaker states for the VSR inference stage.
enum class VSRState : uint8_t {
    Active = 0,     ///< VSR inference is running normally
    CircuitOpen,    ///< VSR bypassed due to sustained slow inference (pass-through)
    Probing         ///< Testing one frame after cooldown to check if VSR is viable again
};

// ============================================================================
// Pipeline Configuration
// ============================================================================

/// Configuration for the real-time VSR pipeline.
struct VSRPipelineConfig {
    /// Maximum packets in the decode input queue (from demuxer).
    size_t packetQueueMaxPackets = 200;

    /// Maximum bytes in the decode input queue.
    size_t packetQueueMaxBytes = 20 * 1024 * 1024;

    /// Capacity of the decoded frame queue (between decode and VSR stages).
    size_t decodedFrameQueueCap = 4;

    /// Capacity of the VSR input queue (aggressive frame dropping, cap 2).
    size_t vsrInputQueueCap = 2;

    /// Capacity of the output frame queue (VSR -> Render).
    size_t outputFrameQueueCap = 4;

    /// VSR inference time threshold in milliseconds.
    /// If 3 consecutive frames exceed this, the circuit breaker opens.
    double vsrSlowThresholdMs = 16.0;

    /// Number of consecutive slow frames before circuit breaker opens.
    int vsrSlowFrameCount = 3;

    /// Cooldown period in seconds before probing VSR after circuit opens.
    double vsrCooldownSeconds = 30.0;

    /// VRAM allocation timeout in milliseconds for VSR inference.
    uint32_t vramAllocationTimeoutMs = 50;
};

// ============================================================================
// VSR Frame Queue (aggressive dropping variant)
// ============================================================================

/// Frame queue with aggressive dropping: when full, discards the oldest frame.
/// Used between Decode and VSR stages where latency matters more than completeness.
class HLPLAYER_CORE_API VSRFrameQueue {
public:
    explicit VSRFrameQueue(size_t capacity = 2) : capacity_(capacity) {}

    /// Push a frame. If queue is full, discard the oldest frame first.
    void pushOrDrop(GpuFrame frame) {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= capacity_) {
            queue_.pop_front(); // Discard oldest
            ++droppedCount_;
        }
        queue_.push_back(std::move(frame));
        notEmpty_.notify_one();
    }

    /// Pop a frame. Blocks until a frame is available or shutdown.
    bool pop(GpuFrame& out, int timeoutMs = -1) {
        std::unique_lock lock(mutex_);
        auto pred = [this] { return !queue_.empty() || shutdown_; };
        if (timeoutMs < 0) {
            notEmpty_.wait(lock, pred);
        } else {
            notEmpty_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred);
        }
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    uint64_t droppedCount() const {
        std::lock_guard lock(mutex_);
        return droppedCount_;
    }

    void flush() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        notEmpty_.notify_all();
    }

    void shutdown() {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
        notEmpty_.notify_all();
    }

    void restart() {
        std::lock_guard lock(mutex_);
        shutdown_ = false;
        queue_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::deque<GpuFrame> queue_;
    size_t capacity_;
    uint64_t droppedCount_ = 0;
    bool shutdown_ = false;
};

// ============================================================================
// RealtimeVSRPipeline
// ============================================================================

/// Four-stage real-time video super-resolution pipeline:
///   Demux → Decode → VSR → Render
///
/// Each stage runs in its own thread. Thread-safe frame queues connect stages.
/// The VSR stage has a circuit breaker that bypasses super-resolution when
/// inference is too slow, with automatic recovery probing after cooldown.
///
/// Lifecycle: configure() → start() → [pause()/resume()] → stop()
///
/// Thread-safety: All public methods are thread-safe.
class HLPLAYER_CORE_API RealtimeVSRPipeline {
public:
    explicit RealtimeVSRPipeline(VSRPipelineConfig config = {});
    ~RealtimeVSRPipeline();

    RealtimeVSRPipeline(const RealtimeVSRPipeline&) = delete;
    RealtimeVSRPipeline& operator=(const RealtimeVSRPipeline&) = delete;

    // ----------------------------------------------------------------
    // Pipeline configuration (call before start)
    // ----------------------------------------------------------------

    /// Set the hardware decoder. Must be called before start().
    void setDecoder(std::shared_ptr<IHWDecoder> decoder);

    /// Set the VSR processing node (e.g., NcnnSuperResolution).
    /// If nullptr, VSR stage is bypassed entirely.
    void setVSRNode(std::shared_ptr<IPipelineNode> vsrNode);

    /// Set the VRAM budget manager for memory tracking.
    void setVRAMBudgetManager(std::shared_ptr<IVRAMBudgetManager> vramManager);

    /// Set the render bridge for output display.
    void setRenderBridge(std::shared_ptr<render::VSRRenderBridge> renderBridge);

    /// Set the sync clock for A/V synchronization.
    void setSyncClock(SyncClock* syncClock);

    /// Set the event bus for state change notifications.
    void setEventBus(EventBus* eventBus);

    /// Set a callback invoked when a new packet is needed from the demuxer.
    /// The pipeline calls this to request the next video packet.
    /// Return true if a packet was pushed, false on end-of-stream or error.
    using PacketProvider = std::function<bool(PacketQueue& queue)>;
    void setPacketProvider(PacketProvider provider);

    /// Set estimated VRAM needed per VSR inference frame (for budget requests).
    void setVSRFrameVRAM(uint64_t bytes);

    // ----------------------------------------------------------------
    // Lifecycle
    // ----------------------------------------------------------------

    /// Initialize all pipeline stages. Must be called after configuration.
    Result<void> initialize();

    /// Start all pipeline threads. Returns error if not initialized.
    Result<void> start();

    /// Pause all pipeline stages. Threads block until resumed.
    void pause();

    /// Resume paused pipeline.
    void resume();

    /// Stop all pipeline threads and release resources.
    void stop();

    /// Flush all queues and reset pipeline state.
    void flush();

    // ----------------------------------------------------------------
    // Query
    // ----------------------------------------------------------------

    /// Get the current VSR circuit breaker state.
    VSRState vsrState() const { return vsrState_.load(std::memory_order_acquire); }

    /// Check if the pipeline is running.
    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /// Check if the pipeline is paused.
    bool isPaused() const { return paused_.load(std::memory_order_acquire); }

    /// Get the current VSR circuit breaker state as a human-readable string.
    static const char* vsrStateToString(VSRState state);

    /// Get number of frames dropped by VSR input queue.
    uint64_t vsrDroppedFrames() const;

    /// Get number of frames processed through VSR.
    uint64_t vsrProcessedFrames() const;

    /// Get number of frames passed through (bypassed VSR).
    uint64_t vsrBypassedFrames() const;

    /// Get last measured VSR inference time in milliseconds.
    double lastVSRInferenceMs() const;

private:
    // ----------------------------------------------------------------
    // Stage thread functions
    // ----------------------------------------------------------------

    /// Decode stage: pull packets → decode → push to VSR input queue.
    void decodeStage();

    /// VSR stage: pull decoded frames → super-resolve → push to output queue.
    /// Handles circuit breaker logic and pass-through.
    void vsrStage();

    /// Render stage: pull frames from output queue → present via render bridge.
    void renderStage();

    // ----------------------------------------------------------------
    // Circuit breaker
    // ----------------------------------------------------------------

    /// Check and update circuit breaker state after a VSR inference.
    void updateCircuitBreaker(double inferenceMs);

    /// Transition to probing state (called after cooldown timer expires).
    void startProbing();

    /// Called when a probe frame completes: resume active or stay circuit-open.
    void handleProbeResult(double inferenceMs);

    // ----------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------

    void joinAllThreads();
    void publishError(PlayerError err, const std::string& message);

    // ----------------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------------

    VSRPipelineConfig config_;

    // ----------------------------------------------------------------
    // Pipeline components (set by caller)
    // ----------------------------------------------------------------

    std::shared_ptr<IHWDecoder> decoder_;
    std::shared_ptr<IPipelineNode> vsrNode_;
    std::shared_ptr<IVRAMBudgetManager> vramManager_;
    std::shared_ptr<render::VSRRenderBridge> renderBridge_;
    SyncClock* syncClock_ = nullptr;
    EventBus* eventBus_ = nullptr;
    PacketProvider packetProvider_;
    uint64_t vsrFrameVRAM_ = 16 * 1024 * 1024; // Default 16MB estimate

    // ----------------------------------------------------------------
    // Queues between stages
    // ----------------------------------------------------------------

    std::unique_ptr<PacketQueue> packetQueue_;     // Demux → Decode
    std::unique_ptr<VSRFrameQueue> vsrInputQueue_;  // Decode → VSR (cap 2, drop oldest)
    std::unique_ptr<VideoFrameQueue> outputQueue_;  // VSR → Render (cap 4)

    // ----------------------------------------------------------------
    // Threads
    // ----------------------------------------------------------------

    std::thread decodeThread_;
    std::thread vsrThread_;
    std::thread renderThread_;

    // ----------------------------------------------------------------
    // State
    // ----------------------------------------------------------------

    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stopRequested_{false};

    // ----------------------------------------------------------------
    // VSR Circuit Breaker
    // ----------------------------------------------------------------

    std::atomic<VSRState> vsrState_{VSRState::Active};
    int consecutiveSlowFrames_ = 0;
    std::chrono::steady_clock::time_point circuitOpenTime_;
    std::atomic<bool> probeRequested_{false};

    // ----------------------------------------------------------------
    // Stats
    // ----------------------------------------------------------------

    std::atomic<uint64_t> vsrDroppedFrames_{0};
    std::atomic<uint64_t> vsrProcessedFrames_{0};
    std::atomic<uint64_t> vsrBypassedFrames_{0};
    std::atomic<double> lastVSRInferenceMs_{0.0};

    // ----------------------------------------------------------------
    // Pause synchronization
    // ----------------------------------------------------------------

    std::mutex pauseMutex_;
    std::condition_variable pauseCv_;
};

} // namespace hlplayer

#endif // HLPLAYER_REALTIMEVSRPIPELINE_H
