#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// Public SDK headers — these define the API contract
#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/PlayerApi.h>
#include <hlplayer/IPlayerFacade.h>
#include <hlplayer/IStreamResolver.h>
#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/IAIPipeline.h>

#include <string>
#include <cstdint>
#include <functional>

// ============================================================================
// Mock implementations — prove the interfaces are implementable
// ============================================================================

class MockPlayerFacade : public hlplayer::IPlayerFacade {
public:
    bool opened = false;
    bool played = false;
    bool paused = false;
    bool stopped = false;
    double seekTime = -1.0;
    double volume = -1.0;
    PlayerState state = PlayerState_Idle;
    std::string lastUrl;

    hlplayer::Result<void> open(const std::string& url) override {
        lastUrl = url;
        opened = true;
        return hlplayer::Result<void>::success();
    }

    hlplayer::Result<void> play() override {
        played = true;
        state = PlayerState_Playing;
        return hlplayer::Result<void>::success();
    }

    hlplayer::Result<void> pause() override {
        paused = true;
        state = PlayerState_Paused;
        return hlplayer::Result<void>::success();
    }

    hlplayer::Result<void> stop() override {
        stopped = true;
        state = PlayerState_Idle;
        return hlplayer::Result<void>::success();
    }

    hlplayer::Result<void> seek(double seconds) override {
        seekTime = seconds;
        return hlplayer::Result<void>::success();
    }

    hlplayer::Result<void> setVolume(double vol) override {
        volume = vol;
        return hlplayer::Result<void>::success();
    }

    PlayerState getState() const override {
        return state;
    }

    double getPosition() const override {
        return 0.0;
    }

    double getDuration() const override {
        return 0.0;
    }
};

class MockStreamResolver : public hlplayer::IStreamResolver {
public:
    bool resolveCalled = false;
    bool cancelled = false;
    std::string lastUrl;
    hlplayer::StreamInfo resolvedInfo;

    hlplayer::Result<void> resolve(
        const std::string& url,
        std::function<void(hlplayer::Result<hlplayer::StreamInfo>)> callback) override
    {
        resolveCalled = true;
        lastUrl = url;
        if (callback) {
            resolvedInfo.url = url;
            resolvedInfo.format = "mp4";
            resolvedInfo.width = 1920;
            resolvedInfo.height = 1080;
            resolvedInfo.bitrate = 5000000;
            callback(hlplayer::Result<hlplayer::StreamInfo>::success(resolvedInfo));
        }
        return hlplayer::Result<void>::success();
    }

    void cancel() override {
        cancelled = true;
    }

    uint32_t getCapabilities() const override {
        return static_cast<uint32_t>(hlplayer::ResolverCapability::HttpProgressive)
             | static_cast<uint32_t>(hlplayer::ResolverCapability::Hls);
    }
};

class MockVideoFrameSink : public hlplayer::IVideoFrameSink {
public:
    int frameCount = 0;
    int formatChangeCount = 0;
    int resetCount = 0;
    hlplayer::GpuFrame lastFrame;
    hlplayer::VideoFormat lastFormat = hlplayer::VideoFormat::Unknown;

    void onFrame(const hlplayer::GpuFrame& frame) override {
        ++frameCount;
        lastFrame = frame;
    }

    void onFormatChanged(hlplayer::VideoFormat format) override {
        ++formatChangeCount;
        lastFormat = format;
    }

    void reset() override {
        ++resetCount;
    }
};

class MockAIPipeline : public hlplayer::IAIPipeline {
public:
    bool hasCapResult = true;
    bool processCalled = false;
    bool loadCalled = false;
    std::string loadedPath;
    hlplayer::AICapability loadedCap = hlplayer::AICapability::None;

    bool hasCapability(hlplayer::AICapability cap) const override {
        (void)cap;
        return hasCapResult;
    }

    hlplayer::Result<hlplayer::GpuFrame> processFrame(
        const hlplayer::GpuFrame& frame, uint32_t capabilities) override
    {
        (void)capabilities;
        processCalled = true;
        return hlplayer::Result<hlplayer::GpuFrame>::success(frame);
    }

    hlplayer::Result<void> loadModel(const std::string& path, hlplayer::AICapability cap) override {
        loadCalled = true;
        loadedPath = path;
        loadedCap = cap;
        return hlplayer::Result<void>::success();
    }
};

class MockGpuFramePool : public hlplayer::GpuFramePool {
public:
    int allocCount = 0;
    int recycleCount = 0;

    hlplayer::GpuFrame allocate(uint32_t width, uint32_t height, hlplayer::PixelFormat format) override {
        ++allocCount;
        hlplayer::GpuFrame frame;
        frame.width = width;
        frame.height = height;
        frame.format = format;
        frame.handle.nativeHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(allocCount * 100));
        return frame;
    }

    void recycle(hlplayer::GpuFrame&& frame) override {
        ++recycleCount;
        (void)frame;
    }

    void reset() override {
        allocCount = 0;
        recycleCount = 0;
    }
};

// ============================================================================
// Result<T> tests
// ============================================================================

TEST_CASE("Result<int> success path", "[result]") {
    auto r = hlplayer::Result<int>::success(42);
    REQUIRE(r.hasValue());
    REQUIRE_FALSE(r.hasError());
    REQUIRE(r.value() == 42);
    REQUIRE(static_cast<int>(r.error()) == 0);
}

TEST_CASE("Result<int> error path", "[result]") {
    auto r = hlplayer::Result<int>::error(hlplayer::PlayerError::NetworkError);
    REQUIRE_FALSE(r.hasValue());
    REQUIRE(r.hasError());
    REQUIRE(r.error() == hlplayer::PlayerError::NetworkError);
}

TEST_CASE("Result<void> success", "[result]") {
    auto r = hlplayer::Result<void>::success();
    REQUIRE(r.hasValue());
    REQUIRE_FALSE(r.hasError());
}

TEST_CASE("Result<void> error", "[result]") {
    auto r = hlplayer::Result<void>::error(hlplayer::PlayerError::DecodeError);
    REQUIRE_FALSE(r.hasValue());
    REQUIRE(r.hasError());
    REQUIRE(r.error() == hlplayer::PlayerError::DecodeError);
}

TEST_CASE("Result<string> move semantics", "[result]") {
    auto r1 = hlplayer::Result<std::string>::success(std::string("hello"));
    REQUIRE(r1.hasValue());
    REQUIRE(r1.value() == "hello");

    auto r2 = std::move(r1);
    REQUIRE(r2.hasValue());
    REQUIRE(r2.value() == "hello");
}

TEST_CASE("Result<int> value_or fallback", "[result]") {
    auto rOk = hlplayer::Result<int>::success(10);
    auto rErr = hlplayer::Result<int>::error(hlplayer::PlayerError::Unknown);
    REQUIRE(rOk.value_or(0) == 10);
    REQUIRE(rErr.value_or(0) == 0);
}

// ============================================================================
// PlayerError enum tests
// ============================================================================

TEST_CASE("PlayerError enum values", "[error]") {
    REQUIRE(static_cast<int>(hlplayer::PlayerError::None) == 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::InvalidURL) > 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::NetworkError) > 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::DecodeError) > 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::DeviceLost) > 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::InvalidState) > 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::UnsupportedFormat) > 0);
    REQUIRE(static_cast<int>(hlplayer::PlayerError::Unknown) > 100);
}

// ============================================================================
// GpuFrame tests
// ============================================================================

TEST_CASE("GpuFrame default construction", "[gpuframe]") {
    hlplayer::GpuFrame frame;
    REQUIRE(frame.format == hlplayer::PixelFormat::Unknown);
    REQUIRE(frame.width == 0);
    REQUIRE(frame.height == 0);
    REQUIRE(frame.colorSpace == hlplayer::ColorSpace::BT709);
    REQUIRE(frame.colorRange == hlplayer::ColorRange::Limited);
    REQUIRE(frame.timestamp == 0.0);
    REQUIRE(frame.deviceLost == false);
    REQUIRE(frame.handle.nativeHandle == nullptr);
    REQUIRE(frame.handle.auxiliaryHandle == nullptr);
    REQUIRE(frame.handle.apiType == 0);
}

TEST_CASE("GpuFrame field assignment", "[gpuframe]") {
    hlplayer::GpuFrame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.format = hlplayer::PixelFormat::NV12;
    frame.colorSpace = hlplayer::ColorSpace::BT709;
    frame.timestamp = 1.5;
    frame.handle.nativeHandle = reinterpret_cast<void*>(0xDEADBEEF);
    frame.handle.auxiliaryHandle = reinterpret_cast<void*>(0xCAFECAFE);
    frame.handle.apiType = 1;

    REQUIRE(frame.width == 1920);
    REQUIRE(frame.height == 1080);
    REQUIRE(frame.format == hlplayer::PixelFormat::NV12);
    REQUIRE(frame.timestamp == Catch::Approx(1.5));
    REQUIRE(frame.handle.nativeHandle == reinterpret_cast<void*>(0xDEADBEEF));
    REQUIRE(frame.handle.auxiliaryHandle == reinterpret_cast<void*>(0xCAFECAFE));
    REQUIRE(frame.handle.apiType == 1);
}

TEST_CASE("GpuFrame is copyable (non-owning view)", "[gpuframe]") {
    hlplayer::GpuFrame frame;
    frame.width = 800;
    frame.height = 600;
    frame.format = hlplayer::PixelFormat::RGBA8;
    frame.handle.nativeHandle = reinterpret_cast<void*>(0x1234);

    hlplayer::GpuFrame copy = frame;
    REQUIRE(copy.width == 800);
    REQUIRE(copy.height == 600);
    REQUIRE(copy.format == hlplayer::PixelFormat::RGBA8);
    REQUIRE(copy.handle.nativeHandle == frame.handle.nativeHandle);
    // Proves it is copyable — compiler enforces this
}

TEST_CASE("GpuFramePool mock allocates and recycles", "[gpuframe]") {
    MockGpuFramePool pool;
    auto f1 = pool.allocate(1920, 1080, hlplayer::PixelFormat::NV12);
    REQUIRE(f1.width == 1920);
    REQUIRE(f1.height == 1080);
    REQUIRE(f1.format == hlplayer::PixelFormat::NV12);
    REQUIRE(pool.allocCount == 1);

    pool.recycle(std::move(f1));
    REQUIRE(pool.recycleCount == 1);

    pool.reset();
    REQUIRE(pool.allocCount == 0);
    REQUIRE(pool.recycleCount == 0);
}

TEST_CASE("PixelFormat and ColorSpace enum ordering", "[gpuframe]") {
    REQUIRE(static_cast<int>(hlplayer::PixelFormat::Unknown) == 0);
    REQUIRE(static_cast<int>(hlplayer::PixelFormat::NV12) == 1);
    REQUIRE(static_cast<int>(hlplayer::PixelFormat::P010) == 2);
    REQUIRE(static_cast<int>(hlplayer::PixelFormat::RGBA8) == 3);
    REQUIRE(static_cast<int>(hlplayer::PixelFormat::RGBA16F) == 4);
    REQUIRE(static_cast<int>(hlplayer::PixelFormat::Vulkan) == 5);

    REQUIRE(static_cast<int>(hlplayer::ColorSpace::BT601) == 0);
    REQUIRE(static_cast<int>(hlplayer::ColorSpace::BT709) == 1);
    REQUIRE(static_cast<int>(hlplayer::ColorSpace::BT2020) == 2);
    REQUIRE(static_cast<int>(hlplayer::ColorSpace::sRGB) == 3);

    REQUIRE(static_cast<int>(hlplayer::ColorRange::Limited) == 0);
    REQUIRE(static_cast<int>(hlplayer::ColorRange::Full) == 1);
}

// ============================================================================
// PlayerState and PlayerErrorCode (C-ABI enums)
// ============================================================================

TEST_CASE("PlayerState enum values", "[abi]") {
    REQUIRE(PlayerState_Idle == 0);
    REQUIRE(PlayerState_ResolvingURL == 1);
    REQUIRE(PlayerState_Prepared == 2);
    REQUIRE(PlayerState_Buffering == 3);
    REQUIRE(PlayerState_Playing == 4);
    REQUIRE(PlayerState_Paused == 5);
    REQUIRE(PlayerState_Error == 6);
    REQUIRE(PlayerState_End == 7);
    REQUIRE(PlayerState_DeviceLost == 8);
}

TEST_CASE("PlayerErrorCode enum values", "[abi]") {
    REQUIRE(PlayerErrorCode_None == 0);
    REQUIRE(PlayerErrorCode_InvalidURL == 1);
    REQUIRE(PlayerErrorCode_NetworkError == 2);
    REQUIRE(PlayerErrorCode_DecodeError == 3);
    REQUIRE(PlayerErrorCode_DeviceLost == 4);
    REQUIRE(PlayerErrorCode_Unknown == 999);
}

// ============================================================================
// IPlayerFacade mock tests
// ============================================================================

TEST_CASE("MockPlayerFacade open/play/pause/stop", "[facade]") {
    MockPlayerFacade player;

    SECTION("open stores URL and returns success") {
        auto result = player.open("http://example.com/stream.mp4");
        REQUIRE(result.hasValue());
        REQUIRE(player.opened);
        REQUIRE(player.lastUrl == "http://example.com/stream.mp4");
    }

    SECTION("play transitions to Playing") {
        player.play();
        REQUIRE(player.played);
        REQUIRE(player.getState() == PlayerState_Playing);
    }

    SECTION("pause transitions to Paused") {
        player.play();
        player.pause();
        REQUIRE(player.paused);
        REQUIRE(player.getState() == PlayerState_Paused);
    }

    SECTION("stop returns to Idle") {
        player.play();
        player.stop();
        REQUIRE(player.stopped);
        REQUIRE(player.getState() == PlayerState_Idle);
    }

    SECTION("seek records target time") {
        player.seek(30.5);
        REQUIRE(player.seekTime == Catch::Approx(30.5));
    }

    SECTION("setVolume records level") {
        player.setVolume(0.75);
        REQUIRE(player.volume == Catch::Approx(0.75));
    }
}

// ============================================================================
// IStreamResolver mock tests
// ============================================================================

TEST_CASE("StreamInfo default construction", "[resolver]") {
    hlplayer::StreamInfo info;
    REQUIRE(info.url.empty());
    REQUIRE(info.format.empty());
    REQUIRE(info.width == 0);
    REQUIRE(info.height == 0);
    REQUIRE(info.bitrate == 0);
    REQUIRE(info.drmInfo.empty());
}

TEST_CASE("StreamInfo field assignment", "[resolver]") {
    hlplayer::StreamInfo info;
    info.url = "http://example.com/video.mp4";
    info.format = "mp4";
    info.width = 1920;
    info.height = 1080;
    info.bitrate = 5000000;
    info.drmInfo = "widevine";

    REQUIRE(info.url == "http://example.com/video.mp4");
    REQUIRE(info.format == "mp4");
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.bitrate == 5000000);
    REQUIRE(info.drmInfo == "widevine");
}

TEST_CASE("MockStreamResolver resolve with callback", "[resolver]") {
    MockStreamResolver resolver;
    bool callbackReceived = false;

    auto result = resolver.resolve(
        "http://example.com/stream.m3u8",
        [&](hlplayer::Result<hlplayer::StreamInfo> r) {
            callbackReceived = true;
            REQUIRE(r.hasValue());
            REQUIRE(r.value().url == "http://example.com/stream.m3u8");
            REQUIRE(r.value().format == "mp4");
            REQUIRE(r.value().width == 1920);
            REQUIRE(r.value().height == 1080);
        });

    REQUIRE(result.hasValue());
    REQUIRE(resolver.resolveCalled);
    REQUIRE(resolver.lastUrl == "http://example.com/stream.m3u8");
    REQUIRE(callbackReceived);
}

TEST_CASE("MockStreamResolver cancel", "[resolver]") {
    MockStreamResolver resolver;
    resolver.cancel();
    REQUIRE(resolver.cancelled);
}

TEST_CASE("MockStreamResolver getCapabilities", "[resolver]") {
    MockStreamResolver resolver;
    uint32_t caps = resolver.getCapabilities();
    REQUIRE((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::HttpProgressive)) != 0);
    REQUIRE((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::Hls)) != 0);
    REQUIRE((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::Dash)) == 0);
}

// ============================================================================
// IVideoFrameSink mock tests
// ============================================================================

TEST_CASE("MockVideoFrameSink receives frames", "[sink]") {
    MockVideoFrameSink sink;

    hlplayer::GpuFrame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.format = hlplayer::PixelFormat::NV12;
    frame.timestamp = 0.033;

    sink.onFrame(frame);
    REQUIRE(sink.frameCount == 1);
    REQUIRE(sink.lastFrame.width == 1920);
    REQUIRE(sink.lastFrame.format == hlplayer::PixelFormat::NV12);
    REQUIRE(sink.lastFrame.timestamp == Catch::Approx(0.033));

    sink.onFrame(frame);
    sink.onFrame(frame);
    REQUIRE(sink.frameCount == 3);
}

TEST_CASE("MockVideoFrameSink format changes and reset", "[sink]") {
    MockVideoFrameSink sink;

    sink.onFormatChanged(hlplayer::VideoFormat::NV12);
    REQUIRE(sink.formatChangeCount == 1);
    REQUIRE(sink.lastFormat == hlplayer::VideoFormat::NV12);

    sink.onFormatChanged(hlplayer::VideoFormat::P010);
    REQUIRE(sink.formatChangeCount == 2);
    REQUIRE(sink.lastFormat == hlplayer::VideoFormat::P010);

    sink.reset();
    REQUIRE(sink.resetCount == 1);
}

// ============================================================================
// IAIPipeline mock tests
// ============================================================================

TEST_CASE("MockAIPipeline hasCapability", "[ai]") {
    MockAIPipeline pipeline;
    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));

    pipeline.hasCapResult = false;
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::FrameInterpolation));
}

TEST_CASE("MockAIPipeline processFrame", "[ai]") {
    MockAIPipeline pipeline;

    hlplayer::GpuFrame input;
    input.width = 640;
    input.height = 480;
    input.format = hlplayer::PixelFormat::RGBA8;

    auto result = pipeline.processFrame(
        input,
        static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasValue());
    REQUIRE(result.value().width == 640);
    REQUIRE(result.value().height == 480);
    REQUIRE(pipeline.processCalled);
}

TEST_CASE("MockAIPipeline loadModel", "[ai]") {
    MockAIPipeline pipeline;

    auto result = pipeline.loadModel("/models/sr.onnx", hlplayer::AICapability::SuperResolution);
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.loadCalled);
    REQUIRE(pipeline.loadedPath == "/models/sr.onnx");
    REQUIRE(pipeline.loadedCap == hlplayer::AICapability::SuperResolution);
}

TEST_CASE("AICapability enum values", "[ai]") {
    REQUIRE(static_cast<int>(hlplayer::AICapability::None) == 0);
    REQUIRE(static_cast<int>(hlplayer::AICapability::SuperResolution) > 0);
    REQUIRE(static_cast<int>(hlplayer::AICapability::FrameInterpolation) > 0);
    REQUIRE(static_cast<int>(hlplayer::AICapability::HDR) > 0);
    REQUIRE(static_cast<int>(hlplayer::AICapability::ToneMapping) > 0);
    REQUIRE(static_cast<int>(hlplayer::AICapability::NoiseReduction) > 0);
    // Verify bit flags are distinct
    REQUIRE(static_cast<int>(hlplayer::AICapability::SuperResolution)
         != static_cast<int>(hlplayer::AICapability::FrameInterpolation));
}

// ============================================================================
// C-ABI type verification (no actual calls — just type checks)
// ============================================================================

TEST_CASE("C-ABI handle and enum types compile correctly", "[abi]") {
    HLPlayerHandle handle = nullptr;
    REQUIRE(handle == nullptr);

    PlayerState state = PlayerState_Idle;
    REQUIRE(state == PlayerState_Idle);

    PlayerErrorCode err = PlayerErrorCode_None;
    REQUIRE(err == PlayerErrorCode_None);

    // Verify arithmetic on enum values
    state = static_cast<PlayerState>(static_cast<int>(state) + 1);
    REQUIRE(state == PlayerState_ResolvingURL);
}
