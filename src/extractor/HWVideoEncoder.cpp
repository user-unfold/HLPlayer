#include "HWVideoEncoder.h"

#include <spdlog/spdlog.h>

#include <algorithm>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

// ============================================================================
// Encoder name mapping table: (backend, codec) -> FFmpeg encoder name
// ============================================================================

struct EncoderMapping {
    EncoderBackend backend;
    Codec codec;
    const char* name;
};

static constexpr EncoderMapping kEncoderMappings[] = {
    {EncoderBackend::NVENC, Codec::H264,  "h264_nvenc"},
    {EncoderBackend::NVENC, Codec::HEVC,  "hevc_nvenc"},
    {EncoderBackend::NVENC, Codec::AV1,   "av1_nvenc"},
    {EncoderBackend::QSV,   Codec::H264,  "h264_qsv"},
    {EncoderBackend::QSV,   Codec::HEVC,  "hevc_qsv"},
    {EncoderBackend::QSV,   Codec::AV1,   "av1_qsv"},
    {EncoderBackend::AMF,   Codec::H264,  "h264_amf"},
    {EncoderBackend::AMF,   Codec::HEVC,  "hevc_amf"},
};

static constexpr int kEncoderMappingCount =
    sizeof(kEncoderMappings) / sizeof(kEncoderMappings[0]);

// ============================================================================
// Constructor / Destructor
// ============================================================================

HWVideoEncoder::HWVideoEncoder() {
    spdlog::info("HWVideoEncoder created");
}

HWVideoEncoder::~HWVideoEncoder() {
    close();
    spdlog::info("HWVideoEncoder destroyed");
}

// ============================================================================
// IVideoEncoder interface
// ============================================================================

Result<void> HWVideoEncoder::open(const EncoderConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (isOpen_.load()) {
        spdlog::error("HWVideoEncoder: already open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (config.width == 0 || config.height == 0) {
        spdlog::error("HWVideoEncoder: invalid dimensions {}x{}", config.width, config.height);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    config_ = config;
    frameIndex_ = 0;

    auto backendResult = selectBackend(config.codec, config.hwAccel);
    if (backendResult.hasError()) {
        return Result<void>::error(backendResult.error());
    }

    activeBackend_ = backendResult.value();
    auto initResult = initEncoder(activeBackend_, config);
    if (initResult.hasError()) {
        return initResult;
    }

    isOpen_.store(true);
    spdlog::info("HWVideoEncoder: opened {}x{} codec={} backend={} bitrate={} crf={}",
                 config.width, config.height,
                 static_cast<int>(config.codec),
                 static_cast<int>(activeBackend_),
                 config.bitrate, config.crf);
    return Result<void>::success();
}

Result<EncodedPacket> HWVideoEncoder::encode(const GpuFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isOpen_.load()) {
        return Result<EncodedPacket>::error(PlayerError::InvalidState);
    }

    if (frame.deviceLost) {
        spdlog::warn("HWVideoEncoder: skipping frame with deviceLost=true");
        return Result<EncodedPacket>::error(PlayerError::DeviceLost);
    }

    AVFramePtr avFrame;
    if (frame.cpuData.empty() && frame.handle.nativeHandle != nullptr) {
        auto gpuResult = frameFromGpuTexture(frame);
        if (gpuResult.hasError()) {
            spdlog::warn("HWVideoEncoder: GPU texture path failed, trying CPU fallback");
            auto cpuResult = frameFromCpuData(frame);
            if (cpuResult.hasError()) {
                return Result<EncodedPacket>::error(cpuResult.error());
            }
            avFrame = std::move(cpuResult.value());
        } else {
            avFrame = std::move(gpuResult.value());
        }
    } else if (!frame.cpuData.empty()) {
        auto cpuResult = frameFromCpuData(frame);
        if (cpuResult.hasError()) {
            return Result<EncodedPacket>::error(cpuResult.error());
        }
        avFrame = std::move(cpuResult.value());
    } else {
        spdlog::error("HWVideoEncoder: frame has no GPU handle and no CPU data");
        return Result<EncodedPacket>::error(PlayerError::UnsupportedFormat);
    }

    avFrame->pts = frameIndex_;
    avFrame->width = static_cast<int>(frame.width);
    avFrame->height = static_cast<int>(frame.height);
    frameIndex_++;

    int ret = avcodec_send_frame(codecCtx_.get(), avFrame.get());
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: avcodec_send_frame failed: {}", errBuf);
        return Result<EncodedPacket>::error(PlayerError::DecodeError);
    }

    AVPacketPtr pkt = makeAVPacket();
    ret = avcodec_receive_packet(codecCtx_.get(), pkt.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        // Frame buffered, no packet available yet — return empty success
        return Result<EncodedPacket>::success(EncodedPacket{});
    }
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: avcodec_receive_packet failed: {}", errBuf);
        return Result<EncodedPacket>::error(PlayerError::DecodeError);
    }

    return Result<EncodedPacket>::success(convertPacket(pkt.get(), timeBase_));
}

Result<std::vector<EncodedPacket>> HWVideoEncoder::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isOpen_.load()) {
        return Result<std::vector<EncodedPacket>>::error(PlayerError::InvalidState);
    }

    int ret = avcodec_send_frame(codecCtx_.get(), nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: flush send failed: {}", errBuf);
        return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
    }

    std::vector<EncodedPacket> packets;
    while (true) {
        AVPacketPtr pkt = makeAVPacket();
        ret = avcodec_receive_packet(codecCtx_.get(), pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::warn("HWVideoEncoder: flush receive error: {}", errBuf);
            break;
        }
        packets.push_back(convertPacket(pkt.get(), timeBase_));
    }

    return Result<std::vector<EncodedPacket>>::success(std::move(packets));
}

void HWVideoEncoder::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isOpen_.exchange(false)) {
        return;
    }

    hwFramesCtx_.reset();
    hwDeviceCtx_.reset();
    codecCtx_.reset();
    frameIndex_ = 0;
    timeBase_ = 0.0;

    spdlog::info("HWVideoEncoder: closed");
}

EncoderConfig HWVideoEncoder::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool HWVideoEncoder::isOpen() const {
    return isOpen_.load();
}

// ============================================================================
// Static utilities
// ============================================================================

std::vector<EncoderBackendInfo> HWVideoEncoder::detectAvailableBackends() {
    std::vector<EncoderBackendInfo> result;

    for (int i = 0; i < kEncoderMappingCount; ++i) {
        const auto& mapping = kEncoderMappings[i];
        const AVCodec* encoder = avcodec_find_encoder_by_name(mapping.name);

        // Avoid duplicate backend entries
        bool backendExists = false;
        for (const auto& existing : result) {
            if (existing.backend == mapping.backend) {
                backendExists = true;
                break;
            }
        }
        if (backendExists) {
            continue;
        }

        result.push_back({
            mapping.backend,
            mapping.name,
            encoder != nullptr
        });
    }

    return result;
}

std::string HWVideoEncoder::getEncoderName(EncoderBackend backend, Codec codec) {
    for (int i = 0; i < kEncoderMappingCount; ++i) {
        const auto& mapping = kEncoderMappings[i];
        if (mapping.backend == backend && mapping.codec == codec) {
            return mapping.name;
        }
    }
    return {};
}

AVHWDeviceType HWVideoEncoder::hwAccelModeToAVType(HwAccelMode mode) {
    switch (mode) {
        case HwAccelMode::CUDA:   return AV_HWDEVICE_TYPE_CUDA;
        case HwAccelMode::Vulkan: return AV_HWDEVICE_TYPE_VULKAN;
        case HwAccelMode::D3D11:  return AV_HWDEVICE_TYPE_D3D11VA;
        default:                  return AV_HWDEVICE_TYPE_NONE;
    }
}

AVPixelFormat HWVideoEncoder::pixelFormatToAV(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::NV12:   return AV_PIX_FMT_NV12;
        case PixelFormat::P010:   return AV_PIX_FMT_P010;
        case PixelFormat::RGBA8:  return AV_PIX_FMT_RGBA;
        case PixelFormat::RGBA16F:return AV_PIX_FMT_RGBA64;
        default:                  return AV_PIX_FMT_NONE;
    }
}

// ============================================================================
// Private helpers
// ============================================================================

Result<EncoderBackend> HWVideoEncoder::selectBackend(Codec codec, HwAccelMode mode) {
    if (mode == HwAccelMode::None) {
        return Result<EncoderBackend>::success(EncoderBackend::Software);
    }

    // Priority order for Auto: NVENC > AMF > QSV (Windows-centric)
    std::vector<EncoderBackend> priorityOrder;
    if (mode == HwAccelMode::Auto) {
        priorityOrder = {EncoderBackend::NVENC, EncoderBackend::AMF, EncoderBackend::QSV};
    } else if (mode == HwAccelMode::CUDA) {
        priorityOrder = {EncoderBackend::NVENC};
    } else if (mode == HwAccelMode::D3D11) {
        priorityOrder = {EncoderBackend::AMF, EncoderBackend::NVENC};
    } else if (mode == HwAccelMode::Vulkan) {
        // Vulkan hw encoding not widely supported, try AMF/NVENC
        priorityOrder = {EncoderBackend::AMF, EncoderBackend::NVENC};
    }

    for (auto backend : priorityOrder) {
        std::string name = getEncoderName(backend, codec);
        if (name.empty()) {
            continue;
        }
        const AVCodec* encoder = avcodec_find_encoder_by_name(name.c_str());
        if (encoder) {
            spdlog::info("HWVideoEncoder: selected backend {} ({})",
                         static_cast<int>(backend), name);
            return Result<EncoderBackend>::success(backend);
        }
    }

    spdlog::warn("HWVideoEncoder: no hardware encoder found for codec={}, hwAccel={}. "
                 "Falling back to software.",
                 static_cast<int>(codec), static_cast<int>(mode));
    return Result<EncoderBackend>::success(EncoderBackend::Software);
}

Result<void> HWVideoEncoder::initEncoder(EncoderBackend backend, const EncoderConfig& config) {
    std::string encoderName;
    if (backend != EncoderBackend::Software) {
        encoderName = getEncoderName(backend, config.codec);
        if (encoderName.empty()) {
            spdlog::error("HWVideoEncoder: no encoder name for backend={} codec={}",
                          static_cast<int>(backend), static_cast<int>(config.codec));
            return Result<void>::error(PlayerError::UnsupportedFormat);
        }
    }

    const AVCodec* codec = nullptr;
    if (!encoderName.empty()) {
        codec = avcodec_find_encoder_by_name(encoderName.c_str());
    } else {
        // Software fallback
        AVCodecID avCodecId = AV_CODEC_ID_NONE;
        switch (config.codec) {
            case Codec::H264: avCodecId = AV_CODEC_ID_H264; break;
            case Codec::HEVC: avCodecId = AV_CODEC_ID_HEVC; break;
            case Codec::AV1:  avCodecId = AV_CODEC_ID_AV1;  break;
            default:
                return Result<void>::error(PlayerError::UnsupportedFormat);
        }
        codec = avcodec_find_encoder(avCodecId);
    }

    if (!codec) {
        spdlog::error("HWVideoEncoder: encoder not found: {}", encoderName.empty() ? "software" : encoderName);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    codecCtx_ = AVCodecContextPtr(avcodec_alloc_context3(codec));
    if (!codecCtx_) {
        spdlog::error("HWVideoEncoder: failed to allocate codec context");
        return Result<void>::error(PlayerError::DecodeError);
    }

    codecCtx_->width = static_cast<int>(config.width);
    codecCtx_->height = static_cast<int>(config.height);
    codecCtx_->time_base = AVRational{1, static_cast<int>(config.frameRate)};
    codecCtx_->framerate = AVRational{static_cast<int>(config.frameRate), 1};
    timeBase_ = 1.0 / config.frameRate;

    if (config.bitrate > 0) {
        codecCtx_->bit_rate = static_cast<int64_t>(config.bitrate);
        codecCtx_->rc_max_rate = static_cast<int64_t>(config.bitrate);
    }

    AVPixelFormat inputPixFmt = pixelFormatToAV(config.inputFormat);
    if (inputPixFmt == AV_PIX_FMT_NONE) {
        inputPixFmt = AV_PIX_FMT_NV12;
    }

    if (backend != EncoderBackend::Software) {
        AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
        switch (backend) {
            case EncoderBackend::NVENC: hwType = AV_HWDEVICE_TYPE_CUDA;    break;
            case EncoderBackend::QSV:   hwType = AV_HWDEVICE_TYPE_QSV;     break;
            case EncoderBackend::AMF:   hwType = AV_HWDEVICE_TYPE_D3D11VA; break;
            default: break;
        }

        if (hwType != AV_HWDEVICE_TYPE_NONE) {
            hwDeviceCtx_ = createHWDeviceContext(hwType);
            if (!hwDeviceCtx_) {
                spdlog::warn("HWVideoEncoder: failed to create hw device context for type={}", 
                             static_cast<int>(hwType));
                // Continue without hw device — some encoders work without explicit hw ctx
            } else {
                codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_.get());

                auto hwFramesResult = setupHWFramesContext(
                    hwType, config.width, config.height, inputPixFmt);
                if (hwFramesResult.hasError()) {
                    spdlog::warn("HWVideoEncoder: hw frames context setup failed, encoding to CPU format");
                    codecCtx_->pix_fmt = inputPixFmt;
                } else {
                    codecCtx_->hw_frames_ctx = av_buffer_ref(hwFramesCtx_.get());
                    // For hw encoding, the pixel format should be the hw format
                    codecCtx_->pix_fmt = static_cast<AVPixelFormat>(
                        reinterpret_cast<AVHWFramesContext*>(
                            hwFramesCtx_->data)->format);
                }
            }
        }
    } else {
        codecCtx_->pix_fmt = inputPixFmt;
    }

    // If hw_frames_ctx was not set, ensure pix_fmt is set
    if (!codecCtx_->hw_frames_ctx && codecCtx_->pix_fmt == AV_PIX_FMT_NONE) {
        codecCtx_->pix_fmt = inputPixFmt;
    }

    // Preset
    if (!config.preset.empty()) {
        av_opt_set(codecCtx_->priv_data, "preset", config.preset.c_str(), 0);
    }

    // CRF / quality
    if (config.bitrate == 0 && config.crf > 0) {
        av_opt_set_int(codecCtx_->priv_data, "crf", config.crf, 0);
    }

    // NVENC-specific tuning
    if (backend == EncoderBackend::NVENC) {
        av_opt_set_int(codecCtx_->priv_data, "rc", 1, 0); // CBR mode if bitrate set
        if (config.bitrate > 0) {
            av_opt_set_int(codecCtx_->priv_data, "cq", 0, 0);
        }
    }

    // AMF-specific
    if (backend == EncoderBackend::AMF) {
        if (config.bitrate > 0) {
            av_opt_set(codecCtx_->priv_data, "rc", "cbr", 0);
        } else {
            av_opt_set(codecCtx_->priv_data, "rc", "cqp", 0);
        }
    }

    // QSV-specific
    if (backend == EncoderBackend::QSV) {
        av_opt_set_int(codecCtx_->priv_data, "async_depth", 1, 0);
    }

    // Threading — hw encoders typically don't support multi-thread
    if (backend != EncoderBackend::Software) {
        codecCtx_->thread_count = 1;
    } else {
        codecCtx_->thread_count = 0; // auto
        codecCtx_->thread_type = FF_THREAD_SLICE;
    }

    codecCtx_->gop_size = static_cast<int>(config.frameRate * 2); // keyframe every ~2 seconds

    AVDictionaryPtr opts = createEmptyAVDictionary();

    // Open codec
    int ret = avcodec_open2(codecCtx_.get(), codec, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: avcodec_open2 failed: {}", errBuf);

        codecCtx_.reset();
        hwFramesCtx_.reset();
        hwDeviceCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    // Re-read time_base after open (encoder may adjust it)
    timeBase_ = av_q2d(codecCtx_->time_base);

    return Result<void>::success();
}

Result<void> HWVideoEncoder::setupHWFramesContext(AVHWDeviceType deviceType,
                                                    uint32_t width, uint32_t height,
                                                    AVPixelFormat swFormat) {
    if (!hwDeviceCtx_) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    AVBufferRef* framesRef = av_hwframe_ctx_alloc(hwDeviceCtx_.get());
    if (!framesRef) {
        spdlog::error("HWVideoEncoder: failed to allocate hw frames context");
        return Result<void>::error(PlayerError::DecodeError);
    }

    auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(framesRef->data);

    switch (deviceType) {
        case AV_HWDEVICE_TYPE_CUDA:
            framesCtx->format = AV_PIX_FMT_CUDA;
            break;
        case AV_HWDEVICE_TYPE_D3D11VA:
            framesCtx->format = AV_PIX_FMT_D3D11;
            break;
        case AV_HWDEVICE_TYPE_QSV:
            framesCtx->format = AV_PIX_FMT_QSV;
            break;
        default:
            framesCtx->format = AV_PIX_FMT_NONE;
            break;
    }

    framesCtx->sw_format = swFormat;
    framesCtx->width = static_cast<int>(width);
    framesCtx->height = static_cast<int>(height);
    framesCtx->initial_pool_size = 4; // encoder internal pool

    int ret = av_hwframe_ctx_init(framesRef);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: hw frames context init failed: {}", errBuf);
        av_buffer_unref(&framesRef);
        return Result<void>::error(PlayerError::DecodeError);
    }

    hwFramesCtx_ = AVBufferRefPtr(framesRef);
    return Result<void>::success();
}

EncodedPacket HWVideoEncoder::convertPacket(AVPacket* pkt, double timeBase) {
    EncodedPacket out;
    if (pkt->size > 0) {
        out.data.assign(pkt->data, pkt->data + pkt->size);
    }
    out.pts = pkt->pts * timeBase;
    out.dts = pkt->dts * timeBase;
    out.duration = pkt->duration * timeBase;
    out.isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
    out.streamIndex = static_cast<uint32_t>(pkt->stream_index);
    return out;
}

Result<AVFramePtr> HWVideoEncoder::frameFromGpuTexture(const GpuFrame& frame) {
    if (!hwFramesCtx_) {
        return Result<AVFramePtr>::error(PlayerError::InvalidState);
    }

    AVFramePtr avFrame = makeAVFrame();
    if (!avFrame) {
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    int ret = av_hwframe_get_buffer(hwFramesCtx_.get(), avFrame.get(), 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: failed to get hw frame buffer: {}", errBuf);
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    // Map the external GPU texture into the AVFrame
    // The GpuFrame.handle.nativeHandle carries the texture/compute handle.
    // For D3D11: nativeHandle is ID3D11Texture2D*
    // For CUDA:  nativeHandle is CUarray or cudaGraphicsResource*
    // We store it in data[3] as an opaque reference and let the hw codec handle it.
    avFrame->data[3] = reinterpret_cast<uint8_t*>(frame.handle.nativeHandle);

    avFrame->width = static_cast<int>(frame.width);
    avFrame->height = static_cast<int>(frame.height);

    return Result<AVFramePtr>::success(std::move(avFrame));
}

Result<AVFramePtr> HWVideoEncoder::frameFromCpuData(const GpuFrame& frame) {
    if (frame.cpuData.empty()) {
        return Result<AVFramePtr>::error(PlayerError::UnsupportedFormat);
    }

    int w = static_cast<int>(frame.width);
    int h = static_cast<int>(frame.height);

    size_t rgbaSize = static_cast<size_t>(w) * h * 4;
    bool needsConversion = (frame.cpuData.size() == rgbaSize);

    AVPixelFormat targetFmt = needsConversion ? codecCtx_->pix_fmt : pixelFormatToAV(frame.format);
    if (targetFmt == AV_PIX_FMT_NONE) {
        targetFmt = codecCtx_->pix_fmt;
    }

    AVFramePtr avFrame = makeAVFrame();
    if (!avFrame) {
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    avFrame->format = targetFmt;
    avFrame->width = w;
    avFrame->height = h;

    int ret = av_frame_get_buffer(avFrame.get(), 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: av_frame_get_buffer failed: {}", errBuf);
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    ret = av_frame_make_writable(avFrame.get());
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("HWVideoEncoder: av_frame_make_writable failed: {}", errBuf);
        return Result<AVFramePtr>::error(PlayerError::DecodeError);
    }

    if (needsConversion) {
        int srcLinesize = w * 4;
        SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_RGBA,
                                          w, h, targetFmt,
                                          SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) {
            spdlog::error("HWVideoEncoder: sws_getContext failed for RGBA->target conversion");
            return Result<AVFramePtr>::error(PlayerError::DecodeError);
        }
        const uint8_t* srcData[1] = { frame.cpuData.data() };
        int srcLinesizes[1] = { srcLinesize };
        sws_scale(sws, srcData, srcLinesizes, 0, h, avFrame->data, avFrame->linesize);
        sws_freeContext(sws);
    } else {
        size_t totalSize = 0;
        for (int i = 0; i < AV_NUM_DATA_POINTERS && avFrame->linesize[i] != 0; ++i) {
            size_t planeSize = static_cast<size_t>(avFrame->linesize[i]) * h;
            if (i == 1 && (targetFmt == AV_PIX_FMT_NV12 || targetFmt == AV_PIX_FMT_P010)) {
                planeSize = static_cast<size_t>(avFrame->linesize[i]) * (h / 2);
            }
            size_t copySize = std::min(planeSize, frame.cpuData.size() - totalSize);
            if (copySize > 0) {
                memcpy(avFrame->data[i], frame.cpuData.data() + totalSize, copySize);
                totalSize += copySize;
            }
        }
    }

    return Result<AVFramePtr>::success(std::move(avFrame));
}

} // namespace extractor
} // namespace hlplayer
