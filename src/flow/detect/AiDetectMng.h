#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/detect/AiDetector.h"

namespace cosmo {

// Regular YOLO detectors use FPS-aware placement.
class AiDetectMng : public MultiChannelActionMng<AiDetector> {
public:
    AiDetectMng() : MultiChannelActionMng("AiDetectMng") {}

    AiDetectorPtr GetDetectorForChannel(const std::string& algCode, const std::string& channelId,
                                        const std::string& task, ActionNode& action);

private:
    using MultiChannelActionMng<AiDetector>::GetInst;
};

}  // namespace cosmo
