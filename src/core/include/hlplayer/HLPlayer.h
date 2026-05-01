#ifndef HLPLAYER_HLPLAYER_H
#define HLPLAYER_HLPLAYER_H

/// @file HLPlayer.h
/// @brief Single-include root header for the HLPlayer SDK.
///
/// Include this one header to get the complete SDK surface:
///   - C API (PlayerApi.h) for cross-language embedding
///   - C++ API (PlayerFacade.h / IPlayerFacade.h) for C++ consumers
///   - SDK lifecycle (Init / Shutdown / Version)
///
/// Usage:
///   hlplayer::sdk::init();          // boots logger + telemetry
///   auto handle = HLPlayer_Create();
///   // ... use C-API or C++ API ...
///   HLPlayer_Destroy(handle);
///   hlplayer::sdk::shutdown();
///
/// Thread safety: Init/Shutdown are NOT thread-safe; call them from main thread
/// before/after any player operations.

#include <hlplayer/Export.h>

// ── C API (extern "C") ──────────────────────────────────────────────────────
#include <hlplayer/PlayerApi.h>

// ── C++ API ─────────────────────────────────────────────────────────────────
#include <hlplayer/IPlayerFacade.h>
#include <hlplayer/PlayerFacade.h>

// ── SDK Lifecycle (C++) ─────────────────────────────────────────────────────
#include <cstdint>

namespace hlplayer {
namespace sdk {

/// SDK version information.
struct Version {
    uint32_t major = 1;
    uint32_t minor = 0;
    uint32_t patch = 0;
    const char* commit = "dev";
};

/// Initialize the HLPlayer SDK.
/// Call once before creating any player instances.
/// Safe to call multiple times (no-op after first call).
/// Boots: spdlog logger, AtomicTelemetry, OtelTelemetry (if TELEMETRY_ENABLED).
HLPLAYER_CORE_API void init();

/// Shut down the HLPlayer SDK.
/// Call once after destroying all player instances.
/// Flushes telemetry, releases logger resources.
HLPLAYER_CORE_API void shutdown();

/// Query SDK version.
/// Thread-safe; can be called at any time.
HLPLAYER_CORE_API Version version();

/// Returns true if init() has been called.
HLPLAYER_CORE_API bool isInitialized();

} // namespace sdk
} // namespace hlplayer

// ── SDK Lifecycle (C API extensions) ────────────────────────────────────────
#ifdef __cplusplus
extern "C" {
#endif

/// Initialize the HLPlayer SDK. Must call before any HLPlayer_Create.
HLPLAYER_CORE_API void HLPlayer_Init(void);

/// Shut down the HLPlayer SDK. Call after all handles destroyed.
HLPLAYER_CORE_API void HLPlayer_Shutdown(void);

/// Get SDK version as MAJOR * 10000 + MINOR * 100 + PATCH.
HLPLAYER_CORE_API uint32_t HLPlayer_GetVersion(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HLPLAYER_HLPLAYER_H
