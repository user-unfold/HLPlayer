#ifndef HLPLAYER_MEDIAPLAYER_H
#define HLPLAYER_MEDIAPLAYER_H

#include <hlplayer/Export.h>
#include <hlplayer/FFPlayer.h>
#include <hlplayer/EventBus.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <variant>

namespace hlplayer {

struct MsgDoPlay {};
struct MsgDoPause {};
struct MsgDoStop {};
struct MsgDoSeek { double seconds = 0.0; };
struct MsgDoSetVolume { double volume = 1.0; };
struct MsgDoOpen { std::string url; };
struct MsgShutdown {};

using PlayerMessage = std::variant<
    MsgDoOpen, MsgDoPlay, MsgDoPause, MsgDoStop,
    MsgDoSeek, MsgDoSetVolume, MsgShutdown>;

class HLPLAYER_CORE_API MediaPlayer {
public:
    explicit MediaPlayer(std::unique_ptr<FFPlayer> player);
    ~MediaPlayer();

    void open(const std::string& url);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setVolume(double volume);

    PlayerState state() const;
    double position() const;
    double duration() const;

    FFPlayer* player() const { return player_.get(); }
    EventBus& eventBus();

private:
    void msgLoop();
    void postMessage(PlayerMessage msg);

    std::unique_ptr<FFPlayer> player_;
    std::thread msgThread_;
    std::queue<PlayerMessage> msgQueue_;
    mutable std::mutex msgMutex_;
    std::condition_variable msgCv_;
    std::atomic<bool> running_{false};
    std::atomic<double> pendingSeekPos_{-1.0};
    std::atomic<bool> seekQueued_{false};
};

} // namespace hlplayer

#endif // HLPLAYER_MEDIAPLAYER_H
