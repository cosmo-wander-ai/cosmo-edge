// Network to local byte order conversion utility.

#pragma once

#include <cstddef>

namespace cosmo::util {

void LtonConvert(void* data, size_t length);

}  // namespace cosmo::util
