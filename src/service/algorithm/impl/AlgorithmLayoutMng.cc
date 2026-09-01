// AIBox platform layout operations - stateless file operations extracted from
// AlgorithmPacketMng

#include "service/algorithm/impl/AlgorithmLayoutMng.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "service/algorithm/IAlgorithmCrud.h"
#include "service/detail/ServiceRegistry.h"
#include "util/DateTimeFormat.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo::service::detail {
namespace alg = algorithm;

namespace {
    // Helper: get string from json with type coercion
    std::string GetStr(const nlohmann::json& doc, const std::string& key) {
        if (!doc.contains(key))
            return "";
        const auto& v = doc[key];
        if (v.is_string())
            return v.get<std::string>();
        if (v.is_number_integer())
            return std::to_string(v.get<int64_t>());
        return "";
    }
}  // namespace

bool AlgorithmLayoutMng::ResolveLayoutDirectory(const std::string& requested_path, bool allow_template,
                                                std::string& resolved_path) {
    resolved_path.clear();

    const std::string resource_path  = cosmo::path::GetResourcePath();
    const std::string algorithm_path = cosmo::path::GetAlgorithmPath();
    std::string resolved_resource;
    std::string resolved_algorithm;
    if (!cosmo::path::ResolveExistingPathWithinRoot(
            resource_path, resource_path, cosmo::path::PathEntryType::kDirectory, resolved_resource) ||
        !cosmo::path::ResolveExistingPathWithinRoot(
            resolved_resource, algorithm_path, cosmo::path::PathEntryType::kDirectory, resolved_algorithm)) {
        return false;
    }

    // The Web client must not need to know the device's configured app-data
    // root.  An empty value means the writable algorithm root, while the
    // stable logical name selects the read-only template root.  Legacy
    // absolute paths remain accepted when they resolve inside this runtime.
    std::string selected_path;
    if (requested_path.empty()) {
        selected_path = algorithm_path;
    } else if (requested_path == "algorithm_template") {
        selected_path = (std::filesystem::path(resolved_resource) / "algorithm_template").string();
    } else {
        selected_path = requested_path;
    }
    if (!cosmo::path::ResolveExistingPathWithinRoot(resolved_resource, selected_path,
                                                    cosmo::path::PathEntryType::kDirectory, resolved_path)) {
        return false;
    }
    if (resolved_path == resolved_algorithm) {
        return true;
    }
    if (!allow_template) {
        resolved_path.clear();
        return false;
    }

    const std::string template_path =
        (std::filesystem::path(resolved_resource) / "algorithm_template").string();
    std::string resolved_template;
    if (!cosmo::path::ResolveExistingPathWithinRoot(
            resolved_resource, template_path, cosmo::path::PathEntryType::kDirectory, resolved_template) ||
        resolved_path != resolved_template) {
        resolved_path.clear();
        return false;
    }
    return true;
}

bool AlgorithmLayoutMng::ResolveManagedRegularFile(const std::string& root, const std::string& candidate,
                                                   bool must_exist, std::string& resolved_path) {
    resolved_path.clear();
    std::string resolved_root;
    if (!cosmo::path::ResolveExistingPathWithinRoot(root, root, cosmo::path::PathEntryType::kDirectory,
                                                    resolved_root)) {
        return false;
    }

    std::error_code status_error;
    const auto link_status = std::filesystem::symlink_status(candidate, status_error);
    if (status_error && status_error != std::errc::no_such_file_or_directory) {
        return false;
    }
    const bool exists = !status_error && std::filesystem::exists(link_status);
    if (exists) {
        if (std::filesystem::is_symlink(link_status)) {
            return false;
        }
        return cosmo::path::ResolveExistingPathWithinRoot(
            resolved_root, candidate, cosmo::path::PathEntryType::kRegularFile, resolved_path);
    }
    if (must_exist) {
        return false;
    }
    return cosmo::path::ResolvePathWithinRoot(resolved_root, candidate, resolved_path);
}

bool AlgorithmLayoutMng::ResolveActionsFile(const std::string& requested_path, std::string& resolved_path) {
    const std::string layout_path    = cosmo::path::GetLayoutPath();
    const std::string actions_path   = cosmo::path::GetActionsJsonPath();
    const std::string& selected_path = requested_path.empty() ? actions_path : requested_path;

    std::string resolved_actions;
    if (!ResolveManagedRegularFile(layout_path, actions_path, false, resolved_actions) ||
        !ResolveManagedRegularFile(layout_path, selected_path, false, resolved_path) ||
        resolved_path != resolved_actions) {
        resolved_path.clear();
        return false;
    }
    return true;
}

cosmo::util::ErrorEnum AlgorithmLayoutMng::LayoutSave(const algorithm::LayoutSaveReq& req) {
    std::string jsonFilePath;
    if (!ResolveLayoutDirectory(req.filePath, false, jsonFilePath) ||
        !cosmo::path::IsSafePathComponent(req.algorithmId)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    MsgAlgorithmMetaData validatedMetadata;
    if (!DecodeAlgorithmMetadata(req.algorithmMetadata, validatedMetadata)) {
        return cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail;
    }
    if (!ValidateAlgorithmMetadataParams(validatedMetadata)) {
        return cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail;
    }
    std::string layoutFilePath = jsonFilePath;
    if (layoutFilePath.back() != '/')
        layoutFilePath += "/";
    std::string targetFile;
    std::string foundPath = cosmo::util::FindPrefixedJsonFile(jsonFilePath, req.algorithmId);
    std::string algorithmName;
    nlohmann::json existingDoc;
    bool useExistingFile = false;
    if (!foundPath.empty()) {
        std::string resolved_found_path;
        if (!ResolveManagedRegularFile(jsonFilePath, foundPath, true, resolved_found_path)) {
            return cosmo::util::ErrorEnum::InvalidParam;
        }
        foundPath  = resolved_found_path;
        targetFile = std::filesystem::path(foundPath).filename().string();
        if (cosmo::util::JsonFileUtil::ReadJsonFile(foundPath, existingDoc) ==
            cosmo::util::ErrorEnum::Success) {
            if (existingDoc.contains("algorithmName") && existingDoc["algorithmName"].is_string())
                algorithmName = existingDoc["algorithmName"].get<std::string>();
            layoutFilePath += targetFile;
            useExistingFile = true;
        }
    }
    if (!useExistingFile) {
        const std::string old_format_candidate =
            jsonFilePath + (jsonFilePath.back() == '/' ? "" : "/") + req.algorithmId + ".json";
        std::string oldFormatFile;
        if (!ResolveManagedRegularFile(jsonFilePath, old_format_candidate, false, oldFormatFile)) {
            return cosmo::util::ErrorEnum::InvalidParam;
        }
        nlohmann::json oldDoc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(oldFormatFile, oldDoc) ==
                cosmo::util::ErrorEnum::Success &&
            oldDoc.contains("algorithmName") && oldDoc["algorithmName"].is_string())
            algorithmName = oldDoc["algorithmName"].get<std::string>();
        if (!algorithmName.empty() && !cosmo::path::IsSafePathComponent(algorithmName)) {
            return cosmo::util::ErrorEnum::InvalidParam;
        }
        uint64_t timestamp = cosmo::util::GetCurrentDateTime().ToYMDHMSInt();
        const std::string target_name =
            algorithmName.empty()
                ? req.algorithmId + "_" + std::to_string(timestamp) + ".json"
                : req.algorithmId + "_" + algorithmName + "_" + std::to_string(timestamp) + ".json";
        if (!ResolveManagedRegularFile(jsonFilePath,
                                       (std::filesystem::path(jsonFilePath) / target_name).string(), false,
                                       layoutFilePath)) {
            return cosmo::util::ErrorEnum::InvalidParam;
        }
    }
    nlohmann::json doc;
    cosmo::util::ErrorEnum ret = cosmo::util::JsonFileUtil::ReadJsonFile(layoutFilePath, doc);
    const bool targetExisted   = ret == cosmo::util::ErrorEnum::Success || useExistingFile;
    const auto previousDoc     = ret == cosmo::util::ErrorEnum::Success ? doc : existingDoc;
    if (ret != cosmo::util::ErrorEnum::Success) {
        if (existingDoc.is_object() && !existingDoc.empty()) {
            doc = existingDoc;
        } else {
            doc          = nlohmann::json::object();
            int64_t code = 0;
            try {
                code = std::stoll(req.algorithmId);
            } catch (const std::exception& e) {
                LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
            }
            doc["algorithmCode"]  = code;
            doc["algorithmName"]  = algorithmName;
            int algorithmCategory = 1;
            if (!req.algorithmCategory.empty()) {
                try {
                    algorithmCategory = std::stoi(req.algorithmCategory);
                    if (algorithmCategory == 0)
                        algorithmCategory = 1;
                } catch (const std::exception& e) {
                    LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
                }
            }
            doc["algorithmCategory"] = algorithmCategory;
            doc["configVersionList"] = nlohmann::json::array();
        }
    }
    int64_t algorithmCode = 0;
    try {
        algorithmCode = std::stoll(req.algorithmId);
    } catch (const std::exception& e) {
        LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
    }
    doc["algorithmCode"] = algorithmCode;

    int algorithmCategory = 1;
    if (!req.algorithmCategory.empty()) {
        try {
            algorithmCategory = std::stoi(req.algorithmCategory);
            if (algorithmCategory == 0)
                algorithmCategory = 1;
        } catch (const std::exception& e) {
            LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
        }
    }
    doc["algorithmCategory"] = algorithmCategory;

    if (!algorithmName.empty())
        doc["algorithmName"] = algorithmName;

    doc["algorithmMetadata"]    = req.algorithmMetadata;
    doc["algorithmProcessdata"] = req.algorithmProcessdata;
    doc["atomicList"]           = req.atomicList;

    if (!doc.contains("configVersionList"))
        doc["configVersionList"] = nlohmann::json::array();
    auto& versionList = doc["configVersionList"];
    bool versionFound = false;
    for (auto& version : versionList) {
        if (version.contains("id") && version["id"].is_string() &&
            version["id"].get<std::string>() == req.confVersionId) {
            version["name"]                 = req.configVersionName;
            version["algorithmMetadata"]    = req.algorithmMetadata;
            version["algorithmProcessdata"] = req.algorithmProcessdata;
            version["atomicList"]           = req.atomicList;
            version["algorithmUpdateTime"] =
                static_cast<uint64_t>(cosmo::util::GetCurrentDateTime().ToTimeStamp()) * 1000;
            versionFound = true;
            break;
        }
    }
    if (!versionFound) {
        nlohmann::json newVersion;
        newVersion["id"]                   = req.confVersionId;
        newVersion["name"]                 = req.configVersionName;
        newVersion["algorithmCode"]        = algorithmCode;
        newVersion["algorithmMetadata"]    = req.algorithmMetadata;
        newVersion["algorithmProcessdata"] = req.algorithmProcessdata;
        newVersion["atomicList"]           = req.atomicList;
        newVersion["algorithmUpdateTime"] =
            static_cast<uint64_t>(cosmo::util::GetCurrentDateTime().ToTimeStamp()) * 1000;
        versionList.push_back(newVersion);
    }
    if (req.configVersionName == "默认")
        doc["confVersionId"] = req.confVersionId;
    ret = cosmo::util::JsonFileUtil::WriteJsonFile(layoutFilePath, doc);
    if (ret != cosmo::util::ErrorEnum::Success)
        return ret;
    const auto reloadRet = cosmo::service::ServiceRegistry::Instance()
                               .Get<cosmo::service::IAlgorithmCrud>()
                               .ReloadAlgorithmFromFile(layoutFilePath);
    if (reloadRet == cosmo::util::ErrorEnum::Success) {
        return reloadRet;
    }

    bool rollbackSucceeded = false;
    if (targetExisted) {
        rollbackSucceeded = cosmo::util::JsonFileUtil::WriteJsonFile(layoutFilePath, previousDoc) ==
                            cosmo::util::ErrorEnum::Success;
    } else {
        std::error_code error;
        rollbackSucceeded = std::filesystem::remove(layoutFilePath, error) && !error;
    }
    if (!rollbackSucceeded) {
        LOG_ERRO("{} failed to restore {} after algorithm hot-reload error:{}", __FUNCTION__, layoutFilePath,
                 static_cast<uint32_t>(reloadRet));
    }
    return reloadRet;
}

cosmo::util::ErrorEnum AlgorithmLayoutMng::GetLayoutDetail(const std::string& id, const std::string& filePath,
                                                           algorithm::LayoutDetailResult& outResult) {
    std::string jsonFilePath;
    if (!ResolveLayoutDirectory(filePath, true, jsonFilePath) || !cosmo::path::IsSafePathComponent(id)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::string foundPath = cosmo::util::FindPrefixedJsonFile(jsonFilePath, id);
    const std::string candidate =
        foundPath.empty() ? (std::filesystem::path(jsonFilePath) / (id + ".json")).string() : foundPath;
    std::string layoutFilePath;
    if (!ResolveManagedRegularFile(jsonFilePath, candidate, false, layoutFilePath)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    nlohmann::json doc;
    if (cosmo::util::JsonFileUtil::ReadJsonFile(layoutFilePath, doc) != cosmo::util::ErrorEnum::Success) {
        return cosmo::util::ErrorEnum::Success;
    }
    outResult.algorithmCode = GetStr(doc, "algorithmCode");
    if (outResult.algorithmCode.empty())
        outResult.algorithmCode = id;
    if (doc.contains("algorithmName") && doc["algorithmName"].is_string())
        outResult.algorithmName = doc["algorithmName"].get<std::string>();
    outResult.algorithmCategory = GetStr(doc, "algorithmCategory");
    outResult.algorithmUsage    = GetStr(doc, "algorithmUsage");
    if (doc.contains("supplier") && doc["supplier"].is_string())
        outResult.supplier = doc["supplier"].get<std::string>();
    if (doc.contains("remark") && doc["remark"].is_string())
        outResult.remark = doc["remark"].get<std::string>();
    if (doc.contains("confVersionId") && doc["confVersionId"].is_string())
        outResult.confVersionId = doc["confVersionId"].get<std::string>();
    if (doc.contains("algorithmMetadata") && doc["algorithmMetadata"].is_string())
        outResult.algorithmMetadata = doc["algorithmMetadata"].get<std::string>();
    outResult.algorithmProcessdata =
        (doc.contains("algorithmProcessdata") && doc["algorithmProcessdata"].is_string())
            ? doc["algorithmProcessdata"].get<std::string>()
            : "[]";
    if (outResult.algorithmProcessdata.empty())
        outResult.algorithmProcessdata = "[]";
    outResult.atomicList = (doc.contains("atomicList") && doc["atomicList"].is_string())
                               ? doc["atomicList"].get<std::string>()
                               : "[]";
    if (outResult.atomicList.empty())
        outResult.atomicList = "[]";
    if (doc.contains("configVersionList") && doc["configVersionList"].is_array()) {
        for (const auto& version : doc["configVersionList"]) {
            alg::LayoutDetailVersion v;
            if (version.contains("id") && version["id"].is_string())
                v.id = version["id"].get<std::string>();
            if (version.contains("name") && version["name"].is_string())
                v.name = version["name"].get<std::string>();
            if (version.contains("algorithmCode") && version["algorithmCode"].is_string())
                v.algorithmCode = version["algorithmCode"].get<std::string>();
            if (version.contains("algorithmMetadata") && version["algorithmMetadata"].is_string())
                v.algorithmMetadata = version["algorithmMetadata"].get<std::string>();
            if (version.contains("algorithmProcessdata") && version["algorithmProcessdata"].is_string())
                v.algorithmProcessdata = version["algorithmProcessdata"].get<std::string>();
            if (version.contains("atomicList") && version["atomicList"].is_string())
                v.atomicList = version["atomicList"].get<std::string>();
            if (version.contains("algorithmUpdateTime") && version["algorithmUpdateTime"].is_number())
                v.algorithmUpdateTime = version["algorithmUpdateTime"].get<uint64_t>();
            outResult.configVersionList.push_back(v);
        }
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmLayoutMng::GetLayoutList(const std::string& /*supplier*/, int usage,
                                                         const std::string& filePath,
                                                         algorithm::LayoutListResult& outResult) {
    std::string algorithmDir;
    if (!ResolveLayoutDirectory(filePath, true, algorithmDir)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    DIR* dir = opendir(algorithmDir.c_str());
    if (!dir)
        return cosmo::util::ErrorEnum::Success;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename[0] == '.' || filename.size() < 5 || filename.substr(filename.size() - 5) != ".json")
            continue;
        if (filename == "actions.json" || filename == "atomicModels.json" || filename == "list.json" ||
            filename == "algorithms.json")
            continue;
        const std::string candidate = (std::filesystem::path(algorithmDir) / filename).string();
        std::string jsonFilePath;
        if (!ResolveManagedRegularFile(algorithmDir, candidate, true, jsonFilePath)) {
            continue;
        }
        nlohmann::json doc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(jsonFilePath, doc) != cosmo::util::ErrorEnum::Success)
            continue;
        if (!doc.contains("algorithmCode"))
            continue;
        if (usage >= 0) {
            int itemUsage = -1;
            if (doc.contains("algorithmUsage")) {
                if (doc["algorithmUsage"].is_number_integer())
                    itemUsage = doc["algorithmUsage"].get<int>();
                else if (doc["algorithmUsage"].is_string()) {
                    try {
                        itemUsage = std::stoi(doc["algorithmUsage"].get<std::string>());
                    } catch (...) {
                    }
                }
            }
            if (itemUsage != usage)
                continue;
        }
        alg::LayoutListItem item;
        item.algorithmCode = GetStr(doc, "algorithmCode");
        if (doc.contains("algorithmName") && doc["algorithmName"].is_string())
            item.algorithmName = doc["algorithmName"].get<std::string>();
        if (doc.contains("supplier") && doc["supplier"].is_string())
            item.supplier = doc["supplier"].get<std::string>();
        item.algorithmUsage = GetStr(doc, "algorithmUsage");
        if (doc.contains("remark") && doc["remark"].is_string())
            item.description = doc["remark"].get<std::string>();
        else if (doc.contains("description") && doc["description"].is_string())
            item.description = doc["description"].get<std::string>();
        outResult.list.push_back(item);
    }
    closedir(dir);
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service::detail
