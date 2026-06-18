// Picture-mode classifier action for single-frame classification.

#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "infer/AiClassifierUnify.h"
#include "util/DurationStat.h"
#include "util/GeometricPos.h"

namespace cosmo {
class PClassifier : public PActionBase {
public:
    PClassifier(const std::string& taskId, ActionNode& action);
    ~PClassifier();

    bool ActionInit() override;
    util::ErrorEnum HandPic(AlgDataPtr algData) override;

    // Modify parameters - update existing parameters incrementally.
    bool ModifyParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear all existing parameters and apply new ones.
    bool SetParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;

private:
    bool ValidKey(MsgDynamicKeyValue& param);
    bool AnalysisKey(MsgDynamicKeyValue& param);

private:
    std::shared_mutex m_mtx;
    AiClassifierUnifyPtr m_inst;  // Classifier instance
    size_t m_handleFrames{0};
    size_t m_filterFrames{0};
    util::ErrorEnum m_actionStatus{util::ErrorEnum::Success};
    util::DurationStat m_durationStat;
};
using PClassifierPtr = std::shared_ptr<PClassifier>;
}  // namespace cosmo
