#ifndef HLPLAYER_AUDIOENCODER_H
#define HLPLAYER_AUDIOENCODER_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/IVideoEncoder.h>
#include <hlplayer/Result.h>
#include <FFmpegRAII.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
typedef struct SwrContext SwrContext;
}

namespace hlplayer {

using namespace hlplayer::ffmpeg;

/// PCM to AAC audio encoder.
class HLPLAYER_CAMERA_API AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();

    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    /// Initialize the audio encoder.
    /// @param sampleRate Sample rate in Hz.
    /// @param channels Number of audio channels.
    /// @param bitrate Bitrate in bits per second.
    Result<void> init(int sampleRate, int channels, int bitrate);

    /// Encode PCM audio data (interleaved S16) to AAC packets.
    /// @param pcmData Pointer to interleaved S16 PCM data.
    /// @param frameCount Number of audio sample frames.
    Result<std::vector<EncodedPacket>> encode(const uint8_t* pcmData, int frameCount);

    /// Flush any remaining buffered data from swr and encoder.
    Result<std::vector<EncodedPacket>> flush();

    /// Close the encoder and release resources.
    void close();

    /// Check if encoder is ready for encoding.
    bool isOpen() const;

    const AVCodecContext* context() const { return codecCtx_.get(); }

private:
    AVCodecContextPtr codecCtx_;
    SwrContext* swrCtx_ = nullptr;
    AVFramePtr swrFrame_;
    int sampleRate_ = 0;
    int channels_ = 0;
    int64_t frameIndex_ = 0;
    double timeBase_ = 0.0;
    bool open_ = false;
    std::mutex mutex_;
};

/// Audio capture device information.
struct AudioDeviceInfo {
    std::string name;
    std::string devicePath;
};

/// Audio capture from DirectShow audio devices.
class HLPLAYER_CAMERA_API AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /// Enumerate available audio capture devices.
    static Result<std::vector<AudioDeviceInfo>> enumerateAudioDevices();

    /// Open an audio capture device.
    Result<void> open(const std::string& devicePath, int sampleRate, int channels);

    /// Read and decode the next audio frame from the device.
    Result<void> readFrame();

    /// Signal the capture thread to stop gracefully via interrupt callback.
    void abort();

    /// Close the capture device and release resources.
    void close();

    /// Check if capture device is open.
    bool isOpen() const;

    /// Get the most recently decoded audio frame.
    const AVFrame* getFrame() const;

    bool isAborted() const { return abortFlag_.load(); }

private:
    static bool deviceRegistered_;

    AVFormatContextPtr formatCtx_;
    AVCodecContextPtr codecCtx_;
    AVFramePtr currentFrame_;
    int audioStreamIndex_ = -1;
    bool open_ = false;
    std::atomic<bool> abortFlag_{false};
};

} // namespace hlplayer

#endif // HLPLAYER_AUDIOENCODER_H
