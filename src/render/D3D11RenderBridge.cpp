#include "D3D11RenderBridge.h"

#ifdef _WIN32
#include <spdlog/spdlog.h>
#endif

namespace hlplayer {
namespace render {

#ifdef _WIN32

D3D11RenderBridge::D3D11RenderBridge(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                       uint32_t stagingPoolSize)
    : stagingPoolSize_(stagingPoolSize) {
    if (device) {
        device_ = device;
    } else {
        UINT createFlags = 0;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          createFlags, nullptr, 0, D3D11_SDK_VERSION,
                          device_.ReleaseAndGetAddressOf(), nullptr, nullptr);
    }

    if (ctx) {
        deviceContext_ = ctx;
    } else if (device_) {
        device_->GetImmediateContext(deviceContext_.ReleaseAndGetAddressOf());
    }
}

D3D11RenderBridge::~D3D11RenderBridge() {
    reset();
}

void D3D11RenderBridge::presentFrame(const GpuFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (deviceLost_ || !deviceContext_) {
        return;
    }

    if (frame.deviceLost) {
        deviceLost_ = true;
        return;
    }

    if (frame.handle.apiType != 1 || !frame.handle.nativeHandle) {
        return;
    }

    auto* srcTexture = static_cast<ID3D11Texture2D*>(frame.handle.nativeHandle);
    UINT srcIndex = static_cast<UINT>(reinterpret_cast<uintptr_t>(frame.handle.auxiliaryHandle));

    ensureStagingTextures(frame.width, frame.height, DXGI_FORMAT_NV12);

    if (stagingTextures_.empty() || !stagingTextures_[0]) {
        return;
    }

    currentTextureIndex_ = (currentTextureIndex_ + 1) % stagingPoolSize_;
    auto* dstTexture = stagingTextures_[currentTextureIndex_].Get();

    deviceContext_->CopySubresourceRegion(
        dstTexture, 0, 0, 0, 0, srcTexture, srcIndex, nullptr);
}

void D3D11RenderBridge::onFormatChange(VideoFormat format) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)format;
    stagingTextures_.clear();
    textureWidth_ = 0;
    textureHeight_ = 0;
    textureFormat_ = DXGI_FORMAT_UNKNOWN;
}

void D3D11RenderBridge::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stagingTextures_.clear();
    currentTextureIndex_ = 0;
    textureWidth_ = 0;
    textureHeight_ = 0;
    textureFormat_ = DXGI_FORMAT_UNKNOWN;
    deviceLost_ = false;
}

ID3D11Device* D3D11RenderBridge::getDevice() const {
    return device_.Get();
}

ID3D11Texture2D* D3D11RenderBridge::getCurrentTexture() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stagingTextures_.empty()) {
        return nullptr;
    }
    return stagingTextures_[currentTextureIndex_].Get();
}

void D3D11RenderBridge::getTextureSize(uint32_t& width, uint32_t& height) const {
    std::lock_guard<std::mutex> lock(mutex_);
    width = textureWidth_;
    height = textureHeight_;
}

DXGI_FORMAT D3D11RenderBridge::getTextureFormat() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return textureFormat_;
}

bool D3D11RenderBridge::isDeviceLost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceLost_;
}

void D3D11RenderBridge::ensureStagingTextures(uint32_t width, uint32_t height,
                                                DXGI_FORMAT format) {
    if (width == textureWidth_ && height == textureHeight_ && format == textureFormat_
        && stagingTextures_.size() == stagingPoolSize_) {
        return;
    }

    stagingTextures_.clear();
    stagingTextures_.resize(stagingPoolSize_);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    for (uint32_t i = 0; i < stagingPoolSize_; ++i) {
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr,
                                               stagingTextures_[i].ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            spdlog::error("D3D11RenderBridge: failed to create staging texture {}/{}: HRESULT 0x{:08X}",
                          i + 1, stagingPoolSize_, static_cast<unsigned>(hr));
            handleDeviceLost(hr);
            stagingTextures_.clear();
            return;
        }
    }

    textureWidth_ = width;
    textureHeight_ = height;
    textureFormat_ = format;
    spdlog::info("D3D11RenderBridge: allocated {} staging textures ({}x{} NV12)",
                 stagingPoolSize_, width, height);
}

void D3D11RenderBridge::handleDeviceLost(HRESULT hr) {
    deviceLost_ = true;
    deviceContext_.Reset();
    device_.Reset();
    stagingTextures_.clear();
    textureWidth_ = 0;
    textureHeight_ = 0;
    textureFormat_ = DXGI_FORMAT_UNKNOWN;
    spdlog::error("D3D11RenderBridge: device lost (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
}

#endif // _WIN32

} // namespace render
} // namespace hlplayer
