// AiLandmarkInterface — Ai Landmark Interface implementation.

#include "infer/AiLandmarkInterface.h"

#include "util/Log.h"

namespace cosmo {
AiLandmarkInterface::AiLandmarkInterface(LandmarkPoolPtr pool, const std::string& algCode,
                                         const std::string& cfgPath, const std::string& modelPath)
    : alg_code_(algCode), cfg_path_(cfgPath), model_path_(modelPath), reuse_obj_(pool) {
    if (reuse_obj_ != nullptr) {
        LOG_DEBUG("AiLandmarkInterface get instance success {}", alg_code_);
        reuse_obj_->CreateTask(alg_code_, cfg_path_, model_path_);
    } else {
        LOG_WARN("AiLandmarkInterface get instance failed {}", alg_code_);
    }
}

AiLandmarkInterface::~AiLandmarkInterface() {
    LOG_DEBUG("{}", "AiLandmarkInterface release");
    reuse_obj_->DeleteTask();
}

util::ErrorEnum AiLandmarkInterface::Marker(VideoFramePtr image, std::vector<AiDetectRstEl>& ioPuts) {
    if (!reuse_obj_) {
        LOG_WARN("AiLandmarkInterface instance is null, ioPuts.size:{}", ioPuts.size());
        return util::ErrorEnum::SysErr;
    }

    auto inst = reuse_obj_->GetInst(alg_code_, cfg_path_, model_path_);
    if (!inst) {
        LOG_WARN("{}", "AiLandmarkInterface get inst failed");
        return util::ErrorEnum::NotInit;
    }

    auto ret = inst->Marker(image, ioPuts);
    reuse_obj_->ReturnInst(inst);

    return ret;
}

}  // namespace cosmo