#ifndef HLPLAYER_WHISPER_ENGINE_H
#define HLPLAYER_WHISPER_ENGINE_H

#include <hlplayer/IASREngine.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declare whisper.cpp types to avoid exposing whisper.h in public header
struct whisper_context;

namespace hlplayer {
namespace asr {

/// Whisper.cpp-based ASR engine implementation.
/// Supports CUDA GPU acceleration with automatic CPU fallback.
class HLPLAYER_ASR_API WhisperEngine : public IASREngine {
public:
    WhisperEngine();
    ~WhisperEngine() override;

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    // IASREngine interface
    bool initialize(const ASRConfig& config) override;
    void feedAudio(const float* samples, size_t count, double pts) override;
    std::vector<SubtitleSegment> getResults() override;
    void reset() override;
    void shutdown() override;
    bool isReady() const override;
    ASRState state() const override;

    /// Detect whether CUDA GPU acceleration is available for Whisper.
    static bool isCUDAAvailable();

    /// Update runtime config (segment length, VAD threshold, etc.) without reloading model.
    void updateConfig(const ASRConfig& config);

private:
    /// Run Whisper inference on the accumulated audio buffer.
    /// Called internally when enough audio has been collected.
    std::vector<SubtitleSegment> runInference(const std::vector<float>& audio, double basePts);

    whisper_context* ctx_ = nullptr;
    ASRConfig config_;
    std::atomic<ASRState> state_{ASRState::Idle};

    // Audio accumulation buffer
    std::vector<float> audioBuffer_;
    double bufferStartPts_ = 0.0;
    int32_t nextSequenceId_ = 0;
    mutable std::mutex mutex_;

    // Completed results waiting to be retrieved
    std::vector<SubtitleSegment> pendingResults_;
    std::mutex resultsMutex_;
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_WHISPER_ENGINE_H
