#include "AudioTempoProcessor.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
}

namespace hlplayer {

AudioTempoProcessor::AudioTempoProcessor() = default;

AudioTempoProcessor::~AudioTempoProcessor() {
    close();
}

AVSampleFormat AudioTempoProcessor::toAVSampleFormat(AudioSampleFormat fmt) const {
    switch (fmt) {
        case AudioSampleFormat::S16:   return AV_SAMPLE_FMT_S16;
        case AudioSampleFormat::S32:   return AV_SAMPLE_FMT_S32;
        case AudioSampleFormat::Float: return AV_SAMPLE_FMT_FLT;
        case AudioSampleFormat::U8:    return AV_SAMPLE_FMT_U8;
        default:                       return AV_SAMPLE_FMT_S16;
    }
}

bool AudioTempoProcessor::initialize(int sampleRate, int channels, AudioSampleFormat format) {
    close();

    sampleRate_ = sampleRate;
    channels_ = channels;
    format_ = format;
    avFormat_ = toAVSampleFormat(format);
    currentTempo_ = 1.0;

    if (!buildFilterGraph(1.0)) {
        return false;
    }

    initialized_ = true;
    spdlog::info("AudioTempoProcessor: initialized rate={} ch={} fmt={}",
                 sampleRate, channels, static_cast<int>(format));
    return true;
}

bool AudioTempoProcessor::buildFilterGraph(double tempo) {
    // Clamp tempo to atempo's valid range per filter instance
    if (tempo < 0.5) tempo = 0.5;
    if (tempo > 100.0) tempo = 100.0;

    // Reset old graph — srcCtx_ and sinkCtx_ are owned by graph
    srcCtx_ = nullptr;
    sinkCtx_ = nullptr;
    graph_.reset();

    AVFilterGraph* graphRaw = avfilter_graph_alloc();
    if (!graphRaw) {
        spdlog::error("AudioTempoProcessor: avfilter_graph_alloc failed");
        return false;
    }
    graph_.reset(graphRaw);

    // --- buffersrc ---
    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    if (!abuffersrc) {
        spdlog::error("AudioTempoProcessor: abuffer filter not found");
        return false;
    }

    // Build channel layout string
    char chLayoutStr[64] = {0};
    AVChannelLayout chLayout;
    av_channel_layout_default(&chLayout, channels_);
    av_channel_layout_describe(&chLayout, chLayoutStr, sizeof(chLayoutStr));
    av_channel_layout_uninit(&chLayout);

    char srcArgs[256];
    std::snprintf(srcArgs, sizeof(srcArgs),
                  "sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                  sampleRate_,
                  av_get_sample_fmt_name(avFormat_),
                  chLayoutStr);

    int ret = avfilter_graph_create_filter(&srcCtx_, abuffersrc, "in",
                                            srcArgs, nullptr, graphRaw);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioTempoProcessor: create buffersrc failed: {}", errBuf);
        return false;
    }

    // --- buffersink ---
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink) {
        spdlog::error("AudioTempoProcessor: abuffersink filter not found");
        return false;
    }

    ret = avfilter_graph_create_filter(&sinkCtx_, abuffersink, "out",
                                        nullptr, nullptr, graphRaw);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioTempoProcessor: create buffersink failed: {}", errBuf);
        return false;
    }

    // --- atempo chain ---
    // Each atempo filter supports [0.5, 100.0]. For values outside a single
    // filter's range we chain multiple instances (though in practice playback
    // rates stay within 0.5-2.0x).
    AVFilterContext* lastCtx = srcCtx_;

    double remaining = tempo;
    int atempoIdx = 0;
    while (std::abs(remaining - 1.0) > 0.01) {
        double thisTempo = remaining;
        if (thisTempo < 0.5) thisTempo = 0.5;
        if (thisTempo > 100.0) thisTempo = 100.0;

        const AVFilter* atempo = avfilter_get_by_name("atempo");
        if (!atempo) {
            spdlog::error("AudioTempoProcessor: atempo filter not found");
            return false;
        }

        char atempoArgs[64];
        std::snprintf(atempoArgs, sizeof(atempoArgs), "tempo=%.4f", thisTempo);

        char filterName[32];
        std::snprintf(filterName, sizeof(filterName), "atempo%d", atempoIdx);

        AVFilterContext* atempoCtx = nullptr;
        ret = avfilter_graph_create_filter(&atempoCtx, atempo, filterName,
                                            atempoArgs, nullptr, graphRaw);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("AudioTempoProcessor: create atempo failed: {}", errBuf);
            return false;
        }

        ret = avfilter_link(lastCtx, 0, atempoCtx, 0);
        if (ret < 0) {
            spdlog::error("AudioTempoProcessor: link to atempo failed");
            return false;
        }

        lastCtx = atempoCtx;
        remaining /= thisTempo;
        atempoIdx++;
    }

    // Link last filter to sink
    ret = avfilter_link(lastCtx, 0, sinkCtx_, 0);
    if (ret < 0) {
        spdlog::error("AudioTempoProcessor: link to buffersink failed");
        return false;
    }

    ret = avfilter_graph_config(graphRaw, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioTempoProcessor: graph config failed: {}", errBuf);
        return false;
    }

    currentTempo_ = tempo;
    return true;
}

void AudioTempoProcessor::setTempo(double tempo) {
    if (tempo < 0.5) tempo = 0.5;
    if (tempo > 100.0) tempo = 100.0;

    if (!initialized_) return;

    if (std::abs(tempo - currentTempo_) < 0.01) return;

    spdlog::info("AudioTempoProcessor: tempo change {:.2f} -> {:.2f}", currentTempo_, tempo);
    buildFilterGraph(tempo);
}

std::vector<std::shared_ptr<AudioFrame>> AudioTempoProcessor::process(
    const std::shared_ptr<AudioFrame>& input) {

    std::vector<std::shared_ptr<AudioFrame>> result;
    if (!initialized_ || !graph_ || !srcCtx_ || !sinkCtx_ || !input) {
        // Pass through if not initialized
        if (input) result.push_back(input);
        return result;
    }

    // At 1.0x tempo, pass through directly to avoid filter overhead
    if (std::abs(currentTempo_ - 1.0) < 0.01) {
        result.push_back(input);
        return result;
    }

    // Build an AVFrame from the AudioFrame
    AVFrame* frame = av_frame_alloc();
    if (!frame) return result;

    int bytesPerSample = av_get_bytes_per_sample(avFormat_);
    int totalSamples = static_cast<int>(input->data.size()) / (bytesPerSample * channels_);

    frame->format = avFormat_;
    frame->sample_rate = sampleRate_;
    frame->nb_samples = totalSamples;
    av_channel_layout_default(&frame->ch_layout, channels_);
    frame->pts = static_cast<int64_t>(input->pts * sampleRate_);

    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_frame_free(&frame);
        result.push_back(input);
        return result;
    }

    std::memcpy(frame->data[0], input->data.data(), input->data.size());

    // Push into filter graph
    ret = av_buffersrc_add_frame(srcCtx_, frame);
    av_frame_free(&frame);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::warn("AudioTempoProcessor: buffersrc add_frame failed: {}", errBuf);
        result.push_back(input);
        return result;
    }

    // Pull all available output frames
    while (true) {
        AVFrame* outFrame = av_frame_alloc();
        if (!outFrame) break;

        ret = av_buffersink_get_frame(sinkCtx_, outFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&outFrame);
            break;
        }
        if (ret < 0) {
            av_frame_free(&outFrame);
            break;
        }

        auto audioFrame = std::make_shared<AudioFrame>();
        int outBytes = outFrame->nb_samples * channels_ * bytesPerSample;
        audioFrame->data.resize(static_cast<size_t>(outBytes));
        std::memcpy(audioFrame->data.data(), outFrame->data[0],
                     static_cast<size_t>(outBytes));

        // PTS from the filter is in sample_rate time base
        audioFrame->pts = static_cast<double>(outFrame->pts) / sampleRate_;
        audioFrame->duration = static_cast<double>(outFrame->nb_samples) / sampleRate_;
        audioFrame->sampleRate = sampleRate_;
        audioFrame->channels = channels_;
        audioFrame->format = format_;

        result.push_back(std::move(audioFrame));
        av_frame_free(&outFrame);
    }

    return result;
}

void AudioTempoProcessor::flush() {
    if (!initialized_ || !graph_) return;

    // Flush by sending NULL to buffersrc, then drain buffersink
    if (srcCtx_) {
        int flushRet = av_buffersrc_add_frame(srcCtx_, nullptr);
        if (flushRet < 0) {
            spdlog::debug("AudioTempoProcessor: flush buffersrc returned {}", flushRet);
        }
    }
    if (sinkCtx_) {
        AVFrame* outFrame = av_frame_alloc();
        if (outFrame) {
            while (av_buffersink_get_frame(sinkCtx_, outFrame) >= 0) {
                av_frame_unref(outFrame);
            }
            av_frame_free(&outFrame);
        }
    }

    // Rebuild the graph to reset state
    buildFilterGraph(currentTempo_);
}

void AudioTempoProcessor::close() {
    srcCtx_ = nullptr;
    sinkCtx_ = nullptr;
    graph_.reset();
    initialized_ = false;
    currentTempo_ = 1.0;
}

} // namespace hlplayer
