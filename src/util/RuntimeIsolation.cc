#include "util/RuntimeIsolation.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>

#include "RuntimePathsConfig.h"

namespace cosmo::runtime {
namespace {

    bool Within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
        auto relative = candidate.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    }

    std::filesystem::path CheckRoot(const std::string& value) {
        namespace fs = std::filesystem;
        const fs::path requested(value);
        if (value.empty() || !requested.is_absolute() || !fs::is_directory(requested)) {
            throw std::invalid_argument("isolated roots must be existing absolute directories");
        }
        auto root = fs::canonical(requested);
        if (root != requested.lexically_normal()) {
            throw std::invalid_argument("isolated roots must use canonical paths without symlinks");
        }
        for (const fs::path production :
             {fs::path(kDefaultDataDir), fs::path(kDefaultAppDataDir), fs::path("/data/cwaiuserdata"),
              fs::path("/userdata/cwaiuserdata"), fs::path("/appfs/cosmo_wander/cwai_data"),
              fs::path("/data/cosmo-engine-deploy")}) {
            const auto actual_production = fs::weakly_canonical(production);
            if (Within(root, actual_production) || Within(actual_production, root)) {
                throw std::invalid_argument("isolated roots must not overlap production roots");
            }
        }
        if (root == root.root_path()) {
            throw std::invalid_argument("isolated roots must not be the filesystem root");
        }
        return root;
    }

    uint16_t ParsePort(const std::string& value) {
        if (value.empty() || value.size() > 5 ||
            !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            throw std::invalid_argument("isolated ports must be decimal numbers in 1024..65535");
        }
        const auto port = std::stoul(value);
        if (port < 1024 || port > 65535) {
            throw std::invalid_argument("isolated ports must be in 1024..65535");
        }
        return static_cast<uint16_t>(port);
    }
}  // namespace

RuntimeOptions ParseRuntimeOptions(const std::vector<std::string>& arguments) {
    RuntimeOptions options;
    if (arguments.size() == 1 && (arguments[0] == "--help" || arguments[0] == "-h")) {
        options.show_help = true;
        return options;
    }
    std::set<std::string> seen;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        if (!seen.insert(arg).second) {
            throw std::invalid_argument("duplicate runtime option: " + arg);
        }
        if (arg == "--isolated") {
            options.isolated = true;
            continue;
        }
        if (arg != "--data-dir" && arg != "--app-dir" && arg != "--http-port" && arg != "--websocket-port") {
            throw std::invalid_argument("unknown runtime option: " + arg);
        }
        if (++i == arguments.size()) {
            throw std::invalid_argument("missing runtime option value: " + arg);
        }
        const auto& value = arguments[i];
        if (arg == "--data-dir")
            options.data_dir = value;
        if (arg == "--app-dir")
            options.app_dir = value;
        if (arg == "--http-port")
            options.http_port = ParsePort(value);
        if (arg == "--websocket-port")
            options.websocket_port = ParsePort(value);
    }
    ValidateRuntimeOptions(options);
    return options;
}

void ValidateRuntimeOptions(const RuntimeOptions& options) {
    if (!options.isolated) {
        if (!options.data_dir.empty() || !options.app_dir.empty() || options.http_port != 0 ||
            options.websocket_port != 0) {
            throw std::invalid_argument("explicit runtime roots and ports require --isolated");
        }
        return;
    }
    if (options.show_help || options.http_port < 1024 || options.websocket_port < 1024 ||
        options.http_port == options.websocket_port) {
        throw std::invalid_argument("isolated mode requires distinct explicit nonprivileged HTTP/WS ports");
    }
    auto data_root = CheckRoot(options.data_dir);
    auto app_root  = CheckRoot(options.app_dir);
    if (Within(data_root, app_root) || Within(app_root, data_root)) {
        throw std::invalid_argument("isolated data and application roots must not overlap");
    }
    struct stat info {};
    if (stat(data_root.c_str(), &info) != 0 || info.st_uid != geteuid() || (info.st_mode & 0777) != 0700) {
        throw std::invalid_argument("isolated data root must be owned by the current user with mode 0700");
    }
    // Reject existing aliases before PathUtil performs cleanup or creates files.
    // Use a task-owned directory; do not copy a production data tree into it.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(data_root)) {
        if (lstat(entry.path().c_str(), &info) != 0 || info.st_uid != geteuid() ||
            (!S_ISDIR(info.st_mode) && (!S_ISREG(info.st_mode) || info.st_nlink != 1))) {
            throw std::invalid_argument("isolated data root contains an alias or non-owned entry");
        }
    }
}

const char* RuntimeOptionsHelp() noexcept {
    return "Usage: cosmo-engine [--help]\n"
           "       cosmo-engine --isolated --data-dir ABS_PRIVATE_DIR --app-dir ABS_PACKAGE_DIR\n"
           "                    --http-port PORT --websocket-port PORT\n"
           "No arguments: normal device operation. Isolated mode: loopback HTTP/WS only;\n"
           "host management, watchdog, discovery, NTP, MQTT and GPIO are disabled.\n"
           "Data directory must already exist, be owned by this user with mode 0700,\n"
           "and be separate from the application and production directories.\n";
}

bool IsHostManagementRoute(const std::string& route) {
    static const std::set<std::string> blocked{"/gtw/cwai/network/modifynetcard",
                                               "/gtw/cwai/network/modifynetdns",
                                               "/gtw/cwai/system/ntpdate",
                                               "/gtw/cwai/system/modifytime",
                                               "/gtw/cwai/system/resetsystem",
                                               "/gtw/cwai/system/upgrade",
                                               "/gtw/cwai/system/checkupgradespace",
                                               "/gtw/cwai/system/resetdevrestartparam",
                                               "/gtw/cwai/system/modifydevrestartparam",
                                               "/gtw/cwai/system/setmqttadapterparam",
                                               "/gtw/cwai/system/modifyiotnetworkparam",
                                               "/gtw/cwai/system/modifyrunmodeparam",
                                               "/gtw/cwai/camera/gb28181manage",
                                               "/gtw/cwai/camera/onvifdiscover",
                                               "/gtw/cwai/camera/onvifprobe",
                                               "/gtw/cwai/camera/onvifsave"};
    return blocked.count(route) != 0;
}
}  // namespace cosmo::runtime
