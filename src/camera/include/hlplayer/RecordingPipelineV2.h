#pragma once
#include <hlplayer/CameraExport.h>
#include <hlplayer/Result.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/HWEncoderDetector.h>
#include <hlplayer/RecordingFrameQueue.h>
#include <hlplayer/StreamMuxer.h>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
struct AVFormatContext;
namespace hlplayer {
class CameraSource;
class AudioCapture;
class AudioEncoder;
class PreviewRenderer;
class HWVideoEncoder;
using StateCallbackV2 = std::function<void(RecordingState, const RecordingStats&)>;
class HLPLAYER_CAMERA_API RecordingPipelineV2 {
public:
    RecordingPipelineV2();
    ~RecordingPipelineV2();
    RecordingPipelineV2(const RecordingPipelineV2&) = delete;
    RecordingPipelineV2& operator=(const RecordingPipelineV2&) = delete;
    Result<void> start(const RecordingConfig& config, StateCallbackV2 callback);
    Result<void> stop();
    Result<void> pause();
    Result<void> resume();
    RecordingStats getStats() const;
    RecordingState getState() const;
    void setPreviewRenderer(PreviewRenderer* renderer);
private:
    void captureThreadFunc();
    void encodeThreadFunc();
    void audioThreadFunc();
    void notifyState();
    void cleanup();
    void writeVideoPacket(const EncodedPacket& encPkt);
    void writeAudioPacket(const EncodedPacket& encPkt);

    RecordingConfig config_;
    StateCallbackV2 callback_;
    EncoderInfo encoderInfo_;
    std::unique_ptr<HWVideoEncoder> videoEncoder_;
    std::unique_ptr<CameraSource> camera_;
    std::unique_ptr<AudioCapture> audioCapture_;
    std::unique_ptr<AudioEncoder> audioEncoder_;
    std::unique_ptr<RecordingFrameQueue> frameQueue_;
    std::unique_ptr<StreamMuxer> streamMuxer_;
    AVFormatContext* outputCtx_ = nullptr;
    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;
    std::thread captureThread_;
    std::thread encodeThread_;
    std::thread audioThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> audioRunning_{false};
    std::atomic<bool> paused_{false};
    std::atomic<RecordingState> state_{RecordingState::Idle};
    std::atomic<int64_t> frameCount_{0};
    int64_t startTimestamp_ = 0;
    std::chrono::steady_clock::time_point startTime_;
    int64_t totalPausedUs_ = 0;  // total microseconds spent paused
    std::chrono::steady_clock::time_point pauseStartTime_;
    PreviewRenderer* previewRenderer_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 0;
    mutable std::recursive_mutex stateMutex_;  // recursive: getStats() may be called from notifyState() while start()/stop() hold the lock
    mutable std::mutex writeMutex_;  // protects av_interleaved_write_frame on outputCtx_
    bool headerWritten_ = false;
    bool trailerWritten_ = false;
};
}
