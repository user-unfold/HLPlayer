#ifndef HLPLAYER_OFFLINE_TRANSCODE_PIPELINE_H
#define HLPLAYER_OFFLINE_TRANSCODE_PIPELINE_H

#include <hlplayer/Export.h>
#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IVideoEncoder.h>
#include <hlplayer/IMuxer.h>
#include <hlplayer/ICheckpointManager.h>
#include <hlplayer/IVRAMBudgetManager.h>
#include <hlplayer/PacketQueue.h>
#include <hlplayer/FrameQueue.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace hlplayer {

class IPipelineNode;

/// Progress information reported by the offline transcode pipeline.
struct TranscodeProgress {
    uint64_t framesProcessed = 0;    ///< Video frames encoded so far
    uint64_t totalFrames = 0;        ///< Total video frames (0 if unknown)
    double currentFps = 0.0;         ///< Measured encoding throughput
    double estimatedSecondsLeft = 0; ///< ETA in seconds (0 if unknown)
    std::string stage;               ///< Current pipeline stage name
    bool isComplete = false;         ///< True when all frames processed
};

/// Configuration for the offline transcode pipeline.
struct OfflineTranscodeConfig {
    // --- I/O ---
    std::string inputPath;           ///< Source video file path
    std::string outputPath;          ///< Output file path
    std::string outputFormat = "mp4";///< Container format ("mp4", "mkv")
    bool fastStart = true;           ///< Move moov atom to front (MP4)

    // --- Decoder ---
    DecodeBackend decodeBackend = DecodeBackend::Auto;

    // --- VSR ---
    std::string vsrModelPath;        ///< Base path for NCNN model (.param + .bin)
    int vsrScaleFactor = 2;          ///< Super-resolution scale factor (2, 3, or 4)

    // --- Encoder ---
    EncoderConfig encoderConfig;     ///< Encoder settings (codec, bitrate, etc.)

    // --- Streams ---
    bool audioPassthrough = true;    ///< Copy audio without re-encoding

    // --- Audio passthrough info (filled by caller) ---
    int audioCodecId = 0;            ///< AVCodecID as int (e.g. AV_CODEC_ID_AAC)
    int audioSampleRate = 0;
    int audioChannels = 0;
    std::vector<uint8_t> audioExtradata;

    // --- Checkpoint ---
    bool enableCheckpoint = true;    ///< Enable checkpoint save/restore
    uint64_t checkpointInterval = 100; ///< Save checkpoint every N frames

    // --- Queue sizing ---
    size_t packetQueueSize = 200;    ///< Max packets between demux and decode
    size_t frameQueueSize = 8;       ///< Max GPU frames between stages
    size_t encodedQueueSize = 200;   ///< Max encoded packets between encode and mux
};

class HLPLAYER_CORE_API OfflineTranscodePipeline {
public:
    OfflineTranscodePipeline();
    ~OfflineTranscodePipeline();

    OfflineTranscodePipeline(const OfflineTranscodePipeline&) = delete;
    OfflineTranscodePipeline& operator=(const OfflineTranscodePipeline&) = delete;

    // -----------------------------------------------------------------------
    // Component injection (call before start())
    // -----------------------------------------------------------------------

    /// Set the hardware decoder. Must be called before start().
    void setDecoder(std::shared_ptr<IHWDecoder> decoder);

    /// Set the video encoder. Must be called before start().
    void setEncoder(std::shared_ptr<IVideoEncoder> encoder);

    /// Set the muxer. Must be called before start().
    void setMuxer(std::shared_ptr<IMuxer> muxer);

    /// Set the checkpoint manager (optional).
    void setCheckpointManager(std::shared_ptr<ICheckpointManager> checkpointManager);

    /// Set the VRAM budget manager (optional but recommended).
    void setVRAMBudgetManager(std::shared_ptr<IVRAMBudgetManager> vramManager);

    /// Set the VSR pipeline node for super-resolution processing.
    void setVSRNode(std::shared_ptr<IPipelineNode> vsrNode);

    // -----------------------------------------------------------------------
    // Pipeline control
    // -----------------------------------------------------------------------

    /// Configure and start the pipeline. Returns error if components are missing.
    Result<void> configure(const OfflineTranscodeConfig& config);

    /// Begin processing (launches all stage threads).
    Result<void> start();

    /// Request cancellation. All stages will finish their current work and stop.
    void cancel();

    /// Request pause. Stages will pause between items.
    void pause();

    /// Resume from paused state.
    void resume();

    /// Block until all stages finish or error occurs.
    Result<void> waitUntilComplete();

    /// Get current progress.
    TranscodeProgress getProgress() const;

    // -----------------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------------

    /// Progress callback. Called from the muxer thread periodically.
    using ProgressCallback = std::function<void(const TranscodeProgress&)>;

    /// Set progress callback.
    void setProgressCallback(ProgressCallback callback);

    /// Error callback. Called when any stage encounters an error.
    using ErrorCallback = std::function<void(PlayerError, const std::string&)>;

    /// Set error callback.
    void setErrorCallback(ErrorCallback callback);

    // -----------------------------------------------------------------------
    // State queries
    // -----------------------------------------------------------------------

    enum class State : uint8_t {
        Idle = 0,
        Configured,
        Running,
        Paused,
        Completed,
        Cancelled,
        Error
    };

    State getState() const;

    std::string stateToString(State state) const;

private:
    // -----------------------------------------------------------------------
    // Stage thread functions
    // -----------------------------------------------------------------------

    /// Stage 1: Demux — read input file, produce MediaPackets.
    void demuxStage();

    /// Stage 2: Decode — decode video packets to GpuFrames.
    void decodeStage();

    /// Stage 3: VSR — super-resolve frames via NcnnSuperResolution.
    void vsrStage();

    /// Stage 4: Encode — encode super-resolved frames.
    void encodeStage();

    /// Stage 5: Mux — write encoded packets to output file.
    void muxStage();

    /// Audio passthrough thread — copy audio packets directly.
    void audioPassthroughStage();

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Save checkpoint if interval elapsed.
    void maybeSaveCheckpoint();

    /// Report progress via callback.
    void reportProgress();

    /// Propagate error to all stages and set error state.
    void setError(PlayerError err, const std::string& msg);

    /// Check if pipeline should keep running.
    bool shouldRun() const;

    /// Wait while paused.
    void waitIfPaused();

    /// Shutdown all inter-stage queues.
    void shutdownQueues();

    static constexpr size_t kDefaultPacketQueueSize = 200;
    static constexpr size_t kDefaultFrameQueueSize = 8;
    static constexpr size_t kDefaultEncodedQueueSize = 200;
    class EncodedPacketQueue {
    public:
        explicit EncodedPacketQueue(size_t capacity = 200)
            : capacity_(capacity) {}

        bool push(EncodedPacket pkt) {
            std::unique_lock lock(mutex_);
            notFull_.wait(lock, [this] { return queue_.size() < capacity_ || shutdown_; });
            if (shutdown_) return false;
            queue_.push(std::move(pkt));
            notEmpty_.notify_one();
            return true;
        }

        bool pop(EncodedPacket& out, int timeoutMs = -1) {
            std::unique_lock lock(mutex_);
            if (timeoutMs < 0) {
                notEmpty_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
            } else {
                notEmpty_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                   [this] { return !queue_.empty() || shutdown_; });
            }
            if (queue_.empty()) return false;
            out = std::move(queue_.front());
            queue_.pop();
            notFull_.notify_one();
            return true;
        }

        size_t size() const {
            std::lock_guard lock(mutex_);
            return queue_.size();
        }

        void shutdown() {
            std::lock_guard lock(mutex_);
            shutdown_ = true;
            notEmpty_.notify_all();
            notFull_.notify_all();
        }

        bool isShutdown() const {
            std::lock_guard lock(mutex_);
            return shutdown_;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable notEmpty_;
        std::condition_variable notFull_;
        std::queue<EncodedPacket> queue_;
        size_t capacity_;
        bool shutdown_ = false;
    };

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    // Configuration
    OfflineTranscodeConfig config_;

    // Injected components
    std::shared_ptr<IHWDecoder> decoder_;
    std::shared_ptr<IVideoEncoder> encoder_;
    std::shared_ptr<IMuxer> muxer_;
    std::shared_ptr<ICheckpointManager> checkpointManager_;
    std::shared_ptr<IVRAMBudgetManager> vramManager_;
    std::shared_ptr<IPipelineNode> vsrNode_;

    std::unique_ptr<PacketQueue> videoPacketQueue_;
    std::unique_ptr<PacketQueue> audioPacketQueue_;
    std::unique_ptr<VideoFrameQueue> decodedFrameQueue_;
    std::unique_ptr<VideoFrameQueue> vsrFrameQueue_;
    std::unique_ptr<EncodedPacketQueue> encodedQueue_;

    // Stream indices (assigned by muxer)
    uint32_t videoStreamIndex_ = 0;
    uint32_t audioStreamIndex_ = 0;

    // State
    mutable std::mutex stateMutex_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> pauseRequested_{false};
    std::atomic<bool> errorFlag_{false};
    PlayerError lastError_ = PlayerError::None;
    std::string lastErrorMsg_;

    // Progress tracking
    mutable std::mutex progressMutex_;
    std::atomic<uint64_t> framesProcessed_{0};
    std::atomic<uint64_t> totalFrames_{0};
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<double> currentFps_{0.0};

    // Pause synchronization
    std::mutex pauseMutex_;
    std::condition_variable pauseCv_;

    // Completion synchronization
    std::mutex completionMutex_;
    std::condition_variable completionCv_;
    Result<void> completionResult_ = Result<void>::success();

    // Threads
    std::thread demuxThread_;
    std::thread decodeThread_;
    std::thread vsrThread_;
    std::thread encodeThread_;
    std::thread muxThread_;
    std::thread audioThread_;

    // Callbacks
    ProgressCallback progressCallback_;
    ErrorCallback errorCallback_;
};

} // namespace hlplayer

#endif // HLPLAYER_OFFLINE_TRANSCODE_PIPELINE_H
