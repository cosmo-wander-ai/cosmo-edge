// IWatchDogService implementation — owns platform::WatchDog lifecycle.

#pragma once

#include <memory>

#include "service/system/IWatchDogService.h"

namespace cosmo::platform {
class WatchDog;
}

namespace cosmo::service {

class WatchDogServiceImpl final : public IWatchDogService {
public:
    WatchDogServiceImpl();
    ~WatchDogServiceImpl() override;

    WatchDogServiceImpl(const WatchDogServiceImpl&)            = delete;
    WatchDogServiceImpl& operator=(const WatchDogServiceImpl&) = delete;

    bool Start() override;
    bool Stop() override;

private:
    std::unique_ptr<cosmo::platform::WatchDog> watchDog_;
};

}  // namespace cosmo::service
