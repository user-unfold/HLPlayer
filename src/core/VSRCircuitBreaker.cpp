#include "VSRCircuitBreaker.h"
#include <hlplayer/EventBus.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace hlplayer {

// ============================================================================
// VSRCircuitBreaker
// ============================================================================

VSRCircuitBreaker::VSRCircuitBreaker(VSRCircuitBreakerConfig config)
    : config_(std::move(config)) {}

// ----------------------------------------------------------------
// State queries
// ----------------------------------------------------------------

VSRBreakerState VSRCircuitBreaker::state() const {
    return state_.load(std::memory_order_acquire);
}

bool VSRCircuitBreaker::shouldBypass() const {
    auto s = state_.load(std::memory_order_acquire);
    return s == VSRBreakerState::CircuitOpen || s == VSRBreakerState::Disabled;
}

int VSRCircuitBreaker::consecutiveSlowFrames() const {
    std::lock_guard lock(metricsMutex_);
    return consecutiveSlowFrames_;
}

std::chrono::steady_clock::time_point VSRCircuitBreaker::circuitOpenTime() const {
    std::lock_guard lock(metricsMutex_);
    return circuitOpenTime_;
}

const char* VSRCircuitBreaker::stateToString(VSRBreakerState state) {
    switch (state) {
        case VSRBreakerState::Active:      return "Active";
        case VSRBreakerState::CircuitOpen: return "CircuitOpen";
        case VSRBreakerState::Probing:     return "Probing";
        case VSRBreakerState::Disabled:    return "Disabled";
    }
    return "Unknown";
}

// ----------------------------------------------------------------
// State transitions
// ----------------------------------------------------------------

void VSRCircuitBreaker::recordInference(double durationMs) {
    VSRBreakerState current = state_.load(std::memory_order_acquire);

    // Only track slow frames in Active state
    if (current != VSRBreakerState::Active) {
        return;
    }

    bool isSlow = durationMs > config_.slowThresholdMs;

    {
        std::lock_guard lock(metricsMutex_);
        if (isSlow) {
            ++consecutiveSlowFrames_;
            if (consecutiveSlowFrames_ >= config_.slowFrameCount) {
                consecutiveSlowFrames_ = 0;
                // Try to transition Active -> CircuitOpen
                if (tryTransition(VSRBreakerState::Active,
                                  VSRBreakerState::CircuitOpen)) {
                    circuitOpenTime_ = std::chrono::steady_clock::now();
                    spdlog::warn("[VSRCircuitBreaker] Circuit OPEN: "
                                 "{} consecutive frames > {:.1f}ms "
                                 "(last={:.1f}ms)",
                                 config_.slowFrameCount,
                                 config_.slowThresholdMs, durationMs);
                    emitTransitionEvent(VSRBreakerState::Active,
                                        VSRBreakerState::CircuitOpen,
                                        "Inference too slow");
                }
                return;
            }
        } else {
            consecutiveSlowFrames_ = 0;
        }
    }
}

void VSRCircuitBreaker::forceDisable() {
    auto prev = state_.load(std::memory_order_acquire);
    while (prev != VSRBreakerState::Disabled) {
        if (state_.compare_exchange_strong(prev, VSRBreakerState::Disabled,
                std::memory_order_acq_rel)) {
            spdlog::warn("[VSRCircuitBreaker] Manually DISABLED (was {})",
                         stateToString(prev));
            emitTransitionEvent(prev, VSRBreakerState::Disabled,
                                "Manual disable");
            return;
        }
        // prev was updated by CAS, retry
    }
}

void VSRCircuitBreaker::forceEnable() {
    auto prev = VSRBreakerState::Disabled;
    if (state_.compare_exchange_strong(prev, VSRBreakerState::Active,
            std::memory_order_acq_rel)) {
        std::lock_guard lock(metricsMutex_);
        consecutiveSlowFrames_ = 0;
        spdlog::info("[VSRCircuitBreaker] Manually ENABLED (was Disabled)");
        emitTransitionEvent(VSRBreakerState::Disabled,
                            VSRBreakerState::Active,
                            "Manual enable");
    } else {
        spdlog::warn("[VSRCircuitBreaker] forceEnable() called but state is {} "
                     "(not Disabled)", stateToString(prev));
    }
}

void VSRCircuitBreaker::reset() {
    auto prev = state_.exchange(VSRBreakerState::Active,
                                std::memory_order_acq_rel);
    if (prev != VSRBreakerState::Active) {
        std::lock_guard lock(metricsMutex_);
        consecutiveSlowFrames_ = 0;
        std::lock_guard vlock(vramMutex_);
        currentVRAMAction_ = VRAMDegradationAction::None;
        effectiveScaleFactor_ = 0.0;
        spdlog::info("[VSRCircuitBreaker] Reset to Active (was {})",
                     stateToString(prev));
    }
}

bool VSRCircuitBreaker::tryStartProbing() {
    auto current = state_.load(std::memory_order_acquire);
    if (current != VSRBreakerState::CircuitOpen) {
        return false;
    }

    // Check cooldown
    std::chrono::steady_clock::time_point openTime;
    {
        std::lock_guard lock(metricsMutex_);
        openTime = circuitOpenTime_;
    }

    auto elapsed = std::chrono::steady_clock::now() - openTime;
    auto cooldown = std::chrono::duration<double>(config_.cooldownSeconds);
    if (elapsed < cooldown) {
        return false;
    }

    if (tryTransition(VSRBreakerState::CircuitOpen,
                      VSRBreakerState::Probing)) {
        spdlog::info("[VSRCircuitBreaker] Probing VSR after "
                     "{:.1f}s cooldown", config_.cooldownSeconds);
        emitTransitionEvent(VSRBreakerState::CircuitOpen,
                            VSRBreakerState::Probing,
                            "Cooldown elapsed");
        return true;
    }
    return false;
}

void VSRCircuitBreaker::handleProbeResult(double inferenceMs) {
    auto current = state_.load(std::memory_order_acquire);
    if (current != VSRBreakerState::Probing) {
        return;
    }

    bool success = inferenceMs <= config_.slowThresholdMs;
    VSRBreakerState target = success ? VSRBreakerState::Active
                                     : VSRBreakerState::CircuitOpen;

    if (tryTransition(VSRBreakerState::Probing, target)) {
        if (success) {
            std::lock_guard lock(metricsMutex_);
            consecutiveSlowFrames_ = 0;
            spdlog::info("[VSRCircuitBreaker] VSR RECOVERED "
                         "(probe={:.1f}ms)", inferenceMs);
            emitTransitionEvent(VSRBreakerState::Probing,
                                VSRBreakerState::Active,
                                "Probe succeeded");
        } else {
            {
                std::lock_guard lock(metricsMutex_);
                circuitOpenTime_ = std::chrono::steady_clock::now();
            }
            spdlog::warn("[VSRCircuitBreaker] Probe FAILED "
                         "({:.1f}ms > {:.1f}ms), staying bypassed",
                         inferenceMs, config_.slowThresholdMs);
            emitTransitionEvent(VSRBreakerState::Probing,
                                VSRBreakerState::CircuitOpen,
                                "Probe failed");
        }
    }
}

// ----------------------------------------------------------------
// VRAM Budget Degradation
// ----------------------------------------------------------------

VRAMDegradationAction VSRCircuitBreaker::checkVRAMPressure(
    IVRAMBudgetManager* vramManager) {
    if (!vramManager) {
        return VRAMDegradationAction::None;
    }

    auto used = vramManager->usedBytes();
    auto available = vramManager->availableBytes();
    auto total = used + available;
    if (total == 0) {
        return VRAMDegradationAction::None;
    }

    double usageRatio = static_cast<double>(used) / static_cast<double>(total);

    VRAMDegradationAction action;
    double scaleFactor;

    if (usageRatio >= config_.vramEmergencyThreshold) {
        action = VRAMDegradationAction::DisableVSR;
        scaleFactor = 0.0; // Signal disable
        spdlog::error("[VSRCircuitBreaker] VRAM EMERGENCY: {:.1f}% usage "
                      "({} / {} bytes) - disabling VSR",
                      usageRatio * 100.0, used, total);
    } else if (usageRatio >= config_.vramDegradationThreshold) {
        action = VRAMDegradationAction::ReduceScale;
        scaleFactor = config_.degradedScaleFactor;
        spdlog::warn("[VSRCircuitBreaker] VRAM DEGRADATION: {:.1f}% usage "
                     "({} / {} bytes) - reducing scale to {:.0f}x",
                     usageRatio * 100.0, used, total, scaleFactor);
    } else {
        action = VRAMDegradationAction::None;
        scaleFactor = 0.0;
    }

    {
        std::lock_guard lock(vramMutex_);
        currentVRAMAction_ = action;
        effectiveScaleFactor_ = scaleFactor;
    }

    // If emergency, force circuit open
    if (action == VRAMDegradationAction::DisableVSR) {
        auto current = state_.load(std::memory_order_acquire);
        if (current == VSRBreakerState::Active ||
            current == VSRBreakerState::Probing) {
            if (tryTransition(current, VSRBreakerState::CircuitOpen)) {
                std::lock_guard lock(metricsMutex_);
                circuitOpenTime_ = std::chrono::steady_clock::now();
                emitTransitionEvent(current, VSRBreakerState::CircuitOpen,
                                    "VRAM emergency");
            }
        }
    }

    return action;
}

double VSRCircuitBreaker::recommendedScaleFactor() const {
    std::lock_guard lock(vramMutex_);
    return effectiveScaleFactor_; // 0.0 = no override
}

// ----------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------

void VSRCircuitBreaker::updateConfig(VSRCircuitBreakerConfig config) {
    std::lock_guard lock(metricsMutex_);
    config_ = std::move(config);
    spdlog::info("[VSRCircuitBreaker] Configuration updated");
}

VSRCircuitBreakerConfig VSRCircuitBreaker::config() const {
    std::lock_guard lock(metricsMutex_);
    return config_;
}

void VSRCircuitBreaker::setEventBus(EventBus* eventBus) {
    eventBus_ = eventBus;
}

// ----------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------

bool VSRCircuitBreaker::tryTransition(VSRBreakerState expected,
                                      VSRBreakerState target) {
    auto prev = expected;
    return state_.compare_exchange_strong(prev, target,
                                          std::memory_order_acq_rel);
}

void VSRCircuitBreaker::emitTransitionEvent(VSRBreakerState from,
                                            VSRBreakerState to,
                                            const std::string& reason) {
    if (!eventBus_) return;

    Event evt;
    evt.type = EventType::StateChanged;
    evt.timestamp = 0.0;

    // Use ErrorPayload for circuit breaker events (no VSR-specific event type)
    // This ensures the event is dispatched via the existing EventBus infrastructure
    evt.payload = ErrorPayload{
        PlayerError::None,
        "[VSRBreaker] " + std::string(stateToString(from)) + " -> "
        + std::string(stateToString(to)) + ": " + reason};

    eventBus_->publish(evt);
}

} // namespace hlplayer
