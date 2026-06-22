// Per-algorithm task unit — manages area, parameter and library config for a single task.

#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "service/model/IModelService.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo {
// Confidence threshold configuration
enum class ConfidenceConfigType {
    kStrict = 0,  // Strict threshold
    kRecommend,   // Recommended threshold
    kMax
};
struct CameraTaskConfidenceConfig {
    std::string label;
    ConfidenceConfigType type{ConfidenceConfigType::kStrict};
    int value{0};
};

struct CameraTaskUnitParam {
    std::vector<MsgDynamicKeyValue> params;
    size_t sign{0};  // Incremented on each modification
    friend void to_json(nlohmann::json& j, const CameraTaskUnitParam& v);
    friend void from_json(const nlohmann::json& j, CameraTaskUnitParam& v);
};

struct CameraTaskUnitArea {
    std::vector<MsgTaskArea> areas;
    std::vector<MsgTaskArea> shieldedAreas;
    size_t sign{0};  // Incremented on each modification
    friend void to_json(nlohmann::json& j, const CameraTaskUnitArea& v);
    friend void from_json(const nlohmann::json& j, CameraTaskUnitArea& v);
};

struct CameraTaskUnitLibPara {
    std::vector<std::string> libParaId;
    friend void to_json(nlohmann::json& j, const CameraTaskUnitLibPara& v);
    friend void from_json(const nlohmann::json& j, CameraTaskUnitLibPara& v);
};

class CameraTaskUnit {
public:
    CameraTaskUnit(const std::string& cameraCfgPath, const std::string& cameraId,
                   const std::string& algorithmCode, std::vector<ModelInfo> models);
    ~CameraTaskUnit();

    util::ErrorEnum SetArea(const std::vector<MsgTaskArea>& areas,
                            const std::vector<MsgTaskArea>& shieldedAreas = {});
    util::ErrorEnum GetArea(std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas);
    util::ErrorEnum SetParams(std::vector<MsgDynamicKeyValue> params);
    util::ErrorEnum SetParams(const MsgTaskConfig& params);
    util::ErrorEnum SetLibPara(std::vector<std::string>& libParaId);

    [[nodiscard]] std::vector<MsgDynamicKeyValue> GetParams() const;

    [[nodiscard]] util::ErrorEnum GetStatus() const;
    [[nodiscard]] bool IsReady() const;
    void TaskEnableParam();
    void RefreshModels(std::vector<ModelInfo> models);

private:
    void SaveParam();
    void SaveArea();
    void SaveLibPara();
    void LoadConfig();
    void EnableParamConfidences(MsgTaskConfig& param);
    void EnableParamConfidences(MsgTaskConfig& param, std::vector<std::string> labelsNeedConfidence,
                                const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs);

    [[nodiscard]] CameraTaskConfidenceConfig GetConfidenceConfig(
        const std::string& label, const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs) const;
    [[nodiscard]] bool GetConfidence(const std::string& label, float& confidenceHigh,
                                     float& confidence) const;
    [[nodiscard]] float CalcConfidence(const CameraTaskConfidenceConfig& config, float& confidenceHigh,
                                       float& confidence) const;

private:
    mutable std::shared_mutex mtx_;
    std::string conf_file_path_{};               // ${cameraCfgPath}/${cameraId}/${algorithmCode}
    std::string conf_area_file_{"area.json"};    //
    std::string conf_param_file_{"param.json"};  //
    std::string conf_lib_file_{"libPara.json"};  //
    std::string channel_id_{};
    std::string algorithm_code_{};
    std::string task_id_{};
    util::ErrorEnum task_status_{util::ErrorEnum::Success};
    std::vector<ModelInfo> models_;
    CameraTaskUnitParam conf_param_{};
    CameraTaskUnitArea conf_area_{};
    CameraTaskUnitLibPara conf_lib_param_{};
    size_t enable_sign_{0};  // Set to m_modifySign when applied to the task
    size_t modify_sign_{
        100};  // Incremented on each modification; initial value forces parameter setup on start
};

using CameraTaskUnitPtr = std::shared_ptr<CameraTaskUnit>;
}  // namespace cosmo
