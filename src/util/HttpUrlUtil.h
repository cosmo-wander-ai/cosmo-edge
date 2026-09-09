#pragma once

#include <string_view>

namespace cosmo::util {

[[nodiscard]] bool IsValidHttpUrl(std::string_view url);

}  // namespace cosmo::util
