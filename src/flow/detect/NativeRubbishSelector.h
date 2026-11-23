#pragma once
#include <string>

#include "nn/device/axera/axera_rubbish_contract.h"
namespace cosmo::native_rubbish {
enum class Selection { NotRubbish, Selected, Invalid };
inline Selection RequireStrict(const std::string& atomic, bool listed) {
    if (atomic != nn::axera::rubbish_contract::kAtomicCode)
        return Selection::NotRubbish;
    return listed ? Selection::Selected : Selection::Invalid;
}
template <typename Active>
bool MatchesPolicy(const Active& active, float threshold) {
    return active.size() == 1 && active[0].label == nn::axera::rubbish_contract::kClassLabel &&
           nn::axera::rubbish_contract::IsThreshold(threshold) &&
           nn::axera::rubbish_contract::IsThreshold(active[0].confidence);
}
}  // namespace cosmo::native_rubbish
