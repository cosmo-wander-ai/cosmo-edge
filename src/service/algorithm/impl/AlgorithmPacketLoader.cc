// AlgorithmPacketLoader — Algorithm packet loading and parsing — extracted from AlgorithmServiceImpl.

#include "service/algorithm/impl/AlgorithmPacketLoader.h"

#include <algorithm>
#include <filesystem>
#include <list>

#include "nlohmann/json.hpp"
#include "service/algorithm/IActionService.h"
#include "service/algorithm/impl/AlgorithmJsonCodec.h"
#include "service/algorithm/impl/AlgorithmLayoutMng.h"
#include "service/algorithm/impl/AlgorithmValidator.h"
#include "service/detail/ServiceRegistry.h"
#include "util/DateTimeFormat.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"

namespace cosmo::service::detail {

std::string AlgorithmPacketLoader::UnzipPackageFile(const std::string& filePath) {
    // Determine output directory: strip archive extension(s)
    std::string upload_path;
    // Handle .tar.gz / .tgz double extension
    if (filePath.size() > 7 && filePath.substr(filePath.size() - 7) == ".tar.gz") {
        upload_path = filePath.substr(0, filePath.size() - 7);
    } else if (filePath.size() > 4 && filePath.substr(filePath.size() - 4) == ".tgz") {
        upload_path = filePath.substr(0, filePath.size() - 4);
    } else {
        upload_path = cosmo::util::RemoveExtension(filePath);
    }

    std::string cmd;
    std::error_code ec;
    std::filesystem::create_directories(upload_path, ec);
    // Choose decompression command based on file extension
    if (filePath.size() > 7 && filePath.substr(filePath.size() - 7) == ".tar.gz") {
        cmd =
            "tar xzf " + cosmo::util::ShellEscape(filePath) + " -C " + cosmo::util::ShellEscape(upload_path);
    } else if (filePath.size() > 4 && filePath.substr(filePath.size() - 4) == ".tgz") {
        cmd =
            "tar xzf " + cosmo::util::ShellEscape(filePath) + " -C " + cosmo::util::ShellEscape(upload_path);
    } else {
        // Default: treat as zip
        cmd = "unzip -d " + cosmo::util::ShellEscape(upload_path) + " " + cosmo::util::ShellEscape(filePath);
    }

    std::string out_str;
    auto ret = cosmo::util::Exec(cmd, out_str);
    if (ret != 0 && ((filePath.size() > 7 && filePath.substr(filePath.size() - 7) == ".tar.gz") ||
                     (filePath.size() > 4 && filePath.substr(filePath.size() - 4) == ".tgz"))) {
        LOG_WARN("{} Failed Result:{}, retry without gzip", cmd, out_str);
        out_str.clear();
        cmd = "tar xf " + cosmo::util::ShellEscape(filePath) + " -C " + cosmo::util::ShellEscape(upload_path);
        ret = cosmo::util::Exec(cmd, out_str);
    }
    if (ret != 0) {
        LOG_WARN("{} Failed Result:{}", cmd, out_str);
        return "";
    }
    LOG_INFO("{} extract file OK {}", cmd, upload_path);
    return upload_path;
}

std::vector<algorithm::AlgorithmPacketInfo> AlgorithmPacketLoader::LoadFromZipDirectory(
    const std::string& path) {
    std::vector<algorithm::AlgorithmPacketInfo> algorithms;
    LOG_INFO("path:{}", path);
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
            if (!entry.is_regular_file(ec))
                continue;
            files.push_back(entry.path());
        }
    }
    LOG_INFO("path:{} total {} algorithm package files", path, files.size());
    for (auto& file : files) {
        auto model_path_name = file.string();
        algorithm::AlgorithmPacketInfo algorithmInfo;
        std::error_condition ret =
            detail::AlgorithmValidator::ParseAndValidatePacket(model_path_name, algorithmInfo);
        if (cosmo::util::ErrorEnum::Success != ret) {
            LOG_WARN("Load {} Get {}", model_path_name, ret.message());
            continue;
        }
        if (algorithmInfo.processdata) {
            ServiceRegistry::Instance().Get<IActionService>().UpdateActionAlg(*algorithmInfo.processdata);
        }
        // Backfill empty id from algorithmCode to prevent dedup false-positives.
        if (algorithmInfo.id.empty() && algorithmInfo.algorithmCode > 0) {
            algorithmInfo.id = std::to_string(algorithmInfo.algorithmCode);
        }
        algorithmInfo.filePath = model_path_name;
        // Deduplicate: keep the newer version
        auto it = std::find_if(
            algorithms.begin(), algorithms.end(),
            [&](const algorithm::AlgorithmPacketInfo& info) { return info.id == algorithmInfo.id; });
        if (it != algorithms.end()) {
            if ((*it).createTime > algorithmInfo.createTime) {
                cosmo::util::RemoveFile(algorithmInfo.filePath);
            } else {
                cosmo::util::RemoveFile((*it).filePath);
                *it = algorithmInfo;
            }
            continue;
        }
        algorithms.push_back(algorithmInfo);
    }
    LOG_INFO("path:{} successfully loaded {} algorithm packages", path, algorithms.size());
    return algorithms;
}

namespace {

    // Helper: get string from json with type coercion (int->string)
    std::string GetStringOrInt(const nlohmann::json& doc, const std::string& key) {
        if (!doc.contains(key))
            return "";
        const auto& v = doc[key];
        if (v.is_string())
            return v.get<std::string>();
        if (v.is_number_integer())
            return std::to_string(v.get<int64_t>());
        return "";
    }

    int GetIntOrString(const nlohmann::json& doc, const std::string& key, int default_val) {
        if (!doc.contains(key))
            return default_val;
        const auto& v = doc[key];
        if (v.is_number_integer())
            return v.get<int>();
        if (v.is_string()) {
            try {
                return std::stoi(v.get<std::string>());
            } catch (...) {
                return default_val;
            }
        }
        return default_val;
    }

}  // namespace

bool AlgorithmPacketLoader::ParsePacketFromJson(const nlohmann::json& doc, const std::string& filePath,
                                                std::string& algorithmCode,
                                                algorithm::AlgorithmPacketInfo& newPacket) {
    algorithmCode = GetStringOrInt(doc, "algorithmId");
    if (algorithmCode.empty())
        algorithmCode = GetStringOrInt(doc, "algorithmCode");
    if (algorithmCode.empty())
        return false;

    newPacket.id = algorithmCode;
    try {
        newPacket.algorithmCode = std::stoi(algorithmCode);
    } catch (const std::exception&) {
        // ignore
    }
    if (doc.contains("algorithmName") && doc["algorithmName"].is_string())
        newPacket.algorithmName = doc["algorithmName"].get<std::string>();
    // Fallback: parse algorithmName from filename if JSON field is missing
    // Filename format: {code}_{name}_{timestamp}.json or {code}_{name}.json
    if (newPacket.algorithmName.empty() && !filePath.empty()) {
        std::string fname = std::filesystem::path(filePath).stem().string();  // remove .json
        // Skip the "{code}_" prefix
        size_t first_underscore = fname.find('_');
        if (first_underscore != std::string::npos && first_underscore + 1 < fname.size()) {
            std::string rest = fname.substr(first_underscore + 1);
            // Remove trailing "_{timestamp}" if present (timestamp is all digits)
            size_t last_underscore = rest.rfind('_');
            if (last_underscore != std::string::npos && last_underscore + 1 < rest.size()) {
                std::string tail = rest.substr(last_underscore + 1);
                bool all_digits  = !tail.empty() && std::all_of(tail.begin(), tail.end(), ::isdigit);
                if (all_digits)
                    rest = rest.substr(0, last_underscore);
            }
            newPacket.algorithmName = rest;
        }
    }
    newPacket.algorithmCategory = GetIntOrString(doc, "algorithmCategory", 0);
    newPacket.algorithmUsage    = GetIntOrString(doc, "algorithmUsage", 0);
    if (doc.contains("remark") && doc["remark"].is_string())
        newPacket.remark = doc["remark"].get<std::string>();
    if (doc.contains("eventType") && doc["eventType"].is_string())
        newPacket.eventType = doc["eventType"].get<std::string>();
    if (doc.contains("supplier") && doc["supplier"].is_string())
        newPacket.supplier = doc["supplier"].get<std::string>();
    else
        newPacket.supplier = "CWAI";
    if (doc.contains("algorithmMetadata") && doc["algorithmMetadata"].is_string())
        newPacket.algorithmMetadata = doc["algorithmMetadata"].get<std::string>();
    if (doc.contains("algorithmProcessdata") && doc["algorithmProcessdata"].is_string())
        newPacket.algorithmProcessdata = doc["algorithmProcessdata"].get<std::string>();
    if (doc.contains("atomicList") && doc["atomicList"].is_string())
        newPacket.atomicList = doc["atomicList"].get<std::string>();
    if (doc.contains("confVersionId") && doc["confVersionId"].is_string())
        newPacket.confVersionId = doc["confVersionId"].get<std::string>();
    if (doc.contains("confVersionName") && doc["confVersionName"].is_string())
        newPacket.confVersionName = doc["confVersionName"].get<std::string>();
    if (doc.contains("configVersionList") && doc["configVersionList"].is_array() &&
        !doc["configVersionList"].empty()) {
        std::string default_version_id = newPacket.confVersionId;
        if (default_version_id.empty())
            default_version_id = GetStringOrInt(doc, "confVersionId");
        for (const auto& version : doc["configVersionList"]) {
            if (version.contains("id") && version["id"].is_string() &&
                version["id"].get<std::string>() == default_version_id) {
                if (version.contains("algorithmUpdateTime") && version["algorithmUpdateTime"].is_number()) {
                    newPacket.algorithmUpdateTime =
                        std::to_string(version["algorithmUpdateTime"].get<uint64_t>());
                }
                break;
            }
        }
        if (newPacket.algorithmUpdateTime.empty() && !doc["configVersionList"].empty()) {
            const auto& first = doc["configVersionList"][0];
            if (first.contains("algorithmUpdateTime") && first["algorithmUpdateTime"].is_number())
                newPacket.algorithmUpdateTime = std::to_string(first["algorithmUpdateTime"].get<uint64_t>());
        }
    }
    if (newPacket.algorithmUpdateTime.empty())
        newPacket.algorithmUpdateTime =
            std::to_string(static_cast<int64_t>(cosmo::util::GetCurrentDateTime().ToTimeStamp()) * 1000);
    if (doc.contains("createTime") && doc["createTime"].is_number_integer())
        newPacket.createTime = doc["createTime"].get<int64_t>();
    if (doc.contains("updateTime") && doc["updateTime"].is_number_integer())
        newPacket.updateTime = doc["updateTime"].get<int64_t>();

    newPacket.filePath = filePath;
    return true;
}

void AlgorithmPacketLoader::ActivateProcessdata(algorithm::AlgorithmPacketInfo& packet) {
    if (packet.algorithmProcessdata.empty() || packet.algorithmProcessdata == "[]")
        return;
    std::string process_data_json =
        detail::NormalizeAlgorithmProcessdataParamValues(packet.algorithmProcessdata);
    packet.processdata = std::make_shared<cosmo::ActionAlg>();
    if (cosmo::util::DecodeJson(process_data_json, packet.processdata->workFlow)) {
        packet.processdata->algorithmCode       = packet.id;
        packet.processdata->algorithmName       = packet.algorithmName;
        packet.processdata->algorithmUpdateTime = packet.algorithmUpdateTime;
        packet.processdata->category            = std::to_string(packet.algorithmCategory);
        // Dispatch to the corresponding action manager based on algorithmUsage
        if (packet.algorithmUsage == 2) {
            ServiceRegistry::Instance().Get<IActionService>().UpdatePicActionAlg(*packet.processdata);
        } else {
            ServiceRegistry::Instance().Get<IActionService>().UpdateActionAlg(*packet.processdata);
        }
    }
}

std::vector<algorithm::AlgorithmPacketInfo> AlgorithmPacketLoader::LoadFromJsonDirectory(
    const std::string& directoryPath) {
    std::vector<algorithm::AlgorithmPacketInfo> results;
    if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath))
        return results;

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath, ec)) {
        if (!entry.is_regular_file())
            continue;
        std::string filename = entry.path().filename().string();
        if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".json")
            continue;
        if (filename == "actions.json" || filename == "atomicModels.json" || filename == "list.json" ||
            filename == "algorithms.json")
            continue;

        std::string file_path = entry.path().string();
        nlohmann::json doc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(file_path, doc) != cosmo::util::ErrorEnum::Success ||
            !doc.is_object())
            continue;

        std::string algorithm_code;
        algorithm::AlgorithmPacketInfo newPacket;
        if (!ParsePacketFromJson(doc, file_path, algorithm_code, newPacket))
            continue;
        ActivateProcessdata(newPacket);
        results.push_back(std::move(newPacket));
    }
    LOG_INFO("LoadFromJsonDirectory {} found {} new algorithms", directoryPath, results.size());
    return results;
}

cosmo::util::ErrorEnum AlgorithmPacketLoader::ReloadFromFile(const std::string& filePath,
                                                             std::string& outAlgorithmCode,
                                                             algorithm::AlgorithmPacketInfo& outPacket) {
    nlohmann::json doc;
    if (cosmo::util::JsonFileUtil::ReadJsonFile(filePath, doc) != cosmo::util::ErrorEnum::Success ||
        !doc.is_object()) {
        LOG_WARN("Failed to read or parse JSON file: {}", filePath);
        return cosmo::util::ErrorEnum::Failed;
    }

    if (!ParsePacketFromJson(doc, filePath, outAlgorithmCode, outPacket)) {
        LOG_WARN("Algorithm file {} missing algorithmId/algorithmCode", filePath);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    ActivateProcessdata(outPacket);
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service::detail
