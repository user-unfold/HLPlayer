#include <hlplayer/HLPlayer.h>
#include <hlplayer/logger.h>
#include <hlplayer/telemetry.h>

#include <atomic>
#include <mutex>

namespace {

std::atomic<bool> g_initialized{false};
std::mutex g_initMutex;

hlplayer::AtomicTelemetry* g_atomicTelemetry = nullptr;
hlplayer::OtelTelemetry* g_otelTelemetry = nullptr;

}

namespace hlplayer {
namespace sdk {

void init() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initialized.load(std::memory_order_acquire)) return;

    Logger::initialize();
    LOG_INFO("HLPlayer SDK v1.0.0 initializing");

    g_atomicTelemetry = new AtomicTelemetry();
    g_otelTelemetry = new OtelTelemetry();
    g_atomicTelemetry->incrementCounter("sdk_init", 1);

#ifdef TELEMETRY_ENABLED
    LOG_INFO("Dual-Telemetry active: OTel + Atomic");
#else
    LOG_INFO("Dual-Telemetry active: Atomic (OTel stub)");
#endif

    g_initialized.store(true, std::memory_order_release);
    LOG_INFO("HLPlayer SDK initialized");
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (!g_initialized.load(std::memory_order_acquire)) return;

    LOG_INFO("HLPlayer SDK shutting down");

    if (g_atomicTelemetry) {
        auto counters = g_atomicTelemetry->getAllCounters();
        LOG_INFO("Telemetry snapshot: {} counters", counters.size());
        for (const auto& [name, value] : counters) {
            LOG_INFO("  {} = {}", name, value);
        }
    }

    delete g_otelTelemetry;
    g_otelTelemetry = nullptr;
    delete g_atomicTelemetry;
    g_atomicTelemetry = nullptr;

    g_initialized.store(false, std::memory_order_release);
    LOG_INFO("HLPlayer SDK shut down");
}

Version version() {
    return Version{1, 0, 0, "dev"};
}

bool isInitialized() {
    return g_initialized.load(std::memory_order_acquire);
}

} // namespace sdk
} // namespace hlplayer

extern "C" {

void HLPlayer_Init(void) {
    hlplayer::sdk::init();
}

void HLPlayer_Shutdown(void) {
    hlplayer::sdk::shutdown();
}

uint32_t HLPlayer_GetVersion(void) {
    auto v = hlplayer::sdk::version();
    return v.major * 10000u + v.minor * 100u + v.patch;
}

} // extern "C"
