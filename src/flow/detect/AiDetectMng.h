#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/detect/AiDetector.h"

namespace cosmo {
class AiDetectMng : public MultiChannelActionMng<AiDetector> {
public:
    AiDetectMng() : MultiChannelActionMng("AiDetectMng") {}
};
}  // namespace cosmo
