#ifndef HLPLAYER_IAUDIORENDERER_H
#define HLPLAYER_IAUDIORENDERER_H

#include <cstdint>
#include <cstddef>

#include <hlplayer/Export.h>
#include <hlplayer/IAudioDecoder.h>

namespace hlplayer {

/// Pure virtual audio renderer interface.
/// Implementations push PCM data to an audio output device.
class HLPLAYER_CORE_API IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;

    /// Open the renderer with the given PCM format
    virtual bool open(const AudioFormat& format) = 0;

    /// Write PCM data to the audio output device.
    /// Called from the demux/decode thread.
    virtual void write(const uint8_t* data, size_t size) = 0;

    /// Pause audio output
    virtual void pause() = 0;

    /// Resume audio output
    virtual void resume() = 0;

    /// Close the renderer and release the audio device
    virtual void close() = 0;

    /// Get the active audio format (may differ from requested)
    virtual AudioFormat format() const = 0;

    /// Get the estimated latency of the audio output in milliseconds
    virtual int getLatencyMs() const = 0;

    /// Discard any buffered audio data (e.g. after a seek)
    virtual void flush() {}

    /// Set the playback volume (0.0 = silence, 1.0 = full volume)
    virtual void setVolume(double volume) { (void)volume; }
};

} // namespace hlplayer

#endif // HLPLAYER_IAUDIORENDERER_H
