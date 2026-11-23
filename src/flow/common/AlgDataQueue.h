// Asynchronous queue

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "flow/common/QueueStatistics.h"
#include "util/FpsCalc.h"
#include "util/Log.h"
#include "util/UuidUtil.h"
#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo {

// NOTE: AlgDataQueueStatusInfoUnit and AlgDataQueueInfo are now defined
// in util/dto/AlgDataQueueTypes.h to avoid cross-layer header dependencies.

// DataType: Queue data type, QueueType: Queue type
template <typename DataType, typename QueueType = std::queue<DataType>>
class AlgDataQueue {
public:
    explicit AlgDataQueue(std::string name, size_t queMaxSize = 20);
    ~AlgDataQueue();

    void Stop();
    void Resume();

    template <typename T>
    bool Insert(T&& Data, bool force = false);

    DataType Pop();

    void SetMaxSize(size_t queSize);
    size_t GetMaxSize() const {
        std::lock_guard<std::mutex> lock(m_mtxQueue);
        return m_maxSize;
    }

    bool IsRunning() const;

    struct LifecycleSnapshot {
        std::uint64_t generation;
        bool running;
    };
    class LifecycleLease {
    public:
        LifecycleLease(LifecycleLease&&) noexcept            = default;
        LifecycleLease& operator=(LifecycleLease&&) noexcept = default;
        LifecycleLease(const LifecycleLease&)                = delete;
        LifecycleLease& operator=(const LifecycleLease&)     = delete;
        explicit operator bool() const noexcept {
            return valid_;
        }
        bool Matches(const AlgDataQueue* queue, LifecycleSnapshot expected) const noexcept {
            return valid_ && lock_.owns_lock() && owner_ == queue && expected.running &&
                   expected.generation == generation_;
        }

    private:
        friend class AlgDataQueue;
        LifecycleLease(std::shared_lock<std::shared_mutex> lock, bool valid, const AlgDataQueue* owner,
                       std::uint64_t generation)
            : lock_(std::move(lock)), valid_(valid), owner_(owner), generation_(generation) {}
        std::shared_lock<std::shared_mutex> lock_;
        bool valid_{false};
        const AlgDataQueue* owner_{nullptr};
        std::uint64_t generation_{0};
    };
    // Zero generation means the counter was exhausted and is never valid.
    LifecycleSnapshot Lifecycle() const;
    LifecycleLease AcquireLifecycle(LifecycleSnapshot expected) const;
    template <typename T>
    bool InsertIfLifecycle(T&& data, LifecycleSnapshot expected);
    // Only use for a single-source queue whose consumer may skip stale work.
    template <typename T>
    bool InsertLatestIfLifecycle(T&& data, LifecycleSnapshot expected);

    /// Snapshot whether an immediate non-forced insert can succeed. This is
    /// used by Rockchip delayed Copy-out to avoid allocating a host frame that
    /// a saturated downstream queue would discard immediately.
    bool CanAccept() const;

    const std::string& Name() const;

    size_t RestSize() const;

    float GetInFps() const;

    // Decode packet order is incorrect, actively discard packets
    void RecordDiscard();

    // Returns m_isRunning
    bool Status(AlgDataQueueInfo& status, unsigned int durationSec = 30);

    // Blocking wait for data, returns false on timeout
    bool WaitForData(int timeoutMs = 100);

private:
    template <typename T>
    bool InsertChecked(T&& data, bool force, const LifecycleSnapshot* expected);
    mutable std::mutex m_mtxQueue;
    mutable std::shared_mutex m_lifecycleMutex;
    mutable std::condition_variable m_cond;
    QueueType m_queue;
    std::string m_name;
    size_t m_maxSize;
    bool m_isRunning;
    std::uint64_t m_lifecycleGeneration{1};
    util::FpsCalc m_inputFps;
    float m_infps{0.0};
    QueueStatistics m_stats;
    std::uint64_t m_supersededCount{0};
    std::chrono::steady_clock::time_point m_timePointLastData;  // Last data time
    std::chrono::steady_clock::time_point m_timePoint;
};

}  // namespace cosmo

#include "flow/common/AlgDataQueue.inl"
