#ifndef HLPLAYER_STREAMINGPIPELINE_H
#define HLPLAYER_STREAMINGPIPELINE_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/Result.h>
#include <hlplayer/StreamMuxer.h>
#include <FFmpegRAII.h>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace hlplayer {

using StreamingStateCallback = std::function<void(StreamingState, const StreamingStats&)>;

class HLPLAYER_CAMERA_API StreamingPipeline {
public:
    StreamingPipeline();
    ~StreamingPipeline();

    StreamingPipeline(const StreamingPipeline&) = delete;
    StreamingPipeline& operator=(const StreamingPipeline&) = delete;

    Result<void> start(const StreamingConfig& config, StreamingStateCallback callback);
    Result<void> cancel();
    StreamingStats getStats() const;
    StreamingState getState() const;
    bool isCancelled() const { return cancelFlag_.load(); }

private:
    void streamingLoop();

    hlplayer::ffmpeg::AVFormatContextPtr sourceCtx_;
    int videoStreamIdx_ = -1;
    int audioStreamIdx_ = -1;
    uint32_t dstVideoIdx_ = 0;
    uint32_t dstAudioIdx_ = 1;

    StreamMuxer muxer_;

    std::thread thread_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<bool> cleanedUp_{false};

    std::atomic<StreamingState> state_{StreamingState::Idle};
    StreamingStats stats_;
    StreamingStateCallback callback_;
    mutable std::mutex statsMutex_;
};

} // namespace hlplayer

#endif // HLPLAYER_STREAMINGPIPELINE_H
