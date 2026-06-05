#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace accel {

template <typename T>
class BlockingQueue {
public:
    void Push(T value) {
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                return;
            }
            queue_.push(std::move(value));
        }
        changed_.notify_one();
    }

    std::optional<T> PopFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, timeout,
                          [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    void Close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::queue<T> queue_;
    bool closed_{false};
};

}  // namespace accel
