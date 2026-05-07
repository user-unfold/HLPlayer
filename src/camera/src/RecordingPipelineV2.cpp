#include "hlplayer/RecordingPipelineV2.h"
#include "hlplayer/CameraSource.h"
#include "hlplayer/AudioEncoder.h"
#include "hlplayer/PreviewRenderer.h"
#include "hlplayer/HWVideoEncoder.h"
#include "hlplayer/HWEncoderDetector.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}
#include <filesystem>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
}

namespace hlplayer {

RecordingPipelineV2::RecordingPipelineV2() = default;

RecordingPipelineV2::~RecordingPipelineV2() {
    if (running_.load()) {
        stop();
    }
    cleanup();
}

void RecordingPipelineV2::setPreviewRenderer(PreviewRenderer* renderer) {
    previewRenderer_ = renderer;
}

Result<void> RecordingPipelineV2::start(const RecordingConfig& config, StateCallbackV2 callback) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (state_ != RecordingState::Idle) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_ = config;
    callback_ = std::move(callback);
    state_ = RecordingState::Error;
    frameCount_ = 0;
    startTimestamp_ = 0;
    totalPausedUs_ = 0;
    paused_.store(false);
    width_ = config.resolution.width;
    height_ = config.resolution.height;
    fps_ = config.resolution.frameRate;
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;

    encoderInfo_ = HWEncoderDetector::detectBest();
    if (!encoderInfo_.available) {
        spdlog::warn("RecordingPipelineV2: no hardware encoder found, using software fallback");
    }

    videoEncoder_ = std::make_unique<HWVideoEncoder>();
    HWEncodeConfig hwConfig;
    hwConfig.width = width_;
    hwConfig.height = height_;
    hwConfig.fps = fps_;
    hwConfig.bitrate = config.videoBitrate;
    hwConfig.gopSize = fps_ * 2;
    hwConfig.maxBFrames = (config.outputMode == RecordingConfig::OutputMode::File) ? 2 : 0;
    hwConfig.encoderInfo = encoderInfo_;
    auto encResult = videoEncoder_->init(hwConfig);
    if (encResult.hasError()) {
        spdlog::error("RecordingPipelineV2: video encoder init failed");
        cleanup();
        notifyState();
        return Result<void>::error(encResult.error());
    }

    const AVOutputFormat* outputFormat = av_guess_format("mp4", nullptr, nullptr);
    if (!outputFormat) {
        spdlog::error("RecordingPipelineV2: mp4 format not found");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    int ret = avformat_alloc_output_context2(&outputCtx_, outputFormat, nullptr, config.outputPath.c_str());
    if (ret < 0 || !outputCtx_) {
        spdlog::error("RecordingPipelineV2: avformat_alloc_output_context2 failed");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }

    if (!config.micDevicePath.empty()) {
        audioCapture_ = std::make_unique<AudioCapture>();
        audioEncoder_ = std::make_unique<AudioEncoder>();

        auto audioEncResult = audioEncoder_->init(config.audioSampleRate, config.audioChannels, config.audioBitrate);
        if (audioEncResult.hasError()) {
            spdlog::warn("RecordingPipelineV2: failed to init audio encoder");
            audioEncoder_.reset();
            audioCapture_.reset();
        }
    }

    auto primeResult = videoEncoder_->ensureExtradata();
    if (primeResult.hasError()) {
        spdlog::error("RecordingPipelineV2: failed to prime video encoder extradata");
    }

    AVStream* videoStream = avformat_new_stream(outputCtx_, nullptr);
    if (!videoStream) {
        spdlog::error("RecordingPipelineV2: failed to create video stream");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }
    videoStreamIndex_ = videoStream->index;

    const AVCodecContext* encCtx = videoEncoder_->context();
    ret = avcodec_parameters_from_context(videoStream->codecpar, encCtx);
    if (ret < 0) {
        spdlog::error("RecordingPipelineV2: avcodec_parameters_from_context failed");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }
    videoStream->time_base = encCtx->time_base;

    if (audioEncoder_) {
        AVStream* audioStream = avformat_new_stream(outputCtx_, nullptr);
        if (!audioStream) {
            spdlog::warn("RecordingPipelineV2: failed to create audio stream, recording video only");
            audioEncoder_.reset();
            audioCapture_.reset();
        } else {
            audioStreamIndex_ = audioStream->index;
            const AVCodecContext* audioEncCtx = audioEncoder_->context();
            avcodec_parameters_from_context(audioStream->codecpar, audioEncCtx);
            audioStream->time_base = audioEncCtx->time_base;
        }
    }

    if (!(outputCtx_->oformat->flags & AVFMT_NOFILE)) {
        std::filesystem::path outPath(config.outputPath);
        std::error_code ec;
        if (!outPath.parent_path().empty()) {
            std::filesystem::create_directories(outPath.parent_path(), ec);
        }

        ret = avio_open2(&outputCtx_->pb, config.outputPath.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
        if (ret < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuf, sizeof(errBuf));
            spdlog::error("RecordingPipelineV2: failed to open output file: {}", errBuf);
            cleanup();
            notifyState();
            return Result<void>::error(PlayerError::Unknown);
        }
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    ret = avformat_write_header(outputCtx_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        spdlog::error("RecordingPipelineV2: avformat_write_header failed");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }
    headerWritten_ = true;
    trailerWritten_ = false;

    // --- Stream output setup (RTMP/SRT) ---
    bool useStream = (config.outputMode == RecordingConfig::OutputMode::Stream ||
                      config.outputMode == RecordingConfig::OutputMode::Both);
    if (useStream && !config.streamUrl.empty()) {
        spdlog::info("RecordingPipelineV2: attempting stream connection to {}", config.streamUrl);
        streamMuxer_ = std::make_unique<StreamMuxer>();
        std::string streamUrl = config.streamUrl;
        auto streamResult = streamMuxer_->open(streamUrl, config.streamProtocol);
        spdlog::info("RecordingPipelineV2: stream open result: {}", streamResult.hasError() ? "FAILED" : "OK");
        if (streamResult.hasError()) {
            spdlog::error("RecordingPipelineV2: failed to open stream to {}", streamUrl);
            streamMuxer_.reset();
            if (config.outputMode == RecordingConfig::OutputMode::Stream) {
                // Stream-only mode: nothing to record, fail
                cleanup();
                notifyState();
                return Result<void>::error(PlayerError::NetworkError);
            }
            // Both mode: continue with file-only recording
            spdlog::warn("RecordingPipelineV2: stream failed, continuing with file-only recording");
        } else {
            const AVCodecContext* vctx = videoEncoder_->context();

            // Log encoder extradata for debugging SPS/PPS issues
            spdlog::info("RTMP video: encoder extradata_size={}, profile={}, level={}, pix_fmt={}",
                         vctx->extradata_size, vctx->profile, vctx->level, static_cast<int>(vctx->pix_fmt));
            if (vctx->extradata && vctx->extradata_size > 0) {
                std::string hex;
                int n = std::min(vctx->extradata_size, 32);
                for (int i = 0; i < n; ++i) {
                    char buf[4];
                    snprintf(buf, sizeof(buf), "%02x ", vctx->extradata[i]);
                    hex += buf;
                }
                spdlog::info("RTMP video: encoder extradata ({} bytes): {}", vctx->extradata_size, hex);
            }

            // Use encoder's codec params directly (not from MP4 output which may
            // have been modified by avformat_write_header).  SRS fails to parse
            // the AVC sequence header when the extradata is corrupted or altered.
            AVCodecParameters* videoParams = avcodec_parameters_alloc();
            avcodec_parameters_from_context(videoParams, vctx);
            auto vAddResult = streamMuxer_->addStream(vctx->codec_id, vctx->time_base, videoParams);

            // Log what addStream actually received
            spdlog::info("RTMP video: addStream codecpar extradata_size={}, codec_tag={}",
                         videoParams->extradata_size, videoParams->codec_tag);
            avcodec_parameters_free(&videoParams);

            if (vAddResult.hasError()) {
                spdlog::error("RecordingPipelineV2: failed to add video stream to muxer");
                streamMuxer_.reset();
            } else if (audioEncoder_) {
                const AVCodecContext* audioEncCtx = audioEncoder_->context();
                AVRational audioTb = audioEncCtx->time_base;

                // Also use encoder params directly for audio
                AVCodecParameters* audioParams = avcodec_parameters_alloc();
                avcodec_parameters_from_context(audioParams, audioEncCtx);
                auto aAddResult = streamMuxer_->addStream(AV_CODEC_ID_AAC, audioTb, audioParams);
                spdlog::info("RTMP audio: addStream codecpar extradata_size={}", audioParams->extradata_size);
                avcodec_parameters_free(&audioParams);

                if (aAddResult.hasError()) {
                    spdlog::warn("RecordingPipelineV2: failed to add audio stream, continuing video-only");
                }
            }

            if (streamMuxer_) {
                spdlog::info("RecordingPipelineV2: stream output opened to {} ({})",
                             streamUrl, config.streamProtocol == StreamingProtocol::RTMP ? "RTMP" : "SRT");
            }
        }
    }

    frameQueue_ = std::make_unique<RecordingFrameQueue>();

    spdlog::info("RecordingPipelineV2: opening camera...");
    camera_ = std::make_unique<CameraSource>();
    auto camResult = camera_->open(config.cameraDevicePath, width_, height_, fps_);
    spdlog::info("RecordingPipelineV2: camera open result: {}", camResult.hasError() ? "FAILED" : "OK");
    if (camResult.hasError()) {
        spdlog::error("RecordingPipelineV2: failed to open camera");
        cleanup();
        notifyState();
        return Result<void>::error(camResult.error());
    }

    if (audioCapture_ && audioEncoder_) {
        auto audioResult = audioCapture_->open(config.micDevicePath, config.audioSampleRate, config.audioChannels);
        if (audioResult.hasError()) {
            spdlog::warn("RecordingPipelineV2: failed to open mic, recording without audio");
            audioCapture_.reset();
            audioEncoder_.reset();
        }
    }

    startTimestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    startTime_ = std::chrono::steady_clock::now();
    running_.store(true);
    audioRunning_.store(true);

    captureThread_ = std::thread(&RecordingPipelineV2::captureThreadFunc, this);
    encodeThread_ = std::thread(&RecordingPipelineV2::encodeThreadFunc, this);
    if (audioCapture_ && audioEncoder_) {
        audioThread_ = std::thread(&RecordingPipelineV2::audioThreadFunc, this);
    }

    state_ = RecordingState::Recording;
    spdlog::info("RecordingPipelineV2: about to notify state (threads={}/{}/{})",
                 captureThread_.joinable(), encodeThread_.joinable(),
                 audioThread_.joinable());
    notifyState();

    spdlog::info("RecordingPipelineV2: started recording to {}", config.outputPath);
    return Result<void>::success();
}

Result<void> RecordingPipelineV2::stop() {
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex_);
        if (state_ != RecordingState::Recording && state_ != RecordingState::Paused) {
            return Result<void>::error(PlayerError::InvalidState);
        }
        state_ = RecordingState::Stopping;
    }
    notifyState();

    paused_.store(false);
    running_.store(false);
    audioRunning_.store(false);

    camera_->abort();
    frameQueue_->shutdown();
    if (audioCapture_) {
        audioCapture_->abort();
    }

    // Give av_read_frame a moment to detect the interrupt callbacks
    // before joining threads (avoids blocking on stuck I/O).
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (encodeThread_.joinable()) {
        encodeThread_.join();
    }
    if (audioThread_.joinable()) {
        audioThread_.join();
    }

    if (outputCtx_ && headerWritten_ && !trailerWritten_) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        av_write_trailer(outputCtx_);
        trailerWritten_ = true;
    }

    if (streamMuxer_) {
        streamMuxer_->close();
    }

    cleanup();

    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex_);
        state_ = RecordingState::Idle;
    }
    notifyState();

    spdlog::info("RecordingPipelineV2: stopped, {} frames recorded", frameCount_.load());
    return Result<void>::success();
}

Result<void> RecordingPipelineV2::pause() {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (state_ != RecordingState::Recording) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    paused_.store(true);
    pauseStartTime_ = std::chrono::steady_clock::now();
    state_ = RecordingState::Paused;
    spdlog::info("RecordingPipelineV2: recording paused");
    notifyState();
    return Result<void>::success();
}

Result<void> RecordingPipelineV2::resume() {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    if (state_ != RecordingState::Paused) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    auto pauseDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - pauseStartTime_).count();
    totalPausedUs_ += pauseDuration;

    paused_.store(false);
    state_ = RecordingState::Recording;
    spdlog::info("RecordingPipelineV2: recording resumed (paused for {} ms)", pauseDuration / 1000);
    notifyState();
    return Result<void>::success();
}

RecordingStats RecordingPipelineV2::getStats() const {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    RecordingStats s;
    s.state = state_;
    s.durationSeconds = 0.0;
    s.currentFps = 0.0;
    s.fileSizeBytes = 0;
    if (startTimestamp_ > 0) {
        int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t currentPauseUs = 0;
        if (paused_.load()) {
            currentPauseUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - pauseStartTime_).count();
        }
        double duration = static_cast<double>(now - startTimestamp_ - totalPausedUs_ - currentPauseUs) / 1000000.0;
        s.durationSeconds = duration;
        s.currentFps = (duration > 0.0) ? static_cast<double>(frameCount_.load()) / duration : 0.0;
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(config_.outputPath, ec);
        if (!ec) {
            s.fileSizeBytes = static_cast<int64_t>(fileSize);
        }
    }
    return s;
}

RecordingState RecordingPipelineV2::getState() const {
    return state_.load();
}

void RecordingPipelineV2::captureThreadFunc() {
    spdlog::info("RecordingPipelineV2: capture thread started");

    int64_t frameIdx = 0;
    AVPixelFormat srcFmt = AV_PIX_FMT_YUV420P;
    SwsContext* swsCtx = nullptr;
    AVFrame* yuvFrame = av_frame_alloc();
    if (!yuvFrame) {
        spdlog::error("RecordingPipelineV2: av_frame_alloc failed");
        return;
    }
    yuvFrame->format = AV_PIX_FMT_YUV420P;
    yuvFrame->width = width_;
    yuvFrame->height = height_;
    if (av_frame_get_buffer(yuvFrame, 0) < 0) {
        spdlog::error("RecordingPipelineV2: av_frame_get_buffer failed");
        av_frame_free(&yuvFrame);
        return;
    }

    using Clock = std::chrono::high_resolution_clock;
    double accReadUs = 0, accPreviewUs = 0, accConvertUs = 0, accPushUs = 0;
    int statCount = 0;

    while (running_.load()) {
        auto t0 = Clock::now();
        auto result = camera_->readFrame();
        auto t1 = Clock::now();
        accReadUs += std::chrono::duration<double, std::micro>(t1 - t0).count();

        if (result.hasError()) {
            if (running_.load()) {
                spdlog::error("RecordingPipelineV2: camera readFrame error");
            }
            break;
        }

        const AVFrame* srcFrame = camera_->getFrame();
        if (!srcFrame) continue;

        // While paused, still feed preview but skip encoding
        if (paused_.load()) {
            if (previewRenderer_ && srcFrame->data[0]) {
                previewRenderer_->onFrame(srcFrame->data[0], srcFrame->width, srcFrame->height, srcFrame->linesize[0]);
            }
            continue;
        }

        if (frameIdx == 0) {
            srcFmt = static_cast<AVPixelFormat>(srcFrame->format);
            spdlog::info("RecordingPipelineV2: source pixel format = {} ({})",
                         static_cast<int>(srcFmt), av_get_pix_fmt_name(srcFmt));
            if (srcFmt != AV_PIX_FMT_YUV420P) {
                swsCtx = sws_getContext(srcFrame->width, srcFrame->height, srcFmt,
                                         width_, height_, AV_PIX_FMT_YUV420P,
                                         SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                if (!swsCtx) {
                    spdlog::error("RecordingPipelineV2: sws_getContext failed for {}→yuv420p",
                                  av_get_pix_fmt_name(srcFmt));
                    break;
                }
            }
            if (previewRenderer_)
                previewRenderer_->setSourceFormat(static_cast<int>(srcFmt));
        }

        auto tp0 = Clock::now();
        if (previewRenderer_ && srcFrame->data[0]) {
            bool paused = paused_.load();
            bool previewFrame = paused || (frameIdx % 6 == 0);
            if (previewFrame) {
                previewRenderer_->onFrame(srcFrame->data[0], srcFrame->width, srcFrame->height, srcFrame->linesize[0]);
            }
        }
        auto tp1 = Clock::now();
        accPreviewUs += std::chrono::duration<double, std::micro>(tp1 - tp0).count();

        auto tc0 = Clock::now();
        AVFrame* frameToQueue = yuvFrame;
        if (av_frame_make_writable(yuvFrame) < 0) {
            spdlog::error("RecordingPipelineV2: av_frame_make_writable failed");
            break;
        }
        yuvFrame->format = AV_PIX_FMT_YUV420P;
        yuvFrame->width = width_;
        yuvFrame->height = height_;
        if (swsCtx && srcFmt != AV_PIX_FMT_YUV420P) {
            sws_scale(swsCtx, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
                      yuvFrame->data, yuvFrame->linesize);
        } else if (srcFmt == AV_PIX_FMT_YUV420P) {
            av_frame_copy(yuvFrame, srcFrame);
        }
        yuvFrame->pts = frameIdx++;
        auto tc1 = Clock::now();
        accConvertUs += std::chrono::duration<double, std::micro>(tc1 - tc0).count();

        if (!frameQueue_->push(frameToQueue)) break;
        auto tEnd = Clock::now();
        accPushUs += std::chrono::duration<double, std::micro>(tEnd - tc1).count();

        if (++statCount % 30 == 0) {
            spdlog::info("capture stats [{} frames]: read={:.0f}us  preview={:.0f}us  convert={:.0f}us  push={:.0f}us  total={:.0f}us",
                         statCount, accReadUs/30, accPreviewUs/30, accConvertUs/30, accPushUs/30,
                         (accReadUs+accPreviewUs+accConvertUs+accPushUs)/30);
            accReadUs = accPreviewUs = accConvertUs = accPushUs = 0;
        }
    }

    if (swsCtx) sws_freeContext(swsCtx);
    av_frame_free(&yuvFrame);
    spdlog::info("RecordingPipelineV2: capture thread exited");
}

void RecordingPipelineV2::encodeThreadFunc() {
    while (running_.load()) {
        AVFrame* frame = frameQueue_->pop();
        if (!frame) break;

        auto encodeResult = videoEncoder_->encode(frame);
        static int encLogCounter = 0;
        if (++encLogCounter % 30 == 1)
            spdlog::info("RecordingPipelineV2: encoded {} frames (latest pts={})", encLogCounter, frame->pts);
        av_frame_free(&frame);
        if (encodeResult.hasError()) {
            static int encErrCounter = 0;
            if (++encErrCounter % 30 == 1)
                spdlog::error("RecordingPipelineV2: video encode failed ({} errors so far)", encErrCounter);
            continue;
        }

        for (const auto& encPkt : encodeResult.value()) {
            EncodedPacket encPktCopy = encPkt;
            encPktCopy.streamIndex = static_cast<uint32_t>(videoStreamIndex_);
            writeVideoPacket(encPktCopy);
        }

        frameCount_++;
    }

    auto flushResult = videoEncoder_->flush();
    if (!flushResult.hasError()) {
        for (const auto& encPkt : flushResult.value()) {
            EncodedPacket encPktCopy = encPkt;
            encPktCopy.streamIndex = static_cast<uint32_t>(videoStreamIndex_);
            writeVideoPacket(encPktCopy);
        }
    }

    spdlog::info("RecordingPipelineV2: encode thread exited, {} frames encoded", frameCount_.load());
}

void RecordingPipelineV2::audioThreadFunc() {
    spdlog::info("RecordingPipelineV2: audio thread started");

    while (audioRunning_.load()) {
        auto result = audioCapture_->readFrame();
        if (result.hasError()) {
            if (audioRunning_.load()) {
                spdlog::error("RecordingPipelineV2: audio capture readFrame error");
            }
            break;
        }

        const AVFrame* frame = audioCapture_->getFrame();
        if (!frame || frame->nb_samples <= 0) continue;

        if (paused_.load()) continue;

        int channels = frame->ch_layout.nb_channels;
        int nbSamples = frame->nb_samples;
        std::vector<uint8_t> pcmData(nbSamples * channels * sizeof(int16_t));

        if (frame->format == AV_SAMPLE_FMT_FLTP) {
            auto* dst = reinterpret_cast<int16_t*>(pcmData.data());
            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<const float*>(frame->data[ch]);
                for (int i = 0; i < nbSamples; ++i) {
                    dst[i * channels + ch] = static_cast<int16_t>(
                        std::clamp(static_cast<int>(src[i] * 32767.0f), -32768, 32767));
                }
            }
        } else if (frame->format == AV_SAMPLE_FMT_S16) {
            std::memcpy(pcmData.data(), frame->data[0], pcmData.size());
        } else if (frame->format == AV_SAMPLE_FMT_S16P) {
            auto* dst = reinterpret_cast<int16_t*>(pcmData.data());
            for (int ch = 0; ch < channels; ++ch) {
                const int16_t* src = reinterpret_cast<const int16_t*>(frame->data[ch]);
                for (int i = 0; i < nbSamples; ++i) {
                    dst[i * channels + ch] = src[i];
                }
            }
        } else {
            continue;
        }

        auto encResult = audioEncoder_->encode(pcmData.data(), nbSamples);
        if (encResult.hasError()) continue;

        for (const auto& encPkt : encResult.value()) {
            EncodedPacket encPktCopy = encPkt;
            encPktCopy.streamIndex = static_cast<uint32_t>(audioStreamIndex_);
            writeAudioPacket(encPktCopy);
        }
    }

    auto flushResult = audioEncoder_->flush();
    if (!flushResult.hasError()) {
        for (const auto& encPkt : flushResult.value()) {
            EncodedPacket encPktCopy = encPkt;
            encPktCopy.streamIndex = static_cast<uint32_t>(audioStreamIndex_);
            writeAudioPacket(encPktCopy);
        }
    }

    spdlog::info("RecordingPipelineV2: audio thread exited");
}

void RecordingPipelineV2::writeVideoPacket(const EncodedPacket& encPkt) {
    bool useFile = (config_.outputMode == RecordingConfig::OutputMode::File ||
                    config_.outputMode == RecordingConfig::OutputMode::Both);
    bool useStream = (config_.outputMode == RecordingConfig::OutputMode::Stream ||
                      config_.outputMode == RecordingConfig::OutputMode::Both);

    if (useFile) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) return;
        av_new_packet(pkt, static_cast<int>(encPkt.data.size()));
        memcpy(pkt->data, encPkt.data.data(), encPkt.data.size());
        pkt->stream_index = videoStreamIndex_;
        pkt->pts = encPkt.pts;
        pkt->dts = encPkt.dts;
        pkt->duration = encPkt.duration;
        if (encPkt.isKeyFrame) pkt->flags |= AV_PKT_FLAG_KEY;
        {
            std::lock_guard<std::mutex> lock(writeMutex_);
            av_interleaved_write_frame(outputCtx_, pkt);
        }
        av_packet_free(&pkt);
    }

    if (useStream && streamMuxer_) {
        streamMuxer_->writePacket(encPkt);
    }
}

void RecordingPipelineV2::writeAudioPacket(const EncodedPacket& encPkt) {
    bool useFile = (config_.outputMode == RecordingConfig::OutputMode::File ||
                    config_.outputMode == RecordingConfig::OutputMode::Both);
    bool useStream = (config_.outputMode == RecordingConfig::OutputMode::Stream ||
                      config_.outputMode == RecordingConfig::OutputMode::Both);

    if (useFile) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) return;
        av_new_packet(pkt, static_cast<int>(encPkt.data.size()));
        memcpy(pkt->data, encPkt.data.data(), encPkt.data.size());
        pkt->stream_index = audioStreamIndex_;
        pkt->pts = encPkt.pts;
        pkt->dts = encPkt.dts;
        pkt->duration = encPkt.duration;
        if (encPkt.isKeyFrame) pkt->flags |= AV_PKT_FLAG_KEY;
        {
            std::lock_guard<std::mutex> lock(writeMutex_);
            av_interleaved_write_frame(outputCtx_, pkt);
        }
        av_packet_free(&pkt);
    }

    if (useStream && streamMuxer_) {
        streamMuxer_->writePacket(encPkt);
    }
}

void RecordingPipelineV2::notifyState() {
    if (callback_) {
        RecordingStats s = getStats();
        callback_(state_.load(), s);
    }
}

void RecordingPipelineV2::cleanup() {
    running_.store(false);
    audioRunning_.store(false);

    if (camera_) camera_->abort();
    if (frameQueue_) frameQueue_->shutdown();

    if (captureThread_.joinable()) captureThread_.join();
    if (encodeThread_.joinable()) encodeThread_.join();
    if (audioThread_.joinable()) audioThread_.join();

    if (outputCtx_) {
        if (headerWritten_ && !trailerWritten_) {
            std::lock_guard<std::mutex> lock(writeMutex_);
            av_write_trailer(outputCtx_);
            trailerWritten_ = true;
            spdlog::info("RecordingPipelineV2: trailer written in cleanup (safety net)");
        }
        if (outputCtx_->pb) {
            avio_closep(&outputCtx_->pb);
        }
        avformat_free_context(outputCtx_);
        outputCtx_ = nullptr;
    }
    headerWritten_ = false;

    videoEncoder_.reset();
    audioEncoder_.reset();
    audioCapture_.reset();
    camera_.reset();
    frameQueue_.reset();
    streamMuxer_.reset();

    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
}

} // namespace hlplayer
