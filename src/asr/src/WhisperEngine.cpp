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
    return true;
#else
    return false;
#endif
}

bool WhisperEngine::detectSpeech(const float* frame, size_t frameSize) {
    float energy = 0.0f;
    for (size_t i = 0; i < frameSize; i++) {
        energy += frame[i] * frame[i];
    }
    energy /= static_cast<float>(frameSize);

    vadFrameCount_++;

    // During the first ~500ms (16 frames @ 30ms), calibrate noise floor.
    // Assume the beginning of the stream is non-speech (lead-in silence).
    if (vadFrameCount_ <= 16) {
        constexpr float kAlpha = 0.9f;
        noiseFloor_ = noiseFloor_ * kAlpha + energy * (1.0f - kAlpha);
        return false;
    }

    // Adapt noise floor during confirmed silence.
    // Slow decay prevents noise floor from rising when speech is present.
    constexpr float kNoiseAlpha = 0.995f;
    constexpr float kSpeechFactor = 4.0f;
    constexpr float kAbsoluteMin = 1e-5f;

    float threshold = std::max(noiseFloor_ * kSpeechFactor, kAbsoluteMin);
    bool speech = energy > threshold;

    if (!speech) {
        noiseFloor_ = noiseFloor_ * kNoiseAlpha + energy * (1.0f - kNoiseAlpha);
    }

    return speech;
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

    std::vector<float> segment;
    double segmentPts = 0.0;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (audioBuffer_.empty()) {
            bufferStartPts_ = pts;
        }

        audioBuffer_.insert(audioBuffer_.end(), samples, samples + count);

        // VAD-driven streaming segmentation.
        // Processes 480-sample (30ms @ 16kHz) frames incrementally,
        // tracks speech/silence transitions, and extracts segments at
        // natural speech boundaries instead of fixed time intervals.
        constexpr size_t kVADFrameSize = 480;           // 30ms @ 16kHz
        constexpr int kMinSilenceFrames = 7;             // 210ms silence → end segment
        constexpr int kMinSpeechFrames = 6;              // 180ms minimum speech
        constexpr size_t kMinExtractSamples = 4800;      // 300ms minimum extract

        bool shouldExtract = false;
        size_t extractEndSamples = 0;
        size_t extractSpeechStart = 0;

        while (vadState_.processedSamples + kVADFrameSize <= audioBuffer_.size()) {
            bool speechDetected = detectSpeech(
                audioBuffer_.data() + vadState_.processedSamples,
                kVADFrameSize);

            if (speechDetected) {
                if (!vadState_.inSpeech) {
                    vadState_.inSpeech = true;
                    vadState_.speechStartSample = vadState_.processedSamples;
                    vadState_.speechFrames = 0;
                }
                vadState_.speechFrames++;
                vadState_.silenceFrames = 0;
            } else if (vadState_.inSpeech) {
                vadState_.silenceFrames++;
                if (vadState_.silenceFrames >= kMinSilenceFrames) {
                    if (vadState_.speechFrames >= kMinSpeechFrames) {
                        shouldExtract = true;
                        extractEndSamples = vadState_.processedSamples;
                        extractSpeechStart = vadState_.speechStartSample;
                    }
                    vadState_.inSpeech = false;
                    vadState_.silenceFrames = 0;
                }
            }

            vadState_.processedSamples += kVADFrameSize;

            if (shouldExtract) break;
        }

        // Force-split if buffer grows too large (continuous speech without pause).
        // First segment uses a smaller threshold to reduce startup latency.
        const size_t maxBuffer = (lastRecognizedEndTime_ == 0.0)
            ? 16000 * 2   // 2s for first segment → first subtitle ~2s after enabling
            : 16000 * 4;  // 4s for subsequent segments
        if (!shouldExtract && audioBuffer_.size() >= maxBuffer) {
            shouldExtract = true;
            extractEndSamples = maxBuffer;
            extractSpeechStart = vadState_.inSpeech ? vadState_.speechStartSample : 0;
            vadState_.inSpeech = false;
            vadState_.silenceFrames = 0;
        }

        if (shouldExtract && extractEndSamples >= kMinExtractSamples) {
            // Trim leading silence so Whisper timestamps align with actual speech.
            const size_t trimStart = extractSpeechStart;
            if (trimStart >= extractEndSamples) {
                shouldExtract = false;
            } else {
                segment.assign(audioBuffer_.begin() + trimStart,
                               audioBuffer_.begin() + extractEndSamples);
                segmentPts = bufferStartPts_ + static_cast<double>(trimStart) / 16000.0;

                const size_t overlapSamples = static_cast<size_t>(config_.audioContextMs) * 16;
                const size_t consumeUpTo = extractEndSamples > overlapSamples
                    ? extractEndSamples - overlapSamples : 0;
                audioBuffer_.erase(audioBuffer_.begin(), audioBuffer_.begin() + consumeUpTo);
                bufferStartPts_ += static_cast<double>(consumeUpTo) / 16000.0;

                // Preserve noiseFloor_ so next utterance doesn't re-calibrate.
                vadState_ = VADState{};
                vadState_.processedSamples = audioBuffer_.size();
            }
        }
    }

    if (!segment.empty()) {
        spdlog::info("WhisperEngine: running inference on {} samples (pts={:.3f})",
                     segment.size(), segmentPts);
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
    lastRecognizedEndTime_ = 0.0;
    lastRecognizedText_.clear();
    vadState_ = VADState{};
    noiseFloor_ = 1e-4f;
    vadFrameCount_ = 0;

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
    wparams.single_segment = false;
    wparams.token_timestamps = true;
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
    const double audioDuration = static_cast<double>(audio.size()) / 16000.0;
    const int nSegments = whisper_full_n_segments(ctx_);
    spdlog::info("WhisperEngine: inference complete, {} segments", nSegments);
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

        // Time offsets from whisper are in centiseconds relative to the audio chunk
        int64_t t0 = whisper_full_get_segment_t0(ctx_, i);
        int64_t t1 = whisper_full_get_segment_t1(ctx_, i);

        // Use token-level t1 for more accurate endTime when token_timestamps is enabled
        const int nTokens = whisper_full_n_tokens(ctx_, i);
        if (nTokens > 0) {
            for (int ti = nTokens - 1; ti >= 0; --ti) {
                auto td = whisper_full_get_token_data(ctx_, i, ti);
                if (td.id < whisper_token_beg(ctx_)) {
                    t1 = td.t1;
                    break;
                }
            }
        }

        double segStart = basePts + static_cast<double>(t0) / 100.0;
        double segEnd = basePts + static_cast<double>(t1) / 100.0;

        // Cap to actual audio duration (whisper may pad beyond input length)
        segEnd = std::min(segEnd, basePts + audioDuration);

        // Overlap deduplication: skip segments fully within previously recognized range
        if (segEnd <= lastRecognizedEndTime_ + 0.05) {
            spdlog::info("WhisperEngine: segment {} overlaps past range ({:.3f} <= {:.3f}), skipping",
                         i, segEnd, lastRecognizedEndTime_);
            continue;
        }
        if (segStart < lastRecognizedEndTime_) {
            segStart = lastRecognizedEndTime_;
        }

        // Text dedup: trim prefix of new segment that overlaps with suffix of
        // previous segment (from audio overlap causing Whisper to re-recognise
        // same speech).  We compare UTF-8 characters, skipping punctuation.
        if (!lastRecognizedText_.empty() && !textStr.empty()) {
            // Decode both strings into UTF-8 codepoint slices (offset, byteLen).
            // Skip punctuation (CJK ，。！？、；： and ASCII ,.!?) from both
            // the trailing edge of prev and leading edge of new.
            auto utf8chars = [](const std::string& s) {
                struct CP { size_t off; size_t len; uint32_t cp; };
                std::vector<CP> out;
                for (size_t i = 0; i < s.size(); ) {
                    unsigned char c = static_cast<unsigned char>(s[i]);
                    size_t cb = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                    uint32_t cp = 0;
                    for (size_t j = 0; j < cb && i + j < s.size(); ++j)
                        cp = (cp << 8) | static_cast<unsigned char>(s[i + j]);
                    out.push_back({i, cb, cp});
                    i += cb;
                }
                return out;
            };

            auto isPunct = [](uint32_t cp) {
                return cp == 0xEFBC8C || cp == 0xE38082 || cp == 0xEFBC81 ||
                       cp == 0xEFBC9F || cp == 0xE38081 || cp == 0xE38084 ||
                       cp == 0xEFBC9B || cp == 0xEFBC9A ||
                       cp == ',' || cp == '.' || cp == '!' || cp == '?';
            };

            auto prevCPs = utf8chars(lastRecognizedText_);
            auto newCPs = utf8chars(textStr);

            // Strip trailing punctuation from prev
            size_t prevEnd = prevCPs.size();
            while (prevEnd > 0 && isPunct(prevCPs[prevEnd - 1].cp)) --prevEnd;

            // Strip leading punctuation from new
            size_t newStart = 0;
            while (newStart < newCPs.size() && isPunct(newCPs[newStart].cp)) ++newStart;

            // Find longest match: suffix of cleaned-prev == prefix of cleaned-new
            size_t matchChars = 0;
            for (size_t len = std::min(prevEnd, newCPs.size() - newStart);
                 len >= 2; --len) {
                bool ok = true;
                for (size_t k = 0; k < len; ++k) {
                    if (prevCPs[prevEnd - len + k].cp != newCPs[newStart + k].cp) {
                        ok = false;
                        break;
                    }
                }
                if (ok) { matchChars = len; break; }
            }

            if (matchChars >= 2) {
                // Erase the matched portion (including any leading punctuation) from textStr
                size_t eraseUpTo = newCPs[newStart + matchChars].off;
                spdlog::info("WhisperEngine: text dedup trimmed {} chars ({} bytes) from '{}' (matched suffix of '{}')",
                             matchChars, eraseUpTo, textStr, lastRecognizedText_);
                textStr.erase(0, eraseUpTo);

                // Trim leftover whitespace
                while (!textStr.empty() && (textStr.front() == ' ' || textStr.front() == '\t'))
                    textStr.erase(0, 1);

                // Skip if dedup leaves a meaningless fragment (< 3 CJK chars / 9 bytes)
                if (textStr.size() < 9) {
                    spdlog::info("WhisperEngine: segment {} too short after dedup ('{}'), skipping", i, textStr);
                    lastRecognizedEndTime_ = std::max(lastRecognizedEndTime_, segEnd);
                    continue;
                }
            }
        }

        lastRecognizedEndTime_ = std::max(lastRecognizedEndTime_, segEnd);

        SubtitleSegment seg;
        seg.text = textStr;
        seg.startTime = segStart;
        seg.endTime = segEnd;
        spdlog::info("WhisperEngine: segment {} text='{}' [{:.3f}-{:.3f}]", i, textStr, segStart, segEnd);

        lastRecognizedText_ = textStr;

        if (config_.language == "auto") {
            int langId = whisper_full_lang_id(ctx_);
            if (langId >= 0) {
                seg.language = whisper_lang_str(langId);
            }
        } else {
            seg.language = config_.language;
        }

        if (config_.enableTranslation) {
            seg.translation = seg.text;
        }

        // Split segments containing multiple sentences (e.g. "第一句。第二句。第三句。")
        // when the total duration exceeds 4s.  Timing is distributed proportionally
        // by UTF-8 byte count (approximation for CJK where 1 char ≈ 3 bytes).
        constexpr double kSplitDurationThreshold = 2.5;
        const double segDuration = segEnd - segStart;

        if (segDuration > kSplitDurationThreshold && textStr.size() > 6) {
            struct Sentence { size_t byteOffset; size_t byteLen; };
            std::vector<Sentence> sentences;
            size_t prevBound = 0;

            for (size_t ci = 0; ci < textStr.size(); ) {
                unsigned char c = static_cast<unsigned char>(textStr[ci]);
                size_t charBytes = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                size_t next = std::min(ci + charBytes, textStr.size());

                bool isSentEnd = false;
                if (charBytes == 3) {
                    if (ci + 2 < textStr.size()) {
                        uint32_t cp = (static_cast<uint32_t>(static_cast<unsigned char>(textStr[ci])) << 16) |
                                      (static_cast<uint32_t>(static_cast<unsigned char>(textStr[ci+1])) << 8) |
                                       static_cast<uint32_t>(static_cast<unsigned char>(textStr[ci+2]));
                        isSentEnd = (cp == 0xE38082u || cp == 0xEFBC81u || cp == 0xEFBC9Fu);
                    }
                } else if (charBytes == 1) {
                    isSentEnd = (textStr[ci] == '!' || textStr[ci] == '?');
                }

                if (isSentEnd && next < textStr.size()) {
                    size_t endPos = next;
                    sentences.push_back({prevBound, endPos - prevBound});
                    prevBound = endPos;
                }
                ci = next;
            }
            if (prevBound < textStr.size()) {
                sentences.push_back({prevBound, textStr.size() - prevBound});
            }

            if (sentences.size() > 1) {
                size_t totalBytes = textStr.size();
                double cursor = segStart;
                for (size_t si = 0; si < sentences.size(); ++si) {
                    double fraction = static_cast<double>(sentences[si].byteLen) / static_cast<double>(totalBytes);
                    double subEnd = (si == sentences.size() - 1) ? segEnd : cursor + segDuration * fraction;

                    SubtitleSegment sub;
                    sub.text = textStr.substr(sentences[si].byteOffset, sentences[si].byteLen);
                    sub.startTime = cursor;
                    sub.endTime = subEnd;
                    sub.language = seg.language;
                    sub.translation = seg.translation;
                    sub.sequenceId = nextSequenceId_++;
                    results.push_back(std::move(sub));

                    spdlog::info("WhisperEngine:   sub-segment '{}' [{:.3f}-{:.3f}]",
                                 results.back().text, cursor, subEnd);
                    cursor = subEnd;
                }
                lastRecognizedEndTime_ = std::max(lastRecognizedEndTime_, segEnd);
                lastRecognizedText_ = results.back().text;
                continue;
            }
        }

        seg.sequenceId = nextSequenceId_++;
        results.push_back(std::move(seg));
    }

    return results;
}

} // namespace asr
} // namespace hlplayer
