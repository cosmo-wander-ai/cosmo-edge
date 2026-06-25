#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/task/ITaskService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockTaskService : public cosmo::service::ITaskService {
public:
    // ITaskLifecycle
    MAKE_MOCK4(TaskCreate,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      cosmo::ActionAlgPtr),
               override);
    MAKE_MOCK1(TaskDelete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK2(ProcessTaskCreate, cosmo::MsgTaskCreateSend(cosmo::MsgTaskCreateRecv&, std::error_condition&),
               override);
    MAKE_MOCK2(ProcessTaskCancel, cosmo::MsgTaskCancleSend(cosmo::MsgTaskCancleRecv&, std::error_condition&),
               override);
    MAKE_MOCK2(TaskStart, bool(const std::string&, const std::string&), override);
    MAKE_MOCK1(TaskStop, bool(const std::string&), override);
    MAKE_MOCK1(TaskIsStart, bool(const std::string&), override);
    MAKE_MOCK3(SetTaskParam, bool(const std::string&, const std::string&, cosmo::MsgTaskConfig&), override);
    MAKE_MOCK2(LogicTest, bool(const std::string&, cosmo::MsgTarget&), override);
    MAKE_MOCK1(ShowActions, void(cosmo::ActionAlgPtr), override);
    MAKE_MOCK1(RecordClearTaskData, void(const std::string&), override);
    MAKE_MOCK2(RecordTaskInfo, void(const std::string&, cosmo::MsgTaskCreateRecv&), override);
    MAKE_MOCK2(RecordTaskAction, void(const std::string&, cosmo::ActionAlgPtr), override);
    // ITaskChannel
    MAKE_MOCK2(TaskChannelSetUrl, void(const std::string&, const std::string&), override);
    MAKE_MOCK3(TaskChannelSetParam, void(const std::string&, const std::string&, int), override);
    MAKE_MOCK2(CaptureImage, VideoFramePtr(const std::string&, int), override);
    int mock_dataStatus = 0;
    bool GetChannelAttr(const std::string& channelId, cosmo::MsgCameraAttr& attr) override {
        attr.dataStatus = mock_dataStatus;
        return true;
    }
    MAKE_MOCK1(TaskDataActive, bool(const std::string&), override);
    MAKE_MOCK1(GetChannelInst, cosmo::AlgChannelPtr(const std::string&), override);
    MAKE_MOCK1(GetChannelTasks, std::vector<std::string>(const std::string&), override);
    MAKE_MOCK2(GetAlarmInst, cosmo::TaskAlarmPtr(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetTaskChannel, std::string(const std::string&), override);
    MAKE_MOCK1(GetCameraInfo, void(std::vector<cosmo::MsgCameraInfo>&), override);
    // ITaskQuery
    MAKE_MOCK1(QueryTasks, std::vector<std::string>(bool), override);
    MAKE_MOCK3(GetTaskParam, bool(const std::string&, const std::string&, cosmo::MsgTaskConfig&), override);
    MAKE_MOCK2(GetTaskStatus, std::vector<cosmo::TaskStatus>(const std::vector<std::string>&, unsigned int),
               override);
    MAKE_MOCK0(CameraTaskInfo, std::vector<cosmo::MsgCameraInfo>(), override);
    MAKE_MOCK6(GetTaskFrameInfo, bool(const std::string&, bool&, int64_t&, int64_t&, int64_t&, std::string&),
               override);
    MAKE_MOCK0(TaskCount, size_t(), override);
    MAKE_MOCK1(GetAlgorithmCount, int(const std::string&), override);
    MAKE_MOCK2(QueueStatus, void(std::vector<cosmo::AlgActionDataQueueStatus>&, unsigned int), override);
    MAKE_MOCK2(QueueStatusDto, void(std::vector<cosmo::AlgActionDataQueueStatusDto>&, unsigned int),
               override);
    MAKE_MOCK4(PacketStatus, void(size_t&, size_t&, size_t&, size_t&), override);
    MAKE_MOCK4(GetTaskLiveOverviewInfo,
               std::vector<cosmo::MsgOverviewMem>(const std::string&, int64_t, int64_t, int64_t), override);
    MAKE_MOCK2(GetChannelAttr2, bool(const std::string&, cosmo::MsgCameraAttr&));
    // Note: GetChannelAttr satisfies both ITaskQuery and ITaskChannel base classes
    MAKE_MOCK5(GetTaskDetHistory,
               std::vector<cosmo::DataDetTrackClassify>(const std::string&, const std::string&, int64_t,
                                                        int64_t, int64_t),
               override);
    MAKE_MOCK2(GetTaskActionDurations,
               (std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>>)(const std::string&, int),
               override);
};

}  // namespace cosmo::test
