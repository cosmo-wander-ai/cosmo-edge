// Hardware watchdog service interface.
// Controls /dev/watchdog timer to prevent system reset on hang.

#pragma once

namespace cosmo::service {

class IWatchDogService {
public:
    virtual ~IWatchDogService() = default;

    /// Start the hardware watchdog timer and feeding thread.
    virtual bool Start() = 0;

    /// Stop feeding the watchdog and disable the timer.
    virtual bool Stop() = 0;
};

}  // namespace cosmo::service
