#include "VulkanVideoSink.h"
#include <spdlog/spdlog.h>
#include <vector>

namespace hlplayer {
namespace render {

VulkanVideoSink::VulkanVideoSink() {
    spdlog::info("VulkanVideoSink constructed");
}

VulkanVideoSink::~VulkanVideoSink() {
    std::lock_guard<std::mutex> lock(mutex_);
    spdlog::info("VulkanVideoSink destructed");
}

void VulkanVideoSink::onFrame(const GpuFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (deviceLost_) {
        spdlog::warn("VulkanVideoSink: dropping frame, device is in lost state");
        return;
    }

    if (frame.deviceLost) {
        handleDeviceLost(frame);
        return;
    }

#ifdef __APPLE__
    if (moltenVKFallback_) {
        spdlog::debug("VulkanVideoSink: MoltenVK fallback mode, using CPU path");
    }
#endif

    updateFrameInfo(frame);

    spdlog::debug("VulkanVideoSink: received frame {}x{} format={} ts={:.3f}",
                  frame.width, frame.height,
                  static_cast<int>(frame.format), frame.timestamp);

    if (onFrameAvailable) {
        onFrameAvailable();
    }
}

void VulkanVideoSink::onFormatChanged(VideoFormat format) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentFormat_ = format;
    spdlog::info("VulkanVideoSink: format changed to {}", static_cast<int>(format));
}

void VulkanVideoSink::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    deviceLost_ = false;
    hasFrame_ = false;
    currentVkImage_ = nullptr;
    auxiliaryHandle_ = nullptr;
    width_ = 0;
    height_ = 0;
    currentFormat_ = VideoFormat::Unknown;
    cpuFrameData_.clear();
    spdlog::info("VulkanVideoSink: reset");
}

void* VulkanVideoSink::getVkImage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceLost_ ? nullptr : currentVkImage_;
}

void* VulkanVideoSink::getAuxiliaryHandle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceLost_ ? nullptr : auxiliaryHandle_;
}

void VulkanVideoSink::getFrameSize(uint32_t& width, uint32_t& height) const {
    std::lock_guard<std::mutex> lock(mutex_);
    width = width_;
    height = height_;
}

void VulkanVideoSink::setRhiHandle(void* rhi) {
    std::lock_guard<std::mutex> lock(mutex_);
    rhi_ = rhi;
    spdlog::debug("VulkanVideoSink: Qt RHI handle set");
}

#ifdef __APPLE__
bool VulkanVideoSink::isMoltenVKAvailable() const {
    return true;
}

void VulkanVideoSink::setMoltenVKFallback(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    moltenVKFallback_ = enabled;
    spdlog::info("VulkanVideoSink: MoltenVK fallback {}", enabled ? "enabled" : "disabled");
}
#endif

void VulkanVideoSink::handleDeviceLost(const GpuFrame& frame) {
    deviceLost_ = true;
    hasFrame_ = false;
    spdlog::error("VulkanVideoSink: GPU device lost detected (frame ts={:.3f})",
                  frame.timestamp);
}

void VulkanVideoSink::updateFrameInfo(const GpuFrame& frame) {
    hasFrame_ = true;
    currentVkImage_ = frame.handle.nativeHandle;
    auxiliaryHandle_ = frame.handle.auxiliaryHandle;
    width_ = frame.width;
    height_ = frame.height;

    if (!frame.cpuData.empty()) {
        cpuFrameData_ = frame.cpuData;
    } else {
        cpuFrameData_.clear();
    }
}

} // namespace render
} // namespace hlplayer
