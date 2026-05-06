#include "hlplayer/StreamMuxer.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

namespace hlplayer {

using namespace hlplayer::ffmpeg;

StreamMuxer::StreamMuxer() {
    av_log_set_level(AV_LOG_ERROR);
}

StreamMuxer::~StreamMuxer() {
    abort();
    close();
}

static int interruptCallback(void* opaque) {
    auto* muxer = static_cast<StreamMuxer*>(opaque);
    return muxer->isAborted() ? 1 : 0;
}

void StreamMuxer::abort() {
    abortFlag_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (formatCtx_ && formatCtx_->pb) {
        avio_flush(formatCtx_->pb);
        avio_closep(&formatCtx_->pb);
    }
}

bool StreamMuxer::isAborted() const {
    return abortFlag_.load();
}

Result<void> StreamMuxer::open(const std::string& url, StreamingProtocol protocol) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (open_.load()) {
        spdlog::error("StreamMuxer::open - already open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    url_ = url;
    protocol_ = protocol;

    const char* formatName = nullptr;
    if (protocol == StreamingProtocol::RTMP) {
        formatName = "flv";
    } else if (protocol == StreamingProtocol::SRT) {
        formatName = "mpegts";
    } else {
        spdlog::error("StreamMuxer::open - unsupported protocol: {}", static_cast<int>(protocol));
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    const AVOutputFormat* outputFormat = av_guess_format(formatName, nullptr, nullptr);
    if (!outputFormat) {
        spdlog::error("StreamMuxer::open - av_guess_format failed for format: {}", formatName);
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_alloc_output_context2(&rawCtx, outputFormat, nullptr, url.c_str());
    if (ret < 0 || !rawCtx) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("StreamMuxer::open - avformat_alloc_output_context2 failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }

    formatCtx_.reset(rawCtx);

    formatCtx_->interrupt_callback.opaque = this;
    formatCtx_->interrupt_callback.callback = interruptCallback;

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        int maxRetries = 3;
        int retryIntervalMs = 3000;

        for (int attempt = 0; attempt <= maxRetries; ++attempt) {
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "timeout", "5000000", 0);

            ret = avio_open2(&formatCtx_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, &opts);
            AVDictionaryPtr cleanupOpts(opts);

            if (ret >= 0) {
                break;
            }

            if (attempt < maxRetries) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                spdlog::warn("StreamMuxer::open - connection failed (attempt {}/{}): {}, retrying in {}ms",
                             attempt + 1, maxRetries + 1, errBuf, retryIntervalMs);
                std::this_thread::sleep_for(std::chrono::milliseconds(retryIntervalMs));
            } else {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                spdlog::error("StreamMuxer::open - avio_open2 failed after {} attempts: {}", maxRetries + 1, errBuf);
                formatCtx_.reset();
                return Result<void>::error(PlayerError::Unknown);
            }
        }
    }

    open_.store(true);
    headerWritten_.store(false);

    spdlog::info("StreamMuxer opened: url={}, protocol={}", url, static_cast<int>(protocol));
    return Result<void>::success();
}

Result<void> StreamMuxer::writePacket(const EncodedPacket& packet) {
    if (abortFlag_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        spdlog::error("StreamMuxer::writePacket - not open");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (packet.streamIndex >= streams_.size()) {
        spdlog::error("StreamMuxer::writePacket - invalid stream index: {} (max={})",
                      packet.streamIndex, streams_.size());
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!headerWritten_.load()) {
        int ret = avformat_write_header(formatCtx_.get(), nullptr);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("StreamMuxer::writePacket - avformat_write_header failed: {}", errBuf);
            return Result<void>::error(PlayerError::Unknown);
        }

        headerWritten_.store(true);
        spdlog::info("StreamMuxer header written");
    }

    AVPacketPtr avPkt = makeAVPacket();
    if (!avPkt) {
        spdlog::error("StreamMuxer::writePacket - failed to allocate AVPacket");
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
        spdlog::error("StreamMuxer::writePacket - av_interleaved_write_frame failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }

    return Result<void>::success();
}

Result<void> StreamMuxer::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        return Result<void>::success();
    }

    if (headerWritten_.load()) {
        int ret = av_write_trailer(formatCtx_.get());
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("StreamMuxer::close - av_write_trailer failed: {}", errBuf);
        }
    }

    if (formatCtx_ && !(formatCtx_->oformat->flags & AVFMT_NOFILE) && formatCtx_->pb) {
        avio_closep(&formatCtx_->pb);
    }

    streams_.clear();
    formatCtx_.reset();
    headerWritten_.store(false);
    open_.store(false);
    abortFlag_.store(false);

    spdlog::info("StreamMuxer closed: {}", url_);
    return Result<void>::success();
}

bool StreamMuxer::isOpen() const {
    return open_.load();
}

Result<uint32_t> StreamMuxer::addStream(AVCodecID codecId, const AVRational& timeBase,
                                         AVCodecParameters* codecParams) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        spdlog::error("StreamMuxer::addStream - not open");
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    if (headerWritten_.load()) {
        spdlog::error("StreamMuxer::addStream - header already written");
        return Result<uint32_t>::error(PlayerError::InvalidState);
    }

    AVStream* stream = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!stream) {
        spdlog::error("StreamMuxer::addStream - avformat_new_stream failed");
        return Result<uint32_t>::error(PlayerError::Unknown);
    }

    if (codecParams) {
        int ret = avcodec_parameters_copy(stream->codecpar, codecParams);
        if (ret < 0) {
            spdlog::error("StreamMuxer::addStream - avcodec_parameters_copy failed");
            return Result<uint32_t>::error(PlayerError::Unknown);
        }
    } else {
        stream->codecpar->codec_id = codecId;
    }

    stream->time_base = timeBase;

    uint32_t index = static_cast<uint32_t>(streams_.size());
    streams_.push_back(stream);

    spdlog::info("StreamMuxer added stream #{}: codec_id={}", index, static_cast<int>(stream->codecpar->codec_id));
    return Result<uint32_t>::success(index);
}

} // namespace hlplayer
