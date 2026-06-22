// Target selection optimization

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/target/TargetChooseBest.h"

namespace cosmo {
class TargetChooseBestMng : public SimpleMapActionMng<TargetChooseBest> {
public:
    TargetChooseBestMng() : SimpleMapActionMng("TargetChooseBestMng") {}
};
}  // namespace cosmo
