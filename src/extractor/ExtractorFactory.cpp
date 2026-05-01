#include "ExtractorFactory.h"

#ifdef HAVE_FFMPEG
#include "FFmpegDemuxer.h"
#include "FFmpegVideoDecoder.h"
#include "FFmpegAudioDecoder.h"
#endif

#include <hlplayer/Result.h>
#include <spdlog/spdlog.h>

namespace hlplayer {
namespace extractor {

namespace {

class StubDemuxer final : public hlplayer::IDemuxer {
public:
    Result<void> open(const std::string& url,
                      const DemuxerConfig& /*config*/,
                      DemuxerCallbacks /*callbacks*/) override {
        spdlog::warn("StubDemuxer::open(\"{}\") — FFmpeg not available", url);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }
    Result<void> start() override {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }
    Result<void> stop() override {
        return Result<void>::success();
    }
    Result<void> seek(double /*seconds*/) override {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }
    PlayerState getState() const override {
        return PlayerState_Idle;
    }
    double getDuration() const override {
        return 0.0;
    }
};

class StubDecoder final : public hlplayer::IHWDecoder {
public:
    Result<void> open(const DecoderConfig& /*config*/) override {
        return Result<void>::error(PlayerError::DecodeError);
    }
    Result<GpuFrame> decode(const uint8_t* /*data*/, size_t /*size*/, double /*pts*/) override {
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }
    Result<std::vector<GpuFrame>> flush() override {
        return Result<std::vector<GpuFrame>>::error(PlayerError::DecodeError);
    }
    void close() override {}
    DecodeBackend getBackend() const override { return DecodeBackend::CPU; }
    bool supportsCodec(Codec /*codec*/) const override { return false; }
};

class StubAudioDecoder final : public hlplayer::IAudioDecoder {
public:
    bool open(const AudioDecodeConfig& /*config*/) override { return false; }
    std::shared_ptr<AudioFrame> decode(const uint8_t* /*data*/, size_t /*size*/, int64_t /*pts*/) override {
        return nullptr;
    }
    void flush() override {}
    void close() override {}
};

} // anonymous namespace

std::unique_ptr<IDemuxer> createFFmpegDemuxer() {
#ifdef HAVE_FFMPEG
    return std::make_unique<FFmpegDemuxer>();
#else
    spdlog::warn("FFmpeg not available — creating stub demuxer");
    return std::make_unique<StubDemuxer>();
#endif
}

std::unique_ptr<IHWDecoder> createFFmpegVideoDecoder() {
#ifdef HAVE_FFMPEG
    return std::make_unique<FFmpegVideoDecoder>();
#else
    spdlog::warn("FFmpeg not available — creating stub video decoder");
    return std::make_unique<StubDecoder>();
#endif
}

std::unique_ptr<IAudioDecoder> createFFmpegAudioDecoder() {
#ifdef HAVE_FFMPEG
    return std::make_unique<FFmpegAudioDecoder>();
#else
    spdlog::warn("FFmpeg not available — creating stub audio decoder");
    return std::make_unique<StubAudioDecoder>();
#endif
}

} // namespace extractor
} // namespace hlplayer
