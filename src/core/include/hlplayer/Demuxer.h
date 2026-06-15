#ifndef HLPLAYER_DEMUXER_H
#define HLPLAYER_DEMUXER_H

#include <hlplayer/Result.h>
#include <hlplayer/PlayerApi.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hlplayer {

/// Stream media type
enum class StreamType : int32_t {
    Unknown = 0,
    Video = 1,
    Audio = 2,
    Subtitle = 3
};

/// A decoded/raw media packet produced by the demuxer
struct HLPLAYER_CORE_API MediaPacket {
    StreamType streamType = StreamType::Unknown;
    std::vector<uint8_t> data;
    double pts = 0.0;        ///< presentation timestamp in seconds
    double duration = 0.0;   ///< packet duration in seconds
    bool keyframe = false;
    uint32_t size = 0;       ///< meaningful payload size (may differ from data.size())
    int seekSerial = 0;      ///< seek generation this packet belongs to (for discarding stale packets)

    MediaPacket() = default;
};

/// Demuxer configuration
struct HLPLAYER_CORE_API DemuxerConfig {
    std::string url;
    std::string format = "auto";   ///< auto, hls, dash, rtmp
    bool lowLatency = true;        ///< enable low-latency demuxing (reduced buffering)
    uint32_t bufferDurationMs = 0; ///< buffer size in ms (0 = use format default)
    uint32_t probeDurationMs = 500000; ///< format probe timeout in microseconds
    bool noBuffer = true;          ///< disable internal buffering (ideal for live streams)

    DemuxerConfig() = default;
};

/// Callbacks for demuxer events — all default to nullptr
struct HLPLAYER_CORE_API DemuxerCallbacks {
    std::function<void(StreamType /*streamType*/,
                       int /*codecId*/,
                       int /*width*/,
                       int /*height*/,
                       int /*sampleRate*/,
                       int /*channels*/,
                       const uint8_t* /*extraData*/,
                       size_t /*extraDataSize*/)> onStreamDetected;

    std::function<void(std::shared_ptr<MediaPacket>)> onPacket;

    std::function<void(PlayerError, const std::string&)> onError;

    /// Called when the demuxer reaches end-of-stream (no more packets)
    std::function<void()> onEndOfStream;

    /// Called when an encrypted .hlv file needs a password/key.
    /// keyMode: 1=password, 2=raw_key (from HlvHeader)
    /// Returns the user-entered password/key string (empty string = cancelled)
    std::function<std::string(const std::string& filePath, int keyMode)> onPasswordRequired;
};

/// Pure virtual demuxer interface
/// Implementations wrap FFmpeg (when available) or provide stub behavior.
class HLPLAYER_CORE_API IDemuxer {
public:
    virtual ~IDemuxer() = default;

    /// Open a media source with the given configuration and callbacks
    virtual Result<void> open(const std::string& url,
                              const DemuxerConfig& config,
                              DemuxerCallbacks callbacks) = 0;

    /// Start demuxing packets
    virtual Result<void> start() = 0;

    /// Stop demuxing and release resources
    virtual Result<void> stop() = 0;

    /// Seek to a position in seconds (VOD only)
    virtual Result<void> seek(double seconds) = 0;

    /// Query current player state
    virtual PlayerState getState() const = 0;

    /// Get media duration in seconds (0 if unknown or live)
    virtual double getDuration() const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_DEMUXER_H
