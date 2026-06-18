// PSamDetector.h — SAM2 segmentation model wrapper for picture/snapshot mode.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "infer/Sam2SegmenterUnify.h"

namespace cosmo {

class PSamDetector : public PActionBase {
public:
    explicit PSamDetector(ActionNode& action, const std::string& taskId = "");
    ~PSamDetector() override;

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

    // Parameter state (default: BOX)
    Sam2InputType input_type_{Sam2InputType::BOX};

    // Inference instance
    Sam2SegmenterUnifyPtr inst_{nullptr};
};

using PSamDetectorPtr = std::shared_ptr<PSamDetector>;

}  // namespace cosmo
