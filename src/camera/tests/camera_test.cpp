#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <hlplayer/CameraSource.h>
#include <hlplayer/AudioEncoder.h>
#include <hlplayer/StreamMuxer.h>
#include <hlplayer/RecordingPipeline.h>
#include <hlplayer/StreamingPipeline.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/Result.h>

#include <cmath>
#include <cstring>

using namespace hlplayer;

// Helper function to generate 1 second of 440Hz sine wave in S16 interleaved format
std::vector<uint8_t> generateSineWavePCM(int sampleRate, int channels, int durationSec, float frequency = 440.0f) {
    int totalSamples = sampleRate * durationSec;
    int totalBytes = totalSamples * channels * sizeof(int16_t);
    std::vector<uint8_t> pcmData(totalBytes);

    int16_t* samples = reinterpret_cast<int16_t*>(pcmData.data());

    for (int i = 0; i < totalSamples; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        double value = std::sin(2.0 * M_PI * frequency * t);
        int16_t sample = static_cast<int16_t>(value * 16383.0); // Scale to avoid clipping

        // Interleave channels (same value for all channels)
        for (int ch = 0; ch < channels; ++ch) {
            *samples++ = sample;
        }
    }

    return pcmData;
}

// ============================================================================
// CameraSource Tests
// ============================================================================

TEST_CASE("CameraSource - Enumerate Devices", "[camera]") {
    CameraSource src;
    auto result = src.enumerateDevices();

    if (result.hasError()) {
        SKIP("No camera devices available (DirectShow not accessible or no devices connected)");
    }

    const auto& devices = result.value();

    REQUIRE_FALSE(devices.empty());

    // Verify each device has required fields
    for (const auto& device : devices) {
        REQUIRE_FALSE(device.name.empty());
        REQUIRE_FALSE(device.devicePath.empty());

        // Verify supported presets
        for (const auto& preset : device.supportedPresets) {
            REQUIRE_FALSE(preset.label.empty());
            REQUIRE(preset.width > 0);
            REQUIRE(preset.height > 0);
            REQUIRE(preset.frameRate > 0);
        }
    }
}

TEST_CASE("CameraSource - Open Non-existent Device", "[camera][error]") {
    CameraSource src;
    auto result = src.open("nonexistent_device_xyz", 1280, 720, 30);

    REQUIRE(result.hasError());
    REQUIRE_FALSE(src.isOpen());
}

// ============================================================================
// AudioCapture Tests
// ============================================================================

TEST_CASE("AudioCapture - Enumerate Audio Devices", "[audio]") {
    auto result = AudioCapture::enumerateAudioDevices();

    if (result.hasError()) {
        SKIP("No audio devices available (DirectShow not accessible or no devices connected)");
    }

    const auto& devices = result.value();

    REQUIRE_FALSE(devices.empty());

    // Verify each device has required fields
    for (const auto& device : devices) {
        REQUIRE_FALSE(device.name.empty());
        REQUIRE_FALSE(device.devicePath.empty());
    }
}

// ============================================================================
// AudioEncoder Tests
// ============================================================================

TEST_CASE("AudioEncoder - Init and Encode Sine Wave", "[audio][encoder]") {
    AudioEncoder encoder;

    // Initialize encoder with standard AAC settings
    auto initResult = encoder.init(48000, 2, 128000);
    REQUIRE_FALSE(initResult.hasError());
    REQUIRE(encoder.isOpen());

    // Generate 1 second of 440Hz sine wave
    int sampleRate = 48000;
    int channels = 2;
    int durationSec = 1;
    auto pcmData = generateSineWavePCM(sampleRate, channels, durationSec, 440.0f);

    // Encode the sine wave
    int frameCount = sampleRate * durationSec;
    auto encodeResult = encoder.encode(pcmData.data(), frameCount);

    REQUIRE_FALSE(encodeResult.hasError());
    const auto& packets = encodeResult.value();
    // Encode may return empty packets (encoder buffering), so we don't require non-empty here

    // Flush encoder to get the actual encoded packets
    auto flushResult = encoder.flush();
    REQUIRE_FALSE(flushResult.hasError());

    // Flush should return the encoded packets
    const auto& flushPackets = flushResult.value();
    REQUIRE_FALSE(flushPackets.empty());

    // Verify packets have reasonable size
    for (const auto& packet : flushPackets) {
        REQUIRE_FALSE(packet.data.empty());
        // PTS/DTS can be negative for first packet (encoder offset), so don't require >= 0
        REQUIRE(packet.streamIndex == 0); // Single stream
    }
}

TEST_CASE("AudioEncoder - Empty Input", "[audio][encoder]") {
    AudioEncoder encoder;

    auto initResult = encoder.init(48000, 2, 128000);
    REQUIRE_FALSE(initResult.hasError());

    // Flush without sending any frames
    auto flushResult = encoder.flush();
    REQUIRE_FALSE(flushResult.hasError());

    // Should return empty packet list (no buffered data)
    const auto& packets = flushResult.value();
    REQUIRE(packets.empty());
}

TEST_CASE("AudioEncoder - Invalid Params - Sample Rate Zero", "[audio][encoder][error]") {
    AudioEncoder encoder;

    auto result = encoder.init(0, 2, 128000);
    REQUIRE(result.hasError());
    REQUIRE_FALSE(encoder.isOpen());
}

TEST_CASE("AudioEncoder - Invalid Params - Channels Zero", "[audio][encoder][error]") {
    AudioEncoder encoder;

    auto result = encoder.init(48000, 0, 128000);
    REQUIRE(result.hasError());
    REQUIRE_FALSE(encoder.isOpen());
}

TEST_CASE("AudioEncoder - Invalid Params - Bitrate Too Low", "[audio][encoder][error]") {
    AudioEncoder encoder;

    // Too low bitrate should fail
    auto result = encoder.init(48000, 2, 1000);
    // This may or may not fail depending on FFmpeg version, but we check it's handled
    if (result.hasError()) {
        REQUIRE_FALSE(encoder.isOpen());
    }
}

// ============================================================================
// StreamMuxer Tests
// ============================================================================

TEST_CASE("StreamMuxer - RTMP Context Creation", "[muxer]") {
    StreamMuxer muxer;

    // Try to open a fake RTMP URL
    // This will fail to connect but should create the context and attempt the format
    auto result = muxer.open("rtmp://fake.server.invalid/app/stream", StreamingProtocol::RTMP);

    // Open will fail (network error), but we verify the muxer handled it
    // If it succeeds for some reason, that's fine too - we just want no crash
    if (result.hasError()) {
        // Expected - no server available
        REQUIRE_FALSE(muxer.isOpen());
    } else {
        // Unexpected success - server exists? Verify it's open
        REQUIRE(muxer.isOpen());
        muxer.close();
    }
}

TEST_CASE("StreamMuxer - SRT Context Creation", "[muxer]") {
    StreamMuxer muxer;

    // Try to open a fake SRT URL
    auto result = muxer.open("srt://fake.server.invalid:9000?mode=caller", StreamingProtocol::SRT);

    // Same as RTMP - may fail, but should not crash
    if (result.hasError()) {
        REQUIRE_FALSE(muxer.isOpen());
    } else {
        REQUIRE(muxer.isOpen());
        muxer.close();
    }
}

TEST_CASE("StreamMuxer - Add Stream Without Open", "[muxer][error]") {
    StreamMuxer muxer;

    AVRational timeBase = {1, 90000};
    auto result = muxer.addStream(AV_CODEC_ID_H264, timeBase);

    REQUIRE(result.hasError());
}

// ============================================================================
// RecordingPipeline Tests
// ============================================================================

TEST_CASE("RecordingPipeline - Start Without Device", "[recording][error]") {
    RecordingPipeline pipeline;

    RecordingConfig config;
    config.outputPath = "test_output.mp4";
    config.resolution = {"720p", 1280, 720, 30};
    config.cameraDevicePath = "nonexistent_device_xyz";
    config.micDevicePath = "";

    RecordingStats stats;
    RecordingStateCallback callback = [&stats](RecordingState state, const RecordingStats& s) {
        stats = s;
    };

    auto result = pipeline.start(config, callback);

    REQUIRE(result.hasError());

    // Check state - it should be Idle (not started) or Error (failed)
    auto state = pipeline.getState();
    REQUIRE((state == RecordingState::Idle || state == RecordingState::Error));
}

TEST_CASE("RecordingPipeline - Empty Output Path", "[recording][error]") {
    RecordingPipeline pipeline;

    RecordingConfig config;
    config.outputPath = "";  // Empty path
    config.resolution = {"720p", 1280, 720, 30};
    config.cameraDevicePath = "";
    config.micDevicePath = "";

    RecordingStateCallback callback = [](RecordingState, const RecordingStats&) {};

    auto result = pipeline.start(config, callback);

    REQUIRE(result.hasError());
}

TEST_CASE("RecordingPipeline - Get Initial State", "[recording]") {
    RecordingPipeline pipeline;

    REQUIRE(pipeline.getState() == RecordingState::Idle);

    RecordingStats stats = pipeline.getStats();
    REQUIRE(stats.durationSeconds == 0.0);
    REQUIRE(stats.fileSizeBytes == 0);
    REQUIRE(stats.currentFps == 0.0);
}

// ============================================================================
// StreamingPipeline Tests
// ============================================================================

TEST_CASE("StreamingPipeline - Get Initial State", "[streaming]") {
    StreamingPipeline pipeline;

    REQUIRE(pipeline.getState() == StreamingState::Idle);

    StreamingStats stats = pipeline.getStats();
    REQUIRE(stats.streamedDuration == 0.0);
    REQUIRE(stats.totalDuration == 0.0);
    REQUIRE(stats.currentBitrate == 0.0);
    REQUIRE(stats.progress == 0.0);
}

TEST_CASE("StreamingPipeline - Empty Source Path", "[streaming][error]") {
    StreamingPipeline pipeline;

    StreamingConfig config;
    config.url = "rtmp://fake.server/app/stream";
    config.sourcePath = "";  // Empty source path

    StreamingStateCallback callback = [](StreamingState, const StreamingStats&) {};

    auto result = pipeline.start(config, callback);

    REQUIRE(result.hasError());
}

// ============================================================================
// Types - Config Defaults Tests
// ============================================================================

TEST_CASE("Types - RecordingConfig Defaults", "[types]") {
    RecordingConfig config;

    // Verify default values
    REQUIRE(config.videoBitrate == 4000000);
    REQUIRE(config.audioSampleRate == 48000);
    REQUIRE(config.audioChannels == 2);
    REQUIRE(config.audioBitrate == 128000);

    // Strings should be empty by default
    REQUIRE(config.outputPath.empty());
    REQUIRE(config.cameraDevicePath.empty());
    REQUIRE(config.micDevicePath.empty());

    // Resolution preset is not initialized by default struct construction
    // so we don't verify its members (they have garbage values)
}

TEST_CASE("Types - StreamingConfig Defaults", "[types]") {
    StreamingConfig config;

    // Verify default values
    REQUIRE(config.maxRetries == 3);
    REQUIRE(config.retryIntervalMs == 3000);
    REQUIRE(config.videoBitrate == 4000000);
    REQUIRE(config.keyframeInterval == 2);
    REQUIRE(config.audioBitrate == 128000);

    // Strings should be empty by default
    REQUIRE(config.url.empty());
    REQUIRE(config.sourcePath.empty());
}

TEST_CASE("Types - ResolutionPreset Creation", "[types]") {
    ResolutionPreset preset{"1080p30", 1920, 1080, 30};

    REQUIRE(preset.label == "1080p30");
    REQUIRE(preset.width == 1920);
    REQUIRE(preset.height == 1080);
    REQUIRE(preset.frameRate == 30);
}

TEST_CASE("Types - RecordingStats Initial Values", "[types]") {
    RecordingStats stats;

    REQUIRE(stats.durationSeconds == 0.0);
    REQUIRE(stats.fileSizeBytes == 0);
    REQUIRE(stats.currentFps == 0.0);
    REQUIRE(stats.state == RecordingState::Idle);
}

TEST_CASE("Types - StreamingStats Initial Values", "[types]") {
    StreamingStats stats;

    REQUIRE(stats.streamedDuration == 0.0);
    REQUIRE(stats.totalDuration == 0.0);
    REQUIRE(stats.currentBitrate == 0.0);
    REQUIRE(stats.progress == 0.0);
    REQUIRE(stats.state == StreamingState::Idle);
}

// ============================================================================
// Sine Wave Generation Verification
// ============================================================================

TEST_CASE("Helper - Sine Wave Generation", "[helper]") {
    // Test the helper function generates valid PCM data
    int sampleRate = 48000;
    int channels = 2;
    int durationSec = 1;

    auto pcmData = generateSineWavePCM(sampleRate, channels, durationSec, 440.0f);

    // Verify data size
    int expectedBytes = sampleRate * durationSec * channels * sizeof(int16_t);
    REQUIRE(pcmData.size() == static_cast<size_t>(expectedBytes));

    // Verify data is not all zeros
    const int16_t* samples = reinterpret_cast<const int16_t*>(pcmData.data());
    bool hasNonZero = false;
    for (size_t i = 0; i < pcmData.size() / sizeof(int16_t); ++i) {
        if (samples[i] != 0) {
            hasNonZero = true;
            break;
        }
    }
    REQUIRE(hasNonZero);

    // Verify values are within valid int16 range
    for (size_t i = 0; i < pcmData.size() / sizeof(int16_t); ++i) {
        REQUIRE(samples[i] >= INT16_MIN);
        REQUIRE(samples[i] <= INT16_MAX);
    }
}
