/// @file IVideoFrameService.h
/// @brief Aggregate video frame processing service interface — inherits all ISP
///        sub-interfaces (IVideoFrameOSD, IVideoFrameTransform, IVideoFrameCodec).
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"

namespace cosmo::service {

/// Aggregate video frame processing service spanning all ISP sub-interfaces.
///
/// New code should depend on the narrow sub-interfaces (IVideoFrameOSD,
/// IVideoFrameTransform, IVideoFrameCodec) rather than this aggregate.
class IVideoFrameService : public IVideoFrameOSD, public IVideoFrameTransform, public IVideoFrameCodec {
public:
    virtual ~IVideoFrameService() = default;
};

}  // namespace cosmo::service
