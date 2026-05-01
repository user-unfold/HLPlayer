#ifndef HLPLAYER_DECODEFALLBACK_H
#define HLPLAYER_DECODEFALLBACK_H

#include <hlplayer/Export.h>
#include <hlplayer/Result.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace hlplayer {

// ============================================================================
// Decode Backend & Fallback States
// ============================================================================

/// Available decode backends.
enum class DecodeBackend : uint8_t {
    Hardware = 0,    ///< Hardware-accelerated decoding (D3D11VA/VAAPI/CUDA)
    Software         ///< Software (CPU) decoding fallback
};

/// Decode fallback state machine states.
enum class DecodeFallbackState : uint8_t {
    HardwareActive = 0,  ///< Using hardware decoder
    SoftwareActive,      ///< Using software decoder (HW failed)
    Error                ///< Both backends failed
};

/// Configuration for decode fallback behavior.
struct DecodeFallbackConfig {
    /// Number of consecutive HW decode failures before switching to SW.
    int hwFailureThreshold = 3;

    /// Number of consecutive SW decode failures before entering Error state.
    int swFailureThreshold = 5;

    /// Whether SW fallback is allowed. If false, goes directly to Error.
    bool softwareFallbackAllowed = true;

    DecodeFallbackConfig() = default;
};

// ============================================================================
// DecodeFallback
// ============================================================================

/// Thread-safe decode fallback state machine: HW -> SW -> Error.
///
/// Monitors decode results and automatically degrades when failures occur.
/// All transitions are logged via spdlog.
///
/// Usage:
///   1. Create with desired consecutive failure thresholds.
///   2. Call onDecodeResult() after each decode attempt.
///   3. Check currentBackend() and currentState() before decoding.
class HLPLAYER_CORE_API DecodeFallback {
public:
    using Config = DecodeFallbackConfig;

    explicit DecodeFallback(Config config = {});
    ~DecodeFallback() = default;

    DecodeFallback(const DecodeFallback&) = delete;
    DecodeFallback& operator=(const DecodeFallback&) = delete;

    // ----------------------------------------------------------------
    // State machine interface
    // ----------------------------------------------------------------

    /// Feed a decode result. Triggers state transitions on failures.
    /// @param backend The backend that produced this result.
    /// @param result The decode result (success or error).
    void onDecodeResult(DecodeBackend backend, const Result<void>& result);

    /// Get the currently active decode backend.
    DecodeBackend currentBackend() const;

    /// Get the current fallback state.
    DecodeFallbackState currentState() const;

    /// Check if decoding is possible (not in Error state).
    bool isOperational() const;

    /// Manually force software decoding (skip HW entirely).
    void forceSoftware();

    /// Manually force hardware decoding (attempt HW again).
    void forceHardware();

    /// Reset to HardwareActive state (e.g. on stream change).
    void reset();

    /// Convert state to human-readable string.
    static const char* stateToString(DecodeFallbackState state);

    /// Convert backend to human-readable string.
    static const char* backendToString(DecodeBackend backend);

    // ----------------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------------

    /// Update configuration at runtime. Thread-safe.
    void updateConfig(Config config);

    /// Get current configuration.
    Config config() const;

    // ----------------------------------------------------------------
    // Statistics
    // ----------------------------------------------------------------

    /// Get total number of HW decode successes since last reset.
    uint64_t hwSuccessCount() const;

    /// Get total number of SW decode successes since last reset.
    uint64_t swSuccessCount() const;

    /// Get total number of HW decode failures since last reset.
    uint64_t hwFailureCount() const;

    /// Get total number of SW decode failures since last reset.
    uint64_t swFailureCount() const;

private:
    /// Attempt atomic state transition. Returns true on success.
    bool tryTransition(DecodeFallbackState expected, DecodeFallbackState target);

    Config config_;

    std::atomic<DecodeFallbackState> state_{DecodeFallbackState::HardwareActive};

    mutable std::mutex metricsMutex_;
    int consecutiveHWFailures_ = 0;
    int consecutiveSWFailures_ = 0;
    uint64_t hwSuccessCount_ = 0;
    uint64_t swSuccessCount_ = 0;
    uint64_t hwFailureCount_ = 0;
    uint64_t swFailureCount_ = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_DECODEFALLBACK_H
