#include "FFmpegMuxer.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/channel_layout.h>
}

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

FFmpegMuxer::FFmpegMuxer() {
    av_log_set_level(AV_LOG_ERROR);
}

FFmpegMuxer::~FFmpegMuxer() {
    close();
}

AVCodecID FFmpegMuxer::toAVCodecID(Codec codec) {
    switch (codec) {
        case Codec::H264:  return AV_CODEC_ID_H264;
        case Codec::HEVC:  return AV_CODEC_ID_HEVC;
        case Codec::AV1:   return AV_CODEC_ID_AV1;
        default:           return AV_CODEC_ID_NONE;
    }
}

const AVOutputFormat* FFmpegMuxer::findOutputFormat(const std::string& format) {
    if (format == "mkv" || format == "matroska") {
        return av_guess_format("matroska", nullptr, nullptr);
    }
    return av_guess_format("mp4", nullptr, nullptr);
}

Result<void> FFmpegMuxer::open(const MuxerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (open_.load()) {
        spdlog::error("FFmpegMuxer::open - already open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_ = config;
    const auto* outputFormat = findOutputFormat(config.format);
    if (!outputFormat) {
        spdlog::error("FFmpegMuxer::open - unsupported format: {}", config.format);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_alloc_output_context2(&rawCtx, outputFormat, nullptr, config.outputPath.c_str());
    if (ret < 0 || !rawCtx) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("FFmpegMuxer::open - avformat_alloc_output_context2 failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }

    formatCtx_.reset(rawCtx);

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&formatCtx_->pb, config.outputPath.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("FFmpegMuxer::open - avio_open2 failed: {}", errBuf);
            formatCtx_.reset();
            return Result<void>::error(PlayerError::Unknown);
        }
    }

    open_.store(true);
    headerWritten_.store(false);

    spdlog::info("FFmpegMuxer opened: path={}, format={}", config.outputPath, config.format);
    return Result<void>::success();
}

Result<uint32_t> FFmpegMuxer::addStream(const EncoderConfig& streamConfig) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        spdlog::error("FFmpegMuxer::addStream - not open");
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    if (headerWritten_.load()) {
        spdlog::error("FFmpegMuxer::addStream - header already written, cannot add stream");
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    AVCodecID codecId = toAVCodecID(streamConfig.codec);
    if (codecId == AV_CODEC_ID_NONE) {
        spdlog::error("FFmpegMuxer::addStream - unsupported codec: {}", static_cast<int>(streamConfig.codec));
        return Result<uint32_t>::error(PlayerError::UnsupportedFormat);
    }

    AVStream* stream = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!stream) {
        spdlog::error("FFmpegMuxer::addStream - avformat_new_stream failed");
        return Result<uint32_t>::error(PlayerError::Unknown);
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = codecId;
    stream->codecpar->width = static_cast<int>(streamConfig.width);
    stream->codecpar->height = static_cast<int>(streamConfig.height);

    stream->time_base = AVRational{1, static_cast<int>(streamConfig.frameRate * 1000)};
    stream->codecpar->sample_aspect_ratio = AVRational{1, 1};

    if (streamConfig.bitrate > 0) {
        stream->codecpar->bit_rate = static_cast<int64_t>(streamConfig.bitrate);
    }

    uint32_t streamIndex = static_cast<uint32_t>(streams_.size());
    streams_.push_back(stream);
    streamConfigs_.push_back(streamConfig);

    spdlog::info("FFmpegMuxer added stream #{}: codec={}, {}x{}, {:.1f}fps",
                 streamIndex, static_cast<int>(streamConfig.codec),
                 streamConfig.width, streamConfig.height, streamConfig.frameRate);
    return Result<uint32_t>::success(streamIndex);
}

Result<uint32_t> FFmpegMuxer::addAudioStream(int codecId, int sampleRate, int channels,
                                              const std::vector<uint8_t>& extradata) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        spdlog::error("FFmpegMuxer::addAudioStream - not open");
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    if (headerWritten_.load()) {
        spdlog::error("FFmpegMuxer::addAudioStream - header already written");
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    if (codecId == 0 || sampleRate <= 0 || channels <= 0) {
        spdlog::error("FFmpegMuxer::addAudioStream - invalid params: codec={}, sr={}, ch={}",
                      codecId, sampleRate, channels);
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    AVStream* stream = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!stream) {
        spdlog::error("FFmpegMuxer::addAudioStream - avformat_new_stream failed");
        return Result<uint32_t>::error(PlayerError::Unknown);
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->codec_id = static_cast<AVCodecID>(codecId);
    stream->codecpar->sample_rate = sampleRate;
    av_channel_layout_default(&stream->codecpar->ch_layout, channels);
    stream->time_base = AVRational{1, sampleRate};

    if (!extradata.empty()) {
        stream->codecpar->extradata = static_cast<uint8_t*>(
            av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (stream->codecpar->extradata) {
            std::memcpy(stream->codecpar->extradata, extradata.data(), extradata.size());
            stream->codecpar->extradata_size = static_cast<int>(extradata.size());
        }
    }

    uint32_t streamIndex = static_cast<uint32_t>(streams_.size());
    streams_.push_back(stream);
    streamConfigs_.emplace_back();

    spdlog::info("FFmpegMuxer added audio stream #{}: codec={}, {}Hz, {}ch",
                 streamIndex, codecId, sampleRate, channels);
    return Result<uint32_t>::success(streamIndex);
}

Result<void> FFmpegMuxer::writePacket(const EncodedPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        spdlog::error("FFmpegMuxer::writePacket - not open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (packet.streamIndex >= streams_.size()) {
        spdlog::error("FFmpegMuxer::writePacket - invalid stream index: {} (max={})",
                      packet.streamIndex, streams_.size());
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!headerWritten_.load()) {
        AVDictionary* opts = nullptr;

        if (config_.fastStart && (config_.format == "mp4")) {
            av_dict_set(&opts, "movflags", "+faststart", 0);
        }

        if (config_.format == "mkv") {
            av_dict_set(&opts, "live", "1", 0);
        }

        int ret = avformat_write_header(formatCtx_.get(), &opts);
        AVDictionaryPtr cleanupOpts(opts);

        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("FFmpegMuxer::writePacket - avformat_write_header failed: {}", errBuf);
            return Result<void>::error(PlayerError::Unknown);
        }

        headerWritten_.store(true);
        spdlog::info("FFmpegMuxer header written");
    }

    AVPacketPtr avPkt = makeAVPacket();
    if (!avPkt) {
        spdlog::error("FFmpegMuxer::writePacket - failed to allocate AVPacket");
        return Result<void>::error(PlayerError::Unknown);
    }

    avPkt->data = const_cast<uint8_t*>(packet.data.data());
    avPkt->size = static_cast<int>(packet.data.size());
    avPkt->stream_index = static_cast<int>(packet.streamIndex);

    AVStream* stream = streams_[packet.streamIndex];
    double tb = av_q2d(stream->time_base);
    if (tb > 0.0) {
        avPkt->pts = static_cast<int64_t>(packet.pts / tb);
        avPkt->dts = static_cast<int64_t>(packet.dts / tb);
        if (packet.duration > 0) {
            avPkt->duration = static_cast<int>(packet.duration / tb);
        }
    } else {
        avPkt->pts = static_cast<int64_t>(packet.pts);
        avPkt->dts = static_cast<int64_t>(packet.dts);
        avPkt->duration = static_cast<int>(packet.duration);
    }

    if (packet.isKeyFrame) {
        avPkt->flags |= AV_PKT_FLAG_KEY;
    }

    int ret = av_interleaved_write_frame(formatCtx_.get(), avPkt.get());
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("FFmpegMuxer::writePacket - av_interleaved_write_frame failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }

    return Result<void>::success();
}

Result<void> FFmpegMuxer::finalize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        spdlog::error("FFmpegMuxer::finalize - not open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!headerWritten_.load()) {
        spdlog::warn("FFmpegMuxer::finalize - no header written, nothing to finalize");
        return Result<void>::success();
    }

    int ret = av_write_trailer(formatCtx_.get());
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("FFmpegMuxer::finalize - av_write_trailer failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }

    spdlog::info("FFmpegMuxer finalized: {}", config_.outputPath);
    headerWritten_.store(false);
    return Result<void>::success();
}

void FFmpegMuxer::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        return;
    }

    if (headerWritten_.load()) {
        av_write_trailer(formatCtx_.get());
    }

    if (formatCtx_ && !(formatCtx_->oformat->flags & AVFMT_NOFILE) && formatCtx_->pb) {
        avio_closep(&formatCtx_->pb);
    }

    streams_.clear();
    streamConfigs_.clear();
    formatCtx_.reset();
    headerWritten_.store(false);
    open_.store(false);

    spdlog::info("FFmpegMuxer closed");
}

MuxerConfig FFmpegMuxer::getConfig() const {
    return config_;
}

uint32_t FFmpegMuxer::getStreamCount() const {
    return static_cast<uint32_t>(streams_.size());
}

bool FFmpegMuxer::isOpen() const {
    return open_.load();
}

} // namespace extractor
} // namespace hlplayer
