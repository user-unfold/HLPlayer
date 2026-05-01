#ifndef HLPLAYER_FFMPEG_RAAI_H
#define HLPLAYER_FFMPEG_RAAI_H

#include <spdlog/spdlog.h>
#include <hlplayer/Result.h>

#ifdef _WIN32
    #define __STDC_CONSTANT_MACROS
    #define __STDC_FORMAT_MACROS
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_dxva2.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
}

#include <memory>

namespace hlplayer {
namespace ffmpeg {

namespace detail {

/// Deleter for AVFormatContext
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

/// Deleter for AVCodecContext
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

/// Deleter for AVBufferRef (for hardware frames)
struct AVBufferRefDeleter {
    void operator()(AVBufferRef* ref) const {
        if (ref) {
            av_buffer_unref(&ref);
        }
    }
};

/// Deleter for AVPacket
struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) {
            av_packet_free(&pkt);
        }
    }
};

/// Deleter for AVFrame
struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

/// Deleter for AVDictionary
struct AVDictionaryDeleter {
    void operator()(AVDictionary* dict) const {
        if (dict) {
            av_dict_free(&dict);
        }
    }
};

/// Deleter for SwsContext (software scale context)
struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) {
            sws_freeContext(ctx);
        }
    }
};

/// Deleter for AVFilterGraph
struct AVFilterGraphDeleter {
    void operator()(AVFilterGraph* graph) const {
        if (graph) {
            avfilter_graph_free(&graph);
        }
    }
};

} // namespace detail

// ============================================================================
// RAII Wrapper Types
// ============================================================================

using AVFormatContextPtr = std::unique_ptr<AVFormatContext, detail::AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, detail::AVCodecContextDeleter>;
using AVBufferRefPtr = std::unique_ptr<AVBufferRef, detail::AVBufferRefDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, detail::AVPacketDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, detail::AVFrameDeleter>;
using AVDictionaryPtr = std::unique_ptr<AVDictionary, detail::AVDictionaryDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, detail::SwsContextDeleter>;
using AVFilterGraphPtr = std::unique_ptr<AVFilterGraph, detail::AVFilterGraphDeleter>;

// ============================================================================
// Helper Functions
// ============================================================================

/// Create an AVPacket with RAII
inline AVPacketPtr makeAVPacket() {
    return AVPacketPtr(av_packet_alloc());
}

/// Create an AVFrame with RAII
inline AVFramePtr makeAVFrame() {
    return AVFramePtr(av_frame_alloc());
}

/// Create an AVDictionary wrapper from raw pointer
inline AVDictionaryPtr wrapAVDictionary(AVDictionary* dict) {
    return AVDictionaryPtr(dict);
}

/// Convert AVDictionary to map for AVDictionary parameter injection
inline AVDictionaryPtr createAVDictionary(const char* key, const char* value) {
    AVDictionary* dict = nullptr;
    av_dict_set(&dict, key, value, 0);
    return AVDictionaryPtr(dict);
}

/// Create empty AVDictionary
inline AVDictionaryPtr createEmptyAVDictionary() {
    return AVDictionaryPtr(nullptr);
}

/// Add multiple options to AVDictionary
inline AVDictionaryPtr appendToDictionary(AVDictionaryPtr dict,
                                          const char* key,
                                          const char* value) {
    AVDictionary* raw = dict.release();
    av_dict_set(&raw, key, value, 0);
    return AVDictionaryPtr(raw);
}

// ============================================================================
// Hardware Device Context Helpers
// ============================================================================

/// Create hardware device context for specified device type
inline AVBufferRefPtr createHWDeviceContext(AVHWDeviceType hwDeviceType) {
    AVBufferRef* hwDeviceCtx = nullptr;
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx, hwDeviceType, nullptr, nullptr, 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to create hardware device context: {}", errBuf);
        return nullptr;
    }
    return AVBufferRefPtr(hwDeviceCtx);
}

#ifdef _WIN32

/// Create D3D11VA hardware device context
inline AVBufferRefPtr createD3D11DeviceContext(ID3D11Device* device = nullptr) {
    AVBufferRef* hwDeviceCtx = nullptr;

    // If device provided, use it; otherwise create new one
    if (device) {
        hwDeviceCtx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!hwDeviceCtx) {
            spdlog::error("Failed to allocate D3D11VA device context");
            return nullptr;
        }

        auto* hwCtx = reinterpret_cast<AVD3D11VADeviceContext*>(
            reinterpret_cast<AVHWDeviceContext*>(hwDeviceCtx->data)->hwctx);
        hwCtx->device = device;
        hwCtx->lock = nullptr;
        hwCtx->unlock = nullptr;

        int ret = av_hwdevice_ctx_init(hwDeviceCtx);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("Failed to init external D3D11VA device: {}", errBuf);
            av_buffer_unref(&hwDeviceCtx);
            return nullptr;
        }
    } else {
        int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("Failed to create D3D11VA device context: {}", errBuf);
            return nullptr;
        }
    }

    return AVBufferRefPtr(hwDeviceCtx);
}

/// Create DXVA2 hardware device context
inline AVBufferRefPtr createDXVA2DeviceContext() {
    AVBufferRef* hwDeviceCtx = nullptr;
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to create DXVA2 device context: {}", errBuf);
        return nullptr;
    }
    return AVBufferRefPtr(hwDeviceCtx);
}

#endif // _WIN32

#ifdef __APPLE__

/// Create VideoToolbox hardware device context
inline AVBufferRefPtr createVideoToolboxDeviceContext() {
    AVBufferRef* hwDeviceCtx = nullptr;
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to create VideoToolbox device context: {}", errBuf);
        return nullptr;
    }
    return AVBufferRefPtr(hwDeviceCtx);
}

#endif // __APPLE__

// ============================================================================
// Hardware Frame Transfer Helpers
// ============================================================================

/// Transfer frame to GPU if needed (zero-copy preferred)
inline Result<AVFramePtr> ensureHardwareFrame(AVFrame* srcFrame, AVBufferRefPtr hwDeviceCtx) {
    if (!srcFrame || !hwDeviceCtx) {
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    // If frame is already in hardware, just wrap it
    if (srcFrame->hw_frames_ctx) {
        AVFramePtr dstFrame = makeAVFrame();
        if (!dstFrame) {
            return Result<AVFramePtr>::error(PlayerError::DecodeError);
        }

        int ret = av_frame_ref(dstFrame.get(), srcFrame);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("Failed to reference hardware frame: {}", errBuf);
            return Result<AVFramePtr>::error(PlayerError::DecodeError);
        }

        return Result<AVFramePtr>::success(std::move(dstFrame));
    }

    // Transfer to hardware frame (zero-copy preferred)
    AVFramePtr hwFrame = makeAVFrame();
    if (!hwFrame) {
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    hwFrame->format = AV_PIX_FMT_NONE; // Let FFmpeg determine format from device context

    AVHWFramesContext* framesCtx = (AVHWFramesContext*)hwDeviceCtx->data;
    hwFrame->format = framesCtx->format;

    int ret = av_hwframe_get_buffer(hwDeviceCtx.get(), hwFrame.get(), 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to allocate hardware frame: {}", errBuf);
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    ret = av_hwframe_transfer_data(hwFrame.get(), srcFrame, 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::warn("Failed to transfer frame to hardware (fallback to CPU): {}", errBuf);
        // Don't fail - allow CPU fallback
        return Result<AVFramePtr>::success(makeAVFrame());
    }

    return Result<AVFramePtr>::success(std::move(hwFrame));
}

} // namespace ffmpeg
} // namespace hlplayer

#endif // HLPLAYER_FFMPEG_RAAI_H
