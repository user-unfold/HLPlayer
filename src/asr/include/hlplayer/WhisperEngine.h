#ifndef HLPLAYER_WHISPER_ENGINE_H
#define HLPLAYER_WHISPER_ENGINE_H

#include <hlplayer/IASREngine.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct whisper_context;

namespace hlplayer {
namespace asr {

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

    static bool isCUDAAvailable();

    void updateConfig(const ASRConfig& config);

private:
    std::vector<SubtitleSegment> runInference(const std::vector<float>& audio, double basePts);

    bool detectSpeech(const float* frame, size_t frameSize);

    whisper_context* ctx_ = nullptr;
    ASRConfig config_;
    std::atomic<ASRState> state_{ASRState::Idle};

    std::vector<float> audioBuffer_;
    double bufferStartPts_ = 0.0;
    int32_t nextSequenceId_ = 0;
    mutable std::mutex mutex_;

    std::vector<SubtitleSegment> pendingResults_;
    std::mutex resultsMutex_;

    double lastRecognizedEndTime_ = 0.0;
    std::string lastRecognizedText_;

    struct VADState {
        bool inSpeech = false;
        size_t speechStartSample = 0;
        int silenceFrames = 0;
        int speechFrames = 0;
        size_t processedSamples = 0;
    };
    VADState vadState_;

    float noiseFloor_ = 1e-4f;
    int vadFrameCount_ = 0;
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_WHISPER_ENGINE_H
