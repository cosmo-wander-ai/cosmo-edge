// Instance manager for AiLandmark video stream actions.

#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/landmark/AiLandmark.h"

namespace cosmo {
class AiLandmarkMng : public SimpleMapActionMng<AiLandmark> {
public:
    AiLandmarkMng() : SimpleMapActionMng("AiLandmarkMng") {}
};
}  // namespace cosmo
