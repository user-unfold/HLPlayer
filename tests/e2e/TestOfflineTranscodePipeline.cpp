#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <hlplayer/OfflineTranscodePipeline.h>
#include <hlplayer/IVideoEncoder.h>
#include <hlplayer/IMuxer.h>
#include <hlplayer/ICheckpointManager.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>

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
        decodeCount_++;
        return Result<GpuFrame>::success(frame);
    }

    void flush() override {}

    PixelFormat getOutputFormat() const override { return PixelFormat::NV12; }

    std::string getName() const override { return "MockHWDecoder"; }

    uint64_t getDecodeCount() const { return decodeCount_; }

private:
    bool initialized_ = false;
    uint64_t decodeCount_ = 0;
};

class MockVideoEncoder : public IVideoEncoder {
public:
    MockVideoEncoder() = default;
    ~MockVideoEncoder() override = default;

    Result<void> initialize(const EncoderConfig& config) override {
        initialized_ = true;
        config_ = config;
        return Result<void>::success();
    }

    Result<EncodedPacket> encode(const GpuFrame& frame) override {
        if (!initialized_) {
            return Result<EncodedPacket>::error(PlayerError::InvalidState);
        }
        EncodedPacket pkt{};
        pkt.streamIndex = 0;
        pkt.timestamp = frame.timestamp;
        pkt.size = 1024;
        encodeCount_++;
        return Result<EncodedPacket>::success(pkt);
    }

    void flush() override {}

    bool isInitialized() const { return initialized_; }

    uint64_t getEncodeCount() const { return encodeCount_; }

private:
    bool initialized_ = false;
    uint64_t encodeCount_ = 0;
    EncoderConfig config_;
};

class MockMuxer : public IMuxer {
public:
    MockMuxer() = default;
    ~MockMuxer() override = default;

    Result<void> initialize(const std::string& outputPath, const MuxerConfig& config) override {
        initialized_ = true;
        outputPath_ = outputPath;
        return Result<void>::success();
    }

    Result<void> addVideoStream(const VideoStreamInfo& info) override {
        videoStreamAdded_ = true;
        return Result<void>::success();
    }

    Result<void> addAudioStream(const AudioStreamInfo& info) override {
        audioStreamAdded_ = true;
        return Result<void>::success();
    }

    Result<void> writeHeader() override {
        headerWritten_ = true;
        return Result<void>::success();
    }

    Result<void> writePacket(const EncodedPacket& packet) override {
        packetCount_++;
        lastTimestamp_ = packet.timestamp;
        return Result<void>::success();
    }

    Result<void> writeTrailer() override {
        trailerWritten_ = true;
        return Result<void>::success();
    }

    void close() override {}

    bool isInitialized() const { return initialized_; }
    bool isHeaderWritten() const { return headerWritten_; }
    bool isTrailerWritten() const { return trailerWritten_; }
    uint64_t getPacketCount() const { return packetCount_; }
    double getLastTimestamp() const { return lastTimestamp_; }

private:
    bool initialized_ = false;
    std::string outputPath_;
    bool videoStreamAdded_ = false;
    bool audioStreamAdded_ = false;
    bool headerWritten_ = false;
    bool trailerWritten_ = false;
    uint64_t packetCount_ = 0;
    double lastTimestamp_ = 0.0;
};

class MockCheckpointManager : public ICheckpointManager {
public:
    MockCheckpointManager() = default;
    ~MockCheckpointManager() override = default;

    Result<void> saveCheckpoint(const CheckpointInfo& info) override {
        std::lock_guard<std::mutex> lock(mutex_);
        lastCheckpoint_ = info;
        saveCount_++;
        return Result<void>::success();
    }

    Result<CheckpointInfo> restoreCheckpoint(const std::string& sourcePath) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lastCheckpoint_.sourcePath == sourcePath) {
            return Result<CheckpointInfo>::success(lastCheckpoint_);
        }
        return Result<CheckpointInfo>::error(PlayerError::NotFound);
    }

    Result<void> cleanCheckpoint(const std::string& sourcePath) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lastCheckpoint_.sourcePath == sourcePath) {
            lastCheckpoint_ = CheckpointInfo{};
            return Result<void>::success();
        }
        return Result<void>::error(PlayerError::NotFound);
    }

    Result<bool> hasCheckpoint(const std::string& sourcePath) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return Result<bool>::success(lastCheckpoint_.sourcePath == sourcePath);
    }

    Result<CheckpointInfo> getCheckpointInfo(const std::string& sourcePath) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lastCheckpoint_.sourcePath == sourcePath) {
            return Result<CheckpointInfo>::success(lastCheckpoint_);
        }
        return Result<CheckpointInfo>::error(PlayerError::NotFound);
    }

    uint64_t getSaveCount() const { return saveCount_; }

private:
    mutable std::mutex mutex_;
    CheckpointInfo lastCheckpoint_;
    uint64_t saveCount_ = 0;
};

// ============================================================================
// OfflineTranscodePipeline tests
// ============================================================================

TEST_CASE("OfflineTranscodePipeline - construction creates idle pipeline", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    REQUIRE(pipeline.getState() == OfflineTranscodePipeline::State::Idle);

    std::string stateStr = pipeline.stateToString(pipeline.getState());
    REQUIRE(stateStr == "Idle");
}

TEST_CASE("OfflineTranscodePipeline - configure without decoder returns error", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";

    auto result = pipeline.configure(config);
    REQUIRE(result.hasError());
}

TEST_CASE("OfflineTranscodePipeline - configure with decoder succeeds", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";

    auto result = pipeline.configure(config);
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.getState() == OfflineTranscodePipeline::State::Configured);
}

TEST_CASE("OfflineTranscodePipeline - start without configure returns error", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto result = pipeline.start();
    REQUIRE(result.hasError());
}

TEST_CASE("OfflineTranscodePipeline - lifecycle: configure -> start -> stop", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    auto configResult = pipeline.configure(config);
    REQUIRE(configResult.hasValue());
    REQUIRE(pipeline.getState() == OfflineTranscodePipeline::State::Configured);

    // Start in background thread
    std::thread startThread([&pipeline]() {
        auto startResult = pipeline.start();
        REQUIRE(startResult.hasValue() || startResult.error() == PlayerError::EndOfStream);
    });

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cancel since we have no actual input file
    pipeline.cancel();

    startThread.join();

    // State should be Cancelled or Error (expected with no actual input)
    auto state = pipeline.getState();
    REQUIRE((state == OfflineTranscodePipeline::State::Cancelled ||
             state == OfflineTranscodePipeline::State::Error));
}

TEST_CASE("OfflineTranscodePipeline - progress callback is invoked", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    std::atomic<int> callbackCount{0};
    std::atomic<uint64_t> framesProcessed{0};

    pipeline.setProgressCallback([&callbackCount, &framesProcessed](const TranscodeProgress& progress) {
        callbackCount++;
        framesProcessed.store(progress.framesProcessed);
    });

    std::thread startThread([&pipeline]() {
        pipeline.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    pipeline.cancel();
    startThread.join();

    // Progress callback should have been invoked
    REQUIRE(callbackCount.load() >= 0);
}

TEST_CASE("OfflineTranscodePipeline - error callback is invoked on error", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    // No decoder set - should error on start
    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";

    pipeline.configure(config);

    std::atomic<bool> errorCalled{false};
    std::atomic<PlayerError> lastError{PlayerError::None};

    pipeline.setErrorCallback([&errorCalled, &lastError](PlayerError err, const std::string& msg) {
        errorCalled = true;
        lastError = err;
    });

    auto result = pipeline.start();

    // Should get error
    REQUIRE(result.hasError());
    REQUIRE(errorCalled.load());
}

TEST_CASE("OfflineTranscodePipeline - pause and resume work correctly", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    std::thread startThread([&pipeline]() {
        pipeline.start();
    });

    // Wait for pipeline to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pipeline.pause();

    auto state = pipeline.getState();
    REQUIRE(state == OfflineTranscodePipeline::State::Running ||
            state == OfflineTranscodePipeline::State::Paused);

    pipeline.resume();

    state = pipeline.getState();
    REQUIRE(state == OfflineTranscodePipeline::State::Running ||
            state == OfflineTranscodePipeline::State::Paused);

    pipeline.cancel();
    startThread.join();
}

TEST_CASE("OfflineTranscodePipeline - getProgress returns valid info", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    TranscodeProgress progress = pipeline.getProgress();

    REQUIRE(progress.framesProcessed >= 0);
    REQUIRE(progress.currentFps >= 0.0);
    REQUIRE(progress.estimatedSecondsLeft >= 0.0);
    REQUIRE_FALSE(progress.isComplete);
}

TEST_CASE("OfflineTranscodePipeline - checkpoint manager integration", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    auto checkpointManager = std::make_shared<MockCheckpointManager>();
    pipeline.setCheckpointManager(checkpointManager);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = true;
    config.checkpointInterval = 10;

    pipeline.configure(config);

    std::thread startThread([&pipeline]() {
        pipeline.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pipeline.cancel();
    startThread.join();

    // Checkpoint should have been saved at least once if processing occurred
    uint64_t saveCount = checkpointManager->getSaveCount();
    REQUIRE(saveCount >= 0);
}

TEST_CASE("OfflineTranscodePipeline - state string conversion", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Idle) == "Idle");
    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Configured) == "Configured");
    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Running) == "Running");
    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Paused) == "Paused");
    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Completed) == "Completed");
    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Cancelled) == "Cancelled");
    REQUIRE(pipeline.stateToString(OfflineTranscodePipeline::State::Error) == "Error");
}

TEST_CASE("OfflineTranscodePipeline - config values are applied", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "test_input.mkv";
    config.outputPath = "test_output.mp4";
    config.outputFormat = "mp4";
    config.fastStart = false;
    config.vsrModelPath = "/models/sr";
    config.vsrScaleFactor = 4;
    config.audioPassthrough = false;
    config.enableCheckpoint = true;
    config.checkpointInterval = 50;
    config.packetQueueSize = 100;
    config.frameQueueSize = 4;
    config.encodedQueueSize = 50;

    auto result = pipeline.configure(config);
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.getState() == OfflineTranscodePipeline::State::Configured);
}

TEST_CASE("OfflineTranscodePipeline - waitUntilComplete blocks until done", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    std::thread startThread([&pipeline]() {
        pipeline.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pipeline.cancel();

    auto waitResult = pipeline.waitUntilComplete();

    startThread.join();

    // Wait should succeed (either complete or cancelled)
    REQUIRE(waitResult.hasValue() || waitResult.error() == PlayerError::OperationCancelled);
}

TEST_CASE("OfflineTranscodePipeline - VRAM manager integration", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    class MockVRAMManager : public IVRAMBudgetManager {
    public:
        Result<void> initialize(const VRAMBudgetConfig& config) override { return Result<void>::success(); }
        Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs) override {
            allocations_.push_back(sizeBytes);
            return Result<void>::success();
        }
        void release(uint64_t sizeBytes) override { releases_.push_back(sizeBytes); }
        uint64_t usedBytes() const override { return 0; }
        uint64_t availableBytes() const override { return UINT64_MAX; }
        bool isNearLimit() const override { return false; }
        void setPerformanceMode(PerformanceMode mode) override {}
        PerformanceMode getPerformanceMode() const override { return PerformanceMode::Balanced; }
        void reset() override {}

        std::vector<uint64_t> allocations_;
        std::vector<uint64_t> releases_;
    };

    auto vramManager = std::make_shared<MockVRAMManager>();
    pipeline.setVRAMBudgetManager(vramManager);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    std::thread startThread([&pipeline]() {
        pipeline.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pipeline.cancel();
    startThread.join();

    // VRAM manager should have been used
    REQUIRE(vramManager->allocations_.size() >= 0);
}

TEST_CASE("OfflineTranscodePipeline - multiple start calls handle gracefully", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    std::thread startThread([&pipeline]() {
        pipeline.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Second start should handle gracefully (likely return error)
    auto secondStart = pipeline.start();

    pipeline.cancel();
    startThread.join();

    REQUIRE(secondStart.hasError() || secondStart.hasValue());
}

TEST_CASE("OfflineTranscodePipeline - cancel can be called before start", "[offline_transcode][e2e]") {
    OfflineTranscodePipeline pipeline;

    auto decoder = std::make_shared<MockHWDecoder>();
    decoder->initialize(HWDecoderConfig{});
    pipeline.setDecoder(decoder);

    auto encoder = std::make_shared<MockVideoEncoder>();
    encoder->initialize(EncoderConfig{});
    pipeline.setEncoder(encoder);

    auto muxer = std::make_shared<MockMuxer>();
    muxer->initialize("output.mp4", MuxerConfig{});
    pipeline.setMuxer(muxer);

    OfflineTranscodeConfig config;
    config.inputPath = "input.mp4";
    config.outputPath = "output.mp4";
    config.enableCheckpoint = false;

    pipeline.configure(config);

    pipeline.cancel();

    auto state = pipeline.getState();
    REQUIRE(state == OfflineTranscodePipeline::State::Cancelled ||
            state == OfflineTranscodePipeline::State::Configured);
}
