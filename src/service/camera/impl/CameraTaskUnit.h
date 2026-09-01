// Per-algorithm task unit — manages area, parameter and library config for a single task.

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

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
    // Keys whose values were explicitly supplied by the channel.  The
    // presence bit distinguishes legacy full snapshots (field absent) from a
    // canonical snapshot with no channel overrides (empty array).
    std::vector<std::string> channelOverrideKeys;
    bool channelOverrideKeysPresent{false};
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
    enum class ParamApplyMode {
        kPendingOnly,
        kBeforeStart,
    };

    CameraTaskUnit(const std::string& cameraCfgPath, const std::string& cameraId,
                   const std::string& algorithmCode, std::vector<ModelInfo> models);
    ~CameraTaskUnit();

    util::ErrorEnum SetArea(const std::vector<MsgTaskArea>& areas,
                            const std::vector<MsgTaskArea>& shieldedAreas = {});
    util::ErrorEnum GetArea(std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas);
    util::ErrorEnum SetParams(std::vector<MsgDynamicKeyValue> params);
    util::ErrorEnum SetParams(const MsgTaskConfig& params);
    util::ErrorEnum SetChannelParams(std::vector<MsgDynamicKeyValue> params);
    util::ErrorEnum SetChannelParams(const MsgTaskConfig& params);
    util::ErrorEnum SetLibPara(std::vector<std::string>& libParaId);

    [[nodiscard]] std::vector<MsgDynamicKeyValue> GetParams() const;

    [[nodiscard]] util::ErrorEnum GetStatus() const;
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] bool ApplyLatestTaskConfig(ParamApplyMode mode = ParamApplyMode::kPendingOnly);
    void RefreshModels(std::vector<ModelInfo> models);

private:
    [[nodiscard]] bool SaveParam();
    [[nodiscard]] bool SaveArea();
    void SaveLibPara();
    void LoadConfig();
    void EnableParamConfidences(MsgTaskConfig& param, const std::vector<ModelInfo>& models);
    size_t MergeChannelParamsLocked(std::vector<MsgDynamicKeyValue> params);
    void EnableParamConfidences(MsgTaskConfig& param, std::vector<std::string> labelsNeedConfidence,
                                const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs,
                                const std::vector<ModelInfo>& models);

    [[nodiscard]] CameraTaskConfidenceConfig GetConfidenceConfig(
        const std::string& label, const std::vector<CameraTaskConfidenceConfig>& confidenceConfigs) const;
    [[nodiscard]] bool GetConfidence(const std::string& label, const std::vector<ModelInfo>& models,
                                     float& confidenceHigh, float& confidence) const;
    [[nodiscard]] float CalcConfidence(const CameraTaskConfidenceConfig& config, float& confidenceHigh,
                                       float& confidence) const;

private:
    mutable std::shared_mutex mtx_;
    std::mutex apply_state_mtx_;
    std::condition_variable apply_state_cv_;
    bool apply_in_progress_{false};
    std::thread::id apply_owner_{};
    std::string conf_file_path_{};               // ${cameraCfgPath}/${cameraId}/${algorithmCode}
    std::string conf_area_file_{"area.json"};    //
    std::string conf_param_file_{"param.json"};  //
    std::string conf_lib_file_{"libPara.json"};  //
    std::string channel_id_{};
    std::string algorithm_code_{};
    std::string task_id_{};
    util::ErrorEnum task_status_{util::ErrorEnum::Success};
    std::vector<ModelInfo> models_;
    std::vector<MsgDynamicElement> metadata_params_;
    CameraTaskUnitParam conf_param_{};
    CameraTaskUnitArea conf_area_{};
    CameraTaskUnitLibPara conf_lib_param_{};
    size_t enable_sign_{0};  // Set to m_modifySign when applied to the task
    size_t modify_sign_{
        100};  // Incremented on each modification; initial value forces parameter setup on start
    // Ownership is acquired only after TaskCreate succeeds.  A failed duplicate
    // constructor must never stop/delete the already existing task in its destructor.
    bool task_created_{false};
};

using CameraTaskUnitPtr = std::shared_ptr<CameraTaskUnit>;
}  // namespace cosmo
