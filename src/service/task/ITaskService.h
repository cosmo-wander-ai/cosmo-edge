/// @file ITaskService.h
/// @brief Aggregate task service interface — inherits all narrow task
///        sub-interfaces (ITaskLifecycle, ITaskQuery, ITaskChannel).
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include "service/detail/ServiceRegistry.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"

namespace cosmo::service {

/// Aggregate task service — inherits all narrow task sub-interfaces.
/// New code should prefer narrow interfaces; this aggregate is kept for backward compatibility.
class ITaskService : public ITaskLifecycle, public ITaskQuery, public ITaskChannel {
public:
    virtual ~ITaskService() = default;
};

// Dependency injection methods

}  // namespace cosmo::service
