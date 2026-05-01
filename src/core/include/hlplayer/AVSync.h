#ifndef HLPLAYER_AVSYNC_H
#define HLPLAYER_AVSYNC_H

#include <atomic>
#include <chrono>
#include <cstdint>

#ifndef HLPLAYER_CORE_API
# ifdef _WIN32
#   ifdef HLPLAYER_CORE_EXPORTS
#     define HLPLAYER_CORE_API __declspec(dllexport)
#   else
#     define HLPLAYER_CORE_API __declspec(dllimport)
#   endif
# else
#   define HLPLAYER_CORE_API
# endif
#endif

namespace hlplayer {

enum class AVSyncMode : int32_t {
    AudioMaster = 0,
    VideoMaster = 1,
    Disabled = 2
};

class HLPLAYER_CORE_API AVSyncClock {
public:
    AVSyncClock();

    void setMode(AVSyncMode mode);
    AVSyncMode getMode() const;

    void onAudioFrame(double pts, double duration);
    void onVideoFrame(double pts, double duration);

    double getClock() const;
    double getDriftMs() const;

    double audioClock() const;
    double videoClock() const;

    void reset();

    void setMaxDriftMs(double maxDriftMs);
    double maxDriftMs() const;

    double computeVideoSleepSec(double videoPts) const;

private:
    std::atomic<AVSyncMode> mode_{AVSyncMode::AudioMaster};
    std::atomic<double> audioClock_{0.0};
    std::atomic<double> videoClock_{0.0};
    std::atomic<double> maxDriftMs_{50.0};
    std::atomic<bool> hasVideoClock_{false};

    mutable std::chrono::steady_clock::time_point wallClockOrigin_;
    mutable std::atomic<double> wallClockBasePts_{0.0};
    mutable std::atomic<bool> wallClockInitialized_{false};
};

} // namespace hlplayer

#endif // HLPLAYER_AVSYNC_H
