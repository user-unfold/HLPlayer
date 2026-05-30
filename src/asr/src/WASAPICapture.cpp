#ifdef _WIN32

#include <hlplayer/WASAPICapture.h>
#include <hlplayer/AudioResampler.h>
#include <hlplayer/IAudioDecoder.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <functional>

#include <ksmedia.h>
#include <wrl/client.h>
#include <functiondiscoverykeys_devpkey.h>

// MinGW does not provide PKEY_Device_FriendlyName in any import library.
// Define it locally with the well-known GUID {A45C254E-DF1C-4EFD-8020-67D146A850E0}, PID 14.
static const PROPERTYKEY s_PKEY_Device_FriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14
};
#define PKEY_Device_FriendlyName s_PKEY_Device_FriendlyName

// Convert wide string (BSTR/wchar_t*) to UTF-8 std::string without _com_util (MinGW compat)
static std::string wideToUtf8(const wchar_t* ws) {
    if (!ws) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, result.data(), len, nullptr, nullptr);
    return result;
}

using Microsoft::WRL::ComPtr;

namespace hlplayer {
namespace asr {

static constexpr REFERENCE_TIME kReftimesPerSec = 10'000'000; // 100ns units
static constexpr REFERENCE_TIME kBufferDuration = kReftimesPerSec / 2; // 500ms buffer
static constexpr DWORD kAudioClientThreadFlags = COINIT_MULTITHREADED;

WASAPICapture::WASAPICapture() {
    resampler_ = std::make_unique<AudioResampler>();
}

WASAPICapture::~WASAPICapture() {
    close();
}

bool WASAPICapture::open(const AudioCaptureConfig& config) {
    if (opened_.load()) {
        spdlog::warn("WASAPICapture: already open");
        return true;
    }

    config_ = config;

    HRESULT hr = CoInitializeEx(nullptr, kAudioClientThreadFlags);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        spdlog::error("WASAPICapture: CoInitializeEx failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    if (!acquireDevice()) {
        return false;
    }

    if (!activateAudioClient()) {
        releaseResources();
        return false;
    }

    opened_.store(true);
    const char* mode = (config_.sourceType == AudioSourceType::SystemAudio) ? "loopback" : "capture";
    spdlog::info("WASAPICapture: {} device '{}'", mode, config_.deviceId.empty() ? "(default)" : config_.deviceId);
    return true;
}

void WASAPICapture::setCallback(AudioCallback callback) {
    std::lock_guard<std::mutex> lock(comMutex_);
    callback_ = std::move(callback);
}

bool WASAPICapture::start() {
    if (!opened_.load()) {
        spdlog::error("WASAPICapture: cannot start — not opened");
        return false;
    }

    if (capturing_.load()) {
        spdlog::warn("WASAPICapture: already capturing");
        return true;
    }

    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        spdlog::error("WASAPICapture: IAudioClient::Start failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    capturing_.store(true);
    deviceLost_.store(false);
    elapsedSeconds_.store(0.0);
    captureStartTime_ = std::chrono::steady_clock::now();

    captureThread_ = std::thread(&WASAPICapture::captureLoop, this);
    spdlog::info("WASAPICapture: capture started");
    return true;
}

void WASAPICapture::stop() {
    if (!capturing_.load()) {
        return;
    }

    capturing_.store(false);

    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(comMutex_);
        if (audioClient_) {
            audioClient_->Stop();
            audioClient_->Reset();
        }
    }

    // Flush resampler remaining samples
    if (resampler_ && resampler_->isConfigured()) {
        auto remaining = resampler_->flush();
        if (!remaining.empty() && callback_) {
            callback_(remaining.data(), remaining.size(), elapsedSeconds_.load());
        }
        resampler_->reset();
    }

    spdlog::info("WASAPICapture: capture stopped");
}

void WASAPICapture::close() {
    stop();

    releaseResources();
    opened_.store(false);
    deviceLost_.store(false);
    spdlog::info("WASAPICapture: closed");
}

bool WASAPICapture::isCapturing() const {
    return capturing_.load();
}

std::vector<std::pair<std::string, std::string>> WASAPICapture::enumerateDevices() const {
    std::vector<std::pair<std::string, std::string>> devices;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);
    if (!coInitialized && hr != RPC_E_CHANGED_MODE) {
        return devices;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        if (coInitialized) CoUninitialize();
        return devices;
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.GetAddressOf());
    if (FAILED(hr)) {
        if (coInitialized) CoUninitialize();
        return devices;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        hr = collection->Item(i, dev.GetAddressOf());
        if (FAILED(hr)) continue;

        LPWSTR deviceId = nullptr;
        dev->GetId(&deviceId);
        std::string idStr = deviceId ? wideToUtf8(deviceId) : "";
        if (deviceId) CoTaskMemFree(deviceId);

        ComPtr<IPropertyStore> props;
        hr = dev->OpenPropertyStore(STGM_READ, props.GetAddressOf());
        if (FAILED(hr)) continue;

        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
        std::string nameStr;
        if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
            nameStr = wideToUtf8(varName.pwszVal);
        }
        PropVariantClear(&varName);

        if (!idStr.empty() && !nameStr.empty()) {
            devices.emplace_back(std::move(idStr), std::move(nameStr));
        }
    }

    if (coInitialized) CoUninitialize();
    spdlog::info("WASAPICapture: enumerated {} microphone(s)", devices.size());
    return devices;
}

void WASAPICapture::captureLoop() {
    spdlog::debug("WASAPICapture: capture thread started");

    while (capturing_.load()) {
        UINT32 packetLength = 0;
        HRESULT hr;

        {
            std::lock_guard<std::mutex> lock(comMutex_);
            if (!captureClient_) break;

            hr = captureClient_->GetNextPacketSize(&packetLength);
        }

        if (FAILED(hr)) {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                spdlog::warn("WASAPICapture: device invalidated/disconnected");
                deviceLost_.store(true);
                break;
            }
            spdlog::error("WASAPICapture: GetNextPacketSize failed (hr=0x{:08X})", static_cast<unsigned>(hr));
            break;
        }

        while (packetLength > 0 && capturing_.load()) {
            BYTE* pData = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            UINT64 devicePosition = 0;

            {
                std::lock_guard<std::mutex> lock(comMutex_);
                if (!captureClient_) break;

                hr = captureClient_->GetBuffer(&pData, &numFrames, &flags, &devicePosition, nullptr);
            }

            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                    spdlog::warn("WASAPICapture: device invalidated during capture");
                    deviceLost_.store(true);
                    break;
                }
                spdlog::error("WASAPICapture: GetBuffer failed (hr=0x{:08X})", static_cast<unsigned>(hr));
                break;
            }

            if (numFrames > 0 && pData && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                size_t dataSize = static_cast<size_t>(numFrames) * mixFormat_->nBlockAlign;
                auto samples = resampler_->resample(pData, dataSize);

                if (!samples.empty()) {
                    auto now = std::chrono::steady_clock::now();
                    double pts = std::chrono::duration<double>(now - captureStartTime_).count();
                    elapsedSeconds_.store(pts);

                    AudioCallback cb;
                    {
                        std::lock_guard<std::mutex> lock(comMutex_);
                        cb = callback_;
                    }
                    if (cb) {
                        cb(samples.data(), samples.size(), pts);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(comMutex_);
                if (captureClient_) {
                    hr = captureClient_->ReleaseBuffer(numFrames);
                }
            }

            if (FAILED(hr)) {
                spdlog::error("WASAPICapture: ReleaseBuffer failed (hr=0x{:08X})", static_cast<unsigned>(hr));
                break;
            }

            {
                std::lock_guard<std::mutex> lock(comMutex_);
                if (!captureClient_) break;
                hr = captureClient_->GetNextPacketSize(&packetLength);
            }
            if (FAILED(hr)) break;
        }

        if (!capturing_.load()) break;

        // Sleep until next buffer period (~10ms for typical 10ms WASAPI buffer)
        // CoSleep is not used; std::this_thread::sleep_for is fine since
        // WASAPI signals via GetNextPacketSize returning 0 when no data is available.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Device was lost — stop capturing cleanly
    if (deviceLost_.load()) {
        capturing_.store(false);
        spdlog::warn("WASAPICapture: capture thread exiting due to device loss");
    }

    spdlog::debug("WASAPICapture: capture thread stopped");
}

bool WASAPICapture::acquireDevice() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(deviceEnumerator_.GetAddressOf()));
    if (FAILED(hr)) {
        spdlog::error("WASAPICapture: failed to create MMDeviceEnumerator (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    if (config_.deviceId.empty()) {
        // For loopback mode, use the render (output) endpoint so we can
        // capture the audio that is being played through the speakers.
        EDataFlow dataFlow = (config_.sourceType == AudioSourceType::SystemAudio)
                                 ? eRender
                                 : eCapture;
        hr = deviceEnumerator_->GetDefaultAudioEndpoint(dataFlow, eConsole, device_.GetAddressOf());
        if (FAILED(hr)) {
            spdlog::error("WASAPICapture: no default {} endpoint (hr=0x{:08X})",
                          dataFlow == eRender ? "render" : "capture",
                          static_cast<unsigned>(hr));
            return false;
        }
    } else {
        // Convert std::string deviceId to wide string for WASAPI
        int wlen = MultiByteToWideChar(CP_UTF8, 0, config_.deviceId.c_str(), -1, nullptr, 0);
        if (wlen <= 0) {
            spdlog::error("WASAPICapture: invalid deviceId encoding");
            return false;
        }
        std::wstring wideId(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, config_.deviceId.c_str(), -1, wideId.data(), wlen);

        hr = deviceEnumerator_->GetDevice(wideId.c_str(), device_.GetAddressOf());
        if (FAILED(hr)) {
            spdlog::error("WASAPICapture: device '{}' not found (hr=0x{:08X})", config_.deviceId, static_cast<unsigned>(hr));
            return false;
        }
    }

    return true;
}

bool WASAPICapture::activateAudioClient() {
    HRESULT hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(audioClient_.GetAddressOf()));
    if (FAILED(hr)) {
        spdlog::error("WASAPICapture: failed to activate IAudioClient (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    // Get the mix format (device's native format)
    hr = audioClient_->GetMixFormat(&mixFormat_);
    if (FAILED(hr) || !mixFormat_) {
        spdlog::error("WASAPICapture: GetMixFormat failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (config_.sourceType == AudioSourceType::SystemAudio) {
        streamFlags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  streamFlags,
                                  kBufferDuration, 0,
                                  mixFormat_, nullptr);
    if (FAILED(hr)) {
        DWORD retryFlags = (config_.sourceType == AudioSourceType::SystemAudio)
                               ? AUDCLNT_STREAMFLAGS_LOOPBACK
                               : 0;
        hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      retryFlags,
                                      kBufferDuration, 0,
                                      mixFormat_, nullptr);
        if (FAILED(hr)) {
            spdlog::error("WASAPICapture: IAudioClient::Initialize failed (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }
    }

    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(captureClient_.GetAddressOf()));
    if (FAILED(hr)) {
        spdlog::error("WASAPICapture: GetService(IAudioCaptureClient) failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    // Configure resampler: device format -> 16kHz mono float32
    AudioSampleFormat srcFormat = wasapiToSampleFormat(mixFormat_);
    if (srcFormat == AudioSampleFormat::None) {
        spdlog::error("WASAPICapture: unsupported WASAPI format (wFormatTag=0x{:04X}, wBitsPerSample={})",
                      mixFormat_->wFormatTag, mixFormat_->wBitsPerSample);
        return false;
    }

    if (!resampler_->configure(mixFormat_->nSamplesPerSec, mixFormat_->nChannels, srcFormat)) {
        spdlog::error("WASAPICapture: failed to configure resampler");
        return false;
    }

    spdlog::info("WASAPICapture: {}Hz {}ch {}bit -> 16kHz mono float32",
                 mixFormat_->nSamplesPerSec, mixFormat_->nChannels, mixFormat_->wBitsPerSample);
    return true;
}

void WASAPICapture::releaseResources() {
    captureClient_.Reset();
    audioClient_.Reset();
    device_.Reset();
    deviceEnumerator_.Reset();
    if (mixFormat_) {
        CoTaskMemFree(mixFormat_);
        mixFormat_ = nullptr;
    }

    CoUninitialize();
}

AudioSampleFormat WASAPICapture::wasapiToSampleFormat(const WAVEFORMATEX* wfx) const {
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return AudioSampleFormat::Float;
    }
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            return AudioSampleFormat::Float;
        }
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            switch (wfx->wBitsPerSample) {
                case 16: return AudioSampleFormat::S16;
                case 32: return AudioSampleFormat::S32;
                case 8:  return AudioSampleFormat::U8;
                default: return AudioSampleFormat::None;
            }
        }
    }
    if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        switch (wfx->wBitsPerSample) {
            case 16: return AudioSampleFormat::S16;
            case 32: return AudioSampleFormat::S32;
            case 8:  return AudioSampleFormat::U8;
            default: return AudioSampleFormat::None;
        }
    }
    return AudioSampleFormat::None;
}

} // namespace asr
} // namespace hlplayer

#endif // _WIN32
