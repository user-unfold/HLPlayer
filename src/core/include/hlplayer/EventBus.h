#ifndef HLPLAYER_EVENTBUS_H
#define HLPLAYER_EVENTBUS_H

#include <hlplayer/PlayerApi.h>
#include <hlplayer/Result.h>
#include <hlplayer/StateMachine.h>

#include <concurrentqueue.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace hlplayer {

enum class EventType {
    StateChanged,
    Error,
    BufferLevelChanged,
    LatencyMeasured,
    ResolutionChanged
};

struct StateChangedPayload {
    PlayerState oldState;
    PlayerState newState;
    PlayerEvent event;
};

struct ErrorPayload {
    PlayerError error;
    std::string message;
};

struct BufferLevelPayload {
    uint8_t percentage;
    uint32_t durationMs;
};

struct LatencyPayload {
    double latencyMs;
};

struct ResolutionPayload {
    uint32_t width;
    uint32_t height;
};

using EventPayload = std::variant<
    StateChangedPayload,
    ErrorPayload,
    BufferLevelPayload,
    LatencyPayload,
    ResolutionPayload>;

struct Event {
    EventType type;
    double timestamp;
    EventPayload payload;
};

using EventCallback = std::function<void(const Event&)>;

class HLPLAYER_CORE_API EventBus {
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    int subscribe(EventType type, EventCallback callback);
    void unsubscribe(int subscriptionId);
    void publish(const Event& event);
    size_t dispatch();

private:
    double getCurrentTimeSeconds() const;

    moodycamel::ConcurrentQueue<Event> queue_;
    std::unordered_map<EventType, std::vector<std::pair<int, EventCallback>>> subscribers_;
    std::mutex subscribersMutex_;
    int nextSubscriptionId_;
};

} // namespace hlplayer

#endif // HLPLAYER_EVENTBUS_H
