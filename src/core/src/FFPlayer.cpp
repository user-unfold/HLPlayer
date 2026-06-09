#include <hlplayer/FFPlayer.h>
#include <hlplayer/Result.h>
#include <hlplayer/logger.h>

#include <algorithm>
#include <chrono>
#include <mutex>

#include <spdlog/spdlog.h>

#include "AudioTempoProcessor.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace hlplayer {

struct FFPlayer::Impl {
    EventBus eventBus;
    std::unique_ptr<IDemuxer> demuxer;
    std::unique_ptr<IHWDecoder> videoDecoder;
    std::unique_ptr<IAudioDecoder> audioDecoder;
    std::unique_ptr<IAudioRenderer> audioRenderer;
    IVideoFrameSink* videoSink = nullptr;

    PacketQueue videoPacketQueue{200, 20 * 1024 * 1024};
    PacketQueue audioPacketQueue{100, 5 * 1024 * 1024};
    VideoFrameQueue videoFrameQueue{4};

    SyncClock syncClock;
    std::unique_ptr<VideoRefreshThread> refreshThread;

    std::thread videoDecodeThread;
    std::thread audioDecodeThread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> demuxerDone{false};
    std::atomic<bool> seekInProgress{false};
    std::atomic<int> seekSerial{0};  // Incremented on each seek to discard stale packets
    std::atomic<double> videoSeekTarget_{-1.0};
    std::atomic<double> audioSeekTarget_{-1.0};
    std::atomic<double> seekDuration_{0.0};
    std::atomic<double> playbackRate{1.0};

    std::atomic<bool> audioDecodeDone_{false};
    std::atomic<bool> videoDecodeDone_{false};
    std::atomic<bool> endTransitioning_{false};
    std::atomic<bool> endRequested_{false};

    std::atomic<PlayerState> state{PlayerState_Idle};
    std::atomic<double> position{0.0};
    std::atomic<double> positionFloor_{-1.0};
    std::atomic<double> lastReportedPos_{0.0};
    std::atomic<double> duration{0.0};
    std::string currentUrl;
    bool videoStreamReady = false;
    bool audioStreamReady = false;

    std::unique_ptr<AudioTempoProcessor> tempoProcessor;

    AudioFrameCallback audioFrameCallback_;

    std::recursive_mutex apiMutex;

    // Protects the decoder objects so that avcodec_flush_buffers() from the
    // seek path never races with an ongoing avcodec_send/receive_packet on
    // the decode threads.
    std::mutex videoDecodeMutex;
    std::mutex audioDecodeMutex;

    void publishStateChanged(PlayerState oldState, PlayerState newState) {
        Event e{EventType::StateChanged, 0.0,
                StateChangedPayload{oldState, newState, PlayerEvent::ErrorOccurred}};
        eventBus.publish(e);
    }

    void tryTransitionToEnd() {
        if (!demuxerDone.load() || seekInProgress.load()) return;
        if (!audioDecodeDone_.load() || !videoDecodeDone_.load()) return;
        if (paused.load()) return;

        bool expected = false;
        if (!endTransitioning_.compare_exchange_strong(expected, true)) return;

        // Signal the refresh thread that no more frames will come.
        // The refresh thread will drain remaining frames from the queue
        // and fire onPlaybackComplete when the last frame is displayed.
        endRequested_.store(true);

        // If there is no video sink (audio-only playback), transition immediately
        if (!videoSink) {
            if (audioRenderer) audioRenderer->pause();
            PlayerState old = state.exchange(PlayerState_End);
            if (old != PlayerState_End) {
                Event e{EventType::StateChanged, 0.0,
                        StateChangedPayload{old, PlayerState_End, PlayerEvent::ErrorOccurred}};
                eventBus.publish(e);
            }
            spdlog::info("Playback finished (audio-only), all queues drained");
            return;
        }

        spdlog::info("All input decoded, waiting for refresh thread to display remaining frames");
    }

    void completeEndTransition() {
        // Wait for remaining audio to play through the device buffer
        // before pausing the renderer.  This prevents cutting off the
        // tail of the audio when the decode loops finish before the
        // audio device has fully drained.
        if (audioRenderer) {
            int drainMs = audioRenderer->getLatencyMs();
            if (drainMs > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(std::min(drainMs + 50, 500)));
            }
            audioRenderer->pause();
        }
        PlayerState old = state.exchange(PlayerState_End);
        if (old != PlayerState_End) {
            Event e{EventType::StateChanged, 0.0,
                    StateChangedPayload{old, PlayerState_End, PlayerEvent::ErrorOccurred}};
            eventBus.publish(e);
        }
        spdlog::info("Playback finished, all frames displayed");
    }
};

FFPlayer::FFPlayer() : impl_(std::make_unique<Impl>()) {}

FFPlayer::~FFPlayer() {
    stop();
}

void FFPlayer::setDemuxer(std::unique_ptr<IDemuxer> demuxer) {
    impl_->demuxer = std::move(demuxer);
}

void FFPlayer::setVideoDecoder(std::unique_ptr<IHWDecoder> decoder) {
    impl_->videoDecoder = std::move(decoder);
}

void FFPlayer::setAudioDecoder(std::unique_ptr<IAudioDecoder> decoder) {
    impl_->audioDecoder = std::move(decoder);
}

void FFPlayer::setAudioRenderer(std::unique_ptr<IAudioRenderer> renderer) {
    impl_->audioRenderer = std::move(renderer);
}

void FFPlayer::setVideoSink(IVideoFrameSink* sink) {
    impl_->videoSink = sink;
}

void FFPlayer::setAudioFrameCallback(AudioFrameCallback callback) {
    impl_->audioFrameCallback_ = std::move(callback);
}

Result<void> FFPlayer::open(const std::string& url) {
    std::lock_guard lock(impl_->apiMutex);
    if (!impl_->demuxer) return Result<void>::error(PlayerError::InvalidState);

    if (impl_->running.load() || impl_->state.load() != PlayerState_Idle) {
        stopInternal();
    }

    impl_->currentUrl = url;
    impl_->videoStreamReady = false;
    impl_->audioStreamReady = false;
    impl_->positionFloor_.store(-1.0);
    impl_->position.store(0.0);
    impl_->lastReportedPos_.store(0.0);

    if (impl_->videoDecoder) impl_->videoDecoder->close();
    if (impl_->audioDecoder) impl_->audioDecoder->close();

    DemuxerCallbacks callbacks;

    callbacks.onStreamDetected =
        [this](StreamType streamType, int codecId, int width, int height,
               int sampleRate, int channels, const uint8_t* extraData,
               size_t extraDataSize) {
            if (streamType == StreamType::Video) {
                impl_->videoStreamReady = true;
                Codec codec = Codec::Unknown;
                switch (codecId) {
                    case AV_CODEC_ID_H264: codec = Codec::H264; break;
                    case AV_CODEC_ID_HEVC: codec = Codec::HEVC; break;
                    case AV_CODEC_ID_AV1: codec = Codec::AV1; break;
                    default: break;
                }
                if (codec == Codec::Unknown) return;

                DecoderConfig config;
                config.backend = DecodeBackend::Auto;
                config.codec = codec;
                config.width = static_cast<uint32_t>(width);
                config.height = static_cast<uint32_t>(height);
                if (extraData && extraDataSize > 0)
                    config.extradata.assign(extraData, extraData + extraDataSize);

                impl_->videoDecoder->open(config);

                Event e{EventType::ResolutionChanged, 0.0,
                        ResolutionPayload{static_cast<uint32_t>(width),
                                           static_cast<uint32_t>(height)}};
                impl_->eventBus.publish(e);
            } else if (streamType == StreamType::Audio) {
                AudioFormat desired;
                desired.sampleRate = sampleRate;
                desired.channels = channels;
                desired.sampleFormat = AudioSampleFormat::S16;
                desired.bytesPerSample = 2;

                if (impl_->audioRenderer) {
                    impl_->audioRenderer->open(desired);
                }

                AudioFormat targetFmt = impl_->audioRenderer
                    ? impl_->audioRenderer->format() : desired;
                AudioDecodeConfig audioConfig;
                audioConfig.codecId = codecId;
                audioConfig.sourceSampleRate = sampleRate;
                audioConfig.sourceChannels = channels;
                audioConfig.targetFormat = targetFmt;
                if (extraData && extraDataSize > 0)
                    audioConfig.extraData.assign(extraData, extraData + extraDataSize);

                if (impl_->audioDecoder->open(audioConfig)) {
                    impl_->audioStreamReady = true;

                    // Initialize tempo processor for pitch-preserving speed changes
                    impl_->tempoProcessor = std::make_unique<AudioTempoProcessor>();
                    impl_->tempoProcessor->initialize(
                        targetFmt.sampleRate, targetFmt.channels, targetFmt.sampleFormat);

                    if (impl_->videoStreamReady) {
                        impl_->syncClock.setMode(SyncClockMode::AudioMaster);
                    } else {
                        impl_->syncClock.setMode(SyncClockMode::VideoMaster);
                    }
                }
            }
        };

    callbacks.onPacket = [this](std::shared_ptr<MediaPacket> pkt) {
        // Tag packet with the current seek serial so it can be discarded
        // if a seek happens before it's decoded.
        pkt->seekSerial = impl_->seekSerial.load();

        if (pkt->streamType == StreamType::Video) {
            impl_->videoPacketQueue.push(std::move(pkt));
        } else if (pkt->streamType == StreamType::Audio) {
            impl_->audioPacketQueue.push(std::move(pkt));
        }
    };

    callbacks.onEndOfStream = [this]() {
        impl_->demuxerDone.store(true);
        spdlog::info("End of stream reached, demuxerDone=true");
    };

    callbacks.onError = [this](PlayerError err, const std::string& msg) {
        Event e{EventType::Error, 0.0, ErrorPayload{err, msg}};
        impl_->eventBus.publish(e);
    };

    DemuxerConfig config;
    config.url = url;
    auto result = impl_->demuxer->open(url, config, callbacks);
    if (result.hasError()) return result;

    impl_->duration.store(impl_->demuxer->getDuration());

    PlayerState old = impl_->state.exchange(PlayerState_Prepared);
    impl_->publishStateChanged(old, PlayerState_Prepared);

    return Result<void>::success();
}

Result<void> FFPlayer::play() {
    std::lock_guard lock(impl_->apiMutex);

    PlayerState cur = impl_->state.load();
    if (cur == PlayerState_Playing) return Result<void>::success();

    if (cur == PlayerState_Paused) {
        impl_->paused.store(false);
        if (impl_->refreshThread) impl_->refreshThread->resume();
        if (impl_->audioRenderer) impl_->audioRenderer->resume();
        // Re-anchor the external clock at the paused position so wall-clock
        // time spent paused does not shift the sync forward.
        impl_->syncClock.initExternalClock(impl_->position.load());
        PlayerState old = impl_->state.exchange(PlayerState_Playing);
        impl_->publishStateChanged(old, PlayerState_Playing);
        return Result<void>::success();
    }

    if (cur == PlayerState_End || (cur == PlayerState_Idle && !impl_->currentUrl.empty())) {
        stopInternal();
        auto openResult = open(impl_->currentUrl);
        if (openResult.hasError()) return openResult;
        cur = impl_->state.load();
    }

    if (cur != PlayerState_Prepared) {
        spdlog::warn("FFPlayer::play() called in state {}, need Prepared", static_cast<int>(cur));
        return Result<void>::error(PlayerError::InvalidState);
    }

    impl_->running.store(true);
    impl_->paused.store(false);
    impl_->demuxerDone.store(false);
    impl_->audioDecodeDone_.store(false);
    impl_->videoDecodeDone_.store(false);
    impl_->endTransitioning_.store(false);
    impl_->endRequested_.store(false);
    impl_->syncClock.reset();
    impl_->syncClock.initExternalClock(0.0);
    impl_->syncClock.setMode(SyncClockMode::ExternalClock);

    impl_->videoPacketQueue.restart();
    impl_->audioPacketQueue.restart();
    impl_->videoFrameQueue.restart();

    if (impl_->videoSink) {
        impl_->refreshThread = std::make_unique<VideoRefreshThread>(
            impl_->videoFrameQueue, impl_->syncClock, impl_->videoSink,
            impl_->endRequested_, impl_->seekSerial,
            [this]() { impl_->completeEndTransition(); });
        impl_->refreshThread->start();
    }

    impl_->videoDecodeThread = std::thread(&FFPlayer::videoDecodeLoop, this);
    impl_->audioDecodeThread = std::thread(&FFPlayer::audioDecodeLoop, this);

    if (impl_->demuxer) {
        auto r = impl_->demuxer->start();
        if (r.hasError()) {
            spdlog::error("FFPlayer: demuxer start failed: {}", static_cast<int>(r.error()));
        }
    }
    if (impl_->audioRenderer) impl_->audioRenderer->resume();

    PlayerState old = impl_->state.exchange(PlayerState_Playing);
    impl_->publishStateChanged(old, PlayerState_Playing);
    return Result<void>::success();
}

Result<void> FFPlayer::pause() {
    std::lock_guard lock(impl_->apiMutex);
    PlayerState cur = impl_->state.load();
    if (cur == PlayerState_End || cur == PlayerState_Idle || cur == PlayerState_Paused) {
        return Result<void>::success();
    }
    impl_->paused.store(true);
    if (impl_->refreshThread) impl_->refreshThread->pause();
    if (impl_->audioRenderer) impl_->audioRenderer->pause();
    PlayerState old = impl_->state.exchange(PlayerState_Paused);
    impl_->publishStateChanged(old, PlayerState_Paused);
    return Result<void>::success();
}

Result<void> FFPlayer::stop() {
    std::lock_guard lock(impl_->apiMutex);
    stopInternal();
    return Result<void>::success();
}

void FFPlayer::stopInternal() {
    impl_->running.store(false);
    impl_->paused.store(false);
    impl_->positionFloor_.store(-1.0);
    impl_->lastReportedPos_.store(0.0);

    impl_->videoPacketQueue.shutdown();
    impl_->audioPacketQueue.shutdown();
    impl_->videoFrameQueue.shutdown();

    if (impl_->refreshThread) {
        impl_->refreshThread->stop();
        impl_->refreshThread.reset();
    }

    if (impl_->videoDecodeThread.joinable()) impl_->videoDecodeThread.join();
    if (impl_->audioDecodeThread.joinable()) impl_->audioDecodeThread.join();

    if (impl_->demuxer) impl_->demuxer->stop();
    if (impl_->audioRenderer) impl_->audioRenderer->close();

    impl_->videoPacketQueue.flush();
    impl_->audioPacketQueue.flush();
    impl_->videoFrameQueue.flush();

    PlayerState old = impl_->state.exchange(PlayerState_Idle);
    if (old != PlayerState_Idle) {
        impl_->publishStateChanged(old, PlayerState_Idle);
    }
}

Result<void> FFPlayer::seek(double seconds) {
    std::lock_guard lock(impl_->apiMutex);
    if (!impl_->demuxer) return Result<void>::error(PlayerError::InvalidState);

    PlayerState currentState = impl_->state.load();
    bool wasPaused = (currentState == PlayerState_Paused);

    bool threadsExist = impl_->videoDecodeThread.joinable()
                     || impl_->audioDecodeThread.joinable();

    impl_->seekInProgress.store(true);
    impl_->seekSerial.fetch_add(1);  // Invalidate all packets queued from before seek

    // Set position protection BEFORE any long operations (flushes, demuxer seek).
    // Without this, getPosition() returns stale getMasterClock() during the
    // demuxer seek (which can take 50-200ms), causing visible position snap-back.
    impl_->syncClock.reset();
    impl_->syncClock.initExternalClock(seconds);
    impl_->syncClock.setMode(SyncClockMode::ExternalClock);
    impl_->syncClock.setAudioClock(seconds);
    impl_->syncClock.setVideoClock(seconds);
    impl_->position.store(seconds);
    impl_->positionFloor_.store(seconds);
    impl_->lastReportedPos_.store(seconds);

    impl_->videoSeekTarget_.store(seconds);
    impl_->audioSeekTarget_.store(seconds);
    impl_->seekDuration_.store(impl_->duration.load());

    impl_->audioDecodeDone_.store(false);
    impl_->videoDecodeDone_.store(false);
    impl_->endTransitioning_.store(false);
    impl_->endRequested_.store(false);

    impl_->videoPacketQueue.flush();
    impl_->audioPacketQueue.flush();
    impl_->videoFrameQueue.flush();

    {
        std::lock_guard lock(impl_->audioDecodeMutex);
        if (impl_->audioDecoder) impl_->audioDecoder->flush();
    }
    if (impl_->tempoProcessor) impl_->tempoProcessor->flush();
    if (impl_->audioRenderer) impl_->audioRenderer->flush();
    {
        std::lock_guard lock(impl_->videoDecodeMutex);
        if (impl_->videoDecoder) {
            auto flushResult = impl_->videoDecoder->flush();
            (void)flushResult;
        }
    }

    auto result = impl_->demuxer->seek(seconds);
    if (result.hasError()) {
        impl_->positionFloor_.store(-1.0);
        impl_->seekInProgress.store(false);
        return result;
    }

    impl_->demuxerDone.store(false);

    impl_->seekInProgress.store(false);

    if (!threadsExist) {
        impl_->running.store(true);
        impl_->paused.store(wasPaused);

        impl_->videoPacketQueue.restart();
        impl_->audioPacketQueue.restart();
        impl_->videoFrameQueue.restart();

        if (impl_->videoSink) {
            impl_->refreshThread = std::make_unique<VideoRefreshThread>(
                impl_->videoFrameQueue, impl_->syncClock, impl_->videoSink,
                impl_->endRequested_, impl_->seekSerial,
                [this]() { impl_->completeEndTransition(); });
            impl_->refreshThread->start();
            if (wasPaused) {
                impl_->refreshThread->pauseAfterNextFrame();
            }
        }

        impl_->videoDecodeThread = std::thread(&FFPlayer::videoDecodeLoop, this);
        impl_->audioDecodeThread = std::thread(&FFPlayer::audioDecodeLoop, this);
    } else if (wasPaused) {
        if (impl_->refreshThread) {
            impl_->refreshThread->resume();
            impl_->refreshThread->pauseAfterNextFrame();
        }
    }

    if (impl_->demuxer) {
        auto r = impl_->demuxer->start();
        if (r.hasError()) {
            spdlog::error("FFPlayer: demuxer start after seek failed: {}", static_cast<int>(r.error()));
        }
    }

    if (wasPaused) {
        if (impl_->audioRenderer) impl_->audioRenderer->pause();
        PlayerState old = impl_->state.exchange(PlayerState_Paused);
        if (old != PlayerState_Paused) {
            impl_->publishStateChanged(old, PlayerState_Paused);
        }
    } else {
        if (impl_->audioRenderer) impl_->audioRenderer->resume();
        PlayerState old = impl_->state.exchange(PlayerState_Playing);
        if (old != PlayerState_Playing) {
            impl_->publishStateChanged(old, PlayerState_Playing);
        }
    }

    return Result<void>::success();
}

Result<void> FFPlayer::setVolume(double volume) {
    if (impl_->audioRenderer) {
        impl_->audioRenderer->setVolume(volume);
    }
    return Result<void>::success();
}

Result<void> FFPlayer::setPlaybackRate(double rate) {
    if (rate < 0.25 || rate > 4.0) {
        spdlog::warn("Playback rate {:.2f} out of range [0.25, 4.0]", rate);
        return Result<void>::error(PlayerError::InvalidState);
    }
    impl_->playbackRate.store(rate);
    if (impl_->refreshThread) {
        impl_->refreshThread->setPlaybackRate(rate);
    }
    spdlog::info("Playback rate set to {:.2f}x", rate);
    return Result<void>::success();
}

double FFPlayer::getPlaybackRate() const {
    return impl_->playbackRate.load();
}

PlayerState FFPlayer::getState() const {
    return impl_->state.load();
}

double FFPlayer::getPosition() const {
    // During seek convergence: return the seek target until the audio
    // decode loop has processed a post-keyframe frame and cleared positionFloor_.
    double floor = impl_->positionFloor_.load();
    if (floor >= 0.0) {
        impl_->lastReportedPos_.store(floor, std::memory_order_release);
        return floor;
    }

    double pos = 0.0;

    if (impl_->state.load() == PlayerState_Playing) {
        // In ExternalClock mode the wall clock drives sync, avoiding the
        // accumulated drift that the audio-clock path suffers from back-
        // pressure sleep overhead.
        if (impl_->syncClock.mode() == SyncClockMode::ExternalClock) {
            double dur = impl_->duration.load();
            pos = impl_->syncClock.getExternalClock();
            if (dur > 0.0 && pos > dur) pos = dur;
        } else {
            pos = impl_->position.load(std::memory_order_acquire);
            if (pos <= 0.0) {
                double master = impl_->syncClock.getMasterClock();
                if (master > 0.0) pos = master;
            }
        }

        // Monotonic guard: position must never go backwards during playback.
        // The stored position = af->pts - latencySec can decrease slightly
        // because SDL audio latency fluctuates between frame writes.
        double last = impl_->lastReportedPos_.load(std::memory_order_acquire);
        if (pos < last) {
            pos = last;
        } else {
            impl_->lastReportedPos_.store(pos, std::memory_order_release);
        }
        return pos;
    }

    pos = impl_->position.load(std::memory_order_acquire);
    impl_->lastReportedPos_.store(pos, std::memory_order_release);
    return pos;
}

double FFPlayer::getDuration() const {
    return impl_->duration.load();
}

double FFPlayer::getFps() const {
    if (impl_->refreshThread) {
        return impl_->refreshThread->fps();
    }
    return 0.0;
}

EventBus& FFPlayer::eventBus() {
    return impl_->eventBus;
}

void FFPlayer::videoDecodeLoop() {
    spdlog::info("VideoDecodeThread started");

    while (impl_->running.load()) {
        auto pkt = impl_->videoPacketQueue.pop(50);
        if (!pkt) {
            // Queue is empty.  Check if we should mark decoding complete.
            if (impl_->demuxerDone.load() && impl_->videoPacketQueue.empty() && !impl_->paused.load()) {
                // Only mark as done if we've successfully decoded at least one frame
                // after the most recent seek (indicated by videoSeekTarget_ < 0).
                // This prevents spurious End transitions on short files.
                if (!impl_->videoDecodeDone_.load() && impl_->videoSeekTarget_.load() < 0.0) {
                    impl_->videoDecodeDone_.store(true);
                    impl_->tryTransitionToEnd();
                }
            }
            continue;
        }

        // Discard packets from old seek generations.
        // pkt->seekSerial was captured when the demuxer created the packet.
        // If it doesn't match the current seekSerial, this packet is stale
        // and should not be decoded.
        int currentSerial = impl_->seekSerial.load();
        if (pkt->seekSerial != currentSerial) {
            continue;  // Skip stale packet
        }

        // Skip while seek is in progress
        if (impl_->seekInProgress.load()) {
            continue;
        }

        // Reset the Done flag - we're about to decode a frame
        if (impl_->videoDecodeDone_.load()) {
            impl_->videoDecodeDone_.store(false);
        }

        // Decode the packet with mutex protection
        auto result = [&]() -> Result<GpuFrame> {
            std::lock_guard lock(impl_->videoDecodeMutex);
            if (impl_->seekInProgress.load()) {
                return Result<GpuFrame>::error(PlayerError::InvalidState);
            }
            return impl_->videoDecoder->decode(pkt->data.data(), pkt->data.size(), pkt->pts);
        }();

        if (result.hasValue()) {
            // One more check: did a seek happen while we were decoding?
            // (Very rare, but possible on slow systems.)
            if (pkt->seekSerial != impl_->seekSerial.load()) {
                continue;  // Discard frame from stale packet
            }

            if (impl_->seekInProgress.load()) {
                continue;
            }

            double ts = result.value().timestamp;

            // Mark that we've decoded at least one frame (seek target is cleared
            // to indicate forward progress).
            if (impl_->videoSeekTarget_.load() >= 0.0) {
                impl_->videoSeekTarget_.store(-1.0);
            }

            // Tag frame with the seek serial so the renderer can discard frames
            // from old seek generations.
            auto frame = result.value();
            frame.seekSerial = currentSerial;

            // Push the valid frame to the display queue
            impl_->videoFrameQueue.push(std::move(frame));
            impl_->syncClock.setVideoClock(ts);
        }
    }
    spdlog::info("VideoDecodeThread stopped");
}

void FFPlayer::audioDecodeLoop() {
    spdlog::info("AudioDecodeThread started");

    while (impl_->running.load()) {
        // Early exit when paused and not seeking
        if (impl_->paused.load() && impl_->audioSeekTarget_.load() < 0.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto pkt = impl_->audioPacketQueue.pop(50);
        if (!pkt) {
            // Queue is empty.  Check if we should mark decoding complete.
            if (impl_->demuxerDone.load() && impl_->audioPacketQueue.empty() && !impl_->paused.load()) {
                // Only mark as done if we've successfully decoded at least one frame
                // after the most recent seek (indicated by audioSeekTarget_ < 0).
                if (!impl_->audioDecodeDone_.load() && impl_->audioSeekTarget_.load() < 0.0) {
                    impl_->audioDecodeDone_.store(true);
                    impl_->tryTransitionToEnd();
                }
            }
            continue;
        }

        // Discard packets from old seek generations.
        // pkt->seekSerial was captured when the demuxer created the packet.
        int currentSerial = impl_->seekSerial.load();
        if (pkt->seekSerial != currentSerial) {
            continue;  // Skip stale packet
        }

        // Skip while seek is in progress
        if (impl_->seekInProgress.load()) {
            continue;
        }

        // Reset the Done flag - we're about to decode a frame
        if (impl_->audioDecodeDone_.load()) {
            impl_->audioDecodeDone_.store(false);
        }

        // Decode the packet with mutex protection
        int64_t ptsUs = static_cast<int64_t>(pkt->pts * 1000000.0);
        std::shared_ptr<AudioFrame> frame;
        {
            std::lock_guard lock(impl_->audioDecodeMutex);
            if (impl_->seekInProgress.load()) {
                continue;
            }
            frame = impl_->audioDecoder->decode(
                pkt->data.data(), pkt->data.size(), ptsUs);
        }

        if (frame) {
            // One more check: did a seek happen while we were decoding?
            if (pkt->seekSerial != impl_->seekSerial.load()) {
                continue;  // Discard frame from stale packet
            }

            if (impl_->seekInProgress.load()) {
                continue;
            }

            // Mark that we've decoded at least one frame (seek target is cleared
            // to indicate forward progress).
            if (impl_->audioSeekTarget_.load() >= 0.0) {
                impl_->audioSeekTarget_.store(-1.0);
            }

            if (impl_->audioFrameCallback_) {
                impl_->audioFrameCallback_(*frame);
            }

            // Process through tempo filter for pitch-preserving speed changes
            double rate = impl_->playbackRate.load();
            std::vector<std::shared_ptr<AudioFrame>> framesToRender;
            if (impl_->tempoProcessor) {
                impl_->tempoProcessor->setTempo(rate);
                framesToRender = impl_->tempoProcessor->process(frame);
            } else {
                framesToRender.push_back(frame);
            }

            for (auto& af : framesToRender) {
                // Render audio unconditionally (must keep flowing to avoid buffer underrun)
                if (impl_->audioRenderer) {
                    // Back-pressure keeps the audio decode loop roughly aligned with
                    // real-time so that the End state is not triggered prematurely.
                    // Sleeping for the exact excess (instead of polling at 5 ms) avoids
                    // the per-frame overhead that accumulates into multi-second drift.
                    int latencyMs = impl_->audioRenderer->getLatencyMs();
                    while (latencyMs > 200 &&
                           impl_->running.load() &&
                           !impl_->paused.load()) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(std::max(latencyMs - 200, 1)));
                        latencyMs = impl_->audioRenderer->getLatencyMs();
                    }
                    impl_->audioRenderer->write(af->data.data(), af->data.size());
                }

                double latencySec = impl_->audioRenderer
                    ? impl_->audioRenderer->getLatencyMs() / 1000.0 : 0.0;
                impl_->syncClock.setAudioDeviceLatency(latencySec);

                // Position and clock tracking — guard against backward PTS after seek.
                // Pre-keyframe audio frames (PTS before seek target) must not corrupt
                // the sync clock, as getPosition() uses getMasterClock() (audio-based)
                // as its primary position source during playback.
                if (!impl_->seekInProgress.load()) {
                    double playbackPos = af->pts - latencySec;
                    if (playbackPos < 0.0) playbackPos = 0.0;
                    double floor = impl_->positionFloor_.load();
                    if (floor >= 0.0 && playbackPos < floor) {
                        // Discard: decoded frame PTS is before seek target.
                        // Don't update sync clock — would corrupt position reporting.
                    } else {
                        // Only update sync clock once frames have caught up to seek target
                        impl_->syncClock.setAudioClock(af->pts + af->duration);
                        if (!impl_->videoStreamReady) {
                            impl_->syncClock.setVideoClock(af->pts + af->duration);
                        }
                        if (floor >= 0.0) {
                            impl_->positionFloor_.store(-1.0);
                        }
                        impl_->position.store(playbackPos);
                    }
                }
            }
        }
    }
    spdlog::info("AudioDecodeThread stopped");
}

} // namespace hlplayer
