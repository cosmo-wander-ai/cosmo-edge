// AiRecognizerInterface — Ai Recognizer Interface implementation.

#include "infer/AiRecognizerInterface.h"

#include "util/Log.h"

namespace cosmo {
AiRecognizerInterface::AiRecognizerInterface(RecognizerPoolPtr pool, const std::string& alg_code,
                                             const std::string& cfg_path, const std::string& model_path)
    : alg_code_(alg_code), cfg_path_(cfg_path), model_path_(model_path), reuse_obj_(pool) {
    if (nullptr != reuse_obj_) {
        LOG_DEBUG("AiRecognizerInterface get instance success {}", alg_code_);
        reuse_obj_->CreateTask(alg_code_, cfg_path, model_path);
    } else {
        LOG_WARN("AiRecognizerInterface get instance failed {}", alg_code_);
    }
}

AiRecognizerInterface::~AiRecognizerInterface() {
    LOG_DEBUG("{}", "AiRecognizerInterface release");
    reuse_obj_->DeleteTask();
}

util::ErrorEnum AiRecognizerInterface::Recognize(VideoFramePtr image, std::vector<AiDetectRstEl>& io_puts,
                                                 bool use_box) {
    if (!reuse_obj_) {
        LOG_WARN("{}", "AiRecognizerInterface instance is nullptr");
        return util::ErrorEnum::SysErr;
    }

    auto inst = reuse_obj_->GetInst(alg_code_, cfg_path_, model_path_);
    if (!inst) {
        LOG_WARN("{}", "AiRecognizerInterface reuse instance failed");
        return util::ErrorEnum::NotInit;
    }

    auto ret = inst->Extract(image, io_puts, use_box);
    reuse_obj_->ReturnInst(inst);

    return ret;
}

float AiRecognizerInterface::CompareFeature(const AiFeature& feature1, const AiFeature& feature2) {
    if (!reuse_obj_) {
        LOG_WARN("{}", "AiRecognizerInterface instance is nullptr");
        return -1.0f;
    }

    auto inst = reuse_obj_->GetInst(alg_code_, cfg_path_, model_path_);
    if (!inst) {
        LOG_WARN("{}", "AiRecognizerInterface reuse instance failed");
        return -1.0f;
    }

    float ret = inst->CompareFeature(feature1, feature2);
    reuse_obj_->ReturnInst(inst);

    return ret;
}

}  // namespace cosmo