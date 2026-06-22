// ScoreCalc — Lightweight snapshot of a single GPU device's memory usage.

#include "util/ScoreCalc.h"

#include <algorithm>

namespace cosmo {

// Score thresholds and tuning constants
constexpr size_t kContinuesDiscardThreshold = 5;  // consecutive discard seconds to trigger penalty
constexpr double kScoreBase                 = 100.0;
constexpr double kGpuMemSafetyFactor        = 0.89;  // memory headroom factor

double CalcCustomScore(double gpu_usage, int64_t gpu_mem_total, int64_t gpu_mem_avail,
                       const std::vector<GpuMemSnapshot>& gpu_devs, double discard_ratio,
                       size_t cont_discard_s) {
    // Packet-loss dominates: score > 100 when sustained discard detected
    if ((cont_discard_s >= kContinuesDiscardThreshold) && (discard_ratio > 0)) {
        return kScoreBase + discard_ratio * kScoreBase;
    }

    // Compute peak GPU memory usage across all devices
    double gpu_mem_usage = 0.0;
    if (gpu_mem_total > 0) {
        gpu_mem_usage =
            static_cast<double>(gpu_mem_total - gpu_mem_avail) / static_cast<double>(gpu_mem_total);
    }
    for (const auto& gpu_mem_unit : gpu_devs) {
        if (gpu_mem_unit.mem_total <= 0) {
            continue;
        }
        double gpu_mem_unit_usage = static_cast<double>(gpu_mem_unit.mem_total - gpu_mem_unit.mem_available) /
                                    static_cast<double>(gpu_mem_unit.mem_total);
        if (gpu_mem_unit_usage > gpu_mem_usage) {
            gpu_mem_usage = gpu_mem_unit_usage;
        }
    }

    // Clamp GPU usage to valid range to guard against anomalous driver values
    double clamped_gpu_usage = std::clamp(gpu_usage, 0.0, 1.0);
    double gpu_capacity_max  = std::max(clamped_gpu_usage, gpu_mem_usage / kGpuMemSafetyFactor) * kScoreBase;
    if (gpu_capacity_max >= kScoreBase) {
        gpu_capacity_max = kScoreBase;
    }
    return gpu_capacity_max + (discard_ratio * kScoreBase * static_cast<double>(cont_discard_s) / 2);
}

}  // namespace cosmo
