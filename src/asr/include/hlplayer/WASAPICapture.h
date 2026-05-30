#ifndef HLPLAYER_WASAPI_CAPTURE_H
#define HLPLAYER_WASAPI_CAPTURE_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/IAudioCapture.h>
#include <hlplayer/IAudioDecoder.h>

#ifdef _WIN32

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

namespace hlplayer {
namespace asr {

using Microsoft::WRL::ComPtr;

/// WASAPI shared-mode microphone capture.
/// Converts captured audio to 16kHz mono float32 via AudioResampler.
class HLPLAYER_ASR_API WASAPICapture : public IAudioCapture {
public:
    WASAPICapture();
    ~WASAPICapture() override;

    WASAPICapture(const WASAPICapture&) = delete;
    WASAPICapture& operator=(const WASAPICapture&) = delete;

    bool open(const AudioCaptureConfig& config) override;
    void setCallback(AudioCallback callback) override;
    bool start() override;
    void stop() override;
    void close() override;
    bool isCapturing() const override;

    /// Returns {deviceId, displayName} pairs for available microphones.
    std::vector<std::pair<std::string, std::string>> enumerateDevices() const override;

private:
    void captureLoop();
    bool acquireDevice();
    bool activateAudioClient();
    void releaseResources();
    AudioSampleFormat wasapiToSampleFormat(const WAVEFORMATEX* wfx) const;

    // WASAPI COM objects — owned, released in releaseResources()
    ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    WAVEFORMATEX* mixFormat_ = nullptr;

    std::unique_ptr<class AudioResampler> resampler_;

    AudioCaptureConfig config_;
    AudioCallback callback_;
    std::thread captureThread_;
    std::atomic<bool> capturing_{false};
    std::atomic<bool> opened_{false};
    std::atomic<bool> deviceLost_{false};
    mutable std::mutex comMutex_;

    std::chrono::steady_clock::time_point captureStartTime_;
    std::atomic<double> elapsedSeconds_{0.0};
};

} // namespace asr
} // namespace hlplayer

#endif // _WIN32
#endif // HLPLAYER_WASAPI_CAPTURE_H
