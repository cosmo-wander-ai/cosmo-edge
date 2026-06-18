#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/sensitivity/PosSaveSensitivity.h"

namespace cosmo {
class PosSaveSensitivityMng : public VectorActionMng<PosSaveSensitivity> {
public:
    PosSaveSensitivityMng() : VectorActionMng("PosSaveSensitivityMng") {}
    ~PosSaveSensitivityMng() override = default;
};
}  // namespace cosmo
