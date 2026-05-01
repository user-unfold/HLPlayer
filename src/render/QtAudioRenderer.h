#ifndef HLPLAYER_QT_AUDIO_RENDERER_H
#define HLPLAYER_QT_AUDIO_RENDERER_H

#include <hlplayer/IAudioRenderer.h>

#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaDevices>
#include <QPointer>
#include <QThread>
#include <QByteArray>

#include <atomic>
#include <memory>

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

class HLPLAYER_RENDER_API QtAudioRenderer : public IAudioRenderer {
public:
    QtAudioRenderer();
    ~QtAudioRenderer() override;

    QtAudioRenderer(const QtAudioRenderer&) = delete;
    QtAudioRenderer& operator=(const QtAudioRenderer&) = delete;

    bool open(const AudioFormat& format) override;
    void write(const uint8_t* data, size_t size) override;
    void pause() override;
    void resume() override;
    void close() override;
    AudioFormat format() const override;
    int getLatencyMs() const override;

private:
    QAudioFormat toQAudioFormat(const AudioFormat& format) const;
    QAudioFormat::SampleFormat toQSampleFormat(AudioSampleFormat fmt) const;
    AudioSampleFormat fromQSampleFormat(QAudioFormat::SampleFormat fmt) const;
    int bytesPerSample(AudioSampleFormat fmt) const;

    std::unique_ptr<QAudioSink> audioSink_;
    QIODevice* ioDevice_ = nullptr;
    std::atomic<bool> isOpen_{false};
    AudioFormat format_;
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_QT_AUDIO_RENDERER_H
