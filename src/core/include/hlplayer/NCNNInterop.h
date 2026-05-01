#ifndef HLPLAYER_NCNNINTEROP_H
#define HLPLAYER_NCNNINTEROP_H

#include <hlplayer/Result.h>
#include <hlplayer/GpuFrameContract.h>
#include <cstdint>
#include <cstddef>

namespace hlplayer {

struct NCNNDeviceConfig {
    void* vulkanInstance = nullptr;
    void* vulkanPhysicalDevice = nullptr;
    void* vulkanDevice = nullptr;
    uint32_t queueFamilyIndex = 0;
    bool enableExternalMemory = true;
};

class HLPLAYER_CORE_API NCNNInterop {
public:
    Result<void> initialize(NCNNDeviceConfig config);
    void shutdown();
    Result<void*> importExternalMemory(const GpuFrameHandle& handle, size_t size);
    void releaseExternalMemory(void* ptr);
    bool isAvailable() const;
};

} // namespace hlplayer

#endif // HLPLAYER_NCNNINTEROP_H
