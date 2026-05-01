#ifndef HLPLAYER_AUDIO_TEMPO_PROCESSOR_H
#define HLPLAYER_AUDIO_TEMPO_PROCESSOR_H

#include <hlplayer/IAudioDecoder.h>
#include <hlplayer/Export.h>

#include <memory>
#include <vector>

#ifdef _WIN32
    #define __STDC_CONSTANT_MACROS
    #define __STDC_FORMAT_MACROS
#endif

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace hlplayer {

/// RAII deleter for AVFilterGraph (local to this translation unit)
struct AVFilterGraphDeleter {
    void operator()(AVFilterGraph* graph) const {
        if (graph) {
            avfilter_graph_free(&graph);
        }
    }
};
using AVFilterGraphPtr = std::unique_ptr<AVFilterGraph, AVFilterGraphDeleter>;

/// Processes decoded audio frames through FFmpeg's atempo filter
/// for pitch-preserving playback speed changes.
class AudioTempoProcessor {
public:
    AudioTempoProcessor();
    ~AudioTempoProcessor();

    AudioTempoProcessor(const AudioTempoProcessor&) = delete;
    AudioTempoProcessor& operator=(const AudioTempoProcessor&) = delete;

    /// Initialize the processor with audio stream parameters.
    bool initialize(int sampleRate, int channels, AudioSampleFormat format);

    /// Update the tempo. Rebuilds filter graph if tempo changed significantly.
    void setTempo(double tempo);

    /// Process a decoded audio frame through the atempo filter.
    /// May return 0, 1, or many output frames.
    std::vector<std::shared_ptr<AudioFrame>> process(const std::shared_ptr<AudioFrame>& input);

    /// Flush the filter graph (e.g. after seek), then rebuild.
    void flush();

    /// Close and release all resources.
    void close();

private:
    bool buildFilterGraph(double tempo);
    AVSampleFormat toAVSampleFormat(AudioSampleFormat fmt) const;

    AVFilterGraphPtr graph_;
    AVFilterContext* srcCtx_ = nullptr;   // owned by graph
    AVFilterContext* sinkCtx_ = nullptr;  // owned by graph
    double currentTempo_ = 1.0;
    int sampleRate_ = 0;
    int channels_ = 0;
    AudioSampleFormat format_ = AudioSampleFormat::None;
    AVSampleFormat avFormat_ = AV_SAMPLE_FMT_NONE;
    bool initialized_ = false;
};

} // namespace hlplayer

#endif // HLPLAYER_AUDIO_TEMPO_PROCESSOR_H
