// LinkageServiceImpl — Linkage service implementation — manages alarm output device

#include "service/infra/impl/LinkageServiceImpl.h"

#include <filesystem>
#include <set>

#include "linkage/AlarmOutputController.h"
#include "linkage/LinkAgeAudioDevice.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/LinkageUtil.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

// Display alarm tone on frontend large screen.

namespace cosmo::service {
using cosmo::linkage::LinkAgeAudioDeviceOperation;
using cosmo::linkage::LinkAgeAudioDeviceTone;
using detail::NormalizeWorkflowJson;

namespace {
    constexpr size_t kMaxStrategyFileBytes = 1024 * 1024;

    bool WriteFileAtomically(const std::string& target, const std::string& content) {
        namespace fs = std::filesystem;
        const fs::path target_path(target);
        std::error_code ec;
        fs::create_directories(target_path.parent_path(), ec);
        if (ec) {
            return false;
        }

        const auto status = fs::symlink_status(target_path, ec);
        if (!ec && (fs::is_symlink(status) || fs::is_directory(status))) {
            return false;
        }
        ec.clear();

        const fs::path temporary = target_path.string() + ".tmp-" + cosmo::util::GenerateUUID();
        if (!cosmo::util::WriteFile(temporary.string(), content)) {
            return false;
        }
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace,
                        ec);
        if (ec) {
            fs::remove(temporary, ec);
            return false;
        }
        fs::rename(temporary, target_path, ec);
        if (ec) {
            std::error_code cleanup_error;
            fs::remove(temporary, cleanup_error);
            return false;
        }
        return true;
    }
}  // namespace

LinkageServiceImpl::LinkageServiceImpl()
    : alarm_outputs_(
          std::make_shared<cosmo::linkage::AlarmOutputController>(cosmo::linkage::LoadAlarmOutputChannels(
              (std::filesystem::path(cosmo::path::GetCfgPath("linkAge")) / "alarmOutputs.json").string()))),
      async_queue_("Linkage Alarm QUE", 100) {
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
    LinkageServiceImpl::Stop();
    LOG_INFO("{}", "LinkageService Quit");
}

void LinkageServiceImpl::Stop() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopped_) {
        return;
    }
    async_queue_.Stop();
    async_queue_.stop();
    alarm_outputs_->Stop();
    stopped_ = true;
}

void LinkageServiceImpl::LoadConfig() {
    auto cfg_path =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_file_name_).string();
    cosmo::LinkageStrategyConfig loaded;
    if (!cosmo::util::LoadStructFromJsonFile(cfg_path, loaded)) {
        LOG_WARN("Failed to load linkage config from {}", cfg_path);
        return;
    }

    std::set<std::string> loaded_ids;
    cosmo::LinkageStrategyConfig validated;
    validated.strategies.reserve(loaded.strategies.size());
    for (auto& strategy : loaded.strategies) {
        if (!cosmo::path::IsSafePathComponent(strategy.strategy_id) ||
            !loaded_ids.insert(strategy.strategy_id).second) {
            LOG_WARN("{}", "Reject invalid or duplicate linkage strategy id");
            continue;
        }
        auto str_info = NormalizeWorkflowJson(ReadStrategy(strategy.strategy_id));
        if (!cosmo::util::DecodeJson(str_info, strategy.strategy.workflow)) {
            LOG_WARN("Read strategy {}/{} failed", strategy.strategy_id, strategy.name);
            continue;
        }
        strategy.task =
            strategy.strategy.workflow.empty() ? nullptr : MakeTask(strategy.name, strategy.strategy);
        if (!strategy.strategy.workflow.empty() && !strategy.task) {
            LOG_WARN("Reject invalid linkage strategy {}/{}", strategy.strategy_id, strategy.name);
            continue;
        }
        validated.strategies.push_back(std::move(strategy));
    }
    config_ = std::move(validated);
    if (config_.strategies.size() != loaded.strategies.size() && !SaveConfig(config_)) {
        LOG_WARN("{}", "Failed to persist repaired linkage config");
    }
}

bool LinkageServiceImpl::SaveConfig(const cosmo::LinkageStrategyConfig& config) {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_file_name_).string();
    std::string content;
    if (!cosmo::util::EncodeJson(config, content) || !WriteFileAtomically(path, content)) {
        LOG_WARN("Failed to save linkage config to {}", path);
        return false;
    }
    return true;
}

bool LinkageServiceImpl::SaveStrategy(const std::string& id, const std::string& linkage_strategy) {
    if (!cosmo::path::IsSafePathComponent(id) || linkage_strategy.empty() ||
        linkage_strategy.size() > kMaxStrategyFileBytes) {
        return false;
    }
    std::string file_name = id + ".json";

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / file_name).string();
    return WriteFileAtomically(path, linkage_strategy);
}

bool LinkageServiceImpl::RmvStrategy(const std::string& id) {
    if (!cosmo::path::IsSafePathComponent(id)) {
        return false;
    }
    std::string file_name = id + ".json";

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / file_name).string();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
}

std::string LinkageServiceImpl::ReadStrategy(const std::string& id) {
    if (!cosmo::path::IsSafePathComponent(id)) {
        return {};
    }
    std::string file_name = id + ".json";
    auto main_cfg = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / file_name).string();
    return cosmo::util::ReadFile(main_cfg, kMaxStrategyFileBytes);
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
