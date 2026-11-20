// Target filtering and tagging
// Tags each target with:
// Size filtering
// Current region/masked region list
// Alarm count filtering
// Stationary threshold filtering
// Alarm interval filtering
// ....
// Usage:
// 1. Register via RegistProcQueue
// 2. Modify AlgDataPtr directly in DoTask

#include "flow/target/TargetFilter.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag       = "TARGETFILTER ";
static constexpr int kFilterLogInterval = 100;
namespace cosmo {

namespace {
    constexpr std::string_view kAttribute{"attr"};

    bool IsBehaviorFilter(const BAFilterParam& filter) {
        return filter.type == BAFilterType::kBehaviorConfidenceMin;
    }

    bool MatchesPersonBehavior(const std::vector<BAFilterParam>& filters, AiDetectRstEl& target) {
        if (!target.hasJointPersonAttributes || target.trackStatus == AITrackingStatus::LOSS)
            return false;

        // A person may satisfy more than one selected behavior. Each match comes from the
        // same detector measurement and keeps its own score for downstream alarm evidence.
        bool matched = false;
        for (const auto& score : target.classifyRst) {
            if (score.source != AiAnnotationSource::Classification ||
                score.atomic_code != target.confidence.atomic_code || !std::isfinite(score.confidence) ||
                score.confidence < 0.0f || score.confidence > 1.0f)
                continue;
            const bool passes = std::any_of(filters.begin(), filters.end(), [&](const auto& filter) {
                return IsBehaviorFilter(filter) && score.label == filter.label && filter.f_value >= 0.0f &&
                       filter.f_value <= 1.0f && score.confidence >= filter.f_value;
            });
            if (!passes)
                continue;
            const auto already_recorded = [&](const auto& annotation) {
                return annotation.label == score.label && annotation.atomic_code == score.atomic_code &&
                       annotation.source == score.source && annotation.score == score.confidence;
            };
            if (std::none_of(target.alarmAnnotations.begin(), target.alarmAnnotations.end(),
                             already_recorded))
                target.alarmAnnotations.push_back(
                    {score.label, score.atomic_code, score.confidence, score.source});
            matched = true;
        }
        return matched;
    }
}  // namespace

TargetFilter::~TargetFilter() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

TargetFilter::TargetFilter(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBAFilter, action, "", taskId) {
    // For video tasks, node params declare the selected behaviors. The task then broadcasts
    // its parameter overrides to all actions, including filters on unrelated branches.
    for (const auto& param : action.configObject.params) {
        BAFilterParam filter;
        if (param.keys.size() == 5 && AnalysisKey(param, filter) && IsBehaviorFilter(filter))
            filter_params_.push_back(std::move(filter));
    }
    action_status = util::ErrorEnum::ActionReady;
    LOG_INFO("{}Task:{} Init", kTag, task_id);
}

/*
    filter.pedestrian.confidence.min
*/
bool TargetFilter::AnalysisKey(const MsgDynamicKeyValue& param, BAFilterParam& filter_el) const {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }
    if (param.keys.size() == 5 && param.keys[0] == key::FILTER && param.keys[1] == kAttribute &&
        param.keys[3] == key::CONFIDENCE && param.keys[4] == key::MIN) {
        filter_el.type    = BAFilterType::kBehaviorConfidenceMin;
        filter_el.label   = param.keys[2];
        filter_el.f_value = util::ParseFloat(param.value.ToString(), std::numeric_limits<float>::quiet_NaN());
        return true;
    }
    if (param.keys.size() != 4) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            task_id, param.key, param.keys.size());
        return false;
    }

    if (key::FILTER != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "DEBUG [{}] param.keys[0] is Not {}",
            task_id, key::FILTER);
        return false;
    }

    filter_el.label = param.keys[1];
    if (key::SIZE == param.keys[2]) {
        if (key::MIN == param.keys[3]) {
            filter_el.type    = BAFilterType::kSizeMin;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else {
            filter_el.type    = BAFilterType::kSizeMax;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        }
    } else if (key::SIDE == param.keys[2]) {
        if (key::MIN == param.keys[3]) {
            filter_el.type    = BAFilterType::kSideMin;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else {
            filter_el.type    = BAFilterType::kSideMax;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        }
    } else if (key::CONFIDENCE == param.keys[2]) {
        if (key::MIN == param.keys[3]) {
            filter_el.type    = BAFilterType::kConfidenceMin;
            filter_el.f_value = util::ParseFloat(param.value.ToString());
        } else {
            filter_el.type    = BAFilterType::kConfidenceMax;
            filter_el.f_value = util::ParseFloat(param.value.ToString());
        }
    } else if (key::MOTION == param.keys[2]) {
        if (key::MOVE == param.keys[3]) {
            filter_el.type    = BAFilterType::kMotionMove;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else {
            filter_el.type    = BAFilterType::kMotionStatic;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        }
    } else {
        LOG_WARN(
            "ModifyParam "
            "[{}] Set {} Failed. Unknow Key",
            task_id, param.key);
        return false;
    }
    LOG_INFO(
        "ModifyParam "
        "[{}] Set {} alg_code:{} label:{} i_value:{} f_value:{}",
        task_id, FilterDesc(filter_el.type), filter_el.alg_code, filter_el.label, filter_el.i_value,
        filter_el.f_value);

    return ((static_cast<int>(filter_el.type) > static_cast<int>(BAFilterType::kNone)) &&
            (static_cast<int>(filter_el.type) < static_cast<int>(BAFilterType::kCount)));
}

static void GetFilterParamKeyParts(BAFilterType type, std::string_view& part2, std::string_view& part3) {
    using BAF = BAFilterType;
    switch (type) {
        case BAF::kSizeMin:
            part2 = key::SIZE;
            part3 = key::MIN;
            break;
        case BAF::kSizeMax:
            part2 = key::SIZE;
            part3 = key::MAX;
            break;
        case BAF::kSideMin:
            part2 = key::SIDE;
            part3 = key::MIN;
            break;
        case BAF::kSideMax:
            part2 = key::SIDE;
            part3 = key::MAX;
            break;
        case BAF::kConfidenceMin:
            part2 = key::CONFIDENCE;
            part3 = key::MIN;
            break;
        case BAF::kConfidenceMax:
            part2 = key::CONFIDENCE;
            part3 = key::MAX;
            break;
        case BAF::kMotionMove:
            part2 = key::MOTION;
            part3 = key::MOVE;
            break;
        case BAF::kMotionStatic:
            part2 = key::MOTION;
            part3 = key::STATIC;
            break;
        default:
            part2 = "";
            part3 = "";
            break;
    }
}

MsgDynamicKeyValue TargetFilter::FilterParamToKeyValue(const BAFilterParam& p) {
    MsgDynamicKeyValue kv;
    std::string_view part2;
    std::string_view part3;
    GetFilterParamKeyParts(p.type, part2, part3);
    if (part2.empty() || part3.empty()) {
        return kv;
    }
    kv.keys = {std::string(key::FILTER), p.label, std::string(part2), std::string(part3)};
    kv.key  = std::string(key::FILTER) + std::string(".") + p.label + "." + std::string(part2) + "." +
             std::string(part3);
    if (p.type == BAFilterType::kConfidenceMin || p.type == BAFilterType::kConfidenceMax) {
        kv.value = std::to_string(p.f_value);
    } else {
        kv.value = std::to_string(p.i_value);
    }
    return kv;
}

void TargetFilter::SyncFilterParamsToWorkFlow() {
    if (!action_alg) {
        return;
    }
    const std::string& flowId = GetFlowActionId();
    for (auto& node : action_alg->workFlow) {
        if (node.flowActionId != flowId) {
            continue;
        }
        std::vector<MsgDynamicKeyValue> newParams;
        std::copy_if(node.configObject.params.begin(), node.configObject.params.end(),
                     std::back_inserter(newParams), [](const auto& param) {
                         return param.keys.empty() || param.keys[0] != key::FILTER ||
                                param.key.ToRefString().rfind("filter.attr.", 0) == 0;
                     });
        for (const auto& fp : filter_params_) {
            auto kv = FilterParamToKeyValue(fp);
            if (!kv.keys.empty()) {
                newParams.push_back(kv);
            }
        }
        node.configObject.params = std::move(newParams);
        break;
    }
}

// Modify parameters based on existing ones
bool TargetFilter::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        BAFilterParam filter_el;
        if (AnalysisKey(param, filter_el)) {
            bool found = false;
            for (auto& filter : filter_params_) {
                // Modify if found
                if ((filter.type == filter_el.type) && (filter.alg_code == filter_el.alg_code) &&
                    (filter.label == filter_el.label)) {
                    filter.i_value = filter_el.i_value;
                    filter.f_value = filter_el.f_value;
                    found          = true;
                }
            }
            // Broadcast overrides can update a selected behavior but cannot select new ones.
            if (!found && !IsBehaviorFilter(filter_el)) {
                filter_params_.push_back(filter_el);
            }
        }
    }
    SyncFilterParamsToWorkFlow();
    return false;
}

// Set parameters - clear previous ones and set fully new ones
bool TargetFilter::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear parameters first
    filter_params_.clear();
    for (auto& param : params) {
        BAFilterParam filter_el;
        if (AnalysisKey(param, filter_el)) {
            // Add parameters
            filter_params_.push_back(filter_el);
        }
    }
    SyncFilterParamsToWorkFlow();
    return false;
}

bool TargetFilter::FilterTarget(const BAFilterParam& filter_param, const AiDetectRstEl& target) const {
    switch (filter_param.type) {
        case BAFilterType::kSizeMin: {
            return MinValueFilter(target.box.width * target.box.height, filter_param.i_value);
        } break;
        case BAFilterType::kSizeMax: {
            return MaxValueFilter(target.box.width * target.box.height, filter_param.i_value);
        } break;
        case BAFilterType::kSideMin: {
            return MinValueFilter(target.box.width * target.box.height,
                                  filter_param.i_value * filter_param.i_value);
        } break;
        case BAFilterType::kSideMax: {
            return MaxValueFilter(target.box.width * target.box.height,
                                  filter_param.i_value * filter_param.i_value);
        } break;
        case BAFilterType::kConfidenceMin: {
            return MinValueFilter(target.confidence.confidence, filter_param.f_value);
        } break;
        case BAFilterType::kConfidenceMax: {
            return MaxValueFilter(target.confidence.confidence, filter_param.f_value);
        } break;
        case BAFilterType::kMotionMove: {
            return target.motionStatus == AIMotionState::MOVING;
        } break;
        case BAFilterType::kMotionStatic: {
            return target.motionStatus == AIMotionState::STILL;
        } break;
        default:
            break;
    }
    return false;
}

std::string TargetFilter::FilterDesc(BAFilterType type) const {
    switch (type) {
        case BAFilterType::kSizeMin: {
            return "Filter Min Size";
        } break;
        case BAFilterType::kSizeMax: {
            return "Filter Max Size";
        } break;
        case BAFilterType::kSideMin: {
            return "Filter Min Side";
        } break;
        case BAFilterType::kSideMax: {
            return "Filter Max Side";
        } break;
        case BAFilterType::kConfidenceMin: {
            return "Filter Min Confidence";
        } break;
        case BAFilterType::kConfidenceMax: {
            return "Filter Max Confidence";
        } break;
        case BAFilterType::kMotionMove: {
            return "Filter Motion Move";
        } break;
        case BAFilterType::kMotionStatic: {
            return "Filter Motion Still";
        } break;
        default:
            break;
    }
    return "";
}

void TargetFilter::DoFilter(DataDetTrackClassifyPtr input) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    // Pass all targets without tagging if no filter rules are configured
    if (filter_params_.empty()) {
        return;
    }
    const bool behavior_filtering =
        std::any_of(filter_params_.begin(), filter_params_.end(), IsBehaviorFilter);
    for (auto& target : input->targets) {
        const bool filter_person_behavior = behavior_filtering && target.confidence.label == "person";
        bool find_label                   = filter_person_behavior;
        for (const auto& filter : filter_params_) {
            if (!IsBehaviorFilter(filter) && (filter.alg_code == target.algCode) &&
                (filter.label == target.confidence.label)) {
                find_label = true;
                if (FilterTarget(filter, target)) {
                    target.bFilter    = true;
                    target.filterType = AIFilterType::TargetFilter;
                    target.filterDesc = FilterDesc(filter.type);
                    break;
                }
            }
        }
        // Filter unselected labels
        if (!find_label) {
            target.bFilter    = true;
            target.filterDesc = "Filter Action Have No Label";
        }
        if (filter_person_behavior && !target.bFilter && !MatchesPersonBehavior(filter_params_, target)) {
            target.bFilter    = true;
            target.filterType = AIFilterType::TargetFilter;
            target.filterDesc = "Filter Person Behavior Confidence";
        }
    }
}

void TargetFilter::HandFrame(AlgDataPtr algData) {
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!((AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{}] Filter {} Frames algData->dataType:{}", kTag, task_id, invalid_frame_cnt,
                     algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    DataDetTrackClassifyPtr input;
    if (AlgDataType::ChannelDataDetect == algData->dataType) {
        input = algData->chanDataDetect.detRet;
    } else if (AlgDataType::TaskDataTrack == algData->dataType) {
        input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
    } else {
        input = algData->GetTaskResult(AlgDataType::TaskDataClassify);
    }

    DoFilter(input);

    // Apply filter results and distribute data.
    action_status = util::ErrorEnum::Success;
    distributor->DistributorData(algData);
}

}  // namespace cosmo
