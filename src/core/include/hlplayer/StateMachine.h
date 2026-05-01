#ifndef HLPLAYER_STATEMACHINE_H
#define HLPLAYER_STATEMACHINE_H

#include <hlplayer/PlayerApi.h>
#include <hlplayer/Result.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace hlplayer {

enum class PlayerEvent {
    Open,
    ResolveSuccess,
    ResolveFailure,
    BufferReady,
    Play,
    Pause,
    Resume,
    Stop,
    Seek,
    EndOfStream,
    ErrorOccurred,
    DeviceLost,
    RecoveryComplete
};

class HLPLAYER_CORE_API StateMachine {
public:
    StateMachine();

    Result<void> transition(PlayerEvent event);
    PlayerState getState() const;
    void reset();
    bool isRecoverable() const;

private:
    struct TransitionKey {
        PlayerState from;
        PlayerEvent event;

        bool operator==(const TransitionKey& other) const noexcept {
            return from == other.from && event == other.event;
        }
    };

    struct TransitionKeyHash {
        std::size_t operator()(const TransitionKey& key) const noexcept {
            auto h1 = std::hash<int>{}(static_cast<int>(key.from));
            auto h2 = std::hash<int>{}(static_cast<int>(key.event));
            return h1 ^ (h2 << 1);
        }
    };

    void buildTransitionTable();

    std::unordered_map<TransitionKey, PlayerState, TransitionKeyHash> transitions_;
    std::atomic<PlayerState> currentState_;
    mutable std::mutex mutex_;
};

} // namespace hlplayer

#endif // HLPLAYER_STATEMACHINE_H
