/// @file IFaceLibService.h
/// @brief Aggregate face library service interface — inherits all ISP
///        sub-interfaces (IFaceLibRepo, IPersonRepo, IFaceFeature, IFaceImport).
///        Callers should prefer the narrow sub-interfaces for new code.
///        Path helpers removed — use IPathMedia / IPathEvent directly.
#pragma once

#include "service/face/IFaceFeature.h"
#include "service/face/IFaceImport.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IPersonRepo.h"
#include "util/dto/EventMsgTypes.h"

namespace cosmo::service {

/// Aggregate face library service providing query state
/// that spans multiple ISP sub-interfaces.
///
/// New code should depend on the narrow sub-interfaces (IFaceLibRepo,
/// IPersonRepo, IFaceFeature, IFaceImport) rather than this aggregate.
class IFaceLibService : public IFaceLibRepo, public IPersonRepo, public IFaceFeature, public IFaceImport {
public:
    virtual ~IFaceLibService() = default;

    // ── Query State Management ──

    /// Store a query condition and return its unique ID.
    /// @param queryCond Query parameters.
    /// @return Unique query identifier.
    virtual std::string SetQueryCond(const cosmo::MsgQueryFacesR& queryCond) = 0;
};

}  // namespace cosmo::service
