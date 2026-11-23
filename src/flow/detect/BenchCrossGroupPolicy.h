#pragma once
#include <array>
#include <string>

#include "flow/detect/BenchDetectorPlacement.h"
#include "flow/detect/BenchSharedPair.h"
namespace cosmo {
template <class Groups>
bool AllBenchOutputGroupsPolicy(const Groups& groups) {
    if (groups.empty())
        return false;
    for (const auto& group : groups) {
        if (!group.que || group.tasks.size() != 1)
            return false;
        const auto& t = group.tasks.front();
        if (!IsScenarioBenchDetectorTask("9275710", t.channel_id, t.task_id))
            return false;
    }
    return true;
}
template <class Groups>
bool CurrentBenchPairPolicy(const std::string& channel, const Groups& groups, bool cross) {
    if (!cross && groups.size() != 2)
        return false;
    if (cross && !AllBenchOutputGroupsPolicy(groups))
        return false;
    std::array<BenchPairRoute, 2> routes{};
    std::size_t n = 0;
    for (const auto& group : groups) {
        if (group.tasks.size() != 1)
            return false;
        const auto& t = group.tasks.front();
        if (t.channel_id != channel) {
            if (cross)
                continue;
            return false;
        }
        if (n == routes.size())
            return false;
        routes[n++] = {t.channel_id, t.task_id, group.que.get()};
    }
    return n == 2 && IsBenchPair(channel, routes);
}
template <class Groups, class Queue>
bool CurrentSingleBenchOutputPolicy(const std::string& channel, const std::string& task, const Queue& queue,
                                    const Groups& groups, bool cross) {
    if (!cross || !AllBenchOutputGroupsPolicy(groups))
        return false;
    std::size_t n = 0;
    bool match    = false;
    for (const auto& g : groups)
        if (g.tasks.front().channel_id == channel) {
            ++n;
            match = g.tasks.front().task_id == task && g.que == queue;
        }
    return n == 1 && match;
}
}  // namespace cosmo
