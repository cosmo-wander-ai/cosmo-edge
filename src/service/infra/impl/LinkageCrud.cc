// LinkageCrud.cc — CRUD operations for LinkageServiceImpl.
// Split from LinkageServiceImpl.cc to reduce file size (DEBT-007).

#include <filesystem>

#include "linkage/LinkAgeAudioDevice.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/LinkageServiceImpl.h"
#include "service/infra/impl/LinkageUtil.h"
#include "util/FormatString.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

// Display alarm tone on frontend large screen.

namespace cosmo::service {
using detail::NormalizeWorkflowJson;

cosmo::util::ErrorEnum LinkageServiceImpl::Add(const std::string& name, const std::string& linkage_strategy,
                                               std::string& id) {
    // 1. Parse the strategy from frontend; report error if unparseable
    std::string normalized_workflow = NormalizeWorkflowJson(linkage_strategy);
    cosmo::linkage::LinkageStrategyWorkflow storage;
    if (!cosmo::util::DecodeJson(normalized_workflow, storage.workflow)) {
        return cosmo::util::ErrorEnum::Failed;
    }
    id = cosmo::util::GenerateUUID();
    SaveStrategy(id, normalized_workflow);
    std::lock_guard<std::shared_mutex> lock(mtx_);

    // 3. Update strategy table and save to local config
    cosmo::LinkageStrategyConfigUnit unit;
    unit.strategy_id      = id;
    unit.name             = name;
    unit.timestamp        = cosmo::util::GetMilliseconds();
    unit.create_timestamp = unit.timestamp;
    unit.strategy         = storage;
    unit.task             = MakeTask(name, storage);
    config_.strategies.push_back(unit);

    SaveConfig();
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum LinkageServiceImpl::Delete(std::string& id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it =
        std::find_if(config_.strategies.begin(), config_.strategies.end(),
                     [&](const cosmo::LinkageStrategyConfigUnit& cfg) { return cfg.strategy_id == id; });
    if (it != config_.strategies.end()) {
        RmvStrategy(it->strategy_id);
        config_.strategies.erase(it);
        SaveConfig();
        return cosmo::util::ErrorEnum::Success;
    }
    return cosmo::util::ErrorEnum::IDNotExist;
}

cosmo::util::ErrorEnum LinkageServiceImpl::Update(const std::string& name, const std::string& strategy_id,
                                                  const std::string& linkage_strategy) {
    std::string normalized_workflow = NormalizeWorkflowJson(linkage_strategy);
    cosmo::linkage::LinkageStrategyWorkflow storage;
    if (!cosmo::util::DecodeJson(normalized_workflow, storage.workflow)) {
        LOG_ERRO("{}/{} dec json failed", strategy_id, name);
        return cosmo::util::ErrorEnum::Failed;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(
        config_.strategies.begin(), config_.strategies.end(),
        [&](const cosmo::LinkageStrategyConfigUnit& cfg) { return cfg.strategy_id == strategy_id; });

    if (it != config_.strategies.end()) {
        SaveStrategy(strategy_id, normalized_workflow);

        it->name      = name;
        it->strategy  = storage;
        it->timestamp = cosmo::util::GetMilliseconds();
        it->task      = MakeTask(name, storage);

        LOG_INFO("{}/{} Update", strategy_id, name);
        SaveConfig();
        return cosmo::util::ErrorEnum::Success;
    } else {
        LOG_INFO("{} Not Exist", strategy_id);
        return cosmo::util::ErrorEnum::StrategyNotExist;
    }
}

cosmo::util::ErrorEnum LinkageServiceImpl::Switch(std::string& id, bool is_open) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it =
        std::find_if(config_.strategies.begin(), config_.strategies.end(),
                     [&](const cosmo::LinkageStrategyConfigUnit& cfg) { return cfg.strategy_id == id; });
    if (it != config_.strategies.end()) {
        if (it->is_open != is_open) {
            it->is_open = is_open;
            SaveConfig();
            LOG_INFO("{}/{} -> {}", it->strategy_id, it->name, it->is_open ? "Open" : "Close");
        }

        return cosmo::util::ErrorEnum::Success;
    }
    return cosmo::util::ErrorEnum::IDNotExist;
}

std::vector<cosmo::LinkageStrategyOutputUnit> LinkageServiceImpl::Query(int page_num, int page_size,
                                                                        const std::string& name,
                                                                        size_t& total) {
    total = 0;

    // Collect matching entries under lock (no file IO)
    struct MatchInfo {
        std::string id;
        std::string name;
        bool status;
    };
    std::vector<MatchInfo> matches;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        size_t index       = 0;
        size_t index_start = (page_num - 1) * page_size;
        size_t index_end   = page_num * page_size;

        for (const auto& info : config_.strategies) {
            if (!name.empty()) {
                if (std::string::npos == info.name.find(name)) {
                    continue;
                }
            }
            index += 1;
            total++;
            if ((index > index_start) && (index <= index_end)) {
                matches.push_back({info.strategy_id, info.name, info.is_open});
            }
        }
    }

    // Read strategy files outside lock to avoid holding lock during IO
    std::vector<cosmo::LinkageStrategyOutputUnit> infos;
    infos.reserve(matches.size());
    for (const auto& m : matches) {
        cosmo::LinkageStrategyOutputUnit unit;
        unit.id       = m.id;
        unit.name     = m.name;
        unit.status   = m.status;
        unit.workFlow = NormalizeWorkflowJson(ReadStrategy(unit.id));
        infos.push_back(std::move(unit));
    }
    return infos;
}

// Get device-supported strategies
bool LinkageServiceImpl::ReadSupportedStorage(int& total_size, std::vector<cosmo::StorageList>& vec_data) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    total_size = static_cast<int>(support_storages_.size());
    vec_data.assign(support_storages_.begin(), support_storages_.end());
    return true;
}

cosmo::linkage::LinkAgeTaskPtr LinkageServiceImpl::MakeTask(
    const std::string& name, cosmo::linkage::LinkageStrategyWorkflow& strategy) {
    if (strategy.workflow.empty()) {
        return nullptr;
    }

    return std::make_shared<cosmo::linkage::LinkAgeTask>(name, strategy);
}

bool LinkageServiceImpl::Alarm(const std::string& channel_id, const std::string& alg_id) {
    cosmo::LinkageAlarm alarmMsg;
    alarmMsg.channel_id = channel_id;
    alarmMsg.alg_id     = alg_id;
    return async_queue_.Insert(alarmMsg);
}

void LinkageServiceImpl::DoAlarm(const std::string& channel_id, const std::string& alg_id) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto& info : config_.strategies) {
        if (!info.is_open) {
            continue;
        }
        if (!info.task) {
            continue;
        }
        LOG_INFO("{}/{} DoAlarm", info.strategy_id, info.name);
        info.task->DoAlarm(channel_id, alg_id);
    }
}

bool LinkageServiceImpl::IsAudioDeviceInUse(const std::string& audio_device_id) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto& info : config_.strategies) {
        if (!info.task) {
            continue;
        }

        if (info.task->IsAudioDeviceInUse(audio_device_id)) {
            return true;
        }
    }

    return false;
}

bool LinkageServiceImpl::IsAudioFileInUse(const std::string& audio_file_id) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto& info : config_.strategies) {
        if (!info.task) {
            continue;
        }

        if (info.task->IsAudioFileInUse(audio_file_id)) {
            return true;
        }
    }
    return false;
}

}  // namespace cosmo::service
