// WatchDogServiceImpl — IWatchDogService implementation — owns platform::WatchDog lifecycle.

#include "service/system/impl/WatchDogServiceImpl.h"

#include "platform/WatchDog.h"
#include "util/Log.h"

namespace cosmo::service {

WatchDogServiceImpl::WatchDogServiceImpl() : watchDog_(std::make_unique<cosmo::platform::WatchDog>()) {}

WatchDogServiceImpl::~WatchDogServiceImpl() {
    WatchDogServiceImpl::Stop();
}

bool WatchDogServiceImpl::Start() {
    LOG_INFO("{}", "WatchDogService: starting hardware watchdog");
    return watchDog_->Start();
}

bool WatchDogServiceImpl::Stop() {
    if (watchDog_) {
        LOG_INFO("{}", "WatchDogService: stopping hardware watchdog");
        return watchDog_->Stop();
    }
    return true;
}

}  // namespace cosmo::service
