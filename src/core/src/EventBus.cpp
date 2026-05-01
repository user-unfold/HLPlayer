#include <hlplayer/EventBus.h>
#include <hlplayer/logger.h>

#include <chrono>

namespace hlplayer {

EventBus::EventBus()
    : nextSubscriptionId_(0) {}

EventBus::~EventBus() = default;

int EventBus::subscribe(EventType type, EventCallback callback) {
    std::lock_guard<std::mutex> lock(subscribersMutex_);
    int id = ++nextSubscriptionId_;
    subscribers_[type].emplace_back(id, std::move(callback));
    return id;
}

void EventBus::unsubscribe(int subscriptionId) {
    std::lock_guard<std::mutex> lock(subscribersMutex_);
    for (auto& [type, callbacks] : subscribers_) {
        auto it = std::find_if(callbacks.begin(), callbacks.end(),
            [subscriptionId](const std::pair<int, EventCallback>& entry) {
                return entry.first == subscriptionId;
            });
        if (it != callbacks.end()) {
            callbacks.erase(it);
            return;
        }
    }
}

void EventBus::publish(const Event& event) {
    Event e = event;
    e.timestamp = getCurrentTimeSeconds();
    queue_.enqueue(std::move(e));
}

size_t EventBus::dispatch() {
    std::vector<Event> events;
    Event e;
    while (queue_.try_dequeue(e)) {
        events.push_back(std::move(e));
    }

    if (events.empty()) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(subscribersMutex_);

    for (const auto& event : events) {
        auto it = subscribers_.find(event.type);
        if (it != subscribers_.end()) {
            for (const auto& [id, callback] : it->second) {
                callback(event);
            }
        }
    }

    return events.size();
}

double EventBus::getCurrentTimeSeconds() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

} // namespace hlplayer
