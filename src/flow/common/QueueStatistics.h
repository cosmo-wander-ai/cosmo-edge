#pragma once

#include <chrono>
#include <cstdint>

#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo {

struct QueueStatsBucket {
    int time_point_sec{0};
    uint64_t insert_count{0};
    uint64_t process_count{0};
    uint64_t discard_count{0};
};

class QueueStatistics {
public:
    static constexpr int kRingBufferSize    = 100;
    static constexpr int kLogSampleInterval = 30;

    QueueStatistics()  = default;
    ~QueueStatistics() = default;

    void RecordInsert(std::chrono::steady_clock::time_point now);
    void RecordDiscard(std::chrono::steady_clock::time_point now);
    void RecordProcess();
    void Reset();

    void FillStatus(AlgDataQueueInfo& info, unsigned int duration_sec) const;

    uint64_t insert_count() const {
        return insert_count_;
    }
    uint64_t discard_count() const {
        return discard_count_;
    }
    uint64_t process_count() const {
        return process_count_;
    }

    uint64_t hold_count{0};
    uint64_t hold_count_max{0};
    uint64_t continuous_discard_count{0};
    uint64_t continuous_discard_count_max{0};

private:
    void UpdateBucket(std::chrono::steady_clock::time_point now, uint64_t QueueStatsBucket::*target_field);

    uint64_t insert_count_{0};
    uint64_t process_count_{0};
    uint64_t discard_count_{0};
    QueueStatsBucket ring_[kRingBufferSize]{};
};

}  // namespace cosmo
