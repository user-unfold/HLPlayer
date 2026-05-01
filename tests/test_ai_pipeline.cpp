#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <hlplayer/AIPipelineStub.h>
#include <hlplayer/NCNNInterop.h>
#include <hlplayer/QoSManager.h>
#include <hlplayer/IAIPipeline.h>
#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>

TEST_CASE("AIPipelineStub - construction has no capabilities", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;

    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::FrameInterpolation));
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::HDR));
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::ToneMapping));
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::NoiseReduction));
}

TEST_CASE("AIPipelineStub - loadModel enables capability", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;

    auto result = pipeline.loadModel("/models/sr.bin", hlplayer::AICapability::SuperResolution);
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
}

TEST_CASE("AIPipelineStub - loadModel with empty path returns error", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;

    auto result = pipeline.loadModel("", hlplayer::AICapability::SuperResolution);
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::InvalidState);
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
}

TEST_CASE("AIPipelineStub - processFrame returns error when no model loaded", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;
    hlplayer::GpuFrame frame{};

    auto result = pipeline.processFrame(frame, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::UnsupportedFormat);
}

TEST_CASE("AIPipelineStub - processFrame returns frame when model loaded", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;

    pipeline.loadModel("/models/sr.bin", hlplayer::AICapability::SuperResolution);

    hlplayer::GpuFrame input{};
    input.width = 1920;
    input.height = 1080;
    input.format = hlplayer::PixelFormat::RGBA8;

    auto result = pipeline.processFrame(input, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasValue());
    REQUIRE(result.value().width == 1920);
    REQUIRE(result.value().height == 1080);
    REQUIRE(result.value().format == hlplayer::PixelFormat::RGBA8);
}

TEST_CASE("AIPipelineStub - multiple capabilities can be loaded", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;

    pipeline.loadModel("/models/sr.bin", hlplayer::AICapability::SuperResolution);
    pipeline.loadModel("/models/ni.bin", hlplayer::AICapability::NoiseReduction);

    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::NoiseReduction));
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::HDR));
}

TEST_CASE("AIPipelineStub - processFrame matches any loaded capability", "[ai_pipeline]") {
    hlplayer::AIPipelineStub pipeline;

    pipeline.loadModel("/models/sr.bin", hlplayer::AICapability::SuperResolution);

    hlplayer::GpuFrame frame{};

    uint32_t caps = static_cast<uint32_t>(hlplayer::AICapability::SuperResolution)
                  | static_cast<uint32_t>(hlplayer::AICapability::HDR);

    auto result = pipeline.processFrame(frame, caps);
    REQUIRE(result.hasValue());
}

TEST_CASE("QoSManager - initial state is Stable", "[qos]") {
    hlplayer::QoSManager qos;

    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Stable);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::High);
}

TEST_CASE("QoSManager - Stable to Buffering transition", "[qos]") {
    hlplayer::QoSManager qos;

    hlplayer::NetworkMetrics m{};
    m.latencyMs = 250.0;
    qos.updateMetrics(m);

    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Buffering);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::Medium);
}

TEST_CASE("QoSManager - Buffering to Degraded transition", "[qos]") {
    hlplayer::QoSManager qos;

    hlplayer::NetworkMetrics m1{};
    m1.latencyMs = 300.0;
    qos.updateMetrics(m1);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Buffering);

    hlplayer::NetworkMetrics m2{};
    m2.latencyMs = 600.0;
    qos.updateMetrics(m2);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Degraded);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::Low);
}

TEST_CASE("QoSManager - Degraded to Recovering transition", "[qos]") {
    hlplayer::QoSManager qos;

    hlplayer::NetworkMetrics m1{};
    m1.latencyMs = 600.0;
    qos.updateMetrics(m1);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Degraded);

    hlplayer::NetworkMetrics m2{};
    m2.latencyMs = 100.0;
    qos.updateMetrics(m2);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Recovering);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::Medium);
}

TEST_CASE("QoSManager - Recovering to Stable transition", "[qos]") {
    hlplayer::QoSManager qos;

    hlplayer::NetworkMetrics m1{};
    m1.latencyMs = 600.0;
    qos.updateMetrics(m1);

    hlplayer::NetworkMetrics m2{};
    m2.latencyMs = 100.0;
    qos.updateMetrics(m2);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Recovering);

    hlplayer::NetworkMetrics m3{};
    m3.latencyMs = 50.0;
    qos.updateMetrics(m3);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Stable);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::High);
}

TEST_CASE("QoSManager - good metrics stays Stable", "[qos]") {
    hlplayer::QoSManager qos;

    hlplayer::NetworkMetrics m{};
    m.latencyMs = 50.0;
    m.bandwidthBps = 50'000'000;
    m.packetLossPercent = 0.0;
    m.jitterMs = 5.0;

    qos.updateMetrics(m);
    REQUIRE(qos.getQoSState() == hlplayer::QoSState::Stable);

    auto metrics = qos.getMetrics();
    REQUIRE(metrics.latencyMs == Catch::Approx(50.0));
    REQUIRE(metrics.bandwidthBps == 50'000'000);
}

TEST_CASE("QoSManager - level recommendations per state", "[qos]") {
    hlplayer::QoSManager qos;

    hlplayer::NetworkMetrics stable{};
    stable.latencyMs = 50.0;
    qos.updateMetrics(stable);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::High);

    hlplayer::NetworkMetrics buffering{};
    buffering.latencyMs = 250.0;
    qos.updateMetrics(buffering);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::Medium);

    hlplayer::NetworkMetrics degraded{};
    degraded.latencyMs = 700.0;
    qos.updateMetrics(degraded);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::Low);

    hlplayer::NetworkMetrics recovering{};
    recovering.latencyMs = 150.0;
    qos.updateMetrics(recovering);
    REQUIRE(qos.getRecommendedLevel() == hlplayer::QoSLevel::Medium);
}

TEST_CASE("NCNNInterop - isAvailable returns false without HAS_NCNN", "[ncnn]") {
    hlplayer::NCNNInterop interop;

    REQUIRE_FALSE(interop.isAvailable());
}

TEST_CASE("NCNNInterop - initialize returns error without HAS_NCNN", "[ncnn]") {
    hlplayer::NCNNInterop interop;
    hlplayer::NCNNDeviceConfig config{};

    auto result = interop.initialize(config);
    REQUIRE(result.hasError());
}

TEST_CASE("NCNNInterop - importExternalMemory returns error without HAS_NCNN", "[ncnn]") {
    hlplayer::NCNNInterop interop;
    hlplayer::GpuFrameHandle handle{};

    auto result = interop.importExternalMemory(handle, 1024);
    REQUIRE(result.hasError());
}

// ============================================================================
// NcnnAIPipeline tests
// ============================================================================

#include <hlplayer/NcnnAIPipeline.h>

TEST_CASE("NcnnAIPipeline - construction creates uninitialized pipeline", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;

    REQUIRE_FALSE(pipeline.isReady());
    REQUIRE(pipeline.getQueueSize() == 0);
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
}

TEST_CASE("NcnnAIPipeline - initialize with Vulkan config succeeds", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    config.vulkanInstance = reinterpret_cast<void*>(0x1000);
    config.vulkanPhysicalDevice = reinterpret_cast<void*>(0x2000);
    config.vulkanDevice = reinterpret_cast<void*>(0x3000);
    config.queueFamilyIndex = 0;
    config.enableExternalMemory = true;

    auto result = pipeline.initialize(config);
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.isReady());
}

TEST_CASE("NcnnAIPipeline - initialize twice returns error", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    auto result1 = pipeline.initialize(config);
    REQUIRE(result1.hasValue());

    auto result2 = pipeline.initialize(config);
    REQUIRE(result2.hasError());
}

TEST_CASE("NcnnAIPipeline - shutdown resets pipeline state", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);
    REQUIRE(pipeline.isReady());

    pipeline.shutdown();
    REQUIRE_FALSE(pipeline.isReady());
}

TEST_CASE("NcnnAIPipeline - loadModel enables capability", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);

    auto result = pipeline.loadModel("/models/sr.param", hlplayer::AICapability::SuperResolution);
    REQUIRE(result.hasValue());
    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
}

TEST_CASE("NcnnAIPipeline - loadModel with empty path returns error", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);

    auto result = pipeline.loadModel("", hlplayer::AICapability::SuperResolution);
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::InvalidState);
}

TEST_CASE("NcnnAIPipeline - processFrame returns error when not initialized", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::GpuFrame frame{};

    auto result = pipeline.processFrame(frame, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::InvalidState);
}

TEST_CASE("NcnnAIPipeline - processFrame returns error when no model loaded", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};
    hlplayer::GpuFrame frame{};

    pipeline.initialize(config);

    auto result = pipeline.processFrame(frame, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::UnsupportedFormat);
}

TEST_CASE("NcnnAIPipeline - processFrame returns error for device lost", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);
    pipeline.loadModel("/models/sr.param", hlplayer::AICapability::SuperResolution);

    hlplayer::GpuFrame frame{};
    frame.deviceLost = true;

    auto result = pipeline.processFrame(frame, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::DeviceLost);
}

TEST_CASE("NcnnAIPipeline - processFrame returns frame when model loaded (pass-through)", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);
    pipeline.loadModel("/models/sr.param", hlplayer::AICapability::SuperResolution);

    hlplayer::GpuFrame input{};
    input.width = 1920;
    input.height = 1080;
    input.format = hlplayer::PixelFormat::RGBA8;
    input.timestamp = 1.5;

    auto result = pipeline.processFrame(input, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasValue());

    auto output = result.value();
    REQUIRE(output.width == 1920);
    REQUIRE(output.height == 1080);
    REQUIRE(output.format == hlplayer::PixelFormat::RGBA8);
    REQUIRE(output.timestamp == 1.5);
    REQUIRE_FALSE(output.deviceLost);
}

TEST_CASE("NcnnAIPipeline - multiple capabilities can be loaded", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);

    pipeline.loadModel("/models/sr.param", hlplayer::AICapability::SuperResolution);
    pipeline.loadModel("/models/ni.param", hlplayer::AICapability::NoiseReduction);

    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::SuperResolution));
    REQUIRE(pipeline.hasCapability(hlplayer::AICapability::NoiseReduction));
    REQUIRE_FALSE(pipeline.hasCapability(hlplayer::AICapability::HDR));
}

TEST_CASE("NcnnAIPipeline - processFrame matches any loaded capability", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);
    pipeline.loadModel("/models/sr.param", hlplayer::AICapability::SuperResolution);

    hlplayer::GpuFrame frame{};

    uint32_t caps = static_cast<uint32_t>(hlplayer::AICapability::SuperResolution)
                  | static_cast<uint32_t>(hlplayer::AICapability::HDR);

    auto result = pipeline.processFrame(frame, caps);
    REQUIRE(result.hasValue());
}

TEST_CASE("NcnnAIPipeline - processFrame with zero capabilities always succeeds", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);

    hlplayer::GpuFrame frame{};
    frame.width = 640;
    frame.height = 480;

    auto result = pipeline.processFrame(frame, 0);
    REQUIRE(result.hasValue());
    REQUIRE(result.value().width == 640);
    REQUIRE(result.value().height == 480);
}

TEST_CASE("NcnnAIPipeline - getQueueSize returns zero when no processing", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);

    REQUIRE(pipeline.getQueueSize() == 0);
}

TEST_CASE("NcnnAIPipeline - processFrame preserves frame metadata", "[ncnn_ai]") {
    hlplayer::NcnnAIPipeline pipeline;
    hlplayer::NCNNDeviceConfig config{};

    pipeline.initialize(config);
    pipeline.loadModel("/models/sr.param", hlplayer::AICapability::SuperResolution);

    hlplayer::GpuFrame input{};
    input.width = 1280;
    input.height = 720;
    input.format = hlplayer::PixelFormat::NV12;
    input.colorSpace = hlplayer::ColorSpace::BT709;
    input.colorRange = hlplayer::ColorRange::Limited;
    input.timestamp = 0.042;
    input.handle.nativeHandle = reinterpret_cast<void*>(0xABCDEF);

    auto result = pipeline.processFrame(input, static_cast<uint32_t>(hlplayer::AICapability::SuperResolution));
    REQUIRE(result.hasValue());

    auto output = result.value();
    REQUIRE(output.width == 1280);
    REQUIRE(output.height == 720);
    REQUIRE(output.format == hlplayer::PixelFormat::NV12);
    REQUIRE(output.colorSpace == hlplayer::ColorSpace::BT709);
    REQUIRE(output.colorRange == hlplayer::ColorRange::Limited);
    REQUIRE(output.timestamp == 0.042);
}
