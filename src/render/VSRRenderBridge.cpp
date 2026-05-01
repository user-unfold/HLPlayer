#include "VSRRenderBridge.h"

#include <spdlog/spdlog.h>

namespace hlplayer {
namespace render {

static constexpr uint32_t kApiTypeD3D11 = 1;
static constexpr uint32_t kApiTypeVulkan = 2;

VSRRenderBridge::VSRRenderBridge(Backend backend, uint32_t stagingPoolSize)
    : backend_(backend), stagingPoolSize_(stagingPoolSize) {
    spdlog::info("VSRRenderBridge constructed (backend={}, poolSize={})",
                 static_cast<int>(backend_), stagingPoolSize_);
}

VSRRenderBridge::~VSRRenderBridge() {
    reset();
}

void VSRRenderBridge::presentFrame(const GpuFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (deviceLost_) {
        spdlog::warn("VSRRenderBridge: dropping frame, device is lost");
        return;
    }

    auto validation = validateFrame(frame);
    if (validation.hasError()) {
        spdlog::warn("VSRRenderBridge: invalid frame (error={})",
                     static_cast<int>(validation.error()));
        return;
    }

    if (frame.deviceLost) {
        Backend detected = detectBackend(frame);
        switch (detected) {
            case Backend::D3D11:
#ifdef _WIN32
                handleD3D11DeviceLost(E_FAIL);
#endif
                break;
            case Backend::Vulkan:
                handleVulkanDeviceLost(frame);
                break;
            default:
                break;
        }
        return;
    }

    Backend backend = (backend_ == Backend::Auto) ? detectBackend(frame) : backend_;

    Result<void> result = Result<void>::success();
    switch (backend) {
        case Backend::D3D11:
#ifdef _WIN32
            result = presentD3D11(frame);
#else
            spdlog::warn("VSRRenderBridge: D3D11 backend not available on this platform");
            result = Result<void>::error(PlayerError::UnsupportedFormat);
#endif
            break;
        case Backend::Vulkan:
            result = presentVulkan(frame);
            break;
        default:
            spdlog::warn("VSRRenderBridge: unknown backend {}", static_cast<int>(backend));
            result = Result<void>::error(PlayerError::InvalidState);
            break;
    }

    if (result.hasValue() && onFrameAvailable_) {
        onFrameAvailable_();
    }
}

void VSRRenderBridge::onFormatChange(VideoFormat format) {
    std::lock_guard<std::mutex> lock(mutex_);
    videoFormat_ = format;
    spdlog::info("VSRRenderBridge: format changed to {}", static_cast<int>(format));

#ifdef _WIN32
    stagingTextures_.clear();
#endif
    textureWidth_ = 0;
    textureHeight_ = 0;
    outputFormat_ = PixelFormat::Unknown;
    currentVkImage_ = nullptr;
    auxiliaryHandle_ = nullptr;
}

void VSRRenderBridge::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    stagingTextures_.clear();
    currentTextureIndex_ = 0;
    deviceContext_.Reset();
    device_.Reset();
#endif
    currentVkImage_ = nullptr;
    auxiliaryHandle_ = nullptr;
    textureWidth_ = 0;
    textureHeight_ = 0;
    outputFormat_ = PixelFormat::Unknown;
    videoFormat_ = VideoFormat::Unknown;
    deviceLost_ = false;
    spdlog::info("VSRRenderBridge: reset");
}

#ifdef _WIN32

Result<void> VSRRenderBridge::setD3D11Device(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    if (!device) {
        return Result<void>::error(PlayerError::InvalidState);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    device_ = device;
    if (ctx) {
        deviceContext_ = ctx;
    } else {
        device_->GetImmediateContext(deviceContext_.ReleaseAndGetAddressOf());
    }
    activeBackend_ = Backend::D3D11;
    spdlog::info("VSRRenderBridge: D3D11 device set");
    return Result<void>::success();
}

ID3D11Texture2D* VSRRenderBridge::getCurrentTexture() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stagingTextures_.empty()) {
        return nullptr;
    }
    return stagingTextures_[currentTextureIndex_].Get();
}

void VSRRenderBridge::getTextureSize(uint32_t& width, uint32_t& height) const {
    std::lock_guard<std::mutex> lock(mutex_);
    width = textureWidth_;
    height = textureHeight_;
}

DXGI_FORMAT VSRRenderBridge::pixelFormatToDxgi(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::NV12:    return DXGI_FORMAT_NV12;
        case PixelFormat::P010:    return DXGI_FORMAT_P010;
        case PixelFormat::RGBA8:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:                   return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT VSRRenderBridge::displayFormatToDxgi(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::RGBA16F:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::RGBA8:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        default:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

void VSRRenderBridge::ensureStagingTextures(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (!stagingTextures_.empty() && stagingTextures_.size() == stagingPoolSize_) {
        D3D11_TEXTURE2D_DESC desc{};
        stagingTextures_[0]->GetDesc(&desc);
        if (desc.Width == width && desc.Height == height && desc.Format == format) {
            return;
        }
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
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    for (uint32_t i = 0; i < stagingPoolSize_; ++i) {
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr,
                                               stagingTextures_[i].ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            spdlog::error("VSRRenderBridge: failed to create staging texture {}/{} ({}x{}): HRESULT 0x{:08X}",
                          i + 1, stagingPoolSize_, width, height, static_cast<unsigned>(hr));
            handleD3D11DeviceLost(hr);
            return;
        }
    }

    textureWidth_ = width;
    textureHeight_ = height;
    spdlog::info("VSRRenderBridge: allocated {} staging textures ({}x{} format=0x{:X})",
                 stagingPoolSize_, width, height, static_cast<unsigned>(format));
}

Result<void> VSRRenderBridge::presentD3D11(const GpuFrame& frame) {
    if (!deviceContext_) {
        return Result<void>::error(PlayerError::DeviceLost);
    }

    auto* srcTexture = static_cast<ID3D11Texture2D*>(frame.handle.nativeHandle);
    UINT srcIndex = static_cast<UINT>(reinterpret_cast<uintptr_t>(frame.handle.auxiliaryHandle));

    if (!srcTexture) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    activeBackend_ = Backend::D3D11;

    DXGI_FORMAT srcFormat = pixelFormatToDxgi(frame.format);
    if (srcFormat == DXGI_FORMAT_UNKNOWN) {
        spdlog::warn("VSRRenderBridge: unsupported pixel format {} for D3D11",
                     static_cast<int>(frame.format));
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    ensureStagingTextures(frame.width, frame.height, srcFormat);

    if (stagingTextures_.empty() || !stagingTextures_[0]) {
        return Result<void>::error(PlayerError::DeviceLost);
    }

    currentTextureIndex_ = (currentTextureIndex_ + 1) % stagingPoolSize_;
    auto* dstTexture = stagingTextures_[currentTextureIndex_].Get();

    deviceContext_->CopySubresourceRegion(dstTexture, 0, 0, 0, 0, srcTexture, srcIndex, nullptr);
    outputFormat_ = frame.format;

    spdlog::debug("VSRRenderBridge(D3D11): presented frame {}x{} format={} ts={:.3f}",
                  frame.width, frame.height, static_cast<int>(frame.format), frame.timestamp);

    return Result<void>::success();
}

void VSRRenderBridge::handleD3D11DeviceLost(HRESULT hr) {
    deviceLost_ = true;
    deviceContext_.Reset();
    device_.Reset();
    stagingTextures_.clear();
    textureWidth_ = 0;
    textureHeight_ = 0;
    outputFormat_ = PixelFormat::Unknown;
    spdlog::error("VSRRenderBridge: D3D11 device lost (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
}

#endif // _WIN32

void* VSRRenderBridge::getCurrentVkImage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceLost_ ? nullptr : currentVkImage_;
}

void* VSRRenderBridge::getAuxiliaryHandle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceLost_ ? nullptr : auxiliaryHandle_;
}

Result<void> VSRRenderBridge::setRhiHandle(void* rhi) {
    if (!rhi) {
        return Result<void>::error(PlayerError::InvalidState);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    rhi_ = rhi;
    spdlog::debug("VSRRenderBridge: Qt RHI handle set");
    return Result<void>::success();
}

bool VSRRenderBridge::isDeviceLost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceLost_;
}

PixelFormat VSRRenderBridge::getOutputFormat() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outputFormat_;
}

VSRRenderBridge::Backend VSRRenderBridge::activeBackend() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeBackend_;
}

void VSRRenderBridge::getFrameSize(uint32_t& width, uint32_t& height) const {
    std::lock_guard<std::mutex> lock(mutex_);
    width = textureWidth_;
    height = textureHeight_;
}

bool VSRRenderBridge::needsFormatConversion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outputFormat_ == PixelFormat::RGBA16F;
}

PixelFormat VSRRenderBridge::getRecommendedDisplayFormat() {
    return PixelFormat::RGBA8;
}

void VSRRenderBridge::setFrameAvailableCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    onFrameAvailable_ = std::move(callback);
}

Result<void> VSRRenderBridge::presentVulkan(const GpuFrame& frame) {
    if (!frame.handle.nativeHandle) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    activeBackend_ = Backend::Vulkan;
    currentVkImage_ = frame.handle.nativeHandle;
    auxiliaryHandle_ = frame.handle.auxiliaryHandle;
    textureWidth_ = frame.width;
    textureHeight_ = frame.height;
    outputFormat_ = frame.format;

#ifdef HAS_QT_RHI
    if (rhi_) {
        spdlog::debug("VSRRenderBridge(Vulkan): frame {}x{} format={} imported to Qt RHI",
                      frame.width, frame.height, static_cast<int>(frame.format));
    } else {
        spdlog::warn("VSRRenderBridge: Qt RHI handle not set, VkImage stored but not imported");
    }
#else
    spdlog::debug("VSRRenderBridge(Vulkan): frame {}x{} format={} stored (no RHI)",
                  frame.width, frame.height, static_cast<int>(frame.format));
#endif

    return Result<void>::success();
}

void VSRRenderBridge::handleVulkanDeviceLost(const GpuFrame& frame) {
    deviceLost_ = true;
    currentVkImage_ = nullptr;
    auxiliaryHandle_ = nullptr;
    spdlog::error("VSRRenderBridge: Vulkan device lost detected (frame ts={:.3f})",
                  frame.timestamp);
}

Result<void> VSRRenderBridge::validateFrame(const GpuFrame& frame) {
    if (!frame.handle.nativeHandle) {
        return Result<void>::error(PlayerError::InvalidState);
    }
    if (frame.width == 0 || frame.height == 0) {
        return Result<void>::error(PlayerError::InvalidState);
    }
    return Result<void>::success();
}

VSRRenderBridge::Backend VSRRenderBridge::detectBackend(const GpuFrame& frame) const {
    switch (frame.handle.apiType) {
        case kApiTypeD3D11: return Backend::D3D11;
        case kApiTypeVulkan: return Backend::Vulkan;
        default:
            if (frame.format == PixelFormat::Vulkan) {
                return Backend::Vulkan;
            }
            return Backend::D3D11;
    }
}

} // namespace render
} // namespace hlplayer
