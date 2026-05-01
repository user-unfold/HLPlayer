#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <hlplayer/VSRCircuitBreaker.h>
#include <hlplayer/DecodeFallback.h>
#include <hlplayer/Result.h>

#include <chrono>
#include <thread>

using namespace hlplayer;
using Catch::Matchers::WithinAbs;

// ============================================================================
// VSRCircuitBreaker tests
// ============================================================================

TEST_CASE("VSRCircuitBreaker - construction creates Active state", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    VSRCircuitBreaker breaker(config);

    REQUIRE(breaker.state() == VSRBreakerState::Active);
    REQUIRE_FALSE(breaker.shouldBypass());
    REQUIRE(breaker.consecutiveSlowFrames() == 0);
}

TEST_CASE("VSRCircuitBreaker - state string conversion", "[vsr_circuit_breaker][e2e]") {
    REQUIRE(std::string(VSRCircuitBreaker::stateToString(VSRBreakerState::Active)) == "Active");
    REQUIRE(std::string(VSRCircuitBreaker::stateToString(VSRBreakerState::CircuitOpen)) == "CircuitOpen");
    REQUIRE(std::string(VSRCircuitBreaker::stateToString(VSRBreakerState::Probing)) == "Probing");
    REQUIRE(std::string(VSRCircuitBreaker::stateToString(VSRBreakerState::Disabled)) == "Disabled");
}

TEST_CASE("VSRCircuitBreaker - single slow inference does not open circuit", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    VSRCircuitBreaker breaker(config);

    REQUIRE(breaker.state() == VSRBreakerState::Active);

    breaker.recordInference(20.0);

    REQUIRE(breaker.state() == VSRBreakerState::Active);
    REQUIRE(breaker.consecutiveSlowFrames() == 1);
}

TEST_CASE("VSRCircuitBreaker - three slow inferences open circuit", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    VSRCircuitBreaker breaker(config);

    REQUIRE(breaker.state() == VSRBreakerState::Active);

    breaker.recordInference(20.0);
    REQUIRE(breaker.state() == VSRBreakerState::Active);

    breaker.recordInference(22.0);
    REQUIRE(breaker.state() == VSRBreakerState::Active);

    breaker.recordInference(18.0);
    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);
    REQUIRE(breaker.shouldBypass());
    REQUIRE(breaker.consecutiveSlowFrames() == 0);
}

TEST_CASE("VSRCircuitBreaker - fast inferences reset slow counter", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    VSRCircuitBreaker breaker(config);

    breaker.recordInference(20.0);
    REQUIRE(breaker.consecutiveSlowFrames() == 1);

    breaker.recordInference(10.0);
    REQUIRE(breaker.consecutiveSlowFrames() == 0);

    breaker.recordInference(20.0);
    breaker.recordInference(22.0);
    REQUIRE(breaker.consecutiveSlowFrames() == 2);

    breaker.recordInference(12.0);
    REQUIRE(breaker.consecutiveSlowFrames() == 0);
}

TEST_CASE("VSRCircuitBreaker - transition CircuitOpen to Probing after cooldown", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    config.cooldownSeconds = 0.1;
    VSRCircuitBreaker breaker(config);

    breaker.recordInference(20.0);
    breaker.recordInference(20.0);
    breaker.recordInference(20.0);

    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);

    bool transitioned = breaker.tryStartProbing();

    REQUIRE(transitioned);
    REQUIRE(breaker.state() == VSRBreakerState::Probing);
    REQUIRE_FALSE(breaker.shouldBypass());
}

TEST_CASE("VSRCircuitBreaker - Probing success returns to Active", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    config.cooldownSeconds = 0.1;
    VSRCircuitBreaker breaker(config);

    breaker.recordInference(20.0);
    breaker.recordInference(20.0);
    breaker.recordInference(20.0);

    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);

    breaker.tryStartProbing();
    REQUIRE(breaker.state() == VSRBreakerState::Probing);

    breaker.handleProbeResult(10.0);

    REQUIRE(breaker.state() == VSRBreakerState::Active);
    REQUIRE_FALSE(breaker.shouldBypass());
}

TEST_CASE("VSRCircuitBreaker - Probing failure returns to CircuitOpen", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    config.cooldownSeconds = 0.1;
    VSRCircuitBreaker breaker(config);

    breaker.recordInference(20.0);
    breaker.recordInference(20.0);
    breaker.recordInference(20.0);

    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);

    breaker.tryStartProbing();
    REQUIRE(breaker.state() == VSRBreakerState::Probing);

    breaker.handleProbeResult(25.0);

    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);
    REQUIRE(breaker.shouldBypass());
}

TEST_CASE("VSRCircuitBreaker - tryStartProbing before cooldown returns false", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    config.cooldownSeconds = 30.0;
    VSRCircuitBreaker breaker(config);

    breaker.recordInference(20.0);
    breaker.recordInference(20.0);
    breaker.recordInference(20.0);

    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);

    bool transitioned = breaker.tryStartProbing();

    REQUIRE_FALSE(transitioned);
    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);
}

TEST_CASE("VSRCircuitBreaker - forceDisable bypasses permanently", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    VSRCircuitBreaker breaker(config);

    REQUIRE(breaker.state() == VSRBreakerState::Active);

    breaker.forceDisable();

    REQUIRE(breaker.state() == VSRBreakerState::Disabled);
    REQUIRE(breaker.shouldBypass());

    breaker.recordInference(10.0);

    REQUIRE(breaker.state() == VSRBreakerState::Disabled);
}

TEST_CASE("VSRCircuitBreaker - forceEnable returns to Active", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    VSRCircuitBreaker breaker(config);

    breaker.forceDisable();
    REQUIRE(breaker.state() == VSRBreakerState::Disabled);

    breaker.forceEnable();

    REQUIRE(breaker.state() == VSRBreakerState::Active);
    REQUIRE_FALSE(breaker.shouldBypass());
}

TEST_CASE("VSRCircuitBreaker - reset returns to Active", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    VSRCircuitBreaker breaker(config);

    breaker.recordInference(20.0);
    breaker.recordInference(20.0);
    breaker.recordInference(20.0);

    REQUIRE(breaker.state() == VSRBreakerState::CircuitOpen);

    breaker.reset();

    REQUIRE(breaker.state() == VSRBreakerState::Active);
    REQUIRE_FALSE(breaker.shouldBypass());
    REQUIRE(breaker.consecutiveSlowFrames() == 0);
}

TEST_CASE("VSRCircuitBreaker - VRAM degradation action at normal usage", "[vsr_circuit_breaker][e2e]") {
    class MockVRAMManager : public IVRAMBudgetManager {
    public:
        Result<void> initialize(const VRAMBudgetConfig& config) override { return Result<void>::success(); }
        Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs) override { return Result<void>::success(); }
        void release(uint64_t sizeBytes) override {}
        uint64_t usedBytes() const override { return 0; }
        uint64_t availableBytes() const override { return 8ULL * 1024 * 1024 * 1024; }
        bool isNearLimit() const override { return false; }
        void setPerformanceMode(PerformanceMode mode) override {}
        PerformanceMode getPerformanceMode() const override { return PerformanceMode::Balanced; }
        void reset() override {}
    };

    VSRCircuitBreakerConfig config;
    config.vramDegradationThreshold = 0.85;
    config.vramEmergencyThreshold = 0.95;
    VSRCircuitBreaker breaker(config);

    MockVRAMManager vramManager;
    auto action = breaker.checkVRAMPressure(&vramManager);

    REQUIRE(action == VRAMDegradationAction::None);
    REQUIRE(breaker.recommendedScaleFactor() == 0.0);
}

TEST_CASE("VSRCircuitBreaker - VRAM degradation action at high usage", "[vsr_circuit_breaker][e2e]") {
    class MockVRAMManager : public IVRAMBudgetManager {
    public:
        Result<void> initialize(const VRAMBudgetConfig& config) override { return Result<void>::success(); }
        Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs) override { return Result<void>::success(); }
        void release(uint64_t sizeBytes) override {}
        uint64_t usedBytes() const override { return 7ULL * 1024 * 1024 * 1024; }
        uint64_t availableBytes() const override { return 1ULL * 1024 * 1024 * 1024; }
        bool isNearLimit() const override { return true; }
        void setPerformanceMode(PerformanceMode mode) override {}
        PerformanceMode getPerformanceMode() const override { return PerformanceMode::Balanced; }
        void reset() override {}
    };

    VSRCircuitBreakerConfig config;
    config.vramDegradationThreshold = 0.85;
    config.vramEmergencyThreshold = 0.95;
    config.degradedScaleFactor = 2.0;
    VSRCircuitBreaker breaker(config);

    MockVRAMManager vramManager;
    auto action = breaker.checkVRAMPressure(&vramManager);

    REQUIRE(action == VRAMDegradationAction::ReduceScale);
    REQUIRE(breaker.recommendedScaleFactor() == 2.0);
}

TEST_CASE("VSRCircuitBreaker - VRAM degradation action at emergency usage", "[vsr_circuit_breaker][e2e]") {
    class MockVRAMManager : public IVRAMBudgetManager {
    public:
        Result<void> initialize(const VRAMBudgetConfig& config) override { return Result<void>::success(); }
        Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs) override { return Result<void>::success(); }
        void release(uint64_t sizeBytes) override {}
        uint64_t usedBytes() const override { return 8ULL * 1024 * 1024 * 1024 - 1; }
        uint64_t availableBytes() const override { return 1; }
        bool isNearLimit() const override { return true; }
        void setPerformanceMode(PerformanceMode mode) override {}
        PerformanceMode getPerformanceMode() const override { return PerformanceMode::Balanced; }
        void reset() override {}
    };

    VSRCircuitBreakerConfig config;
    config.vramDegradationThreshold = 0.85;
    config.vramEmergencyThreshold = 0.95;
    VSRCircuitBreaker breaker(config);

    MockVRAMManager vramManager;
    auto action = breaker.checkVRAMPressure(&vramManager);

    REQUIRE(action == VRAMDegradationAction::DisableVSR);
}

TEST_CASE("VSRCircuitBreaker - update config at runtime", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    VSRCircuitBreaker breaker(config);

    VSRCircuitBreakerConfig newConfig;
    newConfig.slowThresholdMs = 20.0;
    newConfig.slowFrameCount = 5;
    newConfig.cooldownSeconds = 60.0;

    breaker.updateConfig(newConfig);

    VSRCircuitBreakerConfig currentConfig = breaker.config();

    REQUIRE(currentConfig.slowThresholdMs == 20.0);
    REQUIRE(currentConfig.slowFrameCount == 5);
    REQUIRE(currentConfig.cooldownSeconds == 60.0);
}

TEST_CASE("VSRCircuitBreaker - get circuit open time", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.slowThresholdMs = 16.0;
    config.slowFrameCount = 3;
    VSRCircuitBreaker breaker(config);

    auto timeBefore = std::chrono::steady_clock::now();

    breaker.recordInference(20.0);
    breaker.recordInference(20.0);
    breaker.recordInference(20.0);

    auto timeAfter = std::chrono::steady_clock::now();
    auto circuitOpenTime = breaker.circuitOpenTime();

    REQUIRE(circuitOpenTime >= timeBefore);
    REQUIRE(circuitOpenTime <= timeAfter);
}

TEST_CASE("VSRCircuitBreaker - checkVRAMPressure with null manager", "[vsr_circuit_breaker][e2e]") {
    VSRCircuitBreakerConfig config;
    config.vramDegradationThreshold = 0.85;
    config.vramEmergencyThreshold = 0.95;
    VSRCircuitBreaker breaker(config);

    auto action = breaker.checkVRAMPressure(nullptr);

    REQUIRE(action == VRAMDegradationAction::None);
}

// ============================================================================
// DecodeFallback tests
// ============================================================================

TEST_CASE("DecodeFallback - construction creates HardwareActive state", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    DecodeFallback fallback(config);

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);
    REQUIRE(fallback.currentBackend() == DecodeBackend::Hardware);
    REQUIRE(fallback.isOperational());
}

TEST_CASE("DecodeFallback - state string conversion", "[decode_fallback][e2e]") {
    REQUIRE(std::string(DecodeFallback::stateToString(DecodeFallbackState::HardwareActive)) == "HardwareActive");
    REQUIRE(std::string(DecodeFallback::stateToString(DecodeFallbackState::SoftwareActive)) == "SoftwareActive");
    REQUIRE(std::string(DecodeFallback::stateToString(DecodeFallbackState::Error)) == "Error");
}

TEST_CASE("DecodeFallback - backend string conversion", "[decode_fallback][e2e]") {
    REQUIRE(std::string(DecodeFallback::backendToString(DecodeBackend::Hardware)) == "Hardware");
    REQUIRE(std::string(DecodeFallback::backendToString(DecodeBackend::Software)) == "Software");
}

TEST_CASE("DecodeFallback - single HW failure does not trigger fallback", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    DecodeFallback fallback(config);

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);
}

TEST_CASE("DecodeFallback - three HW failures trigger SW fallback", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    DecodeFallback fallback(config);

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);
    REQUIRE(fallback.currentBackend() == DecodeBackend::Software);
}

TEST_CASE("DecodeFallback - HW success resets failure counter", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    DecodeFallback fallback(config);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.hwFailureCount() == 2);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::success());

    REQUIRE(fallback.hwFailureCount() == 0);
    REQUIRE(fallback.hwSuccessCount() == 1);
    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);
}

TEST_CASE("DecodeFallback - SW failure eventually triggers Error state", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    config.swFailureThreshold = 5;
    DecodeFallback fallback(config);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);

    for (int i = 0; i < 4; i++) {
        fallback.onDecodeResult(DecodeBackend::Software, Result<void>::error(PlayerError::DeviceLost));
    }

    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);

    fallback.onDecodeResult(DecodeBackend::Software, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.currentState() == DecodeFallbackState::Error);
    REQUIRE_FALSE(fallback.isOperational());
}

TEST_CASE("DecodeFallback - SW success resets SW failure counter", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    config.swFailureThreshold = 5;
    DecodeFallback fallback(config);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);

    for (int i = 0; i < 4; i++) {
        fallback.onDecodeResult(DecodeBackend::Software, Result<void>::error(PlayerError::DeviceLost));
    }

    REQUIRE(fallback.swFailureCount() == 4);

    fallback.onDecodeResult(DecodeBackend::Software, Result<void>::success());

    REQUIRE(fallback.swFailureCount() == 0);
    REQUIRE(fallback.swSuccessCount() == 1);
}

TEST_CASE("DecodeFallback - forceSoftware skips HW entirely", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    DecodeFallback fallback(config);

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);

    fallback.forceSoftware();

    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);
    REQUIRE(fallback.currentBackend() == DecodeBackend::Software);
}

TEST_CASE("DecodeFallback - forceHardware re-enables HW", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    DecodeFallback fallback(config);

    fallback.forceSoftware();
    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);

    fallback.forceHardware();

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);
    REQUIRE(fallback.currentBackend() == DecodeBackend::Hardware);
}

TEST_CASE("DecodeFallback - reset returns to HardwareActive", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    config.swFailureThreshold = 5;
    DecodeFallback fallback(config);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.currentState() == DecodeFallbackState::SoftwareActive);

    fallback.reset();

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);
    REQUIRE(fallback.currentBackend() == DecodeBackend::Hardware);
    REQUIRE(fallback.hwFailureCount() == 0);
    REQUIRE(fallback.swFailureCount() == 0);
}

TEST_CASE("DecodeFallback - update config at runtime", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    config.swFailureThreshold = 5;
    DecodeFallback fallback(config);

    DecodeFallback::Config newConfig;
    newConfig.hwFailureThreshold = 5;
    newConfig.swFailureThreshold = 10;
    newConfig.softwareFallbackAllowed = false;

    fallback.updateConfig(newConfig);

    DecodeFallback::Config currentConfig = fallback.config();

    REQUIRE(currentConfig.hwFailureThreshold == 5);
    REQUIRE(currentConfig.swFailureThreshold == 10);
    REQUIRE_FALSE(currentConfig.softwareFallbackAllowed);
}

TEST_CASE("DecodeFallback - softwareFallbackAllowed=false goes straight to Error", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    config.hwFailureThreshold = 3;
    config.softwareFallbackAllowed = false;
    DecodeFallback fallback(config);

    REQUIRE(fallback.currentState() == DecodeFallbackState::HardwareActive);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.currentState() == DecodeFallbackState::Error);
    REQUIRE_FALSE(fallback.isOperational());
}

TEST_CASE("DecodeFallback - statistics tracking", "[decode_fallback][e2e]") {
    DecodeFallback::Config config;
    DecodeFallback fallback(config);

    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::success());
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::success());
    fallback.onDecodeResult(DecodeBackend::Hardware, Result<void>::error(PlayerError::DeviceLost));

    fallback.onDecodeResult(DecodeBackend::Software, Result<void>::success());
    fallback.onDecodeResult(DecodeBackend::Software, Result<void>::error(PlayerError::DeviceLost));

    REQUIRE(fallback.hwSuccessCount() == 2);
    REQUIRE(fallback.hwFailureCount() == 1);
    REQUIRE(fallback.swSuccessCount() == 1);
    REQUIRE(fallback.swFailureCount() == 1);
}
