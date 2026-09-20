#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/action/AlgActionBase.h"
#include "flow/target/TargetAssociation.h"
#include "infer/AiDetectInterface.h"

namespace cosmo {

// Generic target association. Class name and AA_00006 ID retain compatibility;
// model inference is reused through the existing detector pool.
class AiPersonFace : public AlgActionBase {
public:
    AiPersonFace(const std::string& init_task_id, ActionNode& action);
    ~AiPersonFace() override;
    bool ModifyParam(const std::string& channel_id, const std::string& task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    void HandFrame(AlgDataPtr data) override;

private:
    bool InitDetector();
    infer::AiDetectInterfacePtr detector_;
    std::string model_code_;
    TargetAssociationConfig config_;
    bool config_valid_{false};
};

class AiPersonFaceMng : public VectorActionMng<AiPersonFace> {
public:
    AiPersonFaceMng() : VectorActionMng("AiPersonFaceMng") {}
};

}  // namespace cosmo
