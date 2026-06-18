// Async processing queue with bounded capacity and backpressure.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "util/Log.h"
#include "util/Thread.h"

namespace cosmo::util {

struct AsyncQueueStatus {
    uint32_t insertCount{0};   // Total inserted nodes
    uint32_t processCount{0};  // Total processed nodes
    uint32_t discardCount{0};  // Total discarded nodes

    uint32_t insertCountPeriod{0};   // Inserted nodes in current period
    uint32_t processCountPeriod{0};  // Processed nodes in current period
    uint32_t discardCountPeriod{0};  // Discarded nodes in current period

    uint32_t continuousDiscardCount{0};     // Current continuous discard streak
    uint32_t continuousDiscardCountMax{0};  // Max continuous discard streak

    uint32_t holdCount{0};     // Current queue depth
    uint32_t holdCountMax{0};  // Peak queue depth
};

struct AsyncQueueInfo {
    std::string name;
    size_t queSize{0};
    size_t queLength{0};
    int64_t periodMs;
    AsyncQueueStatus status;
};

// DataType: element type, QueueType: underlying container type (must support push_back/pop_front)
template <typename DataType, typename QueueType = std::deque<DataType>>
class AsyncQueue : public Thread {
public:
    static constexpr size_t kDefaultMaxSize = 10;

    explicit AsyncQueue(std::string name, size_t max_queue_size = kDefaultMaxSize);
    ~AsyncQueue();

    void Stop();

    AsyncQueue& SetProcessor(std::function<void(DataType&&)> func);
    AsyncQueue& SetProcessor(std::function<void(DataType&&, size_t)> func);
    AsyncQueue& SetChecker(std::function<bool(const DataType&, const std::string&)>
                               func);  // Predicate to check if a key exists in queue

    template <typename T>
    bool Insert(T&& data, bool force = false);
    void SetMaxSize(size_t max_queue_size);

    bool IsRunning() const;
    size_t RestSize() const;

    // Populates status snapshot; returns false if queue is stopped
    bool Status(AsyncQueueInfo& status);

    bool KeyInQueue(const std::string& key);

protected:
    virtual void run() override;

private:
    mutable std::mutex queue_mtx_;
    mutable std::shared_mutex func_mtx_;
    mutable std::condition_variable cond_;
    QueueType queue_;
    std::function<void(DataType&&, size_t)> func_;
    std::function<bool(const DataType&, const std::string&)> key_in_data_func_;
    std::string name_;
    size_t max_size_;
    std::atomic<bool> is_running_{false};
    AsyncQueueStatus status_;
    std::chrono::steady_clock::time_point time_point_;

    static constexpr uint32_t kLogSampleInterval = 30;
};

template <typename DataType, typename QueueType>
AsyncQueue<DataType, QueueType>::AsyncQueue(std::string name, size_t max_queue_size)
    : Thread("AsyncQueue " + name), name_(std::move(name)), max_size_(max_queue_size), is_running_(true) {
    if (!start()) {
        is_running_ = false;
        LOG_ERRO("AsyncQueue {} Start failed: previous thread still joinable", name_);
    }
    time_point_ = std::chrono::steady_clock::now();
}

template <typename DataType, typename QueueType>
AsyncQueue<DataType, QueueType>::~AsyncQueue() {
    Stop();
    stop();
}

template <typename DataType, typename QueueType>
AsyncQueue<DataType, QueueType>& AsyncQueue<DataType, QueueType>::SetProcessor(
    std::function<void(DataType&&)> func) {
    std::lock_guard<std::shared_mutex> lock(func_mtx_);
    func_ = [func](DataType&& data, size_t) { func(std::move(data)); };
    return *this;
}

template <typename DataType, typename QueueType>
AsyncQueue<DataType, QueueType>& AsyncQueue<DataType, QueueType>::SetProcessor(
    std::function<void(DataType&&, size_t)> func) {
    std::lock_guard<std::shared_mutex> lock(func_mtx_);
    func_ = func;
    return *this;
}

template <typename DataType, typename QueueType>
AsyncQueue<DataType, QueueType>& AsyncQueue<DataType, QueueType>::SetChecker(
    std::function<bool(const DataType&, const std::string&)> func) {
    std::lock_guard<std::shared_mutex> lock(func_mtx_);
    key_in_data_func_ = func;
    return *this;
}

template <typename DataType, typename QueueType>
bool AsyncQueue<DataType, QueueType>::KeyInQueue(const std::string& key) {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    if (!is_running_ || !key_in_data_func_) {
        return false;
    }

    for (const auto& element : queue_) {
        if (key_in_data_func_(element, key)) {
            return true;
        }
    }

    return false;
}

template <typename DataType, typename QueueType>
template <typename T>
bool AsyncQueue<DataType, QueueType>::Insert(T&& data, bool force) {
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        if (!is_running_) {
            return false;
        }
        LOG_DEBUG("{} queue size: {}", name_, queue_.size());
        status_.insertCount += 1;
        status_.insertCountPeriod += 1;
        while (queue_.size() >= max_size_) {
            status_.discardCount += 1;
            status_.discardCountPeriod += 1;
            status_.continuousDiscardCount += 1;
            if (status_.continuousDiscardCount > status_.continuousDiscardCountMax) {
                status_.continuousDiscardCountMax = status_.continuousDiscardCount;
            }
            if (!force) {
                if (status_.discardCount % kLogSampleInterval == 0)
                    LOG_INFO("{} queue is full, insert data fail. Total:{} Discard:{}", name_,
                             status_.insertCount, status_.discardCount);
                return false;
            }
            queue_.pop_front();
            if (status_.discardCount % kLogSampleInterval == 0)
                LOG_INFO("{} queue is full, discard data. pop top. Total:{} Discard:{}", name_,
                         status_.insertCount, status_.discardCount);
        }
        queue_.push_back(std::forward<T>(data));
        status_.holdCount = static_cast<uint32_t>(queue_.size());
        if (status_.holdCountMax < status_.holdCount) {
            status_.holdCountMax = status_.holdCount;
        }
    }
    cond_.notify_one();
    return true;
}

template <typename DataType, typename QueueType>
void AsyncQueue<DataType, QueueType>::Stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        if (!is_running_)
            return;
        is_running_ = false;
        if (!queue_.empty()) {
            LOG_INFO("{} stopping with {} items remaining in queue (will be discarded)", name_,
                     queue_.size());
        }
    }
    {
        std::lock_guard<std::shared_mutex> lock(func_mtx_);
        func_ = nullptr;
    }
    cond_.notify_all();
}

template <typename DataType, typename QueueType>
bool AsyncQueue<DataType, QueueType>::IsRunning() const {
    return is_running_;  // atomic, no lock needed
}

template <typename DataType, typename QueueType>
size_t AsyncQueue<DataType, QueueType>::RestSize() const {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    return queue_.size();
}

template <typename DataType, typename QueueType>
bool AsyncQueue<DataType, QueueType>::Status(AsyncQueueInfo& status) {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    if (!is_running_) {
        return is_running_;
    }

    status.name      = name_;
    status.queSize   = max_size_;
    status.queLength = queue_.size();
    status.status    = status_;
    auto now         = std::chrono::steady_clock::now();
    status.periodMs  = std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point_).count();
    time_point_      = now;

    // Reset periodic counters
    status_.insertCountPeriod  = 0;
    status_.discardCountPeriod = 0;
    status_.processCountPeriod = 0;
    return is_running_;
}

template <typename DataType, typename QueueType>
void AsyncQueue<DataType, QueueType>::SetMaxSize(size_t max_queue_size) {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    max_size_ = max_queue_size;
}

template <typename DataType, typename QueueType>
void AsyncQueue<DataType, QueueType>::run() {
    LOG_INFO("thread {} maxsize:{} starting.", name_, max_size_);
    std::unique_lock<std::mutex> lock(queue_mtx_);
    while (is_running_) {
        cond_.wait(lock, [this]() { return !queue_.empty() || !is_running_; });
        if (is_running_ && !queue_.empty()) {
            auto data = std::move(queue_.front());
            queue_.pop_front();
            size_t remaining = queue_.size();
            lock.unlock();
            {
                std::shared_lock<std::shared_mutex> lockFunc(func_mtx_);
                if (func_) {
                    try {
                        func_(std::move(data), remaining);
                    } catch (const std::exception& e) {
                        LOG_ERRO("thread {} throws error: {}", name_, e.what());
                    }
                }
            }
            lock.lock();
            // Update stats after re-acquiring lock to maintain consistency
            status_.processCount += 1;
            status_.processCountPeriod += 1;
            status_.continuousDiscardCount = 0;
        }
    }
    LOG_INFO(
        "thread {} stop. Total Insert {} Nodes, Discard {} Nodes, continuousDiscardCountMax:{} Nodes, hold "
        "Max:{} Nodes, remaining:{} Nodes",
        name_, status_.insertCount, status_.discardCount, status_.continuousDiscardCountMax,
        status_.holdCountMax, queue_.size());
}

}  // namespace cosmo::util

// Backward-compat alias.
namespace cosmo {
using ::cosmo::util::AsyncQueueInfo;
using ::cosmo::util::AsyncQueueStatus;
template <typename DataType, typename QueueType = std::deque<DataType>>
using AsyncQueue = util::AsyncQueue<DataType, QueueType>;
}  // namespace cosmo
