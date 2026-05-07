#include "hlplayer/StreamingPipeline.h"

#include <spdlog/spdlog.h>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace hlplayer {

using namespace hlplayer::ffmpeg;

static int streamingInterruptCallback(void* opaque) {
    auto* pipeline = static_cast<StreamingPipeline*>(opaque);
    return pipeline->isCancelled() ? 1 : 0;
}

StreamingPipeline::StreamingPipeline() {
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();
}

StreamingPipeline::~StreamingPipeline() {
    cancel();
    avformat_network_deinit();
}

Result<void> StreamingPipeline::start(const StreamingConfig& config, StreamingStateCallback callback) {
    if (state_.load() != StreamingState::Idle) {
        spdlog::error("StreamingPipeline::start - not idle (state={})", static_cast<int>(state_.load()));
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (config.sourcePath.empty()) {
        spdlog::error("StreamingPipeline::start - empty source path");
        return Result<void>::error(PlayerError::InvalidURL);
    }

    if (config.url.empty()) {
        spdlog::error("StreamingPipeline::start - empty destination URL");
        return Result<void>::error(PlayerError::InvalidURL);
    }

    callback_ = std::move(callback);
    cancelFlag_.store(false);
    cleanedUp_.store(false);
    videoStreamIdx_ = -1;
    audioStreamIdx_ = -1;

    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_open_input(&rawCtx, config.sourcePath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("StreamingPipeline::start - failed to open source: {}", errBuf);
        state_.store(StreamingState::Failed);
        return Result<void>::error(PlayerError::InvalidURL);
    }
    sourceCtx_.reset(rawCtx);

    sourceCtx_->interrupt_callback.opaque = this;
    sourceCtx_->interrupt_callback.callback = streamingInterruptCallback;

    ret = avformat_find_stream_info(sourceCtx_.get(), nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("StreamingPipeline::start - failed to find stream info: {}", errBuf);
        sourceCtx_.reset();
        state_.store(StreamingState::Failed);
        return Result<void>::error(PlayerError::DecodeError);
    }

    for (unsigned i = 0; i < sourceCtx_->nb_streams; ++i) {
        AVMediaType type = sourceCtx_->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && videoStreamIdx_ < 0) {
            videoStreamIdx_ = static_cast<int>(i);
        } else if (type == AVMEDIA_TYPE_AUDIO && audioStreamIdx_ < 0) {
            audioStreamIdx_ = static_cast<int>(i);
        }
    }

    if (videoStreamIdx_ < 0) {
        spdlog::error("StreamingPipeline::start - no video stream in source");
        sourceCtx_.reset();
        state_.store(StreamingState::Failed);
        return Result<void>::error(PlayerError::DecodeError);
    }

    double totalDuration = 0.0;
    if (sourceCtx_->duration > 0) {
        totalDuration = static_cast<double>(sourceCtx_->duration) / AV_TIME_BASE;
    } else if (sourceCtx_->streams[videoStreamIdx_]->duration != AV_NOPTS_VALUE) {
        AVRational tb = sourceCtx_->streams[videoStreamIdx_]->time_base;
        totalDuration = av_q2d(tb) * static_cast<double>(sourceCtx_->streams[videoStreamIdx_]->duration);
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = {};
        stats_.totalDuration = totalDuration;
    }

    spdlog::info("StreamingPipeline: source={}, duration={:.1f}s, video={}, audio={}",
                 config.sourcePath, totalDuration, videoStreamIdx_, audioStreamIdx_);

    StreamingProtocol protocol = StreamingProtocol::RTMP;
    if (config.url.compare(0, 6, "srt://") == 0) {
        protocol = StreamingProtocol::SRT;
    }

    state_.store(StreamingState::Connecting);
    if (callback_) {
        callback_(StreamingState::Connecting, getStats());
    }

    auto openResult = muxer_.open(config.url, protocol);
    if (openResult.hasError()) {
        spdlog::error("StreamingPipeline::start - failed to connect to {}", config.url);
        sourceCtx_.reset();
        state_.store(StreamingState::Failed);
        if (callback_) {
            callback_(StreamingState::Failed, getStats());
        }
        return Result<void>::error(PlayerError::NetworkError);
    }

    AVStream* videoSrc = sourceCtx_->streams[videoStreamIdx_];
    auto addResult = muxer_.addStream(videoSrc->codecpar->codec_id,
                                       videoSrc->time_base,
                                       videoSrc->codecpar);
    if (addResult.hasError()) {
        spdlog::error("StreamingPipeline::start - failed to add video stream");
        muxer_.close();
        sourceCtx_.reset();
        state_.store(StreamingState::Failed);
        if (callback_) {
            callback_(StreamingState::Failed, getStats());
        }
        return Result<void>::error(PlayerError::Unknown);
    }
    dstVideoIdx_ = addResult.value();

    if (audioStreamIdx_ >= 0) {
        AVStream* audioSrc = sourceCtx_->streams[audioStreamIdx_];
        addResult = muxer_.addStream(audioSrc->codecpar->codec_id,
                                      audioSrc->time_base,
                                      audioSrc->codecpar);
        if (addResult.hasError()) {
            spdlog::warn("StreamingPipeline::start - failed to add audio stream, continuing video-only");
        } else {
            dstAudioIdx_ = addResult.value();
        }
    }

    state_.store(StreamingState::Streaming);
    thread_ = std::thread(&StreamingPipeline::streamingLoop, this);

    spdlog::info("StreamingPipeline started: {} -> {}", config.sourcePath, config.url);
    return Result<void>::success();
}

void StreamingPipeline::streamingLoop() {
    using namespace std::chrono;

    AVPacketPtr packet = makeAVPacket();
    auto streamStartTime = steady_clock::now();
    int64_t firstPts = AV_NOPTS_VALUE;
    auto lastCallbackTime = steady_clock::now();
    int64_t bytesSinceLastUpdate = 0;

    while (!cancelFlag_.load()) {
        int ret = av_read_frame(sourceCtx_.get(), packet.get());
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                spdlog::info("StreamingPipeline: end of source file");
                break;
            }
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("StreamingPipeline: read error: {}", errBuf);
            state_.store(StreamingState::Failed);
            if (callback_) {
                callback_(StreamingState::Failed, getStats());
            }
            break;
        }

        int streamIdx = packet->stream_index;
        bool isVideo = (streamIdx == videoStreamIdx_);
        bool isAudio = (streamIdx == audioStreamIdx_);

        if (!isVideo && !isAudio) {
            av_packet_unref(packet.get());
            continue;
        }

        uint32_t dstIdx = isVideo ? dstVideoIdx_ : dstAudioIdx_;
        AVRational srcTb = sourceCtx_->streams[streamIdx]->time_base;

        int64_t pts = (packet->pts != AV_NOPTS_VALUE) ? packet->pts : packet->dts;

        if (isVideo && firstPts == AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE) {
            firstPts = pts;
            streamStartTime = steady_clock::now();
        }

        if (firstPts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE && isVideo) {
            double elapsed = av_q2d(srcTb) * static_cast<double>(pts - firstPts);
            auto targetTime = streamStartTime + duration<double>(elapsed);
            auto now = steady_clock::now();
            if (targetTime > now) {
                std::this_thread::sleep_until(targetTime);
            }
        }

        EncodedPacket encodedPkt;
        encodedPkt.data.assign(packet->data, packet->data + packet->size);
        encodedPkt.streamIndex = dstIdx;
        encodedPkt.isKeyFrame = (packet->flags & AV_PKT_FLAG_KEY) != 0;

        encodedPkt.pts = (packet->pts != AV_NOPTS_VALUE) ? packet->pts : packet->dts;
        encodedPkt.dts = (packet->dts != AV_NOPTS_VALUE) ? packet->dts : packet->pts;
        encodedPkt.duration = packet->duration;

        auto writeResult = muxer_.writePacket(encodedPkt);
        if (writeResult.hasError()) {
            spdlog::error("StreamingPipeline: write failed (stream {}, pts {})",
                          dstIdx, encodedPkt.pts);
            state_.store(StreamingState::Failed);
            if (callback_) {
                callback_(StreamingState::Failed, getStats());
            }
            av_packet_unref(packet.get());
            break;
        }

        bytesSinceLastUpdate += packet->size;

        auto now = steady_clock::now();
        double sinceLast = duration<double>(now - lastCallbackTime).count();
        if (sinceLast >= 0.1) {
            double ptsSeconds = av_q2d(srcTb) * static_cast<double>(encodedPkt.pts);
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.streamedDuration = ptsSeconds;
                stats_.currentBitrate = (bytesSinceLastUpdate * 8.0) / sinceLast;
                if (stats_.totalDuration > 0.0) {
                    stats_.progress = std::min(ptsSeconds / stats_.totalDuration, 1.0);
                }
            }
            bytesSinceLastUpdate = 0;
            lastCallbackTime = now;
            if (callback_) {
                callback_(StreamingState::Streaming, getStats());
            }
        }

        av_packet_unref(packet.get());
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.progress = 1.0;
        stats_.streamedDuration = stats_.totalDuration;
    }

    if (!cleanedUp_.exchange(true)) {
        muxer_.close();
        sourceCtx_.reset();
    }

    if (!cancelFlag_.load()) {
        state_.store(StreamingState::Completed);
        if (callback_) {
            callback_(StreamingState::Completed, getStats());
        }
    } else {
        state_.store(StreamingState::Cancelled);
        if (callback_) {
            callback_(StreamingState::Cancelled, getStats());
        }
    }

    spdlog::info("StreamingPipeline: loop finished");
}

Result<void> StreamingPipeline::cancel() {
    if (state_.load() == StreamingState::Idle) {
        return Result<void>::success();
    }

    cancelFlag_.store(true);

    muxer_.abort();

    if (thread_.joinable()) {
        if (thread_.get_id() != std::this_thread::get_id()) {
            thread_.join();
        }
    }

    if (!cleanedUp_.exchange(true)) {
        muxer_.close();
        sourceCtx_.reset();
    }

    StreamingState currentState = state_.load();
    if (currentState != StreamingState::Completed && currentState != StreamingState::Failed) {
        state_.store(StreamingState::Cancelled);
        if (callback_) {
            callback_(StreamingState::Cancelled, getStats());
        }
    }

    return Result<void>::success();
}

StreamingStats StreamingPipeline::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    StreamingStats s = stats_;
    s.state = state_.load();
    return s;
}

StreamingState StreamingPipeline::getState() const {
    return state_.load();
}

} // namespace hlplayer
