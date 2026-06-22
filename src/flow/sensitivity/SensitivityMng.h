#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/sensitivity/Sensitivity.h"

namespace cosmo {
class SensitivityMng : public VectorActionMng<Sensitivity> {
public:
    SensitivityMng() : VectorActionMng("SensitivityMng") {}
};
}  // namespace cosmo
