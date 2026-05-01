#include "OfflineTranscodePipeline.h"
#include <hlplayer/logger.h>
#include <hlplayer/IPipelineNode.h>

#include <spdlog/spdlog.h>

#ifdef _WIN32
    #define __STDC_CONSTANT_MACROS
    #define __STDC_FORMAT_MACROS
    #include <windows.h>
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <chrono>
#include <filesystem>
#include <system_error>
#include <csignal>

namespace hlplayer {

OfflineTranscodePipeline::OfflineTranscodePipeline() {
    videoPacketQueue_ = std::make_unique<PacketQueue>(kDefaultPacketQueueSize);
    audioPacketQueue_ = std::make_unique<PacketQueue>(kDefaultPacketQueueSize);
    decodedFrameQueue_ = std::make_unique<VideoFrameQueue>(kDefaultFrameQueueSize);
    vsrFrameQueue_ = std::make_unique<VideoFrameQueue>(kDefaultFrameQueueSize);
    encodedQueue_ = std::make_unique<EncodedPacketQueue>(kDefaultEncodedQueueSize);
}

OfflineTranscodePipeline::~OfflineTranscodePipeline() {
    cancel();
    shutdownQueues();

    for (auto& t : {std::ref(demuxThread_), std::ref(decodeThread_),
                    std::ref(vsrThread_), std::ref(encodeThread_),
                    std::ref(muxThread_), std::ref(audioThread_)}) {
        if (t.get().joinable()) {
            t.get().join();
        }
    }
}

void OfflineTranscodePipeline::setDecoder(std::shared_ptr<IHWDecoder> decoder) {
    decoder_ = std::move(decoder);
}

void OfflineTranscodePipeline::setEncoder(std::shared_ptr<IVideoEncoder> encoder) {
    encoder_ = std::move(encoder);
}

void OfflineTranscodePipeline::setMuxer(std::shared_ptr<IMuxer> muxer) {
    muxer_ = std::move(muxer);
}

void OfflineTranscodePipeline::setCheckpointManager(
    std::shared_ptr<ICheckpointManager> checkpointManager) {
    checkpointManager_ = std::move(checkpointManager);
}

void OfflineTranscodePipeline::setVRAMBudgetManager(
    std::shared_ptr<IVRAMBudgetManager> vramManager) {
    vramManager_ = std::move(vramManager);
}

void OfflineTranscodePipeline::setVSRNode(std::shared_ptr<IPipelineNode> vsrNode) {
    vsrNode_ = std::move(vsrNode);
}

void OfflineTranscodePipeline::setProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

void OfflineTranscodePipeline::setErrorCallback(ErrorCallback callback) {
    errorCallback_ = std::move(callback);
}

Result<void> OfflineTranscodePipeline::configure(const OfflineTranscodeConfig& config) {
    std::lock_guard<std::mutex> lock(stateMutex_);

    if (state_.load() != State::Idle) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!decoder_) {
        LOG_ERROR("OfflineTranscodePipeline::configure: decoder not set");
        return Result<void>::error(PlayerError::InvalidState);
    }
    if (!encoder_) {
        LOG_ERROR("OfflineTranscodePipeline::configure: encoder not set");
        return Result<void>::error(PlayerError::InvalidState);
    }
    if (!muxer_) {
        LOG_ERROR("OfflineTranscodePipeline::configure: muxer not set");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (config.inputPath.empty() || config.outputPath.empty()) {
        LOG_ERROR("OfflineTranscodePipeline::configure: input/output path empty");
        return Result<void>::error(PlayerError::InvalidURL);
    }

    config_ = config;

    videoPacketQueue_ = std::make_unique<PacketQueue>(config_.packetQueueSize);
    audioPacketQueue_ = std::make_unique<PacketQueue>(config_.packetQueueSize);
    decodedFrameQueue_ = std::make_unique<VideoFrameQueue>(config_.frameQueueSize);
    vsrFrameQueue_ = std::make_unique<VideoFrameQueue>(config_.frameQueueSize);
    encodedQueue_ = std::make_unique<EncodedPacketQueue>(config_.encodedQueueSize);

    state_.store(State::Configured);
    LOG_INFO("OfflineTranscodePipeline: configured ({} -> {})",
             config_.inputPath, config_.outputPath);
    return Result<void>::success();
}

Result<void> OfflineTranscodePipeline::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);

    if (state_.load() != State::Configured) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    cancelRequested_.store(false);
    pauseRequested_.store(false);
    errorFlag_.store(false);
    framesProcessed_.store(0);
    currentFps_.store(0.0);

    std::signal(SIGSEGV, [](int) {
        spdlog::critical("SIGSEGV caught in OfflineTranscodePipeline!");
        spdlog::default_logger()->flush();
        std::_Exit(1);
    });
    std::signal(SIGABRT, [](int) {
        spdlog::critical("SIGABRT caught in OfflineTranscodePipeline!");
        spdlog::default_logger()->flush();
        std::_Exit(1);
    });

    MuxerConfig muxConfig;
    muxConfig.outputPath = config_.outputPath;
    muxConfig.format = config_.outputFormat;
    muxConfig.fastStart = config_.fastStart;

    auto muxResult = muxer_->open(muxConfig);
    if (muxResult.hasError()) {
        LOG_ERROR("OfflineTranscodePipeline::start: muxer open failed");
        return Result<void>::error(muxResult.error());
    }

    auto encConfig = config_.encoderConfig;
    auto vsResult = muxer_->addStream(encConfig);
    if (vsResult.hasError()) {
        LOG_ERROR("OfflineTranscodePipeline::start: add video stream failed");
        muxer_->close();
        return Result<void>::error(vsResult.error());
    }
    videoStreamIndex_ = vsResult.value();

    if (config_.audioPassthrough && config_.audioCodecId != 0) {
        auto asResult = muxer_->addAudioStream(
            config_.audioCodecId, config_.audioSampleRate,
            config_.audioChannels, config_.audioExtradata);
        if (!asResult.hasError()) {
            audioStreamIndex_ = asResult.value();
        } else {
            LOG_WARN("OfflineTranscodePipeline::start: audio stream add failed, continuing without audio");
        }
    }

    state_.store(State::Running);
    startTime_ = std::chrono::steady_clock::now();
    LOG_INFO("OfflineTranscodePipeline: started");

    demuxThread_ = std::thread(&OfflineTranscodePipeline::demuxStage, this);
    decodeThread_ = std::thread(&OfflineTranscodePipeline::decodeStage, this);
    vsrThread_ = std::thread(&OfflineTranscodePipeline::vsrStage, this);
    encodeThread_ = std::thread(&OfflineTranscodePipeline::encodeStage, this);
    muxThread_ = std::thread(&OfflineTranscodePipeline::muxStage, this);
    if (config_.audioPassthrough) {
        audioThread_ = std::thread(&OfflineTranscodePipeline::audioPassthroughStage, this);
    }

    return Result<void>::success();
}

void OfflineTranscodePipeline::cancel() {
    cancelRequested_.store(true);
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        pauseRequested_.store(false);
        pauseCv_.notify_all();
    }
    shutdownQueues();
    LOG_INFO("OfflineTranscodePipeline: cancel requested");
}

void OfflineTranscodePipeline::pause() {
    pauseRequested_.store(true);
    LOG_INFO("OfflineTranscodePipeline: pause requested");
}

void OfflineTranscodePipeline::resume() {
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        pauseRequested_.store(false);
        pauseCv_.notify_all();
    }
    LOG_INFO("OfflineTranscodePipeline: resumed");
}

Result<void> OfflineTranscodePipeline::waitUntilComplete() {
    std::unique_lock<std::mutex> lock(completionMutex_);
    completionCv_.wait(lock, [this] {
        auto s = state_.load();
        return s == State::Completed || s == State::Cancelled ||
               s == State::Error;
    });
    return completionResult_;
}

TranscodeProgress OfflineTranscodePipeline::getProgress() const {
    std::lock_guard<std::mutex> lock(progressMutex_);
    TranscodeProgress prog;
    prog.framesProcessed = framesProcessed_.load();
    prog.totalFrames = totalFrames_.load();
    prog.currentFps = currentFps_.load();

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - startTime_).count();
    if (elapsed > 0.0 && prog.framesProcessed > 0) {
        double avgFps = static_cast<double>(prog.framesProcessed) / elapsed;
        prog.estimatedSecondsLeft = (avgFps > 0.0)
            ? (static_cast<double>(prog.totalFrames - prog.framesProcessed) / avgFps)
            : 0.0;
    }

    auto s = state_.load();
    prog.isComplete = (s == State::Completed || s == State::Cancelled);
    prog.stage = stateToString(s);
    return prog;
}

OfflineTranscodePipeline::State OfflineTranscodePipeline::getState() const {
    return state_.load();
}

std::string OfflineTranscodePipeline::stateToString(State state) const {
    switch (state) {
        case State::Idle:        return "Idle";
        case State::Configured:  return "Configured";
        case State::Running:     return "Running";
        case State::Paused:      return "Paused";
        case State::Completed:   return "Completed";
        case State::Cancelled:   return "Cancelled";
        case State::Error:       return "Error";
        default:                 return "Unknown";
    }
}

// ============================================================================
// Stage 1: Demux
// ============================================================================

void OfflineTranscodePipeline::demuxStage() {
    LOG_INFO("Demux: stage started");
    try {
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_open_input(&fmtCtx, config_.inputPath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        setError(PlayerError::UnsupportedFormat,
                 "Failed to open input: " + config_.inputPath + " (" + errBuf + ")");
        return;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        setError(PlayerError::UnsupportedFormat,
                 "Failed to find stream info: " + std::string(errBuf));
        avformat_close_input(&fmtCtx);
        return;
    }

    int videoStreamIdx = -1;
    int audioStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIdx < 0) {
            videoStreamIdx = static_cast<int>(i);
        }
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIdx < 0) {
            audioStreamIdx = static_cast<int>(i);
        }
    }

    if (videoStreamIdx < 0) {
        setError(PlayerError::UnsupportedFormat, "No video stream found in input");
        avformat_close_input(&fmtCtx);
        return;
    }

    {
        int64_t nb = fmtCtx->streams[videoStreamIdx]->nb_frames;
        if (nb > 0) {
            totalFrames_.store(static_cast<uint64_t>(nb));
        } else {
            double dur = fmtCtx->duration / static_cast<double>(AV_TIME_BASE);
            AVRational fr = av_guess_frame_rate(fmtCtx, fmtCtx->streams[videoStreamIdx], nullptr);
            if (dur > 0.0 && fr.num > 0) {
                totalFrames_.store(static_cast<uint64_t>(dur * fr.num / fr.den));
            }
        }
    }

    LOG_INFO("Demux: video stream=%d, audio stream=%d, total_frames=%llu",
             videoStreamIdx, audioStreamIdx,
             static_cast<unsigned long long>(totalFrames_.load()));

    AVPacket* pkt = av_packet_alloc();
    while (shouldRun()) {
        waitIfPaused();

        ret = av_read_frame(fmtCtx, pkt);
        if (ret == AVERROR_EOF) {
            LOG_INFO("Demux: end of stream");
            break;
        }
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            LOG_WARN("Demux: read error: %s", errBuf);
            break;
        }

        int streamIdx = pkt->stream_index;
        auto mediaPkt = std::make_shared<MediaPacket>();

        mediaPkt->data.assign(pkt->data, pkt->data + pkt->size);
        mediaPkt->pts = (pkt->pts != AV_NOPTS_VALUE)
            ? pkt->pts * av_q2d(fmtCtx->streams[streamIdx]->time_base)
            : 0.0;
        mediaPkt->duration = (pkt->duration > 0)
            ? pkt->duration * av_q2d(fmtCtx->streams[streamIdx]->time_base)
            : 0.0;
        mediaPkt->keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        mediaPkt->size = pkt->size;

        if (streamIdx == videoStreamIdx) {
            mediaPkt->streamType = StreamType::Video;
            videoPacketQueue_->push(std::move(mediaPkt));
        } else if (streamIdx == audioStreamIdx && config_.audioPassthrough) {
            mediaPkt->streamType = StreamType::Audio;
            audioPacketQueue_->push(std::move(mediaPkt));
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    videoPacketQueue_->shutdown();
    audioPacketQueue_->shutdown();
    avformat_close_input(&fmtCtx);
    LOG_INFO("Demux: finished");
    } catch (const std::exception& e) {
        LOG_ERROR("Demux: EXCEPTION: {}", e.what());
        setError(PlayerError::Unknown, std::string("Demux crashed: ") + e.what());
    } catch (...) {
        LOG_ERROR("Demux: UNKNOWN EXCEPTION (possible segfault)");
        setError(PlayerError::Unknown, "Demux crashed with unknown exception");
    }
}

// ============================================================================
// Stage 2: Decode
// ============================================================================

void OfflineTranscodePipeline::decodeStage() {
    LOG_INFO("Decode: stage started");
    try {
    while (shouldRun()) {
        waitIfPaused();

        auto mediaPkt = videoPacketQueue_->pop(100);
        if (!mediaPkt) {
            if (videoPacketQueue_->isShutdown()) {
                break;
            }
            continue;
        }

        auto result = decoder_->decode(
            mediaPkt->data.data(), mediaPkt->data.size(), mediaPkt->pts);

        if (result.hasError()) {
            if (result.error() == PlayerError::NeedMoreData) {
                continue;
            }
            LOG_ERROR("Decode: frame decode failed (pts={:.3f})", mediaPkt->pts);
            setError(result.error(), "Decode failed at pts=" + std::to_string(mediaPkt->pts));
            return;
        }

        GpuFrame frame = std::move(result.value());
        frame.timestamp = mediaPkt->pts;

        if (!decodedFrameQueue_->push(std::move(frame))) {
            LOG_WARN("Decode: frame queue shutdown, stopping");
            break;
        }
    }

    auto flushResult = decoder_->flush();
    if (!flushResult.hasError()) {
        for (auto& frame : flushResult.value()) {
            if (!decodedFrameQueue_->push(std::move(frame))) {
                break;
            }
        }
    }

    decodedFrameQueue_->shutdown();
    LOG_INFO("Decode: finished");
    } catch (const std::exception& e) {
        LOG_ERROR("Decode: EXCEPTION: {}", e.what());
        setError(PlayerError::Unknown, std::string("Decode crashed: ") + e.what());
    } catch (...) {
        LOG_ERROR("Decode: UNKNOWN EXCEPTION (possible segfault)");
        setError(PlayerError::Unknown, "Decode crashed with unknown exception");
    }
}

// ============================================================================
// Stage 3: VSR
// ============================================================================

void OfflineTranscodePipeline::vsrStage() {
    LOG_INFO("VSR: stage started");
    if (vsrNode_) {
        LOG_INFO("VSR: using node '{}'", vsrNode_->nodeName());
    }
    try {
    while (shouldRun()) {
        waitIfPaused();

        GpuFrame frame;
        if (!decodedFrameQueue_->pop(frame, 100)) {
            if (decodedFrameQueue_->isShutdown()) {
                break;
            }
            continue;
        }

        if (vramManager_) {
            uint64_t frameBytes = static_cast<uint64_t>(frame.width) * frame.height * 4;
            auto allocResult = vramManager_->requestAllocation(frameBytes);
            if (allocResult.hasError()) {
                LOG_WARN("VSR: VRAM budget exceeded, skipping frame");
                vsrFrameQueue_->push(std::move(frame));
                continue;
            }
        }

        if (vsrNode_ && vsrNode_->isHealthy()) {
            auto result = vsrNode_->process(frame);
            if (result.hasError()) {
                LOG_WARN("VSR: node processing failed, passing frame through");
            } else {
                frame = std::move(result.value());
            }
        }

        if (vramManager_) {
            uint64_t frameBytes = static_cast<uint64_t>(frame.width) * frame.height * 4;
            vramManager_->release(frameBytes);
        }

        if (!vsrFrameQueue_->push(std::move(frame))) {
            LOG_WARN("VSR: output queue shutdown, stopping");
            break;
        }
    }

    vsrFrameQueue_->shutdown();
    LOG_INFO("VSR: finished");
    } catch (const std::exception& e) {
        LOG_ERROR("VSR: EXCEPTION: {}", e.what());
        setError(PlayerError::Unknown, std::string("VSR crashed: ") + e.what());
    } catch (...) {
        LOG_ERROR("VSR: UNKNOWN EXCEPTION (possible segfault)");
        setError(PlayerError::Unknown, "VSR crashed with unknown exception");
    }
}

// ============================================================================
// Stage 4: Encode
// ============================================================================

void OfflineTranscodePipeline::encodeStage() {
    LOG_INFO("Encode: stage started");
    try {
    while (shouldRun()) {
        waitIfPaused();

        GpuFrame frame;
        if (!vsrFrameQueue_->pop(frame, 100)) {
            if (vsrFrameQueue_->isShutdown()) {
                break;
            }
            continue;
        }

        auto encResult = encoder_->encode(frame);
        if (encResult.hasError()) {
            LOG_ERROR("Encode: failed (pts={:.3f})", frame.timestamp);
            setError(encResult.error(), "Encode failed at pts=" + std::to_string(frame.timestamp));
            return;
        }

        EncodedPacket pkt = std::move(encResult.value());
        if (pkt.data.empty()) {
            continue;
        }
        pkt.streamIndex = videoStreamIndex_;

        if (!encodedQueue_->push(std::move(pkt))) {
            LOG_WARN("Encode: output queue shutdown, stopping");
            break;
        }

        uint64_t processed = framesProcessed_.fetch_add(1) + 1;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime_).count();
        if (elapsed > 0.0) {
            currentFps_.store(static_cast<double>(processed) / elapsed);
        }

        if (processed % 50 == 0) {
            reportProgress();
        }

        maybeSaveCheckpoint();
    }

    auto flushResult = encoder_->flush();
    if (!flushResult.hasError()) {
        for (auto& pkt : flushResult.value()) {
            EncodedPacket ep = std::move(pkt);
            ep.streamIndex = videoStreamIndex_;
            if (!encodedQueue_->push(std::move(ep))) {
                break;
            }
            framesProcessed_.fetch_add(1);
        }
    }

    encodedQueue_->shutdown();
    LOG_INFO("Encode: finished, total frames=%llu",
             static_cast<unsigned long long>(framesProcessed_.load()));
    } catch (const std::exception& e) {
        LOG_ERROR("Encode: EXCEPTION: {}", e.what());
        setError(PlayerError::Unknown, std::string("Encode crashed: ") + e.what());
    } catch (...) {
        LOG_ERROR("Encode: UNKNOWN EXCEPTION (possible segfault)");
        setError(PlayerError::Unknown, "Encode crashed with unknown exception");
    }
}

// ============================================================================
// Stage 5: Mux
// ============================================================================

void OfflineTranscodePipeline::muxStage() {
    LOG_INFO("Mux: stage started");
    try {
    while (shouldRun()) {
        waitIfPaused();

        EncodedPacket pkt;
        if (!encodedQueue_->pop(pkt, 100)) {
            if (encodedQueue_->isShutdown()) {
                break;
            }
            continue;
        }

        auto result = muxer_->writePacket(pkt);
        if (result.hasError()) {
            LOG_ERROR("Mux: write failed");
            setError(result.error(), "Mux write failed");
            return;
        }
    }

    auto finalResult = muxer_->finalize();
    if (finalResult.hasError()) {
        LOG_ERROR("Mux: finalize failed");
        setError(finalResult.error(), "Mux finalize failed");
        return;
    }

    muxer_->close();
    LOG_INFO("Mux: finished, output=%s", config_.outputPath.c_str());

    if (!errorFlag_.load() && !cancelRequested_.load()) {
        state_.store(State::Completed);
        if (checkpointManager_) {
            checkpointManager_->cleanCheckpoint(config_.inputPath);
        }
        {
            std::lock_guard<std::mutex> lock(completionMutex_);
            completionResult_ = Result<void>::success();
        }
        completionCv_.notify_all();
        reportProgress();
    }
    } catch (const std::exception& e) {
        LOG_ERROR("Mux: EXCEPTION: {}", e.what());
        setError(PlayerError::Unknown, std::string("Mux crashed: ") + e.what());
    } catch (...) {
        LOG_ERROR("Mux: UNKNOWN EXCEPTION (possible segfault)");
        setError(PlayerError::Unknown, "Mux crashed with unknown exception");
    }
}

// ============================================================================
// Audio Passthrough
// ============================================================================

void OfflineTranscodePipeline::audioPassthroughStage() {
    while (shouldRun()) {
        waitIfPaused();

        auto mediaPkt = audioPacketQueue_->pop(100);
        if (!mediaPkt) {
            if (audioPacketQueue_->isShutdown()) {
                break;
            }
            continue;
        }

        EncodedPacket pkt;
        pkt.data = std::move(mediaPkt->data);
        pkt.pts = mediaPkt->pts;
        pkt.dts = mediaPkt->pts;
        pkt.duration = mediaPkt->duration;
        pkt.isKeyFrame = mediaPkt->keyframe;
        pkt.streamIndex = audioStreamIndex_;

        auto result = muxer_->writePacket(pkt);
        if (result.hasError()) {
            LOG_WARN("AudioPassthrough: write failed (pts=%.3f)", pkt.pts);
        }
    }

    LOG_INFO("AudioPassthrough: finished");
}

// ============================================================================
// Helpers
// ============================================================================

void OfflineTranscodePipeline::maybeSaveCheckpoint() {
    if (!checkpointManager_ || !config_.enableCheckpoint) return;

    uint64_t processed = framesProcessed_.load();
    if (processed == 0 || processed % config_.checkpointInterval != 0) return;

    CheckpointInfo info;
    info.sourcePath = config_.inputPath;
    info.outputPath = config_.outputPath;
    info.lastProcessedFrame = processed;
    info.totalFrames = totalFrames_.load();
    info.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto result = checkpointManager_->saveCheckpoint(info);
    if (result.hasError()) {
        LOG_WARN("Checkpoint save failed at frame %llu",
                 static_cast<unsigned long long>(processed));
    } else {
        LOG_INFO("Checkpoint saved at frame %llu/%llu",
                 static_cast<unsigned long long>(processed),
                 static_cast<unsigned long long>(info.totalFrames));
    }
}

void OfflineTranscodePipeline::reportProgress() {
    if (!progressCallback_) return;
    auto prog = getProgress();
    progressCallback_(prog);
}

void OfflineTranscodePipeline::setError(PlayerError err, const std::string& msg) {
    errorFlag_.store(true);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = err;
        lastErrorMsg_ = msg;
        state_.store(State::Error);
    }
    LOG_ERROR("OfflineTranscodePipeline error: %s", msg.c_str());

    if (errorCallback_) {
        errorCallback_(err, msg);
    }

    shutdownQueues();

    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        completionResult_ = Result<void>::error(err);
    }
    completionCv_.notify_all();
}

bool OfflineTranscodePipeline::shouldRun() const {
    return !cancelRequested_.load() && !errorFlag_.load();
}

void OfflineTranscodePipeline::waitIfPaused() {
    if (!pauseRequested_.load()) return;
    {
        std::unique_lock<std::mutex> lock(pauseMutex_);
        state_.store(State::Paused);
        pauseCv_.wait(lock, [this] { return !pauseRequested_.load(); });
        if (!cancelRequested_.load() && !errorFlag_.load()) {
            state_.store(State::Running);
        }
    }
}

void OfflineTranscodePipeline::shutdownQueues() {
    videoPacketQueue_->shutdown();
    audioPacketQueue_->shutdown();
    decodedFrameQueue_->shutdown();
    vsrFrameQueue_->shutdown();
    encodedQueue_->shutdown();
}

} // namespace hlplayer
