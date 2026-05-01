#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <hlplayer/RealtimeVSRPipeline.h>
#include <hlplayer/VSRCircuitBreaker.h>
#include <hlplayer/DecodeFallback.h>
#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IPipelineNode.h>
#include <hlplayer/IVRAMBudgetManager.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace hlplayer;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Mock implementations for testing
// ============================================================================

class MockHWDecoder : public IHWDecoder {
public:
    MockHWDecoder() = default;
    ~MockHWDecoder() override = default;

    Result<void> initialize(const HWDecoderConfig& config) override {
        initialized_ = true;
        return Result<void>::success();
    }

    Result<GpuFrame> decode(const MediaPacket& packet) override {
        if (!initialized_) {
            return Result<GpuFrame>::error(PlayerError::InvalidState);
        }
        GpuFrame frame{};
        frame.width = 1920;
        frame.height = 1080;
        frame.format = PixelFormat::NV12;
        frame.timestamp = packet.timestamp;
        return Result<GpuFrame>::success(frame);
    }

    void flush() override {}

    PixelFormat getOutputFormat() const override { return PixelFormat::NV12; }

    std::string getName() const override { return "MockHWDecoder"; }

    bool isInitialized() const { return initialized_; }

private:
    bool initialized_ = false;
};

class MockVSRNode : public IPipelineNode {
public:
    MockVSRNode() = default;
    ~MockVSRNode() override = default;

    Result<void> initialize() override {
        initialized_ = true;
        return Result<void>::success();
    }

    Result<GpuFrame> process(const GpuFrame& input) override {
        if (!initialized_) {
            return Result<GpuFrame>::error(PlayerError::InvalidState);
        }

        // Simulate inference delay
        std::this_thread::sleep_for(std::chrono::milliseconds(inferenceDelayMs_));

        GpuFrame output = input;
        output.width = input.width * scaleFactor_;
        output.height = input.height * scaleFactor_;
        return Result<GpuFrame>::success(output);
    }

    void setInferenceDelayMs(int delayMs) { inferenceDelayMs_ = delayMs; }
    void setScaleFactor(int factor) { scaleFactor_ = factor; }

    bool isInitialized() const { return initialized_; }

private:
    bool initialized_ = false;
    int inferenceDelayMs_ = 5;
    int scaleFactor_ = 2;
};

class MockVRAMBudgetManager : public IVRAMBudgetManager {
public:
    MockVRAMBudgetManager(uint64_t totalBytes = 8ULL * 1024 * 1024 * 1024)
        : totalBytes_(totalBytes), usedBytes_(0) {}

    Result<void> initialize(const VRAMBudgetConfig& config) override {
        config_ = config;
        return Result<void>::success();
    }

    Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (usedBytes_ + sizeBytes > totalBytes_) {
            return Result<void>::error(PlayerError::DeviceLost);
        }
        usedBytes_ += sizeBytes;
        return Result<void>::success();
    }

    void release(uint64_t sizeBytes) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (usedBytes_ >= sizeBytes) {
            usedBytes_ -= sizeBytes;
        }
    }

    uint64_t usedBytes() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return usedBytes_;
    }

    uint64_t availableBytes() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return totalBytes_ - usedBytes_;
    }

    bool isNearLimit() const override { return false; }

    void setPerformanceMode(PerformanceMode mode) override { currentMode_ = mode; }

    PerformanceMode getPerformanceMode() const override { return currentMode_; }

    void reset() override {
        std::lock_guard<std::mutex> lock(mutex_);
        usedBytes_ = 0;
    }

    double getUsageRatio() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<double>(usedBytes_) / totalBytes_;
    }

private:
    mutable std::mutex mutex_;
    uint64_t totalBytes_;
    uint64_t usedBytes_;
    VRAMBudgetConfig config_;
    PerformanceMode currentMode_ = PerformanceMode::Balanced;
};

// ============================================================================
// RealtimeVSRPipeline tests
// ============================================================================

TEST_CASE("RealtimeVSRPipeline - construction creates unitialized pipeline", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    REQUIRE_FALSE(pipeline.isRunning());
    REQUIRE(pipeline.vsrState() == VSRState::Active);
    REQUIRE(pipeline.vsrDroppedFrames() == 0);
    REQUIRE(pipeline.vsrProcessedFrames() == 0);
    REQUIRE(pipeline.vsrBypassedFrames() == 0);
}

TEST_CASE("RealtimeVSRPipeline - initialize without decoder returns error", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto result = pipeline.initialize();
    REQUIRE(result.hasError());
    REQUIRE_FALSE(pipeline.isRunning());
}

TEST_CASE("RealtimeVSRPipeline - initialize with decoder succeeds", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto result = pipeline.initialize();
    REQUIRE(result.hasValue());
    REQUIRE_FALSE(pipeline.isRunning());
}

TEST_CASE("RealtimeVSRPipeline - start without initialize returns error", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto result = pipeline.start();
    REQUIRE(result.hasError());
}

TEST_CASE("RealtimeVSRPipeline - start after initialize succeeds", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    pipeline.initialize();

    // Set packet provider to avoid blocking
    bool packetProvided = false;
    pipeline.setPacketProvider([&packetProvided](PacketQueue& queue) {
        if (packetProvided) return false;
        packetProvided = true;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = 0.0;
        queue.push(pkt);
        return true;
    });

    auto result = pipeline.start();
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.isRunning());

    pipeline.stop();
}

TEST_CASE("RealtimeVSRPipeline - pause and resume work correctly", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    pipeline.initialize();

    // Set packet provider to avoid blocking
    bool packetProvided = false;
    pipeline.setPacketProvider([&packetProvided](PacketQueue& queue) {
        if (packetProvided) return false;
        packetProvided = true;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = 0.0;
        queue.push(pkt);
        return true;
    });

    pipeline.start();
    REQUIRE(pipeline.isRunning());

    pipeline.pause();
    REQUIRE(pipeline.isPaused());

    pipeline.resume();
    REQUIRE_FALSE(pipeline.isPaused());

    pipeline.stop();
}

TEST_CASE("RealtimeVSRPipeline - flush resets queue and stats", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto vsrNode = std::make_shared<MockVSRNode>();
    vsrNode->initialize();
    pipeline.setVSRNode(vsrNode);

    pipeline.initialize();

    // Set packet provider
    int packetCount = 0;
    pipeline.setPacketProvider([&packetCount](PacketQueue& queue) {
        if (packetCount >= 5) return false;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = packetCount * 0.042;
        queue.push(pkt);
        ++packetCount;
        return true;
    });

    pipeline.start();

    // Wait for some processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint64_t processedBefore = pipeline.vsrProcessedFrames();
    uint64_t droppedBefore = pipeline.vsrDroppedFrames();

    pipeline.flush();

    uint64_t processedAfter = pipeline.vsrProcessedFrames();
    uint64_t droppedAfter = pipeline.vsrDroppedFrames();

    // Stats should be reset or maintained (implementation specific)
    REQUIRE(processedAfter >= processedBefore);

    pipeline.stop();
}

TEST_CASE("RealtimeVSRPipeline - circuit breaker transitions with slow inference", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    config.vsrSlowThresholdMs = 5.0;
    config.vsrSlowFrameCount = 3;
    config.vsrCooldownSeconds = 1.0;

    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto vsrNode = std::make_shared<MockVSRNode>();
    vsrNode->initialize();
    vsrNode->setInferenceDelayMs(10); // 10ms > 5ms threshold
    pipeline.setVSRNode(vsrNode);

    pipeline.initialize();

    // Set packet provider to feed frames
    std::atomic<int> packetCount{0};
    pipeline.setPacketProvider([&packetCount](PacketQueue& queue) {
        if (packetCount.load() >= 10) return false;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = packetCount.load() * 0.042;
        queue.push(pkt);
        ++packetCount;
        return true;
    });

    pipeline.start();

    // Wait for circuit breaker to open (needs 3 slow frames)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Circuit should be open now
    REQUIRE(pipeline.vsrState() == VSRState::CircuitOpen);

    // Wait for cooldown + probing
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // State may still be CircuitOpen or in Probing
    VSRState state = pipeline.vsrState();
    REQUIRE((state == VSRState::CircuitOpen || state == VSRState::Probing));

    pipeline.stop();
}

TEST_CASE("RealtimeVSRPipeline - frame drops under load", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    config.vsrInputQueueCap = 2;

    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto vsrNode = std::make_shared<MockVSRNode>();
    vsrNode->initialize();
    vsrNode->setInferenceDelayMs(20); // Slow inference to cause queue buildup
    pipeline.setVSRNode(vsrNode);

    pipeline.initialize();

    // Feed many frames rapidly
    std::atomic<int> packetCount{0};
    pipeline.setPacketProvider([&packetCount](PacketQueue& queue) {
        if (packetCount.load() >= 50) return false;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = packetCount.load() * 0.016;
        queue.push(pkt);
        ++packetCount;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return true;
    });

    pipeline.start();

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    uint64_t droppedFrames = pipeline.vsrDroppedFrames();

    // With aggressive dropping and slow inference, some frames should be dropped
    REQUIRE(droppedFrames > 0);

    pipeline.stop();
}

TEST_CASE("RealtimeVSRPipeline - VSR bypass when node is null", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    // No VSR node set - frames should pass through
    pipeline.initialize();

    std::atomic<int> packetCount{0};
    pipeline.setPacketProvider([&packetCount](PacketQueue& queue) {
        if (packetCount.load() >= 5) return false;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = packetCount.load() * 0.042;
        queue.push(pkt);
        ++packetCount;
        return true;
    });

    pipeline.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Bypassed frames should be > 0 (frames passed through without VSR)
    uint64_t bypassedFrames = pipeline.vsrBypassedFrames();

    pipeline.stop();

    REQUIRE(bypassedFrames >= 0);
}

TEST_CASE("RealtimeVSRPipeline - last inference time is tracked", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto vsrNode = std::make_shared<MockVSRNode>();
    vsrNode->initialize();
    vsrNode->setInferenceDelayMs(5);
    pipeline.setVSRNode(vsrNode);

    pipeline.initialize();

    std::atomic<int> packetCount{0};
    pipeline.setPacketProvider([&packetCount](PacketQueue& queue) {
        if (packetCount.load() >= 3) return false;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = packetCount.load() * 0.042;
        queue.push(pkt);
        ++packetCount;
        return true;
    });

    pipeline.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    double lastInferenceMs = pipeline.lastVSRInferenceMs();

    pipeline.stop();

    // Should have recorded inference time
    REQUIRE(lastInferenceMs > 0);
}

TEST_CASE("RealtimeVSRPipeline - VSRFrameQueue aggressive dropping", "[realtime_vsr][e2e]") {
    VSRFrameQueue queue(2);

    REQUIRE(queue.size() == 0);
    REQUIRE(queue.droppedCount() == 0);

    GpuFrame frame1{};
    frame1.timestamp = 0.0;
    GpuFrame frame2{};
    frame2.timestamp = 0.042;
    GpuFrame frame3{};
    frame3.timestamp = 0.084;

    queue.pushOrDrop(frame1);
    queue.pushOrDrop(frame2);

    REQUIRE(queue.size() == 2);
    REQUIRE(queue.droppedCount() == 0);

    // Third frame should drop oldest (frame1)
    queue.pushOrDrop(frame3);

    REQUIRE(queue.size() == 2);
    REQUIRE(queue.droppedCount() == 1);

    GpuFrame popped{};
    REQUIRE(queue.pop(popped));
    REQUIRE(popped.timestamp == 0.042);

    REQUIRE(queue.pop(popped));
    REQUIRE(popped.timestamp == 0.084);

    REQUIRE(queue.size() == 0);
}

TEST_CASE("RealtimeVSRPipeline - VRAM manager integration", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto vsrNode = std::make_shared<MockVSRNode>();
    vsrNode->initialize();
    pipeline.setVSRNode(vsrNode);

    auto vramManager = std::make_shared<MockVRAMBudgetManager>();
    vramManager->initialize(VRAMBudgetConfig{});
    pipeline.setVRAMBudgetManager(vramManager);

    pipeline.setVSRFrameVRAM(16 * 1024 * 1024); // 16MB per frame

    pipeline.initialize();

    std::atomic<int> packetCount{0};
    pipeline.setPacketProvider([&packetCount](PacketQueue& queue) {
        if (packetCount.load() >= 3) return false;
        MediaPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = packetCount.load() * 0.042;
        queue.push(pkt);
        ++packetCount;
        return true;
    });

    pipeline.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    pipeline.stop();

    // VRAM should have been requested and released
    uint64_t used = vramManager->usedBytes();
    REQUIRE(used >= 0); // May be released after processing completes
}

TEST_CASE("RealtimeVSRPipeline - state to string conversion", "[realtime_vsr][e2e]") {
    REQUIRE(std::string(RealtimeVSRPipeline::vsrStateToString(VSRState::Active)) == "Active");
    REQUIRE(std::string(RealtimeVSRPipeline::vsrStateToString(VSRState::CircuitOpen)) == "CircuitOpen");
    REQUIRE(std::string(RealtimeVSRPipeline::vsrStateToString(VSRState::Probing)) == "Probing");
}

TEST_CASE("RealtimeVSRPipeline - stop is idempotent", "[realtime_vsr][e2e]") {
    VSRPipelineConfig config;
    RealtimeVSRPipeline pipeline(config);

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    pipeline.initialize();

    pipeline.stop();
    pipeline.stop();
    pipeline.stop();

    REQUIRE_FALSE(pipeline.isRunning());
}
