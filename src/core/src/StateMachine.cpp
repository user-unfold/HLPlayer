#include <hlplayer/StateMachine.h>
#include <hlplayer/logger.h>

namespace hlplayer {

StateMachine::StateMachine()
    : currentState_(PlayerState_Idle) {
    buildTransitionTable();
}

void StateMachine::buildTransitionTable() {
    transitions_[{PlayerState_Idle, PlayerEvent::Open}] = PlayerState_ResolvingURL;

    transitions_[{PlayerState_ResolvingURL, PlayerEvent::ResolveSuccess}] = PlayerState_Prepared;
    transitions_[{PlayerState_ResolvingURL, PlayerEvent::ResolveFailure}] = PlayerState_Error;

    transitions_[{PlayerState_Prepared, PlayerEvent::Play}] = PlayerState_Buffering;
    transitions_[{PlayerState_Prepared, PlayerEvent::BufferReady}] = PlayerState_Buffering;
    transitions_[{PlayerState_Prepared, PlayerEvent::Stop}] = PlayerState_Idle;
    transitions_[{PlayerState_Prepared, PlayerEvent::ErrorOccurred}] = PlayerState_Error;

    transitions_[{PlayerState_Buffering, PlayerEvent::Play}] = PlayerState_Playing;
    transitions_[{PlayerState_Buffering, PlayerEvent::BufferReady}] = PlayerState_Playing;
    transitions_[{PlayerState_Buffering, PlayerEvent::Pause}] = PlayerState_Paused;
    transitions_[{PlayerState_Buffering, PlayerEvent::ErrorOccurred}] = PlayerState_Error;
    transitions_[{PlayerState_Buffering, PlayerEvent::Stop}] = PlayerState_Idle;

    transitions_[{PlayerState_Playing, PlayerEvent::Pause}] = PlayerState_Paused;
    transitions_[{PlayerState_Playing, PlayerEvent::BufferReady}] = PlayerState_Buffering;
    transitions_[{PlayerState_Playing, PlayerEvent::EndOfStream}] = PlayerState_End;
    transitions_[{PlayerState_Playing, PlayerEvent::ErrorOccurred}] = PlayerState_Error;
    transitions_[{PlayerState_Playing, PlayerEvent::DeviceLost}] = PlayerState_DeviceLost;
    transitions_[{PlayerState_Playing, PlayerEvent::Stop}] = PlayerState_Idle;

    transitions_[{PlayerState_Paused, PlayerEvent::Resume}] = PlayerState_Playing;
    transitions_[{PlayerState_Paused, PlayerEvent::Play}] = PlayerState_Playing;
    transitions_[{PlayerState_Paused, PlayerEvent::Stop}] = PlayerState_Idle;
    transitions_[{PlayerState_Paused, PlayerEvent::ErrorOccurred}] = PlayerState_Error;
    transitions_[{PlayerState_Paused, PlayerEvent::DeviceLost}] = PlayerState_DeviceLost;

    transitions_[{PlayerState_Error, PlayerEvent::Stop}] = PlayerState_Idle;
    transitions_[{PlayerState_Error, PlayerEvent::Open}] = PlayerState_ResolvingURL;
    transitions_[{PlayerState_Error, PlayerEvent::ErrorOccurred}] = PlayerState_Error;
    transitions_[{PlayerState_Error, PlayerEvent::EndOfStream}] = PlayerState_Error;

    transitions_[{PlayerState_End, PlayerEvent::Stop}] = PlayerState_Idle;
    transitions_[{PlayerState_End, PlayerEvent::Open}] = PlayerState_ResolvingURL;
    transitions_[{PlayerState_End, PlayerEvent::ErrorOccurred}] = PlayerState_End;
    transitions_[{PlayerState_End, PlayerEvent::EndOfStream}] = PlayerState_End;

    transitions_[{PlayerState_DeviceLost, PlayerEvent::RecoveryComplete}] = PlayerState_ResolvingURL;
    transitions_[{PlayerState_DeviceLost, PlayerEvent::Stop}] = PlayerState_End;
}

Result<void> StateMachine::transition(PlayerEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);

    PlayerState current = currentState_.load(std::memory_order_acquire);
    TransitionKey key{current, event};
    auto it = transitions_.find(key);

    if (it == transitions_.end()) {
        LOG_WARN("State transition rejected: state={} event={} (invalid transition)",
                 static_cast<int>(current), static_cast<int>(event));
        return Result<void>::error(PlayerError::InvalidState);
    }

    PlayerState next = it->second;
    currentState_.store(next, std::memory_order_release);

    LOG_INFO("State transition: {} -> {} (event={})",
             static_cast<int>(current), static_cast<int>(next), static_cast<int>(event));

    return Result<void>::success();
}

PlayerState StateMachine::getState() const {
    return currentState_.load(std::memory_order_acquire);
}

void StateMachine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    PlayerState old = currentState_.load(std::memory_order_acquire);
    currentState_.store(PlayerState_Idle, std::memory_order_release);
    LOG_INFO("State machine reset: {} -> Idle", static_cast<int>(old));
}

bool StateMachine::isRecoverable() const {
    PlayerState state = currentState_.load(std::memory_order_acquire);
    return state == PlayerState_Error || state == PlayerState_DeviceLost;
}

} // namespace hlplayer
