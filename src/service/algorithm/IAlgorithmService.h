/// @file IAlgorithmService.h
/// @brief Aggregate algorithm service interface — inherits all ISP
///        sub-interfaces (IAlgorithmQuery, IAlgorithmCrud, IAlgorithmLayout).
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include "service/algorithm/IAlgorithmCrud.h"
#include "service/algorithm/IAlgorithmLayout.h"
#include "service/algorithm/IAlgorithmQuery.h"

namespace cosmo::service {

/// Aggregate algorithm service providing initialization and spanning
/// all ISP sub-interfaces.
///
/// New code should depend on the narrow sub-interfaces (IAlgorithmQuery,
/// IAlgorithmCrud, IAlgorithmLayout) rather than this aggregate.
class IAlgorithmService : public IAlgorithmQuery, public IAlgorithmCrud, public IAlgorithmLayout {
public:
    virtual ~IAlgorithmService() = default;

    /// Post-registration initialization. Must be called after all services are registered.
    virtual void Init() = 0;
};

}  // namespace cosmo::service
