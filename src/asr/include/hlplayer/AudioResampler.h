#ifndef HLPLAYER_AUDIO_RESAMPLER_H
#define HLPLAYER_AUDIO_RESAMPLER_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/IAudioDecoder.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Forward declare FFmpeg types
struct SwrContext;
extern "C" void swr_free(SwrContext** ctx);

namespace hlplayer {
namespace asr {

/// RAII deleter for FFmpeg SwrContext.
struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const noexcept {
        if (ctx) swr_free(&ctx);
    }
};

/// Resamples audio to 16kHz mono float32 as required by Whisper.
/// Wraps FFmpeg libswresample for format/rate/channel conversion.
class HLPLAYER_ASR_API AudioResampler {
public:
    AudioResampler();
    ~AudioResampler() = default;

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    /// Configure the resampler for the given source format.
    /// Output is always 16kHz, mono, float32.
    /// @param srcSampleRate   Source sample rate (e.g. 44100, 48000)
    /// @param srcChannels     Source channel count (1, 2, ...)
    /// @param srcFormat       Source sample format
    /// @return true on success
    bool configure(int srcSampleRate, int srcChannels, AudioSampleFormat srcFormat);

    /// Resample audio data from source format to Whisper format (16kHz mono float32).
    /// @param srcData    Pointer to source PCM data
    /// @param srcSize    Size of source data in bytes
    /// @return Vector of float32 samples at 16kHz mono, empty on failure
    std::vector<float> resample(const uint8_t* srcData, size_t srcSize);

    /// Resample an AudioFrame to Whisper format.
    /// Convenience wrapper that extracts format info from the frame.
    std::vector<float> resample(const AudioFrame& frame);

    /// Flush any remaining buffered samples from the resampler.
    std::vector<float> flush();

    /// Reset the resampler state (e.g. after seek).
    void reset();

    /// Check if the resampler has been configured.
    bool isConfigured() const { return configured_; }

private:
    int toFFmpegSampleFormat(AudioSampleFormat fmt) const;

    std::unique_ptr<SwrContext, SwrContextDeleter> swrCtx_;
    bool configured_ = false;

    // Source format parameters (cached for computing sample counts)
    int srcSampleRate_ = 0;
    int srcChannels_ = 0;
    int srcBytesPerSample_ = 0;

    static constexpr int kTargetSampleRate = 16000;
    static constexpr int kTargetChannels = 1;
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_AUDIO_RESAMPLER_H
