#include <hlplayer/PlayerFacade.h>
#include <hlplayer/StateMachine.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/IStreamResolver.h>
#include <hlplayer/Demuxer.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/IAIPipeline.h>
#include <hlplayer/AIPipelineStub.h>
#include <hlplayer/CPUFallbackDecoder.h>
#include <hlplayer/telemetry.h>
#include <hlplayer/AVSync.h>
#include <hlplayer/logger.h>
#include <hlplayer/IAudioDecoder.h>
#include <hlplayer/IAudioRenderer.h>

extern "C" {
#include <libavcodec/codec_id.h>
}

#include <mutex>
#include <thread>
#include <chrono>
#include <deque>
#include <condition_variable>

namespace hlplayer {

namespace {

class StubStreamResolver final : public IStreamResolver {
public:
    Result<void> resolve(const std::string& /*url*/,
                         std::function<void(Result<StreamInfo>)> callback) override {
        callback(Result<StreamInfo>::error(PlayerError::UnsupportedFormat));
        return Result<void>::success();
    }

    void cancel() override {}

    uint32_t getCapabilities() const override { return 0; }
};

class StubDemuxerImpl final : public IDemuxer {
public:
    Result<void> open(const std::string& url,
                      const DemuxerConfig& /*config*/,
                      DemuxerCallbacks /*callbacks*/) override {
        LOG_WARN("StubDemuxer::open(\"%s\") — no FFmpeg", url.c_str());
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    Result<void> start() override {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    Result<void> stop() override {
        return Result<void>::success();
    }

    Result<void> seek(double /*seconds*/) override {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    PlayerState getState() const override {
        return PlayerState_Idle;
    }

    double getDuration() const override {
        return 0.0;
    }
};

class NullVideoSink final : public IVideoFrameSink {
public:
    void onFrame(const GpuFrame& /*frame*/) override {}
    void onFormatChanged(VideoFormat /*format*/) override {}
    void reset() override {}
};

/// Thread-safe bounded packet queue with graceful shutdown.
/// Provides backpressure on push (blocks when full) and waits on pop
/// (blocks when empty). Two termination modes:
///   finish()  – no more items will be added; drain what remains.
///   shutdown() – discard remaining items and unblock all waiters.
class PacketQueue {
public:
    explicit PacketQueue(size_t maxSize = 60) : maxSize_(maxSize) {}

    void push(std::shared_ptr<MediaPacket> pkt) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] { return queue_.size() < maxSize_ || finished_ || shutdown_; });
        if (finished_ || shutdown_) return;
        queue_.push_back(std::move(pkt));
        notEmpty_.notify_one();
    }

    std::shared_ptr<MediaPacket> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] { return !queue_.empty() || finished_ || shutdown_; });
        if (queue_.empty()) return nullptr;
        auto pkt = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return pkt;
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        queue_.clear();
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        notFull_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    bool isShutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<std::shared_ptr<MediaPacket>> queue_;
    size_t maxSize_;
    bool finished_ = false;
    bool shutdown_ = false;
};

} // anonymous namespace

struct PlayerFacade::Impl {
    mutable std::recursive_mutex apiMutex;
    StateMachine stateMachine;
    EventBus eventBus;
    std::unique_ptr<IStreamResolver> resolver;
    std::unique_ptr<IDemuxer> demuxer;
    std::unique_ptr<IHWDecoder> decoder;
    std::unique_ptr<IAIPipeline> aiPipeline;
    IVideoFrameSink* sink = nullptr;
    std::unique_ptr<IVideoFrameSink> ownedSink;

    std::unique_ptr<IAudioDecoder> audioDecoder;
    std::unique_ptr<IAudioRenderer> audioRenderer;
    bool audioStreamReady = false;
    bool videoStreamReady = false;

    PacketQueue audioQueue;
    std::thread audioThread;

    AtomicTelemetry atomicTelemetry;
    OtelTelemetry otelTelemetry;
    AVSyncClock syncClock;

    uint32_t enabledAICapabilities = 0;
    std::string lastError;
    std::string currentUrl;
    StreamInfo streamInfo;
    double volume = 1.0;

    void publishStateChanged(PlayerState oldState, PlayerState newState, PlayerEvent event) {
        Event e{EventType::StateChanged, 0.0,
                StateChangedPayload{oldState, newState, event}};
        eventBus.publish(e);
    }

    void publishError(PlayerError err, const std::string& msg) {
        lastError = msg;
        Event e{EventType::Error, 0.0, ErrorPayload{err, msg}};
        eventBus.publish(e);
    }

    void deliverVideoPacket(std::shared_ptr<MediaPacket> pkt) {
        if (!pkt || pkt->streamType != StreamType::Video) return;

        auto decResult = decoder->decode(
            pkt->data.data(), pkt->data.size(), pkt->pts);

        if (decResult.hasError()) {
            if (decResult.error() == PlayerError::NeedMoreData) {
                return;
            }
            LOG_ERROR("Video decode failed: error={} pts={:.3f} size={}",
                      static_cast<int>(decResult.error()), pkt->pts, pkt->data.size());
            atomicTelemetry.incrementCounter("error_count");
            PlayerState preErr = stateMachine.getState();
            stateMachine.transition(PlayerEvent::ErrorOccurred);
            publishStateChanged(preErr, PlayerState_Error, PlayerEvent::ErrorOccurred);
            publishError(decResult.error(), "Decode failed");
            demuxer->stop();
            return;
        }

        atomicTelemetry.incrementCounter("frames_decoded");
        syncClock.onVideoFrame(decResult.value().timestamp, pkt->duration);

        GpuFrame frame = std::move(decResult.value());

        if (aiPipeline && enabledAICapabilities != 0) {
            auto aiResult = aiPipeline->processFrame(frame, enabledAICapabilities);
            if (aiResult.hasValue()) {
                frame = std::move(aiResult.value());
            }
        }

        {
            double videoPts = frame.timestamp;
            double sleepSec = syncClock.computeVideoSleepSec(videoPts);
            if (sleepSec > 0.001) {
                double sleepMs = sleepSec * 1000.0;
                if (sleepMs > 50.0) sleepMs = 50.0;
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<int64_t>(sleepMs * 1000.0)));
            }
        }

        if (sink) {
            sink->onFrame(frame);
        }
    }

    void deliverAudioPacket(std::shared_ptr<MediaPacket> pkt) {
        if (!pkt || pkt->streamType != StreamType::Audio) return;
        if (!audioDecoder || !audioStreamReady) return;
        audioQueue.push(std::move(pkt));
    }

    void processOneAudioPacket(std::shared_ptr<MediaPacket> pkt) {
        if (!pkt || pkt->streamType != StreamType::Audio) return;
        if (!audioDecoder || !audioStreamReady) return;

        int64_t ptsUs = static_cast<int64_t>(pkt->pts * 1000000.0);
        auto frame = audioDecoder->decode(pkt->data.data(), pkt->data.size(), ptsUs);

        if (frame) {
            if (!videoStreamReady) {
                syncClock.onVideoFrame(frame->pts, pkt->duration);
            }
            if (audioRenderer) {
                while (audioRenderer->getLatencyMs() > 50 && !audioQueue.isShutdown()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                audioRenderer->write(frame->data.data(), frame->data.size());
            }
            syncClock.onAudioFrame(frame->pts, pkt->duration);
        }
    }

    void audioLoop() {
        while (true) {
            auto pkt = audioQueue.pop();
            if (!pkt) break;
            processOneAudioPacket(std::move(pkt));
        }
    }

    void startAudioThread() {
        stopAudioThread();
        audioQueue.clear();
        audioThread = std::thread(&Impl::audioLoop, this);
    }

    void stopAudioThread() {
        audioQueue.shutdown();
        if (audioThread.joinable()) {
            audioThread.join();
        }
    }
};

PlayerFacade::PlayerFacade()
    : impl_(std::make_unique<Impl>()) {
    impl_->resolver = std::make_unique<StubStreamResolver>();
    impl_->demuxer = std::make_unique<StubDemuxerImpl>();
    impl_->decoder = std::make_unique<CPUFallbackDecoder>();
    impl_->aiPipeline = std::make_unique<AIPipelineStub>();
    impl_->ownedSink = std::make_unique<NullVideoSink>();
    impl_->sink = impl_->ownedSink.get();
}

PlayerFacade::PlayerFacade(std::unique_ptr<IStreamResolver> resolver,
                           std::unique_ptr<IDemuxer> demuxer,
                           std::unique_ptr<IHWDecoder> decoder,
                           IVideoFrameSink* sink)
    : impl_(std::make_unique<Impl>()) {
    impl_->resolver = std::move(resolver);
    impl_->demuxer = std::move(demuxer);
    impl_->decoder = std::move(decoder);
    impl_->aiPipeline = std::make_unique<AIPipelineStub>();
    impl_->sink = sink;
}

PlayerFacade::PlayerFacade(std::unique_ptr<IStreamResolver> resolver,
                           std::unique_ptr<IDemuxer> demuxer,
                           std::unique_ptr<IHWDecoder> decoder,
                           std::unique_ptr<IAIPipeline> aiPipeline,
                           IVideoFrameSink* sink)
    : impl_(std::make_unique<Impl>()) {
    impl_->resolver = std::move(resolver);
    impl_->demuxer = std::move(demuxer);
    impl_->decoder = std::move(decoder);
    impl_->aiPipeline = std::move(aiPipeline);
    if (!impl_->aiPipeline) {
        impl_->aiPipeline = std::make_unique<AIPipelineStub>();
    }
    impl_->sink = sink;
}

PlayerFacade::~PlayerFacade() {
    if (impl_) {
        impl_->stopAudioThread();
        std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
        if (impl_->demuxer) {
            impl_->demuxer->stop();
        }
    }
}

void PlayerFacade::setAudioDecoder(std::unique_ptr<IAudioDecoder> decoder) {
    impl_->audioDecoder = std::move(decoder);
}

void PlayerFacade::setAudioRenderer(std::unique_ptr<IAudioRenderer> renderer) {
    impl_->audioRenderer = std::move(renderer);
}

Result<void> PlayerFacade::open(const std::string& url) {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    auto openSpan = impl_->otelTelemetry.startSpan("player.open");

    // Clean up resources from previous session when not in Idle.
    // Supports recovery from Error/End states where the state machine
    // allows direct Open transition without an explicit Stop.
    PlayerState oldState = impl_->stateMachine.getState();
    if (oldState != PlayerState_Idle) {
        impl_->stopAudioThread();
        impl_->demuxer->stop();
        impl_->decoder->close();
        impl_->syncClock.reset();
        if (impl_->audioDecoder) {
            impl_->audioDecoder->close();
            impl_->audioStreamReady = false;
        }
        impl_->videoStreamReady = false;
        if (impl_->audioRenderer) {
            impl_->audioRenderer->close();
        }
        impl_->lastError.clear();
        impl_->stateMachine.reset();
    }

    auto tr = impl_->stateMachine.transition(PlayerEvent::Open);
    if (tr.hasError()) {
        impl_->otelTelemetry.endSpan(openSpan);
        return Result<void>::error(PlayerError::InvalidState);
    }
    impl_->publishStateChanged(PlayerState_Idle, PlayerState_ResolvingURL, PlayerEvent::Open);

    impl_->currentUrl = url;

    auto resolveSpan = impl_->otelTelemetry.startSpan("player.resolve");
    impl_->otelTelemetry.setAttribute(resolveSpan, "url", url);

    bool resolved = false;
    Result<StreamInfo> resolveResult = Result<StreamInfo>::error(PlayerError::Unknown);

    impl_->resolver->resolve(url, [&](Result<StreamInfo> result) {
        resolveResult = result;
        resolved = true;
    });

    if (resolved && resolveResult.hasError()) {
        impl_->otelTelemetry.endSpan(resolveSpan);
        impl_->otelTelemetry.endSpan(openSpan);
        impl_->atomicTelemetry.incrementCounter("error_count");

        PlayerState preErr = impl_->stateMachine.getState();
        impl_->stateMachine.transition(PlayerEvent::ResolveFailure);
        impl_->publishStateChanged(preErr, PlayerState_Error, PlayerEvent::ResolveFailure);
        impl_->publishError(resolveResult.error(), "Failed to resolve URL: " + url);
        return Result<void>::error(resolveResult.error());
    }

    if (resolved) {
        impl_->otelTelemetry.endSpan(resolveSpan);
        impl_->streamInfo = resolveResult.value();

        PlayerState prePrepared = impl_->stateMachine.getState();
        auto prepTr = impl_->stateMachine.transition(PlayerEvent::ResolveSuccess);
        if (prepTr.hasError()) {
            impl_->otelTelemetry.endSpan(openSpan);
            return Result<void>::error(PlayerError::InvalidState);
        }
        impl_->publishStateChanged(prePrepared, PlayerState_Prepared, PlayerEvent::ResolveSuccess);

        DemuxerConfig config;
        config.url = url;
        DemuxerCallbacks callbacks;
        callbacks.onPacket = [this](std::shared_ptr<MediaPacket> pkt) {
            if (pkt->streamType == StreamType::Video) {
                std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
                impl_->deliverVideoPacket(std::move(pkt));
            } else if (pkt->streamType == StreamType::Audio) {
                impl_->deliverAudioPacket(std::move(pkt));
            }
        };
        callbacks.onError = [this](PlayerError err, const std::string& msg) {
            std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
            impl_->atomicTelemetry.incrementCounter("error_count");
            PlayerState preErr = impl_->stateMachine.getState();
            impl_->stateMachine.transition(PlayerEvent::ErrorOccurred);
            impl_->publishStateChanged(preErr, PlayerState_Error, PlayerEvent::ErrorOccurred);
            impl_->publishError(err, msg);
            impl_->demuxer->stop();
        };
        callbacks.onEndOfStream = [this]() {
            impl_->audioQueue.finish();
            if (impl_->audioThread.joinable()) {
                impl_->audioThread.join();
            }
            std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
            PlayerState preEnd = impl_->stateMachine.getState();
            auto tr = impl_->stateMachine.transition(PlayerEvent::EndOfStream);
            if (!tr.hasError()) {
                impl_->publishStateChanged(preEnd, PlayerState_End, PlayerEvent::EndOfStream);
            }
        };
        callbacks.onStreamDetected = [this](StreamType streamType,
                                             int codecId,
                                             int width,
                                             int height,
                                             int sampleRate,
                                             int channels,
                                             const uint8_t* extraData,
                                             size_t extraDataSize) {
            std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
            if (streamType == StreamType::Video) {
                impl_->videoStreamReady = true;
                impl_->syncClock.setMaxDriftMs(50.0);
                if (!impl_->audioStreamReady) {
                    impl_->syncClock.setMode(AVSyncMode::VideoMaster);
                }
                Codec codec = Codec::Unknown;
                switch (codecId) {
                    case AV_CODEC_ID_H264: codec = Codec::H264; break;
                    case AV_CODEC_ID_HEVC: codec = Codec::HEVC; break;
                    case AV_CODEC_ID_AV1:  codec = Codec::AV1;  break;
                    default: break;
                }

                if (codec == Codec::Unknown) return;

                DecoderConfig decoderConfig;
                decoderConfig.backend = DecodeBackend::Auto;
                decoderConfig.codec = codec;
                decoderConfig.width = static_cast<uint32_t>(width);
                decoderConfig.height = static_cast<uint32_t>(height);
                if (extraData && extraDataSize > 0) {
                    decoderConfig.extradata.assign(extraData, extraData + extraDataSize);
                }

                auto openResult = impl_->decoder->open(decoderConfig);
                if (openResult.hasError()) {
                    LOG_ERROR("Decoder open failed for codec {}: {}",
                              codecId, static_cast<int>(openResult.error()));
                }

                Event resEvent{EventType::ResolutionChanged, 0.0,
                               ResolutionPayload{static_cast<uint32_t>(width),
                                                  static_cast<uint32_t>(height)}};
                impl_->eventBus.publish(resEvent);
            } else if (streamType == StreamType::Audio && impl_->audioDecoder && !impl_->audioStreamReady) {
                if (!impl_->videoStreamReady) {
                    impl_->syncClock.setMaxDriftMs(0.0);
                    impl_->syncClock.setMode(AVSyncMode::AudioMaster);
                }
                AudioFormat desiredFormat;
                desiredFormat.sampleRate = sampleRate;
                desiredFormat.channels = channels;
                desiredFormat.sampleFormat = AudioSampleFormat::S16;
                desiredFormat.bytesPerSample = 2;

                if (impl_->audioRenderer) {
                    if (!impl_->audioRenderer->open(desiredFormat)) {
                        LOG_WARN("Audio renderer open failed — audio playback disabled");
                        return;
                    }
                }

                AudioFormat targetFormat = impl_->audioRenderer ? impl_->audioRenderer->format() : desiredFormat;

                AudioDecodeConfig audioConfig;
                audioConfig.codecId = codecId;
                audioConfig.sourceSampleRate = sampleRate;
                audioConfig.sourceChannels = channels;
                audioConfig.targetFormat = targetFormat;

                if (extraData && extraDataSize > 0) {
                    audioConfig.extraData.assign(extraData, extraData + extraDataSize);
                }

                if (impl_->audioDecoder->open(audioConfig)) {
                    impl_->audioStreamReady = true;
                    if (impl_->videoStreamReady) {
                        impl_->syncClock.setMaxDriftMs(50.0);
                        impl_->syncClock.setMode(AVSyncMode::AudioMaster);
                    }
                } else {
                    LOG_WARN("Audio decoder open failed for codec {} — audio disabled", codecId);
                }
            }
        };

        auto demuxResult = impl_->demuxer->open(url, config, callbacks);
        if (demuxResult.hasError()) {
            impl_->otelTelemetry.endSpan(openSpan);
            impl_->atomicTelemetry.incrementCounter("error_count");
            PlayerState preErr = impl_->stateMachine.getState();
            impl_->stateMachine.transition(PlayerEvent::ErrorOccurred);
            impl_->publishStateChanged(preErr, PlayerState_Error, PlayerEvent::ErrorOccurred);
            impl_->publishError(demuxResult.error(), "Demuxer open failed");
            return Result<void>::error(demuxResult.error());
        }
    }

    impl_->otelTelemetry.endSpan(openSpan);
    return Result<void>::success();
}

Result<void> PlayerFacade::play() {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    PlayerState current = impl_->stateMachine.getState();

    if (current == PlayerState_Prepared) {
        PlayerState old = current;
        auto tr1 = impl_->stateMachine.transition(PlayerEvent::Play);
        if (tr1.hasError()) return Result<void>::error(PlayerError::InvalidState);
        impl_->publishStateChanged(old, PlayerState_Buffering, PlayerEvent::Play);

        impl_->startAudioThread();
        auto startResult = impl_->demuxer->start();
        if (startResult.hasError()) {
            impl_->stopAudioThread();
            impl_->atomicTelemetry.incrementCounter("error_count");
            PlayerState preErr = impl_->stateMachine.getState();
            impl_->stateMachine.transition(PlayerEvent::ErrorOccurred);
            impl_->publishStateChanged(preErr, PlayerState_Error, PlayerEvent::ErrorOccurred);
            impl_->publishError(startResult.error(), "Demuxer start failed");
            return Result<void>::error(startResult.error());
        }

        PlayerState old2 = PlayerState_Buffering;
        auto tr2 = impl_->stateMachine.transition(PlayerEvent::BufferReady);
        if (tr2.hasError()) return Result<void>::error(PlayerError::InvalidState);
        impl_->publishStateChanged(old2, PlayerState_Playing, PlayerEvent::BufferReady);

        if (impl_->audioRenderer) {
            impl_->audioRenderer->resume();
        }
        return Result<void>::success();
    }

    if (current == PlayerState_Paused) {
        PlayerState old = current;
        auto tr = impl_->stateMachine.transition(PlayerEvent::Resume);
        if (tr.hasError()) {
            tr = impl_->stateMachine.transition(PlayerEvent::Play);
            if (tr.hasError()) return Result<void>::error(PlayerError::InvalidState);
            impl_->publishStateChanged(old, PlayerState_Playing, PlayerEvent::Play);
        } else {
            impl_->publishStateChanged(old, PlayerState_Playing, PlayerEvent::Resume);
        }

        if (impl_->audioRenderer) {
            impl_->audioRenderer->resume();
        }
        return Result<void>::success();
    }

    if (current == PlayerState_Buffering) {
        impl_->startAudioThread();
        auto startResult = impl_->demuxer->start();
        if (startResult.hasError()) {
            impl_->stopAudioThread();
            impl_->atomicTelemetry.incrementCounter("error_count");
            PlayerState preErr = impl_->stateMachine.getState();
            impl_->stateMachine.transition(PlayerEvent::ErrorOccurred);
            impl_->publishStateChanged(preErr, PlayerState_Error, PlayerEvent::ErrorOccurred);
            impl_->publishError(startResult.error(), "Demuxer start failed");
            return Result<void>::error(startResult.error());
        }
        PlayerState old = current;
        auto tr = impl_->stateMachine.transition(PlayerEvent::BufferReady);
        if (tr.hasError()) return Result<void>::error(PlayerError::InvalidState);
        impl_->publishStateChanged(old, PlayerState_Playing, PlayerEvent::BufferReady);

        if (impl_->audioRenderer) {
            impl_->audioRenderer->resume();
        }
        return Result<void>::success();
    }

    return Result<void>::error(PlayerError::InvalidState);
}

Result<void> PlayerFacade::pause() {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    PlayerState current = impl_->stateMachine.getState();
    if (current != PlayerState_Playing) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    PlayerState old = current;
    auto tr = impl_->stateMachine.transition(PlayerEvent::Pause);
    if (tr.hasError()) return Result<void>::error(PlayerError::InvalidState);
    impl_->publishStateChanged(old, PlayerState_Paused, PlayerEvent::Pause);

    if (impl_->audioRenderer) {
        impl_->audioRenderer->pause();
    }
    return Result<void>::success();
}

Result<void> PlayerFacade::stop() {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    PlayerState current = impl_->stateMachine.getState();
    if (current == PlayerState_Idle) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    impl_->stopAudioThread();
    impl_->demuxer->stop();
    impl_->decoder->close();
    impl_->syncClock.reset();

    if (impl_->audioDecoder) {
        impl_->audioDecoder->close();
        impl_->audioStreamReady = false;
    }
    if (impl_->audioRenderer) {
        impl_->audioRenderer->close();
    }
    if (impl_->sink) {
        impl_->sink->reset();
    }
    impl_->videoStreamReady = false;

    PlayerState old = current;
    auto tr = impl_->stateMachine.transition(PlayerEvent::Stop);
    if (tr.hasError()) return Result<void>::error(PlayerError::InvalidState);
    impl_->publishStateChanged(old, impl_->stateMachine.getState(), PlayerEvent::Stop);
    return Result<void>::success();
}

Result<void> PlayerFacade::seek(double seconds) {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    PlayerState current = impl_->stateMachine.getState();
    if (current != PlayerState_Playing && current != PlayerState_Paused) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    impl_->atomicTelemetry.incrementCounter("seek_count");

    // Flush and clear the audio queue *before* seeking so the demux loop
    // never blocks on a full queue between signalling seekCompleted and
    // PlayerFacade::seek() returning.  This prevents a deadlock when
    // seeking while paused (SDL is not draining the renderer buffer).
    if (impl_->audioDecoder) {
        impl_->audioDecoder->flush();
    }
    impl_->audioQueue.clear();

    auto result = impl_->demuxer->seek(seconds);
    if (result.hasError()) {
        impl_->atomicTelemetry.incrementCounter("error_count");
        return result;
    }

    // Prime both clocks to the seek target.  The sync mode is AudioMaster
    // when an audio stream is present, so getClock() reads audioClock.
    // Without priming audioClock here it stays at 0 after reset(), causing
    // the position to revert to 0 as soon as the seekPending settle window
    // expires in QMLPlayer.
    impl_->syncClock.reset();
    impl_->syncClock.onVideoFrame(seconds, 0);
    impl_->syncClock.onAudioFrame(seconds, 0);

    return Result<void>::success();
}

Result<void> PlayerFacade::setVolume(double volume) {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
    impl_->volume = volume;
    if (impl_->audioRenderer) {
        impl_->audioRenderer->setVolume(volume);
    }
    return Result<void>::success();
}

PlayerState PlayerFacade::getState() const {
    return impl_->stateMachine.getState();
}

double PlayerFacade::getPosition() const {
    return impl_->syncClock.getClock();
}

double PlayerFacade::getDuration() const {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
    if (impl_->demuxer) {
        return impl_->demuxer->getDuration();
    }
    return 0.0;
}

std::string PlayerFacade::getLastError() const {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
    return impl_->lastError;
}

EventBus& PlayerFacade::eventBus() {
    return impl_->eventBus;
}

int64_t PlayerFacade::getTelemetryCounter(const std::string& name) const {
    return impl_->atomicTelemetry.getCounter(name);
}

Result<void> PlayerFacade::enableAICapability(AICapability cap) {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    if (!impl_->aiPipeline) {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }
    if (!impl_->aiPipeline->hasCapability(cap)) {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    impl_->enabledAICapabilities |= static_cast<uint32_t>(cap);
    return Result<void>::success();
}

bool PlayerFacade::isAICapabilityEnabled(AICapability cap) const {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);
    return (impl_->enabledAICapabilities & static_cast<uint32_t>(cap)) != 0;
}

Result<void> PlayerFacade::loadAIModel(const std::string& modelPath, AICapability cap) {
    std::lock_guard<std::recursive_mutex> lock(impl_->apiMutex);

    if (!impl_->aiPipeline) {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }
    return impl_->aiPipeline->loadModel(modelPath, cap);
}

} // namespace hlplayer
