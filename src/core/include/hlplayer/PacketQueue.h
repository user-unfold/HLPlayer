#ifndef HLPLAYER_PACKETQUEUE_H
#define HLPLAYER_PACKETQUEUE_H

#include <hlplayer/Demuxer.h>
#include <hlplayer/Export.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace hlplayer {

class HLPLAYER_CORE_API PacketQueue {
public:
    explicit PacketQueue(size_t maxPackets = 200, size_t maxBytes = 20 * 1024 * 1024)
        : maxPackets_(maxPackets), maxBytes_(maxBytes) {}

    void push(std::shared_ptr<MediaPacket> pkt) {
        std::unique_lock lock(mutex_);
        while (totalBytes_ >= maxBytes_ && !shutdown_) {
            notFull_.wait(lock);
        }
        if (shutdown_) return;
        totalBytes_ += pkt->data.size();
        queue_.push(std::move(pkt));
        notEmpty_.notify_one();
    }

    std::shared_ptr<MediaPacket> pop(int timeoutMs = -1) {
        std::unique_lock lock(mutex_);
        if (timeoutMs < 0) {
            notEmpty_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        } else {
            notEmpty_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                               [this] { return !queue_.empty() || shutdown_; });
        }
        if (queue_.empty()) return nullptr;
        auto pkt = std::move(queue_.front());
        queue_.pop();
        totalBytes_ -= pkt->data.size();
        notFull_.notify_one();
        return pkt;
    }

    bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    void flush() {
        std::lock_guard lock(mutex_);
        while (!queue_.empty()) queue_.pop();
        totalBytes_ = 0;
        notFull_.notify_all();
    }

    void shutdown() {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    bool isShutdown() const {
        std::lock_guard lock(mutex_);
        return shutdown_;
    }

    void restart() {
        std::lock_guard lock(mutex_);
        shutdown_ = false;
        while (!queue_.empty()) queue_.pop();
        totalBytes_ = 0;
        notFull_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<std::shared_ptr<MediaPacket>> queue_;
    size_t totalBytes_ = 0;
    size_t maxPackets_;
    size_t maxBytes_;
    bool shutdown_ = false;
};

} // namespace hlplayer

#endif // HLPLAYER_PACKETQUEUE_H
