// QueueStatistics — Queue Statistics implementation.

#include "flow/common/QueueStatistics.h"

namespace cosmo {

void QueueStatistics::UpdateBucket(std::chrono::steady_clock::time_point now,
                                   uint64_t QueueStatsBucket::*target_field) {
    int64_t timePoint = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    int64_t index     = timePoint % kRingBufferSize;

    if (timePoint == ring_[index].time_point_sec) {
        ring_[index].*target_field += 1;
    } else {
        ring_[index].insert_count   = 0;
        ring_[index].process_count  = 0;
        ring_[index].discard_count  = 0;
        ring_[index].time_point_sec = static_cast<int>(timePoint);

        ring_[index].*target_field = 1;
    }
}

void QueueStatistics::RecordInsert(std::chrono::steady_clock::time_point now) {
    insert_count_ += 1;
    UpdateBucket(now, &QueueStatsBucket::insert_count);
}

void QueueStatistics::RecordDiscard(std::chrono::steady_clock::time_point now) {
    discard_count_ += 1;
    continuous_discard_count += 1;
    if (continuous_discard_count > continuous_discard_count_max) {
        continuous_discard_count_max = continuous_discard_count;
    }
    UpdateBucket(now, &QueueStatsBucket::discard_count);
}

void QueueStatistics::RecordProcess() {
    auto now = std::chrono::steady_clock::now();
    process_count_ += 1;
    continuous_discard_count = 0;
    UpdateBucket(now, &QueueStatsBucket::process_count);
}

void QueueStatistics::Reset() {
    insert_count_                = 0;
    process_count_               = 0;
    discard_count_               = 0;
    hold_count                   = 0;
    hold_count_max               = 0;
    continuous_discard_count     = 0;
    continuous_discard_count_max = 0;
    for (int i = 0; i < kRingBufferSize; ++i) {
        ring_[i] = QueueStatsBucket{};
    }
}

void QueueStatistics::FillStatus(AlgDataQueueInfo& info, unsigned int duration_sec) const {
    if (duration_sec > kRingBufferSize) {
        duration_sec = kRingBufferSize;
    }

    info.status.insertCount  = insert_count_;
    info.status.processCount = process_count_;
    info.status.discardCount = discard_count_;

    info.status.continuousDiscardCount    = continuous_discard_count;
    info.status.continuousDiscardCountMax = continuous_discard_count_max;

    info.status.holdCount    = hold_count;
    info.status.holdCountMax = hold_count_max;

    auto now               = std::chrono::steady_clock::now();
    int64_t timePoint      = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    int64_t startTimePoint = timePoint - duration_sec;
    int64_t startIndex     = (startTimePoint > 0) ? startTimePoint : 0;

    size_t continuousDiscardCountPeriod = 0;
    int secStart                        = -1;
    int secEnd                          = -1;

    for (; startIndex < timePoint; startIndex++) {
        int64_t index = startIndex % kRingBufferSize;
        // Low-frequency action: Action without data every second
        if (ring_[index].time_point_sec < startIndex) {
            continuousDiscardCountPeriod = 0;  // Ignore packet loss of low-frequency actions
            continue;
        }

        if (secStart < 0) {
            secStart = ring_[index].time_point_sec;
        }
        secEnd = ring_[index].time_point_sec;

        info.status.discardCountPeriod += ring_[index].discard_count;
        info.status.processCountPeriod += ring_[index].process_count;
        info.status.insertCountPeriod += ring_[index].insert_count;

        if (ring_[index].discard_count > 0) {
            continuousDiscardCountPeriod += 1;
        } else {
            if (continuousDiscardCountPeriod > info.status.continuousDiscardCountPeriod) {
                info.status.continuousDiscardCountPeriod = continuousDiscardCountPeriod;
            }
            continuousDiscardCountPeriod = 0;
        }
    }

    // After loop ends, if there is continuous packet loss in last few seconds, statistics need to be updated
    if (continuousDiscardCountPeriod > info.status.continuousDiscardCountPeriod) {
        info.status.continuousDiscardCountPeriod = continuousDiscardCountPeriod;
    }
    if ((secStart > 0) && (secEnd > secStart)) {
        info.status.periodMs = (secEnd - secStart) * 1000;
    }
}

}  // namespace cosmo
