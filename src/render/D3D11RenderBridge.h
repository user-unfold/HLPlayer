#ifndef HLPLAYER_D3D11_RENDER_BRIDGE_H
#define HLPLAYER_D3D11_RENDER_BRIDGE_H

#include "VideoSink.h"

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

#include <cstdint>
#include <mutex>
#include <vector>

namespace hlplayer {
namespace render {

/// D3D11 render bridge: receives D3D11VA-decoded NV12 frames and copies them
/// into a pool of app-owned staging textures via CopySubresourceRegion.
/// Thread-safe (mutex-guarded presentFrame). Windows-only.
class HLPLAYER_RENDER_API D3D11RenderBridge : public IRenderBridge {
public:
    /// Construct with an externally owned D3D11 device.
    /// @param device  D3D11 device (shared from Qt or another owner). If null, a software device is created.
    /// @param ctx     D3D11 immediate context. If null, obtained from device.
    /// @param stagingPoolSize Number of staging textures to pre-allocate (default 6 = pool_size + 2).
    explicit D3D11RenderBridge(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                uint32_t stagingPoolSize = 6);
    ~D3D11RenderBridge() override;

    D3D11RenderBridge(const D3D11RenderBridge&) = delete;
    D3D11RenderBridge& operator=(const D3D11RenderBridge&) = delete;
    void presentFrame(const GpuFrame& frame) override;
    void onFormatChange(VideoFormat format) override;
    void reset() override;

    ID3D11Device* getDevice() const;

    /// Returns null if no frame has been presented yet.
    ID3D11Texture2D* getCurrentTexture() const;

    void getTextureSize(uint32_t& width, uint32_t& height) const;
    DXGI_FORMAT getTextureFormat() const;

    bool isDeviceLost() const;

private:
    /// Create/recreate staging textures if dimensions or format changed.
    void ensureStagingTextures(uint32_t width, uint32_t height, DXGI_FORMAT format);

    void handleDeviceLost(HRESULT hr);

    uint32_t stagingPoolSize_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> stagingTextures_;
    uint32_t currentTextureIndex_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;

    uint32_t textureWidth_ = 0;
    uint32_t textureHeight_ = 0;
    DXGI_FORMAT textureFormat_ = DXGI_FORMAT_UNKNOWN;

    mutable std::mutex mutex_;
    bool deviceLost_ = false;
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_D3D11_RENDER_BRIDGE_H
