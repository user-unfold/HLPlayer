#include "hlplayer/RecordingPipeline.h"
#include "hlplayer/CameraSource.h"
#include "hlplayer/AudioEncoder.h"
#include "hlplayer/PreviewRenderer.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/time.h>
}

namespace hlplayer {

using namespace hlplayer::ffmpeg;

RecordingPipeline::RecordingPipeline() = default;

RecordingPipeline::~RecordingPipeline() {
    if (running_.load()) {
        stop();
    }
    cleanup();
}

void RecordingPipeline::setPreviewRenderer(PreviewRenderer* renderer) {
    previewRenderer_ = renderer;
}

Result<void> RecordingPipeline::start(const RecordingConfig& config, RecordingStateCallback callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (state_ != RecordingState::Idle) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_ = config;
    callback_ = std::move(callback);
    state_ = RecordingState::Error;
    frameCount_ = 0;
    startTimestamp_ = 0;
    width_ = config.resolution.width;
    height_ = config.resolution.height;
    fps_ = config.resolution.frameRate;

    if (!config.micDevicePath.empty()) {
        audioCapture_ = std::make_unique<AudioCapture>();
        audioEncoder_ = std::make_unique<AudioEncoder>();
    }

    const AVOutputFormat* outputFormat = av_guess_format("mp4", nullptr, nullptr);
    if (!outputFormat) {
        spdlog::error("RecordingPipeline: mp4 format not found");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    AVFormatContext* rawCtx = nullptr;
    int ret = avformat_alloc_output_context2(&rawCtx, outputFormat, nullptr, config.outputPath.c_str());
    if (ret < 0 || !rawCtx) {
        spdlog::error("RecordingPipeline: avformat_alloc_output_context2 failed");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }
    outputCtx_.reset(rawCtx);

    AVStream* videoStream = avformat_new_stream(outputCtx_.get(), nullptr);
    if (!videoStream) {
        spdlog::error("RecordingPipeline: failed to create video stream");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }
    videoStreamIndex_ = videoStream->index;

    if (!initVideoEncoder(width_, height_, fps_, config.videoBitrate)) {
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = avcodec_parameters_from_context(videoStream->codecpar, videoEncCtx_.get());
    if (ret < 0) {
        spdlog::error("RecordingPipeline: avcodec_parameters_from_context failed");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }
    videoStream->time_base = videoEncCtx_->time_base;

    spdlog::info("RecordingPipeline: creating output file {}", config.outputPath);

    if (audioCapture_ && audioEncoder_) {
        AVStream* audioStream = avformat_new_stream(outputCtx_.get(), nullptr);
        if (!audioStream) {
            spdlog::warn("RecordingPipeline: failed to create audio stream, recording video only");
            audioEncoder_.reset();
            audioCapture_.reset();
        } else {
            audioStreamIndex_ = audioStream->index;
            audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            audioStream->codecpar->codec_id = AV_CODEC_ID_AAC;
            audioStream->codecpar->sample_rate = config.audioSampleRate;
            audioStream->codecpar->bit_rate = config.audioBitrate;
            av_channel_layout_default(&audioStream->codecpar->ch_layout, config.audioChannels);
            audioStream->time_base = AVRational{1, config.audioSampleRate};
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
            spdlog::error("RecordingPipeline: failed to open output file: {}", errBuf);
            cleanup();
            notifyState();
            return Result<void>::error(PlayerError::Unknown);
        }
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    ret = avformat_write_header(outputCtx_.get(), &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        spdlog::error("RecordingPipeline: avformat_write_header failed");
        cleanup();
        notifyState();
        return Result<void>::error(PlayerError::Unknown);
    }

    spdlog::info("RecordingPipeline: header written, opening devices");

    camera_ = std::make_unique<CameraSource>();
    auto camResult = camera_->open(config.cameraDevicePath, width_, height_, fps_);
    if (camResult.hasError()) {
        spdlog::error("RecordingPipeline: failed to open camera");
        cleanup();
        notifyState();
        return Result<void>::error(camResult.error());
    }

    if (audioCapture_ && audioEncoder_) {
        auto audioResult = audioCapture_->open(config.micDevicePath, config.audioSampleRate, config.audioChannels);
        if (audioResult.hasError()) {
            spdlog::warn("RecordingPipeline: failed to open mic, recording without audio");
            audioCapture_.reset();
            audioEncoder_.reset();
        } else {
            auto encResult = audioEncoder_->init(config.audioSampleRate, config.audioChannels, config.audioBitrate);
            if (encResult.hasError()) {
                spdlog::warn("RecordingPipeline: failed to init audio encoder");
                audioCapture_.reset();
                audioEncoder_.reset();
            }
        }
    }

    startTimestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    running_.store(true);
    state_ = RecordingState::Recording;
    notifyState();

    videoThread_ = std::thread(&RecordingPipeline::videoThreadFunc, this);
    if (audioCapture_ && audioEncoder_) {
        audioThread_ = std::thread(&RecordingPipeline::audioThreadFunc, this);
    }

    spdlog::info("RecordingPipeline: started recording to {}", config.outputPath);
    return Result<void>::success();
}

bool RecordingPipeline::initVideoEncoder(int width, int height, int fps, int bitrate) {
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec) {
        spdlog::error("RecordingPipeline: H264 encoder not found");
        return false;
    }

    AVCodecContext* rawCtx = avcodec_alloc_context3(codec);
    if (!rawCtx) {
        spdlog::error("RecordingPipeline: failed to allocate video encoder context");
        return false;
    }
    videoEncCtx_.reset(rawCtx);

    rawCtx->bit_rate = bitrate;
    rawCtx->width = width;
    rawCtx->height = height;
    rawCtx->time_base = AVRational{1, fps};
    rawCtx->framerate = AVRational{fps, 1};
    rawCtx->gop_size = fps * 2;
    rawCtx->max_b_frames = 2;
    rawCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    rawCtx->thread_count = 2;

    av_opt_set(rawCtx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(rawCtx->priv_data, "tune", "zerolatency", 0);

    int     ret = avcodec_open2(rawCtx, codec, nullptr);
    if (ret < 0) {
        spdlog::error("RecordingPipeline: avcodec_open2 for video encoder failed");
        videoEncCtx_.reset();
        return false;
    }

    yuvFrame_ = makeAVFrame();
    yuvFrame_->format = AV_PIX_FMT_YUV420P;
    yuvFrame_->width = width;
    yuvFrame_->height = height;
    ret = av_frame_get_buffer(yuvFrame_.get(), 0);
    if (ret < 0) {
        spdlog::error("RecordingPipeline: av_frame_get_buffer failed for yuvFrame");
        videoEncCtx_.reset();
        return false;
    }

    spdlog::info("RecordingPipeline: video encoder initialized {}x{}@{}fps bitrate={}",
                 width, height, fps, bitrate);
    return true;
}

void RecordingPipeline::videoThreadFunc() {
    spdlog::info("RecordingPipeline: video thread started");
    int64_t frameIdx = 0;
    AVPixelFormat srcFmt = AV_PIX_FMT_YUV420P;

    while (running_.load()) {
        auto result = camera_->readFrame();
        if (result.hasError()) {
            if (running_.load()) {
                spdlog::error("RecordingPipeline: camera readFrame error");
            }
            break;
        }

        const AVFrame* srcFrame = camera_->getFrame();
        if (!srcFrame) continue;

        if (previewRenderer_) {
            previewRenderer_->onFrame(srcFrame->data[0], srcFrame->width, srcFrame->height, srcFrame->linesize[0]);
        }

        if (frameIdx == 0) {
            srcFmt = static_cast<AVPixelFormat>(srcFrame->format);
            if (srcFmt != AV_PIX_FMT_YUV420P) {
                swsCtx_.reset(sws_getContext(srcFrame->width, srcFrame->height, srcFmt,
                                              width_, height_, AV_PIX_FMT_YUV420P,
                                              SWS_BILINEAR, nullptr, nullptr, nullptr));
            }
        }

        av_frame_make_writable(yuvFrame_.get());

        if (swsCtx_ && srcFmt != AV_PIX_FMT_YUV420P) {
            sws_scale(swsCtx_.get(), srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
                      yuvFrame_->data, yuvFrame_->linesize);
        } else {
            int copyRet = av_frame_copy(yuvFrame_.get(), srcFrame);
            if (copyRet < 0) continue;
        }

        yuvFrame_->pts = frameIdx++;

        int ret = avcodec_send_frame(videoEncCtx_.get(), yuvFrame_.get());
        if (ret < 0) {
            if (running_.load()) {
                spdlog::error("RecordingPipeline: avcodec_send_frame failed (video)");
            }
            break;
        }

        while (running_.load()) {
            AVPacketPtr pkt = makeAVPacket();
            ret = avcodec_receive_packet(videoEncCtx_.get(), pkt.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                spdlog::error("RecordingPipeline: avcodec_receive_packet failed (video)");
                break;
            }

            av_packet_rescale_ts(pkt.get(), videoEncCtx_->time_base,
                                 outputCtx_->streams[videoStreamIndex_]->time_base);
            pkt->stream_index = videoStreamIndex_;

            int writeRet = av_interleaved_write_frame(outputCtx_.get(), pkt.get());
            if (writeRet < 0) {
                spdlog::error("RecordingPipeline: av_interleaved_write_frame failed (video)");
            }

            if (frameCount_ == 0) {
                spdlog::info("RecordingPipeline: first video frame written");
            }
            frameCount_++;
            updateStats();
        }
    }

    avcodec_send_frame(videoEncCtx_.get(), nullptr);
    while (true) {
        AVPacketPtr pkt = makeAVPacket();
        int ret = avcodec_receive_packet(videoEncCtx_.get(), pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        av_packet_rescale_ts(pkt.get(), videoEncCtx_->time_base,
                             outputCtx_->streams[videoStreamIndex_]->time_base);
        pkt->stream_index = videoStreamIndex_;
        av_interleaved_write_frame(outputCtx_.get(), pkt.get());
    }

    spdlog::info("RecordingPipeline: video thread exited, {} frames encoded", frameCount_.load());
}

void RecordingPipeline::audioThreadFunc() {
    while (running_.load()) {
        auto result = audioCapture_->readFrame();
        if (result.hasError()) {
            if (running_.load()) {
                spdlog::error("RecordingPipeline: audio capture readFrame error");
            }
            break;
        }

        const AVFrame* frame = audioCapture_->getFrame();
        if (!frame || frame->nb_samples <= 0) continue;

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
            AVPacketPtr pkt = makeAVPacket();
            pkt->data = const_cast<uint8_t*>(encPkt.data.data());
            pkt->size = static_cast<int>(encPkt.data.size());
            pkt->stream_index = audioStreamIndex_;

            double tb = av_q2d(outputCtx_->streams[audioStreamIndex_]->time_base);
            pkt->pts = static_cast<int64_t>(encPkt.pts / tb);
            pkt->dts = static_cast<int64_t>(encPkt.dts / tb);
            pkt->duration = static_cast<int>(encPkt.duration / tb);

            if (encPkt.isKeyFrame) {
                pkt->flags |= AV_PKT_FLAG_KEY;
            }

            av_interleaved_write_frame(outputCtx_.get(), pkt.get());
        }
    }

    auto flushResult = audioEncoder_->flush();
    if (!flushResult.hasError()) {
        for (const auto& encPkt : flushResult.value()) {
            AVPacketPtr pkt = makeAVPacket();
            pkt->data = const_cast<uint8_t*>(encPkt.data.data());
            pkt->size = static_cast<int>(encPkt.data.size());
            pkt->stream_index = audioStreamIndex_;

            double tb = av_q2d(outputCtx_->streams[audioStreamIndex_]->time_base);
            pkt->pts = static_cast<int64_t>(encPkt.pts / tb);
            pkt->dts = static_cast<int64_t>(encPkt.dts / tb);
            pkt->duration = static_cast<int>(encPkt.duration / tb);

            if (encPkt.isKeyFrame) {
                pkt->flags |= AV_PKT_FLAG_KEY;
            }

            av_interleaved_write_frame(outputCtx_.get(), pkt.get());
        }
    }

    spdlog::info("RecordingPipeline: audio thread exited");
}

Result<void> RecordingPipeline::stop() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ != RecordingState::Recording) {
            return Result<void>::error(PlayerError::InvalidState);
        }
        state_ = RecordingState::Stopping;
    }
    notifyState();

    running_.store(false);

    if (videoThread_.joinable()) {
        videoThread_.join();
    }
    if (audioThread_.joinable()) {
        audioThread_.join();
    }

    if (videoEncCtx_) {
        avcodec_send_frame(videoEncCtx_.get(), nullptr);
        AVPacket* flushPkt = av_packet_alloc();
        while (avcodec_receive_packet(videoEncCtx_.get(), flushPkt) >= 0) {
            av_packet_rescale_ts(flushPkt, videoEncCtx_->time_base, outputCtx_->streams[videoStreamIndex_]->time_base);
            flushPkt->stream_index = videoStreamIndex_;
            av_interleaved_write_frame(outputCtx_.get(), flushPkt);
            av_packet_unref(flushPkt);
        }
        av_packet_free(&flushPkt);
    }

    if (outputCtx_) {
        av_write_trailer(outputCtx_.get());
    }

    cleanup();

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = RecordingState::Idle;
    }
    notifyState();

    spdlog::info("RecordingPipeline: stopped, {} frames recorded", frameCount_.load());
    return Result<void>::success();
}

RecordingStats RecordingPipeline::getStats() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    RecordingStats s = stats_;
    s.state = state_;
    return s;
}

RecordingState RecordingPipeline::getState() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

void RecordingPipeline::updateStats() {
    if (startTimestamp_ <= 0) return;

    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    double duration = static_cast<double>(now - startTimestamp_) / 1000000.0;

    std::lock_guard<std::mutex> lock(stateMutex_);
    stats_.durationSeconds = duration;
    stats_.currentFps = (duration > 0.0) ? static_cast<double>(frameCount_.load()) / duration : 0.0;
    stats_.state = state_;

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(config_.outputPath, ec);
    if (!ec) {
        stats_.fileSizeBytes = static_cast<int64_t>(fileSize);
    }
}

void RecordingPipeline::notifyState() {
    if (callback_) {
        RecordingStats s;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            s = stats_;
            s.state = state_;
        }
        callback_(state_, s);
    }
}

void RecordingPipeline::cleanup() {
    running_.store(false);

    if (videoThread_.joinable()) videoThread_.join();
    if (audioThread_.joinable()) audioThread_.join();

    yuvFrame_.reset();
    swsCtx_.reset();
    videoEncCtx_.reset();

    if (outputCtx_) {
        if (outputCtx_->pb) {
            avio_closep(&outputCtx_->pb);
        }
        outputCtx_.reset();
    }

    audioEncoder_.reset();
    audioCapture_.reset();
    camera_.reset();

    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
}

} // namespace hlplayer
