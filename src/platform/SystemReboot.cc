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

constexpr char kModelAuthorizationDirectory[] = "model-guard";

// Execute an immediate system reboot via the `reboot` command.
void ImmReboot() {
    if (!cosmo::platform::SupportsSystemReboot()) {
        LOG_WARN("{}", "System reboot is disabled on the current backend.");
        return;
    }
    sync();
    std::string out;
    cosmo::util::Exec(std::vector<std::string>{"reboot"}, out);
}

}  // namespace

namespace cosmo::platform {

std::error_code ClearFactoryResetData(const std::string& baseDir) {
    std::error_code ec;
    for (std::filesystem::directory_iterator entry(baseDir, ec), end; !ec && entry != end;) {
        const auto path = entry->path();
        entry.increment(ec);
        if (!ec && path.filename() != kModelAuthorizationDirectory) {
            std::filesystem::remove_all(path, ec);
        }
    }
    return ec;
}

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
        LOG_INFO("Clearing factory-resettable data in: {}", baseDir);
        const auto ec = ClearFactoryResetData(baseDir);
        if (ec) {
            LOG_ERRO("Failed to clear resettable data in {}: {}", baseDir, ec.message());
        }

        ImmReboot();
#endif
    });
}

}  // namespace cosmo::platform
