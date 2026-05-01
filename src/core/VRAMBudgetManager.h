#ifndef HLPLAYER_VRAMBUDGETMANAGER_H
#define HLPLAYER_VRAMBUDGETMANAGER_H

#include <hlplayer/IVRAMBudgetManager.h>
#include <hlplayer/Export.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#ifdef _WIN32
#include <wrl/client.h>
#include <dxgi1_6.h>
#endif

namespace hlplayer {

/**
 * @brief Concrete implementation of VRAM budget manager
 *
 * This class provides thread-safe VRAM budget tracking and allocation control.
 * It queries actual GPU VRAM via DXGI on Windows and enforces budget limits
 * based on performance mode configuration.
 *
 * Thread-safety: All methods are thread-safe
 */
class HLPLAYER_CORE_API VRAMBudgetManager : public IVRAMBudgetManager {
public:
    VRAMBudgetManager();
    ~VRAMBudgetManager() override;

    // IVRAMBudgetManager implementation
    Result<void> initialize(const VRAMBudgetConfig& config) override;
    Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs = 0) override;
    void release(uint64_t sizeBytes) override;
    uint64_t usedBytes() const override;
    uint64_t availableBytes() const override;
    bool isNearLimit() const override;
    void setPerformanceMode(PerformanceMode mode) override;
    PerformanceMode getPerformanceMode() const override;
    void reset() override;

private:
    Result<uint64_t> queryTotalVRAM();

    bool canAllocate(uint64_t sizeBytes, uint64_t& available);

    void updateBudget();

    void logUsageWarnings() const;

#ifdef _WIN32
    Result<void> initializeDXGI();

    Result<uint64_t> queryVRAMViaDXGI();

    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
#endif

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    // Lock-free reads via std::atomic
    std::atomic<uint64_t> usedBytes_;
    std::atomic<uint64_t> budgetBytes_;

    VRAMBudgetConfig config_;
    PerformanceMode currentMode_;

    // Queried once at initialization
    uint64_t totalVRAM_;

    static constexpr double PERFORMANCE_MODE_RATIO = 0.9;
    static constexpr double BALANCED_MODE_RATIO = 0.6;

    bool initialized_;
    mutable bool wasAtWarning_;
    mutable bool wasAtDegradation_;
    mutable bool wasAtEmergency_;
};

} // namespace hlplayer

#endif // HLPLAYER_VRAMBUDGETMANAGER_H
