#pragma once
#include <limits>

namespace cosmo {

template <typename DataType, typename QueueType>
AlgDataQueue<DataType, QueueType>::AlgDataQueue(std::string name, size_t queMaxSize)
    : m_name(std::move(name)), m_maxSize(queMaxSize), m_isRunning(true) {
    auto uuid = util::GenerateUUID();
    LOG_INFO("Queue {}/{} Init", m_name, uuid);
    m_timePoint         = std::chrono::steady_clock::now();
    m_timePointLastData = m_timePoint;
}

template <typename DataType, typename QueueType>
AlgDataQueue<DataType, QueueType>::~AlgDataQueue() {
    Stop();
}

template <typename DataType, typename QueueType>
template <typename T>
bool AlgDataQueue<DataType, QueueType>::Insert(T&& Data, bool force) {
    return InsertChecked(std::forward<T>(Data), force, nullptr);
}

template <typename DataType, typename QueueType>
template <typename T>
bool AlgDataQueue<DataType, QueueType>::InsertIfLifecycle(T&& data, LifecycleSnapshot expected) {
    return InsertChecked(std::forward<T>(data), false, &expected);
}

template <typename DataType, typename QueueType>
template <typename T>
bool AlgDataQueue<DataType, QueueType>::InsertLatestIfLifecycle(T&& data, LifecycleSnapshot expected) {
    // Returning a native picture may take other locks. Release replaced
    // payloads only after leaving the queue mutex.
    DataType replaced;
    std::optional<QueueType> backlog;
    {
        std::lock_guard<std::mutex> lock(m_mtxQueue);
        if (!m_isRunning || !expected.running || !expected.generation ||
            expected.generation != m_lifecycleGeneration)
            return false;

        m_supersededCount += m_queue.size();
        if (m_queue.size() > 1) {
            backlog.emplace();
            m_queue.swap(*backlog);
        } else if (!m_queue.empty()) {
            replaced = std::move(m_queue.front());
            m_queue.pop();
        }
        const auto now = std::chrono::steady_clock::now();
        m_infps = m_inputFps.FpsWithFrame().second;
        m_timePointLastData = now;
        m_stats.RecordInsert(now);
        m_queue.push(std::forward<T>(data));
        m_stats.hold_count = 1;
        if (m_stats.hold_count_max < 1)
            m_stats.hold_count_max = 1;
    }
    m_cond.notify_one();
    return true;
}

template <typename DataType, typename QueueType>
template <typename T>
bool AlgDataQueue<DataType, QueueType>::InsertChecked(T&& Data, bool force,
                                                      const LifecycleSnapshot* expected) {
    {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(m_mtxQueue);
        if (!m_isRunning || (expected && (!expected->running || !expected->generation ||
                                          expected->generation != m_lifecycleGeneration))) {
            return false;
        }
        auto inputFps       = m_inputFps.FpsWithFrame();
        m_infps             = inputFps.second;
        m_timePointLastData = now;
        m_stats.RecordInsert(now);

        for (; m_queue.size() >= m_maxSize;) {
            m_stats.RecordDiscard(now);

            if (!force) {
                if (0 == m_stats.discard_count() % QueueStatistics::kLogSampleInterval)
                    LOG_INFO("{} queue is full, insert data fail. Total:{} Discard:{} Proc:{} queSize:{}",
                             m_name, m_stats.insert_count(), m_stats.discard_count(), m_stats.process_count(),
                             m_maxSize);
                return false;
            }
            m_queue.pop();
            if (0 == m_stats.discard_count() % QueueStatistics::kLogSampleInterval)
                LOG_INFO("{} queue is full, discard data. pop top.Total:{} Discard:{}", m_name,
                         m_stats.insert_count(), m_stats.discard_count());
        }
        m_queue.push(std::forward<T>(Data));
        m_stats.hold_count = m_queue.size();
        if (m_stats.hold_count_max < m_stats.hold_count) {
            m_stats.hold_count_max = m_stats.hold_count;
        }
    }
    m_cond.notify_one();
    return true;
}

template <typename DataType, typename QueueType>
DataType AlgDataQueue<DataType, QueueType>::Pop() {
    std::unique_lock<std::mutex> lock(m_mtxQueue);
    if (m_isRunning && !m_queue.empty()) {
        auto data = std::move(m_queue.front());
        m_queue.pop();
        m_stats.RecordProcess();
        return data;
    }

    return {};
}

template <typename DataType, typename QueueType>
void AlgDataQueue<DataType, QueueType>::Stop() {
    // Extract under the lock, but release payloads outside it. Native frame
    // lease destruction can call back into queue/lifecycle code.
    QueueType discarded;
    {
        std::unique_lock<std::shared_mutex> lifecycle_lock(m_lifecycleMutex);
        std::lock_guard<std::mutex> lock(m_mtxQueue);
        if (!m_isRunning)
            return;
        m_isRunning = false;
        if (m_lifecycleGeneration)
            m_lifecycleGeneration = m_lifecycleGeneration == std::numeric_limits<std::uint64_t>::max()
                                        ? 0
                                        : m_lifecycleGeneration + 1;
        LOG_INFO("{} Have Insert {} Packets, Discard {} Packet. Hold:{} Packet.", m_name,
                 m_stats.insert_count(), m_stats.discard_count(), m_stats.hold_count);
        m_queue.swap(discarded);
        if (m_supersededCount) {
            LOG_INFO("{} analysis frames superseded by a newer frame:{}", m_name, m_supersededCount);
            m_supersededCount = 0;
        }
        m_stats.hold_count = 0;
    }
    m_cond.notify_all();
}

template <typename DataType, typename QueueType>
void AlgDataQueue<DataType, QueueType>::Resume() {
    std::unique_lock<std::shared_mutex> lifecycle_lock(m_lifecycleMutex);
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    if (!m_isRunning) {
        m_isRunning = true;
        if (m_lifecycleGeneration)
            m_lifecycleGeneration = m_lifecycleGeneration == std::numeric_limits<std::uint64_t>::max()
                                        ? 0
                                        : m_lifecycleGeneration + 1;
        LOG_INFO("{} Queue Resumed", m_name);
    }
}

template <typename DataType, typename QueueType>
bool AlgDataQueue<DataType, QueueType>::IsRunning() const {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    return m_isRunning;
}

template <typename DataType, typename QueueType>
typename AlgDataQueue<DataType, QueueType>::LifecycleSnapshot AlgDataQueue<DataType, QueueType>::Lifecycle()
    const {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    return {m_lifecycleGeneration, m_isRunning};
}

template <typename DataType, typename QueueType>
typename AlgDataQueue<DataType, QueueType>::LifecycleLease
AlgDataQueue<DataType, QueueType>::AcquireLifecycle(LifecycleSnapshot expected) const {
    std::shared_lock<std::shared_mutex> lifecycle_lock(m_lifecycleMutex);
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    const bool valid = expected.running && expected.generation && m_isRunning &&
                       expected.generation == m_lifecycleGeneration;
    return LifecycleLease(std::move(lifecycle_lock), valid, this, expected.generation);
}

template <typename DataType, typename QueueType>
bool AlgDataQueue<DataType, QueueType>::CanAccept() const {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    return m_isRunning && m_queue.size() < m_maxSize;
}

template <typename DataType, typename QueueType>
const std::string& AlgDataQueue<DataType, QueueType>::Name() const {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    return m_name;
}

template <typename DataType, typename QueueType>
size_t AlgDataQueue<DataType, QueueType>::RestSize() const {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    return m_queue.size();
}

template <typename DataType, typename QueueType>
float AlgDataQueue<DataType, QueueType>::GetInFps() const {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    return m_infps;
}

// Decode packet order is incorrect, actively discard packets
template <typename DataType, typename QueueType>
void AlgDataQueue<DataType, QueueType>::RecordDiscard() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    m_stats.RecordDiscard(now);
}

template <typename DataType, typename QueueType>
bool AlgDataQueue<DataType, QueueType>::Status(AlgDataQueueInfo& status, unsigned int durationSec) {
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    if (false == m_isRunning) {
        return m_isRunning;
    }
    status.name      = m_name;
    status.queSize   = m_maxSize;
    status.queLength = m_queue.size();

    m_stats.FillStatus(status, durationSec);

    return m_isRunning;
}

template <typename DataType, typename QueueType>
void AlgDataQueue<DataType, QueueType>::SetMaxSize(size_t queSize) {
    // Hard limit: Embedded devices have limited memory, prevent infinite queue expansion causing memory
    // backlog
    constexpr size_t kQueMaxSizeHardLimit = 100;
    if (queSize > kQueMaxSizeHardLimit) {
        queSize = kQueMaxSizeHardLimit;
    }
    std::lock_guard<std::mutex> lock(m_mtxQueue);
    if (queSize != m_maxSize) {
        LOG_INFO("Queue {} Change Size From {} To {}", m_name, m_maxSize, queSize);
        m_maxSize = queSize;
    }
}

template <typename DataType, typename QueueType>
bool AlgDataQueue<DataType, QueueType>::WaitForData(int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mtxQueue);
    return m_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [this] { return !m_queue.empty() || !m_isRunning; });
}

}  // namespace cosmo
