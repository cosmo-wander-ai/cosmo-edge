#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace cosmo::util {

// This opt-in is deliberately unavailable in production builds.
inline bool BenchmarkIsolationEnabled() {
#ifdef COSMO_DEV_MODE
    const char* value = std::getenv("COSMO_BENCHMARK_ISOLATION");
    return value && std::string_view(value) == "1";
#else
    return false;
#endif
}

// Input is the router's normalized, lowercase URI. Default-deny ensures new
// management endpoints cannot silently acquire host-control permissions.
inline bool BenchmarkRouteAllowed(std::string_view uri) {
    constexpr std::string_view routes[] = {"/gtw/cwai/login/dologin",
                                           "/gtw/cwai/login/logout",
                                           "/gtw/cwai/system/querydeviceinfo",
                                           "/gtw/cwai/system/queryhardwareresource",
                                           "/v1/cwai/aihost/querydevicememstatus",
                                           "/gtw/cwai/algorithm/layout/save",
                                           "/gtw/cwai/atomic/model/uploadcapabilities",
                                           "/gtw/cwai/atomic/model/uploadtemp",
                                           "/gtw/cwai/atomic/model/cancelupload",
                                           "/gtw/cwai/atomic/model/list",
                                           "/gtw/cwai/atomic/model/page",
                                           "/gtw/cwai/model/querygraphmodelcandidates",
                                           "/gtw/cwai/camera/addvideo",
                                           "/gtw/cwai/camera/page",
                                           "/gtw/cwai/camera/batchdelete",
                                           "/gtw/cwai/task/saveorupdate",
                                           "/gtw/cwai/task/applyparamsbatch",
                                           "/gtw/cwai/task/batchswitchtask",
                                           "/gtw/cwai/task/runningdetail",
                                           "/gtw/cwai/task/page",
                                           "/gtw/cwai/task/selectconfigbyalgorithmid",
                                           "/gtw/cwai/task/delete",
                                           "/gtw/cwai/event/page",
                                           "/gtw/cwai/algorithm/page",
                                           "/gtw/cwai/schedule/page"};
    for (const auto route : routes) {
        if (route == uri)
            return true;
    }
    return false;
}

}  // namespace cosmo::util
