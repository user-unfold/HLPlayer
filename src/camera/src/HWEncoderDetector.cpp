#include "hlplayer/HWEncoderDetector.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace hlplayer {

bool HWEncoderDetector::probeEncoder(const char* name, EncoderInfo& info) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) {
        info.available = false;
        return false;
    }
    info.available = true;
    return true;
}

std::vector<EncoderInfo> HWEncoderDetector::detectAll() {
    std::vector<EncoderInfo> result;

    EncoderInfo nvenc{HWEncoderType::NVENC, "h264_nvenc", "NVIDIA NVENC H.264", "", AV_PIX_FMT_NONE};
    if (probeEncoder("h264_nvenc", nvenc)) result.push_back(nvenc);

    EncoderInfo qsv{HWEncoderType::QSV, "h264_qsv", "Intel QuickSync H.264", "d3d11va", AV_PIX_FMT_D3D11};
    if (probeEncoder("h264_qsv", qsv)) result.push_back(qsv);

    EncoderInfo amf{HWEncoderType::AMF, "h264_amf", "AMD AMF H.264", "d3d11va", AV_PIX_FMT_D3D11};
    if (probeEncoder("h264_amf", amf)) result.push_back(amf);

    EncoderInfo x264{HWEncoderType::X264, "libx264", "x264 Software", "", AV_PIX_FMT_NONE};
    if (probeEncoder("libx264", x264)) result.push_back(x264);

    return result;
}

EncoderInfo HWEncoderDetector::detectBest() {
    auto all = detectAll();
    return all.empty() ? EncoderInfo{} : all.front();
}

bool HWEncoderDetector::isAvailable(HWEncoderType type) {
    auto all = detectAll();
    for (const auto& e : all) {
        if (e.type == type) return true;
    }
    return false;
}

} // namespace hlplayer
