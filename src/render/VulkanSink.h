#ifndef HLPLAYER_VULKANSINK_H
#define HLPLAYER_VULKANSINK_H

#ifndef HLPLAYER_RENDER_API
# ifdef _WIN32
#   ifdef HLPLAYER_RENDER_EXPORTS
#     define HLPLAYER_RENDER_API __declspec(dllexport)
#   else
#     define HLPLAYER_RENDER_API __declspec(dllimport)
#   endif
# else
#   define HLPLAYER_RENDER_API
# endif
#endif

#include "VideoSink.h"
#include <cstdint>
#include <atomic>
#include <functional>

namespace hlplayer {
namespace render {

/// Zero-copy Vulkan frame sink implementing IRenderBridge.
/// When compiled with HAS_QT_RHI, maps GpuFrame handles to QRhi textures.
/// Without Qt RHI, operates in stub mode (logs warnings, accepts frames).
class HLPLAYER_RENDER_API VulkanSink : public IRenderBridge {
public:
    using DeviceLostCallback = std::function<void()>;

    VulkanSink();
    ~VulkanSink() override;

    void presentFrame(const GpuFrame& frame) override;
    void onFormatChange(VideoFormat format) override;
    void reset() override;

    void setDeviceLostCallback(DeviceLostCallback callback);
    bool hasDeviceError() const;
    void clearDeviceError();

#ifdef HAS_QT_RHI
    void setRhiHandle(void* rhi);
#endif

private:
    void handleDeviceLost(const GpuFrame& frame);

    std::atomic<bool> deviceLost_{false};
    DeviceLostCallback onDeviceLost_;
#ifdef HAS_QT_RHI
    void* rhi_ = nullptr;
#endif
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_VULKANSINK_H
