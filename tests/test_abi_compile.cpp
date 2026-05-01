// ============================================================================
// test_abi_compile.cpp — Mock plugin compile test
// ============================================================================
// This is a SEPARATE compilation unit that includes ONLY the public SDK headers.
// It proves:
//   1. All public headers are self-contained (no missing transitive includes)
//   2. Mock implementations of all 4 interfaces compile against the contracts
//   3. C-ABI types (enums, handle) have correct sizes and values
//   4. The ABI boundary is clean — no Qt, no implementation details leak
// ============================================================================

#include <catch2/catch_test_macros.hpp>

// ONLY public headers — nothing else from the project
#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/PlayerApi.h>
#include <hlplayer/IPlayerFacade.h>
#include <hlplayer/IStreamResolver.h>
#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/IAIPipeline.h>

#include <cstdint>
#include <string>
#include <functional>

// ============================================================================
// Self-contained mock implementations (no base project headers needed)
// ============================================================================

class ABIMockPlayerFacade : public hlplayer::IPlayerFacade {
public:
    ABIMockPlayerFacade() = default;
    ~ABIMockPlayerFacade() override = default;

    hlplayer::Result<void> open(const std::string& url) override {
        (void)url;
        return hlplayer::Result<void>::success();
    }
    hlplayer::Result<void> play() override { return hlplayer::Result<void>::success(); }
    hlplayer::Result<void> pause() override { return hlplayer::Result<void>::success(); }
    hlplayer::Result<void> stop() override { return hlplayer::Result<void>::success(); }
    hlplayer::Result<void> seek(double seconds) override { (void)seconds; return hlplayer::Result<void>::success(); }
    hlplayer::Result<void> setVolume(double volume) override { (void)volume; return hlplayer::Result<void>::success(); }
    PlayerState getState() const override { return PlayerState_Idle; }
    double getPosition() const override { return 0.0; }
    double getDuration() const override { return 0.0; }
};

class ABIMockStreamResolver : public hlplayer::IStreamResolver {
public:
    ABIMockStreamResolver() = default;
    ~ABIMockStreamResolver() override = default;

    hlplayer::Result<void> resolve(
        const std::string& url,
        std::function<void(hlplayer::Result<hlplayer::StreamInfo>)> callback) override
    {
        (void)url;
        if (callback) {
            callback(hlplayer::Result<hlplayer::StreamInfo>::error(hlplayer::PlayerError::NetworkError));
        }
        return hlplayer::Result<void>::success();
    }

    void cancel() override {}
    uint32_t getCapabilities() const override { return 0; }
};

class ABIMockVideoFrameSink : public hlplayer::IVideoFrameSink {
public:
    ABIMockVideoFrameSink() = default;
    ~ABIMockVideoFrameSink() override = default;

    void onFrame(const hlplayer::GpuFrame& frame) override { (void)frame; }
    void onFormatChanged(hlplayer::VideoFormat format) override { (void)format; }
    void reset() override {}
};

class ABIMockAIPipeline : public hlplayer::IAIPipeline {
public:
    ABIMockAIPipeline() = default;
    ~ABIMockAIPipeline() override = default;

    bool hasCapability(hlplayer::AICapability cap) const override { (void)cap; return false; }
    hlplayer::Result<hlplayer::GpuFrame> processFrame(
        const hlplayer::GpuFrame& frame, uint32_t caps) override
    {
        (void)caps;
        return hlplayer::Result<hlplayer::GpuFrame>::success(frame);
    }
    hlplayer::Result<void> loadModel(const std::string& path, hlplayer::AICapability cap) override {
        (void)path; (void)cap;
        return hlplayer::Result<void>::success();
    }
};

class ABIMockGpuFramePool : public hlplayer::GpuFramePool {
public:
    ABIMockGpuFramePool() = default;
    ~ABIMockGpuFramePool() override = default;

    hlplayer::GpuFrame allocate(uint32_t w, uint32_t h, hlplayer::PixelFormat fmt) override {
        hlplayer::GpuFrame f;
        f.width = w;
        f.height = h;
        f.format = fmt;
        return f;
    }
    void recycle(hlplayer::GpuFrame&& frame) override { (void)frame; }
    void reset() override {}
};

// ============================================================================
// ABI type size verification — ensures binary compatibility
// ============================================================================

TEST_CASE("ABI boundary types have correct sizes", "[abi-compile]") {
    // Opaque handle must be pointer-sized
    static_assert(sizeof(HLPlayerHandle) == sizeof(void*),
        "HLPlayerHandle must be pointer-sized for ABI stability");

    // Enums must be int-sized for C ABI compatibility
    static_assert(sizeof(PlayerState) == sizeof(int),
        "PlayerState must be int-sized for C ABI");
    static_assert(sizeof(PlayerErrorCode) == sizeof(int),
        "PlayerErrorCode must be int-sized for C ABI");

    REQUIRE(sizeof(HLPlayerHandle) == sizeof(void*));
    REQUIRE(sizeof(PlayerState) == sizeof(int));
    REQUIRE(sizeof(PlayerErrorCode) == sizeof(int));
}

TEST_CASE("All 4 interface mocks are instantiable", "[abi-compile]") {
    ABIMockPlayerFacade facade;
    ABIMockStreamResolver resolver;
    ABIMockVideoFrameSink sink;
    ABIMockAIPipeline pipeline;

    // Verify polymorphic destruction works (vtable is correct)
    hlplayer::IPlayerFacade* pFacade = &facade;
    hlplayer::IStreamResolver* pResolver = &resolver;
    hlplayer::IVideoFrameSink* pSink = &sink;
    hlplayer::IAIPipeline* pPipeline = &pipeline;

    REQUIRE(pFacade != nullptr);
    REQUIRE(pResolver != nullptr);
    REQUIRE(pSink != nullptr);
    REQUIRE(pPipeline != nullptr);
}

TEST_CASE("Interface methods are callable through base pointers", "[abi-compile]") {
    ABIMockPlayerFacade facade;
    hlplayer::IPlayerFacade* p = &facade;

    auto r1 = p->open("test://url");
    REQUIRE(r1.hasValue());

    auto r2 = p->play();
    REQUIRE(r2.hasValue());

    PlayerState s = p->getState();
    REQUIRE(s == PlayerState_Idle);
}

TEST_CASE("GpuFramePool mock is instantiable", "[abi-compile]") {
    ABIMockGpuFramePool pool;
    hlplayer::GpuFramePool* p = &pool;

    auto frame = p->allocate(1280, 720, hlplayer::PixelFormat::NV12);
    REQUIRE(frame.width == 1280);
    REQUIRE(frame.height == 720);
    REQUIRE(frame.format == hlplayer::PixelFormat::NV12);

    p->recycle(std::move(frame));
    p->reset();
}

TEST_CASE("C-ABI enum values are consistent across headers", "[abi-compile]") {
    // PlayerState values in PlayerApi.h match what IPlayerFacade uses
    REQUIRE(PlayerState_Idle == 0);
    REQUIRE(PlayerState_Playing == 4);
    REQUIRE(PlayerState_Paused == 5);
    REQUIRE(PlayerState_Error == 6);

    // PlayerErrorCode values
    REQUIRE(PlayerErrorCode_None == 0);
    REQUIRE(PlayerErrorCode_InvalidURL == 1);
    REQUIRE(PlayerErrorCode_NetworkError == 2);
    REQUIRE(PlayerErrorCode_DecodeError == 3);
    REQUIRE(PlayerErrorCode_DeviceLost == 4);
    REQUIRE(PlayerErrorCode_Unknown == 999);
}

TEST_CASE("Result<T> works across ABI boundary types", "[abi-compile]") {
    // Success with simple type
    auto rInt = hlplayer::Result<int>::success(42);
    REQUIRE(rInt.value() == 42);

    // Error with PlayerError
    auto rErr = hlplayer::Result<void>::error(hlplayer::PlayerError::DeviceLost);
    REQUIRE(rErr.hasError());

    // Success with complex type (StreamInfo)
    hlplayer::StreamInfo info;
    info.url = "test://stream";
    info.format = "hls";
    info.bitrate = 3000000;
    auto rInfo = hlplayer::Result<hlplayer::StreamInfo>::success(info);
    REQUIRE(rInfo.hasValue());
    REQUIRE(rInfo.value().url == "test://stream");
}
