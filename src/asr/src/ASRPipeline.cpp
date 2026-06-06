#include <hlplayer/ASRPipeline.h>
#include <hlplayer/WhisperEngine.h>

#ifdef _WIN32
#include <hlplayer/WASAPICapture.h>
#include <windows.h>
#endif

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>

namespace hlplayer {
namespace asr {

ASRPipeline::ASRPipeline() = default;

ASRPipeline::~ASRPipeline() {
    if (preloadThread_.joinable()) {
        preloadThread_.join();
    }
    shutdown();
}

bool ASRPipeline::initialize(const ASRConfig& config) {
    if (preloading_.load()) {
        spdlog::info("ASRPipeline: waiting for background preload to finish...");
        while (preloading_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        spdlog::info("ASRPipeline: preload finished, reusing cached model");
    }

    if (state_.load() != ASRState::Idle) {
        spdlog::warn("ASRPipeline::initialize called in non-idle state, shutting down first");
        shutdown();
    }

    config_ = config;
    maxQueueSize_ = config.maxQueueSize;
    cacheDuration_ = std::chrono::seconds(config_.cacheDurationSeconds);

    // Defer model loading to the worker thread to avoid blocking the UI.
    // initialize() returns immediately; the worker thread loads the model
    // on start() and reports success/failure via state callbacks.
    setState(ASRState::Loading);

    if (modelCached_ && engine_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUsedTime_);

        if (elapsed < cacheDuration_) {
            spdlog::info("ASRPipeline: reusing cached model (elapsed: {}s / cache: {}s)",
                         elapsed.count(), cacheDuration_.count());
            if (engine_) {
                engine_->reset();
                engine_->updateConfig(config);
            }
            setState(ASRState::Ready);
            return true;
        } else {
            spdlog::info("ASRPipeline: cache expired (elapsed: {}s / cache: {}s), releasing",
                         elapsed.count(), cacheDuration_.count());
            releaseCache();
        }
    }

    // Model will be loaded on the worker thread in start()
    setState(ASRState::Ready);

#ifdef _WIN32
    if (config_.audioSource == AudioSourceType::Microphone) {
        capture_ = std::make_unique<WASAPICapture>();
        AudioCaptureConfig captureConfig;
        captureConfig.sourceType = AudioSourceType::Microphone;
        captureConfig.sampleRate = 16000;
        captureConfig.channels = 1;

        if (!capture_->open(captureConfig)) {
            spdlog::warn("ASRPipeline: failed to open microphone capture — capture will be unavailable");
            capture_.reset();
        } else {
            capture_->setCallback([this](const float* samples, size_t count, double pts) {
                if (!running_.load() || paused_.load()) return;

                size_t currentSize = audioQueue_.size_approx();
                while (currentSize >= maxQueueSize_) {
                    AudioChunk droppedChunk;
                    if (audioQueue_.try_dequeue(droppedChunk)) {
                        --currentSize;
                    } else {
                        break;
                    }
                }

                AudioChunk chunk;
                auto* bytePtr = reinterpret_cast<const uint8_t*>(samples);
                chunk.pcmData.assign(bytePtr, bytePtr + count * sizeof(float));
                chunk.pts = pts;
                chunk.sampleRate = 16000;
                chunk.channels = 1;
                chunk.format = AudioSampleFormat::Float;
                audioQueue_.enqueue(std::move(chunk));
            });

            spdlog::info("ASRPipeline: microphone capture ready");
        }
    }
#endif

    spdlog::info("ASRPipeline: config saved (model will load on worker thread)");
    return true;
}

bool ASRPipeline::start() {
    ASRState expected = ASRState::Ready;
    if (!state_.compare_exchange_strong(expected, ASRState::Running)) {
        if (expected == ASRState::Paused) {
            resume();
            return true;
        }
        spdlog::warn("ASRPipeline::start called in invalid state: {}", static_cast<int>(expected));
        return false;
    }

    running_.store(true);
    paused_.store(false);

    // Start the worker thread
    workerThread_ = std::thread(&ASRPipeline::workerLoop, this);

    // If using mic/system audio, start capture
    // (VideoTrack mode receives audio via feedAudioFrame — no capture needed)
    if (config_.audioSource != AudioSourceType::VideoTrack && capture_) {
        if (!capture_->start()) {
            spdlog::error("ASRPipeline: failed to start audio capture");
            stop();
            return false;
        }
    }

    setState(ASRState::Running);
    spdlog::info("ASRPipeline: started");
    return true;
}

void ASRPipeline::pause() {
    paused_.store(true);
    setState(ASRState::Paused);
    spdlog::debug("ASRPipeline: paused");
}

void ASRPipeline::resume() {
    paused_.store(false);
    setState(ASRState::Running);
    spdlog::debug("ASRPipeline: resumed");
}

void ASRPipeline::stop() {
    running_.store(false);
    paused_.store(false);

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    if (capture_ && capture_->isCapturing()) {
        capture_->stop();
    }

    setState(ASRState::Ready);
    spdlog::info("ASRPipeline: stopped");
}

void ASRPipeline::shutdown() {
    if (state_.load() == ASRState::Idle) {
        return;
    }

    // Stop processing first
    if (running_.load()) {
        stop();
    }

    if (preloadThread_.joinable()) {
        preloadThread_.join();
    }

    // Shut down capture
    if (capture_) {
        capture_->close();
        capture_.reset();
    }

    if (engine_) {
        lastUsedTime_ = std::chrono::steady_clock::now();
        modelCached_ = true;
        cacheDuration_ = std::chrono::seconds(config_.cacheDurationSeconds);
        spdlog::info("ASRPipeline: model kept in cache (duration: {}s)", config_.cacheDurationSeconds);
    }

    subtitleManager_.clear();
    resamplerConfigured_ = false;

    setState(ASRState::Idle);
    spdlog::info("ASRPipeline: shutdown complete");
}

void ASRPipeline::releaseCache() {
    if (!modelCached_ || !engine_) {
        return;
    }

    engine_->shutdown();
    engine_.reset();
    modelCached_ = false;
    spdlog::info("ASRPipeline: model cache released");
}

void ASRPipeline::reset() {
    spdlog::debug("ASRPipeline: resetting state");

    // Drain the audio queue
    AudioChunk chunk;
    while (audioQueue_.try_dequeue(chunk)) {}

    // Reset engine state
    if (engine_) {
        engine_->reset();
    }

    // Reset resampler
    resampler_.reset();

    // Reset PTS tracking
    resampledBasePts_ = -1.0;
    totalResampledSamples_ = 0;
}

void ASRPipeline::feedAudioFrame(const AudioFrame& frame) {
    if (frame.data.empty()) {
        return;
    }

    // Always enqueue audio — even before ASR is enabled.
    // This enables pre-buffering so that when the user enables subtitles,
    // the worker thread can process accumulated audio from the current
    // playback position instead of starting from scratch.
    size_t currentSize = audioQueue_.size_approx();
    while (currentSize >= maxQueueSize_) {
        AudioChunk droppedChunk;
        if (audioQueue_.try_dequeue(droppedChunk)) {
            --currentSize;
        } else {
            break;
        }
    }

    AudioChunk chunk;
    chunk.pcmData = frame.data;
    chunk.pts = frame.pts;
    chunk.sampleRate = frame.sampleRate;
    chunk.channels = frame.channels;
    chunk.format = frame.format;
    audioQueue_.enqueue(std::move(chunk));
}

void ASRPipeline::setSubtitleCallback(SubtitleCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    subtitleCallback_ = std::move(callback);
}

void ASRPipeline::setStateCallback(StateCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stateCallback_ = std::move(callback);
}

ASRState ASRPipeline::state() const {
    return state_.load();
}

std::vector<WhisperModelInfo> ASRPipeline::listAvailableModels(const std::string& modelsDir) {
    std::vector<WhisperModelInfo> models;

    // Expected model files
    struct ModelDef {
        const char* name;
        const char* filename;
    };
    static const ModelDef modelDefs[] = {
        {"tiny",            "ggml-tiny.bin"},
        {"tiny.en",         "ggml-tiny.en.bin"},
        {"base",            "ggml-base.bin"},
        {"base.en",         "ggml-base.en.bin"},
        {"small",           "ggml-small.bin"},
        {"small.en",        "ggml-small.en.bin"},
        {"medium",          "ggml-medium.bin"},
        {"medium.en",       "ggml-medium.en.bin"},
        {"large-v3-turbo",  "ggml-large-v3-turbo.bin"},
        {"large-v3",        "ggml-large-v3.bin"},
    };

    for (const auto& def : modelDefs) {
        WhisperModelInfo info;
        info.name = def.name;
        info.path = (std::filesystem::path(modelsDir) / def.filename).string();

        if (std::filesystem::exists(info.path)) {
            info.available = true;
            info.sizeBytes = static_cast<int64_t>(std::filesystem::file_size(info.path));
        }

        models.push_back(std::move(info));
    }

    return models;
}

void ASRPipeline::workerLoop() {
    spdlog::debug("ASRPipeline: worker thread started");

#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    // Load the Whisper model on this worker thread (not the UI thread).
    // This is the heavy operation that used to block the UI for 2-3 seconds.
    if (!engine_) {
        engine_ = std::make_unique<WhisperEngine>();
        if (!engine_->initialize(config_)) {
            spdlog::error("ASRPipeline: failed to initialize Whisper engine");
            setState(ASRState::Error);
            return;
        }
        spdlog::info("ASRPipeline: model loaded on worker thread");
    }

    setState(ASRState::Running);

    AudioChunk staleChunk;
    size_t dropped = 0;
    while (audioQueue_.try_dequeue(staleChunk)) {
        ++dropped;
    }
    if (dropped > 0) {
        spdlog::info("ASRPipeline: dropped {} queued chunks to sync with live audio", dropped);
    }

    while (running_.load()) {
        // If paused, sleep briefly and retry
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Dequeue audio chunks
        AudioChunk chunk;
        bool hasData = audioQueue_.try_dequeue(chunk);

        if (hasData && engine_) {
            // Resample from source format to 16kHz mono float32 (on worker thread)
            AudioFrame rawFrame;
            rawFrame.data = std::move(chunk.pcmData);
            rawFrame.pts = chunk.pts;
            rawFrame.sampleRate = chunk.sampleRate;
            rawFrame.channels = chunk.channels;
            rawFrame.format = chunk.format;

            std::vector<float> samples = resampler_.resample(rawFrame);
            if (samples.empty()) {
                continue;
            }

            if (resampledBasePts_ < 0.0) {
                resampledBasePts_ = rawFrame.pts;
            }
            double effectivePts = resampledBasePts_ + static_cast<double>(totalResampledSamples_) / 16000.0;
            totalResampledSamples_ += static_cast<int64_t>(samples.size());

            engine_->feedAudio(samples.data(), samples.size(), effectivePts);

            // Check for results
            auto results = engine_->getResults();
            if (!results.empty()) {
                // Store in subtitle manager
                subtitleManager_.addSegments(results);

                // Fire callback
                std::lock_guard<std::mutex> lock(callbackMutex_);
                if (subtitleCallback_) {
                    subtitleCallback_(results);
                }
            }
        } else if (!hasData) {
            // No data available, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Process any remaining audio in the buffer before stopping
    if (engine_) {
        auto results = engine_->getResults();
        if (!results.empty()) {
            subtitleManager_.addSegments(results);

            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (subtitleCallback_) {
                subtitleCallback_(results);
            }
        }
    }

    spdlog::debug("ASRPipeline: worker thread stopped");
}

void ASRPipeline::setState(ASRState newState) {
    ASRState oldState = state_.exchange(newState);
    if (oldState != newState) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (stateCallback_) {
            stateCallback_(oldState, newState);
        }
    }
}

size_t ASRPipeline::queueSize() const {
    return audioQueue_.size_approx();
}

void ASRPipeline::preloadModel(const ASRConfig& config, std::function<void(bool success)> onComplete) {
    if (modelCached_ && engine_) {
        spdlog::info("ASRPipeline: preloadModel skipped — model already cached");
        if (onComplete) onComplete(true);
        return;
    }
    if (preloading_.exchange(true)) {
        spdlog::info("ASRPipeline: preloadModel skipped — preload already in progress");
        return;
    }

    config_ = config;
    maxQueueSize_ = config.maxQueueSize;

    std::thread([this, onComplete]() {
        spdlog::info("ASRPipeline: background model preload started");
        auto engine = std::make_unique<WhisperEngine>();
        bool success = engine->initialize(config_);
        if (success) {
            engine_ = std::move(engine);
            modelCached_ = true;
            lastUsedTime_ = std::chrono::steady_clock::now();
            spdlog::info("ASRPipeline: background model preload complete");
        } else {
            spdlog::error("ASRPipeline: background model preload failed");
        }
        preloading_.store(false);

        if (onComplete) {
            onComplete(success);
        }
    }).detach();
}

void ASRPipeline::setQueueConfigForTesting(size_t maxQueueSize) {
    maxQueueSize_ = maxQueueSize;
    running_.store(true);
    paused_.store(false);
    setState(ASRState::Running);
    spdlog::debug("ASRPipeline: test mode configured (maxQueueSize={}, running=true)", maxQueueSize);
}

} // namespace asr
} // namespace hlplayer
