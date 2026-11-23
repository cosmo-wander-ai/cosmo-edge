#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::runtime {

// An explicitly selected validation mode, independent of Debug/Release builds.
// It restricts engine-owned host management; it is not an OS security sandbox.
struct RuntimeOptions {
    bool isolated  = false;
    bool show_help = false;
    std::string data_dir;
    std::string app_dir;
    uint16_t http_port      = 0;
    uint16_t websocket_port = 0;
};

// Arguments exclude argv[0]. Invalid or incomplete options throw before any
// service, device, log directory or network listener is initialized.
RuntimeOptions ParseRuntimeOptions(const std::vector<std::string>& arguments);
void ValidateRuntimeOptions(const RuntimeOptions& options);
const char* RuntimeOptionsHelp() noexcept;

namespace detail {
    inline std::atomic<bool> isolated_runtime{false};
}

inline bool IsIsolatedRuntime() noexcept {
    return detail::isolated_runtime.load(std::memory_order_acquire);
}

// Install once before creating services, restore only after their destruction.
// Nested scopes are also useful for tests; scope changes are not a runtime API.
class RuntimeIsolationScope {
public:
    explicit RuntimeIsolationScope(bool enabled) noexcept
        : previous_(detail::isolated_runtime.exchange(enabled, std::memory_order_acq_rel)) {}
    ~RuntimeIsolationScope() {
        detail::isolated_runtime.store(previous_, std::memory_order_release);
    }
    RuntimeIsolationScope(const RuntimeIsolationScope&)            = delete;
    RuntimeIsolationScope& operator=(const RuntimeIsolationScope&) = delete;

private:
    bool previous_;
};

// Receives the normalized (lowercase) route used by ApiRouter. Keeping this
// policy at dispatch also returns an explicit refusal for legacy void APIs.
bool IsHostManagementRoute(const std::string& route);

}  // namespace cosmo::runtime
