#include <hlplayer/PerformanceMonitor.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace hlplayer {

PerformanceMonitor::PerformanceMonitor()
    : startTimestamp_(std::chrono::steady_clock::now())
    , lastSummaryTimestamp_(startTimestamp_) {
}

PerformanceMonitor::~PerformanceMonitor() {
    stop();
}

void PerformanceMonitor::setEventBus(EventBus* eventBus) {
    std::lock_guard<std::mutex> lock(mutex_);
    eventBus_ = eventBus;
}

void PerformanceMonitor::setVRAMBudgetManager(std::shared_ptr<IVRAMBudgetManager> vramManager) {
    std::lock_guard<std::mutex> lock(mutex_);
    vramManager_ = vramManager;
}

void PerformanceMonitor::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    startTimestamp_ = std::chrono::steady_clock::now();
    lastSummaryTimestamp_ = startTimestamp_;

    summaryThread_ = std::thread(&PerformanceMonitor::periodicSummaryThread, this);
}

void PerformanceMonitor::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (summaryThread_.joinable()) {
        summaryThread_.join();
    }
}

void PerformanceMonitor::reset() {
    framesDropped_.store(0, std::memory_order_relaxed);
    framesProcessed_.store(0, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    latencyWindow_.clear();

    std::lock_guard<std::mutex> queueLock(queuesMutex_);
    for (auto& [name, data] : queues_) {
        data->currentDepth.store(0, std::memory_order_relaxed);
        data->droppedFrames.store(0, std::memory_order_relaxed);
    }

    startTimestamp_ = std::chrono::steady_clock::now();
    lastSummaryTimestamp_ = startTimestamp_;
}

void PerformanceMonitor::recordInferenceTime(double ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    latencyWindow_.add(ms);
}

void PerformanceMonitor::recordFrameDrop() {
    framesDropped_.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMonitor::recordFrameProcessed() {
    framesProcessed_.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceMonitor::recordQueueDepth(const std::string& stageName, uint32_t depth, uint32_t capacity) {
    std::lock_guard<std::mutex> lock(queuesMutex_);

    auto it = queues_.find(stageName);
    if (it == queues_.end()) {
        queues_[stageName] = std::make_unique<QueueData>();
        queues_[stageName]->capacity = capacity;
    }

    queues_[stageName]->currentDepth.store(depth, std::memory_order_relaxed);
}

void PerformanceMonitor::recordQueueDrop(const std::string& stageName) {
    std::lock_guard<std::mutex> lock(queuesMutex_);

    auto it = queues_.find(stageName);
    if (it == queues_.end()) {
        queues_[stageName] = std::make_unique<QueueData>();
    }

    queues_[stageName]->droppedFrames.fetch_add(1, std::memory_order_relaxed);
}

PerformanceMetricsSnapshot PerformanceMonitor::snapshot() const {
    PerformanceMetricsSnapshot snapshot;

    snapshot.totalFramesProcessed = framesProcessed_.load(std::memory_order_relaxed);
    snapshot.totalFramesDropped = framesDropped_.load(std::memory_order_relaxed);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - startTimestamp_).count();

    if (elapsed > 0.0) {
        snapshot.averageThroughputFps = static_cast<double>(snapshot.totalFramesProcessed) / elapsed;

        auto recentElapsed = std::chrono::duration<double>(now - lastSummaryTimestamp_).count();
        if (recentElapsed > 0.0) {
            uint64_t recentFrames = framesProcessed_.load(std::memory_order_relaxed);
            snapshot.currentThroughputFps = static_cast<double>(recentFrames) / recentElapsed;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.inference.minMs = latencyWindow_.min();
    snapshot.inference.maxMs = latencyWindow_.max();
    snapshot.inference.avgMs = latencyWindow_.avg();
    snapshot.inference.p95Ms = latencyWindow_.p95();
    snapshot.inference.sampleCount = latencyWindow_.count();

    if (vramManager_) {
        snapshot.vramUsedBytes = static_cast<double>(vramManager_->usedBytes());
        snapshot.vramAvailableBytes = static_cast<double>(vramManager_->availableBytes());
        snapshot.vramNearLimit = vramManager_->isNearLimit();
    }

    std::lock_guard<std::mutex> queueLock(queuesMutex_);
    for (const auto& [name, data] : queues_) {
        PerformanceMetricsSnapshot::QueueStats queueStats;
        queueStats.stageName = name;
        queueStats.currentDepth = data->currentDepth.load(std::memory_order_relaxed);
        queueStats.capacity = data->capacity;
        queueStats.utilizationPercent = (data->capacity > 0)
            ? (100.0 * queueStats.currentDepth / data->capacity)
            : 0.0;
        queueStats.droppedFrames = data->droppedFrames.load(std::memory_order_relaxed);
        snapshot.queueStats.push_back(queueStats);
    }

    snapshot.timestampSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();

    return snapshot;
}

void PerformanceMonitor::periodicSummaryThread() {
    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(
            static_cast<int64_t>(kSummaryIntervalSeconds * 1000.0)));

        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        auto metrics = snapshot();
        publishMetricSummary(metrics);
        lastSummaryTimestamp_ = std::chrono::steady_clock::now();
    }
}

void PerformanceMonitor::publishMetricSummary(const PerformanceMetricsSnapshot& metrics) {
    spdlog::info("=== Performance Monitor Summary ===");
    spdlog::info("Total Frames: processed={}, dropped={}", metrics.totalFramesProcessed, metrics.totalFramesDropped);
    spdlog::info("Throughput: current={:.2f} fps, avg={:.2f} fps", metrics.currentThroughputFps, metrics.averageThroughputFps);

    if (metrics.inference.sampleCount > 0) {
        spdlog::info("Inference: min={:.2f}ms, max={:.2f}ms, avg={:.2f}ms, p95={:.2f}ms (samples={})",
                     metrics.inference.minMs, metrics.inference.maxMs,
                     metrics.inference.avgMs, metrics.inference.p95Ms,
                     metrics.inference.sampleCount);
    }

    spdlog::info("VRAM: used={:.2f} MB, available={:.2f} MB, nearLimit={}",
                 metrics.vramUsedBytes / (1024.0 * 1024.0),
                 metrics.vramAvailableBytes / (1024.0 * 1024.0),
                 metrics.vramNearLimit);

    for (const auto& queue : metrics.queueStats) {
        spdlog::info("Queue '{}': depth={}/{}, util={:.1f}%, dropped={}",
                     queue.stageName, queue.currentDepth, queue.capacity,
                     queue.utilizationPercent, queue.droppedFrames);
    }

    if (eventBus_) {
        Event event;
        event.type = EventType::LatencyMeasured;
        event.timestamp = metrics.timestampSeconds;
        event.payload = LatencyPayload{metrics.inference.avgMs};
        eventBus_->publish(event);
    }
}

void PerformanceMonitor::LatencyWindow::add(double ms) {
    samples.push_back(ms);
    if (samples.size() > kWindowSamples) {
        samples.pop_front();
    }
}

void PerformanceMonitor::LatencyWindow::clear() {
    samples.clear();
}

double PerformanceMonitor::LatencyWindow::min() const {
    if (samples.empty()) return 0.0;
    return *std::min_element(samples.begin(), samples.end());
}

double PerformanceMonitor::LatencyWindow::max() const {
    if (samples.empty()) return 0.0;
    return *std::max_element(samples.begin(), samples.end());
}

double PerformanceMonitor::LatencyWindow::avg() const {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (double sample : samples) {
        sum += sample;
    }
    return sum / samples.size();
}

double PerformanceMonitor::LatencyWindow::p95() const {
    if (samples.empty()) return 0.0;

    std::vector<double> sorted(samples.begin(), samples.end());
    std::sort(sorted.begin(), sorted.end());

    size_t index = static_cast<size_t>(sorted.size() * 0.95);
    if (index >= sorted.size()) index = sorted.size() - 1;

    return sorted[index];
}

uint64_t PerformanceMonitor::LatencyWindow::count() const {
    return samples.size();
}

} // namespace hlplayer
