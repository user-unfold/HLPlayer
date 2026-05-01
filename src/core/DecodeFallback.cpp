#include "DecodeFallback.h"

#include <spdlog/spdlog.h>

namespace hlplayer {

// ============================================================================
// DecodeFallback
// ============================================================================

DecodeFallback::DecodeFallback(Config config)
    : config_(std::move(config)) {}

// ----------------------------------------------------------------
// State machine interface
// ----------------------------------------------------------------

void DecodeFallback::onDecodeResult(DecodeBackend backend,
                                     const Result<void>& result) {
    auto current = state_.load(std::memory_order_acquire);

    if (result.hasValue()) {
        // Success - reset failure counters
        std::lock_guard lock(metricsMutex_);
        if (backend == DecodeBackend::Hardware) {
            consecutiveHWFailures_ = 0;
            ++hwSuccessCount_;
        } else {
            consecutiveSWFailures_ = 0;
            ++swSuccessCount_;
        }
        return;
    }

    // Failure
    spdlog::warn("[DecodeFallback] Decode failure on {} backend: error={}",
                 backendToString(backend),
                 static_cast<int>(result.error()));

    if (backend == DecodeBackend::Hardware &&
        current == DecodeFallbackState::HardwareActive) {
        std::lock_guard lock(metricsMutex_);
        ++consecutiveHWFailures_;
        ++hwFailureCount_;
        int threshold = config_.hwFailureThreshold;

        if (consecutiveHWFailures_ >= threshold) {
            if (config_.softwareFallbackAllowed) {
                if (tryTransition(DecodeFallbackState::HardwareActive,
                                  DecodeFallbackState::SoftwareActive)) {
                    consecutiveHWFailures_ = 0;
                    consecutiveSWFailures_ = 0;
                    spdlog::warn("[DecodeFallback] {} consecutive HW decode "
                                 "failures -> switching to Software backend",
                                 threshold);
                }
            } else {
                if (tryTransition(DecodeFallbackState::HardwareActive,
                                  DecodeFallbackState::Error)) {
                    consecutiveHWFailures_ = 0;
                    spdlog::error("[DecodeFallback] {} consecutive HW decode "
                                  "failures -> ERROR (SW fallback disabled)",
                                  threshold);
                }
            }
        }
    } else if (backend == DecodeBackend::Software &&
               current == DecodeFallbackState::SoftwareActive) {
        std::lock_guard lock(metricsMutex_);
        ++consecutiveSWFailures_;
        ++swFailureCount_;
        int threshold = config_.swFailureThreshold;

        if (consecutiveSWFailures_ >= threshold) {
            if (tryTransition(DecodeFallbackState::SoftwareActive,
                              DecodeFallbackState::Error)) {
                consecutiveSWFailures_ = 0;
                spdlog::error("[DecodeFallback] {} consecutive SW decode "
                              "failures -> ERROR state", threshold);
            }
        }
    } else if (current == DecodeFallbackState::Error) {
        // Already in error state, just count
        std::lock_guard lock(metricsMutex_);
        if (backend == DecodeBackend::Hardware) {
            ++hwFailureCount_;
        } else {
            ++swFailureCount_;
        }
    }
}

DecodeBackend DecodeFallback::currentBackend() const {
    auto s = state_.load(std::memory_order_acquire);
    switch (s) {
        case DecodeFallbackState::HardwareActive:
            return DecodeBackend::Hardware;
        case DecodeFallbackState::SoftwareActive:
            return DecodeBackend::Software;
        case DecodeFallbackState::Error:
            return DecodeBackend::Software; // Best effort
    }
    return DecodeBackend::Hardware;
}

DecodeFallbackState DecodeFallback::currentState() const {
    return state_.load(std::memory_order_acquire);
}

bool DecodeFallback::isOperational() const {
    return state_.load(std::memory_order_acquire) != DecodeFallbackState::Error;
}

void DecodeFallback::forceSoftware() {
    auto prev = DecodeFallbackState::HardwareActive;
    if (state_.compare_exchange_strong(prev, DecodeFallbackState::SoftwareActive,
            std::memory_order_acq_rel)) {
        std::lock_guard lock(metricsMutex_);
        consecutiveHWFailures_ = 0;
        consecutiveSWFailures_ = 0;
        spdlog::info("[DecodeFallback] Manually forced Software backend");
    }
    // If already SoftwareActive or Error, do nothing
}

void DecodeFallback::forceHardware() {
    auto prev = state_.load(std::memory_order_acquire);
    while (prev != DecodeFallbackState::HardwareActive) {
        if (state_.compare_exchange_strong(prev,
                DecodeFallbackState::HardwareActive,
                std::memory_order_acq_rel)) {
            std::lock_guard lock(metricsMutex_);
            consecutiveHWFailures_ = 0;
            consecutiveSWFailures_ = 0;
            spdlog::info("[DecodeFallback] Manually forced Hardware backend "
                         "(was {})", stateToString(prev));
            return;
        }
        // prev updated by CAS, retry
    }
}

void DecodeFallback::reset() {
    auto prev = state_.exchange(DecodeFallbackState::HardwareActive,
                                std::memory_order_acq_rel);
    if (prev != DecodeFallbackState::HardwareActive) {
        std::lock_guard lock(metricsMutex_);
        consecutiveHWFailures_ = 0;
        consecutiveSWFailures_ = 0;
        hwSuccessCount_ = 0;
        swSuccessCount_ = 0;
        hwFailureCount_ = 0;
        swFailureCount_ = 0;
        spdlog::info("[DecodeFallback] Reset to HardwareActive (was {})",
                     stateToString(prev));
    }
}

// ----------------------------------------------------------------
// String conversions
// ----------------------------------------------------------------

const char* DecodeFallback::stateToString(DecodeFallbackState state) {
    switch (state) {
        case DecodeFallbackState::HardwareActive: return "HardwareActive";
        case DecodeFallbackState::SoftwareActive: return "SoftwareActive";
        case DecodeFallbackState::Error:          return "Error";
    }
    return "Unknown";
}

const char* DecodeFallback::backendToString(DecodeBackend backend) {
    switch (backend) {
        case DecodeBackend::Hardware: return "Hardware";
        case DecodeBackend::Software: return "Software";
    }
    return "Unknown";
}

// ----------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------

void DecodeFallback::updateConfig(Config config) {
    std::lock_guard lock(metricsMutex_);
    config_ = std::move(config);
    spdlog::info("[DecodeFallback] Configuration updated");
}

DecodeFallback::Config DecodeFallback::config() const {
    std::lock_guard lock(metricsMutex_);
    return config_;
}

// ----------------------------------------------------------------
// Statistics
// ----------------------------------------------------------------

uint64_t DecodeFallback::hwSuccessCount() const {
    std::lock_guard lock(metricsMutex_);
    return hwSuccessCount_;
}

uint64_t DecodeFallback::swSuccessCount() const {
    std::lock_guard lock(metricsMutex_);
    return swSuccessCount_;
}

uint64_t DecodeFallback::hwFailureCount() const {
    std::lock_guard lock(metricsMutex_);
    return hwFailureCount_;
}

uint64_t DecodeFallback::swFailureCount() const {
    std::lock_guard lock(metricsMutex_);
    return swFailureCount_;
}

// ----------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------

bool DecodeFallback::tryTransition(DecodeFallbackState expected,
                                   DecodeFallbackState target) {
    auto prev = expected;
    return state_.compare_exchange_strong(prev, target,
                                          std::memory_order_acq_rel);
}

} // namespace hlplayer
