#pragma once

#include <chrono>
#include <shared_mutex>
#include <vector>

namespace cosmo::mem {

struct BlockFreqData {
    size_t calc_cnt = 5;

    // ms
    int64_t duration = 5000;

    int weight = 50;
    int score  = 100;

    size_t fail_index = 0;
    std::vector<std::chrono::steady_clock::time_point> fail_points;

    size_t full_index = 0;
    std::vector<std::chrono::steady_clock::time_point> full_points;
};

class BlockFreqCalc {
public:
    BlockFreqCalc(size_t cnt = 5, int duration_ms = 5000);

    virtual ~BlockFreqCalc() = default;

    void MallocFail();

    void MallocFull();

    [[nodiscard]] int Score() const;

private:
    [[nodiscard]] int NodeScore(const std::vector<std::chrono::steady_clock::time_point>& tps,
                                const std::chrono::steady_clock::time_point& tp) const;

private:
    mutable std::shared_mutex mutex_;

    BlockFreqData freq_data_;
};

}  // namespace cosmo::mem
