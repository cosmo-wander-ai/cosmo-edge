#pragma once
#include <cstdlib>
#include <cstring>
#include <string>
namespace cosmo {
inline bool BenchmarkCrossChannelEnabled() noexcept {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
    const char* value = std::getenv("COSMO_AXERA_BENCH_CROSS_CHANNEL");
    return value && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

inline bool IsScenarioBenchDetectorTask(const std::string& atomic, const std::string& channel,
                                        const std::string& task) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
    return atomic == "9275710" && !channel.empty() &&
           (task == channel + "_930927510" || task == channel + "_930007463");
#else
    (void)atomic;
    (void)channel;
    (void)task;
    return false;
#endif
}
}  // namespace cosmo
