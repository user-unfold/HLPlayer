#include <catch2/catch_test_macros.hpp>
#include "VulkanVideoSink.h"
#include <hlplayer/GpuFrameContract.h>
#include <memory>

using namespace hlplayer;
using namespace hlplayer::render;

TEST_CASE("VulkanVideoSink construction", "[renderbridge][vulkanvideosink]") {
    SECTION("default construction does not throw") {
        REQUIRE_NOTHROW(VulkanVideoSink());
    }

    SECTION("no device error after construction") {
        VulkanVideoSink sink;
        REQUIRE_FALSE(sink.isDeviceLost());
    }

    SECTION("no frame available after construction") {
        VulkanVideoSink sink;
        REQUIRE_FALSE(sink.hasFrame());
    }

    SECTION("initial format is Unknown") {
        VulkanVideoSink sink;
        REQUIRE(sink.getFormat() == VideoFormat::Unknown);
    }
}

TEST_CASE("VulkanVideoSink onFrame reception", "[renderbridge][vulkanvideosink]") {
    VulkanVideoSink sink;

    SECTION("accepts valid GpuFrame") {
        GpuFrame frame{};
        frame.width = 1920;
        frame.height = 1080;
        frame.format = PixelFormat::Vulkan;
        frame.handle.nativeHandle = reinterpret_cast<void*>(0x1234);
        frame.handle.auxiliaryHandle = reinterpret_cast<void*>(0x5678);
        frame.timestamp = 1.5;

        REQUIRE_NOTHROW(sink.onFrame(frame));
        REQUIRE(sink.hasFrame());
    }

    SECTION("stores VkImage handle correctly") {
        GpuFrame frame{};
        frame.width = 640;
        frame.height = 480;
        frame.format = PixelFormat::Vulkan;
        frame.handle.nativeHandle = reinterpret_cast<void*>(0xDEADBEEF);

        sink.onFrame(frame);
        REQUIRE(sink.getVkImage() == reinterpret_cast<void*>(0xDEADBEEF));
    }

    SECTION("stores auxiliary handle correctly") {
        GpuFrame frame{};
        frame.width = 640;
        frame.height = 480;
        frame.format = PixelFormat::Vulkan;
        frame.handle.auxiliaryHandle = reinterpret_cast<void*>(0xF00BA4);

        sink.onFrame(frame);
        REQUIRE(sink.getAuxiliaryHandle() == reinterpret_cast<void*>(0xF00BA4));
    }

    SECTION("stores frame dimensions correctly") {
        GpuFrame frame{};
        frame.width = 1280;
        frame.height = 720;
        frame.format = PixelFormat::Vulkan;

        sink.onFrame(frame);
        uint32_t w, h;
        sink.getFrameSize(w, h);
        REQUIRE(w == 1280);
        REQUIRE(h == 720);
    }

    SECTION("multiple frames replace previous ones") {
        GpuFrame frame1{};
        frame1.handle.nativeHandle = reinterpret_cast<void*>(0x1111);
        frame1.width = 800;
        frame1.height = 600;

        sink.onFrame(frame1);
        REQUIRE(sink.getVkImage() == reinterpret_cast<void*>(0x1111));

        GpuFrame frame2{};
        frame2.handle.nativeHandle = reinterpret_cast<void*>(0x2222);
        frame2.width = 1920;
        frame2.height = 1080;

        sink.onFrame(frame2);
        REQUIRE(sink.getVkImage() == reinterpret_cast<void*>(0x2222));

        uint32_t w, h;
        sink.getFrameSize(w, h);
        REQUIRE(w == 1920);
        REQUIRE(h == 1080);
    }
}

TEST_CASE("VulkanVideoSink onFormatChanged", "[renderbridge][vulkanvideosink]") {
    VulkanVideoSink sink;

    SECTION("changes format from Unknown") {
        REQUIRE(sink.getFormat() == VideoFormat::Unknown);
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::NV12));
        REQUIRE(sink.getFormat() == VideoFormat::NV12);
    }

    SECTION("changes format multiple times") {
        sink.onFormatChanged(VideoFormat::NV12);
        REQUIRE(sink.getFormat() == VideoFormat::NV12);

        sink.onFormatChanged(VideoFormat::P010);
        REQUIRE(sink.getFormat() == VideoFormat::P010);

        sink.onFormatChanged(VideoFormat::RGBA8);
        REQUIRE(sink.getFormat() == VideoFormat::RGBA8);
    }

    SECTION("accepts all VideoFormat values") {
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::Unknown));
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::NV12));
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::P010));
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::RGBA8));
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::RGBA16F));
    }
}

TEST_CASE("VulkanVideoSink device lost handling", "[renderbridge][vulkanvideosink]") {
    VulkanVideoSink sink;

    SECTION("frame with deviceLost flag sets error state") {
        GpuFrame frame{};
        frame.deviceLost = true;
        frame.timestamp = 5.0;

        sink.onFrame(frame);
        REQUIRE(sink.isDeviceLost());
    }

    SECTION("getVkImage returns nullptr after device lost") {
        GpuFrame normalFrame{};
        normalFrame.handle.nativeHandle = reinterpret_cast<void*>(0xAAAA);
        normalFrame.width = 800;
        normalFrame.height = 600;

        sink.onFrame(normalFrame);
        REQUIRE(sink.getVkImage() == reinterpret_cast<void*>(0xAAAA));

        GpuFrame lostFrame{};
        lostFrame.deviceLost = true;
        sink.onFrame(lostFrame);

        REQUIRE(sink.isDeviceLost());
        REQUIRE(sink.getVkImage() == nullptr);
    }

    SECTION("getAuxiliaryHandle returns nullptr after device lost") {
        GpuFrame normalFrame{};
        normalFrame.handle.auxiliaryHandle = reinterpret_cast<void*>(0xBBBB);

        sink.onFrame(normalFrame);
        REQUIRE(sink.getAuxiliaryHandle() == reinterpret_cast<void*>(0xBBBB));

        GpuFrame lostFrame{};
        lostFrame.deviceLost = true;
        sink.onFrame(lostFrame);

        REQUIRE(sink.isDeviceLost());
        REQUIRE(sink.getAuxiliaryHandle() == nullptr);
    }

    SECTION("frames are dropped after device lost") {
        GpuFrame lostFrame{};
        lostFrame.deviceLost = true;
        sink.onFrame(lostFrame);
        REQUIRE(sink.isDeviceLost());

        GpuFrame normalFrame{};
        normalFrame.handle.nativeHandle = reinterpret_cast<void*>(0xCCCC);
        normalFrame.width = 320;
        normalFrame.height = 240;

        sink.onFrame(normalFrame);
        REQUIRE(sink.isDeviceLost());
        REQUIRE(sink.getVkImage() == nullptr);
    }
}

TEST_CASE("VulkanVideoSink reset functionality", "[renderbridge][vulkanvideosink]") {
    VulkanVideoSink sink;

    SECTION("reset clears frame availability") {
        GpuFrame frame{};
        frame.handle.nativeHandle = reinterpret_cast<void*>(0x1234);
        frame.width = 640;
        frame.height = 480;

        sink.onFrame(frame);
        REQUIRE(sink.hasFrame());

        sink.reset();
        REQUIRE_FALSE(sink.hasFrame());
    }

    SECTION("reset clears handles") {
        GpuFrame frame{};
        frame.handle.nativeHandle = reinterpret_cast<void*>(0x5678);
        frame.handle.auxiliaryHandle = reinterpret_cast<void*>(0x9ABC);

        sink.onFrame(frame);
        REQUIRE(sink.getVkImage() != nullptr);
        REQUIRE(sink.getAuxiliaryHandle() != nullptr);

        sink.reset();
        REQUIRE(sink.getVkImage() == nullptr);
        REQUIRE(sink.getAuxiliaryHandle() == nullptr);
    }

    SECTION("reset clears frame size") {
        GpuFrame frame{};
        frame.width = 1920;
        frame.height = 1080;

        sink.onFrame(frame);

        uint32_t w, h;
        sink.getFrameSize(w, h);
        REQUIRE(w == 1920);
        REQUIRE(h == 1080);

        sink.reset();

        sink.getFrameSize(w, h);
        REQUIRE(w == 0);
        REQUIRE(h == 0);
    }

    SECTION("reset clears format") {
        sink.onFormatChanged(VideoFormat::NV12);
        REQUIRE(sink.getFormat() == VideoFormat::NV12);

        sink.reset();
        REQUIRE(sink.getFormat() == VideoFormat::Unknown);
    }

    SECTION("reset clears device lost state") {
        GpuFrame frame{};
        frame.deviceLost = true;

        sink.onFrame(frame);
        REQUIRE(sink.isDeviceLost());

        sink.reset();
        REQUIRE_FALSE(sink.isDeviceLost());
    }

    SECTION("reset after format change and frame receipt") {
        sink.onFormatChanged(VideoFormat::RGBA16F);

        GpuFrame frame{};
        frame.handle.nativeHandle = reinterpret_cast<void*>(0x1111);
        frame.width = 800;
        frame.height = 600;

        sink.onFrame(frame);
        REQUIRE(sink.getFormat() == VideoFormat::RGBA16F);
        REQUIRE(sink.hasFrame());
        REQUIRE(sink.getVkImage() == reinterpret_cast<void*>(0x1111));

        sink.reset();
        REQUIRE(sink.getFormat() == VideoFormat::Unknown);
        REQUIRE_FALSE(sink.hasFrame());
        REQUIRE(sink.getVkImage() == nullptr);
    }
}

TEST_CASE("VulkanVideoSink Qt RHI integration", "[renderbridge][vulkanvideosink]") {
    VulkanVideoSink sink;

    SECTION("setRhiHandle accepts void pointer") {
        void* rhi = reinterpret_cast<void*>(0xABCDEF);
        REQUIRE_NOTHROW(sink.setRhiHandle(rhi));
    }

    SECTION("setRhiHandle with nullptr") {
        REQUIRE_NOTHROW(sink.setRhiHandle(nullptr));
    }

    SECTION("multiple setRhiHandle calls") {
        void* rhi1 = reinterpret_cast<void*>(0x1111);
        void* rhi2 = reinterpret_cast<void*>(0x2222);

        REQUIRE_NOTHROW(sink.setRhiHandle(rhi1));
        REQUIRE_NOTHROW(sink.setRhiHandle(rhi2));
    }
}

#ifdef __APPLE__
TEST_CASE("VulkanVideoSink macOS MoltenVK support", "[renderbridge][vulkanvideosink][macos]") {
    VulkanVideoSink sink;

    SECTION("isMoltenVKAvailable returns true") {
        REQUIRE(sink.isMoltenVKAvailable());
    }

    SECTION("setMoltenVKFallback accepts boolean") {
        REQUIRE_NOTHROW(sink.setMoltenVKFallback(false));
        REQUIRE_NOTHROW(sink.setMoltenVKFallback(true));
    }

    SECTION("toggle MoltenVK fallback") {
        sink.setMoltenVKFallback(true);
        REQUIRE_NOTHROW(sink.setMoltenVKFallback(false));
        REQUIRE_NOTHROW(sink.setMoltenVKFallback(true));
    }

    SECTION("MoltenVK fallback with frame reception") {
        sink.setMoltenVKFallback(true);

        GpuFrame frame{};
        frame.width = 640;
        frame.height = 480;
        frame.format = PixelFormat::Vulkan;

        REQUIRE_NOTHROW(sink.onFrame(frame));
    }
}
#endif

TEST_CASE("VulkanVideoSink edge cases", "[renderbridge][vulkanvideosink]") {
    SECTION("frame with zero dimensions") {
        VulkanVideoSink sink;

        GpuFrame frame{};
        frame.width = 0;
        frame.height = 0;
        frame.format = PixelFormat::Vulkan;

        REQUIRE_NOTHROW(sink.onFrame(frame));
        REQUIRE(sink.hasFrame());

        uint32_t w, h;
        sink.getFrameSize(w, h);
        REQUIRE(w == 0);
        REQUIRE(h == 0);
    }

    SECTION("frame with null native handle") {
        VulkanVideoSink sink;

        GpuFrame frame{};
        frame.handle.nativeHandle = nullptr;
        frame.width = 800;
        frame.height = 600;

        REQUIRE_NOTHROW(sink.onFrame(frame));
        REQUIRE(sink.getVkImage() == nullptr);
    }

    SECTION("frame with null auxiliary handle") {
        VulkanVideoSink sink;

        GpuFrame frame{};
        frame.handle.auxiliaryHandle = nullptr;
        frame.width = 800;
        frame.height = 600;

        REQUIRE_NOTHROW(sink.onFrame(frame));
        REQUIRE(sink.getAuxiliaryHandle() == nullptr);
    }

    SECTION("reset on empty sink") {
        VulkanVideoSink sink;
        REQUIRE_NOTHROW(sink.reset());
        REQUIRE_FALSE(sink.hasFrame());
        REQUIRE_FALSE(sink.isDeviceLost());
    }

    SECTION("getFrameSize on empty sink") {
        VulkanVideoSink sink;
        uint32_t w, h;
        sink.getFrameSize(w, h);
        REQUIRE(w == 0);
        REQUIRE(h == 0);
    }

    SECTION("onFormatChanged with Unknown format") {
        VulkanVideoSink sink;
        REQUIRE_NOTHROW(sink.onFormatChanged(VideoFormat::Unknown));
        REQUIRE(sink.getFormat() == VideoFormat::Unknown);
    }
}

TEST_CASE("VulkanVideoSink lifecycle", "[renderbridge][vulkanvideosink]") {
    SECTION("full lifecycle: construct, frame, format, reset, destruct") {
        {
            VulkanVideoSink sink;

            sink.onFormatChanged(VideoFormat::NV12);

            GpuFrame frame{};
            frame.handle.nativeHandle = reinterpret_cast<void*>(0x1234);
            frame.handle.auxiliaryHandle = reinterpret_cast<void*>(0x5678);
            frame.width = 1920;
            frame.height = 1080;
            frame.timestamp = 0.5;

            sink.onFrame(frame);

            REQUIRE(sink.hasFrame());
            REQUIRE(sink.getFormat() == VideoFormat::NV12);
            REQUIRE(sink.getVkImage() == reinterpret_cast<void*>(0x1234));
            REQUIRE(sink.getAuxiliaryHandle() == reinterpret_cast<void*>(0x5678));

            sink.reset();

            REQUIRE_FALSE(sink.hasFrame());
            REQUIRE(sink.getFormat() == VideoFormat::Unknown);
        }

        REQUIRE(true);
    }

    SECTION("multiple sink instances") {
        VulkanVideoSink sink1;
        VulkanVideoSink sink2;

        GpuFrame frame1{};
        frame1.handle.nativeHandle = reinterpret_cast<void*>(0x1111);
        frame1.width = 640;
        frame1.height = 480;

        GpuFrame frame2{};
        frame2.handle.nativeHandle = reinterpret_cast<void*>(0x2222);
        frame2.width = 1280;
        frame2.height = 720;

        sink1.onFrame(frame1);
        sink2.onFrame(frame2);

        REQUIRE(sink1.getVkImage() == reinterpret_cast<void*>(0x1111));
        REQUIRE(sink2.getVkImage() == reinterpret_cast<void*>(0x2222));

        uint32_t w1, h1, w2, h2;
        sink1.getFrameSize(w1, h1);
        sink2.getFrameSize(w2, h2);

        REQUIRE(w1 == 640);
        REQUIRE(h1 == 480);
        REQUIRE(w2 == 1280);
        REQUIRE(h2 == 720);
    }
}
