#ifndef HLPLAYER_IMUXER_H
#define HLPLAYER_IMUXER_H

#include <hlplayer/Result.h>
#include <hlplayer/IVideoEncoder.h>
#include <cstdint>
#include <string>
#include <vector>

namespace hlplayer {

/// Configuration for media muxer.
struct MuxerConfig {
    std::string outputPath;
    std::string format = "mp4";
    std::string mimeType;
    bool fastStart = true;
};

/// Interface for media muxing.
/// Multiplexes encoded packets from multiple streams into a container format.
class HLPLAYER_CORE_API IMuxer {
public:
    virtual ~IMuxer() = default;

    /// Open and configure the muxer.
    /// @param config Muxer configuration parameters.
    /// @return Result<void>::success() on success, or an error code on failure.
    virtual Result<void> open(const MuxerConfig& config) = 0;

    /// Add a stream to the muxer.
    /// @param streamConfig Configuration for the new stream.
    /// @return Result<uint32_t> containing the stream index, or an error.
    virtual Result<uint32_t> addStream(const EncoderConfig& streamConfig) = 0;

    /// Add an audio stream for passthrough (copy without re-encoding).
    /// @param codecId FFmpeg AVCodecID (as int) for the audio codec.
    /// @param sampleRate Audio sample rate in Hz.
    /// @param channels Number of audio channels.
    /// @param extradata Codec extradata (e.g. AudioSpecificConfig for AAC).
    /// @return Result<uint32_t> containing the stream index, or an error.
    virtual Result<uint32_t> addAudioStream(int codecId, int sampleRate, int channels,
                                             const std::vector<uint8_t>& extradata) {
        (void)codecId; (void)sampleRate; (void)channels; (void)extradata;
        return Result<uint32_t>::error(PlayerError::UnsupportedFormat);
    }

    /// Write an encoded packet to the appropriate stream.
    /// @param packet Encoded packet to write.
    /// @return Result<void>::success() on success, or an error code on failure.
    virtual Result<void> writePacket(const EncodedPacket& packet) = 0;

    /// Finalize the output file and close the muxer.
    /// @return Result<void>::success() on success, or an error code on failure.
    virtual Result<void> finalize() = 0;

    /// Close the muxer and release resources.
    virtual void close() = 0;

    /// Get the current muxer configuration.
    /// @return Current muxer configuration.
    virtual MuxerConfig getConfig() const = 0;

    /// Get number of streams added to the muxer.
    /// @return Number of streams.
    virtual uint32_t getStreamCount() const = 0;

    /// Check if muxer is open and ready for writing.
    /// @return true if muxer is open, false otherwise.
    virtual bool isOpen() const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IMUXER_H
