// Picture-mode landmark keypoint detection action.

#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "infer/AiLandmarkInterface.h"
#include "util/DurationStat.h"

namespace cosmo {
class PLandmark : public PActionBase {
public:
    PLandmark(const std::string& task_id, ActionNode& action);
    ~PLandmark() override;

    [[nodiscard]] bool ActionInit() override;
    util::ErrorEnum HandPic(AlgDataPtr alg_data) override;

private:
    std::shared_mutex mtx_;
    AiLandmarkInterfacePtr inst_;
    util::ErrorEnum action_status_{util::ErrorEnum::Success};
    util::DurationStat duration_stat_;
};
using PLandmarkPtr = std::shared_ptr<PLandmark>;
}  // namespace cosmo
