#include <hlplayer/NCNNInterop.h>
#include <hlplayer/logger.h>

namespace hlplayer {

Result<void> NCNNInterop::initialize(NCNNDeviceConfig) {
#ifdef HAS_NCNN
    LOG_INFO("NCNNInterop::initialize — stub, NCNN available");
    return Result<void>::success();
#else
    LOG_WARN("NCNNInterop::initialize — NCNN not available (HAS_NCNN not defined)");
    return Result<void>::error(PlayerError::UnsupportedFormat);
#endif
}

void NCNNInterop::shutdown() {
#ifdef HAS_NCNN
    LOG_INFO("NCNNInterop::shutdown — stub");
#else
    LOG_WARN("NCNNInterop::shutdown — NCNN not available");
#endif
}

Result<void*> NCNNInterop::importExternalMemory(const GpuFrameHandle&, size_t) {
#ifdef HAS_NCNN
    LOG_INFO("NCNNInterop::importExternalMemory — stub, returning dummy pointer");
    static char s_stubBuffer[1] = {0};
    return Result<void*>::success(static_cast<void*>(s_stubBuffer));
#else
    LOG_WARN("NCNNInterop::importExternalMemory — NCNN not available");
    return Result<void*>::error(PlayerError::UnsupportedFormat);
#endif
}

void NCNNInterop::releaseExternalMemory(void*) {
#ifdef HAS_NCNN
    LOG_INFO("NCNNInterop::releaseExternalMemory — stub");
#else
    LOG_WARN("NCNNInterop::releaseExternalMemory — NCNN not available");
#endif
}

bool NCNNInterop::isAvailable() const {
#ifdef HAS_NCNN
    return true;
#else
    return false;
#endif
}

} // namespace hlplayer
