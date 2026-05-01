#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "RenderBridge.h"
#include "VideoSink.h"
#include "VulkanSink.h"
#include "ColorConverter.h"
#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/GpuFrameContract.h>
#include <vector>
#include <memory>
#include <cstdint>

using Catch::Approx;

// ============================================================================
// FrameSinkAdapter tests
// ============================================================================

namespace {

class MockRenderBridge : public hlplayer::render::IRenderBridge {
public:
    hlplayer::GpuFrame lastFrame{};
    hlplayer::VideoFormat lastFormat = hlplayer::VideoFormat::Unknown;
    int presentCount = 0;
    int formatChangeCount = 0;
    int resetCount = 0;

    void presentFrame(const hlplayer::GpuFrame& frame) override {
        lastFrame = frame;
        ++presentCount;
    }

    void onFormatChange(hlplayer::VideoFormat format) override {
        lastFormat = format;
        ++formatChangeCount;
    }

    void reset() override {
        ++resetCount;
    }
};

} // anonymous namespace

TEST_CASE("FrameSinkAdapter delegates frames to IRenderBridge", "[render][videosink]") {
    auto bridge = std::make_shared<MockRenderBridge>();
    hlplayer::render::FrameSinkAdapter adapter(bridge);

    SECTION("onFrame forwards GpuFrame to bridge") {
        hlplayer::GpuFrame frame{};
        frame.width = 1920;
        frame.height = 1080;
        frame.format = hlplayer::PixelFormat::NV12;
        frame.timestamp = 1.5;

        adapter.onFrame(frame);

        REQUIRE(bridge->presentCount == 1);
        REQUIRE(bridge->lastFrame.width == 1920);
        REQUIRE(bridge->lastFrame.height == 1080);
        REQUIRE(bridge->lastFrame.timestamp == Approx(1.5));
    }

    SECTION("onFormatChanged delegates to bridge") {
        adapter.onFormatChanged(hlplayer::VideoFormat::NV12);
        REQUIRE(bridge->formatChangeCount == 1);
        REQUIRE(bridge->lastFormat == hlplayer::VideoFormat::NV12);
    }

    SECTION("reset delegates to bridge") {
        adapter.reset();
        REQUIRE(bridge->resetCount == 1);
    }

    SECTION("multiple frames are all forwarded") {
        hlplayer::GpuFrame f1{};
        f1.timestamp = 0.0;
        hlplayer::GpuFrame f2{};
        f2.timestamp = 0.033;

        adapter.onFrame(f1);
        adapter.onFrame(f2);

        REQUIRE(bridge->presentCount == 2);
    }

    SECTION("getBridge returns the shared pointer") {
        REQUIRE(adapter.getBridge().get() == bridge.get());
    }
}

TEST_CASE("FrameSinkAdapter with null bridge does not crash", "[render][videosink]") {
    hlplayer::render::FrameSinkAdapter adapter(nullptr);

    SECTION("onFrame with null bridge") {
        hlplayer::GpuFrame frame{};
        REQUIRE_NOTHROW(adapter.onFrame(frame));
    }

    SECTION("onFormatChanged with null bridge") {
        REQUIRE_NOTHROW(adapter.onFormatChanged(hlplayer::VideoFormat::RGBA8));
    }

    SECTION("reset with null bridge") {
        REQUIRE_NOTHROW(adapter.reset());
    }
}

// ============================================================================
// VulkanSink tests
// ============================================================================

TEST_CASE("VulkanSink construction and configuration", "[render][vulkan]") {
    SECTION("default construction does not throw") {
        REQUIRE_NOTHROW(hlplayer::render::VulkanSink());
    }

    SECTION("no device error after construction") {
        hlplayer::render::VulkanSink sink;
        REQUIRE_FALSE(sink.hasDeviceError());
    }

    SECTION("reset clears state") {
        hlplayer::render::VulkanSink sink;
        REQUIRE_NOTHROW(sink.reset());
        REQUIRE_FALSE(sink.hasDeviceError());
    }
}

TEST_CASE("VulkanSink presents frames without error", "[render][vulkan]") {
    hlplayer::render::VulkanSink sink;

    SECTION("presentFrame accepts valid frame") {
        hlplayer::GpuFrame frame{};
        frame.width = 640;
        frame.height = 480;
        frame.format = hlplayer::PixelFormat::Vulkan;

        REQUIRE_NOTHROW(sink.presentFrame(frame));
        REQUIRE_FALSE(sink.hasDeviceError());
    }

    SECTION("onFormatChange does not throw") {
        REQUIRE_NOTHROW(sink.onFormatChange(hlplayer::VideoFormat::RGBA8));
    }
}

TEST_CASE("VulkanSink device lost detection and recovery", "[render][vulkan]") {
    hlplayer::render::VulkanSink sink;

    SECTION("frame with deviceLost flag triggers error state") {
        hlplayer::GpuFrame frame{};
        frame.deviceLost = true;
        frame.timestamp = 5.0;

        sink.presentFrame(frame);
        REQUIRE(sink.hasDeviceError());
    }

    SECTION("device lost invokes recovery callback") {
        bool callbackCalled = false;
        sink.setDeviceLostCallback([&callbackCalled]() {
            callbackCalled = true;
        });

        hlplayer::GpuFrame frame{};
        frame.deviceLost = true;

        sink.presentFrame(frame);
        REQUIRE(callbackCalled);
    }

    SECTION("frames are dropped after device lost") {
        hlplayer::GpuFrame lostFrame{};
        lostFrame.deviceLost = true;
        sink.presentFrame(lostFrame);
        REQUIRE(sink.hasDeviceError());

        hlplayer::GpuFrame normalFrame{};
        normalFrame.width = 320;
        normalFrame.height = 240;
        sink.presentFrame(normalFrame);

        REQUIRE(sink.hasDeviceError());
    }

    SECTION("clearDeviceError resets error state") {
        hlplayer::GpuFrame frame{};
        frame.deviceLost = true;
        sink.presentFrame(frame);
        REQUIRE(sink.hasDeviceError());

        sink.clearDeviceError();
        REQUIRE_FALSE(sink.hasDeviceError());
    }

    SECTION("reset clears device lost state") {
        hlplayer::GpuFrame frame{};
        frame.deviceLost = true;
        sink.presentFrame(frame);

        sink.reset();
        REQUIRE_FALSE(sink.hasDeviceError());
    }

    SECTION("callback is not invoked for normal frames") {
        int callCount = 0;
        sink.setDeviceLostCallback([&callCount]() { ++callCount; });

        hlplayer::GpuFrame frame{};
        frame.width = 100;
        frame.height = 100;
        sink.presentFrame(frame);

        REQUIRE(callCount == 0);
    }
}

// ============================================================================
// ColorConverter tests
// ============================================================================

TEST_CASE("ColorConverter returns correct BT.601 Kr/Kb coefficients", "[render][color]") {
    REQUIRE(hlplayer::render::ColorConverter::bt601Kr() == Approx(0.299f));
    REQUIRE(hlplayer::render::ColorConverter::bt601Kb() == Approx(0.114f));
}

TEST_CASE("ColorConverter returns correct BT.709 Kr/Kb coefficients", "[render][color]") {
    REQUIRE(hlplayer::render::ColorConverter::bt709Kr() == Approx(0.2126f));
    REQUIRE(hlplayer::render::ColorConverter::bt709Kb() == Approx(0.0722f));
}

TEST_CASE("ColorConverter returns correct BT.2020 Kr/Kb coefficients", "[render][color]") {
    REQUIRE(hlplayer::render::ColorConverter::bt2020Kr() == Approx(0.2627f));
    REQUIRE(hlplayer::render::ColorConverter::bt2020Kb() == Approx(0.0593f));
}

TEST_CASE("ColorConverter BT.709 limited range matrix", "[render][color]") {
    auto m = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Limited);

    const float sy = 255.0f / 219.0f;
    REQUIRE(m.yCoeff[0] == Approx(sy));
    REQUIRE(m.yCoeff[1] == Approx(sy));
    REQUIRE(m.yCoeff[2] == Approx(sy));
    REQUIRE(m.crCoeff[0] == Approx(2.0f * (1.0f - 0.2126f) * (255.0f / 224.0f)));
    REQUIRE(m.crCoeff[0] > 1.5f);
}

TEST_CASE("ColorConverter BT.709 full range matrix", "[render][color]") {
    auto m = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Full);

    REQUIRE(m.yCoeff[0] == Approx(1.0f));
    REQUIRE(m.yCoeff[1] == Approx(1.0f));
    REQUIRE(m.yCoeff[2] == Approx(1.0f));
    REQUIRE(m.crCoeff[0] == Approx(2.0f * (1.0f - 0.2126f)));
    REQUIRE(m.crCoeff[0] == Approx(1.5748f).margin(0.01f));
}

TEST_CASE("ColorConverter BT.601 matrix differs from BT.709", "[render][color]") {
    auto m601 = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT601, hlplayer::ColorRange::Limited);
    auto m709 = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Limited);

    REQUIRE(m601.crCoeff[0] != Approx(m709.crCoeff[0]));
}

TEST_CASE("ColorConverter BT.2020 matrix has wider gamut coefficients", "[render][color]") {
    auto m2020 = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT2020, hlplayer::ColorRange::Limited);

    const float sc = 255.0f / 224.0f;
    REQUIRE(m2020.crCoeff[0] == Approx(2.0f * (1.0f - 0.2627f) * sc).margin(0.01f));
}

TEST_CASE("ColorConverter sRGB uses BT.709 coefficients", "[render][color]") {
    auto mSrgb = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::sRGB, hlplayer::ColorRange::Full);
    auto m709 = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Full);

    for (int i = 0; i < 3; ++i) {
        REQUIRE(mSrgb.yCoeff[i] == Approx(m709.yCoeff[i]));
        REQUIRE(mSrgb.crCoeff[i] == Approx(m709.crCoeff[i]).margin(0.001f));
        REQUIRE(mSrgb.cbCoeff[i] == Approx(m709.cbCoeff[i]).margin(0.001f));
    }
}

TEST_CASE("ColorConverter limited range Y coefficient is scaled", "[render][color]") {
    auto mLimited = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Limited);
    auto mFull = hlplayer::render::ColorConverter::getMatrix(
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Full);

    const float sy = 255.0f / 219.0f;
    REQUIRE(mLimited.yCoeff[0] == Approx(sy));
    REQUIRE(mFull.yCoeff[0] == Approx(1.0f));
    REQUIRE(mLimited.yCoeff[0] > mFull.yCoeff[0]);
}

TEST_CASE("ColorConverter convertYUV420 black pixel produces near-zero RGB", "[render][color]") {
    constexpr uint32_t w = 4, h = 4;
    std::vector<uint8_t> yPlane(w * h, 16);   // limited-range black Y=16
    std::vector<uint8_t> uPlane(w * h / 4, 128);
    std::vector<uint8_t> vPlane(w * h / 4, 128);
    std::vector<uint8_t> rgb(w * h * 3, 0xFF);

    hlplayer::render::ColorConverter::convertYUV420(
        yPlane.data(), uPlane.data(), vPlane.data(),
        w, h, w, w / 2,
        rgb.data(), w * 3,
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Limited);

    for (uint32_t i = 0; i < w * h; ++i) {
        REQUIRE(rgb[i * 3] < 5);
        REQUIRE(rgb[i * 3 + 1] < 5);
        REQUIRE(rgb[i * 3 + 2] < 5);
    }
}

TEST_CASE("ColorConverter convertYUV420 white pixel produces near-255 RGB", "[render][color]") {
    constexpr uint32_t w = 4, h = 4;
    std::vector<uint8_t> yPlane(w * h, 235);  // limited-range white Y=235
    std::vector<uint8_t> uPlane(w * h / 4, 128);
    std::vector<uint8_t> vPlane(w * h / 4, 128);
    std::vector<uint8_t> rgb(w * h * 3, 0);

    hlplayer::render::ColorConverter::convertYUV420(
        yPlane.data(), uPlane.data(), vPlane.data(),
        w, h, w, w / 2,
        rgb.data(), w * 3,
        hlplayer::ColorSpace::BT709, hlplayer::ColorRange::Limited);

    for (uint32_t i = 0; i < w * h; ++i) {
        REQUIRE(rgb[i * 3] > 250);
        REQUIRE(rgb[i * 3 + 1] > 250);
        REQUIRE(rgb[i * 3 + 2] > 250);
    }
}

// ============================================================================
// RenderBridge (existing public API) backward compatibility
// ============================================================================

TEST_CASE("RenderBridge basic operations", "[render]") {
    hlplayer::render::RenderBridge bridge;

    SECTION("RenderBridge can be constructed") {
        REQUIRE_NOTHROW(hlplayer::render::RenderBridge());
    }

    SECTION("RenderBridge can be initialized") {
        REQUIRE_NOTHROW(bridge.initialize());
    }

    SECTION("RenderBridge can render") {
        bridge.initialize();
        REQUIRE_NOTHROW(bridge.render());
    }
}
