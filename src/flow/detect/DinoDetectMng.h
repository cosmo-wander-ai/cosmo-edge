#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/detect/DinoDetector.h"

namespace cosmo {
class DinoDetectMng : public MultiChannelActionMng<DinoDetector> {
public:
    DinoDetectMng() : MultiChannelActionMng("DinoDetectMng") {}
};
}  // namespace cosmo
