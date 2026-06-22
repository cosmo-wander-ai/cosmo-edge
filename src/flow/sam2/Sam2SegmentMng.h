// Instance manager for Sam2Segmenter multi-channel actions.

#pragma once

#include "flow/action/ActionInstMngBase.h"
#include "flow/sam2/Sam2Segmenter.h"

namespace cosmo {
class Sam2SegmentMng : public MultiChannelActionMng<Sam2Segmenter> {
public:
    Sam2SegmentMng() : MultiChannelActionMng("Sam2SegmentMng") {}
};
}  // namespace cosmo
