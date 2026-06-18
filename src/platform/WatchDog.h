// Hardware watchdog driver for /dev/watchdog.
// Periodically feeds the kernel watchdog to prevent system reset.

#pragma once

#include <atomic>

#include "util/Thread.h"

namespace cosmo::platform {

// Default watchdog timeout in seconds.
constexpr int kWatchdogTimeoutSec = 30;

// Default feed interval in seconds.
constexpr int kWatchdogFeedIntervalSec = 1;

class WatchDog : public cosmo::util::Thread {
public:
    WatchDog();
    ~WatchDog();

    WatchDog(const WatchDog&)            = delete;
    WatchDog& operator=(const WatchDog&) = delete;

    // Start the watchdog timer and feeding thread.
    [[nodiscard]] bool Start();

    // Stop the feeding thread and disable the watchdog.
    bool Stop();

protected:
    void run() override;

private:
    // Open /dev/watchdog device and set timeout.
    [[nodiscard]] bool OpenDevice();

    // Disable and close the watchdog device.
    void CloseDevice();

    // Send keepalive ioctl to the watchdog.
    void Feed();

    std::atomic<bool> is_running_{false};
    int timeout_sec_{kWatchdogTimeoutSec};
    int fd_{-1};
};

}  // namespace cosmo::platform
