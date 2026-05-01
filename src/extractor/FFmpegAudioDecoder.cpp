#include "FFmpegAudioDecoder.h"

#include <spdlog/spdlog.h>
#include <cstring>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

FFmpegAudioDecoder::FFmpegAudioDecoder() = default;

FFmpegAudioDecoder::~FFmpegAudioDecoder() {
    close();
}

AVSampleFormat FFmpegAudioDecoder::toAVSampleFormat(AudioSampleFormat fmt) const {
    switch (fmt) {
        case AudioSampleFormat::S16:    return AV_SAMPLE_FMT_S16;
        case AudioSampleFormat::S32:    return AV_SAMPLE_FMT_S32;
        case AudioSampleFormat::Float:  return AV_SAMPLE_FMT_FLT;
        case AudioSampleFormat::U8:     return AV_SAMPLE_FMT_U8;
        default:                        return AV_SAMPLE_FMT_S16;
    }
}

AudioSampleFormat FFmpegAudioDecoder::fromAVSampleFormat(AVSampleFormat fmt) const {
    switch (fmt) {
        case AV_SAMPLE_FMT_S16:  return AudioSampleFormat::S16;
        case AV_SAMPLE_FMT_S32:  return AudioSampleFormat::S32;
        case AV_SAMPLE_FMT_FLT:  return AudioSampleFormat::Float;
        case AV_SAMPLE_FMT_U8:   return AudioSampleFormat::U8;
        default:                 return AudioSampleFormat::S16;
    }
}

int FFmpegAudioDecoder::bytesPerSample(AudioSampleFormat fmt) const {
    switch (fmt) {
        case AudioSampleFormat::S16:   return 2;
        case AudioSampleFormat::S32:   return 4;
        case AudioSampleFormat::Float: return 4;
        case AudioSampleFormat::U8:    return 1;
        default:                       return 2;
    }
}

bool FFmpegAudioDecoder::open(const AudioDecodeConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (isOpen_) {
        close();
    }

    targetFormat_ = config.targetFormat;

    const AVCodec* codec = avcodec_find_decoder(static_cast<AVCodecID>(config.codecId));
    if (!codec) {
        spdlog::warn("FFmpegAudioDecoder: no decoder found for codec id {}", config.codecId);
        return false;
    }
    codec_ = codec;

    AVCodecContext* rawCtx = avcodec_alloc_context3(codec);
    if (!rawCtx) {
        spdlog::error("FFmpegAudioDecoder: failed to allocate codec context");
        codec_ = nullptr;
        return false;
    }
    codecCtx_.reset(rawCtx);

    AVSampleFormat avTargetFmt = toAVSampleFormat(config.targetFormat.sampleFormat);
    rawCtx->request_sample_fmt = avTargetFmt;

    if (!config.extraData.empty()) {
        rawCtx->extradata_size = static_cast<int>(config.extraData.size());
        rawCtx->extradata = static_cast<uint8_t*>(av_mallocz(config.extraData.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (rawCtx->extradata) {
            std::memcpy(rawCtx->extradata, config.extraData.data(), config.extraData.size());
        }
    } else if (config.sourceSampleRate > 0 && config.sourceChannels > 0) {
        rawCtx->sample_rate = config.sourceSampleRate;
        av_channel_layout_default(&rawCtx->ch_layout, config.sourceChannels);
    }

    int ret = avcodec_open2(rawCtx, codec, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("FFmpegAudioDecoder: avcodec_open2 failed: {}", errBuf);
        codecCtx_.reset();
        codec_ = nullptr;
        return false;
    }

    swrFrame_ = makeAVFrame();

    isOpen_ = true;
    spdlog::info("FFmpegAudioDecoder: opened codec={}, sampleRate={}, channels={}, format={}",
                 codec->name, config.targetFormat.sampleRate,
                 config.targetFormat.channels,
                 static_cast<int>(config.targetFormat.sampleFormat));
    return true;
}

std::shared_ptr<AudioFrame> FFmpegAudioDecoder::decode(const uint8_t* data, size_t size, int64_t pts) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isOpen_ || !codecCtx_) {
        return nullptr;
    }

    // Input pts is in microseconds — convert to seconds once for passthrough.
    // We do NOT rely on avFrame->pts after decoding; the input pts is authoritative.
    double inputPtsSec = static_cast<double>(pts) / 1000000.0;

    AVPacketPtr pkt = makeAVPacket();
    if (!pkt) {
        return nullptr;
    }
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    pkt->pts = pts;
    pkt->dts = pts;

    int ret = avcodec_send_packet(codecCtx_.get(), pkt.get());
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::warn("FFmpegAudioDecoder: avcodec_send_packet failed: {}", errBuf);
        return nullptr;
    }

    AVFramePtr avFrame = makeAVFrame();
    ret = avcodec_receive_frame(codecCtx_.get(), avFrame.get());
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::warn("FFmpegAudioDecoder: avcodec_receive_frame failed: {}", errBuf);
        }
        return nullptr;
    }

    AVSampleFormat targetFmt = toAVSampleFormat(targetFormat_.sampleFormat);
    int targetRate = targetFormat_.sampleRate;
    int targetChannels = targetFormat_.channels;

    if (avFrame->format == targetFmt &&
        avFrame->sample_rate == targetRate &&
        avFrame->ch_layout.nb_channels == targetChannels) {
        auto frame = std::make_shared<AudioFrame>();
        int bufSize = avFrame->nb_samples * targetChannels * bytesPerSample(targetFormat_.sampleFormat);
        frame->data.resize(static_cast<size_t>(bufSize));
        std::memcpy(frame->data.data(), avFrame->data[0], static_cast<size_t>(bufSize));

        frame->pts = inputPtsSec;
        frame->duration = avFrame->nb_samples * (avFrame->sample_rate > 0
                             ? 1.0 / avFrame->sample_rate : 0.0);
        frame->sampleRate = avFrame->sample_rate;
        frame->channels = avFrame->ch_layout.nb_channels;
        frame->format = targetFormat_.sampleFormat;
        return frame;
    }

    if (!swrCtx_) {
        AVChannelLayout targetLayout;
        av_channel_layout_default(&targetLayout, targetChannels);

        ret = swr_alloc_set_opts2(&swrCtx_,
                                   &targetLayout, targetFmt, targetRate,
                                   &avFrame->ch_layout, static_cast<AVSampleFormat>(avFrame->format), avFrame->sample_rate,
                                   0, nullptr);
        if (ret < 0 || !swrCtx_) {
            spdlog::error("FFmpegAudioDecoder: swr_alloc_set_opts2 failed");
            return nullptr;
        }

        ret = swr_init(swrCtx_);
        if (ret < 0) {
            spdlog::error("FFmpegAudioDecoder: swr_init failed");
            swr_free(&swrCtx_);
            return nullptr;
        }
    }

    int outSamples = swr_get_out_samples(swrCtx_, avFrame->nb_samples);
    uint8_t** outBuf = nullptr;
    int outLinesize = 0;
    ret = av_samples_alloc_array_and_samples(&outBuf, &outLinesize, targetChannels,
                                              outSamples, targetFmt, 0);
    if (ret < 0 || !outBuf) {
        spdlog::error("FFmpegAudioDecoder: av_samples_alloc failed");
        return nullptr;
    }

    int converted = swr_convert(swrCtx_,
                                outBuf, outSamples,
                                const_cast<const uint8_t**>(avFrame->data), avFrame->nb_samples);

    if (converted < 0) {
        spdlog::warn("FFmpegAudioDecoder: swr_convert failed");
        av_freep(&outBuf[0]);
        av_freep(&outBuf);
        return nullptr;
    }

    int bufSize = converted * targetChannels * bytesPerSample(targetFormat_.sampleFormat);

    auto frame = std::make_shared<AudioFrame>();
    frame->data.assign(outBuf[0], outBuf[0] + static_cast<size_t>(bufSize));
    av_freep(&outBuf[0]);
    av_freep(&outBuf);

    frame->pts = inputPtsSec;
    frame->duration = converted * (targetRate > 0 ? 1.0 / targetRate : 0.0);
    frame->sampleRate = targetRate;
    frame->channels = targetChannels;
    frame->format = targetFormat_.sampleFormat;
    return frame;
}

void FFmpegAudioDecoder::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_.get());
    }
    if (swrCtx_) {
        swr_free(&swrCtx_);
    }
}

void FFmpegAudioDecoder::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (swrCtx_) {
        swr_free(&swrCtx_);
    }
    codecCtx_.reset();
    swrFrame_.reset();
    codec_ = nullptr;
    isOpen_ = false;
}

} // namespace extractor
} // namespace hlplayer
