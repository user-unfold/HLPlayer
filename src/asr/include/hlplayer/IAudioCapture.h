#ifndef HLPLAYER_IAUDIO_CAPTURE_H
#define HLPLAYER_IAUDIO_CAPTURE_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/ASRTypes.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace hlplayer {
namespace asr {

/// Pure virtual audio capture interface.
/// Implementations provide audio data from various sources
/// (microphone, system audio, video track).
/// Platform-specific (WASAPI on Windows, CoreAudio on macOS, etc.)
class HLPLAYER_ASR_API IAudioCapture {
public:
    virtual ~IAudioCapture() = default;

    /// Callback type for delivering captured audio.
    /// @param samples  Float32 PCM samples (16kHz, mono)
    /// @param count    Number of samples
    /// @param pts      Timestamp in seconds (0.0 for real-time sources)
    using AudioCallback = std::function<void(const float* samples, size_t count, double pts)>;

    /// Open the capture device/source.
    virtual bool open(const AudioCaptureConfig& config) = 0;

    /// Set the callback to receive captured audio data.
    /// Must be called before start().
    virtual void setCallback(AudioCallback callback) = 0;

    /// Start capturing audio.
    virtual bool start() = 0;

    /// Stop capturing audio.
    virtual void stop() = 0;

    /// Close the capture device and release resources.
    virtual void close() = 0;

    /// Check whether the source is actively capturing.
    virtual bool isCapturing() const = 0;

    /// Enumerate available capture devices (for mic/system audio).
    /// Returns list of {deviceId, displayName} pairs.
    virtual std::vector<std::pair<std::string, std::string>> enumerateDevices() const = 0;
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_IAUDIO_CAPTURE_H
