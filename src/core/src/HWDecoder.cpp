#include <hlplayer/HWDecoder.h>

namespace hlplayer {

#ifdef HAS_FFMPEG

// FFmpeg hwaccel stubs — will be implemented when FFmpeg is integrated.
// These provide the Vulkan/CUDA/D3D11 hardware decode paths via
// av_hwdevice_ctx_create and avcodec_get_hw_config.

IHWDecoder* createFFmpegHWDecoder(const DecoderConfig& config) {
    (void)config;
    // TODO: Initialize AVCodecContext with hwaccel
    // TODO: Create AVBufferRef for hw device context (Vulkan: av_hwdevice_ctx_create)
    // TODO: Configure AVCodecParameters from Codec enum
    return nullptr;
}

#endif // HAS_FFMPEG

} // namespace hlplayer
