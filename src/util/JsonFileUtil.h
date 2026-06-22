// JSON file read/write utility.
// Used to read and save data from/to JSON files.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/ErrorCode.h"

namespace cosmo::util {

class JsonFileUtil {
public:
    [[nodiscard]] static cosmo::util::ErrorEnum ReadJsonFile(const std::string& file_path,
                                                             nlohmann::json& doc);
    [[nodiscard]] static cosmo::util::ErrorEnum WriteJsonFile(const std::string& file_path,
                                                              const nlohmann::json& doc);
    [[nodiscard]] static cosmo::util::ErrorEnum ReadJsonArray(const std::string& file_path,
                                                              nlohmann::json& doc);
    [[nodiscard]] static cosmo::util::ErrorEnum AppendToJsonArray(const std::string& file_path,
                                                                  const nlohmann::json& item);
    [[nodiscard]] static cosmo::util::ErrorEnum UpdateJsonArrayItem(const std::string& file_path,
                                                                    const std::string& key,
                                                                    const std::string& value,
                                                                    const nlohmann::json& new_item);
    [[nodiscard]] static cosmo::util::ErrorEnum DeleteJsonArrayItem(const std::string& file_path,
                                                                    const std::string& key,
                                                                    const std::string& value);
    [[nodiscard]] static nlohmann::json* FindItemInArray(nlohmann::json& doc, const std::string& key,
                                                         const std::string& value);
};

}  // namespace cosmo::util
