// AIBox platform layout operations - stateless file operations extracted from
// AlgorithmPacketMng

#include "service/algorithm/impl/AlgorithmLayoutMng.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

cosmo::util::ErrorEnum AlgorithmLayoutMng::LayoutSave(const algorithm::LayoutSaveReq& req) {
    std::string jsonFilePath   = req.filePath.empty() ? cosmo::path::GetAlgorithmPath() : req.filePath;
    std::string layoutFilePath = jsonFilePath;
    if (layoutFilePath.back() != '/')
        layoutFilePath += "/";
    std::string targetFile;
    std::string foundPath = cosmo::util::FindPrefixedJsonFile(jsonFilePath, req.algorithmId);
    std::string algorithmName;
    nlohmann::json existingDoc;
    bool useExistingFile = false;
    if (!foundPath.empty()) {
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
        std::string oldFormatFile =
            jsonFilePath + (jsonFilePath.back() == '/' ? "" : "/") + req.algorithmId + ".json";
        nlohmann::json oldDoc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(oldFormatFile, oldDoc) ==
                cosmo::util::ErrorEnum::Success &&
            oldDoc.contains("algorithmName") && oldDoc["algorithmName"].is_string())
            algorithmName = oldDoc["algorithmName"].get<std::string>();
        uint64_t timestamp = cosmo::util::GetCurrentDateTime().ToYMDHMSInt();
        layoutFilePath += algorithmName.empty() ? req.algorithmId + "_" + std::to_string(timestamp) + ".json"
                                                : req.algorithmId + "_" + algorithmName + "_" +
                                                      std::to_string(timestamp) + ".json";
    }
    nlohmann::json doc;
    cosmo::util::ErrorEnum ret = cosmo::util::JsonFileUtil::ReadJsonFile(layoutFilePath, doc);
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
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IAlgorithmCrud>().ReloadAlgorithmFromFile(
        layoutFilePath);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmLayoutMng::GetLayoutDetail(const std::string& id, const std::string& filePath,
                                                           algorithm::LayoutDetailResult& outResult) {
    std::string jsonFilePath   = filePath.empty() ? cosmo::path::GetAlgorithmPath() : filePath;
    std::string layoutFilePath = jsonFilePath + (jsonFilePath.back() == '/' ? "" : "/");
    std::string foundPath      = cosmo::util::FindPrefixedJsonFile(jsonFilePath, id);
    std::string targetFile;
    if (!foundPath.empty())
        targetFile = std::filesystem::path(foundPath).filename().string();
    if (targetFile.empty())
        targetFile = id + ".json";
    layoutFilePath += targetFile;
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
    std::string algorithmDir = filePath.empty() ? cosmo::path::GetAlgorithmPath() : filePath;
    DIR* dir                 = opendir(algorithmDir.c_str());
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
        std::string jsonFilePath = algorithmDir + (algorithmDir.back() == '/' ? "" : "/") + filename;
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
