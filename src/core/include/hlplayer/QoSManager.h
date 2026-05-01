#ifndef HLPLAYER_QOSMANAGER_H
#define HLPLAYER_QOSMANAGER_H

#include <cstdint>
#include <atomic>

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

enum class QoSState : int {
    Stable = 0,
    Buffering,
    Degraded,
    Recovering
};

enum class QoSLevel : int {
    Auto = 0,
    Low,
    Medium,
    High,
    Ultra
};

struct NetworkMetrics {
    uint64_t bandwidthBps = 0;
    double latencyMs = 0.0;
    double packetLossPercent = 0.0;
    double jitterMs = 0.0;
};

class HLPLAYER_CORE_API QoSManager {
public:
    QoSManager();
    ~QoSManager() = default;

    void updateMetrics(const NetworkMetrics& metrics);
    QoSState getQoSState() const;
    QoSLevel getRecommendedLevel() const;
    NetworkMetrics getMetrics() const;

private:
    static constexpr double kBufferingLatencyMs = 200.0;
    static constexpr double kDegradedLatencyMs = 500.0;
    static constexpr double kRecoveryLatencyMs = 300.0;

    std::atomic<QoSState> state_{QoSState::Stable};
    NetworkMetrics metrics_{};
};

} // namespace hlplayer

#endif // HLPLAYER_QOSMANAGER_H
