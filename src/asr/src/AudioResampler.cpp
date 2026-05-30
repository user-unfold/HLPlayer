#include <hlplayer/AudioResampler.h>

#include <spdlog/spdlog.h>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <cstring>

namespace hlplayer {
namespace asr {

AudioResampler::AudioResampler() = default;

int AudioResampler::toFFmpegSampleFormat(AudioSampleFormat fmt) const {
    switch (fmt) {
        case AudioSampleFormat::S16:   return AV_SAMPLE_FMT_S16;
        case AudioSampleFormat::S32:   return AV_SAMPLE_FMT_S32;
        case AudioSampleFormat::Float: return AV_SAMPLE_FMT_FLT;
        case AudioSampleFormat::U8:    return AV_SAMPLE_FMT_U8;
        default:                       return AV_SAMPLE_FMT_NONE;
    }
}

bool AudioResampler::configure(int srcSampleRate, int srcChannels, AudioSampleFormat srcFormat) {
    // Clean up previous context
    swrCtx_.reset();
    configured_ = false;

    int avSrcFormat = toFFmpegSampleFormat(srcFormat);
    if (avSrcFormat == AV_SAMPLE_FMT_NONE) {
        spdlog::error("AudioResampler: unsupported source format");
        return false;
    }

    // Allocate SwrContext
    swrCtx_.reset(swr_alloc());
    if (!swrCtx_) {
        spdlog::error("AudioResampler: failed to allocate SwrContext");
        return false;
    }

    // Configure source format
    AVChannelLayout srcLayout = {};
    av_channel_layout_default(&srcLayout, srcChannels);
    av_opt_set_chlayout(swrCtx_.get(), "in_chlayout", &srcLayout, 0);
    av_opt_set_int(swrCtx_.get(), "in_sample_rate", srcSampleRate, 0);
    av_opt_set_sample_fmt(swrCtx_.get(), "in_sample_fmt", static_cast<AVSampleFormat>(avSrcFormat), 0);

    // Configure target format: 16kHz, mono, float32
    AVChannelLayout dstLayout = {};
    av_channel_layout_default(&dstLayout, kTargetChannels);
    av_opt_set_chlayout(swrCtx_.get(), "out_chlayout", &dstLayout, 0);
    av_opt_set_int(swrCtx_.get(), "out_sample_rate", kTargetSampleRate, 0);
    av_opt_set_sample_fmt(swrCtx_.get(), "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    av_channel_layout_uninit(&srcLayout);
    av_channel_layout_uninit(&dstLayout);

    int ret = swr_init(swrCtx_.get());
    if (ret < 0) {
        char errBuf[256] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioResampler: swr_init failed: {}", errBuf);
        swrCtx_.reset();
        return false;
    }

    srcSampleRate_ = srcSampleRate;
    srcChannels_ = srcChannels;

    // Calculate bytes per sample for the source format
    switch (srcFormat) {
        case AudioSampleFormat::U8:    srcBytesPerSample_ = 1; break;
        case AudioSampleFormat::S16:   srcBytesPerSample_ = 2; break;
        case AudioSampleFormat::S32:   srcBytesPerSample_ = 4; break;
        case AudioSampleFormat::Float: srcBytesPerSample_ = 4; break;
        default:                       srcBytesPerSample_ = 0; break;
    }

    configured_ = true;
    spdlog::info("AudioResampler: configured {}Hz {}ch {} -> {}Hz {}ch float32",
                 srcSampleRate, srcChannels,
                 static_cast<int>(srcFormat),
                 kTargetSampleRate, kTargetChannels);
    return true;
}

std::vector<float> AudioResampler::resample(const uint8_t* srcData, size_t srcSize) {
    std::vector<float> output;

    if (!configured_ || !swrCtx_ || !srcData || srcSize == 0) {
        return output;
    }

    // Calculate number of source samples (per channel)
    const int srcSamples = static_cast<int>(srcSize / (srcChannels_ * srcBytesPerSample_));
    if (srcSamples <= 0) {
        return output;
    }

    // Estimate output sample count
    int64_t dstSamples = swr_get_delay(swrCtx_.get(), srcSampleRate_) + srcSamples;
    dstSamples = av_rescale_rnd(dstSamples, kTargetSampleRate, srcSampleRate_, AV_ROUND_UP);

    // Allocate output buffer
    output.resize(static_cast<size_t>(dstSamples));
    uint8_t* outPtr = reinterpret_cast<uint8_t*>(output.data());
    const uint8_t* inPtr = srcData;

    int converted = swr_convert(swrCtx_.get(),
                                &outPtr, static_cast<int>(dstSamples),
                                &inPtr, srcSamples);

    if (converted < 0) {
        char errBuf[256] = {0};
        av_strerror(converted, errBuf, sizeof(errBuf));
        spdlog::warn("AudioResampler: swr_convert failed: {}", errBuf);
        return {};
    }

    output.resize(static_cast<size_t>(converted));
    return output;
}

std::vector<float> AudioResampler::resample(const AudioFrame& frame) {
    if (frame.data.empty()) {
        return {};
    }

    // Auto-configure on first frame or format change
    if (!configured_ ||
        frame.sampleRate != srcSampleRate_ ||
        frame.channels != srcChannels_) {
        if (!configure(frame.sampleRate, frame.channels, frame.format)) {
            return {};
        }
    }

    return resample(frame.data.data(), frame.data.size());
}

std::vector<float> AudioResampler::flush() {
    if (!configured_ || !swrCtx_) {
        return {};
    }

    // Flush remaining samples from the resampler
    int64_t dstSamples = swr_get_delay(swrCtx_.get(), kTargetSampleRate);
    if (dstSamples <= 0) {
        return {};
    }

    std::vector<float> output(static_cast<size_t>(dstSamples));
    uint8_t* outPtr = reinterpret_cast<uint8_t*>(output.data());

    int converted = swr_convert(swrCtx_.get(), &outPtr, static_cast<int>(dstSamples), nullptr, 0);
    if (converted <= 0) {
        return {};
    }

    output.resize(static_cast<size_t>(converted));
    return output;
}

void AudioResampler::reset() {
    if (swrCtx_) {
        // Re-initialize to clear internal buffers
        swr_close(swrCtx_.get());
        swr_init(swrCtx_.get());
    }
}

} // namespace asr
} // namespace hlplayer
