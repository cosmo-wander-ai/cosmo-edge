// IStorageCleanService implementation — owns StorageSpace + PeriodicTimer lifecycle.

#pragma once

#include <cstdint>
#include <memory>

#include "service/infra/IStorageCleanService.h"
#include "service/infra/impl/StorageSpace.h"
#include "util/PeriodicTimer.h"

namespace cosmo::service {

class StorageCleanServiceImpl final : public IStorageCleanService {
public:
    StorageCleanServiceImpl() = default;
    ~StorageCleanServiceImpl() override;

    StorageCleanServiceImpl(const StorageCleanServiceImpl&)            = delete;
    StorageCleanServiceImpl& operator=(const StorageCleanServiceImpl&) = delete;

    void Start() override;
    void Stop() override;

private:
    std::shared_ptr<cosmo::StorageSpace> storage_;
    std::unique_ptr<cosmo::PeriodicTimer> timer_;
    cosmo::TaskId cleanTaskId_{0};
};

}  // namespace cosmo::service
