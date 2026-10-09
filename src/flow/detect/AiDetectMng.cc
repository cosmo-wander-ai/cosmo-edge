// AiDetectMng.cc — fps-aware instance placement for AiDetector.
//
// Hides (not overrides) MultiChannelActionMng::GetInst. Placement rules under m_mtx:
//   1) an existing channel always reuses its instance (single inference per channel);
//   2) otherwise the first existing instance that fits under the dynamic channel cap and fps budget;
//   3) else create a new instance.

#include "flow/detect/AiDetectMng.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/StringUtil.h"

namespace cosmo {

namespace {
    constexpr const char* kTag = "AiDetectMng";

    std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    bool EndsWith(const std::string& value, const std::string& suffix) {
        return value.size() >= suffix.size() &&
               value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool IsFpsPlacementKey(const std::string& raw_key, const std::string& alg_code,
                           const ActionNode& action) {
        const std::string lower_key = ToLower(std::string(util::Trim(raw_key)));
        if (lower_key.empty()) {
            return false;
        }
        if (lower_key == std::string(key::FPS) || EndsWith(lower_key, ".fps") ||
            lower_key.find(".fps.") != std::string::npos) {
            return true;
        }

        // Some pressure-test orchestrations report fps as "<model-code>=<fps>" instead of populating
        // ActionNode::initFps. Accept exact model-code keys as a placement-only fallback.
        const std::string alg_key       = ToLower(std::string(util::Trim(alg_code)));
        const std::string atomic_key    = ToLower(std::string(util::Trim(action.atomicCode)));
        const std::string atom_name_key = ToLower(std::string(util::Trim(action.atomAlgName)));
        return (!alg_key.empty() && lower_key == alg_key) ||
               (!atomic_key.empty() && lower_key == atomic_key) ||
               (!atom_name_key.empty() && lower_key == atom_name_key);
    }

    float ResolvePlacementFps(const std::string& alg_code, const ActionNode& action) {
        if (ai_detector_fps::HasConfiguredFps(action.initFps)) {
            return action.initFps;
        }

        for (const auto& param : action.configObject.params) {
            if (!IsFpsPlacementKey(param.key.ToString(), alg_code, action)) {
                continue;
            }

            const float fps = util::ParseFloat(param.value.ToString(), -1.0f);
            if (ai_detector_fps::HasConfiguredFps(fps)) {
                return fps;
            }
        }

        return action.initFps;
    }
}  // namespace

AiDetectorPtr AiDetectMng::GetInst(const std::string& algCode, const std::string& channelId,
                                   const std::string& task, ActionNode& action) {
    const float requested_fps  = ResolvePlacementFps(algCode, action);
    const float normalized_fps = ai_detector_fps::NormalizeRequestedFps(requested_fps);

    std::lock_guard<std::shared_mutex> lock(m_mtx);
    auto& algInsts = m_insts[algCode];

    auto applyParams = [&action](const AiDetectorPtr& inst, const std::string& ch, const std::string& t) {
        if (!action.configObject.params.empty()) {
            inst->ModifyParam(ch, t, action.configObject.params);
        }
    };

    // Rule 1: existing channel → reuse. Splitting one channel across instances would duplicate
    // inference and break history/overview/confidence activation, so fps budget never rejects it.
    for (size_t i = 0; i < algInsts.size(); ++i) {
        const auto& inst = algInsts[i];
        if (inst->ChannelExist(channelId)) {
            if (!inst->AddTask(channelId, task, requested_fps)) {
                LOG_WARN("[{}] Channel:{} Task:{} Get {} Inst Failed", kTag, channelId, task, algCode);
                return nullptr;
            }
            applyParams(inst, channelId, task);
            LOG_INFO(
                "[{}] Place {} ch:{} task:{} actionInitFps:{} reqFps:{} normFps:{} -> reuse-existing "
                "idx:{} chs:{} assignedFps:{} budget:{} runtimeInFps:{}",
                kTag, algCode, channelId, task, action.initFps, requested_fps, normalized_fps, i,
                inst->ChannelCount(), inst->AssignedFps(), inst->GetInstanceFpsBudget(), inst->GetInFps());
            return inst;
        }
    }

    // Rule 2: first existing instance that fits under both the dynamic channel cap and the fps budget.
    for (size_t i = 0; i < algInsts.size(); ++i) {
        const auto& inst            = algInsts[i];
        const float assigned_before = inst->AssignedFps();
        const float delta           = inst->DeltaFpsForTask(channelId, normalized_fps);
        if (inst->CanAccept(channelId, requested_fps)) {
            if (!inst->AddTask(channelId, task, requested_fps)) {
                LOG_WARN("[{}] Channel:{} Task:{} Get {} Inst Failed", kTag, channelId, task, algCode);
                return nullptr;
            }
            applyParams(inst, channelId, task);
            LOG_INFO(
                "[{}] Place {} ch:{} task:{} actionInitFps:{} reqFps:{} normFps:{} -> accept-existing "
                "idx:{} chs:{} assignedFpsBefore:{} delta:{} budget:{} runtimeInFps:{}",
                kTag, algCode, channelId, task, action.initFps, requested_fps, normalized_fps, i,
                inst->ChannelCount(), assigned_before, delta, inst->GetInstanceFpsBudget(), inst->GetInFps());
            return inst;
        }
    }

    // Rule 3: no existing instance fits → create a new one.
    auto inst = std::make_shared<AiDetector>(action);
    if (!inst->AddTask(channelId, task, requested_fps)) {
        LOG_WARN("[{}] Channel:{} Task:{} Get {} Inst Failed", kTag, channelId, task, algCode);
        return nullptr;
    }
    applyParams(inst, channelId, task);
    algInsts.push_back(inst);
    LOG_INFO(
        "[{}] Place {} ch:{} task:{} actionInitFps:{} reqFps:{} normFps:{} -> create-new idx:{} chs:{} "
        "assignedFps:{} budget:{} runtimeInFps:{}",
        kTag, algCode, channelId, task, action.initFps, requested_fps, normalized_fps, algInsts.size() - 1,
        inst->ChannelCount(), inst->AssignedFps(), inst->GetInstanceFpsBudget(), inst->GetInFps());
    return inst;
}

}  // namespace cosmo
