#include <catch2/catch_test_macros.hpp>

#include <hlplayer/PlayerFacade.h>
#include <hlplayer/PlayerApi.h>
#include <hlplayer/StateMachine.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/IStreamResolver.h>
#include <hlplayer/Demuxer.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/IAIPipeline.h>
#include <hlplayer/CPUFallbackDecoder.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace hlplayer;

namespace {

class MockStreamResolver final : public IStreamResolver {
public:
    Result<void> resolveResult = Result<void>::success();
    StreamInfo streamInfo;
    bool shouldFail = false;
    bool cancelCalled = false;
    int resolveCallCount = 0;

    Result<void> resolve(const std::string& /*url*/,
                         std::function<void(Result<StreamInfo>)> callback) override {
        resolveCallCount++;
        if (shouldFail) {
            callback(Result<StreamInfo>::error(PlayerError::NetworkError));
        } else {
            callback(Result<StreamInfo>::success(streamInfo));
        }
        return resolveResult;
    }

    void cancel() override { cancelCalled = true; }

    uint32_t getCapabilities() const override { return 0; }
};

class MockDemuxer final : public IDemuxer {
public:
    bool openCalled = false;
    bool startCalled = false;
    bool stopCalled = false;
    bool seekCalled = false;
    double lastSeekSeconds = 0.0;
    bool shouldFailOpen = false;
    bool shouldFailStart = false;
    DemuxerCallbacks storedCallbacks;

    Result<void> open(const std::string& /*url*/,
                      const DemuxerConfig& /*config*/,
                      DemuxerCallbacks callbacks) override {
        openCalled = true;
        storedCallbacks = callbacks;
        if (shouldFailOpen) return Result<void>::error(PlayerError::UnsupportedFormat);
        return Result<void>::success();
    }

    Result<void> start() override {
        startCalled = true;
        if (shouldFailStart) return Result<void>::error(PlayerError::DecodeError);
        return Result<void>::success();
    }

    Result<void> stop() override {
        stopCalled = true;
        return Result<void>::success();
    }

    Result<void> seek(double seconds) override {
        seekCalled = true;
        lastSeekSeconds = seconds;
        return Result<void>::success();
    }

    PlayerState getState() const override { return PlayerState_Idle; }
    double getDuration() const override { return 0.0; }
};

class MockDecoder final : public IHWDecoder {
public:
    bool openCalled = false;
    bool closeCalled = false;
    int decodeCallCount = 0;
    bool shouldFailDecode = false;

    Result<void> open(const DecoderConfig& /*config*/) override {
        openCalled = true;
        return Result<void>::success();
    }

    Result<GpuFrame> decode(const uint8_t* /*data*/, size_t /*size*/, double pts) override {
        decodeCallCount++;
        if (shouldFailDecode) {
            return Result<GpuFrame>::error(PlayerError::DecodeError);
        }
        GpuFrame frame{};
        frame.timestamp = pts;
        frame.width = 320;
        frame.height = 240;
        frame.format = PixelFormat::NV12;
        return Result<GpuFrame>::success(frame);
    }

    Result<std::vector<GpuFrame>> flush() override {
        return Result<std::vector<GpuFrame>>::success({});
    }

    void close() override { closeCalled = true; }

    DecodeBackend getBackend() const override { return DecodeBackend::CPU; }
    bool supportsCodec(Codec /*codec*/) const override { return true; }
};

class MockAIPipeline final : public IAIPipeline {
public:
    int processCallCount = 0;
    GpuFrame lastInputFrame;
    uint32_t lastCapabilities = 0;
    bool shouldFail = false;
    std::unordered_set<AICapability> loadedModels;

    bool hasCapability(AICapability cap) const override {
        return loadedModels.count(cap) > 0;
    }

    Result<GpuFrame> processFrame(const GpuFrame& frame, uint32_t capabilities) override {
        processCallCount++;
        lastInputFrame = frame;
        lastCapabilities = capabilities;
        if (shouldFail) {
            return Result<GpuFrame>::error(PlayerError::DecodeError);
        }
        GpuFrame out = frame;
        out.width = frame.width * 2;
        out.height = frame.height * 2;
        return Result<GpuFrame>::success(out);
    }

    Result<void> loadModel(const std::string& /*path*/, AICapability cap) override {
        loadedModels.insert(cap);
        return Result<void>::success();
    }
};

class MockVideoSink final : public IVideoFrameSink {
public:
    int frameCount = 0;
    GpuFrame lastFrame;
    int formatChangeCount = 0;
    VideoFormat lastFormat = VideoFormat::Unknown;
    int resetCount = 0;

    void onFrame(const GpuFrame& frame) override {
        frameCount++;
        lastFrame = frame;
    }

    void onFormatChanged(VideoFormat format) override {
        formatChangeCount++;
        lastFormat = format;
    }

    void reset() override { resetCount++; }
};

struct FacadeContext {
    MockStreamResolver* resolver = nullptr;
    MockDemuxer* demuxer = nullptr;
    MockDecoder* decoder = nullptr;
    MockAIPipeline* aiPipeline = nullptr;
    std::unique_ptr<MockVideoSink> sinkOwner;
    std::unique_ptr<PlayerFacade> facade;
};

FacadeContext makeFacade() {
    FacadeContext ctx;
    auto r = std::make_unique<MockStreamResolver>();
    auto d = std::make_unique<MockDemuxer>();
    auto dec = std::make_unique<MockDecoder>();
    auto ai = std::make_unique<MockAIPipeline>();
    ctx.resolver = r.get();
    ctx.demuxer = d.get();
    ctx.decoder = dec.get();
    ctx.aiPipeline = ai.get();
    ctx.sinkOwner = std::make_unique<MockVideoSink>();

    ctx.facade = std::make_unique<PlayerFacade>(
        std::move(r), std::move(d), std::move(dec), std::move(ai), ctx.sinkOwner.get());

    return ctx;
}

} // namespace

TEST_CASE("PlayerFacade initial state is Idle", "[facade]") {
    auto ctx = makeFacade();
    REQUIRE(ctx.facade->getState() == PlayerState_Idle);
}

TEST_CASE("PlayerFacade open transitions ResolvingURL -> Prepared", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.resolver->streamInfo.width = 1920;
    ctx.resolver->streamInfo.height = 1080;

    auto result = ctx.facade->open("http://example.com/stream.mp4");
    REQUIRE(result.hasValue());
    REQUIRE(ctx.facade->getState() == PlayerState_Prepared);
    REQUIRE(ctx.resolver->resolveCallCount == 1);
    REQUIRE(ctx.demuxer->openCalled);
}

TEST_CASE("PlayerFacade play from Prepared goes Buffering -> Playing", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    REQUIRE(ctx.facade->getState() == PlayerState_Prepared);

    auto playResult = ctx.facade->play();
    REQUIRE(playResult.hasValue());
    REQUIRE(ctx.facade->getState() == PlayerState_Playing);
    REQUIRE(ctx.demuxer->startCalled);
}

TEST_CASE("PlayerFacade pause/resume transitions", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();
    REQUIRE(ctx.facade->getState() == PlayerState_Playing);

    SECTION("pause from Playing -> Paused") {
        auto r = ctx.facade->pause();
        REQUIRE(r.hasValue());
        REQUIRE(ctx.facade->getState() == PlayerState_Paused);
    }

    SECTION("resume from Paused -> Playing") {
        ctx.facade->pause();
        auto r = ctx.facade->play();
        REQUIRE(r.hasValue());
        REQUIRE(ctx.facade->getState() == PlayerState_Playing);
    }
}

TEST_CASE("PlayerFacade stop resets to Idle", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();
    REQUIRE(ctx.facade->getState() == PlayerState_Playing);

    auto r = ctx.facade->stop();
    REQUIRE(r.hasValue());
    REQUIRE(ctx.facade->getState() == PlayerState_Idle);
    REQUIRE(ctx.demuxer->stopCalled);
    REQUIRE(ctx.decoder->closeCalled);
}

TEST_CASE("PlayerFacade error propagation from resolver", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->shouldFail = true;

    auto result = ctx.facade->open("http://example.com/bad.mp4");
    REQUIRE(result.hasError());
    REQUIRE(ctx.facade->getState() == PlayerState_Error);
    REQUIRE(ctx.facade->getLastError().find("Failed to resolve URL") != std::string::npos);

    SECTION("can open again from Error state") {
        ctx.resolver->shouldFail = false;
        ctx.resolver->streamInfo.url = "http://example.com/good.mp4";

        auto r2 = ctx.facade->open("http://example.com/good.mp4");
        REQUIRE(r2.hasValue());
        REQUIRE(ctx.facade->getState() == PlayerState_Prepared);
    }

    SECTION("can stop from Error state to Idle") {
        auto r2 = ctx.facade->stop();
        REQUIRE(r2.hasValue());
        REQUIRE(ctx.facade->getState() == PlayerState_Idle);
    }
}

TEST_CASE("PlayerFacade publishes StateChanged events", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";

    std::vector<StateChangedPayload> stateChanges;
    ctx.facade->eventBus().subscribe(EventType::StateChanged, [&](const Event& e) {
        stateChanges.push_back(std::get<StateChangedPayload>(e.payload));
    });

    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->eventBus().dispatch();
    REQUIRE(stateChanges.size() >= 2);

    bool foundResolving = false;
    bool foundPrepared = false;
    for (auto& sc : stateChanges) {
        if (sc.newState == PlayerState_ResolvingURL) foundResolving = true;
        if (sc.newState == PlayerState_Prepared) foundPrepared = true;
    }
    REQUIRE(foundResolving);
    REQUIRE(foundPrepared);
}

TEST_CASE("PlayerFacade telemetry counters", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();
    REQUIRE(ctx.facade->getState() == PlayerState_Playing);

    SECTION("error_count increments on resolver failure") {
        ctx.facade->stop();
        ctx.resolver->shouldFail = true;
        ctx.facade->open("http://example.com/bad.mp4");
        REQUIRE(ctx.facade->getTelemetryCounter("error_count") >= 1);
    }

    SECTION("seek_count increments on seek") {
        auto seekResult = ctx.facade->seek(5.0);
        REQUIRE(seekResult.hasValue());
        REQUIRE(ctx.facade->getTelemetryCounter("seek_count") == 1);
    }
}

TEST_CASE("PlayerFacade frame delivery through pipeline", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();
    REQUIRE(ctx.facade->getState() == PlayerState_Playing);

    auto packet = std::make_shared<MediaPacket>();
    packet->streamType = StreamType::Video;
    packet->pts = 1.0;
    packet->duration = 0.033;
    packet->data = {0x00, 0x01, 0x02, 0x03};

    ctx.demuxer->storedCallbacks.onPacket(packet);

    REQUIRE(ctx.sinkOwner->frameCount == 1);
    REQUIRE(ctx.sinkOwner->lastFrame.timestamp == 1.0);
    REQUIRE(ctx.decoder->decodeCallCount == 1);
    REQUIRE(ctx.facade->getTelemetryCounter("frames_decoded") == 1);
}

TEST_CASE("C-ABI functions work through facade", "[facade][cabi]") {
    HLPlayerHandle handle = HLPlayer_Create();
    REQUIRE(handle != nullptr);
    REQUIRE(HLPlayer_GetState(handle) == PlayerState_Idle);

    SECTION("create and destroy") {
        HLPlayer_Destroy(handle);
    }

    SECTION("default stub open fails gracefully") {
        auto err = HLPlayer_Open(handle, "http://example.com/test.mp4");
        REQUIRE(err != PlayerErrorCode_None);
        REQUIRE(HLPlayer_GetState(handle) == PlayerState_Error);

        char buf[256] = {0};
        HLPlayer_GetError(handle, buf, sizeof(buf));
        REQUIRE(std::string(buf).length() > 0);
        HLPlayer_Destroy(handle);
    }

    SECTION("setVolume always succeeds") {
        REQUIRE(HLPlayer_SetVolume(handle, 0.5) == PlayerErrorCode_None);
        HLPlayer_Destroy(handle);
    }

    SECTION("null handle safety") {
        REQUIRE(HLPlayer_GetState(nullptr) == PlayerState_Idle);
        REQUIRE(HLPlayer_Open(nullptr, "url") != PlayerErrorCode_None);
        REQUIRE(HLPlayer_Play(nullptr) != PlayerErrorCode_None);
        REQUIRE(HLPlayer_Pause(nullptr) != PlayerErrorCode_None);
        REQUIRE(HLPlayer_Stop(nullptr) != PlayerErrorCode_None);
        HLPlayer_Destroy(nullptr);
    }
}

TEST_CASE("C-ABI full lifecycle with mock facade", "[facade][cabi]") {
    auto ctx = makeFacade();
    ctx.resolver->streamInfo.url = "http://example.com/test.mp4";

    auto* rawFacade = ctx.facade.get();
    HLPlayerHandle handle = reinterpret_cast<HLPlayerHandle>(rawFacade);

    REQUIRE(HLPlayer_Open(handle, "http://example.com/test.mp4") == PlayerErrorCode_None);
    REQUIRE(HLPlayer_GetState(handle) == PlayerState_Prepared);

    REQUIRE(HLPlayer_Play(handle) == PlayerErrorCode_None);
    REQUIRE(HLPlayer_GetState(handle) == PlayerState_Playing);

    REQUIRE(HLPlayer_Pause(handle) == PlayerErrorCode_None);
    REQUIRE(HLPlayer_GetState(handle) == PlayerState_Paused);

    REQUIRE(HLPlayer_Play(handle) == PlayerErrorCode_None);
    REQUIRE(HLPlayer_GetState(handle) == PlayerState_Playing);

    REQUIRE(HLPlayer_Stop(handle) == PlayerErrorCode_None);
    REQUIRE(HLPlayer_GetState(handle) == PlayerState_Idle);
}

TEST_CASE("PlayerFacade play from invalid state returns error", "[facade]") {
    auto ctx = makeFacade();

    REQUIRE(ctx.facade->play().hasError());
    REQUIRE(ctx.facade->pause().hasError());
    REQUIRE(ctx.facade->stop().hasError());
}

TEST_CASE("PlayerFacade seek from Playing state", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();

    auto r = ctx.facade->seek(10.0);
    REQUIRE(r.hasValue());
    REQUIRE(ctx.demuxer->seekCalled);
    REQUIRE(ctx.demuxer->lastSeekSeconds == 10.0);
}

TEST_CASE("PlayerFacade seek from invalid state returns error", "[facade]") {
    auto ctx = makeFacade();

    auto r = ctx.facade->seek(5.0);
    REQUIRE(r.hasError());
}

TEST_CASE("PlayerFacade publishes Error event on resolver failure", "[facade]") {
    auto ctx = makeFacade();

    ctx.resolver->shouldFail = true;

    ErrorPayload capturedError;
    ctx.facade->eventBus().subscribe(EventType::Error, [&](const Event& e) {
        capturedError = std::get<ErrorPayload>(e.payload);
    });

    ctx.facade->open("http://example.com/bad.mp4");
    ctx.facade->eventBus().dispatch();

    REQUIRE(capturedError.error == PlayerError::NetworkError);
    REQUIRE(capturedError.message.find("Failed to resolve URL") != std::string::npos);
}

TEST_CASE("PlayerFacade setVolume always succeeds", "[facade]") {
    auto ctx = makeFacade();

    REQUIRE(ctx.facade->setVolume(0.5).hasValue());
    REQUIRE(ctx.facade->setVolume(0.0).hasValue());
    REQUIRE(ctx.facade->setVolume(1.5).hasValue());
}

TEST_CASE("PlayerFacade AI pipeline not invoked when no capabilities enabled", "[facade][ai]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();

    auto packet = std::make_shared<MediaPacket>();
    packet->streamType = StreamType::Video;
    packet->pts = 1.0;
    packet->duration = 0.033;
    packet->data = {0x00, 0x01, 0x02, 0x03};

    ctx.demuxer->storedCallbacks.onPacket(packet);

    REQUIRE(ctx.aiPipeline->processCallCount == 0);
    REQUIRE(ctx.sinkOwner->lastFrame.width == 320);
    REQUIRE(ctx.sinkOwner->lastFrame.height == 240);
}

TEST_CASE("PlayerFacade AI pipeline invoked when capability enabled and model loaded", "[facade][ai]") {
    auto ctx = makeFacade();

    ctx.aiPipeline->loadedModels.insert(AICapability::SuperResolution);
    auto enResult = ctx.facade->enableAICapability(AICapability::SuperResolution);
    REQUIRE(enResult.hasValue());
    REQUIRE(ctx.facade->isAICapabilityEnabled(AICapability::SuperResolution));

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();

    auto packet = std::make_shared<MediaPacket>();
    packet->streamType = StreamType::Video;
    packet->pts = 1.0;
    packet->duration = 0.033;
    packet->data = {0x00, 0x01, 0x02, 0x03};

    ctx.demuxer->storedCallbacks.onPacket(packet);

    REQUIRE(ctx.aiPipeline->processCallCount == 1);
    REQUIRE(ctx.aiPipeline->lastCapabilities == static_cast<uint32_t>(AICapability::SuperResolution));
    REQUIRE(ctx.sinkOwner->lastFrame.width == 640);
    REQUIRE(ctx.sinkOwner->lastFrame.height == 480);
}

TEST_CASE("PlayerFacade enableAICapability rejects unloaded capability", "[facade][ai]") {
    auto ctx = makeFacade();

    auto result = ctx.facade->enableAICapability(AICapability::SuperResolution);
    REQUIRE(result.hasError());
    REQUIRE(!ctx.facade->isAICapabilityEnabled(AICapability::SuperResolution));
}

TEST_CASE("PlayerFacade loadAIModel delegates to pipeline", "[facade][ai]") {
    auto ctx = makeFacade();

    auto result = ctx.facade->loadAIModel("/models/sr.ncnn", AICapability::SuperResolution);
    REQUIRE(result.hasValue());
    REQUIRE(ctx.aiPipeline->hasCapability(AICapability::SuperResolution));

    auto enResult = ctx.facade->enableAICapability(AICapability::SuperResolution);
    REQUIRE(enResult.hasValue());
}

TEST_CASE("PlayerFacade AI pipeline failure does not crash, frame still delivered", "[facade][ai]") {
    auto ctx = makeFacade();

    ctx.aiPipeline->loadedModels.insert(AICapability::SuperResolution);
    ctx.aiPipeline->shouldFail = true;
    ctx.facade->enableAICapability(AICapability::SuperResolution);

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();

    auto packet = std::make_shared<MediaPacket>();
    packet->streamType = StreamType::Video;
    packet->pts = 1.0;
    packet->duration = 0.033;
    packet->data = {0x00, 0x01, 0x02, 0x03};

    ctx.demuxer->storedCallbacks.onPacket(packet);

    REQUIRE(ctx.aiPipeline->processCallCount == 1);
    REQUIRE(ctx.sinkOwner->frameCount == 1);
}

TEST_CASE("PlayerFacade decode error transitions to Error state", "[facade]") {
    auto ctx = makeFacade();

    ctx.decoder->shouldFailDecode = true;

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();

    auto packet = std::make_shared<MediaPacket>();
    packet->streamType = StreamType::Video;
    packet->pts = 1.0;
    packet->data = {0x00, 0x01};

    ctx.demuxer->storedCallbacks.onPacket(packet);

    REQUIRE(ctx.facade->getState() == PlayerState_Error);
    REQUIRE(ctx.facade->getLastError().find("Decode failed") != std::string::npos);
    REQUIRE(ctx.facade->getTelemetryCounter("error_count") >= 1);
}

TEST_CASE("PlayerFacade thread safety: concurrent pause/play calls", "[facade][thread]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");
    ctx.facade->play();

    constexpr int numThreads = 8;
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; j++) {
                auto r = ctx.facade->pause();
                if (r.hasValue()) {
                    successCount++;
                } else {
                    errorCount++;
                }
                r = ctx.facade->play();
                if (r.hasValue()) {
                    successCount++;
                } else {
                    errorCount++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(successCount + errorCount == numThreads * 200);
    REQUIRE(successCount > 0);
}

TEST_CASE("PlayerFacade thread safety: concurrent state reads", "[facade][thread]") {
    auto ctx = makeFacade();

    ctx.resolver->streamInfo.url = "http://example.com/stream.mp4";
    ctx.facade->open("http://example.com/stream.mp4");

    constexpr int numThreads = 8;
    std::atomic<bool> allValid{true};

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 1000; j++) {
                PlayerState s = ctx.facade->getState();
                if (s < PlayerState_Idle || s > PlayerState_DeviceLost) {
                    allValid = false;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(allValid);
}

TEST_CASE("PlayerFacade C-API exception safety: no crash on any input", "[facade][cabi]") {
    HLPlayerHandle handle = HLPlayer_Create();
    REQUIRE(handle != nullptr);

    auto openResult = HLPlayer_Open(handle, "");
    REQUIRE((openResult == PlayerErrorCode_None || openResult != PlayerErrorCode_None));
    REQUIRE(HLPlayer_Open(handle, nullptr) != PlayerErrorCode_None);

    auto seekResult = HLPlayer_Seek(handle, -1.0);
    REQUIRE((seekResult == PlayerErrorCode_None || seekResult != PlayerErrorCode_None));

    char tinyBuf[1] = {0};
    HLPlayer_GetError(handle, tinyBuf, 1);

    HLPlayer_GetError(handle, nullptr, 100);

    REQUIRE(HLPlayer_GetState(handle) != PlayerState_DeviceLost);

    HLPlayer_Destroy(handle);
}

TEST_CASE("PlayerFacade default constructor creates working facade with stubs", "[facade]") {
    PlayerFacade facade;
    REQUIRE(facade.getState() == PlayerState_Idle);
    REQUIRE(facade.eventBus().dispatch() == 0);
    REQUIRE(facade.setVolume(0.5).hasValue());
}
