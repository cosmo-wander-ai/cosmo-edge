// LinkageCrud.cc — CRUD operations for LinkageServiceImpl.
// Split from LinkageServiceImpl.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "linkage/LinkAgeAlarm.h"
#include "linkage/LinkAgeAlarmOutput.h"
#include "linkage/LinkAgeAudioDevice.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/LinkageServiceImpl.h"
#include "service/infra/impl/LinkageUtil.h"
#include "util/FormatString.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

// Display alarm tone on frontend large screen.

namespace cosmo::service {
using detail::NormalizeWorkflowJson;

namespace {
    template <typename Predicate>
    std::string FindParameter(const cosmo::linkage::LinkAgeParamNode& node, const Predicate& predicate) {
        for (const auto& parameter : node.config_object.params) {
            const auto& key = parameter.key.ToRefString();
            if (predicate(key)) {
                return parameter.value.ToRefString();
            }
        }
        return {};
    }

    std::string FindParameter(const cosmo::linkage::LinkAgeParamNode& node, std::string_view key) {
        return FindParameter(node, [key](std::string_view candidate) { return candidate == key; });
    }

    bool HasValidAlarmBinding(const cosmo::linkage::LinkAgeParamNode& node) {
        const auto value = FindParameter(node, cosmo::linkage::IsAlarmAlgorithmsKey);
        std::vector<cosmo::linkage::LinkAgeAlarmTaskUnit> tasks;
        return !value.empty() && cosmo::util::DecodeJson(value, tasks) && !tasks.empty() &&
               std::all_of(tasks.begin(), tasks.end(), [](const auto& task) {
                   return !task.channel_id.empty() && !task.algorithm_id.empty();
               });
    }

    bool HasValidAudioBinding(const cosmo::linkage::LinkAgeParamNode& node) {
        const auto device_id = FindParameter(node, cosmo::linkage::IsAudioDeviceIdKey);
        const auto operation =
            cosmo::util::ParseInt(FindParameter(node, cosmo::linkage::kKeyStrageAudioDeviceOperation));
        if (device_id.empty() || !cosmo::linkage::IsValidOperation(operation)) {
            return false;
        }
        if (operation == static_cast<int>(cosmo::linkage::LinkAgeAudioDeviceOperation::kAudioPlay)) {
            return !FindParameter(node, cosmo::linkage::kKeyStrageAudioDeviceData).empty();
        }
        return !FindParameter(node, cosmo::linkage::IsAudioTextKey).empty();
    }
}  // namespace

cosmo::util::ErrorEnum LinkageServiceImpl::Add(const std::string& name, const std::string& linkage_strategy,
                                               std::string& id) {
    // 1. Parse the strategy from frontend; report error if unparseable
    std::string normalized_workflow = NormalizeWorkflowJson(linkage_strategy);
    cosmo::linkage::LinkageStrategyWorkflow storage;
    if (!cosmo::util::DecodeJson(normalized_workflow, storage.workflow)) {
        return cosmo::util::ErrorEnum::Failed;
    }
    auto task = storage.workflow.empty() ? nullptr : MakeTask(name, storage);
    if (!storage.workflow.empty() && !task) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    const std::string new_id = cosmo::util::GenerateUUID();
    std::lock_guard<std::shared_mutex> lock(mtx_);

    cosmo::LinkageStrategyConfigUnit unit;
    unit.strategy_id      = new_id;
    unit.name             = name;
    unit.timestamp        = cosmo::util::GetMilliseconds();
    unit.create_timestamp = unit.timestamp;
    unit.strategy         = storage;
    unit.task             = std::move(task);

    auto next_config = config_;
    next_config.strategies.push_back(std::move(unit));
    if (!SaveStrategy(new_id, normalized_workflow)) {
        return cosmo::util::ErrorEnum::Failed;
    }
    if (!SaveConfig(next_config)) {
        RmvStrategy(new_id);
        return cosmo::util::ErrorEnum::Failed;
    }

    config_ = std::move(next_config);
    id      = new_id;
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum LinkageServiceImpl::Delete(std::string& id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it =
        std::find_if(config_.strategies.begin(), config_.strategies.end(),
                     [&](const cosmo::LinkageStrategyConfigUnit& cfg) { return cfg.strategy_id == id; });
    if (it != config_.strategies.end()) {
        auto next_config = config_;
        next_config.strategies.erase(next_config.strategies.begin() +
                                     std::distance(config_.strategies.begin(), it));
        if (!SaveConfig(next_config)) {
            return cosmo::util::ErrorEnum::Failed;
        }
        config_ = std::move(next_config);
        if (!RmvStrategy(id)) {
            // The durable index no longer references the strategy.  A stale
            // sidecar is harmless and will not be loaded after restart.
            LOG_WARN("Failed to remove unreferenced linkage strategy file {}", id);
        }
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
    auto task = MakeTask(name, storage);
    if (!task) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(
        config_.strategies.begin(), config_.strategies.end(),
        [&](const cosmo::LinkageStrategyConfigUnit& cfg) { return cfg.strategy_id == strategy_id; });

    if (it != config_.strategies.end()) {
        const std::string previous_workflow = ReadStrategy(strategy_id);
        auto next_config                    = config_;
        auto next_it       = next_config.strategies.begin() + std::distance(config_.strategies.begin(), it);
        next_it->name      = name;
        next_it->strategy  = storage;
        next_it->timestamp = cosmo::util::GetMilliseconds();
        next_it->task      = std::move(task);

        if (!SaveStrategy(strategy_id, normalized_workflow)) {
            return cosmo::util::ErrorEnum::Failed;
        }
        if (!SaveConfig(next_config)) {
            if (previous_workflow.empty()) {
                RmvStrategy(strategy_id);
            } else if (!SaveStrategy(strategy_id, previous_workflow)) {
                LOG_ERRO("Failed to restore linkage strategy {} after config write failure", strategy_id);
            }
            return cosmo::util::ErrorEnum::Failed;
        }
        config_ = std::move(next_config);

        LOG_INFO("{}/{} Update", strategy_id, name);
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
            const std::string strategy_name = it->name;
            auto next_config                = config_;
            auto next_it     = next_config.strategies.begin() + std::distance(config_.strategies.begin(), it);
            next_it->is_open = is_open;
            if (!SaveConfig(next_config)) {
                return cosmo::util::ErrorEnum::Failed;
            }
            config_ = std::move(next_config);
            LOG_INFO("{}/{} -> {}", id, strategy_name, is_open ? "Open" : "Close");
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
        std::string workflow;
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
                std::string workflow;
                if (!cosmo::util::EncodeJson(info.strategy.workflow, workflow)) {
                    continue;
                }
                matches.push_back({info.strategy_id, info.name, std::move(workflow), info.is_open});
            }
        }
    }

    // Materialize response DTOs after releasing the service lock.
    std::vector<cosmo::LinkageStrategyOutputUnit> infos;
    infos.reserve(matches.size());
    for (auto& m : matches) {
        cosmo::LinkageStrategyOutputUnit unit;
        unit.id       = m.id;
        unit.name     = m.name;
        unit.status   = m.status;
        unit.workFlow = std::move(m.workflow);
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
    if (!ValidateWorkflow(strategy)) {
        return nullptr;
    }

    return std::make_shared<cosmo::linkage::LinkAgeTask>(name, strategy, alarm_outputs_);
}

bool LinkageServiceImpl::ValidateWorkflow(const cosmo::linkage::LinkageStrategyWorkflow& strategy) const {
    constexpr size_t kMaxWorkflowNodes = 128;
    if (strategy.workflow.empty() || strategy.workflow.size() > kMaxWorkflowNodes) {
        return false;
    }

    std::unordered_map<std::string, const cosmo::linkage::LinkAgeParamNode*> nodes;
    nodes.reserve(strategy.workflow.size());
    size_t root_count = 0;
    for (const auto& node : strategy.workflow) {
        const auto action_kind = cosmo::linkage::ClassifyLinkAgeActionId(node.action_id);
        const bool is_alarm    = action_kind == cosmo::linkage::LinkAgeActionKind::kAlarm;
        const bool is_audio    = action_kind == cosmo::linkage::LinkAgeActionKind::kAudioDevice;
        const bool is_output   = action_kind == cosmo::linkage::LinkAgeActionKind::kAlarmOutput;
        cosmo::linkage::LinkAgeAlarmOutputParam output_param;
        if (action_kind == cosmo::linkage::LinkAgeActionKind::kUnsupported || node.flowActionId.empty() ||
            node.flowActionId == cosmo::key::alg::ACTION_ROOT_VALUE ||
            !nodes.emplace(node.flowActionId, &node).second || (is_alarm && !HasValidAlarmBinding(node)) ||
            (is_audio && !HasValidAudioBinding(node)) ||
            (is_output && !cosmo::linkage::ParseAlarmOutputParam(node, output_param))) {
            return false;
        }
        if (node.preFlowActionId == cosmo::key::alg::ACTION_ROOT_VALUE) {
            if (!is_alarm) {
                return false;
            }
            ++root_count;
        }
    }
    if (root_count != 1) {
        return false;
    }

    for (const auto& node : strategy.workflow) {
        std::unordered_set<std::string> visited;
        const cosmo::linkage::LinkAgeParamNode* current = &node;
        while (current->preFlowActionId != cosmo::key::alg::ACTION_ROOT_VALUE) {
            if (!visited.insert(current->flowActionId).second) {
                return false;
            }
            const auto parent = nodes.find(current->preFlowActionId);
            if (parent == nodes.end()) {
                return false;
            }
            current = parent->second;
        }
    }
    return true;
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
