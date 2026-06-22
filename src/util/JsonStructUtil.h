// JSON to C++ Struct Serialization Utility.
// Uses nlohmann/json for both parsing and struct mapping.

#pragma once

#include <exception>
#include <fstream>
#include <memory>
#include <string>

#include "util/FileUtil.h"
#include "util/JsonCompat.h"
#include "util/Log.h"

// Enable nlohmann::json serialization of std::shared_ptr<T>.
// Serializes as the pointed-to value (or null).
namespace nlohmann {
template <typename T>
struct adl_serializer<std::shared_ptr<T>> {
    static void to_json(json& j, const std::shared_ptr<T>& ptr) {
        if (ptr)
            j = *ptr;
        else
            j = nullptr;
    }
    static void from_json(const json& j, std::shared_ptr<T>& ptr) {
        if (j.is_null())
            ptr = nullptr;
        else
            ptr = std::make_shared<T>(j.get<T>());
    }
};
}  // namespace nlohmann

namespace cosmo::util {

template <typename Type>
[[nodiscard]] inline bool LoadStructFromJsonFile(const std::string& file_path, Type& t) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        LOG_WARN("Failed to open file: {}", file_path);
        return false;
    }

    try {
        auto j = nlohmann::json::parse(ifs);
        j.get_to(t);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("Struct mapping failed for {}: {}", file_path, e.what());
        return false;
    }
}

template <typename Type>
[[nodiscard]] inline bool DecodeJson(const std::string& json_input, Type& t) {
    try {
        auto j = nlohmann::json::parse(json_input);
        j.get_to(t);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("JSON decoding failed: {}", e.what());
        return false;
    }
}

template <typename Type>
[[nodiscard]] inline bool EncodeJson(const Type& t, std::string& json_output, int indent_count = 2,
                                     [[maybe_unused]] char indent_char       = ' ',
                                     [[maybe_unused]] int max_decimal_places = 2) {
    try {
        nlohmann::json j = t;
        json_output      = j.dump(indent_count);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("Struct encoding failed: {}", e.what());
        return false;
    }
}

template <typename Type>
[[nodiscard]] inline bool SaveStructToJsonFile(const std::string& file_path, const Type& t) {
    std::string content;
    if (!EncodeJson(t, content)) {
        return false;
    }
    return WriteFile(file_path, content);
}

}  // namespace cosmo::util
