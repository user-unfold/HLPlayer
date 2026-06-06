#ifndef HLPLAYER_ASR_PIPELINE_H
#define HLPLAYER_ASR_PIPELINE_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/ASRTypes.h>
#include <hlplayer/IASREngine.h>
#include <hlplayer/IAudioCapture.h>
#include <hlplayer/AudioResampler.h>
#include <hlplayer/SubtitleManager.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <concurrentqueue.h>

namespace hlplayer {
namespace asr {

/// Audio chunk delivered via the lock-free queue.
/// Carries raw PCM data from the decode thread; resampling happens
/// on the ASR worker thread to avoid blocking the audio decode path.
struct AudioChunk {
    std::vector<uint8_t> pcmData; ///< Raw interleaved PCM bytes
    double pts = 0.0;             ///< Presentation timestamp in seconds
    int sampleRate = 0;           ///< Source sample rate
    int channels = 0;             ///< Source channel count
    AudioSampleFormat format = AudioSampleFormat::None; ///< Source sample format
};

/// Callback fired when new subtitle segments are available
using SubtitleCallback = std::function<void(const std::vector<SubtitleSegment>&)>;

/// Callback fired when ASR state changes
using StateCallback = std::function<void(ASRState oldState, ASRState newState)>;

/// Complete ASR pipeline: audio capture → resampling → VAD → recognition → subtitle output.
/// Runs recognition on a dedicated background thread.
class HLPLAYER_ASR_API ASRPipeline {
public:
    ASRPipeline();
    ~ASRPipeline();

    ASRPipeline(const ASRPipeline&) = delete;
    ASRPipeline& operator=(const ASRPipeline&) = delete;

    /// Initialize the pipeline with the given configuration.
    /// Loads the Whisper model (may be slow on first call).
    bool initialize(const ASRConfig& config);

    /// Start recognition. Begins consuming audio and producing subtitles.
    bool start();

    /// Pause recognition (e.g. when video is paused).
    /// Processes remaining audio in buffer before pausing.
    void pause();

    /// Resume recognition after pause.
    void resume();

    /// Stop recognition and release capture resources.
    void stop();

    /// Shut down the entire pipeline, including the Whisper engine.
    void shutdown();

    /// Release cached model and free memory immediately.
    /// Normally model is kept in cache for cacheDurationSeconds after shutdown.
    void releaseCache();

    /// Reset state for seek / video switch.
    /// Clears audio buffer and pending results.
    void reset();

    /// Feed audio from the video playback pipeline (VideoTrack mode).
    /// Called from the audio decode thread — must be non-blocking.
    /// @param frame  Decoded audio frame (any format — will be resampled internally)
    void feedAudioFrame(const AudioFrame& frame);

    /// Set callback for new subtitle segments.
    void setSubtitleCallback(SubtitleCallback callback);

    /// Set callback for state changes.
    void setStateCallback(StateCallback callback);

    /// Get the subtitle manager for history/export access.
    SubtitleManager& subtitleManager() { return subtitleManager_; }
    const SubtitleManager& subtitleManager() const { return subtitleManager_; }

    /// Get current pipeline state.
    ASRState state() const;

    /// Get current configuration.
    const ASRConfig& config() const { return config_; }

    /// Get current audio chunk queue size.
    size_t queueSize() const;

    /// Preload the Whisper model in a background thread.
    /// Call this when playback starts so the model is ready when the user enables ASR.
    /// Returns immediately; model loads asynchronously.
    /// @param onComplete  Called on the main thread (via QTimer) when loading finishes.
    ///                    Pass nullptr if no notification is needed.
    void preloadModel(const ASRConfig& config, std::function<void(bool success)> onComplete = nullptr);

    /// List available Whisper models in the models directory.
    static std::vector<WhisperModelInfo> listAvailableModels(const std::string& modelsDir);

    /// Test-only: configure queue behavior without loading a model.
    /// Sets maxQueueSize_ and running_ = true to allow feedAudioFrame() calls.
    /// Does NOT initialize the Whisper engine or start the worker thread.
    /// This is only for testing queue behavior (dropping, non-blocking, etc.).
    void setQueueConfigForTesting(size_t maxQueueSize);

private:
    /// Background worker thread function.
    void workerLoop();

    /// Transition to a new state and fire callback.
    void setState(ASRState newState);

    ASRConfig config_;
    std::unique_ptr<IASREngine> engine_;
    std::unique_ptr<IAudioCapture> capture_;
    AudioResampler resampler_;
    SubtitleManager subtitleManager_;

    // Lock-free queue for audio data (producer: capture/playback thread, consumer: worker thread)
    moodycamel::ConcurrentQueue<AudioChunk> audioQueue_;

    // Max queue size (drops oldest chunks when exceeded)
    size_t maxQueueSize_;

    // Worker thread
    std::thread workerThread_;
    std::thread preloadThread_;  // Background model preload thread
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<ASRState> state_{ASRState::Idle};

    // Callbacks
    SubtitleCallback subtitleCallback_;
    StateCallback stateCallback_;
    std::mutex callbackMutex_;

    // Resampler configuration state
    bool resamplerConfigured_ = false;

    // PTS tracking: compute effective PTS from accumulated resampled output
    // to eliminate drift caused by resampler buffering.
    double resampledBasePts_ = -1.0;
    int64_t totalResampledSamples_ = 0;

    std::chrono::steady_clock::time_point lastUsedTime_;
    std::chrono::seconds cacheDuration_{300};
    bool modelCached_{false};
    std::atomic<bool> preloading_{false};
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_ASR_PIPELINE_H
