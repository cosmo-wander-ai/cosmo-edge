// System time

#pragma once

#include <cstdint>

#include "util/ErrorCode.h"

namespace cosmo::platform {

// Set system time, timestamp_ms is the millisecond timestamp
cosmo::util::ErrorEnum SetSystemTime(int64_t timestamp_ms);

}  // namespace cosmo::platform
