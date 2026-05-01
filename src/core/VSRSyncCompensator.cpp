#include <hlplayer/VSRSyncCompensator.h>
#include <hlplayer/SyncClock.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/logger.h>

#include <algorithm>
#include <cmath>

namespace hlplayer {

// ============================================================================
// Construction / Destruction
// ============================================================================

VSRSyncCompensator::VSRSyncCompensator() = default;

VSRSyncCompensator::~VSRSyncCompensator() = default;

// ============================================================================
// Configuration
// ============================================================================

void VSRSyncCompensator::setSyncClock(SyncClock* syncClock) {
    syncClock_ = syncClock;
}

void VSRSyncCompensator::setEventBus(EventBus* eventBus) {
    eventBus_ = eventBus;
}

void VSRSyncCompensator::setDropThreshold(double thresholdSec) {
    dropThresholdSec_ = thresholdSec;
}

// ============================================================================
// Latency tracking
// ============================================================================

void VSRSyncCompensator::updateInferenceLatency(double durationMs) {
    {
        std::lock_guard lock(latencyMutex_);

        if (latencyBuffer_.size() >= kLatencyWindowSize) {
            // Remove oldest sample from running sum
            latencySum_ -= latencyBuffer_.front();
            latencyBuffer_.pop_front();
        }

        latencyBuffer_.push_back(durationMs);
        latencySum_ += durationMs;
    }

    // Emit latency event for monitoring/telemetry
    emitLatencyEvent(durationMs);
}

double VSRSyncCompensator::getAverageLatency() const {
    std::lock_guard lock(latencyMutex_);

    if (latencyBuffer_.empty()) {
        return 0.0;
    }

    return latencySum_ / static_cast<double>(latencyBuffer_.size());
}

double VSRSyncCompensator::getAverageLatencySec() const {
    return getAverageLatency() / 1000.0;
}

// ============================================================================
// PTS compensation
// ============================================================================

double VSRSyncCompensator::compensatePTS(GpuFrame& frame) {
    double avgLatencySec = getAverageLatencySec();
    double compensatedPts = frame.timestamp - avgLatencySec;
    frame.timestamp = compensatedPts;
    ++totalCompensatedFrames_;

    // Update SyncClock video clock with compensated PTS
    if (syncClock_) {
        syncClock_->setVideoClock(compensatedPts);
    }

    return compensatedPts;
}

double VSRSyncCompensator::compensatePTSValue(double pts) const {
    return pts - getAverageLatencySec();
}

// ============================================================================
// Frame drop logic
// ============================================================================

bool VSRSyncCompensator::shouldDropFrame(const GpuFrame& frame) {
    if (!syncClock_) {
        return false;
    }

    double masterClock = syncClock_->getMasterClock();
    double drift = masterClock - frame.timestamp;

    if (drift > dropThresholdSec_) {
        totalDroppedFrames_.fetch_add(1, std::memory_order_relaxed);
        LOG_WARN("VSRSyncCompensator: dropping frame PTS={:.3f}, master={:.3f}, drift={:.3f}s > threshold {:.3f}s",
                 frame.timestamp, masterClock, drift, dropThresholdSec_);
        return true;
    }

    return false;
}

bool VSRSyncCompensator::shouldDropFrameAggressive(const GpuFrame& frame,
                                                    size_t queueDepth,
                                                    size_t queueCapacity) {
    // Aggressive drop: if queue is at capacity, drop oldest immediately
    if (queueDepth >= queueCapacity && queueCapacity > 0) {
        totalAggressiveDrops_.fetch_add(1, std::memory_order_relaxed);
        LOG_WARN("VSRSyncCompensator: aggressive drop, queue full (depth={}, cap={})",
                 queueDepth, queueCapacity);
        return true;
    }

    // Also check sync drift
    return shouldDropFrame(frame);
}

// ============================================================================
// Stats
// ============================================================================

uint64_t VSRSyncCompensator::totalDroppedFrames() const {
    return totalDroppedFrames_.load(std::memory_order_acquire);
}

uint64_t VSRSyncCompensator::totalCompensatedFrames() const {
    return totalCompensatedFrames_.load(std::memory_order_acquire);
}

uint64_t VSRSyncCompensator::totalAggressiveDrops() const {
    return totalAggressiveDrops_.load(std::memory_order_acquire);
}

size_t VSRSyncCompensator::latencySampleCount() const {
    std::lock_guard lock(latencyMutex_);
    return latencyBuffer_.size();
}

// ============================================================================
// Lifecycle
// ============================================================================

void VSRSyncCompensator::reset() {
    {
        std::lock_guard lock(latencyMutex_);
        latencyBuffer_.clear();
        latencySum_ = 0.0;
    }

    totalDroppedFrames_.store(0, std::memory_order_release);
    totalCompensatedFrames_.store(0, std::memory_order_release);
    totalAggressiveDrops_.store(0, std::memory_order_release);
}

// ============================================================================
// Helpers (private)
// ============================================================================

void VSRSyncCompensator::emitLatencyEvent(double latencyMs) {
    if (!eventBus_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    double timestampSec = std::chrono::duration<double>(
        now.time_since_epoch()
    ).count();

    eventBus_->publish(Event{
        EventType::LatencyMeasured,
        timestampSec,
        LatencyPayload{latencyMs}
    });
}

} // namespace hlplayer
