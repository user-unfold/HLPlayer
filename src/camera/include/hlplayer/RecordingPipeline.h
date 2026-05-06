#ifndef HLPLAYER_RECORDINGPIPELINE_H
#define HLPLAYER_RECORDINGPIPELINE_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/Result.h>
#include <FFmpegRAII.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace hlplayer {

using namespace hlplayer::ffmpeg;

class CameraSource;
class AudioCapture;
class AudioEncoder;
class PreviewRenderer;

/// Callback for recording state changes and statistics updates.
using RecordingStateCallback = std::function<void(RecordingState, const RecordingStats&)>;

class HLPLAYER_CAMERA_API RecordingPipeline {
public:
    RecordingPipeline();
    ~RecordingPipeline();

    RecordingPipeline(const RecordingPipeline&) = delete;
    RecordingPipeline& operator=(const RecordingPipeline&) = delete;

    Result<void> start(const RecordingConfig& config, RecordingStateCallback callback);
    Result<void> stop();
    RecordingStats getStats() const;
    RecordingState getState() const;
    void setPreviewRenderer(PreviewRenderer* renderer);

private:
    void videoThreadFunc();
    void audioThreadFunc();
    void updateStats();
    void notifyState();
    void cleanup();
    bool initVideoEncoder(int width, int height, int fps, int bitrate);

    std::unique_ptr<CameraSource> camera_;
    std::unique_ptr<AudioCapture> audioCapture_;
    std::unique_ptr<AudioEncoder> audioEncoder_;

    AVFormatContextPtr outputCtx_;
    AVCodecContextPtr videoEncCtx_;
    SwsContextPtr swsCtx_;
    AVFramePtr yuvFrame_;

    std::thread videoThread_;
    std::thread audioThread_;
    std::atomic<bool> running_{false};
    mutable std::mutex stateMutex_;
    RecordingState state_ = RecordingState::Idle;

    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 0;

    std::atomic<int64_t> frameCount_{0};
    int64_t startTimestamp_ = 0;
    RecordingStats stats_;

    RecordingConfig config_;
    RecordingStateCallback callback_;
    PreviewRenderer* previewRenderer_ = nullptr;
};

} // namespace hlplayer

#endif // HLPLAYER_RECORDINGPIPELINE_H
