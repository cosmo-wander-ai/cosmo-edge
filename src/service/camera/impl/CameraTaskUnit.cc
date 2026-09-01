// Per-algorithm task unit — config persistence, parameter merge and confidence calculation.

#include "service/camera/impl/CameraTaskUnit.h"

#include <algorithm>
#include <exception>
#include <filesystem>

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

void CameraTaskUnit::SaveArea() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_area_file_).string();
    if (!util::SaveStructToJsonFile(path, conf_area_)) {
        LOG_WARN("[{}_{}] Failed to save area config to {}", channel_id_, algorithm_code_, path);
    }
}

void CameraTaskUnit::SaveParam() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_param_file_).string();
    if (!util::SaveStructToJsonFile(path, conf_param_)) {
        LOG_WARN("[{}_{}] Failed to save param config to {}", channel_id_, algorithm_code_, path);
    }
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

    // Load failure may occur if parameters have never been set yet
    auto paramPath =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_param_file_).string();
    if ((!util::LoadStructFromJsonFile(paramPath, conf_param_)) || (conf_param_.params.empty())) {
        LOG_INFO("[{}/{}] Config load failed, fetching defaults from algorithm package", channel_id_,
                 algorithm_code_);
        {
            auto metadataStr =
                service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetMetaData(
                    algorithm_code_);
            MsgAlgorithmMetaData metadata;
            if (!util::DecodeJson(metadataStr, metadata)) {
                task_status_ = util::ErrorEnum::ActionAlgArrangeConfigFail;
                return;
            }
            conf_param_.params.insert(conf_param_.params.begin(), metadata.params.begin(),
                                      metadata.params.end());
            SaveParam();
        }
    } else {
        // Config loaded successfully, but algorithm metadata may have added new parameters
        // (e.g. filter.face.side.min). Merge new params into saved config to avoid old tasks
        // missing new parameters.
        auto metadataStr =
            service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetMetaData(algorithm_code_);
        MsgAlgorithmMetaData metadata;
        if (util::DecodeJson(metadataStr, metadata)) {
            int mergedCount = 0;
            for (const auto& metaParam : metadata.params) {
                bool exists = std::any_of(
                    conf_param_.params.begin(), conf_param_.params.end(),
                    [&metaParam](const auto& localParam) { return localParam.key == metaParam.key; });
                if (!exists) {
                    conf_param_.params.push_back(metaParam);
                    mergedCount++;
                }
            }
            if (mergedCount > 0) {
                LOG_INFO("[{}/{}] Merged {} new params from algorithm metadata, total: {}", channel_id_,
                         algorithm_code_, mergedCount, conf_param_.params.size());
                SaveParam();
            }
        }
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
    TaskEnableParam();
    task_status_ = util::ErrorEnum::Success;
}

void CameraTaskUnit::TaskEnableParam() {
    if (task_status_ != util::ErrorEnum::Success) {
        LOG_WARN("[{}_{}] Skip Set Param because task is not ready, status:{}", channel_id_, task_id_,
                 static_cast<uint32_t>(task_status_));
        return;
    }
    if (modify_sign_ == enable_sign_) {
        return;
    }

    MsgTaskConfig param;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        param.params.insert(param.params.end(), conf_param_.params.begin(), conf_param_.params.end());
        param.areas         = conf_area_.areas;
        param.shieldedAreas = conf_area_.shieldedAreas;
    }
    LOG_INFO("[{}_{}] Set Param: ParamSize:{} AreaSize:{}", channel_id_, task_id_, param.params.size(),
             param.areas.size());
    EnableParamConfidences(param);

    if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().SetTaskParam(
            channel_id_, task_id_, param)) {
        enable_sign_ = modify_sign_;
    } else {
        LOG_WARN("[{}_{}] Set task parameters failed; keep change pending for retry", channel_id_, task_id_);
    }
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
    TaskEnableParam();
}

void CameraTaskUnit::EnableParamConfidences(MsgTaskConfig& param) {
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

    EnableParamConfidences(param, labelsNeedConfidence, confidenceConfigs);
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
bool CameraTaskUnit::GetConfidence(const std::string& label, float& confidenceHigh, float& confidence) const {
    for (auto& model : models_) {
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

void CameraTaskUnit::EnableParamConfidences(
    MsgTaskConfig& param, std::vector<std::string> labelsNeedConfidence,
    const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs) {
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
                (void)GetConfidence(needConfidenceLabel, confidenceHigh, confidence);
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

    LOG_INFO("[{}_{}] Set Area Size From {} To {}", channel_id_, algorithm_code_, conf_area_.areas.size(),
             areas.size());
    conf_area_.areas         = areas;
    conf_area_.shieldedAreas = shieldedAreas;
    conf_area_.sign += 1;
    modify_sign_ += 1;
    SaveArea();
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
    auto ret = SetParams(params.params);
    if (util::ErrorEnum::Success != ret) {
        return ret;
    }
    return SetArea(params.areas, params.shieldedAreas);
}

util::ErrorEnum CameraTaskUnit::SetLibPara(std::vector<std::string>& libParaId) {
    conf_lib_param_.libParaId = std::move(libParaId);
    SaveLibPara();

    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskUnit::SetParams(std::vector<MsgDynamicKeyValue> params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    int paramChangeCount = 0;
    for (auto& paramUnit : params) {
        bool bFindKey = false;
        for (auto& localUnit : conf_param_.params) {
            if (paramUnit.key != localUnit.key) {
                continue;
            }

            if (paramUnit.value != localUnit.value) {
                LOG_INFO("[{}_{}] Param:{} Set From {} To {}", channel_id_, algorithm_code_, localUnit.key,
                         localUnit.value, paramUnit.value);
                localUnit.value = paramUnit.value;
                paramChangeCount += 1;
            }
            bFindKey = true;
            break;
        }

        if (false == bFindKey) {
            LOG_WARN("[{}_{}] Param:{}({}) Not Found In Local", channel_id_, algorithm_code_, paramUnit.key,
                     paramUnit.value);
            conf_param_.params.push_back(paramUnit);
            paramChangeCount += 1;
        }
    }

    if (paramChangeCount > 0) {
        LOG_INFO("[{}/{}] Have {}'st Param Changed", channel_id_, algorithm_code_, paramChangeCount);
        conf_param_.sign += 1;
        modify_sign_ += 1;
        paramChangeCount += 1;
        SaveParam();
    }
    return util::ErrorEnum::Success;
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
    if (j.contains("params") && !j["params"].is_null())
        j.at("params").get_to(v.params);
}

void to_json(nlohmann::json& j, const CameraTaskUnitParam& v) {
    j["params"] = v.params;
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
