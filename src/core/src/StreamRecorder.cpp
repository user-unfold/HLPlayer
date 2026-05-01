#include "hlplayer/StreamRecorder.h"
#include "hlplayer/logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>

extern "C" {
#include <libavutil/opt.h>
}

namespace hlplayer {

StreamRecorder::StreamRecorder() = default;

StreamRecorder::~StreamRecorder() {
    stop();
}

Result<void> StreamRecorder::start(const std::string& outputDir) {
    if (recording_.load()) {
        LOG_ERROR("StreamRecorder::start - already recording");
        return Result<void>::error(PlayerError::InvalidState);
    }

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        LOG_ERROR("StreamRecorder::start - cannot create directory {}: {}", outputDir, ec.message());
        return Result<void>::error(PlayerError::Unknown);
    }

    outputDir_ = outputDir;
    fileSequence_ = 0;
    currentFileSize_.store(0);
    durationSeconds_.store(0.0);
    startTimestamp_ = 0.0;
    headerWritten_ = false;
    knownStreams_.clear();
    queueShutdown_.store(false);
    recording_.store(true);

    thread_ = std::thread(&StreamRecorder::recordLoop, this);

    LOG_INFO("StreamRecorder started, output dir: {}", outputDir);
    return Result<void>::success();
}

void StreamRecorder::writePacket(AVPacket* src, AVStream* srcStream) {
    if (!recording_.load() || !src || !srcStream) return;

    AVPacketPtr pkt(av_packet_alloc());
    if (!pkt) return;

    if (av_packet_ref(pkt.get(), src) < 0) return;

    QueueEntry entry;
    entry.packet = std::move(pkt);
    entry.srcStream = srcStream;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.size() >= kMaxQueueSize) return;
        queue_.push(std::move(entry));
    }
    queueCv_.notify_one();
}

void StreamRecorder::stop() {
    if (!recording_.load()) return;

    recording_.store(false);
    queueShutdown_.store(true);
    queueCv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }

    knownStreams_.clear();
    LOG_INFO("StreamRecorder stopped");
}

bool StreamRecorder::isRecording() const {
    return recording_.load();
}

uint64_t StreamRecorder::currentFileSize() const {
    return currentFileSize_.load();
}

double StreamRecorder::currentDuration() const {
    return durationSeconds_.load();
}

void StreamRecorder::recordLoop() {
    while (true) {
        QueueEntry entry;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] {
                return !queue_.empty() || queueShutdown_.load();
            });

            if (queue_.empty() && queueShutdown_.load()) break;
            if (queue_.empty()) continue;

            entry = std::move(queue_.front());
            queue_.pop();
        }

        if (!formatCtx_) {
            auto r = openNextFile();
            if (r.hasError()) {
                LOG_ERROR("StreamRecorder - failed to open output file, stopping");
                break;
            }
        }

        int srcIdx = entry.srcStream->index;

        auto it = knownStreams_.find(srcIdx);
        if (it == knownStreams_.end()) {
            auto addResult = addOutputStream(entry.srcStream);
            if (addResult.hasError()) {
                LOG_WARN("StreamRecorder - failed to add stream #{}", srcIdx);
                continue;
            }
            knownStreams_[srcIdx] = {entry.srcStream, addResult.value()};
            it = knownStreams_.find(srcIdx);
        }

        KnownStream& ks = it->second;
        AVPacket* pkt = entry.packet.get();
        int outIdx = ks.outIndex;

        // Capture values before write — av_interleaved_write_frame consumes the packet
        int64_t pts = pkt->pts;
        AVRational srcTb = entry.srcStream->time_base;
        AVRational outTb = formatCtx_->streams[outIdx]->time_base;

        if (startTimestamp_ == 0.0 && pts != AV_NOPTS_VALUE && srcTb.den > 0) {
            startTimestamp_ = av_q2d(srcTb) * static_cast<double>(pts);
        }

        av_packet_rescale_ts(pkt, srcTb, outTb);
        pkt->stream_index = outIdx;

        int ret = av_interleaved_write_frame(formatCtx_.get(), pkt);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            if (ret == AVERROR(ENOSPC) || ret == AVERROR(EIO)) {
                LOG_ERROR("StreamRecorder - disk full or I/O error: {}, stopping", errBuf);
                closeCurrentFile();
                recording_.store(false);
                break;
            }
            LOG_WARN("StreamRecorder - write failed: {}", errBuf);
            continue;
        }

        if (pts != AV_NOPTS_VALUE && srcTb.den > 0) {
            double ptsSec = av_q2d(srcTb) * static_cast<double>(pts);
            if (startTimestamp_ > 0.0 && ptsSec > startTimestamp_) {
                durationSeconds_.store(ptsSec - startTimestamp_);
            }
        }

        if (formatCtx_->pb) {
            currentFileSize_.store(static_cast<uint64_t>(avio_tell(formatCtx_->pb)));
        }

        if (currentFileSize_.load() >= kMaxFileSize) {
            LOG_INFO("StreamRecorder - rotating at {} bytes", currentFileSize_.load());
            closeCurrentFile();
        }
    }

    closeCurrentFile();
}

Result<void> StreamRecorder::openNextFile() {
    auto now = std::chrono::system_clock::now();
    auto timeNow = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow{};
#ifdef _WIN32
    localtime_s(&tmNow, &timeNow);
#else
    localtime_r(&timeNow, &tmNow);
#endif

    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tmNow);

    std::string filename = std::format("stream_{}.mkv", timeBuf);
    std::string fullPath = (std::filesystem::path(outputDir_) / filename).string();

    const AVOutputFormat* outputFormat = av_guess_format("matroska", nullptr, nullptr);
    if (!outputFormat) {
        LOG_ERROR("StreamRecorder - matroska format not available");
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_alloc_output_context2(&rawCtx, outputFormat, nullptr, fullPath.c_str());
    if (ret < 0 || !rawCtx) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_ERROR("StreamRecorder - alloc output context failed: {}", errBuf);
        return Result<void>::error(PlayerError::Unknown);
    }

    formatCtx_.reset(rawCtx);

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&formatCtx_->pb, fullPath.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            LOG_ERROR("StreamRecorder - avio_open2 failed: {}", errBuf);
            formatCtx_.reset();
            return Result<void>::error(PlayerError::Unknown);
        }
    }

    auto addResult = addAllKnownStreams();
    if (addResult.hasError()) {
        LOG_ERROR("StreamRecorder - failed to add known streams");
        formatCtx_.reset();
        return addResult;
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "live", "1", 0);
    ret = avformat_write_header(formatCtx_.get(), &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_ERROR("StreamRecorder - write header failed: {}", errBuf);
        formatCtx_.reset();
        return Result<void>::error(PlayerError::Unknown);
    }

    headerWritten_ = true;
    currentFileSize_.store(0);
    startTimestamp_ = 0.0;
    durationSeconds_.store(0.0);
    fileSequence_++;

    LOG_INFO("StreamRecorder opened: {}", fullPath);
    return Result<void>::success();
}

Result<int> StreamRecorder::addOutputStream(AVStream* srcStream) {
    if (!formatCtx_ || !srcStream) {
        return Result<int>::error(PlayerError::InvalidState);
    }

    AVStream* outStream = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!outStream) {
        LOG_ERROR("StreamRecorder - avformat_new_stream failed");
        return Result<int>::error(PlayerError::Unknown);
    }

    int ret = avcodec_parameters_copy(outStream->codecpar, srcStream->codecpar);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOG_ERROR("StreamRecorder - avcodec_parameters_copy failed: {}", errBuf);
        return Result<int>::error(PlayerError::Unknown);
    }

    outStream->time_base = srcStream->time_base;
    outStream->codecpar->codec_tag = 0;

    int outIdx = outStream->index;
    LOG_INFO("StreamRecorder added stream #{} (src #{})", outIdx, srcStream->index);
    return Result<int>::success(outIdx);
}

Result<void> StreamRecorder::addAllKnownStreams() {
    for (auto& [srcIdx, ks] : knownStreams_) {
        auto r = addOutputStream(ks.srcStream);
        if (r.hasError()) {
            return Result<void>::error(r.error());
        }
        ks.outIndex = r.value();
    }
    return Result<void>::success();
}

void StreamRecorder::closeCurrentFile() {
    if (!formatCtx_) return;

    if (headerWritten_) {
        av_write_trailer(formatCtx_.get());
        headerWritten_ = false;
    }

    if (formatCtx_->pb) {
        avio_closep(&formatCtx_->pb);
    }

    formatCtx_.reset();
}

} // namespace hlplayer
