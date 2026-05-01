#ifndef HLPLAYER_IVRAMBUDGETMANAGER_H
#define HLPLAYER_IVRAMBUDGETMANAGER_H

#include <hlplayer/Result.h>
#include <hlplayer/Export.h>
#include <cstdint>

namespace hlplayer {

/// Performance mode determines VRAM budget allocation strategy
enum class PerformanceMode : uint32_t {
    /// Aggressive mode: Use 90% of available VRAM for performance
    Performance = 0,
    /// Balanced mode: Use 60% of available VRAM for stability
    Balanced = 1
};

/// Configuration for VRAM budget management
struct VRAMBudgetConfig {
    /// Total VRAM budget in bytes
    uint64_t totalBudgetBytes = 0;
    /// Performance mode for budget allocation
    PerformanceMode performanceMode = PerformanceMode::Balanced;
    /// Warning threshold (default: 70% of budget)
    double warningThreshold = 0.7;
    /// Degradation threshold (default: 85% of budget)
    double degradeThreshold = 0.85;
    /// Emergency threshold (default: 95% of budget)
    double emergencyThreshold = 0.95;
};

/**
 * @brief Pure virtual interface for VRAM budget management
 *
 * This interface provides thread-safe VRAM budget tracking and allocation control.
 * It is intended to be held by std::shared_ptr and shared across multiple threads.
 *
 * Thread-safety: All methods must be thread-safe, can be called from any thread
 * Ownership: Held by std::shared_ptr, shared across multiple threads
 */
class HLPLAYER_CORE_API IVRAMBudgetManager {
public:
    virtual ~IVRAMBudgetManager() = default;

    /// Initialize the budget manager with given configuration
    virtual Result<void> initialize(const VRAMBudgetConfig& config) = 0;

    /// Request allocation of specified bytes with optional timeout
    virtual Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs = 0) = 0;

    /// Release previously allocated bytes
    virtual void release(uint64_t sizeBytes) = 0;

    /// Get currently used bytes
    virtual uint64_t usedBytes() const = 0;

    /// Get currently available bytes
    virtual uint64_t availableBytes() const = 0;

    /// Check if VRAM usage is near limit (exceeds warning threshold)
    virtual bool isNearLimit() const = 0;

    /// Set the performance mode
    virtual void setPerformanceMode(PerformanceMode mode) = 0;

    /// Get the current performance mode
    virtual PerformanceMode getPerformanceMode() const = 0;

    /// Reset all allocations and clear budget usage
    virtual void reset() = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IVRAMBUDGETMANAGER_H
