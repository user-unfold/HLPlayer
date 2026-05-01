#ifndef HLPLAYER_PERFORMANCEMONITOR_H
#define HLPLAYER_PERFORMANCEMONITOR_H

#include <hlplayer/Export.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/IVRAMBudgetManager.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hlplayer {

class IVRAMBudgetManager;

struct PerformanceMetricsSnapshot {
    uint64_t totalFramesProcessed = 0;
    uint64_t totalFramesDropped = 0;
    double currentThroughputFps = 0.0;
    double averageThroughputFps = 0.0;

    double vramUsedBytes = 0.0;
    double vramAvailableBytes = 0.0;
    bool vramNearLimit = false;

    struct InferenceStats {
        double minMs = 0.0;
        double maxMs = 0.0;
        double avgMs = 0.0;
        double p95Ms = 0.0;
        uint64_t sampleCount = 0;
    } inference;

    struct QueueStats {
        std::string stageName;
        uint32_t currentDepth = 0;
        uint32_t capacity = 0;
        double utilizationPercent = 0.0;
        uint64_t droppedFrames = 0;
    };

    std::vector<QueueStats> queueStats;

    double timestampSeconds = 0.0;
};

class HLPLAYER_CORE_API PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor();

    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    void setEventBus(EventBus* eventBus);
    void setVRAMBudgetManager(std::shared_ptr<IVRAMBudgetManager> vramManager);

    void start();
    void stop();
    void reset();

    void recordInferenceTime(double ms);
    void recordFrameDrop();
    void recordFrameProcessed();
    void recordQueueDepth(const std::string& stageName, uint32_t depth, uint32_t capacity);
    void recordQueueDrop(const std::string& stageName);

    PerformanceMetricsSnapshot snapshot() const;

private:
    struct LatencyWindow {
        std::deque<double> samples;
        static constexpr size_t kWindowSamples = 1000;

        void add(double ms);
        void clear();

        double min() const;
        double max() const;
        double avg() const;
        double p95() const;
        uint64_t count() const;
    };

    void periodicSummaryThread();
    void publishMetricSummary(const PerformanceMetricsSnapshot& metrics);

    mutable std::mutex mutex_;

    EventBus* eventBus_ = nullptr;
    std::shared_ptr<IVRAMBudgetManager> vramManager_;

    LatencyWindow latencyWindow_;
    std::atomic<uint64_t> framesDropped_{0};
    std::atomic<uint64_t> framesProcessed_{0};

    struct QueueData {
        std::atomic<uint32_t> currentDepth{0};
        uint32_t capacity = 0;
        std::atomic<uint64_t> droppedFrames{0};
    };

    std::map<std::string, std::unique_ptr<QueueData>> queues_;
    mutable std::mutex queuesMutex_;

    std::atomic<bool> running_{false};
    std::thread summaryThread_;
    static constexpr double kSummaryIntervalSeconds = 60.0;

    std::chrono::steady_clock::time_point startTimestamp_;
    std::chrono::steady_clock::time_point lastSummaryTimestamp_;
};

} // namespace hlplayer

#endif // HLPLAYER_PERFORMANCEMONITOR_H
