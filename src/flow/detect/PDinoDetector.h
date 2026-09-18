// PDinoDetector.h — Grounding DINO detector wrapper for picture/snapshot mode.

#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "infer/DinoDetectorUnify.h"

namespace cosmo {

class PDinoDetector : public PActionBase {
public:
    explicit PDinoDetector(ActionNode& action, const std::string& taskId = "");
    ~PDinoDetector() override;

    bool ActionInit() override;
    void ActionDestroy() override;
    util::ErrorEnum HandPic(AlgDataPtr algData) override;

    bool ModifyParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;

private:
    [[nodiscard]] bool ValidKey(const MsgDynamicKeyValue& param) const;
    bool AnalysisKey(const MsgDynamicKeyValue& param);

private:
    std::shared_mutex mtx_;

    // Parameter state
    float box_confidence_{0.3f};
    float text_confidence_{0.25f};
    std::string prompt_{"person"};

    // Inference instance
    DinoDetectorUnifyPtr inst_{nullptr};
};

using PDinoDetectorPtr = std::shared_ptr<PDinoDetector>;

}  // namespace cosmo
