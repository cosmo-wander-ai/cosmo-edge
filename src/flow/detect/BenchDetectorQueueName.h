#pragma once

#include <string>

namespace cosmo {

inline std::string BenchDetectorQueueName(const std::string& atomic_code, const std::string& instance_id) {
    const std::string base = atomic_code + " AiDetector";
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
    if (atomic_code == "9275710") {
        return base + " " + instance_id;
    }
#else
    (void)instance_id;
#endif
    return base;
}

}  // namespace cosmo
