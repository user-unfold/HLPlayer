#include <hlplayer/WhisperEngine.h>

#include <spdlog/spdlog.h>
#include <whisper.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace hlplayer {
namespace asr {

WhisperEngine::WhisperEngine() = default;

WhisperEngine::~WhisperEngine() {
    shutdown();
}

bool WhisperEngine::isCUDAAvailable() {
#ifdef GGML_USE_CUDA
    // whisper.cpp compiled with CUDA support — assume available.
    // Actual device probing happens at context creation time.
    return true;
#else
    return false;
#endif
}

bool WhisperEngine::initialize(const ASRConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ctx_) {
        spdlog::warn("WhisperEngine::initialize called while already initialized, shutting down first");
        whisper_free(ctx_);
        ctx_ = nullptr;
    }

    state_.store(ASRState::Loading);
    config_ = config;

    // Validate model file
    if (!std::filesystem::exists(config_.modelPath)) {
        spdlog::error("Whisper model file not found: {}", config_.modelPath);
        state_.store(ASRState::Error);
        return false;
    }

    // Configure context parameters
    struct whisper_context_params cparams = whisper_context_default_params();

    // GPU acceleration: Vulkan or CUDA.
    // whisper.cpp auto-selects the best available backend when use_gpu is set.
    if (config_.useGPU) {
        cparams.use_gpu = true;
        cparams.gpu_device = config_.gpuDevice;
        spdlog::info("WhisperEngine: enabling GPU acceleration (device {})", config_.gpuDevice);
    } else {
        cparams.use_gpu = false;
        spdlog::info("WhisperEngine: using CPU (GPU disabled by config)");
    }

    // Load the model
    ctx_ = whisper_init_from_file_with_params(config_.modelPath.c_str(), cparams);

    if (!ctx_) {
        // If GPU init failed, try CPU fallback
        if (cparams.use_gpu) {
            spdlog::warn("WhisperEngine: GPU initialization failed, retrying with CPU");
            cparams.use_gpu = false;
            ctx_ = whisper_init_from_file_with_params(config_.modelPath.c_str(), cparams);
        }

        if (!ctx_) {
            spdlog::error("WhisperEngine: failed to load model from {}", config_.modelPath);
            state_.store(ASRState::Error);
            return false;
        }
    }

    spdlog::info("WhisperEngine: model loaded successfully from {}", config_.modelPath);
    state_.store(ASRState::Ready);
    return true;
}

void WhisperEngine::feedAudio(const float* samples, size_t count, double pts) {
    if (!ctx_ || state_.load() == ASRState::Error) {
        return;
    }

    // Extract segment for inference under lock, then release before
    // running the expensive whisper_full() call to avoid blocking
    // audio buffer updates during inference.
    std::vector<float> segment;
    double segmentPts = 0.0;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (audioBuffer_.empty()) {
            bufferStartPts_ = pts;
        }

        audioBuffer_.insert(audioBuffer_.end(), samples, samples + count);

        const size_t maxSamples = static_cast<size_t>(config_.maxSegmentLengthMs) * 16;

        if (audioBuffer_.size() >= maxSamples) {
            segment.assign(audioBuffer_.begin(), audioBuffer_.begin() + maxSamples);
            segmentPts = bufferStartPts_;

            const size_t overlapSamples = static_cast<size_t>(config_.audioContextMs) * 16;
            if (audioBuffer_.size() > overlapSamples) {
                audioBuffer_.erase(audioBuffer_.begin(), audioBuffer_.begin() + (maxSamples - overlapSamples));
                bufferStartPts_ = segmentPts + static_cast<double>(maxSamples - overlapSamples) / 16000.0;
            } else {
                audioBuffer_.clear();
            }
        }
    }

    if (!segment.empty()) {
        spdlog::info("WhisperEngine: running inference on {} samples (pts={:.3f})", segment.size(), segmentPts);
        auto results = runInference(segment, segmentPts);
        if (!results.empty()) {
            std::lock_guard<std::mutex> rlock(resultsMutex_);
            pendingResults_.insert(pendingResults_.end(), results.begin(), results.end());
        }
    }
}

std::vector<SubtitleSegment> WhisperEngine::getResults() {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    std::vector<SubtitleSegment> results;
    results.swap(pendingResults_);
    return results;
}

void WhisperEngine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    audioBuffer_.clear();
    bufferStartPts_ = 0.0;

    std::lock_guard<std::mutex> rlock(resultsMutex_);
    pendingResults_.clear();
}

void WhisperEngine::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
    audioBuffer_.clear();
    state_.store(ASRState::Idle);

    std::lock_guard<std::mutex> rlock(resultsMutex_);
    pendingResults_.clear();
}

bool WhisperEngine::isReady() const {
    return ctx_ != nullptr && state_.load() == ASRState::Ready;
}

void WhisperEngine::updateConfig(const ASRConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    audioBuffer_.clear();
    bufferStartPts_ = 0.0;
}

ASRState WhisperEngine::state() const {
    return state_.load();
}

std::vector<SubtitleSegment> WhisperEngine::runInference(const std::vector<float>& audio, double basePts) {
    std::vector<SubtitleSegment> results;

    if (!ctx_ || audio.empty()) {
        return results;
    }

    // Check VAD — simple energy-based detection
    // Compute RMS energy of the audio segment
    double energy = 0.0;
    for (const float sample : audio) {
        energy += static_cast<double>(sample) * sample;
    }
    energy = std::sqrt(energy / static_cast<double>(audio.size()));

    // Skip silent segments
    const float silenceThreshold = 0.01f * config_.vadThreshold;
    if (energy < silenceThreshold) {
        spdlog::info("WhisperEngine: skipping silent segment (energy={:.6f}, threshold={:.6f})", energy, silenceThreshold);
        return results;
    }

    // Configure Whisper inference parameters
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    // Language setting
    if (config_.language == "auto") {
        wparams.language = nullptr; // auto-detect
        wparams.detect_language = true;
    } else {
        wparams.language = config_.language.c_str();
        wparams.detect_language = false;
    }

    // Translation mode
    wparams.translate = config_.enableTranslation;

    // Performance settings for real-time use.
    // Whisper on CPU is memory-bandwidth-bound, not compute-bound.
    // Using too many threads causes cache thrashing and actually slows down
    // inference. 3-4 threads is the sweet spot for base/small models.
    // Combined with BELOW_NORMAL priority on the ASR worker thread,
    // this leaves enough CPU headroom for video decode, audio render, and UI.
    int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
    wparams.n_threads = std::min(std::max(2, hwThreads / 2), 4);
    wparams.no_timestamps = false;
    wparams.single_segment = true;
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_special = false;
    wparams.print_timestamps = false;
    wparams.suppress_blank = true;
    wparams.suppress_nst = true;

    if (config_.language == "zh") {
        wparams.initial_prompt = "以下是普通话的句子。";
        wparams.carry_initial_prompt = true;
    }

    wparams.no_speech_thold = config_.vadThreshold;
    wparams.entropy_thold = 2.4f;
    wparams.logprob_thold = -0.8f;
    wparams.greedy.best_of = 5;

    // Run inference
    state_.store(ASRState::Running);
    int ret = whisper_full(ctx_, wparams, audio.data(), static_cast<int>(audio.size()));
    state_.store(ASRState::Ready);

    if (ret != 0) {
        spdlog::warn("WhisperEngine: inference failed with code {}", ret);
        return results;
    }

    // Extract results
    const int nSegments = whisper_full_n_segments(ctx_);
    spdlog::info("WhisperEngine: inference complete, {} segments (energy={:.6f})", nSegments, energy);
    for (int i = 0; i < nSegments; ++i) {
        const char* text = whisper_full_get_segment_text(ctx_, i);
        if (!text || text[0] == '\0') {
            spdlog::info("WhisperEngine: segment {} empty, skipping", i);
            continue;
        }

        // Skip segments that are likely noise (e.g. "[BLANK_AUDIO]", "(music)")
        std::string textStr(text);
        if (textStr.front() == '[' || textStr.front() == '(') {
            spdlog::info("WhisperEngine: segment {} looks like noise: '{}', skipping", i, textStr);
            continue;
        }

        // Skip common video watermark/credit text that Whisper hallucinates
        if (textStr.find("字幕志愿者") != std::string::npos ||
            textStr.find("字幕制作") != std::string::npos ||
            textStr.find("字幕组") != std::string::npos ||
            textStr.find("谢谢观看") != std::string::npos ||
            textStr.find("感谢观看") != std::string::npos) {
            spdlog::info("WhisperEngine: segment {} looks like watermark/credits: '{}', skipping", i, textStr);
            continue;
        }

        SubtitleSegment seg;
        seg.text = textStr;
        spdlog::info("WhisperEngine: segment {} text='{}'", i, textStr);

        // Time offsets from whisper are in centiseconds relative to the audio chunk
        int64_t t0 = whisper_full_get_segment_t0(ctx_, i);
        int64_t t1 = whisper_full_get_segment_t1(ctx_, i);
        seg.startTime = basePts + static_cast<double>(t0) / 100.0;
        seg.endTime = basePts + static_cast<double>(t1) / 100.0;

        // Language detection result
        if (config_.language == "auto") {
            int langId = whisper_full_lang_id(ctx_);
            if (langId >= 0) {
                seg.language = whisper_lang_str(langId);
            }
        } else {
            seg.language = config_.language;
        }

        // If translation is enabled, the text IS the translation (Whisper translate outputs English).
        // We need a separate pass for original text. For simplicity in Phase 1,
        // when translate mode is on, text contains the English translation
        // and we store it in both fields.
        if (config_.enableTranslation) {
            seg.translation = seg.text;
            // Original text would require a second inference pass without translate.
            // This is deferred to a later optimization phase.
        }

        seg.sequenceId = nextSequenceId_++;
        results.push_back(std::move(seg));
    }

    return results;
}

} // namespace asr
} // namespace hlplayer
