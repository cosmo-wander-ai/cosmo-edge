// BlockFreqCalc — Block Freq Calc implementation.

#include "mem/BlockFreqCalc.h"

#include "util/Log.h"

namespace cosmo::mem {

BlockFreqCalc::BlockFreqCalc(size_t cnt, int duration_ms) {
    freq_data_.calc_cnt = cnt;
    freq_data_.duration = duration_ms;
    freq_data_.weight   = 100;
    freq_data_.score    = 100;
}

void BlockFreqCalc::MallocFail() {
    auto time_point = std::chrono::steady_clock::now();
    std::lock_guard<std::shared_mutex> lock(mutex_);

    if (freq_data_.fail_points.size() < freq_data_.calc_cnt) {
        freq_data_.fail_points.push_back(time_point);
    } else {
        freq_data_.fail_points[freq_data_.fail_index % freq_data_.calc_cnt] = time_point;
    }

    freq_data_.fail_index++;
}

void BlockFreqCalc::MallocFull() {
    // BlockRecord(freq_data_.mem_full_node);
    auto time_point = std::chrono::steady_clock::now();
    std::lock_guard<std::shared_mutex> lock(mutex_);

    if (freq_data_.full_points.size() < freq_data_.calc_cnt) {
        freq_data_.full_points.push_back(time_point);
    } else {
        freq_data_.full_points[freq_data_.full_index % freq_data_.calc_cnt] = time_point;
    }

    freq_data_.full_index++;
}

int BlockFreqCalc::Score() const {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto fail_score = NodeScore(freq_data_.fail_points, now);
    auto full_score = NodeScore(freq_data_.full_points, now);

    LOG_INFO("failScore:{} memFullScore:{} Size:{} {}", fail_score, full_score, freq_data_.fail_points.size(),
             freq_data_.full_points.size());

    return std::max(fail_score, full_score);
}

int BlockFreqCalc::NodeScore(const std::vector<std::chrono::steady_clock::time_point>& points,
                             const std::chrono::steady_clock::time_point& tp) const {
    size_t count = 0;
    if (points.empty())
        return 0;

    for (const auto& time_point : points) {
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - time_point).count();
        if (diff < freq_data_.duration) {
            count += 1;
        }
    }

    double weight = 100.0 / static_cast<double>(points.size());
    double score  = weight * static_cast<double>(count);
    return static_cast<int>(score);
}

}  // namespace cosmo::mem