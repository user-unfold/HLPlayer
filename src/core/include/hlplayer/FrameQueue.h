#ifndef HLPLAYER_FRAMEQUEUE_H
#define HLPLAYER_FRAMEQUEUE_H

#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/Export.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace hlplayer {

class HLPLAYER_CORE_API VideoFrameQueue {
public:
    explicit VideoFrameQueue(size_t capacity = 4) : queue_(capacity), capacity_(capacity) {}

    bool push(GpuFrame frame) {
        std::unique_lock lock(mutex_);
        notFull_.wait(lock, [this] { return count_ < capacity_ || shutdown_; });
        if (shutdown_) return false;
        size_t slot = (head_ + count_) % capacity_;
        queue_[slot] = std::move(frame);
        count_++;
        notEmpty_.notify_one();
        return true;
    }

    bool pop(GpuFrame& out, int timeoutMs = -1) {
        std::unique_lock lock(mutex_);
        if (timeoutMs < 0) {
            notEmpty_.wait(lock, [this] { return count_ > 0 || shutdown_; });
        } else {
            notEmpty_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                               [this] { return count_ > 0 || shutdown_; });
        }
        if (count_ == 0) return false;
        out = std::move(queue_[head_]);
        head_ = (head_ + 1) % capacity_;
        count_--;
        notFull_.notify_one();
        return true;
    }

    bool peek(GpuFrame& out) const {
        std::lock_guard lock(mutex_);
        if (count_ == 0) return false;
        out = queue_[head_];
        return true;
    }

    bool empty() const {
        std::lock_guard lock(mutex_);
        return count_ == 0;
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return count_;
    }

    void flush() {
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < count_; i++) {
            queue_[(head_ + i) % capacity_] = GpuFrame{};
        }
        head_ = 0;
        count_ = 0;
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
        for (size_t i = 0; i < count_; i++) {
            queue_[(head_ + i) % capacity_] = GpuFrame{};
        }
        head_ = 0;
        count_ = 0;
        notFull_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::vector<GpuFrame> queue_;
    size_t capacity_;
    size_t head_ = 0;
    size_t count_ = 0;
    bool shutdown_ = false;
};

} // namespace hlplayer

#endif // HLPLAYER_FRAMEQUEUE_H
