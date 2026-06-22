// Asynchronous queue

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
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
    mutable std::mutex m_mtxQueue;
    mutable std::condition_variable m_cond;
    QueueType m_queue;
    std::string m_name;
    size_t m_maxSize;
    size_t m_durationIndex{0};
    bool m_isRunning;
    util::FpsCalc m_inputFps;
    float m_infps{0.0};
    QueueStatistics m_stats;
    std::chrono::steady_clock::time_point m_timePointLastData;  // Last data time
    std::chrono::steady_clock::time_point m_timePoint;
};

}  // namespace cosmo

#include "flow/common/AlgDataQueue.inl"
