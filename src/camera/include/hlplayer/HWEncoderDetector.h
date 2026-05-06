// HWEncoderDetector.h
#pragma once

#include <hlplayer/CameraExport.h>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace hlplayer {

enum class HWEncoderType {
    NVENC,
    QSV,
    AMF,
    X264,
    None
};

struct EncoderInfo {
    HWEncoderType type = HWEncoderType::None;
    std::string name;
    std::string displayName;
    std::string hwDeviceType;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
    bool available = false;
};

class HLPLAYER_CAMERA_API HWEncoderDetector {
public:
    static std::vector<EncoderInfo> detectAll();

    static EncoderInfo detectBest();

    static bool isAvailable(HWEncoderType type);

private:
    static bool probeEncoder(const char* name, EncoderInfo& info);
};

} // namespace hlplayer
