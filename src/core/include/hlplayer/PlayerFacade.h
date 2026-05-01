#ifndef HLPLAYER_PLAYERFACADE_H
#define HLPLAYER_PLAYERFACADE_H

#include <hlplayer/IPlayerFacade.h>
#include <hlplayer/IAIPipeline.h>
#include <hlplayer/IAudioDecoder.h>
#include <hlplayer/IAudioRenderer.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace hlplayer {

class IStreamResolver;
class IDemuxer;
class IHWDecoder;
class IVideoFrameSink;
class IAudioDecoder;
class IAudioRenderer;
class EventBus;

class HLPLAYER_CORE_API PlayerFacade : public IPlayerFacade {
public:
    PlayerFacade();

    PlayerFacade(std::unique_ptr<IStreamResolver> resolver,
                 std::unique_ptr<IDemuxer> demuxer,
                 std::unique_ptr<IHWDecoder> decoder,
                 IVideoFrameSink* sink = nullptr);

    PlayerFacade(std::unique_ptr<IStreamResolver> resolver,
                 std::unique_ptr<IDemuxer> demuxer,
                 std::unique_ptr<IHWDecoder> decoder,
                 std::unique_ptr<IAIPipeline> aiPipeline,
                 IVideoFrameSink* sink);

    ~PlayerFacade() override;

    PlayerFacade(const PlayerFacade&) = delete;
    PlayerFacade& operator=(const PlayerFacade&) = delete;
    PlayerFacade(PlayerFacade&&) = delete;
    PlayerFacade& operator=(PlayerFacade&&) = delete;

    void setAudioDecoder(std::unique_ptr<IAudioDecoder> decoder);
    void setAudioRenderer(std::unique_ptr<IAudioRenderer> renderer);

    Result<void> open(const std::string& url) override;
    Result<void> play() override;
    Result<void> pause() override;
    Result<void> stop() override;
    Result<void> seek(double seconds) override;
    Result<void> setVolume(double volume) override;
    PlayerState getState() const override;
    double getPosition() const override;
    double getDuration() const override;

    std::string getLastError() const;
    EventBus& eventBus();
    int64_t getTelemetryCounter(const std::string& name) const;

    Result<void> enableAICapability(AICapability cap);
    bool isAICapabilityEnabled(AICapability cap) const;
    Result<void> loadAIModel(const std::string& modelPath, AICapability cap);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hlplayer

#endif // HLPLAYER_PLAYERFACADE_H
