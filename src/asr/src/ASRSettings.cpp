#include <hlplayer/ASRSettings.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace hlplayer {
namespace asr {

namespace {
    // Helper to convert AudioSourceType to/from string
    std::string audioSourceTypeToString(AudioSourceType type) {
        switch (type) {
            case AudioSourceType::VideoTrack: return "VideoTrack";
            case AudioSourceType::Microphone: return "Microphone";
            case AudioSourceType::SystemAudio: return "SystemAudio";
            default: return "VideoTrack";
        }
    }

    AudioSourceType stringToAudioSourceType(const std::string& str) {
        if (str == "Microphone") return AudioSourceType::Microphone;
        if (str == "SystemAudio") return AudioSourceType::SystemAudio;
        return AudioSourceType::VideoTrack;
    }

    // Helper to convert SubtitleDisplayMode to/from string
    std::string displayModeToString(SubtitleDisplayMode mode) {
        switch (mode) {
            case SubtitleDisplayMode::OriginalOnly: return "OriginalOnly";
            case SubtitleDisplayMode::TranslationOnly: return "TranslationOnly";
            case SubtitleDisplayMode::Bilingual: return "Bilingual";
            default: return "OriginalOnly";
        }
    }

    SubtitleDisplayMode stringToDisplayMode(const std::string& str) {
        if (str == "TranslationOnly") return SubtitleDisplayMode::TranslationOnly;
        if (str == "Bilingual") return SubtitleDisplayMode::Bilingual;
        return SubtitleDisplayMode::OriginalOnly;
    }
}

ASRSettings::ASRSettings() {
    resetToDefaults();
}

ASRSettings::ASRSettings(const ASRConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    modelPath = config.modelPath;
    language = config.language;
    enableTranslation = config.enableTranslation;
    useGPU = config.useGPU;
    gpuDevice = config.gpuDevice;
    vadThreshold = config.vadThreshold;
    maxSegmentLengthMs = config.maxSegmentLengthMs;
    audioContextMs = config.audioContextMs;
    audioSource = config.audioSource;
    maxQueueSize = config.maxQueueSize;

    // UI settings use defaults
    displayMode = SubtitleDisplayMode::OriginalOnly;
    fontSize = 18;
    fontColor = "#FFFFFF";
}

ASRConfig ASRSettings::toConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ASRConfig config;
    config.modelPath = modelPath;
    config.language = language;
    config.enableTranslation = enableTranslation;
    config.useGPU = useGPU;
    config.gpuDevice = gpuDevice;
    config.vadThreshold = vadThreshold;
    config.maxSegmentLengthMs = maxSegmentLengthMs;
    config.audioContextMs = audioContextMs;
    config.audioSource = audioSource;
    config.maxQueueSize = maxQueueSize;
    return config;
}

void ASRSettings::resetToDefaults() {
    std::lock_guard<std::mutex> lock(mutex_);
    modelPath = "";
    language = "auto";
    enableTranslation = false;
    useGPU = true;
    gpuDevice = 0;
    vadThreshold = 0.6f;
    maxSegmentLengthMs = 5000;
    audioContextMs = 500;
    audioSource = AudioSourceType::VideoTrack;
    maxQueueSize = 100;
    displayMode = SubtitleDisplayMode::OriginalOnly;
    fontSize = 18;
    fontColor = "#FFFFFF";
}

std::string ASRSettings::getDefaultConfigPath() {
    // Use current working directory + "asr_config.json" for simplicity
    // This avoids Qt dependency (QStandardPaths)
    std::filesystem::path cwd = std::filesystem::current_path();
    return (cwd / "asr_config.json").string();
}

std::string ASRSettings::toJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    json j;
    j["modelPath"] = modelPath;
    j["language"] = language;
    j["enableTranslation"] = enableTranslation;
    j["useGPU"] = useGPU;
    j["gpuDevice"] = gpuDevice;
    j["vadThreshold"] = vadThreshold;
    j["maxSegmentLengthMs"] = maxSegmentLengthMs;
    j["audioContextMs"] = audioContextMs;
    j["audioSource"] = audioSourceTypeToString(audioSource);
    j["maxQueueSize"] = maxQueueSize;
    j["displayMode"] = displayModeToString(displayMode);
    j["fontSize"] = fontSize;
    j["fontColor"] = fontColor;
    return j.dump(4);
}

bool ASRSettings::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);

        std::lock_guard<std::mutex> lock(mutex_);
        if (j.contains("modelPath")) modelPath = j["modelPath"].get<std::string>();
        if (j.contains("language")) language = j["language"].get<std::string>();
        if (j.contains("enableTranslation")) enableTranslation = j["enableTranslation"].get<bool>();
        if (j.contains("useGPU")) useGPU = j["useGPU"].get<bool>();
        if (j.contains("gpuDevice")) gpuDevice = j["gpuDevice"].get<int>();
        if (j.contains("vadThreshold")) vadThreshold = j["vadThreshold"].get<float>();
        if (j.contains("maxSegmentLengthMs")) maxSegmentLengthMs = j["maxSegmentLengthMs"].get<int>();
        if (j.contains("audioContextMs")) audioContextMs = j["audioContextMs"].get<int>();
        if (j.contains("audioSource")) audioSource = stringToAudioSourceType(j["audioSource"].get<std::string>());
        if (j.contains("maxQueueSize")) maxQueueSize = j["maxQueueSize"].get<size_t>();
        if (j.contains("displayMode")) displayMode = stringToDisplayMode(j["displayMode"].get<std::string>());
        if (j.contains("fontSize")) fontSize = j["fontSize"].get<int>();
        if (j.contains("fontColor")) fontColor = j["fontColor"].get<std::string>();

        return true;
    } catch (const json::exception& e) {
        spdlog::warn("Failed to parse ASR settings JSON: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse ASR settings: {}", e.what());
        return false;
    }
}

bool ASRSettings::load(const std::string& configPath) {
    std::string path = configPath.empty() ? getDefaultConfigPath() : configPath;

    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::info("ASR settings file not found at {}, using defaults", path);
        resetToDefaults();
        return true;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    if (fromJson(content)) {
        configPath_ = path;
        return true;
    }

    spdlog::warn("Corrupt ASR settings file at {}, using defaults", path);
    resetToDefaults();
    return false;
}

bool ASRSettings::save(const std::string& configPath) {
    std::string path = configPath.empty() ? getDefaultConfigPath() : configPath;

    try {
        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Failed to open ASR settings file for writing: {}", path);
            return false;
        }

        file << toJson();
        file.close();

        configPath_ = path;
        spdlog::info("ASR settings saved to {}", path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to save ASR settings: {}", e.what());
        return false;
    }
}

} // namespace asr
} // namespace hlplayer