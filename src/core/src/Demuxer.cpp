#include <hlplayer/Demuxer.h>
#include <hlplayer/logger.h>

#ifndef HAS_FFMPEG

namespace hlplayer {

class StubDemuxer : public IDemuxer {
public:
    Result<void> open(const std::string& url,
                      const DemuxerConfig& /*config*/,
                      DemuxerCallbacks /*callbacks*/) override {
        LOG_WARN("Demuxer::open(\"%s\") called — FFmpeg not available (HAS_FFMPEG not defined)",
                 url.c_str());
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    Result<void> start() override {
        LOG_WARN("Demuxer::start() called — FFmpeg not available");
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    Result<void> stop() override {
        return Result<void>::success();
    }

    Result<void> seek(double seconds) override {
        LOG_WARN("Demuxer::seek(%.3f) called — FFmpeg not available", seconds);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    PlayerState getState() const override {
        return PlayerState_Idle;
    }

    double getDuration() const override {
        return 0.0;
    }
};

} // namespace hlplayer

#else // HAS_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace hlplayer {

class FFmpegDemuxer : public IDemuxer {
public:
    Result<void> open(const std::string& url,
                      const DemuxerConfig& config,
                      DemuxerCallbacks callbacks) override {
        // FFmpeg integration points for Task 7 (Vulkan Decode):
        //
        // 1. avformat_open_input(&fmtCtx_, url.c_str(), nullptr, &dict)
        //    - Pass AVDictionary with low-latency options when config.lowLatency:
        //      {"fflags", "nobuffer"}, {"flags", "low_delay"},
        //      {"analyzeduration", std::to_string(config.probeDurationMs)},
        //      {"probesize", "32768"}
        //    - For RTMP: {"rtmp_live", "live"}, {"fflags", "nobuffer+discardcorrupt"}
        //
        // 2. avformat_find_stream_info(fmtCtx_, nullptr)
        //    - Use config.probeDurationMs for max_analyze_duration
        //
        // 3. For each stream: avcodec_find_decoder(), avcodec_alloc_context3(),
        //    avcodec_parameters_to_context()
        //    - Report via callbacks.onStreamDetected(streamType, codecId, w, h, sr, ch)
        //
        // 4. For low-latency: av_dict_set(&dict, "fflags", "nobuffer", 0)
        //    - Set AVFMT_FLAG_NOBUFFER on fmtCtx_->flags when config.noBuffer

        LOG_WARN("FFmpegDemuxer::open(\"%s\") — FFmpeg stub, not yet implemented", url.c_str());
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    Result<void> start() override {
        // FFmpeg integration:
        // 1. Start demux thread: loop av_read_frame(fmtCtx_, &pkt)
        // 2. For each packet: create MediaPacket, fill data/pts/duration/keyframe/size
        // 3. Route to callbacks.onPacket(shared_ptr<MediaPacket>)
        // 4. On EOF: report EndOfStream event

        LOG_WARN("FFmpegDemuxer::start() — FFmpeg stub, not yet implemented");
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    Result<void> stop() override {
        // FFmpeg integration:
        // 1. Signal demux thread to stop (atomic flag)
        // 2. Join thread
        // 3. avformat_close_input(&fmtCtx_)
        // 4. Free codec contexts

        return Result<void>::success();
    }

    Result<void> seek(double seconds) override {
        // FFmpeg integration:
        // 1. avformat_seek_file(fmtCtx_, -1, INT64_MIN,
        //    (int64_t)(seconds * AV_TIME_BASE), INT64_MAX, AVSEEK_FLAG_BACKWARD)
        // 2. Flush codec buffers: avcodec_flush_buffers()

        LOG_WARN("FFmpegDemuxer::seek(%.3f) — FFmpeg stub, not yet implemented", seconds);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    PlayerState getState() const override {
        return PlayerState_Idle;
    }

    double getDuration() const override {
        return 0.0;
    }

private:
    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext** codecCtxs_ = nullptr;
    int streamCount_ = 0;
};

} // namespace hlplayer

#endif // HAS_FFMPEG
