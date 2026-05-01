#include <hlplayer/VideoRefreshThread.h>
#include <hlplayer/IVideoFrameSink.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <spdlog/spdlog.h>

namespace hlplayer {

VideoRefreshThread::VideoRefreshThread(VideoFrameQueue& frameQueue,
                                       SyncClock& syncClock,
                                       IVideoFrameSink* sink,
                                       std::atomic<bool>& endRequested,
                                       std::atomic<int>& seekSerial,
                                       std::function<void()> onPlaybackComplete)
    : frameQueue_(frameQueue), syncClock_(syncClock), sink_(sink),
      endRequested_(endRequested), seekSerial_(seekSerial), onPlaybackComplete_(std::move(onPlaybackComplete)) {}

VideoRefreshThread::~VideoRefreshThread() {
    stop();
}

void VideoRefreshThread::start() {
    if (running_.load()) return;
    running_.store(true);
    paused_.store(false);
    frameTimer_ = 0.0;
    lastDuration_ = 0.04;
    videoClock_ = 0.0;
    thread_ = std::thread(&VideoRefreshThread::run, this);
}

void VideoRefreshThread::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void VideoRefreshThread::pause() {
    paused_.store(true);
}

void VideoRefreshThread::pauseAfterNextFrame() {
    pauseAfterNext_.store(true);
}

void VideoRefreshThread::resume() {
    paused_.store(false);
    frameTimer_ = 0.0;
}

void VideoRefreshThread::setPlaybackRate(double rate) {
    playbackRate_.store(rate);
}

double VideoRefreshThread::computeFrameDuration(double currentPts, double lastPts,
                                                 double fallback) {
    double duration = currentPts - lastPts;
    if (duration <= 0.0 || duration > kMaxDuration) {
        duration = fallback;
    }
    return duration;
}

double VideoRefreshThread::clampDuration(double duration, double diff) {
    double syncThreshold = std::max(duration, kRepeatThreshold);
    if (std::fabs(diff) < syncThreshold) {
        duration += diff;
        duration = std::max(duration, kMinDuration);
    }
    return std::clamp(duration, kMinDuration, kMaxDuration);
}

void VideoRefreshThread::run() {
    spdlog::info("VideoRefreshThread started");

    GpuFrame lastFrame;
    bool hasLastFrame = false;
    bool playbackCompleted = false;
    auto lastFpsTime = std::chrono::steady_clock::now();
    int frameCount = 0;

    while (running_.load()) {
        if (paused_.load() && !pauseAfterNext_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        GpuFrame frame;
        bool got = frameQueue_.pop(frame, 50);

        if (!got) {
            if (endRequested_.load() && frameQueue_.empty()) {
                if (!playbackCompleted && onPlaybackComplete_) {
                    playbackCompleted = true;
                    onPlaybackComplete_();
                }
            }
            if (hasLastFrame && sink_) {
                sink_->onFrame(lastFrame);
            }
            continue;
        }

        // Discard frames from old seek generations.  Frame.seekSerial was
        // set when the frame was decoded, so it identifies which seek
        // generation it belongs to.  If the current seekSerial is different,
        // this frame is stale and should not be displayed.
        int currentSerial = seekSerial_.load();
        if (frame.seekSerial != currentSerial) {
            continue;  // Skip stale frame
        }

        if (playbackCompleted) {
            playbackCompleted = false;
        }

        double nominalDuration = computeFrameDuration(frame.timestamp, videoClock_, lastDuration_);
        double master = syncClock_.getMasterClock();
        double diff = frame.timestamp - master;

        if (diff < -kDropThreshold) {
            spdlog::debug("VideoRefresh: drop frame, diff={:.3f}s", diff);
            GpuFrame next;
            while (frameQueue_.pop(next, 0)) {
                diff = next.timestamp - master;
                if (diff >= -kDropThreshold) {
                    frame = std::move(next);
                    break;
                }
                spdlog::debug("VideoRefresh: drop frame, diff={:.3f}s", diff);
            }
            nominalDuration = computeFrameDuration(frame.timestamp, videoClock_, lastDuration_);
            diff = frame.timestamp - master;
        }

        double delay = clampDuration(nominalDuration, diff);

        double rate = playbackRate_.load();
        if (rate > 0.0 && std::fabs(rate - 1.0) > 0.01) {
            delay /= rate;
            delay = std::max(delay, kMinDuration);
        }

        if (frameTimer_ > 0.0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now.time_since_epoch()).count();
            double remaining = frameTimer_ + delay - elapsed;
            if (remaining > 0.001) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(std::min(remaining, delay)));
            }
        }

        if (sink_) {
            sink_->onFrame(frame);
        }

        if (pauseAfterNext_.exchange(false)) {
            paused_.store(true);
        }

        videoClock_ = frame.timestamp;
        lastDuration_ = nominalDuration;
        lastFrame = std::move(frame);
        hasLastFrame = true;
        frameTimer_ = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        double fpsElapsed = std::chrono::duration<double>(now - lastFpsTime).count();
        if (fpsElapsed >= 1.0) {
            fps_.store(frameCount / fpsElapsed, std::memory_order_release);
            frameCount = 0;
            lastFpsTime = now;
        }
    }

    spdlog::info("VideoRefreshThread stopped");
}

} // namespace hlplayer
