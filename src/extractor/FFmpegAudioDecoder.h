#ifndef HLPLAYER_FFMPEG_AUDIO_DECODER_H
#define HLPLAYER_FFMPEG_AUDIO_DECODER_H

#include <hlplayer/IAudioDecoder.h>
#include "FFmpegRAII.h"

#include <atomic>
#include <memory>
#include <mutex>

#ifdef _WIN32
    #ifdef HLPLAYER_EXTRACTOR_EXPORTS
        #define HLPLAYER_EXTRACTOR_API __declspec(dllexport)
    #else
        #define HLPLAYER_EXTRACTOR_API __declspec(dlimport)
    #endif
#else
    #define HLPLAYER_EXTRACTOR_API
#endif

extern "C" {
#include <libswresample/swresample.h>
}

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

class HLPLAYER_EXTRACTOR_API FFmpegAudioDecoder : public IAudioDecoder {
public:
    FFmpegAudioDecoder();
    ~FFmpegAudioDecoder() override;

    FFmpegAudioDecoder(const FFmpegAudioDecoder&) = delete;
    FFmpegAudioDecoder& operator=(const FFmpegAudioDecoder&) = delete;

    bool open(const AudioDecodeConfig& config) override;
    std::shared_ptr<AudioFrame> decode(const uint8_t* data, size_t size, int64_t pts) override;
    void flush() override;
    void close() override;

private:
    AVSampleFormat toAVSampleFormat(AudioSampleFormat fmt) const;
    AudioSampleFormat fromAVSampleFormat(AVSampleFormat fmt) const;
    int bytesPerSample(AudioSampleFormat fmt) const;

    AVCodecContextPtr codecCtx_;
    SwrContext* swrCtx_ = nullptr;
    AVFramePtr swrFrame_;
    const AVCodec* codec_ = nullptr;
    bool isOpen_ = false;
    AudioFormat targetFormat_;
    std::mutex mutex_;
};

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_FFMPEG_AUDIO_DECODER_H
