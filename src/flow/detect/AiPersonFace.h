#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/action/AlgActionBase.h"
#include "infer/AiDetectInterface.h"

namespace cosmo {

// Task-local association; model inference is reused through the detector pool.
class AiPersonFace : public AlgActionBase {
public:
    AiPersonFace(const std::string& task_id, ActionNode& action);
    ~AiPersonFace() override;
    bool ModifyParam(const std::string& channel_id, const std::string& task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    void HandFrame(AlgDataPtr data) override;

private:
    bool InitDetector();
    infer::AiDetectInterfacePtr detector_;
    std::string model_code_;
    int min_face_size_{60};
    float face_confidence_{0.66f};
};

class AiPersonFaceMng : public VectorActionMng<AiPersonFace> {
public:
    AiPersonFaceMng() : VectorActionMng("AiPersonFaceMng") {}
};

}  // namespace cosmo
