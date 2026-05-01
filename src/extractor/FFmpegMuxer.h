#ifndef HLPLAYER_FFMPEG_MUXER_H
#define HLPLAYER_FFMPEG_MUXER_H

#include <hlplayer/IMuxer.h>
#include "FFmpegRAII.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#ifdef _WIN32
    #ifdef HLPLAYER_EXTRACTOR_EXPORTS
        #define HLPLAYER_EXTRACTOR_API __declspec(dllexport)
    #else
        #define HLPLAYER_EXTRACTOR_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_EXTRACTOR_API
#endif

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

/// RAII deleter for output AVFormatContext (uses avformat_free_context,
/// unlike FFmpegRAII's AVFormatContextDeleter which uses avformat_close_input).
struct AVFormatOutputDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_free_context(ctx);
        }
    }
};

using AVFormatOutputPtr = std::unique_ptr<AVFormatContext, AVFormatOutputDeleter>;

/// FFmpeg-based muxer implementation supporting MP4 and MKV containers.
class HLPLAYER_EXTRACTOR_API FFmpegMuxer : public IMuxer {
public:
    FFmpegMuxer();
    ~FFmpegMuxer() override;

    FFmpegMuxer(const FFmpegMuxer&) = delete;
    FFmpegMuxer& operator=(const FFmpegMuxer&) = delete;

    Result<void> open(const MuxerConfig& config) override;
    Result<uint32_t> addStream(const EncoderConfig& streamConfig) override;
    Result<uint32_t> addAudioStream(int codecId, int sampleRate, int channels,
                                     const std::vector<uint8_t>& extradata) override;
    Result<void> writePacket(const EncodedPacket& packet) override;
    Result<void> finalize() override;
    void close() override;

    MuxerConfig getConfig() const override;
    uint32_t getStreamCount() const override;
    bool isOpen() const override;

private:
    /// Map Codec enum to FFmpeg AVCodecID.
    static AVCodecID toAVCodecID(Codec codec);

    /// Find the output format for a given format string ("mp4", "mkv").
    static const AVOutputFormat* findOutputFormat(const std::string& format);

    AVFormatOutputPtr formatCtx_;
    std::vector<AVStream*> streams_;
    std::vector<EncoderConfig> streamConfigs_;

    MuxerConfig config_;
    std::atomic<bool> open_{false};
    std::atomic<bool> headerWritten_{false};
    mutable std::mutex mutex_;
};

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_FFMPEG_MUXER_H
