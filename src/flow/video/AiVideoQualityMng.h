// Instance manager for AiVideoQuality actions.

#pragma once

#include <mutex>
#include <shared_mutex>
#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/video/AiVideoQuality.h"

namespace cosmo {
class AiVideoQualityMng : public MapActionMng<AiVideoQuality> {
public:
    AiVideoQualityMng() : MapActionMng("AiVideoQualityMng") {}

    AiVideoQualityPtr GetInst(const std::string& task_id, ActionNode& action_param);

    bool DeleteInst(AiVideoQualityPtr inst, const std::string& task_id);
};
}  // namespace cosmo
