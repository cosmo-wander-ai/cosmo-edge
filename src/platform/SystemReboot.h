// System reboot and factory-reset operations.
//
// Provides RebootManager class for managing reboot/reset lifecycle with
// proper thread synchronization. Replaces the previous bare global
// std::thread with a mutex-protected class member.
#pragma once

#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace cosmo::platform {

// Device backends may invoke the operating-system reboot command. CPU builds
// must never reboot a developer or CI host.
constexpr bool SupportsSystemReboot() noexcept {
#if defined(COSMO_NN_USE_SOPHON_BACKEND) || defined(COSMO_NN_USE_RKNN_BACKEND)
    return true;
#else
    return false;
#endif
}

// Remove factory-resettable data while retaining the top-level model-guard directory.
std::error_code ClearFactoryResetData(const std::string& baseDir);

// Manages system reboot and factory-reset operations.
// Thread-safe: all public methods serialize on an internal mutex to prevent
// concurrent reboot thread re-assignment.
class RebootManager {
public:
    RebootManager() = default;
    ~RebootManager();

    RebootManager(const RebootManager&)            = delete;
    RebootManager& operator=(const RebootManager&) = delete;

    // Schedule a system reboot after a grace period.
    // reason: Human-readable reason for the reboot (logged).
    void Reboot(const std::string& reason);

    // Schedule a factory reset (clear resettable entries in baseDir then reboot).
    // reason:  Human-readable reason for the reset (logged).
    // baseDir: Absolute path containing resettable data.
    void Reset(const std::string& reason, const std::string& baseDir);

private:
    // Join any previously scheduled reboot thread. Must be called under mtx_.
    void JoinPendingLocked();

    std::mutex mtx_;
    std::thread thread_;
};

}  // namespace cosmo::platform
