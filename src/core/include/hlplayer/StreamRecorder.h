#ifndef HLPLAYER_STREAM_RECORDER_H
#define HLPLAYER_STREAM_RECORDER_H

#include <hlplayer/Export.h>
#include <hlplayer/Result.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

extern "C" {
#include <libavformat/avformat.h>
}

namespace hlplayer {

struct AVFormatOutputDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) avformat_free_context(ctx);
    }
};

using AVFormatOutputPtr = std::unique_ptr<AVFormatContext, AVFormatOutputDeleter>;

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) av_packet_free(&pkt);
    }
};

using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

class HLPLAYER_CORE_API StreamRecorder {
public:
    static constexpr uint64_t kMaxFileSize = 10ULL * 1024ULL * 1024ULL * 1024ULL;

    StreamRecorder();
    ~StreamRecorder();

    StreamRecorder(const StreamRecorder&) = delete;
    StreamRecorder& operator=(const StreamRecorder&) = delete;

    /// @param outputDir  Directory to write .mkv files into.
    Result<void> start(const std::string& outputDir);

    void writePacket(AVPacket* src, AVStream* srcStream);

    void stop();

    bool isRecording() const;
    uint64_t currentFileSize() const;
    double currentDuration() const;

private:
    void recordLoop();
    Result<void> openNextFile();
    void closeCurrentFile();

    /// @return Output stream index on success.
    Result<int> addOutputStream(AVStream* srcStream);
    Result<void> addAllKnownStreams();

    struct KnownStream {
        AVStream* srcStream; // demuxer-owned; safe to dereference during recording session
        int outIndex;
    };

    struct QueueEntry {
        AVPacketPtr packet;
        AVStream* srcStream;
    };

    std::unordered_map<int, KnownStream> knownStreams_;

    AVFormatOutputPtr formatCtx_;
    std::string outputDir_;
    uint64_t fileSequence_ = 0;
    std::atomic<uint64_t> currentFileSize_{0};
    double startTimestamp_ = 0.0;
    std::atomic<double> durationSeconds_{0.0};
    bool headerWritten_ = false;

    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<QueueEntry> queue_;
    static constexpr size_t kMaxQueueSize = 500;
    std::atomic<bool> queueShutdown_{false};

    std::thread thread_;
    std::atomic<bool> recording_{false};
};

} // namespace hlplayer

#endif // HLPLAYER_STREAM_RECORDER_H
