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
    avformat_network_init();
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

    spdlog::info("StreamMuxer::open step1: alloc context for {}", url);

    const AVOutputFormat* forceFormat = nullptr;
    if (protocol == StreamingProtocol::RTMP || url.find("rtmp://") == 0 || url.find("rtmps://") == 0) {
        forceFormat = av_guess_format("flv", nullptr, nullptr);
        if (!forceFormat) {
            spdlog::error("StreamMuxer::open - FLV format not available for RTMP");
            return Result<void>::error(PlayerError::UnsupportedFormat);
        }
    }

    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_alloc_output_context2(&rawCtx, nullptr, forceFormat ? "flv" : nullptr, url.c_str());
    if (ret < 0 || !rawCtx) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("StreamMuxer::open - avformat_alloc_output_context2 failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }
    spdlog::info("StreamMuxer::open step2: alloc_context OK, oformat={}", rawCtx->oformat->name);

    formatCtx_.reset(rawCtx);

    formatCtx_->interrupt_callback.opaque = this;
    formatCtx_->interrupt_callback.callback = interruptCallback;

    spdlog::info("StreamMuxer::open step3: about to avio_open2, flags={}",
                 (formatCtx_->oformat->flags & AVFMT_NOFILE) ? "NOFILE" : "NEEDS_FILE");

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        int maxRetries = 1;
        int retryIntervalMs = 500;

        for (int attempt = 0; attempt <= maxRetries; ++attempt) {
            ret = avio_open2(&formatCtx_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);

            if (ret >= 0) {
                spdlog::info("StreamMuxer::open step4: avio_open2 OK on attempt {}", attempt + 1);
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
    headerFailed_.store(false);
    writeFailed_.store(false);

    spdlog::info("StreamMuxer opened: url={}, protocol={}", url, static_cast<int>(protocol));
    return Result<void>::success();
}

Result<void> StreamMuxer::writePacket(const EncodedPacket& packet) {
    if (abortFlag_.load() || writeFailed_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!open_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (writeFailed_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (packet.streamIndex >= streams_.size()) {
        spdlog::error("StreamMuxer::writePacket - invalid stream index: {} (max={})",
                      packet.streamIndex, streams_.size());
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (headerFailed_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!headerWritten_.load()) {
        int ret = avformat_write_header(formatCtx_.get(), nullptr);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("StreamMuxer::writePacket - avformat_write_header failed: {}", errBuf);
            headerFailed_.store(true);
            return Result<void>::error(PlayerError::Unknown);
        }

        headerWritten_.store(true);
        spdlog::info("StreamMuxer header written");
    }

    AVPacketPtr avPkt = makeAVPacket();
    if (!avPkt) {
        return Result<void>::error(PlayerError::Unknown);
    }

    avPkt->data = const_cast<uint8_t*>(packet.data.data());
    avPkt->size = static_cast<int>(packet.data.size());
    avPkt->stream_index = static_cast<int>(packet.streamIndex);

    avPkt->pts = packet.pts;
    avPkt->dts = (packet.dts != AV_NOPTS_VALUE) ? packet.dts : packet.pts;
    avPkt->duration = packet.duration;

    if (packet.isKeyFrame) {
        avPkt->flags |= AV_PKT_FLAG_KEY;
    }

    int ret = av_interleaved_write_frame(formatCtx_.get(), avPkt.get());
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        int errors = consecutiveWriteErrors_.fetch_add(1) + 1;
        spdlog::error("StreamMuxer::writePacket - av_interleaved_write_frame failed ({}): {}",
                       errors, errBuf);
        if (errors >= 10) {
            spdlog::error("StreamMuxer: {} consecutive write errors, marking stream as failed", errors);
            writeFailed_.store(true);
        }
        return Result<void>::error(PlayerError::Unknown);
    }

    consecutiveWriteErrors_.store(0);
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
        stream->codecpar->codec_type = avcodec_get_type(codecId);
    }

    stream->codecpar->codec_tag = 0;
    stream->time_base = timeBase;

    uint32_t index = static_cast<uint32_t>(streams_.size());
    streams_.push_back(stream);

    spdlog::info("StreamMuxer added stream #{}: codec_id={}", index, static_cast<int>(stream->codecpar->codec_id));
    return Result<uint32_t>::success(index);
}

} // namespace hlplayer
