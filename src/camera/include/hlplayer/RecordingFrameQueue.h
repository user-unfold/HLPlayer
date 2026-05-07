#pragma once

#include <hlplayer/CameraExport.h>
#include <condition_variable>
#include <mutex>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace hlplayer {

/// Bounded blocking single-producer single-consumer frame queue with backpressure.
/// Producer (capture thread) blocks on push() when full — this throttles the
/// capture loop so the dshow device buffer never overflows. Pattern follows
/// ffmpeg's tq_send() in fftools/thread_queue.c.
template<size_t Capacity>
class FrameQueue {
public:
    FrameQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i] = av_frame_alloc();
        }
    }

    ~FrameQueue() {
        shutdown();
        for (size_t i = 0; i < Capacity; ++i) {
            if (slots_[i])
                av_frame_free(&slots_[i]);
        }
    }

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    bool push(AVFrame* srcFrame) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] {
            return count_ < Capacity || shutdown_;
        });
        if (shutdown_)
            return false;

        AVFrame* dst = slots_[writeIdx_];
        av_frame_unref(dst);
        int ret = av_frame_ref(dst, srcFrame);
        if (ret < 0) {
            spdlog::error("RecordingFrameQueue: av_frame_ref failed (ret={}), dropping frame", ret);
            return true;  // don't stall producer; frame is silently dropped
        }
        writeIdx_ = (writeIdx_ + 1) % Capacity;
        ++count_;

        notEmpty_.notify_one();
        return true;
    }

    AVFrame* pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] {
            return count_ > 0 || shutdown_;
        });
        if (shutdown_ && count_ == 0)
            return nullptr;

        // Move frame data out of the slot so the producer can safely
        // reuse this slot without corrupting data the consumer still
        // holds.  The caller owns the returned frame and must free it
        // with av_frame_free().
        AVFrame* result = av_frame_alloc();
        av_frame_move_ref(result, slots_[readIdx_]);
        readIdx_ = (readIdx_ + 1) % Capacity;
        --count_;

        notFull_.notify_one();
        return result;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    bool shutdown_ = false;

    AVFrame*   slots_[Capacity] = {};
    size_t     writeIdx_ = 0;
    size_t     readIdx_  = 0;
    size_t     count_    = 0;   // guarded by mutex_
};

using RecordingFrameQueue = FrameQueue<16>;

} // namespace hlplayer
