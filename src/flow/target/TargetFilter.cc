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
#include <iterator>

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag       = "TARGETFILTER ";
static constexpr int kFilterLogInterval = 100;
namespace cosmo {
namespace {
    constexpr std::string_view kCategoryFilterKey = "categoryFilter";
    constexpr std::string_view kSizeFilterKey     = "sizeFilter";
    constexpr std::string_view kEnabledKey        = "enabled";
    constexpr std::string_view kEmptyAtomicCode   = "_";

    std::vector<std::string> ExpandedKeys(const MsgDynamicKeyValue& param) {
        if (!param.keys.empty()) {
            return param.keys;
        }
        const auto parts = util::Split(param.key.ToRefString(), ".");
        return {parts.begin(), parts.end()};
    }

    std::string DecodeAtomicCode(const std::string& value) {
        return value == kEmptyAtomicCode ? std::string{} : value;
    }

    std::string EncodeAtomicCode(const std::string& value) {
        return value.empty() ? std::string{kEmptyAtomicCode} : value;
    }
}  // namespace

TargetFilter::~TargetFilter() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

TargetFilter::TargetFilter(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBAFilter, action, "", taskId),
      mode_(action.actionId == BASizeFilter_Code ? TargetFilterMode::kSize : TargetFilterMode::kCategory) {
    LoadConfiguredParams(action.configObject.params, true);
    action_status = util::ErrorEnum::ActionReady;
    LOG_INFO("{}Task:{} Init mode:{}", kTag, task_id, mode_ == TargetFilterMode::kSize ? "size" : "category");
}

/*
    filter.pedestrian.confidence.min
*/
bool TargetFilter::AnalysisKey(const MsgDynamicKeyValue& param, BAFilterParam& filter_el) const {
    const auto keys = ExpandedKeys(param);
    if (keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }

    size_t type_index = 0;
    if (keys[0] == kSizeFilterKey) {
        if (mode_ != TargetFilterMode::kSize || keys.size() != 6 || keys[1] != GetFlowActionId()) {
            return false;
        }
        filter_el.alg_code = DecodeAtomicCode(keys[2]);
        filter_el.label    = keys[3];
        type_index         = 4;
    } else if (keys[0] == key::FILTER) {
        // Legacy BA_00002 combined category/size component. Keep accepting it so installed
        // tasks continue to run until their workflow is explicitly migrated.
        if (mode_ != TargetFilterMode::kCategory || keys.size() != 4) {
            return false;
        }
        filter_el.label = keys[1];
        type_index      = 2;
    } else {
        return false;
    }

    if (filter_el.label.empty()) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. label is empty",
            task_id, param.key);
        return false;
    }

    if (key::SIZE == keys[type_index]) {
        if (key::MIN == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kSizeMin;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else if (key::MAX == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kSizeMax;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else {
            return false;
        }
    } else if (key::SIDE == keys[type_index]) {
        if (key::MIN == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kSideMin;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else if (key::MAX == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kSideMax;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else {
            return false;
        }
    } else if (key::CONFIDENCE == keys[type_index]) {
        if (key::MIN == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kConfidenceMin;
            filter_el.f_value = util::ParseFloat(param.value.ToString());
        } else if (key::MAX == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kConfidenceMax;
            filter_el.f_value = util::ParseFloat(param.value.ToString());
        } else {
            return false;
        }
    } else if (key::MOTION == keys[type_index]) {
        if (key::MOVE == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kMotionMove;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else if (key::STATIC == keys[type_index + 1]) {
            filter_el.type    = BAFilterType::kMotionStatic;
            filter_el.i_value = util::ParseInt(param.value.ToString());
        } else {
            return false;
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

bool TargetFilter::AnalysisCategoryKey(const MsgDynamicKeyValue& param,
                                       BACategoryFilterParam& category_el) const {
    if (mode_ != TargetFilterMode::kCategory) {
        return false;
    }
    const auto keys = ExpandedKeys(param);
    if (keys.size() != 5 || keys[0] != kCategoryFilterKey || keys[1] != GetFlowActionId() ||
        keys[4] != kEnabledKey || param.value.ToString() != "1") {
        return false;
    }
    category_el.alg_code = DecodeAtomicCode(keys[2]);
    category_el.label    = keys[3];
    return !category_el.label.empty();
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

MsgDynamicKeyValue TargetFilter::SizeFilterParamToKeyValue(const BAFilterParam& p) const {
    MsgDynamicKeyValue kv;
    std::string_view part2;
    std::string_view part3;
    GetFilterParamKeyParts(p.type, part2, part3);
    if (part2.empty() || part3.empty()) {
        return kv;
    }
    kv.keys = {std::string(kSizeFilterKey), GetFlowActionId(), EncodeAtomicCode(p.alg_code), p.label,
               std::string(part2),          std::string(part3)};
    kv.key  = std::string(kSizeFilterKey) + "." + GetFlowActionId() + "." + EncodeAtomicCode(p.alg_code) +
             "." + p.label + "." + std::string(part2) + "." + std::string(part3);
    if (p.type == BAFilterType::kConfidenceMin || p.type == BAFilterType::kConfidenceMax) {
        kv.value = std::to_string(p.f_value);
    } else {
        kv.value = std::to_string(p.i_value);
    }
    return kv;
}

MsgDynamicKeyValue TargetFilter::CategoryParamToKeyValue(const BACategoryFilterParam& p) const {
    MsgDynamicKeyValue kv;
    kv.keys = {std::string(kCategoryFilterKey), GetFlowActionId(), EncodeAtomicCode(p.alg_code), p.label,
               std::string(kEnabledKey)};
    kv.key  = std::string(kCategoryFilterKey) + "." + GetFlowActionId() + "." + EncodeAtomicCode(p.alg_code) +
             "." + p.label + "." + std::string(kEnabledKey);
    kv.value = "1";
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
                     std::back_inserter(newParams), [&](const auto& param) {
                         const auto keys = ExpandedKeys(param);
                         if (keys.empty()) {
                             return true;
                         }
                         if (mode_ == TargetFilterMode::kSize) {
                             return keys[0] != kSizeFilterKey;
                         }
                         return keys[0] != kCategoryFilterKey && keys[0] != key::FILTER;
                     });
        if (mode_ == TargetFilterMode::kSize) {
            for (const auto& fp : filter_params_) {
                auto kv = SizeFilterParamToKeyValue(fp);
                if (!kv.keys.empty()) {
                    newParams.push_back(std::move(kv));
                }
            }
        } else if (!category_params_.empty()) {
            for (const auto& category : category_params_) {
                newParams.push_back(CategoryParamToKeyValue(category));
            }
        } else {
            for (const auto& fp : filter_params_) {
                auto kv = FilterParamToKeyValue(fp);
                if (!kv.keys.empty()) {
                    newParams.push_back(std::move(kv));
                }
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
    LoadConfiguredParams(params, false);
    SyncFilterParamsToWorkFlow();
    return false;
}

// Set parameters - clear previous ones and set fully new ones
bool TargetFilter::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    LoadConfiguredParams(params, true);
    SyncFilterParamsToWorkFlow();
    return false;
}

void TargetFilter::UpsertFilterParam(const BAFilterParam& filter_el) {
    auto it = std::find_if(filter_params_.begin(), filter_params_.end(), [&](const auto& filter) {
        return filter.type == filter_el.type && filter.alg_code == filter_el.alg_code &&
               filter.label == filter_el.label;
    });
    if (it == filter_params_.end()) {
        filter_params_.push_back(filter_el);
    } else {
        *it = filter_el;
    }
}

void TargetFilter::UpsertCategoryParam(const BACategoryFilterParam& category_el) {
    const auto it = std::find_if(category_params_.begin(), category_params_.end(), [&](const auto& category) {
        return category.alg_code == category_el.alg_code && category.label == category_el.label;
    });
    if (it == category_params_.end()) {
        category_params_.push_back(category_el);
    }
}

void TargetFilter::LoadConfiguredParams(const std::vector<MsgDynamicKeyValue>& params, bool replace) {
    if (replace) {
        category_params_.clear();
        filter_params_.clear();
    }
    for (const auto& param : params) {
        BACategoryFilterParam category_el;
        if (AnalysisCategoryKey(param, category_el)) {
            UpsertCategoryParam(category_el);
            continue;
        }
        BAFilterParam filter_el;
        if (AnalysisKey(param, filter_el)) {
            UpsertFilterParam(filter_el);
        }
    }
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

bool TargetFilter::MatchesTarget(const std::string& alg_code, const std::string& label,
                                 const AiDetectRstEl& target) const {
    return label == target.confidence.label &&
           (alg_code.empty() || target.algCode.empty() || alg_code == target.algCode);
}

void TargetFilter::DoCategoryFilter(DataDetTrackClassifyPtr input) const {
    if (category_params_.empty()) {
        return;
    }
    for (auto& target : input->targets) {
        const bool selected = std::any_of(
            category_params_.begin(), category_params_.end(),
            [&](const auto& category) { return MatchesTarget(category.alg_code, category.label, target); });
        if (!selected) {
            target.bFilter    = true;
            target.filterType = AIFilterType::TargetFilter;
            target.filterDesc = "Category Filter: label not selected";
        }
    }
}

void TargetFilter::DoSizeFilter(DataDetTrackClassifyPtr input) const {
    if (filter_params_.empty()) {
        return;
    }
    for (auto& target : input->targets) {
        for (const auto& filter : filter_params_) {
            if (MatchesTarget(filter.alg_code, filter.label, target) && FilterTarget(filter, target)) {
                target.bFilter    = true;
                target.filterType = AIFilterType::TargetFilter;
                target.filterDesc = FilterDesc(filter.type);
                break;
            }
        }
    }
}

void TargetFilter::DoLegacyCombinedFilter(DataDetTrackClassifyPtr input) const {
    if (filter_params_.empty()) {
        return;
    }
    for (auto& target : input->targets) {
        bool find_label = false;
        for (const auto& filter : filter_params_) {
            if (MatchesTarget(filter.alg_code, filter.label, target)) {
                find_label = true;
                if (FilterTarget(filter, target)) {
                    target.bFilter    = true;
                    target.filterType = AIFilterType::TargetFilter;
                    target.filterDesc = FilterDesc(filter.type);
                    break;
                }
            }
        }
        if (!find_label) {
            target.bFilter    = true;
            target.filterDesc = "Filter Action Have No Label";
        }
    }
}

void TargetFilter::DoFilter(DataDetTrackClassifyPtr input) {
    if (!input) {
        return;
    }
    std::shared_lock<std::shared_mutex> lock(mtx);
    if (mode_ == TargetFilterMode::kSize) {
        DoSizeFilter(input);
    } else if (!category_params_.empty()) {
        DoCategoryFilter(input);
    } else {
        DoLegacyCombinedFilter(input);
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
