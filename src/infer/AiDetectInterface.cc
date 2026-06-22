// AiDetectInterface — Ai Detect Interface implementation.

#include "infer/AiDetectInterface.h"

#include "util/Log.h"

namespace cosmo::infer {
AiDetectInterface::AiDetectInterface(DetectorPoolPtr pool, const std::string& alg_code,
                                     const std::string& cfg_path, const std::string& model_path)
    : alg_code_(alg_code), cfg_path_(cfg_path), model_path_(model_path), reuse_obj_(pool) {
    if (nullptr != reuse_obj_) {
        LOG_DEBUG("AiDetectReuseInterface get instance success {}", alg_code_);
        reuse_obj_->CreateTask(alg_code, cfg_path, model_path);
    } else {
        LOG_WARN("AiDetectReuseInterface get instance failed {}", alg_code_);
    }
}

AiDetectInterface::~AiDetectInterface() {
    LOG_DEBUG("{}", "AiDetectReuseInterface release");
    reuse_obj_->DeleteTask();
}

util::ErrorEnum AiDetectInterface::Detect(const VideoFramePtr& image,
                                          const std::vector<AiConfidence>& conf_thres,
                                          std::vector<AiDetectRstEl>& rst) const {
    if (!reuse_obj_) {
        LOG_WARN("{}", "AiDetectReuseInterface instance is nullptr");
        return util::ErrorEnum::SysErr;
    }

    auto inst = reuse_obj_->GetInst(alg_code_, cfg_path_, model_path_);
    if (!inst) {
        LOG_WARN("{}", "AiDetectReuseInterface reuse instance failed");
        return util::ErrorEnum::NotInit;
    }
    std::vector<VideoFramePtr> images;
    std::vector<std::vector<AiDetectRstEl>> rsts;
    images.push_back(image);
    auto ret = inst->Detect(images, conf_thres, rsts);
    reuse_obj_->ReturnInst(inst);
    if (!rsts.empty()) {
        rst = rsts[0];
    }
    return ret;
}

}  // namespace cosmo::infer