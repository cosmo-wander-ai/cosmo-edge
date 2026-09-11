// Hardware watchdog driver implementation.

#include "platform/WatchDog.h"

#include <fcntl.h>
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>

#include "platform/PlatformConstants.h"
#include "util/Log.h"
#include "util/TimingConstants.h"

namespace cosmo::platform {

namespace {
// Both edge backends use the Linux watchdog API. CPU evaluation and development
// builds must never arm a hardware watchdog, even through a direct Start call.
#if !defined(COSMO_DEV_MODE) && (defined(COSMO_NN_USE_SOPHON_BACKEND) || defined(COSMO_NN_USE_RKNN_BACKEND))
    constexpr bool kHardwareWatchdogEnabled = true;
#else
    constexpr bool kHardwareWatchdogEnabled = false;
#endif
}  // namespace

WatchDog::WatchDog() : Thread("WatchDog Thread") {}

WatchDog::~WatchDog() {
    Stop();
}

bool WatchDog::OpenDevice() {
    fd_ = open(kWatchdogDevice.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd_ == -1) {
        LOG_ERRO("Failed to open {}: {}", kWatchdogDevice, std::strerror(errno));
        return false;
    }
    timeout_sec_ = kWatchdogTimeoutSec;
    if (ioctl(fd_, WDIOC_SETTIMEOUT, &timeout_sec_) == -1) {
        LOG_WARN("Failed to set watchdog timeout: {}; querying driver timeout", std::strerror(errno));
        if (ioctl(fd_, WDIOC_GETTIMEOUT, &timeout_sec_) == -1) {
            LOG_ERRO("Failed to read watchdog timeout: {}", std::strerror(errno));
            CloseDevice();
            return false;
        }
    }
    if (timeout_sec_ <= kWatchdogFeedIntervalSec) {
        LOG_ERRO("Watchdog timeout {}s must exceed feed interval {}s", timeout_sec_,
                 kWatchdogFeedIntervalSec);
        CloseDevice();
        return false;
    }
    LOG_INFO("Watchdog opened, timeout={}s", timeout_sec_);
    return true;
}

bool WatchDog::CloseDevice() {
    if (fd_ == -1) {
        return true;
    }
    int option          = WDIOS_DISABLECARD;
    const bool disabled = ioctl(fd_, WDIOC_SETOPTIONS, &option) == 0;
    if (!disabled) {
        LOG_ERRO("Failed to disable watchdog: {}; attempting magic close", std::strerror(errno));
        // Best effort for drivers supporting magic close. A successful write
        // does not prove that close can stop the timer (e.g. nowayout).
        if (write(fd_, "V", 1) != 1) {
            LOG_ERRO("Failed to write watchdog magic close: {}", std::strerror(errno));
        }
    }
    const bool closed = close(fd_) == 0;
    if (!closed) {
        LOG_ERRO("Failed to close watchdog descriptor: {}", std::strerror(errno));
    }
    // Do not retry close: Linux may already have released the descriptor.
    fd_ = -1;
    if (disabled && closed) {
        LOG_INFO("{}", "Watchdog stop accepted by driver; descriptor closed");
    } else {
        LOG_WARN("{}", "Watchdog shutdown not confirmed; timer may still be running");
    }
    return disabled && closed;
}

bool WatchDog::Feed() {
    if (ioctl(fd_, WDIOC_KEEPALIVE, 0) == -1) {
        LOG_ERRO("Failed to feed watchdog: {}", std::strerror(errno));
        return false;
    }
    return true;
}

void WatchDog::run() {
    while (is_running_) {
        Feed();
        std::this_thread::sleep_for(cosmo::timing::kOneSecondInterval);
    }
}

bool WatchDog::Start() {
    if (!kHardwareWatchdogEnabled) {
        LOG_INFO("{}", "Hardware watchdog disabled for this build");
        return true;
    }
    if (is_running_) {
        return true;
    }
    if (!OpenDevice()) {
        return false;
    }
    // Validate keepalive before reporting startup success.
    if (!Feed()) {
        CloseDevice();
        return false;
    }
    is_running_ = true;
    try {
        if (start()) {
            return true;
        }
    } catch (...) {
        is_running_ = false;
        CloseDevice();
        throw;
    }
    is_running_ = false;
    CloseDevice();
    return false;
}

bool WatchDog::Stop() {
    is_running_ = false;
    stop();
    return CloseDevice();
}

}  // namespace cosmo::platform
