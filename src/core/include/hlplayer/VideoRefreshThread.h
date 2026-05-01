#ifndef HLPLAYER_VIDEOREFRESHTHREAD_H
#define HLPLAYER_VIDEOREFRESHTHREAD_H

#include <hlplayer/Export.h>
#include <hlplayer/FrameQueue.h>
#include <hlplayer/SyncClock.h>
#include <hlplayer/GpuFrameContract.h>

#include <atomic>
#include <functional>
#include <thread>

namespace hlplayer {

class IVideoFrameSink;

class HLPLAYER_CORE_API VideoRefreshThread {
public:
    VideoRefreshThread(VideoFrameQueue& frameQueue, SyncClock& syncClock,
                       IVideoFrameSink* sink,
                       std::atomic<bool>& endRequested,
                       std::atomic<int>& seekSerial,
                       std::function<void()> onPlaybackComplete = nullptr);
    ~VideoRefreshThread();

    void start();
    void stop();
    void pause();
    void pauseAfterNextFrame();
    void resume();
    void setPlaybackRate(double rate);

    double fps() const { return fps_.load(std::memory_order_acquire); }

private:
    void run();
    double computeFrameDuration(double currentPts, double lastPts, double lastDuration);
    double clampDuration(double duration, double diff);

    VideoFrameQueue& frameQueue_;
    SyncClock& syncClock_;
    IVideoFrameSink* sink_;
    std::atomic<bool>& endRequested_;
    std::atomic<int>& seekSerial_;
    std::function<void()> onPlaybackComplete_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> pauseAfterNext_{false};

    double frameTimer_ = 0.0;
    double lastDuration_ = 0.04;
    double videoClock_ = 0.0;
    std::atomic<double> fps_{0.0};
    std::atomic<double> playbackRate_{1.0};

    static constexpr double kDropThreshold = 0.1;
    static constexpr double kRepeatThreshold = 0.04;
    static constexpr double kMinDuration = 0.005;
    static constexpr double kMaxDuration = 0.5;
};

} // namespace hlplayer

#endif // HLPLAYER_VIDEOREFRESHTHREAD_H
