#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cosmo {

// Lightweight snapshot of a single GPU device's memory usage.
// Avoids coupling util/ to service-layer DTOs.
struct GpuMemSnapshot {
    int64_t mem_total{0};
    int64_t mem_available{0};
};

// Calculate a composite health score from GPU utilization and packet discard.
// Returns 0-100 under normal load; >100 indicates resource exhaustion.
//
// @param gpu_usage       Overall GPU utilization ratio (0.0 – 1.0)
// @param gpu_mem_total   Aggregate GPU memory total (bytes)
// @param gpu_mem_avail   Aggregate GPU memory available (bytes)
// @param gpu_devs        Per-device memory snapshots
// @param discard_ratio   Packet discard ratio (0.0 – 1.0)
// @param cont_discard_s  Consecutive seconds of sustained discard
double CalcCustomScore(double gpu_usage, int64_t gpu_mem_total, int64_t gpu_mem_avail,
                       const std::vector<GpuMemSnapshot>& gpu_devs, double discard_ratio,
                       size_t cont_discard_s);

}  // namespace cosmo
