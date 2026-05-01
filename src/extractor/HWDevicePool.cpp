#include "HWDevicePool.h"

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <d3d11.h>
extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}
#endif

namespace hlplayer {
namespace extractor {

HWDevicePool& HWDevicePool::instance() {
    static HWDevicePool pool;
    return pool;
}

AVBufferRef* HWDevicePool::getDeviceRef(AVHWDeviceType type) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_devices.find(static_cast<int>(type));
    if (it != m_devices.end()) {
        return it->second.get();
    }

    auto device = createDevice(type);
    if (!device) {
        return nullptr;
    }

    auto* raw = device.get();
    m_devices[static_cast<int>(type)] = std::move(device);
    return raw;
}

hlplayer::ffmpeg::AVBufferRefPtr HWDevicePool::createDevice(AVHWDeviceType type) {
    AVBufferRef* ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&ctx, type, nullptr, nullptr, 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("[HWDevicePool] Failed to create device (type={}): {}",
                      static_cast<int>(type), errBuf);
        return nullptr;
    }
    spdlog::info("[HWDevicePool] Created device context for type {}",
                 static_cast<int>(type));
    return hlplayer::ffmpeg::AVBufferRefPtr(ctx);
}

#ifdef _WIN32
bool HWDevicePool::setExternalD3D11Device(ID3D11Device* device) {
    if (!device) {
        spdlog::error("[HWDevicePool] setExternalD3D11Device: device must not be null");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    AVBufferRef* ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!ref) {
        spdlog::error("[HWDevicePool] Failed to allocate D3D11VA device context");
        return false;
    }

    auto* hwCtx = reinterpret_cast<AVD3D11VADeviceContext*>(
        reinterpret_cast<AVHWDeviceContext*>(ref->data)->hwctx);
    hwCtx->device = device;
    hwCtx->lock = nullptr;
    hwCtx->unlock = nullptr;

    int ret = av_hwdevice_ctx_init(ref);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("[HWDevicePool] Failed to init external D3D11VA device: {}",
                      errBuf);
        av_buffer_unref(&ref);
        return false;
    }

    m_devices[static_cast<int>(AV_HWDEVICE_TYPE_D3D11VA)] =
        hlplayer::ffmpeg::AVBufferRefPtr(ref);
    spdlog::info("[HWDevicePool] External D3D11 device injected successfully");
    return true;
}
#endif

void HWDevicePool::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_devices.clear();
    spdlog::info("[HWDevicePool] All device contexts cleared");
}
}
}
