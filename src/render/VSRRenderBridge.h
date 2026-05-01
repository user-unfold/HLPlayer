#ifndef HLPLAYER_VSRRENDERBRIDGE_H
#define HLPLAYER_VSRRENDERBRIDGE_H

#include "VideoSink.h"
#include <hlplayer/Result.h>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace hlplayer {
namespace render {

/// Render bridge for VSR (Video Super Resolution) pipeline output.
/// Supports D3D11 and Vulkan backends with zero-copy GPU rendering.
/// Staging textures carry D3D11_BIND_RENDER_TARGET for GPU format conversion.
/// Thread-safe (mutex-guarded).
class HLPLAYER_RENDER_API VSRRenderBridge : public IRenderBridge {
public:
    enum class Backend : uint8_t {
        Auto = 0,   ///< Auto-detect from GpuFrame.apiType (default)
        D3D11 = 1,  ///< Expect D3D11 textures (apiType == 1)
        Vulkan = 2  ///< Expect Vulkan textures (apiType == 2)
    };

    explicit VSRRenderBridge(Backend backend = Backend::Auto, uint32_t stagingPoolSize = 4);
    ~VSRRenderBridge() override;

    VSRRenderBridge(const VSRRenderBridge&) = delete;
    VSRRenderBridge& operator=(const VSRRenderBridge&) = delete;

    void presentFrame(const GpuFrame& frame) override;
    void onFormatChange(VideoFormat format) override;
    void reset() override;

#ifdef _WIN32
    Result<void> setD3D11Device(ID3D11Device* device, ID3D11DeviceContext* ctx = nullptr);
    ID3D11Texture2D* getCurrentTexture() const;
    void getTextureSize(uint32_t& width, uint32_t& height) const;
#endif

    void* getCurrentVkImage() const;
    void* getAuxiliaryHandle() const;
    Result<void> setRhiHandle(void* rhi);

    bool isDeviceLost() const;
    PixelFormat getOutputFormat() const;
    Backend activeBackend() const;
    void getFrameSize(uint32_t& width, uint32_t& height) const;
    bool needsFormatConversion() const;
    static PixelFormat getRecommendedDisplayFormat();

    using FrameCallback = std::function<void()>;
    /// WARNING: called under internal mutex lock — do NOT re-enter the bridge.
    void setFrameAvailableCallback(FrameCallback callback);

private:
#ifdef _WIN32
    void ensureStagingTextures(uint32_t width, uint32_t height, DXGI_FORMAT format);
    Result<void> presentD3D11(const GpuFrame& frame);
    void handleD3D11DeviceLost(HRESULT hr);
    static DXGI_FORMAT pixelFormatToDxgi(PixelFormat fmt);
    static DXGI_FORMAT displayFormatToDxgi(PixelFormat fmt);
#endif

    Result<void> presentVulkan(const GpuFrame& frame);
    void handleVulkanDeviceLost(const GpuFrame& frame);
    Result<void> validateFrame(const GpuFrame& frame);
    Backend detectBackend(const GpuFrame& frame) const;

    Backend backend_;
    Backend activeBackend_ = Backend::Auto;
    uint32_t stagingPoolSize_;

#ifdef _WIN32
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> stagingTextures_;
    uint32_t currentTextureIndex_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
#endif

    void* currentVkImage_ = nullptr;
    void* auxiliaryHandle_ = nullptr;
    void* rhi_ = nullptr;

    uint32_t textureWidth_ = 0;
    uint32_t textureHeight_ = 0;
    PixelFormat outputFormat_ = PixelFormat::Unknown;
    VideoFormat videoFormat_ = VideoFormat::Unknown;

    mutable std::mutex mutex_;
    bool deviceLost_ = false;
    FrameCallback onFrameAvailable_;
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_VSRRENDERBRIDGE_H
