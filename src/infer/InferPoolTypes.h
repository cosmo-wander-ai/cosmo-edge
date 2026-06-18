#pragma once

#include <memory>

#include "util/InstancePool.h"

namespace cosmo {

// Forward declarations — full definitions live in the respective *Unify.h headers.
class AiRecognizerUnify;
class AiLandmarkerUnify;
class AiDetectorUnify;

using RecognizerPool    = InstancePool<AiRecognizerUnify, std::shared_ptr<AiRecognizerUnify>>;
using RecognizerPoolPtr = std::shared_ptr<RecognizerPool>;

using LandmarkPool    = InstancePool<AiLandmarkerUnify, std::shared_ptr<AiLandmarkerUnify>>;
using LandmarkPoolPtr = std::shared_ptr<LandmarkPool>;

using DetectorPool    = InstancePool<AiDetectorUnify, std::shared_ptr<AiDetectorUnify>>;
using DetectorPoolPtr = std::shared_ptr<DetectorPool>;

}  // namespace cosmo
