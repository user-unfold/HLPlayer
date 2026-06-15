#include "FFmpegDemuxer.h"

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "HlvHeader.h"
#include "KeyManager.h"
#include "DecryptingAVIOContext.h"
#include "SessionKeyCache.h"
#include "constant_time.h"
#include "HmacSha256.h"

extern "C" {
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

namespace hlplayer {
namespace extractor {

using namespace hlplayer::ffmpeg;

FFmpegDemuxer::FFmpegDemuxer() {
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();
    spdlog::info("FFmpegDemuxer created, FFmpeg version: {}", av_version_info());
}

FFmpegDemuxer::~FFmpegDemuxer() {
    stop();
    avformat_network_deinit();
    spdlog::info("FFmpegDemuxer destroyed");
}

Result<void> FFmpegDemuxer::open(const std::string& url,
                                  const DemuxerConfig& config,
                                  DemuxerCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != PlayerState_Idle) {
        spdlog::error("Cannot open demuxer: not in Idle state (state={})", static_cast<int>(state_.load()));
        return Result<void>::error(PlayerError::InvalidState);
    }

    config_ = config;
    callbacks_ = std::move(callbacks);
    shouldStop_.store(false);

    spdlog::info("Opening demuxer for URL: {}", url);

    AVDictionaryPtr options = createFFmpegOptions();

    AVFormatContext* rawFormatCtx = avformat_alloc_context();
    if (!rawFormatCtx) {
        spdlog::error("Failed to allocate AVFormatContext");
        return Result<void>::error(PlayerError::DecodeError);
    }

    rawFormatCtx->interrupt_callback.opaque = this;
    rawFormatCtx->interrupt_callback.callback = [](void* opaque) -> int {
        auto* demuxer = static_cast<FFmpegDemuxer*>(opaque);
        // Only check shouldStop_ here. Do NOT check seekRequested_ because
        // avformat_seek_file() performs I/O internally, and if the callback
        // returns 1 during that I/O, the seek is aborted mid-operation,
        // leaving the format context in a corrupt state.
        return demuxer->shouldStop_.load() ? 1 : 0;
    };

    AVDictionary* rawOptions = options.release();

    // --- .hlv encrypted file detection ---
    AVIOContext* decryptAvioCtx = nullptr;

    if (hlplayer::crypto::hasHlvExtension(url) || hlplayer::crypto::isHlvFile(url)) {
        // 1. Read 112-byte header
        FILE* hlvFile = nullptr;
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
        if (wlen > 1) {
            auto* wpath = new wchar_t[static_cast<size_t>(wlen)];
            MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wpath, wlen);
            hlvFile = _wfopen(wpath, L"rb");
            delete[] wpath;
        }
#else
        hlvFile = std::fopen(url.c_str(), "rb");
#endif
        if (!hlvFile) {
            return Result<void>::error(PlayerError::InvalidURL);
        }

        uint8_t headerBuf[hlplayer::crypto::HLV_HEADER_SIZE];
        size_t bytesRead = fread(headerBuf, 1, sizeof(headerBuf), hlvFile);
        fclose(hlvFile);

        if (bytesRead != hlplayer::crypto::HLV_HEADER_SIZE) {
            return Result<void>::error(PlayerError::CorruptFile);
        }

        // 2. Validate magic
        if (memcmp(headerBuf, hlplayer::crypto::HLV_MAGIC, 8) != 0) {
            return Result<void>::error(PlayerError::CorruptFile);
        }

        // 3. Deserialize header
        auto header = hlplayer::crypto::HlvHeader::deserialize(headerBuf, bytesRead);
        if (!header.isValid()) {
            return Result<void>::error(PlayerError::CorruptFile);
        }

        // 4. Try session cache first
        uint8_t headerHmac[32];
        memcpy(headerHmac, headerBuf + 0x50, 32);

        static hlplayer::crypto::SessionKeyCache sessionCache;
        auto cachedKey = sessionCache.tryFindKey(headerBuf, headerHmac);

        hlplayer::crypto::SecureBytes aesKey;
        hlplayer::crypto::SecureBytes hmacKey;

        if (cachedKey) {
            aesKey = std::move(*cachedKey);
        } else {
            // 5. Need password from UI
            if (!callbacks_.onPasswordRequired) {
                return Result<void>::error(PlayerError::WrongPassword);
            }

            std::string userInput = callbacks_.onPasswordRequired(url, static_cast<int>(header.keyMode));
            if (userInput.empty()) {
                return Result<void>::error(PlayerError::WrongPassword);
            }

            // 6. Derive keys based on key mode
            if (header.keyMode == hlplayer::crypto::KeyMode::Password) {
                auto derived = hlplayer::crypto::KeyManager::deriveFromPassword(
                    userInput, header.salt, header.getClampedIterations());
                aesKey = std::move(derived.aesKey);
                hmacKey = std::move(derived.hmacKey);
            } else {
                // Raw key mode: parse key string
                auto rawKeyResult = hlplayer::crypto::KeyManager::parseKeyString(userInput);
                if (rawKeyResult.hasError()) {
                    return Result<void>::error(PlayerError::WrongPassword);
                }
                auto derived = hlplayer::crypto::KeyManager::deriveFromRawKey(rawKeyResult.value());
                aesKey = std::move(derived.aesKey);
                hmacKey = std::move(derived.hmacKey);
            }

            // 7. Verify HMAC (constant-time compare)
            uint8_t computedHmac[32];
            hlplayer::crypto::HmacSha256::compute(
                hmacKey.data(), 32, headerBuf, 80, computedHmac);

            if (!hlplayer::crypto::constant_time_compare(computedHmac, headerHmac, 32)) {
                return Result<void>::error(PlayerError::WrongPassword);
            }

            // 8. Store in session cache
            sessionCache.put(url, aesKey, hmacKey);
        }

        // 9. Create DecryptingAVIOContext
        hlplayer::crypto::DecryptConfig decryptConfig;
        decryptConfig.filePath = url;
        decryptConfig.aesKey = aesKey;
        memcpy(decryptConfig.nonce, header.nonce, 12);
        decryptConfig.originalSize = header.originalSize;

        decryptAvioCtx = hlplayer::crypto::DecryptingAVIOContext::create(decryptConfig);
        if (!decryptAvioCtx) {
            return Result<void>::error(PlayerError::DecodeError);
        }

        // 10. Inject into AVFormatContext BEFORE avformat_open_input
        rawFormatCtx->pb = decryptAvioCtx;
    }

    const char* openUrl = decryptAvioCtx ? "" : url.c_str();
    int ret = avformat_open_input(&rawFormatCtx, openUrl, nullptr, &rawOptions);
    if (ret < 0) {
        if (decryptAvioCtx) {
            hlplayer::crypto::DecryptingAVIOContext::destroy(decryptAvioCtx);
        }
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to open input: {}", errBuf);
        return Result<void>::error(PlayerError::InvalidURL);
    }

    // Store decryptAvioCtx for later cleanup
    decryptAvioCtx_ = decryptAvioCtx;
    formatCtx_.reset(rawFormatCtx);

    ret = avformat_find_stream_info(formatCtx_.get(), nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        spdlog::error("Failed to find stream info: {}", errBuf);
        return Result<void>::error(PlayerError::DecodeError);
    }

    // ffplay hack: find_stream_info can leave eof_reached set on
    // small files; clear it so the demux loop can continue reading.
    if (formatCtx_->pb) {
        formatCtx_->pb->eof_reached = 0;
    }

    if (rawOptions) {
        av_dict_free(&rawOptions);
    }

    streamInfoMap_.clear();
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
    subtitleStreamIndex_ = -1;

    for (unsigned int i = 0; i < formatCtx_->nb_streams; ++i) {
        AVStream* stream = formatCtx_->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (videoStreamIndex_ == -1) {
                videoStreamIndex_ = static_cast<int>(i);
                videoTimeBase_ = stream->time_base;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audioStreamIndex_ == -1) {
                audioStreamIndex_ = static_cast<int>(i);
                audioTimeBase_ = stream->time_base;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (subtitleStreamIndex_ == -1) {
                subtitleStreamIndex_ = static_cast<int>(i);
            }
        }

        StreamInfo info;
        info.index = static_cast<int>(i);
        info.codecId = stream->codecpar->codec_id;

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            info.width = stream->codecpar->width;
            info.height = stream->codecpar->height;
            info.pixelFormat = static_cast<AVPixelFormat>(stream->codecpar->format);

            if (stream->avg_frame_rate.den > 0) {
                info.frameRate = av_q2d(stream->avg_frame_rate);
            }

            if (stream->codecpar->bit_rate > 0) {
                info.bitRate = static_cast<int>(stream->codecpar->bit_rate);
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            info.sampleRate = stream->codecpar->sample_rate;
            info.channels = stream->codecpar->ch_layout.nb_channels;
            info.sampleFormat = static_cast<AVSampleFormat>(stream->codecpar->format);

            if (stream->codecpar->bit_rate > 0) {
                info.bitRate = static_cast<int>(stream->codecpar->bit_rate);
            }
        }

        streamInfoMap_[i] = info;

        if ((stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && static_cast<int>(i) == videoStreamIndex_)
            || (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && static_cast<int>(i) == audioStreamIndex_)
            || (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && static_cast<int>(i) == subtitleStreamIndex_)) {
            notifyStreamDetected(stream);
        }
    }

    state_.store(PlayerState_Prepared);

    spdlog::info("Demuxer opened successfully: video={} audio={} subtitle={}",
                 videoStreamIndex_, audioStreamIndex_, subtitleStreamIndex_);

    return Result<void>::success();
}

Result<void> FFmpegDemuxer::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == PlayerState_Idle) {
        spdlog::error("Cannot start demuxer: not opened (state={})", static_cast<int>(state_.load()));
        return Result<void>::error(PlayerError::InvalidState);
    }

    shouldStop_.store(false);
    startReading_.store(true);

    if (demuxThread_.joinable()) {
        state_.store(PlayerState_Playing);
        commandCv_.notify_one();
    } else {
        state_.store(PlayerState_Playing);
        demuxThread_ = std::thread(&FFmpegDemuxer::demuxLoop, this);
    }

    spdlog::info("Demuxer started");

    return Result<void>::success();
}

Result<void> FFmpegDemuxer::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ == PlayerState_Idle) {
            return Result<void>::success();
        }

        shouldStop_.store(true);
    }

    commandCv_.notify_one();

    if (demuxThread_.joinable()) {
        if (std::this_thread::get_id() == demuxThread_.get_id()) {
            demuxThread_.detach();
        } else {
            demuxThread_.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Detach custom AVIOContext from format context before destroying it
        // to prevent avformat_close_input from trying to close our custom pb
        if (formatCtx_ && decryptAvioCtx_) {
            formatCtx_->pb = nullptr;
        }

        formatCtx_.reset();

        // Now safe to destroy our custom AVIOContext
        if (decryptAvioCtx_) {
            hlplayer::crypto::DecryptingAVIOContext::destroy(decryptAvioCtx_);
            decryptAvioCtx_ = nullptr;
        }

        state_.store(PlayerState_Idle);
    }

    spdlog::info("Demuxer stopped");
    return Result<void>::success();
}

Result<void> FFmpegDemuxer::seek(double seconds) {
    PlayerState curState = state_.load();
    if (curState != PlayerState_Playing && curState != PlayerState_Paused && curState != PlayerState_Prepared) {
        spdlog::error("Cannot seek: invalid state (state={})", static_cast<int>(curState));
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!formatCtx_) {
        spdlog::error("Cannot seek: format context is null");
        return Result<void>::error(PlayerError::InvalidState);
    }

    {
        std::lock_guard lock(commandMutex_);
        seekCompleted_.store(false);
        pendingSeekTarget_.store(seconds);
        seekRequested_.store(true);
    }
    commandCv_.notify_one();

    {
        std::unique_lock lock(commandMutex_);
        commandCv_.wait(lock, [this] {
            return seekCompleted_.load() || shouldStop_.load();
        });
    }

    if (shouldStop_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    return Result<void>::success();
}

PlayerState FFmpegDemuxer::getState() const {
    return state_.load();
}

double FFmpegDemuxer::getDuration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!formatCtx_ || formatCtx_->duration <= 0) {
        return 0.0;
    }
    return static_cast<double>(formatCtx_->duration) / AV_TIME_BASE;
}

void FFmpegDemuxer::setFFmpegOptions(const FFmpegDemuxerOptions& options) {
    ffmpegOptions_ = options;
}

Result<StreamInfo> FFmpegDemuxer::getStreamInfo(StreamType streamType) const {
    std::lock_guard<std::mutex> lock(mutex_);

    int index = -1;
    if (streamType == StreamType::Video) {
        index = videoStreamIndex_;
    } else if (streamType == StreamType::Audio) {
        index = audioStreamIndex_;
    } else if (streamType == StreamType::Subtitle) {
        index = subtitleStreamIndex_;
    }

    auto it = streamInfoMap_.find(index);
    if (it != streamInfoMap_.end()) {
        return Result<StreamInfo>::success(it->second);
    }

    return Result<StreamInfo>::error(PlayerError::DecodeError);
}

void FFmpegDemuxer::demuxLoop() {
    AVPacketPtr packet = makeAVPacket();
    bool seekJustDone = false;

    spdlog::info("Demux loop started, shouldStop={}, startReading={}",
                 shouldStop_.load(), startReading_.load());

    while (!shouldStop_.load()) {
        // ── Seek command handling ──────────────────────────────────────
        if (seekRequested_.load()) {
            double target = pendingSeekTarget_.load();
            int64_t timestamp = static_cast<int64_t>(target * AV_TIME_BASE);

            spdlog::info("Demuxer: seeking to {:.3f} seconds (eofReached={})",
                         target, eofReached_.load());

            bool needReopen = false;

            // Always use full reopen for post-EOF seeks to ensure proper file pointer reset
            if (eofReached_.load()) {
                needReopen = true;
                spdlog::debug("Demuxer: forcing full reopen for post-EOF seek");
            }

            if (needReopen || (eofReached_.load() && !formatCtx_)) {
                // After EOF the MOV demuxer's internal sample pointer is
                // past the last entry.  avformat_seek_file reports success
                // but av_read_frame immediately returns AVERROR_EOF because
                // the demuxer state is corrupt.  The only reliable fix is
                // to close and reopen the format context from scratch.
                std::string url = config_.url;
                formatCtx_.reset();

                AVFormatContext* rawCtx = avformat_alloc_context();
                bool reopenOk = false;
                if (rawCtx) {
                    rawCtx->interrupt_callback.opaque = this;
                    rawCtx->interrupt_callback.callback = [](void* opaque) -> int {
                        return static_cast<FFmpegDemuxer*>(opaque)->shouldStop_.load() ? 1 : 0;
                    };

                    // Re-create DecryptingAVIOContext for .hlv files
                    AVIOContext* reopenDecryptCtx = nullptr;
                    if (decryptAvioCtx_ && (hlplayer::crypto::hasHlvExtension(url) || hlplayer::crypto::isHlvFile(url))) {
                        // Read header again to get HMAC for cache lookup
                        FILE* hlvFile = nullptr;
                        fopen_s(&hlvFile, url.c_str(), "rb");
                        if (hlvFile) {
                            uint8_t headerBuf[hlplayer::crypto::HLV_HEADER_SIZE];
                            size_t bytesRead = fread(headerBuf, 1, sizeof(headerBuf), hlvFile);
                            fclose(hlvFile);

                            if (bytesRead == hlplayer::crypto::HLV_HEADER_SIZE) {
                                uint8_t headerHmac[32];
                                memcpy(headerHmac, headerBuf + 0x50, 32);

                                static hlplayer::crypto::SessionKeyCache sessionCache;
                                auto cachedKey = sessionCache.tryFindKey(headerBuf, headerHmac);

                                if (cachedKey) {
                                    auto header = hlplayer::crypto::HlvHeader::deserialize(headerBuf, bytesRead);
                                    if (header.isValid()) {
                                        hlplayer::crypto::DecryptConfig decryptConfig;
                                        decryptConfig.filePath = url;
                                        decryptConfig.aesKey = std::move(*cachedKey);
                                        memcpy(decryptConfig.nonce, header.nonce, 12);
                                        decryptConfig.originalSize = header.originalSize;

                                        reopenDecryptCtx = hlplayer::crypto::DecryptingAVIOContext::create(decryptConfig);
                                        if (reopenDecryptCtx) {
                                            rawCtx->pb = reopenDecryptCtx;
                                        }
                                    }
                                } else {
                                    spdlog::error("Reopen-seek: key not found in cache for .hlv file");
                                }
                            }
                        }
                    }

                    int openRet = avformat_open_input(&rawCtx, reopenDecryptCtx ? "" : url.c_str(), nullptr, nullptr);
                    if (openRet >= 0) {
                        if (reopenDecryptCtx) {
                            decryptAvioCtx_ = reopenDecryptCtx;
                        }
                        // Don't call avformat_find_stream_info on reopen - it reads to EOF and breaks seeking.
                        // We already know the stream indices from the initial open.
                        if (rawCtx->pb) {
                            rawCtx->pb->eof_reached = 0;
                            rawCtx->pb->error = 0;
                        }
                        formatCtx_.reset(rawCtx);

                        int seekIdx = (videoStreamIndex_ >= 0 &&
                                       videoStreamIndex_ < static_cast<int>(formatCtx_->nb_streams))
                                          ? videoStreamIndex_
                                          : -1;
                        int seekRet;
                        if (seekIdx >= 0) {
                            AVRational tb = formatCtx_->streams[seekIdx]->time_base;
                            int64_t streamTs = av_rescale_q(timestamp, AV_TIME_BASE_Q, tb);
                            int64_t streamMin = av_rescale_q(INT64_MIN, AV_TIME_BASE_Q, tb);
                            int64_t streamMax = av_rescale_q(INT64_MAX, AV_TIME_BASE_Q, tb);
                            seekRet = avformat_seek_file(formatCtx_.get(), seekIdx,
                                                          streamMin, streamTs, streamMax,
                                                          AVSEEK_FLAG_BACKWARD);
                        } else {
                            seekRet = avformat_seek_file(formatCtx_.get(), -1, INT64_MIN,
                                                          timestamp, INT64_MAX, AVSEEK_FLAG_BACKWARD);
                        }
                        if (seekRet < 0) {
                            spdlog::error("Reopen-seek to {:.3f}s failed (ret={})", target, seekRet);
                        } else {
                            if (formatCtx_->pb) {
                                formatCtx_->pb->eof_reached = 0;
                                formatCtx_->pb->error = 0;
                            }

                            // Probe read to ensure file pointer is positioned correctly after seek
                            AVPacketPtr probePkt = makeAVPacket();
                            int probeRet = av_read_frame(formatCtx_.get(), probePkt.get());
                            if (probeRet >= 0) {
                                processPacket(probePkt.get());
                                spdlog::debug("Reopen-seek probe read succeeded");
                            } else {
                                spdlog::warn("Reopen-seek probe read failed (ret={}), but seek is still considered OK", probeRet);
                            }

                            reopenOk = true;
                            spdlog::info("Reopen-seek to {:.3f}s succeeded (stream={}, pb->pos={})",
                                         target, seekIdx,
                                         formatCtx_->pb ? formatCtx_->pb->pos : -1);
                        }
                    } else {
                        spdlog::error("Reopen of {} failed (ret={})", url, openRet);
                        formatCtx_.reset();
                        if (reopenDecryptCtx) {
                            hlplayer::crypto::DecryptingAVIOContext::destroy(reopenDecryptCtx);
                        }
                    }
                }

                if (!reopenOk) {
                    spdlog::error("Seek to {:.3f}s failed via reopen path", target);
                }
            } else {
                int ret = avformat_seek_file(formatCtx_.get(), -1, INT64_MIN,
                                             timestamp, INT64_MAX, AVSEEK_FLAG_BACKWARD);

                if (ret >= 0 && formatCtx_->pb) {
                    formatCtx_->pb->eof_reached = 0;
                    formatCtx_->pb->error = 0;
                    spdlog::info("Seeked to {:.3f}s (pb->pos={})",
                                 target,
                                 formatCtx_->pb ? formatCtx_->pb->pos : -1);
                } else {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    spdlog::error("Failed to seek to {:.3f} seconds: {} (ret={})",
                                  target, errBuf, ret);
                }
            }

            eofReached_.store(false);
            av_packet_unref(packet.get());

            {
                std::lock_guard lock(commandMutex_);
                seekRequested_.store(false);
                seekCompleted_.store(true);
            }
            commandCv_.notify_one();

            // Resume reading immediately after seek completes - don't wait for startReading_
            // because FFPlayer doesn't call demuxer->start() again after seeking
            seekJustDone = true;
            continue;
        }

        // ── startReading gate (first start or restart after EOF) ───────
        if (startReading_.load()) {
            startReading_.store(false);
            spdlog::info("Demuxer: first read, pb state: pos={}, eof_reached={}, error={}, seekable={}, file_size={}",
                         formatCtx_ && formatCtx_->pb ? formatCtx_->pb->pos : -1,
                         formatCtx_ && formatCtx_->pb ? formatCtx_->pb->eof_reached : -1,
                         formatCtx_ && formatCtx_->pb ? formatCtx_->pb->error : -1,
                         formatCtx_ && formatCtx_->pb ? (formatCtx_->pb->seekable & AVIO_SEEKABLE_NORMAL) : -1,
                         formatCtx_ && formatCtx_->pb ? (formatCtx_->pb->seekable & AVIO_SEEKABLE_NORMAL ? avio_size(formatCtx_->pb) : -1) : -1);

            if (seekJustDone) {
                // A seek was just performed — skip the seek-to-zero so we don't
                // override the seek target with position 0.
                seekJustDone = false;
            } else {
                // Reset file pointer to beginning - avformat_find_stream_info may have read to EOF
                if (formatCtx_ && formatCtx_->pb) {
                    formatCtx_->pb->eof_reached = 0;
                    formatCtx_->pb->error = 0;
                }
                avformat_seek_file(formatCtx_.get(), -1, 0, 0, INT64_MAX, AVSEEK_FLAG_BACKWARD);
            }
        }

        if (!formatCtx_) {
            spdlog::warn("Demuxer: formatCtx_ is null, exiting loop");
            break;
        }

        // ── Read frame ────────────────────────────────────────────────
        int ret = av_read_frame(formatCtx_.get(), packet.get());
        if (ret < 0) {
            if (seekRequested_.load()) {
                spdlog::debug("Demuxer: seekRequested during read error, retrying");
                av_packet_unref(packet.get());
                continue;
            }

            if (ret == AVERROR(EAGAIN)) {
                spdlog::debug("Demuxer: av_read_frame returned EAGAIN, retrying in 10ms");
                av_packet_unref(packet.get());
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (ret == AVERROR_EOF) {
                eofReached_.store(true);
                spdlog::info("End of stream reached (pb: pos={}, eof_reached={}, error={})",
                             formatCtx_ && formatCtx_->pb ? formatCtx_->pb->pos : -1,
                             formatCtx_ && formatCtx_->pb ? formatCtx_->pb->eof_reached : -1,
                             formatCtx_ && formatCtx_->pb ? formatCtx_->pb->error : -1);
                if (callbacks_.onEndOfStream) {
                    callbacks_.onEndOfStream();
                }
            } else {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                spdlog::warn("Demuxer: av_read_frame error (ret={}): {}, will retry", ret, errBuf);
                notifyError(PlayerError::DecodeError, std::string("Failed to read frame: ") + errBuf);
                av_packet_unref(packet.get());
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            av_packet_unref(packet.get());

            spdlog::debug("Demuxer: parking on EOF, waiting for seek/stop/startReading");
            std::unique_lock lock(commandMutex_);
            commandCv_.wait(lock, [this] {
                return seekRequested_.load() || shouldStop_.load() || startReading_.load();
            });
            if (startReading_.load()) {
                startReading_.store(false);
                spdlog::info("Demuxer: woken from EOF park, resuming read");
            }
            continue;
        }

        processPacket(packet.get());
        av_packet_unref(packet.get());
    }

    spdlog::info("Demux loop finished");
}

bool FFmpegDemuxer::processPacket(AVPacket* packet) {
    if (!packet || !packet->data) {
        return false;
    }

    int streamIndex = packet->stream_index;

    auto it = streamInfoMap_.find(streamIndex);
    if (it == streamInfoMap_.end()) {
        return false;
    }

    const StreamInfo& info = it->second;

    int64_t packetPts = packet->pts;
    if (packetPts == AV_NOPTS_VALUE) {
        packetPts = packet->dts;
    }

    double pts = 0.0;
    if (packetPts != AV_NOPTS_VALUE) {
        if (streamIndex == videoStreamIndex_) {
            pts = av_q2d(videoTimeBase_) * static_cast<double>(packetPts);
        } else if (streamIndex == audioStreamIndex_) {
            pts = av_q2d(audioTimeBase_) * static_cast<double>(packetPts);
        } else {
            pts = static_cast<double>(packetPts);
        }
    } else {
        pts = 0.0;
    }

    double duration = 0.0;
    int64_t packetDuration = packet->duration;
    if (packetDuration > 0) {
        if (streamIndex == videoStreamIndex_) {
            duration = av_q2d(videoTimeBase_) * static_cast<double>(packetDuration);
        } else if (streamIndex == audioStreamIndex_) {
            duration = av_q2d(audioTimeBase_) * static_cast<double>(packetDuration);
        } else {
            duration = static_cast<double>(packetDuration);
        }
    }

    auto mediaPacket = std::make_shared<MediaPacket>();

    if (info.index == videoStreamIndex_) {
        mediaPacket->streamType = StreamType::Video;
    } else if (info.index == audioStreamIndex_) {
        mediaPacket->streamType = StreamType::Audio;
    } else if (info.index == subtitleStreamIndex_) {
        mediaPacket->streamType = StreamType::Subtitle;
    } else {
        mediaPacket->streamType = StreamType::Unknown;
    }

    mediaPacket->data.assign(packet->data, packet->data + packet->size);
    mediaPacket->pts = pts;
    mediaPacket->duration = duration;
    mediaPacket->keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    mediaPacket->size = static_cast<uint32_t>(packet->size);

    notifyPacket(mediaPacket);

    return true;
}

void FFmpegDemuxer::notifyError(PlayerError error, const std::string& message) {
    if (callbacks_.onError) {
        callbacks_.onError(error, message);
    }
}

void FFmpegDemuxer::notifyStreamDetected(const AVStream* stream) {
    if (!callbacks_.onStreamDetected || !stream) {
        return;
    }

    StreamType streamType = getStreamType(stream);

    int codecId = static_cast<int>(stream->codecpar->codec_id);
    int width = 0, height = 0;
    int sampleRate = 0, channels = 0;

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        width = stream->codecpar->width;
        height = stream->codecpar->height;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        sampleRate = stream->codecpar->sample_rate;
        channels = stream->codecpar->ch_layout.nb_channels;
    }

    const uint8_t* extraData = nullptr;
    size_t extraDataSize = 0;
    if (stream->codecpar && stream->codecpar->extradata && stream->codecpar->extradata_size > 0) {
        extraData = stream->codecpar->extradata;
        extraDataSize = static_cast<size_t>(stream->codecpar->extradata_size);
    }

    callbacks_.onStreamDetected(streamType, codecId, width, height, sampleRate, channels, extraData, extraDataSize);
}

void FFmpegDemuxer::notifyPacket(std::shared_ptr<MediaPacket> packet) {
    if (callbacks_.onPacket) {
        callbacks_.onPacket(packet);
    }
}

AVDictionaryPtr FFmpegDemuxer::createFFmpegOptions() const {
    AVDictionary* dict = nullptr;

    if (!ffmpegOptions_.userAgent.empty()) {
        av_dict_set(&dict, "user_agent", ffmpegOptions_.userAgent.c_str(), 0);
    }

    for (const auto& [key, value] : ffmpegOptions_.headers) {
        std::string header = key + ": " + value;
        av_dict_set(&dict, "headers", header.c_str(), AV_DICT_APPEND);
    }

    if (ffmpegOptions_.timeoutUs > 0) {
        std::string timeoutStr = std::to_string(ffmpegOptions_.timeoutUs);
        av_dict_set(&dict, "timeout", timeoutStr.c_str(), 0);
    }

    if (ffmpegOptions_.bufferSize > 0) {
        std::string bufferSizeStr = std::to_string(ffmpegOptions_.bufferSize);
        av_dict_set(&dict, "buffer_size", bufferSizeStr.c_str(), 0);
    }

    if (ffmpegOptions_.maxAnalyzeDuration > 0) {
        std::string analyzeStr = std::to_string(ffmpegOptions_.maxAnalyzeDuration);
        av_dict_set(&dict, "analyzeduration", analyzeStr.c_str(), 0);
    }

    if (ffmpegOptions_.lowLatency) {
        // Reconnect options only make sense for network streams.
        if (config_.url.compare(0, 7, "http://") == 0 ||
            config_.url.compare(0, 8, "https://") == 0 ||
            config_.url.compare(0, 6, "rtmp://") == 0 ||
            config_.url.compare(0, 7, "rtmps://") == 0) {
            av_dict_set(&dict, "reconnect", "1", 0);
            av_dict_set(&dict, "reconnect_streamed", "1", 0);
            av_dict_set(&dict, "reconnect_delay_max", "2", 0);
        }
    }

    if (!config_.format.empty() && config_.format != "auto") {
        av_dict_set(&dict, "format", config_.format.c_str(), 0);
    }

    return AVDictionaryPtr(dict);
}

StreamType FFmpegDemuxer::getStreamType(const AVStream* stream) const {
    if (!stream) {
        return StreamType::Unknown;
    }

    switch (stream->codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            return StreamType::Video;
        case AVMEDIA_TYPE_AUDIO:
            return StreamType::Audio;
        case AVMEDIA_TYPE_SUBTITLE:
            return StreamType::Subtitle;
        default:
            return StreamType::Unknown;
    }
}

} // namespace extractor
} // namespace hlplayer
