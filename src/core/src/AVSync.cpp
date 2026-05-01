#include <hlplayer/AVSync.h>
#include <hlplayer/logger.h>

#include <cmath>

namespace hlplayer {

AVSyncClock::AVSyncClock() = default;

void AVSyncClock::setMode(AVSyncMode mode) {
    mode_.store(mode, std::memory_order_release);
}

AVSyncMode AVSyncClock::getMode() const {
    return mode_.load(std::memory_order_acquire);
}

void AVSyncClock::onAudioFrame(double pts, double duration) {
    double newVal = pts + duration;
    audioClock_.store(newVal, std::memory_order_release);

    double drift = std::abs(getDriftMs());
    double threshold = maxDriftMs_.load(std::memory_order_acquire);
    if (threshold > 0.0 && drift > threshold) {
        LOG_WARN("A/V drift {:.1f} ms exceeds threshold {:.1f} ms",
                 drift, threshold);
    }
}

void AVSyncClock::onVideoFrame(double pts, double duration) {
    double newVal = pts + duration;
    videoClock_.store(newVal, std::memory_order_release);
    hasVideoClock_.store(true, std::memory_order_release);
}

double AVSyncClock::getClock() const {
    AVSyncMode mode = mode_.load(std::memory_order_acquire);
    switch (mode) {
        case AVSyncMode::AudioMaster:
            return audioClock_.load(std::memory_order_acquire);
        case AVSyncMode::VideoMaster:
            return videoClock_.load(std::memory_order_acquire);
        case AVSyncMode::Disabled:
            return audioClock_.load(std::memory_order_acquire);
        default:
            return 0.0;
    }
}

double AVSyncClock::getDriftMs() const {
    if (!hasVideoClock_.load(std::memory_order_acquire)) {
        return 0.0;
    }
    double audio = audioClock_.load(std::memory_order_acquire);
    double video = videoClock_.load(std::memory_order_acquire);
    return (audio - video) * 1000.0;
}

double AVSyncClock::audioClock() const {
    return audioClock_.load(std::memory_order_acquire);
}

double AVSyncClock::videoClock() const {
    return videoClock_.load(std::memory_order_acquire);
}

void AVSyncClock::reset() {
    audioClock_.store(0.0, std::memory_order_release);
    videoClock_.store(0.0, std::memory_order_release);
    hasVideoClock_.store(false, std::memory_order_release);
    wallClockInitialized_.store(false, std::memory_order_release);
    wallClockBasePts_.store(0.0, std::memory_order_release);
}

void AVSyncClock::setMaxDriftMs(double maxDriftMs) {
    maxDriftMs_.store(maxDriftMs, std::memory_order_release);
}

double AVSyncClock::maxDriftMs() const {
    return maxDriftMs_.load(std::memory_order_acquire);
}

double AVSyncClock::computeVideoSleepSec(double videoPts) const {
    if (!wallClockInitialized_.load(std::memory_order_acquire)) {
        wallClockOrigin_ = std::chrono::steady_clock::now();
        wallClockBasePts_.store(videoPts, std::memory_order_release);
        wallClockInitialized_.store(true, std::memory_order_release);
        return 0.0;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(now - wallClockOrigin_).count();
    double targetPts = wallClockBasePts_.load(std::memory_order_acquire) + elapsedSec;
    double sleepSec = videoPts - targetPts;

    if (sleepSec < -1.0) {
        wallClockOrigin_ = now;
        wallClockBasePts_.store(videoPts, std::memory_order_release);
        return 0.0;
    }

    return sleepSec > 0.0 ? sleepSec : 0.0;
}

} // namespace hlplayer
