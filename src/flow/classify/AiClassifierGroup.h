// AI classifier action with group-based classification.

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/classify/AiClassifierBase.h"

namespace cosmo {
struct AAClassifyGroupParam {
    AlgClassifyGroupType group_type{
        AlgClassifyGroupType::AlgClassifyGroupTypeAreaTargetCount};  // Grouping method
    int group_area_target_count{2};                                  // Target count per area group
};
class AiClassifierGroup : public AiClassifierBase {
public:
    AiClassifierGroup(const std::string& taskId, ActionNode& action);
    ~AiClassifierGroup() = default;

    // Set areas - clears previous areas and applies a full replacement.
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

private:
    struct GroupAreaData {
        std::string area_id;
        std::string area_name;
        int group_id{-1};
        std::string group_id_string;
        std::vector<int> targets;       // Current targets
        std::vector<int> last_targets;  // Previous targets
        std::vector<AiDetectRstEl> real_targets;
    };
    void SetAssoAreas(std::vector<MsgTaskArea>& areas);

    bool GenGroupEl(AiGroupEl& el);
    std::vector<AiGroupEl> GenGroupEmements();
    void GenAreaSource(DataDetTrackClassifyPtr input);

    bool CheckDataAvailable(AlgDataPtr algData) override;
    void HandFramesEx(std::vector<AlgDataPtr> algDatas) override;

    int group_id_{0};
    AAClassifyGroupParam params_;
    std::vector<GroupAreaData> group_area_datas_;
};
using AiClassifierGroupPtr = std::shared_ptr<AiClassifierGroup>;
}  // namespace cosmo
