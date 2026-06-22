// RebootManager implementation.
#include "platform/SystemReboot.h"

#include <sys/reboot.h>
#include <unistd.h>

#include <filesystem>
#include <thread>

#include "util/Exec.h"
#include "util/Log.h"
#include "util/TimingConstants.h"

namespace {

// Execute an immediate system reboot via the `reboot` command.
void ImmReboot() {
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    LOG_WARN("{}", "System reboot is disabled on x86 platform.");
    return;
#else
    sync();
    std::string out;
    cosmo::util::Exec("reboot", out);
#endif
}

}  // namespace

namespace cosmo::platform {

RebootManager::~RebootManager() {
    std::lock_guard<std::mutex> lock(mtx_);
    JoinPendingLocked();
}

void RebootManager::JoinPendingLocked() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void RebootManager::Reboot(const std::string& reason) {
    LOG_INFO("REBOOT:{}", reason);
    sync();
    cosmo::log::FlushLog();

    std::lock_guard<std::mutex> lock(mtx_);
    JoinPendingLocked();
    thread_ = std::thread([]() {
        std::this_thread::sleep_for(cosmo::timing::kRebootGracePeriod);
        ImmReboot();
    });
}

void RebootManager::Reset(const std::string& reason, const std::string& baseDir) {
    LOG_INFO("ResetSystem:{}", reason);
    sync();
    cosmo::log::FlushLog();

    std::lock_guard<std::mutex> lock(mtx_);
    JoinPendingLocked();
    thread_ = std::thread([baseDir]() {
        std::this_thread::sleep_for(cosmo::timing::kServiceReadyDelay);
#ifndef COSMO_NN_USE_SOPHON_BACKEND
        LOG_WARN("System reset (clear base dir and reboot) is disabled on x86 platform. Base dir: {}",
                 baseDir);
        return;
#else
        LOG_INFO("Removing base dir: {}", baseDir);
        std::error_code ec;
        std::filesystem::remove_all(baseDir, ec);
        if (ec) {
            LOG_ERRO("Failed to remove {}: {}", baseDir, ec.message());
        }

        ImmReboot();
#endif
    });
}

}  // namespace cosmo::platform
