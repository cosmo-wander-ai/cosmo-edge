// AiRecognizerInterface — Recognizer model interface.

#pragma once

#include "infer/AiRecognizerUnify.h"
#include "infer/InferPoolTypes.h"

namespace cosmo {
class AiRecognizerInterface {
public:
    AiRecognizerInterface(RecognizerPoolPtr pool, const std::string& alg_code, const std::string& cfg_path,
                          const std::string& model_path);
    ~AiRecognizerInterface();

    util::ErrorEnum Recognize(VideoFramePtr images, std::vector<AiDetectRstEl>& io_puts,
                              bool use_box = false);  // False use landmark, true use box.

    float CompareFeature(const AiFeature& feature1, const AiFeature& feature2);

private:
    std::string alg_code_;
    std::string cfg_path_;
    std::string model_path_;
    RecognizerPoolPtr reuse_obj_{nullptr};
};

using AiRecognizerInterfacePtr = std::shared_ptr<AiRecognizerInterface>;

}  // namespace cosmo
