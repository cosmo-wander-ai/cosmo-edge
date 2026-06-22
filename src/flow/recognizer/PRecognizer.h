#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "infer/AiRecognizerInterface.h"
#include "util/DurationStat.h"

namespace cosmo {
class PRecognizer : public PActionBase {
public:
    PRecognizer(const std::string& task_id, ActionNode& action);
    ~PRecognizer();

    bool ActionInit() override;
    util::ErrorEnum HandPic(AlgDataPtr alg_data) override;

    // Modify parameters - modify based on existing parameters
    bool ModifyParam(const std::string& task_id, std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous parameters and set entirely new ones
    bool SetParam(const std::string& task_id, std::vector<MsgDynamicKeyValue>& params) override;

private:
    bool AnalysisKey(MsgDynamicKeyValue& param);

    std::shared_mutex mtx_;
    AiRecognizerInterfacePtr inst_;
    util::ErrorEnum action_status_pr_{util::ErrorEnum::Success};
    util::DurationStat duration_stat_;
    std::vector<std::string> face_set_;  // Bound face sets
};
using PRecognizerPtr = std::shared_ptr<PRecognizer>;
}  // namespace cosmo
