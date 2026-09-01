// Per-algorithm task unit — config persistence, parameter merge and confidence calculation.

#include "service/camera/impl/CameraTaskUnit.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <unordered_set>

#include "service/algorithm/IAlgorithmQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/task/ITaskLifecycle.h"
#include "util/Exec.h"
#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"

namespace cosmo {
namespace {
    bool SameParamSnapshot(const std::vector<MsgDynamicKeyValue>& lhs,
                           const std::vector<MsgDynamicKeyValue>& rhs) {
        return lhs.size() == rhs.size() &&
               std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const auto& left, const auto& right) {
                   return left.key == right.key && left.value == right.value;
               });
    }

    bool HasDuplicateParamKeys(const std::vector<MsgDynamicKeyValue>& params) {
        std::unordered_set<std::string> keys;
        keys.reserve(params.size());
        return std::any_of(params.begin(), params.end(),
                           [&](const auto& param) { return !keys.emplace(param.key.ToRefString()).second; });
    }

    bool SameStringSnapshot(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {
        return lhs == rhs;
    }

    bool ContainsKey(const std::vector<std::string>& keys, std::string_view key) {
        return std::any_of(keys.begin(), keys.end(),
                           [&](const auto& candidate) { return std::string_view(candidate) == key; });
    }

    bool AddKey(std::vector<std::string>& keys, const std::string& key) {
        if (ContainsKey(keys, key)) {
            return false;
        }
        keys.push_back(key);
        return true;
    }

    bool IsKeyOnlyChannelCompatibilityException(std::string_view key) {
        return key == cosmo::key::CHANNEL_SOURCE_REPEAT;
    }

}  // namespace

CameraTaskUnit::CameraTaskUnit(const std::string& cameraCfgPath, const std::string& cameraId,
                               const std::string& algorithmCode, std::vector<ModelInfo> models)
    : conf_file_path_((std::filesystem::path(cameraCfgPath) / algorithmCode).string()),
      channel_id_(cameraId),
      algorithm_code_(algorithmCode),
      models_(std::move(models)) {
    // set taskId
    task_id_ = channel_id_ + "_" + algorithm_code_;
    LoadConfig();
    LOG_INFO("[{}_{}] CameraTaskUnit Init ModelCount:{}", channel_id_, algorithm_code_, models_.size());
}

CameraTaskUnit::~CameraTaskUnit() {
    // Stop/delete only the task created by this unit.  In particular, TaskCreate
    // can return Created when a duplicate unit is constructed concurrently; that
    // failed unit must not tear down the original owner's task.
    auto& registry = service::ServiceRegistry::Instance();
    if (task_created_) {
        try {
            if (registry.GetLifecycleState() != service::ServiceRegistry::LifecycleState::kShuttingDown &&
                registry.Has<cosmo::service::ITaskLifecycle>()) {
                registry.Get<cosmo::service::ITaskLifecycle>().TaskStop(task_id_);
                registry.Get<cosmo::service::ITaskLifecycle>().TaskDelete(task_id_);
            }
        } catch (const std::exception& error) {
            // Destructors must not terminate shutdown if the registry began
            // tearing down between the state check and the service lookup.
            LOG_WARN("[{}_{}] Skip task cleanup during registry shutdown: {}", channel_id_, algorithm_code_,
                     error.what());
        }
    }
    LOG_INFO("[{}_{}] CameraTaskUnit Delete", channel_id_, algorithm_code_);
}

bool CameraTaskUnit::SaveArea() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_area_file_).string();
    if (!util::SaveStructToJsonFile(path, conf_area_)) {
        LOG_WARN("[{}_{}] Failed to save area config to {}", channel_id_, algorithm_code_, path);
        return false;
    }
    return true;
}

bool CameraTaskUnit::SaveParam() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_param_file_).string();
    if (!util::SaveStructToJsonFile(path, conf_param_)) {
        LOG_WARN("[{}_{}] Failed to save param config to {}", channel_id_, algorithm_code_, path);
        return false;
    }
    return true;
}

void CameraTaskUnit::SaveLibPara() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_lib_file_).string();
    if (!util::SaveStructToJsonFile(path, conf_lib_param_)) {
        LOG_WARN("[{}_{}] Failed to save lib param to {}", channel_id_, algorithm_code_, path);
    }
}

void CameraTaskUnit::LoadConfig() {
    auto areaPath =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_area_file_).string();
    if (!util::LoadStructFromJsonFile(areaPath, conf_area_)) {
        LOG_WARN("[{}_{}] Failed to load area config from {}", channel_id_, algorithm_code_, areaPath);
    }

    auto libPath =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_lib_file_).string();
    if (!util::LoadStructFromJsonFile(libPath, conf_lib_param_)) {
        LOG_WARN("[{}_{}] Failed to load lib param from {}", channel_id_, algorithm_code_, libPath);
    }

    LOG_INFO("[{}_{}] Load..", channel_id_, algorithm_code_);

    auto metadataStr =
        service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetMetaData(algorithm_code_);
    MsgAlgorithmMetaData metadata;
    if (!DecodeAlgorithmMetadata(metadataStr, metadata) || !ValidateAlgorithmMetadataParams(metadata)) {
        LOG_WARN(
            "[{}/{}] Invalid algorithm metadata; refuse to create task with an unverified parameter "
            "ownership policy",
            channel_id_, algorithm_code_);
        task_status_ = util::ErrorEnum::ActionAlgArrangeConfigFail;
        return;
    }
    // A legacy param.json stores a full effective snapshot without provenance. Preserve only values
    // that the old channel editor could actually expose. A frozen legacyChannelEditable hint is
    // authoritative because current senior/channelEditable may already describe a later ownership
    // change. Explicit current ownership without a frozen hint is newer than the markerless snapshot
    // and therefore fails closed instead of retroactively granting override authority.
    auto legacyOwnershipParams = metadata.params;
    for (auto& param : legacyOwnershipParams) {
        if (param.legacyChannelEditable.has_value()) {
            param.channelEditable = *param.legacyChannelEditable;
        } else if (param.channelEditable.has_value()) {
            if (MsgDynamicElement::IsLegacyChannelEditableException(param.type,
                                                                     param.key.ToRefString())) {
                param.channelEditable.reset();
            } else {
                param.channelEditable = false;
            }
        } else {
            param.channelEditable.reset();
        }
    }
    MsgDynamicElement::NormalizeLegacyChannelOwnership(legacyOwnershipParams);
    std::vector<bool> legacyChannelEditable;
    legacyChannelEditable.reserve(legacyOwnershipParams.size());
    for (size_t index = 0; index < legacyOwnershipParams.size(); ++index) {
        legacyChannelEditable.push_back(metadata.params[index].legacyChannelEditable.value_or(
            legacyOwnershipParams[index].IsChannelEditable()));
    }
    MsgDynamicElement::NormalizeLegacyChannelOwnership(metadata.params);
    metadata_params_ = std::move(metadata.params);

    // A missing or unreadable param file means that the metadata baseline is the whole snapshot. An
    // existing empty snapshot and an algorithm with no metadata params are both valid.
    auto paramPath =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_param_file_).string();
    CameraTaskUnitParam persisted;
    const bool persistedLoaded = util::LoadStructFromJsonFile(paramPath, persisted);

    std::vector<MsgDynamicKeyValue> effectiveParams;
    std::vector<std::string> effectiveOverrideKeys;
    effectiveParams.reserve(metadata_params_.size() + persisted.params.size());
    effectiveOverrideKeys.reserve(metadata_params_.size());
    for (size_t index = 0; index < metadata_params_.size(); ++index) {
        const auto& metaParam             = metadata_params_[index];
        MsgDynamicKeyValue effectiveParam = metaParam;
        bool usePersistedOverride         = false;
        if (persistedLoaded && metaParam.IsChannelEditable()) {
            if (persisted.channelOverrideKeysPresent) {
                usePersistedOverride =
                    ContainsKey(persisted.channelOverrideKeys, metaParam.key.ToRefString());
            } else {
                // Legacy files contain only a full effective snapshot. The pre-provenance visibility
                // policy is the strongest available evidence of whether this value came from the channel.
                usePersistedOverride = legacyChannelEditable[index];
            }
        }
        if (usePersistedOverride) {
            auto localIt = std::find_if(
                persisted.params.begin(), persisted.params.end(),
                [&metaParam](const auto& localParam) { return localParam.key == metaParam.key; });
            if (localIt != persisted.params.end()) {
                effectiveParam.value = localIt->value;
                effectiveOverrideKeys.push_back(metaParam.key.ToString());
            }
        }
        effectiveParams.push_back(std::move(effectiveParam));
    }

    // Unknown historical keys cannot keep overriding the scene merely because an old full snapshot
    // happened to contain them.  videoRepeatCount is the sole key-level compatibility exception;
    // retroDirect remains compatible only while a descriptor of that type still exists above.
    if (persistedLoaded) {
        for (const auto& localParam : persisted.params) {
            const bool described =
                std::any_of(metadata_params_.begin(), metadata_params_.end(),
                            [&localParam](const auto& metaParam) { return metaParam.key == localParam.key; });
            const bool marked = !persisted.channelOverrideKeysPresent ||
                                ContainsKey(persisted.channelOverrideKeys, localParam.key.ToRefString());
            const bool alreadyAdded =
                std::any_of(effectiveParams.begin(), effectiveParams.end(),
                            [&localParam](const auto& param) { return param.key == localParam.key; });
            if (!described && !alreadyAdded && marked &&
                IsKeyOnlyChannelCompatibilityException(localParam.key.ToRefString())) {
                effectiveParams.push_back(localParam);
                AddKey(effectiveOverrideKeys, localParam.key.ToString());
            }
        }
    }

    const bool paramsChanged = !persistedLoaded || !SameParamSnapshot(persisted.params, effectiveParams) ||
                               !persisted.channelOverrideKeysPresent ||
                               !SameStringSnapshot(persisted.channelOverrideKeys, effectiveOverrideKeys);
    conf_param_                            = persistedLoaded ? std::move(persisted) : CameraTaskUnitParam{};
    conf_param_.params                     = std::move(effectiveParams);
    conf_param_.channelOverrideKeys        = std::move(effectiveOverrideKeys);
    conf_param_.channelOverrideKeysPresent = true;
    if (paramsChanged) {
        conf_param_.sign += 1;
        modify_sign_ += 1;
        LOG_INFO("[{}/{}] Rebuilt effective params from metadata, total:{} generation:{}", channel_id_,
                 algorithm_code_, conf_param_.params.size(), modify_sign_);
        (void)SaveParam();
    }
    auto algData =
        service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetAlgorithm(algorithm_code_);
    if (!algData) {
        LOG_WARN("[{}_{}] GetAlgorithm Failed", channel_id_, algorithm_code_);
        task_status_ = util::ErrorEnum::ActionAlgLoadFailed;
        return;
    }

    auto ret = service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskCreate(
        channel_id_, channel_id_, task_id_, algData);
    if (util::ErrorEnum::Success != ret) {
        task_status_ = util::ErrorEnum::TaskCreateFailed;
        return;
    }
    task_created_ = true;
    (void)ApplyLatestTaskConfig();
    task_status_ = util::ErrorEnum::Success;
}

bool CameraTaskUnit::ApplyLatestTaskConfig(ParamApplyMode mode) {
    {
        std::unique_lock<std::mutex> lock(apply_state_mtx_);
        if (apply_in_progress_ && apply_owner_ == std::this_thread::get_id()) {
            LOG_WARN("[{}_{}] Reject reentrant task parameter apply", channel_id_, task_id_);
            return false;
        }
        if (mode == ParamApplyMode::kPendingOnly && apply_in_progress_) {
            return false;
        }
        if (mode == ParamApplyMode::kBeforeStart) {
            apply_state_cv_.wait(lock, [this]() { return !apply_in_progress_; });
        }
        apply_in_progress_ = true;
        apply_owner_       = std::this_thread::get_id();
    }

    struct ApplySlotGuard {
        std::mutex& mutex;
        std::condition_variable& cv;
        bool& inProgress;
        std::thread::id& owner;

        ~ApplySlotGuard() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                inProgress = false;
                owner      = std::thread::id{};
            }
            cv.notify_all();
        }
    } applySlotGuard{apply_state_mtx_, apply_state_cv_, apply_in_progress_, apply_owner_};

    MsgTaskConfig param;
    std::vector<ModelInfo> models;
    size_t targetGeneration = 0;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        if (task_status_ != util::ErrorEnum::Success) {
            LOG_WARN("[{}_{}] Skip Set Param because task is not ready, status:{}", channel_id_, task_id_,
                     static_cast<uint32_t>(task_status_));
            return false;
        }
        if (mode == ParamApplyMode::kPendingOnly && modify_sign_ == enable_sign_) {
            return true;
        }
        targetGeneration = modify_sign_;
        param.params.insert(param.params.end(), conf_param_.params.begin(), conf_param_.params.end());
        param.areas         = conf_area_.areas;
        param.shieldedAreas = conf_area_.shieldedAreas;
        models              = models_;
    }
    LOG_INFO("[{}_{}] Set Param: ParamSize:{} AreaSize:{}", channel_id_, task_id_, param.params.size(),
             param.areas.size());
    EnableParamConfidences(param, models);

    bool applied = false;
    try {
        applied = service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().SetTaskParam(
            channel_id_, task_id_, param);
    } catch (const std::exception& error) {
        LOG_WARN("[{}_{}] Set task parameters threw an exception: {}", channel_id_, task_id_, error.what());
    } catch (...) {
        LOG_WARN("[{}_{}] Set task parameters threw an unknown exception", channel_id_, task_id_);
    }

    bool latestGenerationApplied = false;
    if (applied) {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        enable_sign_            = targetGeneration;
        latestGenerationApplied = modify_sign_ == targetGeneration;
    } else {
        LOG_WARN("[{}_{}] Set task parameters failed; keep change pending for retry", channel_id_, task_id_);
    }
    return applied && latestGenerationApplied;
}

util::ErrorEnum CameraTaskUnit::GetStatus() const {
    return task_status_;
}

bool CameraTaskUnit::IsReady() const {
    return task_status_ == util::ErrorEnum::Success;
}

void CameraTaskUnit::RefreshModels(std::vector<ModelInfo> models) {
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        models_ = std::move(models);
        modify_sign_ += 1;
    }
    (void)ApplyLatestTaskConfig();
}

void CameraTaskUnit::EnableParamConfidences(MsgTaskConfig& param, const std::vector<ModelInfo>& models) {
    std::vector<std::string>
        labelsNeedConfidence;  // Labels needing confidence (aiParam.xxx.confidence with empty value)
    std::vector<CameraTaskConfidenceConfig> confidenceConfigs;  // All aiParam.xxx.confidenceConfig entries
    for (auto& actionKeyParam : param.params) {
        auto keys = util::Split(actionKeyParam.key.ToRefString(), ".");
        actionKeyParam.keys.assign(keys.begin(), keys.end());
        if ((3 == actionKeyParam.keys.size()) && (key::AI_PARAM == actionKeyParam.keys.at(0))) {
            if (key::CONFIDENCE == actionKeyParam.keys.at(2)) {
                if (actionKeyParam.value.empty()) {
                    labelsNeedConfidence.push_back(actionKeyParam.keys.at(1));
                }
            } else if (key::CONFIDENCE_CONFIG == actionKeyParam.keys.at(2)) {
                auto confidenConfigValue = util::Split(actionKeyParam.value.ToRefString(), ",");
                if (2 == confidenConfigValue.size()) {
                    CameraTaskConfidenceConfig confidenceConfig;
                    confidenceConfig.type =
                        static_cast<ConfidenceConfigType>(util::ParseInt(confidenConfigValue.at(0).data()));
                    confidenceConfig.value = util::ParseInt(confidenConfigValue.at(1).data());
                    confidenceConfig.label = actionKeyParam.keys.at(1);
                    confidenceConfigs.push_back(confidenceConfig);
                }
            }
        }
    }

    EnableParamConfidences(param, labelsNeedConfidence, confidenceConfigs, models);
}

CameraTaskConfidenceConfig CameraTaskUnit::GetConfidenceConfig(
    const std::string& label, const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs) const {
    CameraTaskConfidenceConfig confidenceConfig;
    bool bFindit = false;
    auto it      = std::find_if(confidenceConfigs.begin(), confidenceConfigs.end(),
                                [&label](const auto& config) { return label == config.label; });
    if (it != confidenceConfigs.end()) {
        confidenceConfig = *it;
        bFindit          = true;
    }
    LOG_INFO("[{}_{}] label:{} Confidence {} Label:{} {}/{}", channel_id_, algorithm_code_, label,
             bFindit ? "Found" : "Not Found", confidenceConfig.label, confidenceConfig.type,
             confidenceConfig.value);
    return confidenceConfig;
}

// Retrieve high/low confidence thresholds from model labels
bool CameraTaskUnit::GetConfidence(const std::string& label, const std::vector<ModelInfo>& models,
                                   float& confidenceHigh, float& confidence) const {
    for (const auto& model : models) {
        auto it = std::find_if(model.labels.begin(), model.labels.end(),
                               [&label](const auto& labelInfo) { return label == labelInfo.code; });
        if (it != model.labels.end()) {
            confidenceHigh = it->confidenceHigh;
            confidence     = it->confidence;
            LOG_INFO("[{}_{}] label:{} Found confidenceHigh:{} confidence:{}", channel_id_, algorithm_code_,
                     label, confidenceHigh, confidence);
            return true;
        }
    }
    LOG_WARN("[{}_{}] label:{} Not Found confidenceHigh And confidence", channel_id_, algorithm_code_, label);
    return false;
}

// Calculate actual confidence from config and model-provided high/low thresholds
float CameraTaskUnit::CalcConfidence(const CameraTaskConfidenceConfig& config, float& confidenceHigh,
                                     float& confidence) const {
    float confidenceUsing = confidenceHigh;
    if (ConfidenceConfigType::kRecommend == config.type) {
        confidenceUsing = confidence;
    }

    if (0 == config.value) {
        LOG_INFO("[{}_{}] label:{} Set Real Confidence {}", channel_id_, algorithm_code_, config.label,
                 confidenceUsing);
        return confidenceUsing;
    }

    float confidenceDiff = 0.0f;
    if (config.value > 0) {
        confidenceDiff = (1.0f - confidenceUsing) / 100.0f;
    } else {
        confidenceDiff = confidenceUsing / 100.0f;
    }
    confidenceUsing = confidenceUsing + (confidenceDiff * static_cast<float>(config.value));
    if ((confidenceUsing > 1.0f) || (confidenceUsing < 0.0f)) {
        LOG_WARN("[{}_{}] label:{} According To The Type/Value:{}/{} Confidence:{}/{} Get Real Confidence {}",
                 channel_id_, algorithm_code_, config.label, config.type, config.value, confidenceHigh,
                 confidence, confidenceUsing);
        confidenceUsing = confidence;
    } else {
        LOG_INFO("[{}_{}] label:{} According To The Type/Value:{}/{} Confidence:{}/{} Get Real Confidence {}",
                 channel_id_, algorithm_code_, config.label, config.type, config.value, confidenceHigh,
                 confidence, confidenceUsing);
    }
    return confidenceUsing;
}

void CameraTaskUnit::EnableParamConfidences(MsgTaskConfig& param,
                                            std::vector<std::string> labelsNeedConfidence,
                                            const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs,
                                            const std::vector<ModelInfo>& models) {
    for (auto needConfidenceLabel : labelsNeedConfidence)  // All labels requiring confidence config
    {
        for (auto& actionKeyParam : param.params) {
            if ((3 == actionKeyParam.keys.size()) && (key::AI_PARAM == actionKeyParam.keys.at(0)) &&
                (needConfidenceLabel == actionKeyParam.keys.at(1))  // Found matching label in config
                && (key::CONFIDENCE == actionKeyParam.keys.at(2))) {
                // Find confidenceConfig for this label from confidenceConfigs
                auto confidenceConfig = GetConfidenceConfig(needConfidenceLabel, confidenceConfigs);
                float confidenceHigh  = 0.10f;
                float confidence      = 0.10f;
                // Retrieve strict/normal confidence from model labels
                (void)GetConfidence(needConfidenceLabel, models, confidenceHigh, confidence);
                // Calculate confidence from confidenceConfig and strict/normal thresholds
                actionKeyParam.value =
                    std::to_string(CalcConfidence(confidenceConfig, confidenceHigh, confidence));
                LOG_INFO("====== {}:{} {}/{}/{}  {}/{}", actionKeyParam.key, actionKeyParam.value,
                         confidenceConfig.type, confidenceConfig.label, confidenceConfig.value,
                         confidenceHigh, confidence);
                break;
            }
        }
    }
}

util::ErrorEnum CameraTaskUnit::SetArea(const std::vector<MsgTaskArea>& areas,
                                        const std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    const auto previousArea       = conf_area_;
    const auto previousModifySign = modify_sign_;

    LOG_INFO("[{}_{}] Set Area Size From {} To {}", channel_id_, algorithm_code_, conf_area_.areas.size(),
             areas.size());
    conf_area_.areas         = areas;
    conf_area_.shieldedAreas = shieldedAreas;
    conf_area_.sign += 1;
    modify_sign_ += 1;
    if (!SaveArea()) {
        conf_area_   = previousArea;
        modify_sign_ = previousModifySign;
        return util::ErrorEnum::FileOpenFailed;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskUnit::GetArea(std::vector<MsgTaskArea>& areas,
                                        std::vector<MsgTaskArea>& shieldedAreas) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    areas         = conf_area_.areas;
    shieldedAreas = conf_area_.shieldedAreas;
    LOG_INFO("[{}_{}] Get Area Size {} ", channel_id_, algorithm_code_, conf_area_.areas.size());
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskUnit::SetParams(const MsgTaskConfig& params) {
    // BindTaskLibPara uses this trusted path for channel-level library selectors.  It has the same
    // ownership and transactional requirements as a channel form update.
    return SetChannelParams(params);
}

util::ErrorEnum CameraTaskUnit::SetChannelParams(const MsgTaskConfig& params) {
    if (HasDuplicateParamKeys(params.params)) {
        LOG_WARN("[{}/{}] Reject channel parameter snapshot with duplicate keys", channel_id_,
                 algorithm_code_);
        return util::ErrorEnum::InvalidParam;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    const auto previousParam      = conf_param_;
    const auto previousArea       = conf_area_;
    const auto previousModifySign = modify_sign_;
    const auto paramChangeCount   = MergeChannelParamsLocked(params.params);

    LOG_INFO("[{}_{}] Set Area Size From {} To {}", channel_id_, algorithm_code_, conf_area_.areas.size(),
             params.areas.size());
    conf_area_.areas         = params.areas;
    conf_area_.shieldedAreas = params.shieldedAreas;
    conf_area_.sign += 1;
    modify_sign_ += 1;
    if (!SaveArea()) {
        conf_param_  = previousParam;
        conf_area_   = previousArea;
        modify_sign_ = previousModifySign;
        if (!SaveArea()) {
            LOG_ERRO("[{}/{}] Failed to restore area config after channel update failure", channel_id_,
                     algorithm_code_);
        }
        return util::ErrorEnum::FileOpenFailed;
    }

    if (paramChangeCount > 0) {
        LOG_INFO("[{}/{}] Have {} Channel Params Changed", channel_id_, algorithm_code_, paramChangeCount);
        conf_param_.sign += 1;
        modify_sign_ += 1;
        if (!SaveParam()) {
            conf_param_              = previousParam;
            conf_area_               = previousArea;
            modify_sign_             = previousModifySign;
            const bool areaRestored  = SaveArea();
            const bool paramRestored = SaveParam();
            if (!areaRestored || !paramRestored) {
                LOG_ERRO("[{}/{}] Failed to fully restore persisted config after channel update failure",
                         channel_id_, algorithm_code_);
            }
            return util::ErrorEnum::FileOpenFailed;
        }
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskUnit::SetLibPara(std::vector<std::string>& libParaId) {
    conf_lib_param_.libParaId = std::move(libParaId);
    SaveLibPara();

    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskUnit::SetParams(std::vector<MsgDynamicKeyValue> params) {
    return SetChannelParams(std::move(params));
}

util::ErrorEnum CameraTaskUnit::SetChannelParams(std::vector<MsgDynamicKeyValue> params) {
    if (HasDuplicateParamKeys(params)) {
        LOG_WARN("[{}/{}] Reject channel parameter patch with duplicate keys", channel_id_, algorithm_code_);
        return util::ErrorEnum::InvalidParam;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    const auto previousParam      = conf_param_;
    const auto previousModifySign = modify_sign_;
    const auto paramChangeCount   = MergeChannelParamsLocked(std::move(params));
    if (paramChangeCount > 0) {
        LOG_INFO("[{}/{}] Have {} Channel Params Changed", channel_id_, algorithm_code_, paramChangeCount);
        conf_param_.sign += 1;
        modify_sign_ += 1;
        if (!SaveParam()) {
            conf_param_  = previousParam;
            modify_sign_ = previousModifySign;
            return util::ErrorEnum::FileOpenFailed;
        }
    }
    return util::ErrorEnum::Success;
}

size_t CameraTaskUnit::MergeChannelParamsLocked(std::vector<MsgDynamicKeyValue> params) {
    size_t paramChangeCount = 0;

    conf_param_.channelOverrideKeysPresent = true;

    // Metadata ownership may become stricter after an upgrade.  Never let a stale marker retain
    // authority over a scene-owned or deleted key (apart from the one key-level compatibility item).
    for (auto it = conf_param_.channelOverrideKeys.begin(); it != conf_param_.channelOverrideKeys.end();) {
        const auto metaIt  = std::find_if(metadata_params_.begin(), metadata_params_.end(),
                                          [&](const auto& metaParam) { return metaParam.key == *it; });
        const bool allowed = metaIt != metadata_params_.end() ? metaIt->IsChannelEditable()
                                                              : IsKeyOnlyChannelCompatibilityException(*it);
        if (!allowed) {
            it = conf_param_.channelOverrideKeys.erase(it);
            paramChangeCount += 1;
        } else {
            ++it;
        }
    }

    // Restore every scene-managed key defensively before considering the channel patch, including
    // keys omitted by the caller.
    for (const auto& metaParam : metadata_params_) {
        if (metaParam.IsChannelEditable()) {
            continue;
        }

        auto localIt =
            std::find_if(conf_param_.params.begin(), conf_param_.params.end(),
                         [&metaParam](const auto& localParam) { return localParam.key == metaParam.key; });
        if (localIt == conf_param_.params.end()) {
            conf_param_.params.push_back(metaParam);
            paramChangeCount += 1;
        } else if (localIt->value != metaParam.value) {
            LOG_INFO("[{}_{}] Restore scene-managed param:{} From {} To {}", channel_id_, algorithm_code_,
                     localIt->key, localIt->value, metaParam.value);
            localIt->value = metaParam.value;
            paramChangeCount += 1;
        }
    }

    for (auto& paramUnit : params) {
        const auto paramKey = paramUnit.key.ToString();
        auto metaIt =
            std::find_if(metadata_params_.begin(), metadata_params_.end(),
                         [&paramUnit](const auto& metaParam) { return metaParam.key == paramUnit.key; });
        if (metaIt != metadata_params_.end() && !metaIt->IsChannelEditable()) {
            LOG_WARN("[{}_{}] Ignore channel update for scene-managed param:{} requested:{} baseline:{}",
                     channel_id_, algorithm_code_, paramUnit.key, paramUnit.value, metaIt->value);
            continue;
        }

        if (metaIt == metadata_params_.end() &&
            !IsKeyOnlyChannelCompatibilityException(paramUnit.key.ToRefString())) {
            LOG_WARN("[{}_{}] Ignore unknown channel param:{}({})", channel_id_, algorithm_code_,
                     paramUnit.key, paramUnit.value);
            continue;
        }

        auto localIt =
            std::find_if(conf_param_.params.begin(), conf_param_.params.end(),
                         [&paramUnit](const auto& localParam) { return localParam.key == paramUnit.key; });
        if (localIt == conf_param_.params.end()) {
            LOG_INFO("[{}_{}] Add channel param:{}({})", channel_id_, algorithm_code_, paramUnit.key,
                     paramUnit.value);
            conf_param_.params.push_back(std::move(paramUnit));
            paramChangeCount += 1;
        } else if (localIt->value != paramUnit.value) {
            LOG_INFO("[{}_{}] Channel param:{} Set From {} To {}", channel_id_, algorithm_code_, localIt->key,
                     localIt->value, paramUnit.value);
            localIt->value = paramUnit.value;
            paramChangeCount += 1;
        }
        if (AddKey(conf_param_.channelOverrideKeys, paramKey)) {
            paramChangeCount += 1;
        }
    }

    std::vector<std::string> canonicalOverrideKeys;
    canonicalOverrideKeys.reserve(conf_param_.channelOverrideKeys.size());
    for (const auto& metaParam : metadata_params_) {
        if (ContainsKey(conf_param_.channelOverrideKeys, metaParam.key.ToRefString())) {
            canonicalOverrideKeys.push_back(metaParam.key.ToString());
        }
    }
    if (ContainsKey(conf_param_.channelOverrideKeys, cosmo::key::CHANNEL_SOURCE_REPEAT) &&
        std::none_of(metadata_params_.begin(), metadata_params_.end(), [](const auto& metaParam) {
            return std::string_view(metaParam.key.ToRefString()) == cosmo::key::CHANNEL_SOURCE_REPEAT;
        })) {
        canonicalOverrideKeys.emplace_back(std::string(cosmo::key::CHANNEL_SOURCE_REPEAT));
    }
    if (!SameStringSnapshot(conf_param_.channelOverrideKeys, canonicalOverrideKeys)) {
        conf_param_.channelOverrideKeys = std::move(canonicalOverrideKeys);
        paramChangeCount += 1;
    }

    return paramChangeCount;
}

std::vector<MsgDynamicKeyValue> CameraTaskUnit::GetParams() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return conf_param_.params;
}
}  // namespace cosmo

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, CameraTaskUnitParam& v) {
    v.params.clear();
    if (j.contains("params") && !j["params"].is_null())
        j.at("params").get_to(v.params);
    v.channelOverrideKeys.clear();
    v.channelOverrideKeysPresent = j.contains("channelOverrideKeys");
    if (v.channelOverrideKeysPresent) {
        j.at("channelOverrideKeys").get_to(v.channelOverrideKeys);
    }
}

void to_json(nlohmann::json& j, const CameraTaskUnitParam& v) {
    j["params"]              = v.params;
    j["channelOverrideKeys"] = v.channelOverrideKeys;
}

void from_json(const nlohmann::json& j, CameraTaskUnitArea& v) {
    if (j.contains("areas") && !j["areas"].is_null())
        j.at("areas").get_to(v.areas);
    if (j.contains("shieldedAreas") && !j["shieldedAreas"].is_null())
        j.at("shieldedAreas").get_to(v.shieldedAreas);
}

void to_json(nlohmann::json& j, const CameraTaskUnitArea& v) {
    j["areas"]         = v.areas;
    j["shieldedAreas"] = v.shieldedAreas;
}

void from_json(const nlohmann::json& j, CameraTaskUnitLibPara& v) {
    if (j.contains("libParaId") && !j["libParaId"].is_null())
        j.at("libParaId").get_to(v.libParaId);
}

void to_json(nlohmann::json& j, const CameraTaskUnitLibPara& v) {
    j["libParaId"] = v.libParaId;
}

}  // namespace cosmo
