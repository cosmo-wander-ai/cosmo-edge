#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_set>

#include "service/gb28181/IGb28181SourceService.h"

namespace cosmo::service {

class Gb28181SourceServiceImpl final : public IGb28181SourceService {
public:
    bool Resolve(const std::string& source, Gb28181Source& resolved) const override;
    bool IsStreamActive(const std::string& source) override;

private:
    bool FetchActiveStreams(std::unordered_set<std::string>& activeStreams) const;

    std::mutex cache_mtx_;
    std::chrono::steady_clock::time_point cache_time_{};
    std::unordered_set<std::string> cached_active_streams_;
    bool cache_valid_{false};
};

}  // namespace cosmo::service
