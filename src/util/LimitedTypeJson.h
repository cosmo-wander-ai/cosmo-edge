// nlohmann/json ADL serialization for LimitedType wrappers.
// Include this header in files that need to serialize String<>, RangeInt<>, RangeValue<> to JSON.
// Separated from LimitedType.h to avoid forcing nlohmann dependency on all modules.

#pragma once

#include "util/JsonCompat.h"
#include "util/LimitedType.h"

namespace cosmo::util {

template <size_t MinSize, size_t MaxSize>
void to_json(nlohmann::json& j, const String<MinSize, MaxSize>& s) {
    j = s.ToRefString();
}

template <size_t MinSize, size_t MaxSize>
void from_json(const nlohmann::json& j, String<MinSize, MaxSize>& s) {
    s = j.get<std::string>();
}

template <int MinValue, int MaxValue>
void to_json(nlohmann::json& j, const RangeInt<MinValue, MaxValue>& v) {
    j = static_cast<int>(v);
}

template <int MinValue, int MaxValue>
void from_json(const nlohmann::json& j, RangeInt<MinValue, MaxValue>& v) {
    v = j.get<int>();
}

template <typename T>
void to_json(nlohmann::json& j, const RangeValue<T>& v) {
    j = static_cast<T>(v);
}

template <typename T>
void from_json(const nlohmann::json& j, RangeValue<T>& v) {
    v = j.get<T>();
}

}  // namespace cosmo::util
