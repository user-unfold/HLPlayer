#include "hlplayer/AudioEncoder.h"

#include <spdlog/spdlog.h>
#include <string>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace hlplayer {

using namespace hlplayer::ffmpeg;

static EncodedPacket convertPacket(AVPacket* pkt) {
    EncodedPacket out;
    if (pkt->size > 0) {
        out.data.assign(pkt->data, pkt->data + pkt->size);
    }
    out.pts = pkt->pts;
    out.dts = pkt->dts;
    out.duration = pkt->duration;
    out.isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
    out.streamIndex = static_cast<uint32_t>(pkt->stream_index);
    return out;
}

// ============================================================================
// AudioEncoder
// ============================================================================

AudioEncoder::AudioEncoder() = default;

AudioEncoder::~AudioEncoder() {
    close();
}

Result<void> AudioEncoder::init(int sampleRate, int channels, int bitrate) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (open_) {
        spdlog::error("AudioEncoder: already initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (sampleRate <= 0 || channels <= 0) {
        spdlog::error("AudioEncoder: invalid params sampleRate={} channels={}",
                      sampleRate, channels);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    sampleRate_ = sampleRate;
    channels_ = channels;
    frameIndex_ = 0;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        spdlog::error("AudioEncoder: AAC encoder not found");
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    AVCodecContext* rawCtx = avcodec_alloc_context3(codec);
    if (!rawCtx) {
        spdlog::error("AudioEncoder: failed to allocate codec context");
        return Result<void>::error(PlayerError::DecodeError);
    }
    codecCtx_.reset(rawCtx);

    rawCtx->bit_rate = bitrate;
    rawCtx->sample_rate = sampleRate;
    rawCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&rawCtx->ch_layout, channels);

    int ret = avcodec_open2(rawCtx, codec, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioEncoder: avcodec_open2 failed: {}", errBuf);
        codecCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    timeBase_ = av_q2d(rawCtx->time_base);

    AVChannelLayout outLayout, inLayout;
    av_channel_layout_default(&outLayout, channels);
    av_channel_layout_default(&inLayout, channels);

    ret = swr_alloc_set_opts2(&swrCtx_,
                               &outLayout, AV_SAMPLE_FMT_FLTP, sampleRate,
                               &inLayout, AV_SAMPLE_FMT_S16, sampleRate,
                               0, nullptr);
    av_channel_layout_uninit(&outLayout);
    av_channel_layout_uninit(&inLayout);

    if (ret < 0 || !swrCtx_) {
        spdlog::error("AudioEncoder: swr_alloc_set_opts2 failed");
        codecCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = swr_init(swrCtx_);
    if (ret < 0) {
        spdlog::error("AudioEncoder: swr_init failed");
        swr_free(&swrCtx_);
        codecCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    int frameSize = rawCtx->frame_size;
    if (frameSize <= 0) frameSize = 1024;

    swrFrame_ = makeAVFrame();
    swrFrame_->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&swrFrame_->ch_layout, channels);
    swrFrame_->sample_rate = sampleRate;
    swrFrame_->nb_samples = frameSize;

    ret = av_frame_get_buffer(swrFrame_.get(), 0);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioEncoder: av_frame_get_buffer failed: {}", errBuf);
        swr_free(&swrCtx_);
        codecCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    open_ = true;
    spdlog::info("AudioEncoder: initialized sampleRate={} channels={} bitrate={} frameSize={}",
                 sampleRate, channels, bitrate, frameSize);
    return Result<void>::success();
}

Result<std::vector<EncodedPacket>> AudioEncoder::encode(const uint8_t* pcmData, int frameCount) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_) {
        return Result<std::vector<EncodedPacket>>::error(PlayerError::InvalidState);
    }

    if (!pcmData || frameCount <= 0) {
        return Result<std::vector<EncodedPacket>>::success({});
    }

    av_frame_make_writable(swrFrame_.get());

    const uint8_t* inData[1] = {pcmData};
    int outSamples = swr_convert(swrCtx_, swrFrame_->data, swrFrame_->nb_samples,
                                  inData, frameCount);
    if (outSamples < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(outSamples, errBuf, sizeof(errBuf));
        spdlog::error("AudioEncoder: swr_convert failed: {}", errBuf);
        return Result<std::vector<EncodedPacket>>::error(PlayerError::DecodeError);
    }

    if (outSamples <= 0) {
        return Result<std::vector<EncodedPacket>>::success({});
    }

    swrFrame_->pts = frameIndex_;
    frameIndex_ += outSamples;

    int ret = avcodec_send_frame(codecCtx_.get(), swrFrame_.get());
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioEncoder: avcodec_send_frame failed: {}", errBuf);
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
            spdlog::warn("AudioEncoder: avcodec_receive_packet failed: {}", errBuf);
            break;
        }
        packets.push_back(convertPacket(pkt.get()));
    }

    return Result<std::vector<EncodedPacket>>::success(std::move(packets));
}

Result<std::vector<EncodedPacket>> AudioEncoder::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_) {
        return Result<std::vector<EncodedPacket>>::error(PlayerError::InvalidState);
    }

    std::vector<EncodedPacket> packets;

    if (swrCtx_) {
        while (true) {
            av_frame_make_writable(swrFrame_.get());
            int outSamples = swr_convert(swrCtx_, swrFrame_->data, swrFrame_->nb_samples,
                                          nullptr, 0);
            if (outSamples <= 0) break;

            swrFrame_->pts = frameIndex_;
            frameIndex_ += outSamples;

            int ret = avcodec_send_frame(codecCtx_.get(), swrFrame_.get());
            if (ret < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                spdlog::warn("AudioEncoder: flush swr send failed: {}", errBuf);
                break;
            }

            while (true) {
                AVPacketPtr pkt = makeAVPacket();
                ret = avcodec_receive_packet(codecCtx_.get(), pkt.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    spdlog::warn("AudioEncoder: flush swr receive error: {}", errBuf);
                    break;
                }
                packets.push_back(convertPacket(pkt.get()));
            }
        }
    }

    int ret = avcodec_send_frame(codecCtx_.get(), nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::warn("AudioEncoder: encoder flush send failed: {}", errBuf);
    } else {
        while (true) {
            AVPacketPtr pkt = makeAVPacket();
            ret = avcodec_receive_packet(codecCtx_.get(), pkt.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                spdlog::warn("AudioEncoder: encoder flush receive error: {}", errBuf);
                break;
            }
            packets.push_back(convertPacket(pkt.get()));
        }
    }

    return Result<std::vector<EncodedPacket>>::success(std::move(packets));
}

void AudioEncoder::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return;

    if (swrCtx_) {
        swr_free(&swrCtx_);
    }
    swrFrame_.reset();
    codecCtx_.reset();
    frameIndex_ = 0;
    timeBase_ = 0.0;
    sampleRate_ = 0;
    channels_ = 0;
    open_ = false;

    spdlog::info("AudioEncoder: closed");
}

bool AudioEncoder::isOpen() const {
    return open_;
}

// ============================================================================
// AudioCapture
// ============================================================================

bool AudioCapture::deviceRegistered_ = false;

static int audioInterruptCallback(void* opaque) {
    auto* cap = static_cast<AudioCapture*>(opaque);
    return cap->isAborted() ? 1 : 0;
}

AudioCapture::AudioCapture() {
    if (!deviceRegistered_) {
        avdevice_register_all();
        deviceRegistered_ = true;
        spdlog::info("AudioCapture: avdevice registered");
    }
}

AudioCapture::~AudioCapture() {
    close();
}

Result<std::vector<AudioDeviceInfo>> AudioCapture::enumerateAudioDevices() {
    const AVInputFormat* dshowFmt = av_find_input_format("dshow");
    if (!dshowFmt) {
        spdlog::error("AudioCapture: dshow input format not found");
        return Result<std::vector<AudioDeviceInfo>>::error(PlayerError::DecodeError);
    }

    AVDeviceInfoList* deviceList = nullptr;
    int ret = avdevice_list_input_sources(dshowFmt, nullptr, nullptr, &deviceList);
    if (ret < 0 || !deviceList) {
        spdlog::error("AudioCapture: failed to list dshow devices (ret={})", ret);
        return Result<std::vector<AudioDeviceInfo>>::error(PlayerError::DeviceLost);
    }

    std::vector<AudioDeviceInfo> devices;
    for (int i = 0; i < deviceList->nb_devices; ++i) {
        AVDeviceInfo* dev = deviceList->devices[i];
        const char* pnp = dev->device_name ? dev->device_name : "";
        std::string pnpStr(pnp);
        // Skip all video-related devices:
        // - "vid_" without "{" → pure video capture device
        // - "vid_" with "{"   → combined video+audio device (webcam with mic)
        // Only include pure audio devices (GUID class like "{33D9A...}" without "vid_").
        if (pnpStr.find("vid_") != std::string::npos) {
            continue;
        }
        // Also skip non-audio devices (pure video GUIDs look different)
        if (pnpStr.find('{') == std::string::npos) {
            continue;
        }

        AudioDeviceInfo info;
        info.name = dev->device_description ? dev->device_description : dev->device_name;
        info.devicePath = dev->device_description ? dev->device_description : "";
        devices.push_back(std::move(info));
        spdlog::debug("AudioCapture: found device '{}' ({})", info.name, info.devicePath);
    }

    avdevice_free_list_devices(&deviceList);
    spdlog::info("AudioCapture: enumerated {} device(s)", devices.size());
    return Result<std::vector<AudioDeviceInfo>>::success(std::move(devices));
}

Result<void> AudioCapture::open(const std::string& devicePath, int sampleRate, int channels) {
    if (open_) {
        spdlog::warn("AudioCapture: already open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!av_find_input_format("dshow")) {
        spdlog::error("AudioCapture: dshow not available");
        return Result<void>::error(PlayerError::DecodeError);
    }

    spdlog::info("AudioCapture: opening audio={} ({}Hz, {}ch)", devicePath, sampleRate, channels);

    AVDictionary* options = nullptr;
    av_dict_set(&options, "sample_rate", std::to_string(sampleRate).c_str(), 0);
    av_dict_set(&options, "channels", std::to_string(channels).c_str(), 0);

    std::string url = "audio=" + devicePath;

    const AVInputFormat* fmt = av_find_input_format("dshow");
    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_open_input(&rawCtx, url.c_str(), fmt, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioCapture: avformat_open_input failed: {}", errBuf);
        return Result<void>::error(PlayerError::DeviceLost);
    }
    formatCtx_.reset(rawCtx);

    formatCtx_->interrupt_callback.opaque = this;
    formatCtx_->interrupt_callback.callback = audioInterruptCallback;

    ret = avformat_find_stream_info(formatCtx_.get(), nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioCapture: avformat_find_stream_info failed: {}", errBuf);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    audioStreamIndex_ = -1;
    for (unsigned i = 0; i < formatCtx_->nb_streams; ++i) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex_ = static_cast<int>(i);
            break;
        }
    }

    if (audioStreamIndex_ < 0) {
        spdlog::error("AudioCapture: no audio stream found");
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    AVStream* audioStream = formatCtx_->streams[audioStreamIndex_];
    const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec) {
        spdlog::error("AudioCapture: decoder not found for codec_id={}",
                      static_cast<int>(audioStream->codecpar->codec_id));
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    AVCodecContext* rawCodecCtx = avcodec_alloc_context3(codec);
    if (!rawCodecCtx) {
        spdlog::error("AudioCapture: failed to allocate codec context");
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = avcodec_parameters_to_context(rawCodecCtx, audioStream->codecpar);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioCapture: avcodec_parameters_to_context failed: {}", errBuf);
        avcodec_free_context(&rawCodecCtx);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    rawCodecCtx->thread_count = 1;

    ret = avcodec_open2(rawCodecCtx, codec, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioCapture: avcodec_open2 failed: {}", errBuf);
        avcodec_free_context(&rawCodecCtx);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::DecodeError);
    }

    codecCtx_.reset(rawCodecCtx);
    currentFrame_ = makeAVFrame();
    open_ = true;

    spdlog::info("AudioCapture: opened {} (codec={}, {}Hz, {}ch)",
                 devicePath, codec->name, rawCodecCtx->sample_rate,
                 rawCodecCtx->ch_layout.nb_channels);
    return Result<void>::success();
}

Result<void> AudioCapture::readFrame() {
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
            spdlog::error("AudioCapture: av_read_frame failed: {}", errBuf);
            return Result<void>::error(PlayerError::DecodeError);
        }
        if (packet->stream_index == audioStreamIndex_) {
            break;
        }
        av_packet_unref(packet.get());
    }

    int ret = avcodec_send_packet(codecCtx_.get(), packet.get());
    av_packet_unref(packet.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioCapture: avcodec_send_packet failed: {}", errBuf);
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = avcodec_receive_frame(codecCtx_.get(), currentFrame_.get());
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return Result<void>::success();
        }
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("AudioCapture: avcodec_receive_frame failed: {}", errBuf);
        return Result<void>::error(PlayerError::DecodeError);
    }

    return Result<void>::success();
}

void AudioCapture::abort() {
    abortFlag_.store(true);
}

void AudioCapture::close() {
    if (!open_) return;

    currentFrame_.reset();
    codecCtx_.reset();
    formatCtx_.reset();
    audioStreamIndex_ = -1;
    open_ = false;

    spdlog::info("AudioCapture: closed");
}

bool AudioCapture::isOpen() const {
    return open_;
}

const AVFrame* AudioCapture::getFrame() const {
    return currentFrame_.get();
}

} // namespace hlplayer
