#ifndef HLPLAYER_VSRSYNCCOMPENSATOR_H
#define HLPLAYER_VSRSYNCCOMPENSATOR_H

#include <hlplayer/Export.h>
#include <hlplayer/GpuFrameContract.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>

namespace hlplayer {

class SyncClock;
class EventBus;

// ============================================================================
// VSRSyncCompensator
// ============================================================================

/// Compensates for VSR processing latency to maintain A/V synchronization.
///
/// Wraps SyncClock and tracks a rolling average of VSR inference times.
/// When frames exit the VSR pipeline, their PTS is adjusted downward by
/// the average inference latency so that downstream sync logic (VideoRefreshThread)
/// sees them at the correct wall-clock time.
///
/// Thread-safety: All public methods are thread-safe.
/// - Mutex-protected: latency ring buffer, internal state
/// - Atomic: drop counters, stats
class HLPLAYER_CORE_API VSRSyncCompensator {
public:
    /// Default: drop frames >100ms late, track last 30 inference samples.
    static constexpr double kDropThresholdSec = 0.1;
    static constexpr size_t kLatencyWindowSize = 30;

    // ----------------------------------------------------------------
    // Construction
    // ----------------------------------------------------------------

    VSRSyncCompensator();
    ~VSRSyncCompensator();

    VSRSyncCompensator(const VSRSyncCompensator&) = delete;
    VSRSyncCompensator& operator=(const VSRSyncCompensator&) = delete;
    VSRSyncCompensator(VSRSyncCompensator&&) = delete;
    VSRSyncCompensator& operator=(VSRSyncCompensator&&) = delete;

    // ----------------------------------------------------------------
    // Configuration (call before using)
    // ----------------------------------------------------------------

    /// Set the sync clock for compensated PTS updates.
    /// If null, compensatePTS still adjusts frame PTS but does not update SyncClock.
    void setSyncClock(SyncClock* syncClock);

    /// Set the event bus for frame-drop and timing events.
    /// If null, no events are emitted.
    void setEventBus(EventBus* eventBus);

    /// Set the drop threshold in seconds (default 0.1s).
    void setDropThreshold(double thresholdSec);

    // ----------------------------------------------------------------
    // Latency tracking
    // ----------------------------------------------------------------

    /// Feed a VSR inference duration into the rolling average.
    /// Called after each VSR frame completes.
    void updateInferenceLatency(double durationMs);

    /// Get the current average VSR inference latency in milliseconds.
    double getAverageLatency() const;

    /// Get the current average latency in seconds (convenience).
    double getAverageLatencySec() const;

    // ----------------------------------------------------------------
    // PTS compensation
    // ----------------------------------------------------------------

    /// Compensate a frame's PTS by subtracting the average inference latency.
    /// Also updates SyncClock::videoClock with the compensated PTS.
    /// Returns the compensated PTS value.
    double compensatePTS(GpuFrame& frame);

    /// Compensate a raw PTS value (does not modify any frame).
    double compensatePTSValue(double pts) const;

    // ----------------------------------------------------------------
    // Frame drop logic
    // ----------------------------------------------------------------

    /// Check whether a frame should be dropped based on sync drift.
    /// Returns true if the frame's PTS is more than dropThreshold behind
    /// the master clock.
    bool shouldDropFrame(const GpuFrame& frame);

    /// Check whether a frame should be dropped given the current queue
    /// depth and frame PTS. Returns true if the frame is late OR the
    /// queue is at capacity (aggressive drop).
    bool shouldDropFrameAggressive(const GpuFrame& frame, size_t queueDepth, size_t queueCapacity);

    // ----------------------------------------------------------------
    // Stats
    // ----------------------------------------------------------------

    /// Total number of frames dropped by sync compensation.
    uint64_t totalDroppedFrames() const;

    /// Total number of frames that had PTS compensated.
    uint64_t totalCompensatedFrames() const;

    /// Total number of frames aggressively dropped (queue-full).
    uint64_t totalAggressiveDrops() const;

    /// Number of inference latency samples collected.
    size_t latencySampleCount() const;

    // ----------------------------------------------------------------
    // Lifecycle
    // ----------------------------------------------------------------

    /// Reset all state: latency buffer, drop counters, stats.
    void reset();

private:
    // ----------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------

    /// Emit a LatencyPayload event via EventBus.
    void emitLatencyEvent(double latencyMs);

    // ----------------------------------------------------------------
    // External dependencies (not owned)
    // ----------------------------------------------------------------

    SyncClock* syncClock_ = nullptr;
    EventBus* eventBus_ = nullptr;

    // ----------------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------------

    double dropThresholdSec_ = kDropThresholdSec;

    // ----------------------------------------------------------------
    // Latency tracking (mutex-protected)
    // ----------------------------------------------------------------

    mutable std::mutex latencyMutex_;
    std::deque<double> latencyBuffer_;  ///< Rolling window of inference durations (ms)
    double latencySum_ = 0.0;           ///< Running sum for O(1) average

    // ----------------------------------------------------------------
    // Stats (atomic)
    // ----------------------------------------------------------------

    std::atomic<uint64_t> totalDroppedFrames_{0};
    std::atomic<uint64_t> totalCompensatedFrames_{0};
    std::atomic<uint64_t> totalAggressiveDrops_{0};
};

} // namespace hlplayer

#endif // HLPLAYER_VSRSYNCCOMPENSATOR_H
