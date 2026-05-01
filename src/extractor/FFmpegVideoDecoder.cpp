#include "FFmpegVideoDecoder.h"

#include <spdlog/spdlog.h>
#include <FFmpegRAII.h>
#include "HWDevicePool.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

FFmpegVideoDecoder::FFmpegVideoDecoder(EventBus* eventBus)
    : eventBus_(eventBus) {
    spdlog::info("FFmpegVideoDecoder created");
}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
    close();
    spdlog::info("FFmpegVideoDecoder destroyed");
}

Result<void> FFmpegVideoDecoder::open(const DecoderConfig& config) {
    if (isOpen_.load()) {
        spdlog::error("Decoder already open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_ = config;
    if (config_.backend == DecodeBackend::CPU) {
        ffmpegConfig_.hwBackend = HWAccelBackend::None;
    }
    width_ = static_cast<int>(config.width);
    height_ = static_cast<int>(config.height);

    AVCodecID avCodecId = AV_CODEC_ID_NONE;
    switch (config.codec) {
        case Codec::H264:
            avCodecId = AV_CODEC_ID_H264;
            break;
        case Codec::HEVC:
            avCodecId = AV_CODEC_ID_HEVC;
            break;
        case Codec::AV1:
            avCodecId = AV_CODEC_ID_AV1;
            break;
        case Codec::VP8:
            avCodecId = AV_CODEC_ID_VP8;
            break;
        case Codec::VP9:
            avCodecId = AV_CODEC_ID_VP9;
            break;
        case Codec::MPEG2:
            avCodecId = AV_CODEC_ID_MPEG2VIDEO;
            break;
        case Codec::MPEG4:
            avCodecId = AV_CODEC_ID_MPEG4;
            break;
        case Codec::VC1:
            avCodecId = AV_CODEC_ID_VC1;
            break;
        default:
            spdlog::error("Unsupported codec: {}", static_cast<int>(config.codec));
            return Result<void>::error(PlayerError::DecodeError);
    }

    codec_ = avcodec_find_decoder(avCodecId);
    if (!codec_) {
        spdlog::error("Failed to find decoder for codec ID: {}", static_cast<int>(avCodecId));
        return Result<void>::error(PlayerError::DecodeError);
    }

    auto hwSetupResult = setupHardwareContext();
    if (hwSetupResult.hasError() && !ffmpegConfig_.allowCpuFallback) {
        return Result<void>::error(hwSetupResult.error());
    }

    auto createResult = createCodecContext(codec_);
    if (createResult.hasError()) {
        return createResult;
    }

    if (ffmpegConfig_.enableLowDelay) {
        codecCtx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        codecCtx_->flags2 |= AV_CODEC_FLAG2_FAST;
    }

    if (activeBackend_.load() == DecodeBackend::D3D11) {
        // D3D11VA requires thread_count = 1 (doesn't support multi-threaded decoding)
        codecCtx_->thread_count = 1;
        codecCtx_->thread_type = FF_THREAD_FRAME;
    } else {
        codecCtx_->thread_count = ffmpegConfig_.threadCount > 0 ? ffmpegConfig_.threadCount : 0;
        codecCtx_->thread_type = FF_THREAD_SLICE;
    }

    if (config_.backend == DecodeBackend::D3D11 || config_.backend == DecodeBackend::CUDA) {
        codecCtx_->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pixFmts) -> enum AVPixelFormat {
            for (int i = 0; pixFmts[i] != AV_PIX_FMT_NONE; i++) {
                if (pixFmts[i] == AV_PIX_FMT_D3D11 || pixFmts[i] == AV_PIX_FMT_CUDA) {
                    // Dynamically create hw_frames_ctx for D3D11VA
                    if (pixFmts[i] == AV_PIX_FMT_D3D11 && ctx->hw_device_ctx) {
                        AVBufferRef* framesRef = av_hwframe_ctx_alloc(ctx->hw_device_ctx);
                        if (!framesRef) {
                            spdlog::error("get_format: failed to allocate hw_frames_ctx");
                            return AV_PIX_FMT_NONE;
                        }

                        auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(framesRef->data);
                        framesCtx->format = AV_PIX_FMT_D3D11;
                        framesCtx->width = ctx->coded_width;
                        framesCtx->height = ctx->coded_height;

                        // Codec-specific pool sizing
                        int poolSize = 1;
                        auto codecId = ctx->codec_id;
                        if (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC) {
                            poolSize += 16;  // DPB needs up to 16 reference frames
                        } else if (codecId == AV_CODEC_ID_VP9 || codecId == AV_CODEC_ID_AV1) {
                            poolSize += 8;   // VP9/AV1 need up to 8 reference frames
                        } else if (codecId == AV_CODEC_ID_MPEG2VIDEO || codecId == AV_CODEC_ID_MPEG4 ||
                                   codecId == AV_CODEC_ID_VC1) {
                            poolSize += 3;   // MPEG2/MPEG4/VC1 need 3 reference frames
                        } else {
                            poolSize += 2;   // Default for other codecs (e.g., VP8)
                        }
                        framesCtx->initial_pool_size = poolSize;

                        // Determine sw_format from sw_pix_fmt
                        switch (ctx->sw_pix_fmt) {
                            case AV_PIX_FMT_YUV420P10LE:
                                framesCtx->sw_format = AV_PIX_FMT_P010;
                                break;
                            case AV_PIX_FMT_YUV422P:
                            case AV_PIX_FMT_YUV422P10LE:
                                framesCtx->sw_format = AV_PIX_FMT_P210;
                                break;
                            case AV_PIX_FMT_YUV420P:
                            case AV_PIX_FMT_YUVJ420P:
                            default:
                                framesCtx->sw_format = AV_PIX_FMT_NV12;
                                break;
                        }

                        // Set D3D11-specific BindFlags
                        auto* d3d11Ctx = reinterpret_cast<AVD3D11VAFramesContext*>(framesCtx->hwctx);
                        d3d11Ctx->BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;

                        int ret = av_hwframe_ctx_init(framesRef);
                        if (ret < 0) {
                            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                            av_strerror(ret, errBuf, sizeof(errBuf));
                            spdlog::error("get_format: hw_frames_ctx init failed: {}", errBuf);
                            av_buffer_unref(&framesRef);
                            return AV_PIX_FMT_NONE;
                        }

                        // Transfer ownership to codecCtx
                        ctx->hw_frames_ctx = framesRef;
                        spdlog::info("get_format: created hw_frames_ctx (pool={}, sw_fmt={}, {}x{})",
                                     poolSize, av_get_pix_fmt_name(framesCtx->sw_format),
                                     framesCtx->width, framesCtx->height);
                    }

                    return pixFmts[i];
                }
            }
            return AV_PIX_FMT_NONE;  // No HW format found
        };
    }

    int ret = avcodec_open2(codecCtx_.get(), codec_, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to open codec: {}", errBuf);

        if (hwDeviceCtx_ && ffmpegConfig_.allowCpuFallback) {
            spdlog::warn("Falling back to CPU decoding");
            hwDeviceCtx_.reset();
            hwFramesCtx_.reset();
            codecCtx_->hw_device_ctx = nullptr;
            codecCtx_->hw_frames_ctx = nullptr;

            ret = avcodec_open2(codecCtx_.get(), codec_, nullptr);
            if (ret < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                spdlog::error("CPU fallback also failed: {}", errBuf);
                return Result<void>::error(PlayerError::DecodeError);
            }
            activeBackend_.store(DecodeBackend::CPU);
        } else {
            return Result<void>::error(PlayerError::DecodeError);
        }
    }

    isOpen_.store(true);
    spdlog::info("Decoder opened successfully: backend={} codec={}",
                 static_cast<int>(activeBackend_.load()), static_cast<int>(config.codec));

    return Result<void>::success();
}

Result<void> FFmpegVideoDecoder::setupHardwareContext() {
    AVHWDeviceType hwDeviceType = AV_HWDEVICE_TYPE_NONE;

#ifdef _WIN32
    if (ffmpegConfig_.hwBackend == HWAccelBackend::Auto) {
        hwDeviceType = AV_HWDEVICE_TYPE_D3D11VA;
        spdlog::info("Auto-selecting D3D11VA for Windows");
    } else if (ffmpegConfig_.hwBackend == HWAccelBackend::D3D11VA) {
        hwDeviceType = AV_HWDEVICE_TYPE_D3D11VA;
    } else if (ffmpegConfig_.hwBackend == HWAccelBackend::DXVA2) {
        hwDeviceType = AV_HWDEVICE_TYPE_DXVA2;
    }
#elif defined(__APPLE__)
    if (ffmpegConfig_.hwBackend == HWAccelBackend::Auto ||
        ffmpegConfig_.hwBackend == HWAccelBackend::VideoToolbox) {
        hwDeviceType = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
        spdlog::info("Auto-selecting VideoToolbox for macOS");
    }
#elif defined(__linux__)
    if (ffmpegConfig_.hwBackend == HWAccelBackend::Auto ||
        ffmpegConfig_.hwBackend == HWAccelBackend::VAAPI) {
        hwDeviceType = AV_HWDEVICE_TYPE_VAAPI;
    }
#endif

    if (hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
        spdlog::warn("No hardware acceleration available or disabled");
        return Result<void>::success();
    }

    AVBufferRef* poolRef = HWDevicePool::instance().getDeviceRef(hwDeviceType);
    if (poolRef) {
        hwDeviceCtx_ = AVBufferRefPtr(av_buffer_ref(poolRef));
    }

    if (!hwDeviceCtx_) {
        spdlog::warn("Failed to create hardware device context");
        return Result<void>::error(PlayerError::DecodeError);
    }

    if (hwDeviceType == AV_HWDEVICE_TYPE_D3D11VA) {
        activeBackend_.store(DecodeBackend::D3D11);
        spdlog::info("Hardware acceleration: D3D11VA");
    } else if (hwDeviceType == AV_HWDEVICE_TYPE_DXVA2) {
        activeBackend_.store(DecodeBackend::D3D11);
        spdlog::info("Hardware acceleration: DXVA2");
    } else if (hwDeviceType == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
        activeBackend_.store(DecodeBackend::Auto);
        spdlog::info("Hardware acceleration: VideoToolbox");
    } else if (hwDeviceType == AV_HWDEVICE_TYPE_VAAPI) {
        activeBackend_.store(DecodeBackend::Auto);
        spdlog::info("Hardware acceleration: VAAPI");
    } else if (hwDeviceType == AV_HWDEVICE_TYPE_CUDA) {
        activeBackend_.store(DecodeBackend::CUDA);
        spdlog::info("Hardware acceleration: CUDA");
    }

    return Result<void>::success();
}

Result<void> FFmpegVideoDecoder::createCodecContext(const AVCodec* codec) {
    codecCtx_.reset(avcodec_alloc_context3(codec));
    if (!codecCtx_) {
        spdlog::error("Failed to allocate codec context");
        return Result<void>::error(PlayerError::DecodeError);
    }

    if (config_.width > 0 && config_.height > 0) {
        codecCtx_->width = static_cast<int>(config_.width);
        codecCtx_->height = static_cast<int>(config_.height);
    }

    if (!config_.extradata.empty()) {
        codecCtx_->extradata_size = static_cast<int>(config_.extradata.size());
        codecCtx_->extradata = (uint8_t*)av_mallocz(config_.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!codecCtx_->extradata) {
            spdlog::error("Failed to allocate extradata");
            return Result<void>::error(PlayerError::DecodeError);
        }
        memcpy(codecCtx_->extradata, config_.extradata.data(), config_.extradata.size());
        spdlog::info("Decoder extradata set: {} bytes", config_.extradata.size());
    }

    if (hwDeviceCtx_) {
        codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_.get());
        if (!codecCtx_->hw_device_ctx) {
            spdlog::error("Failed to set hardware device context");
            return Result<void>::error(PlayerError::DecodeError);
        }
    }

    swPixelFormat_ = AV_PIX_FMT_YUV420P;

    return Result<void>::success();
}

Result<GpuFrame> FFmpegVideoDecoder::decode(const uint8_t* data, size_t size, double pts) {
    if (!isOpen_.load() || !codecCtx_) {
        return Result<GpuFrame>::error(PlayerError::InvalidState);
    }

    AVPacketPtr packet = makeAVPacket();
    if (!packet) {
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    packet->data = const_cast<uint8_t*>(data);
    packet->size = static_cast<int>(size);
    packet->pts = static_cast<int64_t>(pts * AV_TIME_BASE);
    packet->dts = AV_NOPTS_VALUE;

    int ret = avcodec_send_packet(codecCtx_.get(), packet.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return Result<GpuFrame>::error(PlayerError::NeedMoreData);
    }
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to send packet: {}", errBuf);
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    AVFramePtr frame = makeAVFrame();
    if (!frame) {
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    ret = avcodec_receive_frame(codecCtx_.get(), frame.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return Result<GpuFrame>::error(PlayerError::NeedMoreData);
    }
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to receive frame: {}", errBuf);
        consecutiveDecodeFailures_++;
        if (consecutiveDecodeFailures_ >= MAX_CONSECUTIVE_FAILURES && activeBackend_.load() != DecodeBackend::CPU) {
            spdlog::error("FFmpegVideoDecoder: {} consecutive decode failures, triggering runtime fallback",
                          consecutiveDecodeFailures_);
            GpuFrame lostFrame;
            lostFrame.deviceLost = true;
            lostFrame.timestamp = pts;
            return Result<GpuFrame>::success(std::move(lostFrame));
        }
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    consecutiveDecodeFailures_ = 0;  // Reset on success

    GpuFrame gpuFrame = convertToGpuFrame(frame.get());
    gpuFrame.timestamp = pts;

    if (frameCallback_) {
        frameCallback_(gpuFrame);
    }

    return Result<GpuFrame>::success(std::move(gpuFrame));
}

Result<std::vector<GpuFrame>> FFmpegVideoDecoder::flush() {
    if (!isOpen_.load() || !codecCtx_) {
        return Result<std::vector<GpuFrame>>::error(PlayerError::InvalidState);
    }

    std::vector<GpuFrame> frames;

    avcodec_send_packet(codecCtx_.get(), nullptr);

    AVFramePtr frame = makeAVFrame();
    while (true) {
        int ret = avcodec_receive_frame(codecCtx_.get(), frame.get());
        if (ret == AVERROR_EOF || ret < 0) {
            break;
        }

        frames.push_back(convertToGpuFrame(frame.get()));
    }

    avcodec_flush_buffers(codecCtx_.get());

    spdlog::info("Flushed decoder, got {} frames", frames.size());
    return Result<std::vector<GpuFrame>>::success(std::move(frames));
}

void FFmpegVideoDecoder::close() {
    if (!isOpen_.exchange(false)) {
        return;
    }

    consecutiveDecodeFailures_ = 0;

    codecCtx_.reset();
    hwFramesCtx_.reset();
    hwDeviceCtx_.reset();
    cachedSwsCtx_.reset();
    swsSrcW_ = 0;
    swsSrcH_ = 0;
    swsSrcFmt_ = AV_PIX_FMT_NONE;
    codec_ = nullptr;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!frameQueue_.empty()) {
            frameQueue_.pop();
        }
    }

    spdlog::info("Decoder closed");
}

DecodeBackend FFmpegVideoDecoder::getBackend() const {
    return activeBackend_.load();
}

bool FFmpegVideoDecoder::supportsCodec(Codec codec) const {
    switch (codec) {
        case Codec::H264:
        case Codec::HEVC:
        case Codec::AV1:
        case Codec::VP8:
        case Codec::VP9:
        case Codec::MPEG2:
        case Codec::MPEG4:
        case Codec::VC1:
            return true;
        default:
            return false;
    }
}

void FFmpegVideoDecoder::setFrameCallback(GpuFrameCallback callback) {
    frameCallback_ = std::move(callback);
}

Result<void> FFmpegVideoDecoder::setExternalDevice(void* device) {
    if (isOpen_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_.gpuDevice = device;
    return Result<void>::success();
}

GpuFrame FFmpegVideoDecoder::convertToGpuFrame(AVFrame* frame) {
    GpuFrame gpuFrame;

    if (!frame) {
        gpuFrame.deviceLost = true;
        return gpuFrame;
    }

    gpuFrame.width = frame->width;
    gpuFrame.height = frame->height;
    gpuFrame.format = avPixelFormatToGpuFormat(static_cast<AVPixelFormat>(frame->format));
    gpuFrame.colorSpace = getFrameColorSpace(frame);

    double timestamp = 0.0;
    if (frame->pts != AV_NOPTS_VALUE) {
        AVRational timeBase = {1, AV_TIME_BASE};
        timestamp = av_q2d(timeBase) * frame->pts;
    }
    gpuFrame.timestamp = timestamp;

    AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
    AVPixelFormat swSrcFmt = srcFmt;

    if (frame->hw_frames_ctx || frame->format == AV_PIX_FMT_D3D11 ||
        frame->format == AV_PIX_FMT_CUDA || frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        gpuFrame.handle.nativeHandle = frame->data[0];
        gpuFrame.handle.auxiliaryHandle = frame->data[1];
        gpuFrame.handle.apiType = 1;

        AVFramePtr swFrame = makeAVFrame();
        if (swFrame) {
            int ret = av_hwframe_transfer_data(swFrame.get(), frame, 0);
            if (ret >= 0) {
                swSrcFmt = static_cast<AVPixelFormat>(swFrame->format);
                gpuFrame.cpuData.resize(frame->width * frame->height * 4);

                if (!cachedSwsCtx_ || swsSrcW_ != swFrame->width || swsSrcH_ != swFrame->height || swsSrcFmt_ != swSrcFmt) {
                    cachedSwsCtx_.reset(sws_getContext(
                        swFrame->width, swFrame->height, swSrcFmt,
                        frame->width, frame->height, AV_PIX_FMT_RGBA,
                        SWS_BILINEAR, nullptr, nullptr, nullptr));
                    swsSrcW_ = swFrame->width;
                    swsSrcH_ = swFrame->height;
                    swsSrcFmt_ = swSrcFmt;
                }

                if (cachedSwsCtx_) {
                    uint8_t* dstData[4] = {gpuFrame.cpuData.data()};
                    int dstLinesize[4] = {static_cast<int>(frame->width * 4)};
                    sws_scale(cachedSwsCtx_.get(), swFrame->data, swFrame->linesize, 0, swFrame->height, dstData, dstLinesize);
                }
            }
        }
    } else {
        gpuFrame.handle.nativeHandle = nullptr;
        gpuFrame.handle.auxiliaryHandle = nullptr;
        gpuFrame.handle.apiType = 0;
        gpuFrame.format = PixelFormat::RGBA8;

        gpuFrame.cpuData.resize(frame->width * frame->height * 4);

        if (!cachedSwsCtx_ || swsSrcW_ != frame->width || swsSrcH_ != frame->height || swsSrcFmt_ != srcFmt) {
            cachedSwsCtx_.reset(sws_getContext(
                frame->width, frame->height, srcFmt,
                frame->width, frame->height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr));
            swsSrcW_ = frame->width;
            swsSrcH_ = frame->height;
            swsSrcFmt_ = srcFmt;
        }

        if (cachedSwsCtx_) {
            uint8_t* dstData[4] = {gpuFrame.cpuData.data()};
            int dstLinesize[4] = {static_cast<int>(frame->width * 4)};
            sws_scale(cachedSwsCtx_.get(), frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
        } else {
            spdlog::error("convertToGpuFrame CPU: sws_getContext returned null for {} -> RGBA", av_get_pix_fmt_name(srcFmt));
        }
    }

    return gpuFrame;
}

PixelFormat FFmpegVideoDecoder::avPixelFormatToGpuFormat(AVPixelFormat fmt) const {
    switch (fmt) {
        case AV_PIX_FMT_YUV420P:
            return PixelFormat::NV12;
        case AV_PIX_FMT_NV12:
            return PixelFormat::NV12;
        case AV_PIX_FMT_P010:
            return PixelFormat::P010;
        case AV_PIX_FMT_RGBA:
            return PixelFormat::RGBA8;
        case AV_PIX_FMT_BGRA:
            return PixelFormat::RGBA8;
        case AV_PIX_FMT_RGBA64:
            return PixelFormat::RGBA16F;
        case AV_PIX_FMT_D3D11:
        case AV_PIX_FMT_CUDA:
        case AV_PIX_FMT_VIDEOTOOLBOX:
            return PixelFormat::Vulkan;
        default:
            return PixelFormat::Unknown;
    }
}

ColorSpace FFmpegVideoDecoder::getFrameColorSpace(AVFrame* frame) const {
    if (!frame || !frame->colorspace) {
        return ColorSpace::BT709;
    }

    switch (frame->colorspace) {
        case AVCOL_SPC_BT709:
            return ColorSpace::BT709;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
            return ColorSpace::BT601;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            return ColorSpace::BT2020;
        case AVCOL_SPC_RGB:
            return ColorSpace::sRGB;
        default:
            return ColorSpace::BT709;
    }
}

} // namespace extractor
} // namespace hlplayer
