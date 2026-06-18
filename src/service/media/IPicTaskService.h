/// @file IPicTaskService.h
/// @brief Aggregate image analysis task service interface — inherits all ISP
///        sub-interfaces (IPicTaskLifecycle, IPicTaskDetect, IPicTaskQuery).
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include "service/detail/ServiceRegistry.h"
#include "service/media/IPicTaskDetect.h"
#include "service/media/IPicTaskLifecycle.h"
#include "service/media/IPicTaskQuery.h"

namespace cosmo::service {

/// Aggregate image analysis task service spanning all ISP sub-interfaces.
///
/// New code should depend on the narrow sub-interfaces (IPicTaskLifecycle,
/// IPicTaskDetect, IPicTaskQuery) rather than this aggregate.
class IPicTaskService : public IPicTaskLifecycle, public IPicTaskDetect, public IPicTaskQuery {
public:
    virtual ~IPicTaskService() = default;
};

// Dependency injection methods

}  // namespace cosmo::service
