#include "hlplayer/CameraSource.h"

#include <spdlog/spdlog.h>
#include <atomic>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/opt.h>
}

namespace hlplayer {

bool CameraSource::deviceRegistered_ = false;

static int cameraInterruptCallback(void* opaque) {
    auto* src = static_cast<CameraSource*>(opaque);
    return src->isAborted() ? 1 : 0;
}

CameraSource::CameraSource() {
    if (!deviceRegistered_) {
        avdevice_register_all();
        deviceRegistered_ = true;
        spdlog::info("CameraSource: avdevice registered");
    }
}

CameraSource::~CameraSource() {
    close();
}

void CameraSource::abort() {
    abortFlag_.store(true);
    // We no longer force-close the format context here. The capture thread
    // will exit when av_read_frame checks the interrupt callback (set above).
    // Format context cleanup happens in close(), called after thread join,
    // eliminating the use-after-free race that caused crashes on pause/stop.
}

Result<std::vector<CameraDeviceInfo>> CameraSource::enumerateDevices() {
    const AVInputFormat* dshowFmt = av_find_input_format("dshow");
    if (!dshowFmt) {
        spdlog::error("CameraSource: dshow input format not found");
        return Result<std::vector<CameraDeviceInfo>>::error(PlayerError::DecodeError);
    }

    AVDeviceInfoList* deviceList = nullptr;
    int ret = avdevice_list_input_sources(dshowFmt, nullptr, nullptr, &deviceList);
    if (ret < 0 || !deviceList) {
        spdlog::error("CameraSource: failed to list dshow devices (ret={})", ret);
        return Result<std::vector<CameraDeviceInfo>>::error(PlayerError::DeviceLost);
    }

    std::vector<CameraDeviceInfo> devices;
    for (int i = 0; i < deviceList->nb_devices; ++i) {
        AVDeviceInfo* dev = deviceList->devices[i];
        const char* desc = dev->device_description ? dev->device_description : "";
        const char* pnp = dev->device_name ? dev->device_name : "";
        std::string pnpStr(pnp);
        // Video devices contain "vid_" in PNP; audio devices have GUID class like "{33D9A..."
        if (pnpStr.find("vid_") == std::string::npos && pnpStr.find('{') != std::string::npos) {
            continue;
        }

        CameraDeviceInfo info;
        info.name = dev->device_description ? dev->device_description : dev->device_name;
        info.devicePath = std::string("video=") + desc;

        info.supportedPresets = {
            {"1080p30", 1920, 1080, 30},
            {"720p30",  1280,  720, 30},
            {"720p60",  1280,  720, 60},
            {"480p30",   640,  480, 30},
        };

        devices.push_back(std::move(info));
        spdlog::debug("CameraSource: found device '{}' ({})", info.name, info.devicePath);
    }

    avdevice_free_list_devices(&deviceList);
    spdlog::info("CameraSource: enumerated {} device(s)", devices.size());
    return Result<std::vector<CameraDeviceInfo>>::success(std::move(devices));
}

Result<void> CameraSource::open(const std::string& devicePath, int width, int height, int fps) {
    if (open_) {
        spdlog::warn("CameraSource: already open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!av_find_input_format("dshow")) {
        spdlog::error("CameraSource: dshow not available");
        return Result<void>::error(PlayerError::DecodeError);
    }

    spdlog::info("CameraSource: opening {} ({}x{}@{})", devicePath, width, height, fps);

    AVDictionary* options = nullptr;
    std::string videoSize = std::to_string(width) + "x" + std::to_string(height);
    av_dict_set(&options, "video_size", videoSize.c_str(), 0);
    av_dict_set(&options, "framerate", std::to_string(fps).c_str(), 0);
    av_dict_set(&options, "rtbufsize", "104857600", 0);  // 100MB

    const AVInputFormat* fmt = av_find_input_format("dshow");
    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_open_input(&rawCtx, devicePath.c_str(), fmt, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("CameraSource: avformat_open_input failed: {}", errBuf);
        return Result<void>::error(PlayerError::DeviceLost);
    }
    formatCtx_.reset(rawCtx);

    formatCtx_->interrupt_callback.opaque = this;
    formatCtx_->interrupt_callback.callback = cameraInterruptCallback;

    ret = avformat_find_stream_info(formatCtx_.get(), nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("CameraSource: avformat_find_stream_info failed: {}", errBuf);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    videoStreamIndex_ = -1;
    for (unsigned i = 0; i < formatCtx_->nb_streams; ++i) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex_ < 0) {
        spdlog::error("CameraSource: no video stream found");
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        spdlog::error("CameraSource: decoder not found for codec_id={}", static_cast<int>(videoStream->codecpar->codec_id));
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    AVCodecContext* rawCodecCtx = avcodec_alloc_context3(codec);
    if (!rawCodecCtx) {
        spdlog::error("CameraSource: failed to allocate codec context");
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = avcodec_parameters_to_context(rawCodecCtx, videoStream->codecpar);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("CameraSource: avcodec_parameters_to_context failed: {}", errBuf);
        avcodec_free_context(&rawCodecCtx);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    rawCodecCtx->thread_count = 1;

    ret = avcodec_open2(rawCodecCtx, codec, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("CameraSource: avcodec_open2 failed: {}", errBuf);
        avcodec_free_context(&rawCodecCtx);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    codecCtx_.reset(rawCodecCtx);
    currentFrame_ = makeAVFrame();
    open_ = true;

    spdlog::info("CameraSource: opened {} (codec={}, {}x{})",
                 devicePath, codec->name, rawCodecCtx->width, rawCodecCtx->height);
    return Result<void>::success();
}

Result<void> CameraSource::close() {
    if (!open_) {
        return Result<void>::success();
    }

    currentFrame_.reset();
    codecCtx_.reset();
    formatCtx_.reset();
    videoStreamIndex_ = -1;
    open_ = false;

    spdlog::info("CameraSource: closed");
    return Result<void>::success();
}

Result<void> CameraSource::readFrame() {
    if (!open_ || !formatCtx_ || !codecCtx_) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    AVPacketPtr packet = makeAVPacket();

    while (true) {
        int ret = av_read_frame(formatCtx_.get(), packet.get());
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("CameraSource: av_read_frame failed: {}", errBuf);
            return Result<void>::error(PlayerError::DecodeError);
        }

        if (packet->stream_index == videoStreamIndex_) {
            break;
        }

        av_packet_unref(packet.get());
    }

    int ret = avcodec_send_packet(codecCtx_.get(), packet.get());
    av_packet_unref(packet.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("CameraSource: avcodec_send_packet failed: {}", errBuf);
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = avcodec_receive_frame(codecCtx_.get(), currentFrame_.get());
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return Result<void>::success();
        }
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("CameraSource: avcodec_receive_frame failed: {}", errBuf);
        return Result<void>::error(PlayerError::DecodeError);
    }

    return Result<void>::success();
}

bool CameraSource::isOpen() const {
    return open_;
}

const AVFrame* CameraSource::getFrame() const {
    return currentFrame_.get();
}

} // namespace hlplayer
