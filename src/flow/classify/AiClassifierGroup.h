// AI classifier action with group-based classification.

#pragma once

#include <cstdint>
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
    std::vector<AiGroupEl> GenGroupEmements(std::vector<GroupAreaData>& areas, int& next_group_id);
    void GenAreaSource(DataDetTrackClassifyPtr input, std::vector<GroupAreaData>& areas);

    bool CheckDataAvailable(AlgDataPtr algData) override;
    void HandFramesEx(std::vector<AlgDataPtr> algDatas) override;

    int group_id_{0};
    // Protected by mtx along with group_id_ and group_area_datas_.
    uint64_t group_state_generation_{0};
    AAClassifyGroupParam params_;
    std::vector<GroupAreaData> group_area_datas_;
};
using AiClassifierGroupPtr = std::shared_ptr<AiClassifierGroup>;
}  // namespace cosmo
