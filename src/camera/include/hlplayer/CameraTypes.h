#ifndef HLPLAYER_CAMERATYPES_H
#define HLPLAYER_CAMERATYPES_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/IVideoEncoder.h>
#include <cstdint>
#include <string>
#include <vector>

namespace hlplayer {

/// Resolution preset for camera capture.
struct ResolutionPreset {
    std::string label;       ///< "1080p30", "720p60", etc.
    int width;
    int height;
    int frameRate;
};

/// Camera device information.
struct CameraDeviceInfo {
    std::string name;        ///< Device display name
    std::string devicePath;  ///< Device path for avdevice (e.g. "video=USB Camera")
    std::vector<ResolutionPreset> supportedPresets;  ///< Supported resolution/fps combos
};

/// Streaming protocol.
enum class StreamingProtocol {
    RTMP,
    SRT
};

/// Recording configuration.
struct RecordingConfig {
    enum class OutputMode {
        File,    // MP4 file output only
        Stream,  // RTMP/SRT stream output only
        Both     // Simultaneous file + stream
    };

    std::string outputPath;  ///< Output MP4 file path
    std::string streamUrl;   ///< RTMP/SRT stream URL (for Stream/Both mode)
    OutputMode outputMode = OutputMode::File;
    StreamingProtocol streamProtocol = StreamingProtocol::RTMP;
    ResolutionPreset resolution;
    int videoBitrate = 4000000;   ///< 4 Mbps default
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitrate = 128000;    ///< 128 kbps default
    std::string cameraDevicePath;
    std::string micDevicePath;
};

/// Streaming configuration.
struct StreamingConfig {
    std::string url;         ///< RTMP or SRT URL
    std::string sourcePath;  ///< Local MP4 file to stream
    int maxRetries = 3;
    int retryIntervalMs = 3000;
    int videoBitrate = 4000000;
    int keyframeInterval = 2;  ///< seconds
    int audioBitrate = 128000;
};

/// Recording state.
enum class RecordingState {
    Idle,
    Recording,
    Paused,
    Stopping,
    Error
};

/// Streaming state.
enum class StreamingState {
    Idle,
    Connecting,
    Streaming,
    Completed,
    Failed,
    Cancelled
};

/// Recording statistics.
struct RecordingStats {
    double durationSeconds = 0.0;
    int64_t fileSizeBytes = 0;
    double currentFps = 0.0;
    RecordingState state = RecordingState::Idle;
};

/// Streaming statistics.
struct StreamingStats {
    double streamedDuration = 0.0;
    double totalDuration = 0.0;
    double currentBitrate = 0.0;
    double progress = 0.0;  ///< 0.0 - 1.0
    StreamingState state = StreamingState::Idle;
};

} // namespace hlplayer

#endif // HLPLAYER_CAMERATYPES_H
