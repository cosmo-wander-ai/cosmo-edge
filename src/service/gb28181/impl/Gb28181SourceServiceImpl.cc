#include "service/gb28181/impl/Gb28181SourceServiceImpl.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <utility>

#include "service/detail/ServiceRegistry.h"
#include "service/network/IHttpClient.h"
#include "util/Log.h"

namespace cosmo::service {

namespace {
    constexpr const char* kScheme        = "gb28181://";
    constexpr const char* kMediaBaseUrl  = "rtmp://127.0.0.1:1936/live/";
    constexpr const char* kStreamsApiUrl = "http://127.0.0.1:1985/api/v1/streams/?start=0&count=1000";
    constexpr auto kCacheTtl             = std::chrono::milliseconds(500);

    bool IsDeviceId(const std::string& value) {
        return value.size() == 20 &&
               std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch); });
    }
}  // namespace

bool Gb28181SourceServiceImpl::Resolve(const std::string& source, Gb28181Source& resolved) const {
    std::string device_id = source;
    if (device_id.rfind(kScheme, 0) == 0) {
        device_id.erase(0, std::char_traits<char>::length(kScheme));
    }
    if (!IsDeviceId(device_id)) {
        return false;
    }

    resolved.deviceId   = device_id;
    resolved.logicalUrl = std::string(kScheme) + device_id;
    resolved.mediaUrl   = std::string(kMediaBaseUrl) + device_id;
    return true;
}

bool Gb28181SourceServiceImpl::IsStreamActive(const std::string& source) {
    Gb28181Source resolved;
    if (!Resolve(source, resolved)) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(cache_mtx_);
        if (cache_valid_ && now - cache_time_ <= kCacheTtl) {
            return cached_active_streams_.count(resolved.deviceId) != 0;
        }
    }

    // Never hold cache_mtx_ across HTTP I/O. Concurrent stale callers may perform
    // duplicate refreshes, which is preferable to serializing camera probes on the network.
    std::unordered_set<std::string> active_streams;
    if (!FetchActiveStreams(active_streams)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(cache_mtx_);
        cached_active_streams_ = std::move(active_streams);
        cache_time_            = std::chrono::steady_clock::now();
        cache_valid_           = true;
        return cached_active_streams_.count(resolved.deviceId) != 0;
    }
}

bool Gb28181SourceServiceImpl::FetchActiveStreams(std::unordered_set<std::string>& activeStreams) const {
    const auto response = ServiceRegistry::Instance().Get<IHttpClient>().Get(kStreamsApiUrl, 1, 2);
    if (response.statusCode != 200) {
        LOG_INFO("GB28181 stream status query failed with HTTP {}", response.statusCode);
        return false;
    }

    const auto root = nlohmann::json::parse(response.body, nullptr, false);
    if (root.is_discarded() || !root.is_object() || root.value("code", -1) != 0 ||
        !root.contains("streams") || !root["streams"].is_array()) {
        LOG_WARN("{}", "GB28181 stream status response is invalid");
        return false;
    }

    for (const auto& stream : root["streams"]) {
        if (!stream.is_object() || stream.value("app", std::string{}) != "live") {
            continue;
        }
        const auto publish = stream.find("publish");
        if (publish == stream.end() || !publish->is_object() || !publish->value("active", false)) {
            continue;
        }
        const auto name = stream.value("name", std::string{});
        if (IsDeviceId(name)) {
            activeStreams.insert(name);
        }
    }
    return true;
}

}  // namespace cosmo::service
