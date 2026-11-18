#pragma once

#include <optional>
#include <vector>

namespace cosmo::nn {

enum class Yolo26OutputLayout { kInvalid, kDecoded, kRaw };

// An absent override selects decoded rows first when both layouts are possible.
Yolo26OutputLayout ResolveYolo26OutputLayout(const std::vector<int>& shape,
                                             std::optional<bool> raw_output = std::nullopt);

}  // namespace cosmo::nn
