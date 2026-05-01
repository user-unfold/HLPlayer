#include "VulkanSink.h"
#include <spdlog/spdlog.h>

namespace hlplayer {
namespace render {

VulkanSink::VulkanSink() {
    spdlog::info("VulkanSink constructed");
}

VulkanSink::~VulkanSink() {
    spdlog::info("VulkanSink destructed");
}

void VulkanSink::presentFrame(const GpuFrame& frame) {
    if (deviceLost_.load()) {
        spdlog::warn("VulkanSink: dropping frame, device is in lost state");
        return;
    }

    if (frame.deviceLost) {
        handleDeviceLost(frame);
        return;
    }

#ifdef HAS_QT_RHI
    if (!rhi_) {
        spdlog::warn("VulkanSink: QRhi not set, cannot present frame");
        return;
    }
    // Map GpuFrame nativeHandle to QRhi texture via QRhi::nativeHandles()
    // Zero-copy: no CPU readback, pass VkImage directly to QRhi
    spdlog::debug("VulkanSink: presenting frame {}x{} format={} ts={:.3f}",
                  frame.width, frame.height,
                  static_cast<int>(frame.format), frame.timestamp);
#else
    spdlog::debug("VulkanSink(stub): received frame {}x{} format={} ts={:.3f}",
                  frame.width, frame.height,
                  static_cast<int>(frame.format), frame.timestamp);
#endif
}

void VulkanSink::onFormatChange(VideoFormat format) {
    spdlog::info("VulkanSink: format changed to {}", static_cast<int>(format));
}

void VulkanSink::reset() {
    deviceLost_.store(false);
    spdlog::info("VulkanSink: reset");
}

void VulkanSink::setDeviceLostCallback(DeviceLostCallback callback) {
    onDeviceLost_ = std::move(callback);
}

bool VulkanSink::hasDeviceError() const {
    return deviceLost_.load();
}

void VulkanSink::clearDeviceError() {
    deviceLost_.store(false);
}

#ifdef HAS_QT_RHI
void VulkanSink::setRhiHandle(void* rhi) {
    rhi_ = rhi;
}
#endif

void VulkanSink::handleDeviceLost(const GpuFrame& frame) {
    deviceLost_.store(true);
    spdlog::error("VulkanSink: GPU device lost detected (frame ts={:.3f})",
                  frame.timestamp);
    if (onDeviceLost_) {
        onDeviceLost_();
    }
}

} // namespace render
} // namespace hlplayer
