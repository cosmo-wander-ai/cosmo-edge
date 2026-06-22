// AlgorithmJsonCodec — Algorithm JSON serialization/deserialization utilities

#include "service/algorithm/impl/AlgorithmJsonCodec.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "util/DateTimeFormat.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service::detail {

const char kDefaultAlgorithmMetadata[] =
    "{\"params\":[],\"region\":{\"heads\":[],\"areasTitle\":null},\"regionType\":\"0\","
    "\"scheduleSupport\":false,\"enableShieldedRegion\":false,\"maxAreaCount\":4,"
    "\"defaultFullScreen\":false,\"shieldedRegion\":{}}";

// Convert numeric values in configObject.params[].value to strings in algorithmProcessdata JSON,
// preventing cosmo::util::DecodeJson parse failures
std::string NormalizeAlgorithmProcessdataParamValues(const std::string& processDataJson) {
    if (processDataJson.empty() || processDataJson == "[]")
        return processDataJson;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(processDataJson);
    } catch (...) {
        return processDataJson;
    }
    if (!doc.is_array())
        return processDataJson;

    for (auto& node : doc) {
        if (!node.is_object())
            continue;
        if (!node.contains("configObject") || !node["configObject"].is_object())
            continue;
        auto& config_obj = node["configObject"];
        if (!config_obj.contains("params") || !config_obj["params"].is_array())
            continue;
        for (auto& param : config_obj["params"]) {
            if (!param.is_object() || !param.contains("value"))
                continue;
            auto& val = param["value"];
            if (val.is_number_integer()) {
                param["value"] = std::to_string(val.get<int>());
            } else if (val.is_number_float()) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.10g", val.get<double>());
                param["value"] = std::string(buf);
            } else if (val.is_array()) {
                std::string joined;
                for (size_t k = 0; k < val.size(); k++) {
                    if (k)
                        joined += ",";
                    if (val[k].is_string())
                        joined += val[k].get<std::string>();
                }
                param["value"] = joined;
            }
        }
    }
    return doc.dump();
}

// Update algorithmName/remark/algorithmCategory in algorithm JSON file and rename the file
std::string UpdateAlgorithmJsonFile(const std::string& algorithmId, const std::string& algorithmName,
                                    int algorithmCategory, const std::string& remark) {
    const std::string algorithm_layout_dir = cosmo::path::GetAlgorithmPath();
    std::string file_path = cosmo::util::FindPrefixedJsonFile(algorithm_layout_dir, algorithmId);
    if (file_path.empty())
        file_path = (std::filesystem::path(algorithm_layout_dir) / (algorithmId + ".json")).string();
    nlohmann::json doc;
    if (cosmo::util::JsonFileUtil::ReadJsonFile(file_path, doc) != cosmo::util::ErrorEnum::Success ||
        !doc.is_object())
        return "";
    doc["algorithmName"] = algorithmName;
    doc["remark"]        = remark;
    doc["description"]   = remark;
    if (doc.contains("algorithmCategory")) {
        if (doc["algorithmCategory"].is_number_integer())
            doc["algorithmCategory"] = algorithmCategory;
        else if (doc["algorithmCategory"].is_string())
            doc["algorithmCategory"] = std::to_string(algorithmCategory);
    } else {
        doc["algorithmCategory"] = algorithmCategory;
    }
    (void)cosmo::util::JsonFileUtil::WriteJsonFile(file_path, doc);

    // Rename file to reflect updated algorithmName
    std::string clean_name;
    for (char c : algorithmName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            clean_name += '_';
        else
            clean_name += c;
    }
    if (clean_name.empty())
        clean_name = "algorithm";

    // Extract timestamp from old filename if present, otherwise generate new one
    std::string old_stem = std::filesystem::path(file_path).stem().string();
    std::string timestamp;
    size_t last_underscore = old_stem.rfind('_');
    if (last_underscore != std::string::npos && last_underscore + 1 < old_stem.size()) {
        std::string tail = old_stem.substr(last_underscore + 1);
        bool all_digits  = !tail.empty() && std::all_of(tail.begin(), tail.end(), ::isdigit);
        if (all_digits)
            timestamp = tail;
    }
    if (timestamp.empty())
        timestamp = std::to_string(cosmo::util::GetCurrentDateTime().ToYMDHMSInt());

    std::string new_file_name = algorithmId + "_" + clean_name + "_" + timestamp + ".json";
    std::string new_file_path = (std::filesystem::path(algorithm_layout_dir) / new_file_name).string();

    if (new_file_path != file_path) {
        std::error_code ec;
        std::filesystem::rename(file_path, new_file_path, ec);
        if (ec) {
            LOG_WARN("Failed to rename algorithm file {} -> {}: {}", file_path, new_file_path, ec.message());
            return file_path;  // return old path on rename failure
        }
    }
    return new_file_path;
}

void BuildDefaultLayoutJson(nlohmann::json& outDoc, const std::string& algorithmCode,
                            const std::string& algorithmName, int algorithmCategory, int algorithmUsage,
                            int checkType, int64_t msTimestamp, const std::string& remark) {
    std::string default_version_id = "default-" + algorithmCode;

    nlohmann::json default_version;
    default_version["id"]                   = default_version_id;
    default_version["name"]                 = "默认";
    default_version["algorithmCode"]        = algorithmCode;
    default_version["algorithmMetadata"]    = kDefaultAlgorithmMetadata;
    default_version["algorithmProcessdata"] = "[]";
    default_version["atomicList"]           = "[]";
    default_version["algorithmUpdateTime"]  = static_cast<uint64_t>(msTimestamp);
    default_version["remark"]               = remark;

    outDoc                      = nlohmann::json::object();
    outDoc["algorithmId"]       = algorithmCode;
    outDoc["algorithmCode"]     = algorithmCode;
    outDoc["algorithmName"]     = algorithmName;
    outDoc["algorithmCategory"] = std::to_string(algorithmCategory);
    outDoc["algorithmUsage"]    = algorithmUsage;
    outDoc["checkType"]         = checkType;
    outDoc["createTime"]        = msTimestamp;
    outDoc["updateTime"]        = msTimestamp;
    outDoc["confVersionId"]     = default_version_id;
    outDoc["algorithmMetadata"] = kDefaultAlgorithmMetadata;
    outDoc["configVersionList"] = nlohmann::json::array({default_version});
}

}  // namespace cosmo::service::detail
