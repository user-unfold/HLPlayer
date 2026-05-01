#ifndef HLPLAYER_IAUDIODECODER_H
#define HLPLAYER_IAUDIODECODER_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

#include <hlplayer/Export.h>

namespace hlplayer {

/// Portable audio sample format (maps to FFmpeg AVSampleFormat in implementation)
enum class AudioSampleFormat : int8_t {
    None = 0,
    S16,     ///< Signed 16-bit integer PCM
    S32,     ///< Signed 32-bit integer PCM
    Float,   ///< 32-bit IEEE float PCM
    U8       ///< Unsigned 8-bit integer PCM
};

/// Describes an audio stream's format parameters
struct HLPLAYER_CORE_API AudioFormat {
    int sampleRate = 0;
    int channels = 0;
    AudioSampleFormat sampleFormat = AudioSampleFormat::None;
    int bytesPerSample = 0;
};

/// Configuration for opening an audio decoder
struct HLPLAYER_CORE_API AudioDecodeConfig {
    int codecId = 0;              ///< FFmpeg AVCodecID value
    int sourceSampleRate = 0;
    int sourceChannels = 0;
    std::vector<uint8_t> extraData;
    AudioFormat targetFormat;     ///< Desired PCM output format
};

/// A decoded audio frame containing raw PCM data
struct HLPLAYER_CORE_API AudioFrame {
    std::vector<uint8_t> data;    ///< Interleaved PCM samples
    double pts = 0.0;             ///< Presentation timestamp in seconds
    double duration = 0.0;        ///< Frame duration in seconds
    int sampleRate = 0;
    int channels = 0;
    AudioSampleFormat format = AudioSampleFormat::None;
};

/// Pure virtual audio decoder interface.
/// Implementations wrap FFmpeg's libavcodec + libswresample.
class HLPLAYER_CORE_API IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    /// Open the decoder with the given codec and target output format
    virtual bool open(const AudioDecodeConfig& config) = 0;

    /// Decode compressed audio data into PCM frames.
    /// Returns decoded frame or nullptr on failure/need-more-data.
    virtual std::shared_ptr<AudioFrame> decode(const uint8_t* data, size_t size, int64_t pts) = 0;

    /// Flush the decoder (e.g. after seek)
    virtual void flush() = 0;

    /// Close the decoder and release resources
    virtual void close() = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IAUDIODECODER_H
