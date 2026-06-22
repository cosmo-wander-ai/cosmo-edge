// AiLandmarkInterface — Landmark model interface.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "infer/AiLandmarkerUnify.h"
#include "infer/InferPoolTypes.h"

namespace cosmo {

class AiLandmarkInterface {
public:
    AiLandmarkInterface(LandmarkPoolPtr pool, const std::string& algCode, const std::string& cfgPath,
                        const std::string& modelPath);
    ~AiLandmarkInterface();

    util::ErrorEnum Marker(VideoFramePtr image, std::vector<AiDetectRstEl>& ioPuts);

private:
    std::string alg_code_;
    std::string cfg_path_;
    std::string model_path_;
    LandmarkPoolPtr reuse_obj_{nullptr};
};

using AiLandmarkInterfacePtr = std::shared_ptr<AiLandmarkInterface>;

}  // namespace cosmo
