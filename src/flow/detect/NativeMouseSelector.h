#pragma once
#include <string>

#include "nn/device/axera/axera_mouse_contract.h"
namespace cosmo::native_mouse {
enum class Selection { NotMouse, Selected, Invalid };
inline Selection RequireStrict(const std::string& atomic, bool listed) {
    if (atomic != nn::axera::mouse_contract::kAtomicCode)
        return Selection::NotMouse;
    return listed ? Selection::Selected : Selection::Invalid;
}
template <typename Active>
bool MatchesPolicy(const Active& active, float threshold) {
    return active.size() == 1 && active[0].label == nn::axera::mouse_contract::kClassLabel &&
           nn::axera::mouse_contract::IsThreshold(threshold) &&
           nn::axera::mouse_contract::IsThreshold(active[0].confidence);
}
}  // namespace cosmo::native_mouse
