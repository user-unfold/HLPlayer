#ifndef HLPLAYER_QUEUEWATERMARKS_H
#define HLPLAYER_QUEUEWATERMARKS_H

#include <cstdint>

namespace hlplayer {

// ============================================================================
// Queue Capacity Constants
// ============================================================================

/// Maximum capacity of VSR input queue (aggressive frame dropping)
constexpr uint32_t kVSRInputQueueCap = 2;

/// Maximum capacity of VSR output queue
constexpr uint32_t kVSROutputQueueCap = 4;

/// Maximum capacity of decode queue
constexpr uint32_t kDecodeQueueCap = 8;

// ============================================================================
// VSR Circuit Breaker Thresholds
// ============================================================================

/// VSR inference time threshold in milliseconds (circuit breaker opens if 3 consecutive frames exceed this)
constexpr double kVSRInferenceThresholdMs = 16.0;

/// Circuit breaker cooldown period in seconds before probing VSR again
constexpr double kVSRCooldownSec = 30.0;

// ============================================================================
// Frame Drop Thresholds
// ============================================================================

/// Frame drop threshold in seconds (sync drift that triggers frame dropping)
constexpr double kFrameDropThresholdSec = 0.1;

// ============================================================================
// VRAM Performance Mode Thresholds
// ============================================================================

/// VRAM usage percentage for Performance mode (aggressive, use 90% of available VRAM)
constexpr uint32_t kVRAMPerformancePercent = 90;

/// VRAM usage percentage for Balanced mode (stable, use 60% of available VRAM)
constexpr uint32_t kVRAMBalancedPercent = 60;

} // namespace hlplayer

#endif // HLPLAYER_QUEUEWATERMARKS_H
