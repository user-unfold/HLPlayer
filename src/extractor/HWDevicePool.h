#ifndef HLPLAYER_HW_DEVICE_POOL_H
#define HLPLAYER_HW_DEVICE_POOL_H

#ifdef _WIN32
    #ifdef HLPLAYER_EXTRACTOR_EXPORTS
        #define HLPLAYER_EXTRACTOR_API __declspec(dllexport)
    #else
        #define HLPLAYER_EXTRACTOR_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_EXTRACTOR_API
#endif

#include "FFmpegRAII.h"

#include <mutex>
#include <unordered_map>

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11DeviceContext;
#endif

namespace hlplayer {
namespace extractor {

/// Global singleton managing GPU device contexts for FFmpeg hw-accel decoding.
/// Devices are created lazily and held for process lifetime.  Thread-safe.
class HLPLAYER_EXTRACTOR_API HWDevicePool {
public:
    static HWDevicePool& instance();

    HWDevicePool(const HWDevicePool&) = delete;
    HWDevicePool& operator=(const HWDevicePool&) = delete;

    /// Returns pool-owned raw pointer.  Caller MUST av_buffer_ref() before
    /// sharing across threads or scopes.
    AVBufferRef* getDeviceRef(AVHWDeviceType type);

#ifdef _WIN32
    /// Inject an externally-created D3D11 device.  Replaces any existing
    /// D3D11VA device.  Caller retains COM ownership of device.
    bool setExternalD3D11Device(ID3D11Device* device);
#endif

    void clearAll();

private:
    HWDevicePool() = default;
    ~HWDevicePool() = default;

    hlplayer::ffmpeg::AVBufferRefPtr createDevice(AVHWDeviceType type);

    std::mutex m_mutex;
    std::unordered_map<int, hlplayer::ffmpeg::AVBufferRefPtr> m_devices;
};

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_HW_DEVICE_POOL_H
