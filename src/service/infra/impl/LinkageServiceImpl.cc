// LinkageServiceImpl — Linkage service implementation — manages alarm output device

#include "service/infra/impl/LinkageServiceImpl.h"

#include <filesystem>

#include "linkage/LinkAgeAudioDevice.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/LinkageUtil.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

// Display alarm tone on frontend large screen.

namespace cosmo::service {
using cosmo::linkage::LinkAgeAudioDeviceOperation;
using cosmo::linkage::LinkAgeAudioDeviceTone;
using detail::NormalizeWorkflowJson;

LinkageServiceImpl::LinkageServiceImpl() : async_queue_("Linkage Alarm QUE", 100) {
    LoadConfig();
    const std::string linkage_storages_file_path = cosmo::path::GetLinkageStoragesJsonPath();
    if (!LoadSupportedStorageFromJson(linkage_storages_file_path)) {
        LOG_WARN("Linkage storages json not found or invalid: {}, return empty list",
                 linkage_storages_file_path);
    }
    async_queue_.SetProcessor([this](cosmo::LinkageAlarm&& data) { DoAlarm(data.channel_id, data.alg_id); });
    LOG_INFO("{}", "LinkageService Init Ok");
}

LinkageServiceImpl::~LinkageServiceImpl() {
    async_queue_.Stop();
    LOG_INFO("{}", "LinkageService Quit");
}

void LinkageServiceImpl::LoadConfig() {
    auto cfg_path =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_file_name_).string();
    if (!cosmo::util::LoadStructFromJsonFile(cfg_path, config_)) {
        LOG_WARN("Failed to load linkage config from {}", cfg_path);
    }
    // Load strategies
    for (auto& strategy : config_.strategies) {
        auto str_info = NormalizeWorkflowJson(ReadStrategy(strategy.strategy_id));
        if (!cosmo::util::DecodeJson(str_info, strategy.strategy.workflow)) {
            LOG_INFO("Read Strage {}/{} Failed", strategy.strategy_id, strategy.name);
            continue;
        }
        strategy.task = MakeTask(strategy.name, strategy.strategy);
    }
}

void LinkageServiceImpl::SaveConfig() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_file_name_).string();
    if (!cosmo::util::SaveStructToJsonFile(path, config_)) {
        LOG_WARN("Failed to save linkage config to {}", path);
    }
}

void LinkageServiceImpl::SaveStrategy(const std::string& id, const std::string& linkage_strategy) {
    std::string file_name = id + ".json";

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / file_name).string();
    cosmo::util::WriteFile(path, linkage_strategy);
}

void LinkageServiceImpl::RmvStrategy(const std::string& id) {
    std::string file_name = id + ".json";

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / file_name).string();
    cosmo::util::RemoveFile(path);
}

std::string LinkageServiceImpl::ReadStrategy(const std::string& id) {
    std::string file_name = id + ".json";
    auto main_cfg = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / file_name).string();
    return cosmo::util::ReadFile(main_cfg);
}

bool LinkageServiceImpl::LoadSupportedStorageFromJson(const std::string& file_path) {
    nlohmann::json doc;
    cosmo::util::ErrorEnum ret = cosmo::util::JsonFileUtil::ReadJsonArray(file_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success) {
        support_storages_.clear();
        return false;
    }

    std::vector<cosmo::StorageList> storages;
    storages.reserve(doc.size());
    for (const auto& item : doc) {
        if (!item.is_object()) {
            continue;
        }
        if (!item.contains("id") || !item["id"].is_string() || !item.contains("actionName") ||
            !item["actionName"].is_string()) {
            continue;
        }

        cosmo::StorageList storage;
        storage.id         = item["id"].get<std::string>();
        storage.actionName = item["actionName"].get<std::string>();
        if (item.contains("businessCategory") && item["businessCategory"].is_string()) {
            storage.businessCategory = item["businessCategory"].get<std::string>();
        }
        if (item.contains("remark") && item["remark"].is_string()) {
            storage.remark = item["remark"].get<std::string>();
        }
        if (item.contains("inputParamConfig") && item["inputParamConfig"].is_string()) {
            storage.inputParamConfig = item["inputParamConfig"].get<std::string>();
        }
        storages.push_back(std::move(storage));
    }

    support_storages_ = std::move(storages);
    LOG_INFO("Load linkage storages from json success, file:{}, size:{}", file_path,
             support_storages_.size());
    return true;
}

}  // namespace cosmo::service

namespace cosmo {
void to_json(nlohmann::json& j, const LinkageStrategyConfigUnit& v) {
    j["strategyId"]      = v.strategy_id;
    j["name"]            = v.name;
    j["timestamp"]       = v.timestamp;
    j["createTimestamp"] = v.create_timestamp;
    j["bOpen"]           = v.is_open;
}
void from_json(const nlohmann::json& j, LinkageStrategyConfigUnit& v) {
    if (j.contains("strategyId") && !j["strategyId"].is_null())
        j.at("strategyId").get_to(v.strategy_id);
    if (j.contains("name") && !j["name"].is_null())
        j.at("name").get_to(v.name);
    if (j.contains("timestamp") && !j["timestamp"].is_null())
        j.at("timestamp").get_to(v.timestamp);
    if (j.contains("createTimestamp") && !j["createTimestamp"].is_null())
        j.at("createTimestamp").get_to(v.create_timestamp);
    if (j.contains("bOpen") && !j["bOpen"].is_null())
        j.at("bOpen").get_to(v.is_open);
}

void to_json(nlohmann::json& j, const LinkageStrategyConfig& v) {
    j["strategies"] = v.strategies;
}
void from_json(const nlohmann::json& j, LinkageStrategyConfig& v) {
    if (j.contains("strategies") && !j["strategies"].is_null())
        j.at("strategies").get_to(v.strategies);
}
}  // namespace cosmo
