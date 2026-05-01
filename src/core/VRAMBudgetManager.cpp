#include "VRAMBudgetManager.h"

#include <spdlog/spdlog.h>

namespace hlplayer {

VRAMBudgetManager::VRAMBudgetManager()
    : usedBytes_(0)
    , budgetBytes_(0)
    , currentMode_(PerformanceMode::Balanced)
    , totalVRAM_(0)
    , initialized_(false)
    , wasAtWarning_(false)
    , wasAtDegradation_(false)
    , wasAtEmergency_(false) {
}

VRAMBudgetManager::~VRAMBudgetManager() = default;

Result<void> VRAMBudgetManager::initialize(const VRAMBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        spdlog::warn("VRAMBudgetManager already initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_ = config;
    currentMode_ = config.performanceMode;

#ifdef _WIN32
    auto dxgiResult = initializeDXGI();
    if (!dxgiResult.hasValue()) {
        spdlog::warn("Failed to initialize DXGI, will use estimated VRAM");
        totalVRAM_ = 4ULL * 1024 * 1024 * 1024; // 4GB fallback
    } else {
        auto vramResult = queryVRAMViaDXGI();
        if (vramResult.hasValue()) {
            totalVRAM_ = vramResult.value();
        } else {
            totalVRAM_ = 4ULL * 1024 * 1024 * 1024; // 4GB fallback
        }
    }
#else
    totalVRAM_ = 4ULL * 1024 * 1024 * 1024; // 4GB fallback for non-Windows
#endif

    if (config_.totalBudgetBytes > 0) {
        totalVRAM_ = config_.totalBudgetBytes;
    }

    updateBudget();
    initialized_ = true;

    spdlog::info("VRAMBudgetManager initialized: total={} MB, budget={} MB, mode={}",
                 totalVRAM_ / (1024 * 1024), budgetBytes_.load() / (1024 * 1024),
                 currentMode_ == PerformanceMode::Performance ? "Performance" : "Balanced");

    return Result<void>::success();
}

Result<void> VRAMBudgetManager::requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs) {
    if (!initialized_) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (sizeBytes == 0) {
        return Result<void>::success();
    }

    std::unique_lock<std::mutex> lock(mutex_);

    uint64_t available;
    if (!canAllocate(sizeBytes, available)) {
        logUsageWarnings();

        if (timeoutMs == 0) {
            return Result<void>::error(PlayerError::Timeout);
        }

        // Wait for space to become available
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!canAllocate(sizeBytes, available)) {
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                spdlog::warn("VRAM allocation timeout: requested={} MB, available={} MB",
                             sizeBytes / (1024 * 1024), available / (1024 * 1024));
                return Result<void>::error(PlayerError::Timeout);
            }
        }
    }

    usedBytes_.fetch_add(sizeBytes, std::memory_order_release);
    logUsageWarnings();

    return Result<void>::success();
}

void VRAMBudgetManager::release(uint64_t sizeBytes) {
    if (!initialized_ || sizeBytes == 0) {
        return;
    }

    usedBytes_.fetch_sub(sizeBytes, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_all();
    }
}

uint64_t VRAMBudgetManager::usedBytes() const {
    return usedBytes_.load(std::memory_order_acquire);
}

uint64_t VRAMBudgetManager::availableBytes() const {
    uint64_t budget = budgetBytes_.load(std::memory_order_acquire);
    uint64_t used = usedBytes_.load(std::memory_order_acquire);

    if (budget <= used) {
        return 0;
    }

    return budget - used;
}

bool VRAMBudgetManager::isNearLimit() const {
    uint64_t budget = budgetBytes_.load(std::memory_order_acquire);
    uint64_t used = usedBytes_.load(std::memory_order_acquire);
    double ratio = static_cast<double>(used) / static_cast<double>(budget);
    return ratio > config_.warningThreshold;
}

void VRAMBudgetManager::setPerformanceMode(PerformanceMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (currentMode_ == mode) {
        return;
    }

    currentMode_ = mode;
    updateBudget();

    spdlog::info("Performance mode changed to {}: budget recalculated to {} MB",
                 mode == PerformanceMode::Performance ? "Performance" : "Balanced",
                 budgetBytes_.load() / (1024 * 1024));

    cv_.notify_all();
}

PerformanceMode VRAMBudgetManager::getPerformanceMode() const {
    return currentMode_;
}

void VRAMBudgetManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    usedBytes_.store(0, std::memory_order_release);
    wasAtWarning_ = false;
    wasAtDegradation_ = false;
    wasAtEmergency_ = false;

    cv_.notify_all();

    spdlog::info("VRAMBudgetManager reset: all allocations cleared");
}

Result<uint64_t> VRAMBudgetManager::queryTotalVRAM() {
#ifdef _WIN32
    return queryVRAMViaDXGI();
#else
    return Result<uint64_t>::error(PlayerError::Unknown);
#endif
}

bool VRAMBudgetManager::canAllocate(uint64_t sizeBytes, uint64_t& available) {
    uint64_t budget = budgetBytes_.load(std::memory_order_acquire);
    uint64_t used = usedBytes_.load(std::memory_order_acquire);

    available = (budget > used) ? (budget - used) : 0;
    return sizeBytes <= available;
}

void VRAMBudgetManager::updateBudget() {
    double ratio = (currentMode_ == PerformanceMode::Performance)
                   ? PERFORMANCE_MODE_RATIO : BALANCED_MODE_RATIO;

    budgetBytes_.store(static_cast<uint64_t>(static_cast<double>(totalVRAM_) * ratio),
                       std::memory_order_release);
}

void VRAMBudgetManager::logUsageWarnings() const {
    uint64_t budget = budgetBytes_.load(std::memory_order_acquire);
    uint64_t used = usedBytes_.load(std::memory_order_acquire);

    if (budget == 0) {
        return;
    }

    double ratio = static_cast<double>(used) / static_cast<double>(budget);

    bool atWarning = ratio > config_.warningThreshold;
    bool atDegradation = ratio > config_.degradeThreshold;
    bool atEmergency = ratio > config_.emergencyThreshold;

    // Log threshold warnings only once per crossing
    if (atWarning && !wasAtWarning_) {
        spdlog::warn("VRAM usage at warning level: {:.1f}% ({} MB / {} MB)",
                     ratio * 100.0, used / (1024 * 1024), budget / (1024 * 1024));
        wasAtWarning_ = true;
    } else if (!atWarning && wasAtWarning_) {
        wasAtWarning_ = false;
    }

    if (atDegradation && !wasAtDegradation_) {
        spdlog::warn("VRAM usage at degradation level: {:.1f}% ({} MB / {} MB) - Consider performance reduction",
                     ratio * 100.0, used / (1024 * 1024), budget / (1024 * 1024));
        wasAtDegradation_ = true;
    } else if (!atDegradation && wasAtDegradation_) {
        wasAtDegradation_ = false;
    }

    if (atEmergency && !wasAtEmergency_) {
        spdlog::error("VRAM usage at emergency level: {:.1f}% ({} MB / {} MB) - Immediate action required",
                      ratio * 100.0, used / (1024 * 1024), budget / (1024 * 1024));
        wasAtEmergency_ = true;
    } else if (!atEmergency && wasAtEmergency_) {
        wasAtEmergency_ = false;
    }
}

#ifdef _WIN32
Result<void> VRAMBudgetManager::initializeDXGI() {
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), nullptr);
    if (FAILED(hr)) {
        return Result<void>::error(PlayerError::Unknown);
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return Result<void>::error(PlayerError::Unknown);
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
    hr = factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                              IID_PPV_ARGS(&adapter));
    if (FAILED(hr)) {
        return Result<void>::error(PlayerError::Unknown);
    }

    adapter_ = adapter;
    return Result<void>::success();
}

Result<uint64_t> VRAMBudgetManager::queryVRAMViaDXGI() {
    if (!adapter_) {
        return Result<uint64_t>::error(PlayerError::Unknown);
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo = {};
    HRESULT hr = adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo);
    if (FAILED(hr)) {
        return Result<uint64_t>::error(PlayerError::Unknown);
    }

    return Result<uint64_t>::success(memoryInfo.Budget);
}
#endif

} // namespace hlplayer
