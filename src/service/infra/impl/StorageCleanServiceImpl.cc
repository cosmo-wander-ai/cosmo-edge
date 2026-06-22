// StorageCleanServiceImpl — IStorageCleanService implementation — owns StorageSpace + PeriodicTimer lifec...

#include "service/infra/impl/StorageCleanServiceImpl.h"

#include "service/detail/ServiceRegistry.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimingConstants.h"

namespace cosmo::service {

StorageCleanServiceImpl::~StorageCleanServiceImpl() {
    StorageCleanServiceImpl::Stop();
}

void StorageCleanServiceImpl::Start() {
    auto event_root_path = cosmo::path::GetEventRootPath();
    storage_             = std::make_shared<cosmo::StorageSpace>(event_root_path);

    timer_ = std::make_unique<cosmo::PeriodicTimer>("StorageSpace");
    timer_->Start();

    cleanTaskId_ =
        timer_->Schedule([this]() { storage_->DoClean(); }, cosmo::timing::kStorageCleanIntervalMs);

    LOG_INFO("StorageCleanService: started periodic cleanup (interval={}ms)",
             cosmo::timing::kStorageCleanIntervalMs);
}

void StorageCleanServiceImpl::Stop() {
    if (timer_) {
        timer_->Cancel(cleanTaskId_);
        cleanTaskId_ = cosmo::kInvalidTaskId;
        timer_->Destroy();
        timer_.reset();
    }
    storage_.reset();
}

}  // namespace cosmo::service
