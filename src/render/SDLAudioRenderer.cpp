#include "SDLAudioRenderer.h"

#include <SDL2/SDL.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace hlplayer {
namespace render {

SDLAudioRenderer::SDLAudioRenderer() {
    SDL_Init(SDL_INIT_AUDIO);
}

SDLAudioRenderer::~SDLAudioRenderer() {
    close();
}

SDL_AudioFormat SDLAudioRenderer::toSDLAudioFormat(AudioSampleFormat fmt) {
    switch (fmt) {
        case AudioSampleFormat::S16:   return AUDIO_S16SYS;
        case AudioSampleFormat::S32:   return AUDIO_S32SYS;
        case AudioSampleFormat::Float: return AUDIO_F32SYS;
        case AudioSampleFormat::U8:    return AUDIO_U8;
        default:                       return AUDIO_S16SYS;
    }
}

static int sdlBytesPerSample(SDL_AudioFormat fmt) {
    switch (fmt) {
        case AUDIO_U8:      return 1;
        case AUDIO_S16SYS:  return 2;
        case AUDIO_S32SYS:  return 4;
        case AUDIO_F32SYS:  return 4;
        default:            return 2;
    }
}

static AudioSampleFormat fromSDLAudioFormat(SDL_AudioFormat fmt) {
    switch (fmt) {
        case AUDIO_U8:      return AudioSampleFormat::U8;
        case AUDIO_S16SYS:  return AudioSampleFormat::S16;
        case AUDIO_S32SYS:  return AudioSampleFormat::S32;
        case AUDIO_F32SYS:  return AudioSampleFormat::Float;
        default:            return AudioSampleFormat::S16;
    }
}

bool SDLAudioRenderer::open(const AudioFormat& format) {
    if (isOpen_.load()) {
        close();
    }

    SDL_AudioSpec desired;
    std::memset(&desired, 0, sizeof(desired));
    desired.freq = format.sampleRate;
    desired.channels = static_cast<Uint8>(format.channels);
    desired.format = toSDLAudioFormat(format.sampleFormat);
    desired.samples = 4096;
    desired.callback = nullptr;
    desired.userdata = nullptr;

    SDL_AudioSpec obtained;
    const char* deviceName = SDL_GetAudioDeviceName(0, 0);
    if (!deviceName) {
        deviceName = nullptr;
    }

    deviceId_ = SDL_OpenAudioDevice(deviceName, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (deviceId_ == 0) {
        spdlog::error("SDLAudioRenderer: failed to open audio device: {}", SDL_GetError());
        return false;
    }

    obtainedSpec_ = obtained;
    format_.sampleRate = obtained.freq;
    format_.channels = obtained.channels;
    format_.sampleFormat = fromSDLAudioFormat(obtained.format);
    format_.bytesPerSample = sdlBytesPerSample(obtained.format);

    SDL_ClearQueuedAudio(deviceId_);
    SDL_PauseAudioDevice(deviceId_, 0);
    isOpen_.store(true);

    spdlog::info("SDLAudioRenderer: opened device='{}' freq={} channels={} format={} bps={}",
                 deviceName ? deviceName : "default",
                 obtained.freq, obtained.channels,
                 static_cast<int>(format_.sampleFormat), format_.bytesPerSample);
    return true;
}

void SDLAudioRenderer::write(const uint8_t* data, size_t size) {
    if (!isOpen_.load()) {
        return;
    }

    double volume = volume_.load();
    const double kVolumeThreshold = 0.001;

    if (std::abs(volume - 1.0) < kVolumeThreshold) {
        if (SDL_QueueAudio(deviceId_, data, size) < 0) {
            spdlog::warn("SDLAudioRenderer: SDL_QueueAudio failed: {}", SDL_GetError());
        }
        return;
    }

    std::vector<uint8_t> scaledData(size);
    const size_t bytesPerSample = format_.bytesPerSample;

    switch (format_.sampleFormat) {
        case AudioSampleFormat::S16: {
            const int16_t* in = reinterpret_cast<const int16_t*>(data);
            int16_t* out = reinterpret_cast<int16_t*>(scaledData.data());
            size_t numSamples = size / bytesPerSample;
            for (size_t i = 0; i < numSamples; ++i) {
                float scaled = static_cast<float>(in[i]) * volume;
                scaled = std::clamp(scaled, -32768.0f, 32767.0f);
                out[i] = static_cast<int16_t>(scaled);
            }
            break;
        }
        case AudioSampleFormat::S32: {
            const int32_t* in = reinterpret_cast<const int32_t*>(data);
            int32_t* out = reinterpret_cast<int32_t*>(scaledData.data());
            size_t numSamples = size / bytesPerSample;
            for (size_t i = 0; i < numSamples; ++i) {
                double scaled = static_cast<double>(in[i]) * volume;
                scaled = std::clamp(scaled, static_cast<double>(INT32_MIN), static_cast<double>(INT32_MAX));
                out[i] = static_cast<int32_t>(scaled);
            }
            break;
        }
        case AudioSampleFormat::Float: {
            const float* in = reinterpret_cast<const float*>(data);
            float* out = reinterpret_cast<float*>(scaledData.data());
            size_t numSamples = size / bytesPerSample;
            for (size_t i = 0; i < numSamples; ++i) {
                out[i] = in[i] * volume;
            }
            break;
        }
        case AudioSampleFormat::U8: {
            const uint8_t* in = data;
            uint8_t* out = scaledData.data();
            for (size_t i = 0; i < size; ++i) {
                float scaled = static_cast<float>(in[i]) * volume;
                scaled = std::clamp(scaled, 0.0f, 255.0f);
                out[i] = static_cast<uint8_t>(scaled);
            }
            break;
        }
        default: {
            std::memcpy(scaledData.data(), data, size);
            break;
        }
    }

    if (SDL_QueueAudio(deviceId_, scaledData.data(), size) < 0) {
        spdlog::warn("SDLAudioRenderer: SDL_QueueAudio failed: {}", SDL_GetError());
    }
}

void SDLAudioRenderer::pause() {
    if (!isOpen_.load()) {
        return;
    }
    SDL_PauseAudioDevice(deviceId_, 1);
}

void SDLAudioRenderer::resume() {
    if (!isOpen_.load()) {
        return;
    }
    SDL_PauseAudioDevice(deviceId_, 0);
}

void SDLAudioRenderer::close() {
    if (isOpen_.load() && deviceId_ != 0) {
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
    }
    isOpen_.store(false);
}

AudioFormat SDLAudioRenderer::format() const {
    return format_;
}

int SDLAudioRenderer::getLatencyMs() const {
    if (!isOpen_.load() || deviceId_ == 0) {
        return 0;
    }

    Uint32 queuedBytes = SDL_GetQueuedAudioSize(deviceId_);
    if (queuedBytes == 0 || format_.sampleRate == 0 || format_.channels == 0 || format_.bytesPerSample == 0) {
        return 0;
    }

    int bytesPerSec = format_.bytesPerSample * format_.channels * format_.sampleRate;
    return static_cast<int>(queuedBytes * 1000 / bytesPerSec);
}

void SDLAudioRenderer::flush() {
    if (isOpen_.load() && deviceId_ != 0) {
        SDL_ClearQueuedAudio(deviceId_);
    }
}

void SDLAudioRenderer::setVolume(double volume) {
    volume = std::max(0.0, std::min(1.0, volume));
    volume_.store(volume);
}

} // namespace render
} // namespace hlplayer
