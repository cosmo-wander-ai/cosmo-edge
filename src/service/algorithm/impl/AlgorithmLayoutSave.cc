// AlgorithmLayoutSave.cc — Layout save logic for AlgorithmLayoutMng.
// Split from AlgorithmLayoutMng.cc to reduce file size (DEBT-007).

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "service/algorithm/impl/AlgorithmLayoutMng.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service::detail {
namespace alg = algorithm;

cosmo::util::ErrorEnum AlgorithmLayoutMng::LayoutExportSingle(
    const std::string& code, const std::string& name, const std::string& /*category*/,
    const std::string& /*supplier*/, const std::string& /*versionId*/, alg::LayoutExportResult& outResult) {
    std::string algorithmDir = cosmo::path::GetAlgorithmPath();
    std::string jsonFilePath = cosmo::util::FindPrefixedJsonFile(algorithmDir, code);
    if (jsonFilePath.empty())
        return cosmo::util::ErrorEnum::ParameterException;
    std::string algorithmName = name;
    if (algorithmName.empty()) {
        nlohmann::json nameDoc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(jsonFilePath, nameDoc) ==
                cosmo::util::ErrorEnum::Success &&
            nameDoc.contains("algorithmName") && nameDoc["algorithmName"].is_string())
            algorithmName = nameDoc["algorithmName"].get<std::string>();
    }
    std::string cleanAlgorithmName;
    for (char c : algorithmName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            cleanAlgorithmName += '_';
        else
            cleanAlgorithmName += c;
    }
    if (cleanAlgorithmName.empty())
        cleanAlgorithmName = "algorithm";
    std::string tempDir = cosmo::path::GetTemporaryDirPath() + "/algorithm_export_" + code + "_" +
                          std::to_string(time(nullptr));
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    if (ec)
        return cosmo::util::ErrorEnum::ParameterException;

    std::string destJsonFile =
        (std::filesystem::path(tempDir) / (code + "_" + cleanAlgorithmName + ".json")).string();
    std::filesystem::copy_file(jsonFilePath, destJsonFile, std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
        std::filesystem::remove_all(tempDir, ec);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::string timestamp = std::to_string(time(nullptr));
    std::string archiveFilePath =
        cosmo::path::GetTemporaryDirPath() + "/algorithm_" + code + "_" + timestamp + ".tar.gz";
    std::string jsonFileName = code + "_" + cleanAlgorithmName + ".json";
    std::string outStr;
    if (cosmo::util::Exec("tar -czf " + cosmo::util::ShellEscape(archiveFilePath) + " -C " +
                              cosmo::util::ShellEscape(tempDir) + " " +
                              cosmo::util::ShellEscape(jsonFileName) + " 2>&1",
                          outStr) != 0) {
        std::filesystem::remove_all(tempDir, ec);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::filesystem::remove_all(tempDir, ec);
    outResult.filePath = archiveFilePath;
    outResult.fileName = cosmo::util::GetFileName(archiveFilePath);
    return cosmo::util::ErrorEnum::Success;
}

namespace {

    // Read a JSON field that may be string or int, return as string (DEBT-C08 B3)
    std::string ReadJsonFieldAsString(const nlohmann::json& doc, const std::string& key) {
        if (!doc.contains(key))
            return "";
        const auto& val = doc[key];
        if (val.is_string())
            return val.get<std::string>();
        if (val.is_number_integer())
            return std::to_string(val.get<int>());
        return "";
    }

    // Check if a document matches all export filter criteria
    bool MatchesExportFilter(const nlohmann::json& doc, const std::unordered_set<std::string>& idSet,
                             bool filterByIds, const std::string& algorithmName, const std::string& supplier,
                             const std::string& algorithmUsage, const std::string& algorithmCategory) {
        if (!doc.contains("algorithmCode"))
            return false;

        if (filterByIds) {
            std::string docCode = ReadJsonFieldAsString(doc, "algorithmCode");
            if (idSet.find(docCode) == idSet.end())
                return false;
        }
        if (!algorithmName.empty()) {
            std::string docName = ReadJsonFieldAsString(doc, "algorithmName");
            if (docName.find(algorithmName) == std::string::npos)
                return false;
        }
        if (!supplier.empty()) {
            if (ReadJsonFieldAsString(doc, "supplier") != supplier)
                return false;
        }
        if (!algorithmUsage.empty()) {
            if (ReadJsonFieldAsString(doc, "algorithmUsage") != algorithmUsage)
                return false;
        }
        if (!algorithmCategory.empty()) {
            if (ReadJsonFieldAsString(doc, "algorithmCategory") != algorithmCategory)
                return false;
        }
        return true;
    }

}  // namespace

cosmo::util::ErrorEnum AlgorithmLayoutMng::LayoutExportAll(const std::string& algorithmName,
                                                           const std::string& supplier,
                                                           const std::string& algorithmUsage,
                                                           const std::string& algorithmCategory,
                                                           const std::vector<std::string>& algorithmIds,
                                                           alg::LayoutExportResult& outResult) {
    std::string algorithmDir = cosmo::path::GetAlgorithmPath();
    DIR* dir                 = opendir(algorithmDir.c_str());
    if (!dir) {
        LOG_ERRO("LayoutExportAll: cannot open algorithm directory {}", algorithmDir);
        return cosmo::util::ErrorEnum::ParameterException;
    }

    std::unordered_set<std::string> idSet(algorithmIds.begin(), algorithmIds.end());
    bool filterByIds  = !idSet.empty();
    bool hasAnyFilter = filterByIds || !algorithmName.empty() || !supplier.empty() ||
                        !algorithmUsage.empty() || !algorithmCategory.empty();

    // Create temp directory for export
    std::string timestamp = std::to_string(time(nullptr));
    std::string tempDir   = cosmo::path::GetTemporaryDirPath() + "/algorithm_export_all_" + timestamp;
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        closedir(dir);
        LOG_ERRO("LayoutExportAll: cannot create temp directory {}", tempDir);
        return cosmo::util::ErrorEnum::ParameterException;
    }

    int fileCount = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename[0] == '.' || filename.size() < 5 || filename.substr(filename.size() - 5) != ".json")
            continue;
        if (filename == "actions.json" || filename == "atomicModels.json" || filename == "list.json" ||
            filename == "algorithms.json")
            continue;

        std::string jsonFilePath = algorithmDir + (algorithmDir.back() == '/' ? "" : "/") + filename;

        if (hasAnyFilter) {
            nlohmann::json doc;
            if (cosmo::util::JsonFileUtil::ReadJsonFile(jsonFilePath, doc) != cosmo::util::ErrorEnum::Success)
                continue;
            if (!MatchesExportFilter(doc, idSet, filterByIds, algorithmName, supplier, algorithmUsage,
                                     algorithmCategory))
                continue;
        }

        std::string destFile = (std::filesystem::path(tempDir) / filename).string();
        std::filesystem::copy_file(jsonFilePath, destFile, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (!ec)
            fileCount++;
    }
    closedir(dir);

    if (fileCount == 0) {
        std::filesystem::remove_all(tempDir, ec);
        LOG_WARN("{}", "LayoutExportAll: no algorithm files found to export");
        return cosmo::util::ErrorEnum::ParameterException;
    }

    // Package as tar.gz
    std::string archiveFilePath =
        cosmo::path::GetTemporaryDirPath() + "/algorithms_export_" + timestamp + ".tar.gz";
    std::string outStr;
    if (cosmo::util::Exec("tar -czf " + cosmo::util::ShellEscape(archiveFilePath) + " -C " +
                              cosmo::util::ShellEscape(tempDir) + " . 2>&1",
                          outStr) != 0) {
        LOG_ERRO("LayoutExportAll: tar failed: {}", outStr);
        std::filesystem::remove_all(tempDir, ec);
        return cosmo::util::ErrorEnum::ParameterException;
    }

    std::filesystem::remove_all(tempDir, ec);
    outResult.filePath = archiveFilePath;
    outResult.fileName = "algorithms_export_" + timestamp + ".tar.gz";
    LOG_INFO("LayoutExportAll: exported {} algorithm(s) to {}", fileCount, archiveFilePath);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmLayoutMng::GetAtomicActionList(int actionUsage, const std::string& filePath,
                                                               alg::AtomicActionListResult& outResult) {
    const std::string actionsFilePath = filePath.empty() ? cosmo::path::GetActionsJsonPath() : filePath;
    nlohmann::json doc;
    if (cosmo::util::JsonFileUtil::ReadJsonArray(actionsFilePath, doc) != cosmo::util::ErrorEnum::Success) {
        return cosmo::util::ErrorEnum::Success;
    }
    for (const auto& action : doc) {
        if (!action.is_object())
            continue;
        int itemUsage = 0;
        if (action.contains("actionUsage")) {
            const auto& usageVal = action["actionUsage"];
            if (usageVal.is_number_integer())
                itemUsage = usageVal.get<int>();
            else if (usageVal.is_string()) {
                try {
                    itemUsage = std::stoi(usageVal.get<std::string>());
                } catch (const std::exception& e) {
                    LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
                    itemUsage = 0;
                }
            }
        }
        int itemType = 0;
        if (action.contains("actionType")) {
            const auto& typeVal = action["actionType"];
            if (typeVal.is_number_integer())
                itemType = typeVal.get<int>();
            else if (typeVal.is_string()) {
                try {
                    itemType = std::stoi(typeVal.get<std::string>());
                } catch (const std::exception& e) {
                    LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
                    itemType = 0;
                }
            }
        }
        if (actionUsage > 0 && itemUsage > 0 && itemUsage != actionUsage)
            continue;
        alg::AtomicAction item;
        item.actionUsage = itemUsage;
        item.actionType  = itemType;
        if (action.contains("id") && action["id"].is_string())
            item.id = action["id"].get<std::string>();
        if (action.contains("name") && action["name"].is_string())
            item.name = action["name"].get<std::string>();
        if (action.contains("actionName") && action["actionName"].is_string())
            item.actionName = action["actionName"].get<std::string>();
        else
            item.actionName = item.name;
        if (item.name.empty())
            item.name = item.actionName;
        if (action.contains("inputParamConfig") && action["inputParamConfig"].is_string())
            item.inputParamConfig = action["inputParamConfig"].get<std::string>();
        outResult.list.push_back(item);
    }
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service::detail
