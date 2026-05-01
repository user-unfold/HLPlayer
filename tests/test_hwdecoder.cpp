#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <hlplayer/HWDecoder.h>
#include <hlplayer/CPUFallbackDecoder.h>

#include <vector>
#include <cstdint>

// ============================================================================
// DecodeBackend enum
// ============================================================================

TEST_CASE("DecodeBackend enum has expected values", "[hwdecoder]") {
    REQUIRE(static_cast<int>(hlplayer::DecodeBackend::Auto) == 0);
    REQUIRE(static_cast<int>(hlplayer::DecodeBackend::Vulkan) == 1);
    REQUIRE(static_cast<int>(hlplayer::DecodeBackend::CUDA) == 2);
    REQUIRE(static_cast<int>(hlplayer::DecodeBackend::D3D11) == 3);
    REQUIRE(static_cast<int>(hlplayer::DecodeBackend::CPU) == 4);
}

// ============================================================================
// Codec enum
// ============================================================================

TEST_CASE("Codec enum has expected values", "[hwdecoder]") {
    REQUIRE(static_cast<int>(hlplayer::Codec::Unknown) == 0);
    REQUIRE(static_cast<int>(hlplayer::Codec::H264) == 1);
    REQUIRE(static_cast<int>(hlplayer::Codec::HEVC) == 2);
    REQUIRE(static_cast<int>(hlplayer::Codec::AV1) == 3);
}

// ============================================================================
// DecoderConfig defaults
// ============================================================================

TEST_CASE("DecoderConfig default values", "[hwdecoder]") {
    hlplayer::DecoderConfig config;
    REQUIRE(config.backend == hlplayer::DecodeBackend::Auto);
    REQUIRE(config.codec == hlplayer::Codec::Unknown);
    REQUIRE(config.gpuDevice == nullptr);
    REQUIRE(config.width == 0);
    REQUIRE(config.height == 0);
    REQUIRE(config.outputPixelFormat == hlplayer::PixelFormat::NV12);
}

// ============================================================================
// CPUFallbackDecoder construction
// ============================================================================

TEST_CASE("CPUFallbackDecoder default construction", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    REQUIRE(decoder.getBackend() == hlplayer::DecodeBackend::CPU);
}

// ============================================================================
// CPUFallbackDecoder getBackend
// ============================================================================

TEST_CASE("CPUFallbackDecoder getBackend returns CPU", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    REQUIRE(decoder.getBackend() == hlplayer::DecodeBackend::CPU);
}

// ============================================================================
// CPUFallbackDecoder supportsCodec
// ============================================================================

TEST_CASE("CPUFallbackDecoder supportsCodec returns true for all codecs", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    REQUIRE(decoder.supportsCodec(hlplayer::Codec::Unknown));
    REQUIRE(decoder.supportsCodec(hlplayer::Codec::H264));
    REQUIRE(decoder.supportsCodec(hlplayer::Codec::HEVC));
    REQUIRE(decoder.supportsCodec(hlplayer::Codec::AV1));
}

// ============================================================================
// CPUFallbackDecoder open/decode/close
// ============================================================================

TEST_CASE("CPUFallbackDecoder open with valid config", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 320;
    config.height = 240;

    auto result = decoder.open(config);
    REQUIRE(result.hasValue());
}

TEST_CASE("CPUFallbackDecoder open with zero dimensions returns error", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;

    SECTION("zero width") {
        config.width = 0;
        config.height = 240;
        auto result = decoder.open(config);
        REQUIRE(result.hasError());
        REQUIRE(result.error() == hlplayer::PlayerError::InvalidState);
    }

    SECTION("zero height") {
        config.width = 320;
        config.height = 0;
        auto result = decoder.open(config);
        REQUIRE(result.hasError());
        REQUIRE(result.error() == hlplayer::PlayerError::InvalidState);
    }
}

TEST_CASE("CPUFallbackDecoder decode creates valid GpuFrame", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 64;
    config.height = 48;

    REQUIRE(decoder.open(config).hasValue());

    std::vector<uint8_t> fakeFrame(64 * 48 * 3 / 2, 0xAB);
    auto result = decoder.decode(fakeFrame.data(), fakeFrame.size(), 0.033);

    REQUIRE(result.hasValue());
    auto frame = result.value();
    REQUIRE(frame.width == 64);
    REQUIRE(frame.height == 48);
    REQUIRE(frame.format == hlplayer::PixelFormat::NV12);
    REQUIRE(frame.timestamp == Catch::Approx(0.033));
    REQUIRE(frame.handle.nativeHandle != nullptr);
}

TEST_CASE("CPUFallbackDecoder decode sets PixelFormat from config", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 16;
    config.height = 16;
    config.outputPixelFormat = hlplayer::PixelFormat::RGBA8;

    REQUIRE(decoder.open(config).hasValue());

    std::vector<uint8_t> data(16 * 16 * 4, 0xFF);
    auto result = decoder.decode(data.data(), data.size(), 0.0);

    REQUIRE(result.hasValue());
    REQUIRE(result.value().format == hlplayer::PixelFormat::RGBA8);
}

TEST_CASE("CPUFallbackDecoder decode with zero-size data returns error", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 64;
    config.height = 48;

    REQUIRE(decoder.open(config).hasValue());

    SECTION("nullptr data") {
        auto result = decoder.decode(nullptr, 100, 0.0);
        REQUIRE(result.hasError());
        REQUIRE(result.error() == hlplayer::PlayerError::DecodeError);
    }

    SECTION("zero size") {
        std::vector<uint8_t> data(1, 0);
        auto result = decoder.decode(data.data(), 0, 0.0);
        REQUIRE(result.hasError());
        REQUIRE(result.error() == hlplayer::PlayerError::DecodeError);
    }
}

TEST_CASE("CPUFallbackDecoder decode without open returns error", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    std::vector<uint8_t> data(100, 0);

    auto result = decoder.decode(data.data(), data.size(), 0.0);
    REQUIRE(result.hasError());
    REQUIRE(result.error() == hlplayer::PlayerError::InvalidState);
}

// ============================================================================
// CPUFallbackDecoder flush
// ============================================================================

TEST_CASE("CPUFallbackDecoder flush returns empty vector", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 64;
    config.height = 48;

    REQUIRE(decoder.open(config).hasValue());

    auto result = decoder.flush();
    REQUIRE(result.hasValue());
    REQUIRE(result.value().empty());
}

// ============================================================================
// CPUFallbackDecoder close idempotent
// ============================================================================

TEST_CASE("CPUFallbackDecoder close is idempotent", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 64;
    config.height = 48;

    REQUIRE(decoder.open(config).hasValue());
    decoder.close();
    decoder.close();
    decoder.close();

    auto result = decoder.decode(nullptr, 1, 0.0);
    REQUIRE(result.hasError());
}

// ============================================================================
// CPUFallbackDecoder polymorphism
// ============================================================================

TEST_CASE("CPUFallbackDecoder implements IHWDecoder polymorphically", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::IHWDecoder& iface = decoder;

    REQUIRE(iface.getBackend() == hlplayer::DecodeBackend::CPU);
    REQUIRE(iface.supportsCodec(hlplayer::Codec::H264));
    REQUIRE(iface.supportsCodec(hlplayer::Codec::HEVC));
}

// ============================================================================
// Multiple decode calls
// ============================================================================

TEST_CASE("Multiple decode calls produce valid frames", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 32;
    config.height = 24;

    REQUIRE(decoder.open(config).hasValue());

    std::vector<uint8_t> fakeFrame(32 * 24 * 3 / 2, 0xCD);

    for (int i = 0; i < 5; ++i) {
        auto result = decoder.decode(fakeFrame.data(), fakeFrame.size(), i * 0.04);
        REQUIRE(result.hasValue());
        auto frame = result.value();
        REQUIRE(frame.width == 32);
        REQUIRE(frame.height == 24);
        REQUIRE(frame.format == hlplayer::PixelFormat::NV12);
        REQUIRE(frame.timestamp == Catch::Approx(i * 0.04));
    }
}

// ============================================================================
// Reopen after close
// ============================================================================

TEST_CASE("CPUFallbackDecoder can be reopened after close", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 100;
    config.height = 100;

    REQUIRE(decoder.open(config).hasValue());
    decoder.close();

    config.width = 200;
    config.height = 150;
    REQUIRE(decoder.open(config).hasValue());

    std::vector<uint8_t> data(200 * 150 * 3 / 2, 0x42);
    auto result = decoder.decode(data.data(), data.size(), 1.0);
    REQUIRE(result.hasValue());
    REQUIRE(result.value().width == 200);
    REQUIRE(result.value().height == 150);
}

// ============================================================================
// Decode with data smaller than buffer
// ============================================================================

TEST_CASE("CPUFallbackDecoder decode with data smaller than buffer", "[hwdecoder]") {
    hlplayer::CPUFallbackDecoder decoder;
    hlplayer::DecoderConfig config;
    config.width = 320;
    config.height = 240;

    REQUIRE(decoder.open(config).hasValue());

    std::vector<uint8_t> smallData(10, 0xFF);
    auto result = decoder.decode(smallData.data(), smallData.size(), 0.0);

    REQUIRE(result.hasValue());
    REQUIRE(result.value().width == 320);
    REQUIRE(result.value().height == 240);
}
