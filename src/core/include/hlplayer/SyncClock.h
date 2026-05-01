#ifndef HLPLAYER_SYNCCLOCK_H
#define HLPLAYER_SYNCCLOCK_H

#include <hlplayer/Export.h>

#include <atomic>
#include <chrono>

namespace hlplayer {

enum class SyncClockMode : int {
    AudioMaster = 0,
    VideoMaster,
    ExternalClock
};

class HLPLAYER_CORE_API SyncClock {
public:
    SyncClock() = default;

    void setMode(SyncClockMode mode) { mode_.store(mode, std::memory_order_release); }
    SyncClockMode mode() const { return mode_.load(std::memory_order_acquire); }

    void setAudioClock(double pts) { audioClock_.store(pts, std::memory_order_release); }
    double audioClock() const { return audioClock_.load(std::memory_order_acquire); }

    void setVideoClock(double pts) { videoClock_.store(pts, std::memory_order_release); }
    double videoClock() const { return videoClock_.load(std::memory_order_acquire); }

    void setAudioDeviceLatency(double latencySec) {
        audioDeviceLatency_.store(latencySec, std::memory_order_release);
    }

    double getMasterClock() const {
        SyncClockMode m = mode_.load(std::memory_order_acquire);
        switch (m) {
            case SyncClockMode::AudioMaster: {
                double audio = audioClock_.load(std::memory_order_acquire);
                double latency = audioDeviceLatency_.load(std::memory_order_acquire);
                return audio - latency;
            }
            case SyncClockMode::VideoMaster:
                return videoClock_.load(std::memory_order_acquire);
            case SyncClockMode::ExternalClock:
                return getExternalClock();
        }
        return 0.0;
    }

    double getDriftMs() const {
        double video = videoClock_.load(std::memory_order_acquire);
        double master = getMasterClock();
        return (video - master) * 1000.0;
    }

    void initExternalClock(double pts) {
        extOrigin_ = std::chrono::steady_clock::now();
        extBasePts_.store(pts, std::memory_order_release);
        extInitialized_.store(true, std::memory_order_release);
    }

    double getExternalClock() const {
        if (!extInitialized_.load(std::memory_order_acquire)) return 0.0;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - extOrigin_).count();
        return extBasePts_.load(std::memory_order_acquire) + elapsed;
    }

    void reset() {
        audioClock_.store(0.0, std::memory_order_release);
        videoClock_.store(0.0, std::memory_order_release);
        audioDeviceLatency_.store(0.0, std::memory_order_release);
        extInitialized_.store(false, std::memory_order_release);
        extBasePts_.store(0.0, std::memory_order_release);
    }

private:
    std::atomic<double> audioClock_{0.0};
    std::atomic<double> videoClock_{0.0};
    std::atomic<double> audioDeviceLatency_{0.0};
    std::atomic<SyncClockMode> mode_{SyncClockMode::AudioMaster};
    std::atomic<bool> extInitialized_{false};
    std::atomic<double> extBasePts_{0.0};
    std::chrono::steady_clock::time_point extOrigin_;
};

} // namespace hlplayer

#endif // HLPLAYER_SYNCCLOCK_H
