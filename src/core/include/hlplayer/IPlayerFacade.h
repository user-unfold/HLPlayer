#ifndef HLPLAYER_IPLAYERFACADE_H
#define HLPLAYER_IPLAYERFACADE_H

#include <hlplayer/Result.h>
#include <hlplayer/PlayerApi.h>
#include <string>

namespace hlplayer {

class HLPLAYER_CORE_API IPlayerFacade {
public:
    virtual ~IPlayerFacade() = default;

    virtual Result<void> open(const std::string& url) = 0;
    virtual Result<void> play() = 0;
    virtual Result<void> pause() = 0;
    virtual Result<void> stop() = 0;
    virtual Result<void> seek(double seconds) = 0;
    virtual Result<void> setVolume(double volume) = 0;
    virtual PlayerState getState() const = 0;
    virtual double getPosition() const = 0;
    virtual double getDuration() const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IPLAYERFACADE_H
