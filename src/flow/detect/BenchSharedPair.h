#pragma once
#include <array>
#include <string>

namespace cosmo {
// Policy only: exact task identities do not themselves authorize a model,
// native witness, output generation, or queue lifecycle.
struct BenchPairRoute {
    std::string channel;
    std::string task;
    const void* queue{};
};
inline bool IsBenchPair(const std::string& channel, const std::array<BenchPairRoute, 2>& routes) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
    if (channel.empty() || !routes[0].queue || !routes[1].queue || routes[0].queue == routes[1].queue)
        return false;
    if (routes[0].channel != channel || routes[1].channel != channel)
        return false;
    const auto person = channel + "_930927510", helmet = channel + "_930007463";
    return (routes[0].task == person && routes[1].task == helmet) ||
           (routes[1].task == person && routes[0].task == helmet);
#else
    (void)channel;
    (void)routes;
    return false;
#endif
}
inline bool IsBenchInputPair(const std::string& channel, const std::array<BenchPairRoute, 2>& routes) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
    if (channel.empty() || !routes[0].queue || routes[0].queue != routes[1].queue ||
        routes[0].channel != channel || routes[1].channel != channel)
        return false;
    const auto person = channel + "_930927510", helmet = channel + "_930007463";
    return (routes[0].task == person && routes[1].task == helmet) ||
           (routes[1].task == person && routes[0].task == helmet);
#else
    (void)channel;
    (void)routes;
    return false;
#endif
}
inline std::string BenchSibling(const std::string& channel, const std::string& task) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
    if (!channel.empty() && task == channel + "_930927510")
        return channel + "_930007463";
    if (!channel.empty() && task == channel + "_930007463")
        return channel + "_930927510";
#else
    (void)channel;
    (void)task;
#endif
    return {};
}
}  // namespace cosmo
