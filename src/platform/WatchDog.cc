// Hardware watchdog driver implementation.

#include "platform/WatchDog.h"

#include <fcntl.h>
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <thread>

#include "platform/PlatformConstants.h"
#include "util/Log.h"
#include "util/TimingConstants.h"

namespace cosmo::platform {

WatchDog::WatchDog() : Thread("WatchDog Thread") {}

WatchDog::~WatchDog() {
    Stop();
}

bool WatchDog::OpenDevice() {
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    return true;
#else
    fd_ = open(kWatchdogDevice.c_str(), O_WRONLY);
    if (fd_ == -1) {
        LOG_ERRO("Failed to open {}", kWatchdogDevice);
        return false;
    }
    ioctl(fd_, WDIOC_SETTIMEOUT, &timeout_sec_);
    LOG_INFO("Watchdog opened, timeout={}s", timeout_sec_);
    return true;
#endif
}

void WatchDog::CloseDevice() {
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    return;
#else
    if (fd_ == -1) {
        return;
    }
    // Disable the watchdog before closing to prevent immediate reboot.
    int option = WDIOS_DISABLECARD;
    ioctl(fd_, WDIOC_SETOPTIONS, &option);

    // Write magic close character 'V' to allow clean shutdown.
    auto ret = write(fd_, "V", 1);
    (void)ret;
    close(fd_);
    fd_ = -1;
    LOG_INFO("{}", "Watchdog closed");
#endif
}

void WatchDog::Feed() {
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    return;
#else
    ioctl(fd_, WDIOC_KEEPALIVE, 0);
#endif
}

void WatchDog::run() {
    while (is_running_) {
        Feed();
        std::this_thread::sleep_for(cosmo::timing::kOneSecondInterval);
    }
}

bool WatchDog::Start() {
    if (!OpenDevice()) {
        return false;
    }
    is_running_ = true;
    if (!start()) {
        is_running_ = false;
        CloseDevice();
        return false;
    }
    return true;
}

bool WatchDog::Stop() {
    is_running_ = false;
    stop();
    CloseDevice();
    return true;
}

}  // namespace cosmo::platform
