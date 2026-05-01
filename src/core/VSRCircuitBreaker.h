#ifndef HLPLAYER_VSRCIRCUITBREAKER_H
#define HLPLAYER_VSRCIRCUITBREAKER_H

#include <hlplayer/Export.h>
#include <hlplayer/Result.h>
#include <hlplayer/IVRAMBudgetManager.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace hlplayer {

class EventBus;

// ============================================================================
// VSR Circuit Breaker States
// ============================================================================

/// States for the VSR inference circuit breaker.
enum class VSRBreakerState : uint8_t {
    Active = 0,      ///< VSR inference running normally
    CircuitOpen,     ///< VSR bypassed due to sustained slow inference
    Probing,         ///< Testing one frame to check recovery viability
    Disabled         ///< VSR permanently disabled (manual override)
};

/// VRAM budget degradation action determined by current VRAM pressure.
enum class VRAMDegradationAction : uint8_t {
    None = 0,        ///< No degradation needed (VRAM OK)
    ReduceScale,     ///< Reduce scale factor (e.g. 4x -> 2x)
    DisableVSR       ///< Disable VSR entirely (emergency)
};

// ============================================================================
// Configuration
// ============================================================================

/// Configuration for the VSR circuit breaker state machine.
struct VSRCircuitBreakerConfig {
    /// Inference time threshold in ms. Consecutive frames above this open the circuit.
    double slowThresholdMs = 16.0;

    /// Number of consecutive slow frames before circuit opens.
    int slowFrameCount = 3;

    /// Cooldown in seconds before probing after circuit opens.
    double cooldownSeconds = 30.0;

    /// VRAM usage ratio that triggers scale reduction (default 85%).
    double vramDegradationThreshold = 0.85;

    /// VRAM usage ratio that triggers full VSR disable (default 95%).
    double vramEmergencyThreshold = 0.95;

    /// Scale factor used when degradation kicks in (default 2x).
    double degradedScaleFactor = 2.0;
};

// ============================================================================
// VSRCircuitBreaker
// ============================================================================

/// Thread-safe circuit breaker state machine for VSR inference.
///
/// States: Active -> CircuitOpen -> Probing -> Active/CircuitOpen
///         Any -> Disabled (manual override)
///         Disabled -> Active (manual re-enable)
///
/// Transitions use atomic compare_exchange for lock-free state changes.
/// All state transitions emit events via EventBus and spdlog.
///
/// VRAM Budget Degradation is integrated: when VRAM usage exceeds the
/// degradation threshold, recommends scale reduction; at emergency threshold,
/// forces circuit open.
class HLPLAYER_CORE_API VSRCircuitBreaker {
public:
    explicit VSRCircuitBreaker(VSRCircuitBreakerConfig config = {});
    ~VSRCircuitBreaker() = default;

    VSRCircuitBreaker(const VSRCircuitBreaker&) = delete;
    VSRCircuitBreaker& operator=(const VSRCircuitBreaker&) = delete;

    // ----------------------------------------------------------------
    // State machine interface
    // ----------------------------------------------------------------

    /// Get current circuit breaker state.
    VSRBreakerState state() const;

    /// Returns true if VSR should be bypassed (CircuitOpen or Disabled).
    bool shouldBypass() const;

    /// Record a VSR inference result. Updates circuit breaker state.
    /// Call from the VSR processing thread.
    void recordInference(double durationMs);

    /// Force-disable VSR permanently (manual override).
    void forceDisable();

    /// Re-enable VSR after manual disable. Transitions Disabled -> Active.
    void forceEnable();

    /// Reset to Active state (e.g. on pipeline stop/restart).
    void reset();

    /// Get the current recommended scale factor based on VRAM pressure.
    /// Returns 0.0 if no degradation is needed (caller should use default).
    double recommendedScaleFactor() const;

    /// Check VRAM pressure and return degradation action.
    /// Call periodically (e.g. once per frame) from the VSR thread.
    /// @param vramManager VRAM budget manager (may be nullptr).
    VRAMDegradationAction checkVRAMPressure(
        IVRAMBudgetManager* vramManager);

    /// Set the event bus for state transition notifications.
    void setEventBus(EventBus* eventBus);

    /// Get number of consecutive slow frames recorded.
    int consecutiveSlowFrames() const;

    /// Get the time point when circuit was opened (for cooldown check).
    std::chrono::steady_clock::time_point circuitOpenTime() const;

    /// Convert state to human-readable string.
    static const char* stateToString(VSRBreakerState state);

    // ----------------------------------------------------------------
    // Internal transition hooks (called by pipeline integration)
    // ----------------------------------------------------------------

    /// Check if cooldown has elapsed and we should start probing.
    /// Returns true if transitioned to Probing state.
    bool tryStartProbing();

    /// Handle result of a probe frame.
    /// Transitions Probing -> Active (success) or Probing -> CircuitOpen (fail).
    void handleProbeResult(double inferenceMs);

    // ----------------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------------

    /// Update configuration at runtime. Thread-safe.
    void updateConfig(VSRCircuitBreakerConfig config);

    /// Get current configuration.
    VSRCircuitBreakerConfig config() const;

private:
    /// Attempt atomic state transition. Returns true on success.
    bool tryTransition(VSRBreakerState expected, VSRBreakerState target);

    /// Emit a state transition event via EventBus.
    void emitTransitionEvent(VSRBreakerState from, VSRBreakerState to,
                             const std::string& reason);

    // Configuration
    VSRCircuitBreakerConfig config_;

    // State (atomic for lock-free reads)
    std::atomic<VSRBreakerState> state_{VSRBreakerState::Active};

    // Circuit breaker metrics (protected by mutex for multi-field consistency)
    mutable std::mutex metricsMutex_;
    int consecutiveSlowFrames_ = 0;
    std::chrono::steady_clock::time_point circuitOpenTime_;

    // VRAM degradation state (protected by mutex)
    mutable std::mutex vramMutex_;
    VRAMDegradationAction currentVRAMAction_ = VRAMDegradationAction::None;
    double effectiveScaleFactor_ = 0.0; // 0.0 = no override

    // EventBus (not owned)
    EventBus* eventBus_ = nullptr;
};

} // namespace hlplayer

#endif // HLPLAYER_VSRCIRCUITBREAKER_H
