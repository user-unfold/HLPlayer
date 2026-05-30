#ifndef HLPLAYER_IASR_ENGINE_H
#define HLPLAYER_IASR_ENGINE_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/ASRTypes.h>

#include <functional>
#include <vector>

namespace hlplayer {
namespace asr {

/// Pure virtual ASR engine interface.
/// Implementations wrap a speech recognition backend (e.g. Whisper.cpp).
class HLPLAYER_ASR_API IASREngine {
public:
    virtual ~IASREngine() = default;

    /// Initialize the engine with the given configuration.
    /// Loads the model file — may take several seconds.
    virtual bool initialize(const ASRConfig& config) = 0;

    /// Feed raw audio samples (16kHz, mono, float32) for recognition.
    /// @param samples  Pointer to float32 PCM samples
    /// @param count    Number of samples (not bytes)
    /// @param pts      Presentation timestamp in seconds (for time alignment)
    virtual void feedAudio(const float* samples, size_t count, double pts) = 0;

    /// Retrieve any completed subtitle segments since last call.
    /// Returns empty vector if no new results are available.
    virtual std::vector<SubtitleSegment> getResults() = 0;

    /// Reset internal state (e.g. after seek). Clears audio buffer and pending results.
    virtual void reset() = 0;

    /// Shut down the engine and release resources (model, GPU context).
    virtual void shutdown() = 0;

    /// Check whether the engine is initialized and ready to process audio.
    virtual bool isReady() const = 0;

    /// Get the current engine state.
    virtual ASRState state() const = 0;

    /// Update runtime config without reloading the model.
    virtual void updateConfig(const ASRConfig& config) = 0;
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_IASR_ENGINE_H
