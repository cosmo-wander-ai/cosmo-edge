/// @file IDeviceContextService.h
/// @brief Compatibility alias — canonical interface is now mem::IDeviceContext.
///
/// Upper layers (flow, media, service) use this typedef so that existing code
/// continues to compile.  The real interface lives in mem/IDeviceContext.h to
/// respect the dependency direction: Foundation defines, Business consumes.
#pragma once

#include "mem/IDeviceContext.h"

namespace cosmo::service {

using IDeviceContextService = cosmo::mem::IDeviceContext;

}  // namespace cosmo::service
