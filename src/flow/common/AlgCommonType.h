#pragma once

// No includes are necessary for this file.

namespace cosmo {
enum class AlgCompareType {
    AlgCompareTypeInvalid = -1,
    AlgCompareTypeLess    = 0,  // Less than alarm
    AlgCompareTypeGreater,      // Greater than alarm
    AlgCompareTypeLE,           // Less than or equal to alarm
    AlgCompareTypeGE,           // Greater than or equal to alarm
    AlgCompareTypeEqual,        // Equal to alarm
    AlgCompareTypeMax
};
constexpr bool ValidateCompareType(int value) noexcept {
    return value >= static_cast<int>(AlgCompareType::AlgCompareTypeLess) &&
           value < static_cast<int>(AlgCompareType::AlgCompareTypeMax);
}

enum class AlgClassifyGroupType {
    AlgClassifyGroupTypeNone = 0,         // None
    AlgClassifyGroupTypeAreaTargetCount,  // Area target count grouping
    AlgClassifyGroupTypeMax
};
constexpr bool ValidateClassifyGroupType(int value) noexcept {
    return value >= static_cast<int>(AlgClassifyGroupType::AlgClassifyGroupTypeNone) &&
           value < static_cast<int>(AlgClassifyGroupType::AlgClassifyGroupTypeMax);
}

template <typename T>
bool AlgCompareTypeResult(AlgCompareType type, T lValue, T rValue) {
    if ((lValue < 0) || (rValue < 0)) {
        return false;
    }
    bool ret = false;
    switch (type) {
        case AlgCompareType::AlgCompareTypeLess: {
            ret = (lValue < rValue);
        } break;
        case AlgCompareType::AlgCompareTypeGreater: {
            ret = (lValue > rValue);
        } break;
        case AlgCompareType::AlgCompareTypeLE: {
            ret = (lValue <= rValue);
        } break;
        case AlgCompareType::AlgCompareTypeGE: {
            ret = (lValue >= rValue);
        } break;
        case AlgCompareType::AlgCompareTypeEqual: {
            ret = (lValue == rValue);
        } break;
        default: {
            ret = false;
        }
    }
    return ret;
}

}  // namespace cosmo
