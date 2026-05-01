#ifndef HLPLAYER_SDL_AUDIO_RENDERER_H
#define HLPLAYER_SDL_AUDIO_RENDERER_H

#include <hlplayer/IAudioRenderer.h>

#include <SDL2/SDL.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

#ifdef _WIN32
    #ifdef HLPLAYER_RENDER_EXPORTS
        #define HLPLAYER_RENDER_API __declspec(dllexport)
    #else
        #define HLPLAYER_RENDER_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_RENDER_API
#endif

namespace hlplayer {
namespace render {

class HLPLAYER_RENDER_API SDLAudioRenderer : public IAudioRenderer {
public:
    SDLAudioRenderer();
    ~SDLAudioRenderer() override;

    SDLAudioRenderer(const SDLAudioRenderer&) = delete;
    SDLAudioRenderer& operator=(const SDLAudioRenderer&) = delete;

    bool open(const AudioFormat& format) override;
    void write(const uint8_t* data, size_t size) override;
    void pause() override;
    void resume() override;
    void close() override;
    AudioFormat format() const override;
    int getLatencyMs() const override;
    void flush() override;
    void setVolume(double volume) override;

private:
    static SDL_AudioFormat toSDLAudioFormat(AudioSampleFormat fmt);

    SDL_AudioDeviceID deviceId_ = 0;
    SDL_AudioSpec obtainedSpec_{};
    std::atomic<bool> isOpen_{false};
    std::atomic<double> volume_{1.0};
    AudioFormat format_;
    std::mutex mutex_;
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_SDL_AUDIO_RENDERER_H
