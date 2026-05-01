#ifndef HLPLAYER_VULKANVIDEOSINK_H
#define HLPLAYER_VULKANVIDEOSINK_H

#ifndef HLPLAYER_RENDER_API
#ifdef _WIN32
#ifdef HLPLAYER_RENDER_EXPORTS
#define HLPLAYER_RENDER_API __declspec(dllexport)
#else
#define HLPLAYER_RENDER_API __declspec(dllimport)
#endif
#else
#define HLPLAYER_RENDER_API
#endif
#endif

#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/GpuFrameContract.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <cstddef>

namespace hlplayer {
namespace render {

/// Vulkan video sink implementing IVideoFrameSink.
/// Receives GPU frames from L3 (AI) or L5 (Decoder) and prepares them
/// for Qt RHI/QML layer consumption. Provides access to VkImage or compatible
/// texture handles for zero-copy rendering.
class HLPLAYER_RENDER_API VulkanVideoSink : public IVideoFrameSink {
public:
    VulkanVideoSink();
    ~VulkanVideoSink() override;

    /// IVideoFrameSink implementation
    void onFrame(const GpuFrame& frame) override;
    void onFormatChanged(VideoFormat format) override;
    void reset() override;

    /// Get the current frame's native VkImage handle (Vulkan)
    /// Returns nullptr if no frame is available or device is lost
    void* getVkImage() const;

    /// Get the current frame's auxiliary handle (e.g., VkDeviceMemory, VkImageView)
    /// Returns nullptr if not available
    void* getAuxiliaryHandle() const;

    /// Get the current frame dimensions
    void getFrameSize(uint32_t& width, uint32_t& height) const;

    /// Get the current video format
    VideoFormat getFormat() const { return currentFormat_; }

    /// Check if GPU device is lost
    bool isDeviceLost() const { return deviceLost_; }

    /// Check if a frame is available for rendering
    bool hasFrame() const { return hasFrame_; }

    /// Check if CPU frame data is available
    bool hasCpuFrame() const { return !cpuFrameData_.empty(); }

    /// Get CPU frame data pointer (RGBA pixels)
    const uint8_t* getCpuFrameData() const { return cpuFrameData_.data(); }

    /// Get CPU frame data size in bytes
    size_t getCpuFrameDataSize() const { return cpuFrameData_.size(); }

    /// Set the Qt RHI handle for texture mapping (optional)
    /// @param rhi Pointer to QRhi instance
    void setRhiHandle(void* rhi);

    /// Callback invoked after a new frame is stored.
    /// Called while the internal mutex is held, so callees must NOT
    /// re-enter the sink via locking methods (getFrameSize, etc.).
    std::function<void()> onFrameAvailable;

    /// Get frame dimensions without locking the mutex.
    /// Safe to call from onFrameAvailable (which is invoked under lock).
    uint32_t frameWidth() const { return width_; }
    uint32_t frameHeight() const { return height_; }

#ifdef __APPLE__
    /// On macOS, check if MoltenVK is available
    bool isMoltenVKAvailable() const;

    /// Set MoltenVK fallback mode
    /// @param enabled If true, use CPU fallback when MoltenVK is unavailable
    void setMoltenVKFallback(bool enabled);
#endif

private:
    void handleDeviceLost(const GpuFrame& frame);
    void updateFrameInfo(const GpuFrame& frame);

    mutable std::mutex mutex_;
    void* currentVkImage_ = nullptr;
    void* auxiliaryHandle_ = nullptr;
    void* rhi_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VideoFormat currentFormat_ = VideoFormat::Unknown;
    bool deviceLost_ = false;
    bool hasFrame_ = false;
    std::vector<uint8_t> cpuFrameData_;

#ifdef __APPLE__
    bool moltenVKFallback_ = false;
#endif
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_VULKANVIDEOSINK_H
