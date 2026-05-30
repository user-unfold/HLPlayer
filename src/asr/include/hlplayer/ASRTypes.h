#ifndef HLPLAYER_ASR_TYPES_H
#define HLPLAYER_ASR_TYPES_H

#include <hlplayer/ASRExport.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hlplayer {
namespace asr {

/// Audio source type for ASR input
enum class AudioSourceType : int8_t {
    VideoTrack = 0, ///< Audio track from video file being played
    Microphone,     ///< Microphone input via WASAPI
    SystemAudio     ///< System audio capture via WASAPI loopback
};

/// Subtitle display mode when translation is enabled
enum class SubtitleDisplayMode : int8_t {
    OriginalOnly = 0, ///< Show only recognized original text
    TranslationOnly,  ///< Show only translated text
    Bilingual         ///< Show original on top, translation below
};

/// State of the ASR engine
enum class ASRState : int8_t {
    Idle = 0,     ///< Not initialized
    Loading,      ///< Model is being loaded
    Ready,        ///< Model loaded, ready to process
    Running,      ///< Actively recognizing audio
    Paused,       ///< Temporarily paused (e.g. video paused)
    Error         ///< An error occurred
};

/// A single recognized subtitle segment with timing info
struct HLPLAYER_ASR_API SubtitleSegment {
    std::string text;           ///< Recognized original text
    std::string translation;    ///< Translated text (empty if translation disabled)
    double startTime = 0.0;     ///< Start time in seconds (relative to source)
    double endTime = 0.0;       ///< End time in seconds
    std::string language;       ///< Detected language code ("zh", "en", etc.)
    int32_t sequenceId = 0;     ///< Monotonic sequence number
};

/// Configuration for audio capture sources
struct HLPLAYER_ASR_API AudioCaptureConfig {
    AudioSourceType sourceType = AudioSourceType::VideoTrack;
    std::string deviceId;       ///< Device ID for mic/system audio (empty = default)
    int sampleRate = 16000;     ///< Target sample rate (Whisper requires 16kHz)
    int channels = 1;           ///< Target channels (Whisper requires mono)
};

/// Configuration for the ASR engine
struct HLPLAYER_ASR_API ASRConfig {
    std::string modelPath;              ///< Path to Whisper GGML model file
    std::string language = "auto";      ///< Recognition language: "zh", "en", "auto"
    bool enableTranslation = false;     ///< Enable Whisper translate mode (→ English)
    bool useGPU = true;                 ///< Use CUDA GPU acceleration if available
    int gpuDevice = 0;                  ///< CUDA device index
    float vadThreshold = 0.6f;          ///< VAD energy threshold (0.0 - 1.0)
    int maxSegmentLengthMs = 5000;      ///< Max audio segment length for recognition
    int audioContextMs = 500;           ///< Audio context overlap between segments
    AudioSourceType audioSource = AudioSourceType::VideoTrack;
    size_t maxQueueSize = 100;          ///< Max audio chunk queue size (drops oldest if exceeded)
    int cacheDurationSeconds = 300;     ///< Model cache duration after shutdown (default 5 minutes)
};

/// Information about an available Whisper model
struct HLPLAYER_ASR_API WhisperModelInfo {
    std::string name;           ///< Display name ("tiny", "base", "small")
    std::string path;           ///< Full path to model file
    int64_t sizeBytes = 0;      ///< File size in bytes
    bool available = false;     ///< Whether the file exists on disk
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_ASR_TYPES_H
