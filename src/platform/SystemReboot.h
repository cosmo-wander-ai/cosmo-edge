// System reboot and factory-reset operations.
//
// Provides RebootManager class for managing reboot/reset lifecycle with
// proper thread synchronization. Replaces the previous bare global
// std::thread with a mutex-protected class member.
#pragma once

#include <mutex>
#include <string>
#include <thread>

namespace cosmo::platform {

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

    // Schedule a factory reset (delete baseDir then reboot).
    // reason:  Human-readable reason for the reset (logged).
    // baseDir: Absolute path to the data directory to remove.
    void Reset(const std::string& reason, const std::string& baseDir);

private:
    // Join any previously scheduled reboot thread. Must be called under mtx_.
    void JoinPendingLocked();

    std::mutex mtx_;
    std::thread thread_;
};

}  // namespace cosmo::platform
