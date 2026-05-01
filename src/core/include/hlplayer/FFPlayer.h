#ifndef HLPLAYER_FFPLAYER_H
#define HLPLAYER_FFPLAYER_H

#include <hlplayer/Export.h>
#include <hlplayer/Demuxer.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IAudioDecoder.h>
#include <hlplayer/IAudioRenderer.h>
#include <hlplayer/IVideoFrameSink.h>
#include <hlplayer/PacketQueue.h>
#include <hlplayer/FrameQueue.h>
#include <hlplayer/SyncClock.h>
#include <hlplayer/VideoRefreshThread.h>
#include <hlplayer/EventBus.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace hlplayer {

class HLPLAYER_CORE_API FFPlayer {
public:
    FFPlayer();
    ~FFPlayer();

    FFPlayer(const FFPlayer&) = delete;
    FFPlayer& operator=(const FFPlayer&) = delete;

    void setDemuxer(std::unique_ptr<IDemuxer> demuxer);
    void setVideoDecoder(std::unique_ptr<IHWDecoder> decoder);
    void setAudioDecoder(std::unique_ptr<IAudioDecoder> decoder);
    void setAudioRenderer(std::unique_ptr<IAudioRenderer> renderer);
    void setVideoSink(IVideoFrameSink* sink);

    Result<void> open(const std::string& url);
    Result<void> play();
    Result<void> pause();
    Result<void> stop();
    Result<void> seek(double seconds);
    Result<void> setVolume(double volume);
    Result<void> setPlaybackRate(double rate);
    double getPlaybackRate() const;

    PlayerState getState() const;
    double getPosition() const;
    double getDuration() const;
    double getFps() const;

    EventBus& eventBus();

private:
    void videoDecodeLoop();
    void audioDecodeLoop();
    void stopInternal();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hlplayer

#endif // HLPLAYER_FFPLAYER_H
