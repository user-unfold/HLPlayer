#ifndef HLPLAYER_ASR_SETTINGS_H
#define HLPLAYER_ASR_SETTINGS_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/ASRTypes.h>

#include <string>
#include <mutex>
#include <filesystem>

namespace hlplayer {
namespace asr {

/// ASR settings with JSON persistence capability
/// Extends ASRConfig with UI-related settings (display mode, subtitle styling)
class HLPLAYER_ASR_API ASRSettings {
public:
    /// Default constructor with all default values
    ASRSettings();

    /// Construct from ASRConfig (uses default UI settings)
    explicit ASRSettings(const ASRConfig& config);

    /// Convert to ASRConfig
    ASRConfig toConfig() const;

    /// Load settings from JSON file
    /// @param configPath Path to config file (empty = use default path)
    /// @return true if loaded successfully (or defaults used for missing file)
    bool load(const std::string& configPath = "");

    /// Save settings to JSON file
    /// @param configPath Path to config file (empty = use default path)
    /// @return true if saved successfully
    bool save(const std::string& configPath = "");

    /// Get the default config file path
    /// @return Default path based on app data location
    static std::string getDefaultConfigPath();

    /// Reset to default values
    void resetToDefaults();

    // ASRConfig fields
    std::string modelPath;
    std::string language;
    bool enableTranslation;
    bool useGPU;
    int gpuDevice;
    float vadThreshold;
    int maxSegmentLengthMs;
    int audioContextMs;
    AudioSourceType audioSource;
    size_t maxQueueSize;

    // UI-related settings
    SubtitleDisplayMode displayMode;
    int fontSize;
    std::string fontColor;

private:
    /// Thread-safe JSON serialization
    std::string toJson() const;

    /// Thread-safe JSON deserialization
    /// @param json JSON string to parse
    /// @return true if parsed successfully
    bool fromJson(const std::string& json);

    mutable std::string configPath_;  ///< Cached config path
    mutable std::mutex mutex_;       ///< Protects all settings access
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_ASR_SETTINGS_H