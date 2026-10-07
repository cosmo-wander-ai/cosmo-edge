#pragma once

#include <algorithm>

#include "service/ai/IVisualDecisionService.h"

namespace cosmo::service {
using VisualDecisionRuns = std::vector<std::shared_ptr<VisualDecisionRun>>;

inline void NormalizeVisualRuns(VisualDecisionRuns& runs) {
    std::sort(runs.begin(), runs.end());
    runs.erase(std::unique(runs.begin(), runs.end()), runs.end());
}

inline bool VisualRunsActive(const VisualDecisionRuns& runs) {
    return std::all_of(runs.begin(), runs.end(), [](const auto& run) { return run && run->Active(); });
}

// Lock all distinct configurations in one consistent order. The callback runs
// exactly once only while every contributing run is current. As with a single
// fence, it must not re-enter any run or perform model inference.
inline bool CommitVisualRuns(VisualDecisionRuns runs, const std::function<void()>& publish) {
    NormalizeVisualRuns(runs);
    if (std::find(runs.begin(), runs.end(), nullptr) != runs.end())
        return false;
    std::function<bool(size_t)> commit = [&](size_t index) {
        if (index == runs.size()) {
            publish();
            return true;
        }
        bool published = false;
        return runs[index]->CommitIfCurrent([&] { published = commit(index + 1); }) && published;
    };
    return commit(0);
}
}  // namespace cosmo::service
