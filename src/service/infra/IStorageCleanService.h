// Periodic storage cleanup service interface.
// Manages old event file cleanup and disk space reclamation.

#pragma once

namespace cosmo::service {

class IStorageCleanService {
public:
    virtual ~IStorageCleanService() = default;

    /// Start periodic storage cleanup timer.
    virtual void Start() = 0;

    /// Stop periodic cleanup and release resources.
    virtual void Stop() = 0;
};

}  // namespace cosmo::service
