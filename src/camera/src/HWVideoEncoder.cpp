#include "hlplayer/HWVideoEncoder.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
}

namespace hlplayer {

HWVideoEncoder::HWVideoEncoder() = default;

HWVideoEncoder::~HWVideoEncoder() {
    if (hwFrame_) {
        av_frame_free(&hwFrame_);
    }
    if (hwFramesCtx_) {
        av_buffer_unref(&hwFramesCtx_);
    }
    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
    }
    if (encCtx_) {
        avcodec_free_context(&encCtx_);
    }
}

Result<void> HWVideoEncoder::initHardwareContext() {
    if (config_.encoderInfo.hwPixFmt == AV_PIX_FMT_NONE) {
        return Result<void>::success();
    }

    AVBufferRef* hwDeviceCtx = nullptr;
    int ret = -1;

    // Try each D3D11 adapter until one succeeds. Device "0" is the primary
    // GPU (usually the one connected to the display), which is most likely
    // to support NV12 textures required for hardware encoding.
    // We try "0", "1", then "auto" (no device hint).
    const char* devices[] = {"0", "1", nullptr};
    for (const char* dev : devices) {
        AVDictionary* hwOpts = nullptr;
        if (dev) {
            av_dict_set(&hwOpts, "device", dev, 0);
        }
        ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA,
                                     nullptr, hwOpts, 0);
        av_dict_free(&hwOpts);
        if (ret >= 0) break;
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::debug("HWVideoEncoder: D3D11 device '{}' failed: {}", dev ? dev : "auto", errBuf);
    }

    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: av_hwdevice_ctx_create failed: {}", errBuf);
        return Result<void>::error(PlayerError::DeviceLost);
    }
    hwDeviceCtx_ = hwDeviceCtx;

    AVBufferRef* hwFramesRef = av_hwframe_ctx_alloc(hwDeviceCtx_);
    if (!hwFramesRef) {
        spdlog::error("HWVideoEncoder: av_hwframe_ctx_alloc failed");
        return Result<void>::error(PlayerError::DecodeError);
    }

    AVHWFramesContext* framesCtx = reinterpret_cast<AVHWFramesContext*>(hwFramesRef->data);
    framesCtx->format = AV_PIX_FMT_D3D11;
    framesCtx->sw_format = AV_PIX_FMT_NV12;
    framesCtx->width = config_.width;
    framesCtx->height = config_.height;
    framesCtx->initial_pool_size = 6;

    ret = av_hwframe_ctx_init(hwFramesRef);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: av_hwframe_ctx_init failed (code={}): {} "
                      "(D3D11 NV12 {}x{})",
                      ret, errBuf, config_.width, config_.height);
        av_buffer_unref(&hwFramesRef);
        return Result<void>::error(PlayerError::DecodeError);
    }
    hwFramesCtx_ = hwFramesRef;

    AVFrame* hwFrame = av_frame_alloc();
    if (!hwFrame) {
        spdlog::error("HWVideoEncoder: av_frame_alloc failed");
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = av_hwframe_get_buffer(hwFramesCtx_, hwFrame, 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: av_hwframe_get_buffer failed: {}", errBuf);
        av_frame_free(&hwFrame);
        return Result<void>::error(PlayerError::DecodeError);
    }

    hwFrame_ = hwFrame;
    return Result<void>::success();
}

Result<void> HWVideoEncoder::initEncoder() {
    const AVCodec* codec = avcodec_find_encoder_by_name(config_.encoderInfo.name.c_str());
    if (!codec) {
        spdlog::error("HWVideoEncoder: encoder '{}' not found", config_.encoderInfo.name);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    AVCodecContext* encCtx = avcodec_alloc_context3(codec);
    if (!encCtx) {
        spdlog::error("HWVideoEncoder: avcodec_alloc_context3 failed");
        return Result<void>::error(PlayerError::DecodeError);
    }
    encCtx_ = encCtx;

    encCtx->width = config_.width;
    encCtx->height = config_.height;
    encCtx->time_base = AVRational{1, config_.fps};
    encCtx->framerate = AVRational{config_.fps, 1};
    encCtx->bit_rate = config_.bitrate;
    encCtx->gop_size = config_.gopSize;
    encCtx->max_b_frames = config_.maxBFrames;

    if (hwFramesCtx_) {
        encCtx->hw_frames_ctx = av_buffer_ref(hwFramesCtx_);
        if (!encCtx->hw_frames_ctx) {
            spdlog::error("HWVideoEncoder: av_buffer_ref failed for hw_frames_ctx");
            return Result<void>::error(PlayerError::DecodeError);
        }
        encCtx->pix_fmt = config_.encoderInfo.hwPixFmt;
    } else {
        encCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    AVDictionary* opts = nullptr;
    if (hwFramesCtx_) {
        av_dict_set(&opts, "preset", "p4", 0);
        av_dict_set(&opts, "tune", "ll", 0);
        av_dict_set(&opts, "rc", "vbr", 0);
        av_dict_set(&opts, "delay", "0", 0);
        av_dict_set(&opts, "b_ref_mode", "disabled", 0);
    } else {
        av_dict_set(&opts, "preset", "ultrafast", 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
    }

    int ret = avcodec_open2(encCtx, codec, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: avcodec_open2 failed: {}", errBuf);
        return Result<void>::error(PlayerError::DecodeError);
    }

    frameIndex_ = 0;
    spdlog::info("HWVideoEncoder: initialized with encoder '{}' ({}) {}x{}@{}fps bitrate={}",
                 config_.encoderInfo.name, config_.encoderInfo.displayName,
                 config_.width, config_.height, config_.fps, config_.bitrate);
    return Result<void>::success();
}

Result<void> HWVideoEncoder::init(const HWEncodeConfig& config) {
    config_ = config;

    Result<void> hwResult = initHardwareContext();
    if (hwResult.hasError() && config_.encoderInfo.hwPixFmt != AV_PIX_FMT_NONE) {
        spdlog::warn("HWVideoEncoder: hardware context init failed, falling back to software encoder");
        config_.encoderInfo.hwPixFmt = AV_PIX_FMT_NONE;
        config_.encoderInfo.name = "libx264";
        config_.encoderInfo.displayName = "x264 Software";
    }

    return initEncoder();
}

Result<std::vector<EncodedPacket>> HWVideoEncoder::encode(const AVFrame* cpuFrame) {
    if (!encCtx_) {
        return Result<std::vector<EncodedPacket>>::error(PlayerError::InvalidState);
    }

    AVFrame* frameToEncode = nullptr;
    AVFrame* tempHwFrame = nullptr;

    if (cpuFrame && hwFramesCtx_) {
        tempHwFrame = av_frame_alloc();
        if (!tempHwFrame) {
            spdlog::error("HWVideoEncoder: av_frame_alloc failed for temp frame");
            return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
        }

        int ret = av_hwframe_get_buffer(hwFramesCtx_, tempHwFrame, 0);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("HWVideoEncoder: av_hwframe_get_buffer failed: {}", errBuf);
            av_frame_free(&tempHwFrame);
            return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
        }

        ret = av_hwframe_transfer_data(tempHwFrame, cpuFrame, 0);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("HWVideoEncoder: av_hwframe_transfer_data failed: {}", errBuf);
            av_frame_free(&tempHwFrame);
            return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
        }

        tempHwFrame->pts = cpuFrame->pts;
        frameToEncode = tempHwFrame;
    } else {
        frameToEncode = const_cast<AVFrame*>(cpuFrame);
    }

    int ret = avcodec_send_frame(encCtx_, frameToEncode);
    if (tempHwFrame) {
        av_frame_free(&tempHwFrame);
    }

    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: avcodec_send_frame failed: {}", errBuf);
        return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
    }

    std::vector<EncodedPacket> packets;
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        spdlog::error("HWVideoEncoder: av_packet_alloc failed");
        return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
    }

    while (true) {
        ret = avcodec_receive_packet(encCtx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::warn("HWVideoEncoder: avcodec_receive_packet failed: {}", errBuf);
            break;
        }

        EncodedPacket out;
        if (pkt->size > 0) {
            out.data.assign(pkt->data, pkt->data + pkt->size);
        }
        // Preserve pts/dts in encoder time_base units; the muxer thread
        // rescales them to output stream time_base via av_packet_rescale_ts.
        out.pts = pkt->pts;
        out.dts = pkt->dts;
        out.duration = pkt->duration;
        out.isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        packets.push_back(std::move(out));

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return Result<std::vector<EncodedPacket>>::success(std::move(packets));
}

Result<std::vector<EncodedPacket>> HWVideoEncoder::flush() {
    return encode(nullptr);
}

const AVCodecContext* HWVideoEncoder::context() const {
    return encCtx_;
}

AVPixelFormat HWVideoEncoder::hwPixFmt() const {
    return config_.encoderInfo.hwPixFmt;
}

bool HWVideoEncoder::isHardware() const {
    return config_.encoderInfo.hwPixFmt != AV_PIX_FMT_NONE;
}

}
