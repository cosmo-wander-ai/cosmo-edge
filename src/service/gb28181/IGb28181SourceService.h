#pragma once

#include <string>

namespace cosmo::service {

struct Gb28181Source {
    std::string deviceId;
    std::string logicalUrl;
    std::string mediaUrl;
};

/// Maps a GB28181 device identity onto the local media stream exposed by SRS.
class IGb28181SourceService {
public:
    virtual ~IGb28181SourceService() = default;

    virtual bool Resolve(const std::string& source, Gb28181Source& resolved) const = 0;
    virtual bool IsStreamActive(const std::string& source) = 0;
};

}  // namespace cosmo::service
