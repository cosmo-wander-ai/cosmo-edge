#include "flow/alarm/AlarmBatch.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace cosmo::alarm {
namespace {

    bool IsBatchable(const DataAlarmUnit& unit, OnEventsPropertyType propertyType, unsigned int multiAlarms) {
        return !unit.attributeRecord && propertyType == OnEventsPropertyType::None && multiAlarms == 1 &&
               unit.reportType == OnEventsReportType::Trigger && !unit.targets.empty();
    }

    bool HasSameBatchKey(const DataAlarmUnit& left, const DataAlarmUnit& right) {
        return left.flowActionId == right.flowActionId && left.areaId == right.areaId &&
               left.assoAreaId == right.assoAreaId && left.reportType == right.reportType;
    }

    util::Box UnionBoxes(const util::Box& left, const util::Box& right) {
        if (left.empty()) {
            return right;
        }
        if (right.empty()) {
            return left;
        }

        const int64_t x1 = std::min<int64_t>(left.x, right.x);
        const int64_t y1 = std::min<int64_t>(left.y, right.y);
        const int64_t x2 = std::max<int64_t>(static_cast<int64_t>(left.x) + left.width,
                                             static_cast<int64_t>(right.x) + right.width);
        const int64_t y2 = std::max<int64_t>(static_cast<int64_t>(left.y) + left.height,
                                             static_cast<int64_t>(right.y) + right.height);

        util::Box result;
        result.x = static_cast<int>(
            std::clamp<int64_t>(x1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        result.y = static_cast<int>(
            std::clamp<int64_t>(y1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        result.width  = static_cast<int>(std::clamp<int64_t>(x2 - x1, 0, std::numeric_limits<int>::max()));
        result.height = static_cast<int>(std::clamp<int64_t>(y2 - y1, 0, std::numeric_limits<int>::max()));
        return result;
    }

    template <typename T>
    void Append(std::vector<T>& destination, const std::vector<T>& source) {
        destination.insert(destination.end(), source.begin(), source.end());
    }

}  // namespace

std::vector<AlarmBatchIndices> BuildAlarmBatches(const std::deque<DataAlarmUnit>& alarms,
                                                 OnEventsPropertyType propertyType,
                                                 unsigned int multiAlarms) {
    std::vector<AlarmBatchIndices> batches;
    for (size_t index = 0; index < alarms.size(); ++index) {
        const auto& unit = alarms[index];
        if (!IsBatchable(unit, propertyType, multiAlarms)) {
            batches.push_back({index});
            continue;
        }

        auto batch = std::find_if(batches.begin(), batches.end(), [&](const auto& candidate) {
            if (candidate.empty()) {
                return false;
            }
            const auto& representative = alarms[candidate.front()];
            return IsBatchable(representative, propertyType, multiAlarms) &&
                   HasSameBatchKey(representative, unit);
        });
        if (batch == batches.end()) {
            batches.push_back({index});
        } else {
            batch->push_back(index);
        }
    }
    for (auto& batch : batches) {
        std::sort(batch.begin(), batch.end(), [&](size_t left, size_t right) {
            const auto& leftUnit  = alarms[left];
            const auto& rightUnit = alarms[right];
            if (leftUnit.trackId != rightUnit.trackId) {
                return leftUnit.trackId < rightUnit.trackId;
            }
            if (leftUnit.strTrackId != rightUnit.strTrackId) {
                return leftUnit.strTrackId < rightUnit.strTrackId;
            }
            return left < right;
        });
    }
    return batches;
}

DataAlarmUnit MergeAlarmBatch(const std::deque<DataAlarmUnit>& alarms,
                              const AlarmBatchIndices& acceptedIndices) {
    if (acceptedIndices.empty()) {
        return {};
    }

    DataAlarmUnit result = alarms.at(acceptedIndices.front());
    util::Box unionBox   = result.box;
    for (size_t offset = 1; offset < acceptedIndices.size(); ++offset) {
        const auto& member = alarms.at(acceptedIndices[offset]);
        unionBox           = UnionBoxes(unionBox, member.box);
        Append(result.targets, member.targets);
        Append(result.boxs, member.boxs);
        Append(result.friends, member.friends);
        Append(result.bestInfos, member.bestInfos);
    }
    result.box = unionBox;
    if (acceptedIndices.size() > 1) {
        // Scalar related-target state belongs to one target and must not drive
        // the crop for a batch. Use the union of all primary boxes instead.
        result.haveRelated = false;
    }
    return result;
}

}  // namespace cosmo::alarm
