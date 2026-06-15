#ifndef HLPLAYER_FFMPEG_DEMUXER_H
#define HLPLAYER_FFMPEG_DEMUXER_H

#include <hlplayer/Demuxer.h>
#include "FFmpegRAII.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
    #ifdef HLPLAYER_EXTRACTOR_EXPORTS
        #define HLPLAYER_EXTRACTOR_API __declspec(dllexport)
    #else
        #define HLPLAYER_EXTRACTOR_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_EXTRACTOR_API
#endif

extern "C" {
struct AVIOContext;
}

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

/// Additional options for FFmpeg demuxer initialization
struct FFmpegDemuxerOptions {
    std::unordered_map<std::string, std::string> headers;
    std::string userAgent = "HLPlayer/1.0";
    int64_t timeoutUs = 10000000;
    int bufferSize = 32768;
    int maxAnalyzeDuration = 5000000;
    bool lowLatency = true;
    bool probeStreams = true;

    FFmpegDemuxerOptions() = default;
};

/// Stream information detected by demuxer
struct StreamInfo {
    int index = -1;
    AVCodecID codecId = AV_CODEC_ID_NONE;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    int bitRate = 0;
    int sampleRate = 0;
    int channels = 0;
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
};

/// FFmpeg-based demuxer implementation
class HLPLAYER_EXTRACTOR_API FFmpegDemuxer : public hlplayer::IDemuxer {
public:
    explicit FFmpegDemuxer();
    ~FFmpegDemuxer() override;

    FFmpegDemuxer(const FFmpegDemuxer&) = delete;
    FFmpegDemuxer& operator=(const FFmpegDemuxer&) = delete;

    Result<void> open(const std::string& url,
                     const DemuxerConfig& config,
                     DemuxerCallbacks callbacks) override;

    Result<void> start() override;
    Result<void> stop() override;
    Result<void> seek(double seconds) override;
    PlayerState getState() const override;

    double getDuration() const override;

    void setFFmpegOptions(const FFmpegDemuxerOptions& options);

    Result<StreamInfo> getStreamInfo(StreamType streamType) const;

private:
    void demuxLoop();
    bool processPacket(AVPacket* packet);
    void notifyError(PlayerError error, const std::string& message);
    void notifyStreamDetected(const AVStream* stream);
    void notifyPacket(std::shared_ptr<MediaPacket> packet);

    AVDictionaryPtr createFFmpegOptions() const;
    StreamType getStreamType(const AVStream* stream) const;

    AVFormatContextPtr formatCtx_;
    std::unordered_map<int, StreamInfo> streamInfoMap_;

    DemuxerConfig config_;
    DemuxerCallbacks callbacks_;
    FFmpegDemuxerOptions ffmpegOptions_;

    std::atomic<PlayerState> state_{PlayerState_Idle};
    std::atomic<bool> shouldStop_{false};
    std::thread demuxThread_;
    mutable std::mutex mutex_;

    // Persistent-thread command mechanism: seek() posts a request and waits
    // for the demuxer thread to perform it.  The thread does NOT exit on EOF;
    // instead it parks on commandCv_ until a new seek or stop arrives.
    // After a seek, the thread parks again until startReading_ is set,
    // preventing onEndOfStream from firing before the caller is ready.
    std::mutex commandMutex_;
    std::condition_variable commandCv_;
    std::atomic<bool> seekRequested_{false};
    std::atomic<double> pendingSeekTarget_{0.0};
    std::atomic<bool> seekCompleted_{false};
    std::atomic<bool> startReading_{false};

    // True when the demuxer has previously hit EOF on the current file.
    // Used to decide whether seek requires a full close/reopen to reset
    // the MOV demuxer's internal sample-pointer state.
    std::atomic<bool> eofReached_{false};

    // Custom AVIOContext for decrypting .hlv files (nullptr for non-encrypted files)
    AVIOContext* decryptAvioCtx_ = nullptr;

    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;
    int subtitleStreamIndex_ = -1;

    AVRational videoTimeBase_{0, 1};
    AVRational audioTimeBase_{0, 1};
};

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_FFMPEG_DEMUXER_H
