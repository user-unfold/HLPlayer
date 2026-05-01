#include <hlplayer/QoSManager.h>

namespace hlplayer {

QoSManager::QoSManager() = default;

void QoSManager::updateMetrics(const NetworkMetrics& metrics) {
    metrics_ = metrics;

    auto current = state_.load(std::memory_order_acquire);
    QoSState next = current;

    switch (current) {
        case QoSState::Stable:
            if (metrics.latencyMs > kDegradedLatencyMs) {
                next = QoSState::Degraded;
            } else if (metrics.latencyMs > kBufferingLatencyMs) {
                next = QoSState::Buffering;
            }
            break;

        case QoSState::Buffering:
            if (metrics.latencyMs > kDegradedLatencyMs) {
                next = QoSState::Degraded;
            } else if (metrics.latencyMs < kBufferingLatencyMs) {
                next = QoSState::Stable;
            }
            break;

        case QoSState::Degraded:
            if (metrics.latencyMs < kRecoveryLatencyMs) {
                next = QoSState::Recovering;
            }
            break;

        case QoSState::Recovering:
            if (metrics.latencyMs < kBufferingLatencyMs) {
                next = QoSState::Stable;
            } else if (metrics.latencyMs > kDegradedLatencyMs) {
                next = QoSState::Degraded;
            }
            break;
    }

    state_.store(next, std::memory_order_release);
}

QoSState QoSManager::getQoSState() const {
    return state_.load(std::memory_order_acquire);
}

QoSLevel QoSManager::getRecommendedLevel() const {
    switch (state_.load(std::memory_order_acquire)) {
        case QoSState::Stable:     return QoSLevel::High;
        case QoSState::Buffering:  return QoSLevel::Medium;
        case QoSState::Degraded:   return QoSLevel::Low;
        case QoSState::Recovering: return QoSLevel::Medium;
    }
    return QoSLevel::Medium;
}

NetworkMetrics QoSManager::getMetrics() const {
    return metrics_;
}

} // namespace hlplayer
