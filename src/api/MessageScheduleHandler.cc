// MessageScheduleHandler — Message Schedule Handler implementation.

#include "api/MessageScheduleHandler.h"

#include "service/camera/ICameraTaskConfig.h"
#include "service/task/IScheduleService.h"
#include "util/ErrorCode.h"

namespace cosmo {

MessageScheduleHandler::MessageScheduleHandler(service::IScheduleService& schedule_service,
                                               service::ICameraTaskConfig& camera_task_config)
    : schedule_service_(schedule_service), camera_task_config_(camera_task_config) {}

Schedule::MsgAddSend MessageScheduleHandler::Handle(Schedule::MsgAddRecv&& data, std::error_condition& errc) {
    Schedule::MsgAddSend retData{};
    errc = schedule_service_.Add(data, retData.resData.id);

    return retData;
}

Schedule::MsgUpdateSend MessageScheduleHandler::Handle(Schedule::MsgUpdateRecv&& data,
                                                       std::error_condition& errc) {
    Schedule::MsgUpdateSend retData{};
    errc = schedule_service_.Update(data);

    return retData;
}

Schedule::MsgPageSend MessageScheduleHandler::Handle(Schedule::MsgPageRecv&& data,
                                                     std::error_condition& /*errc*/) {
    Schedule::MsgPageSend retData{};
    retData.resData.rows =
        schedule_service_.Query(data.groupId, data.pageNum, data.pageSize, retData.resData.total);

    return retData;
}

Schedule::MsgSelectScheduleInfoSend MessageScheduleHandler::Handle(
    Schedule::MsgSelectScheduleInfoRecv&& /*data*/, std::error_condition& /*errc*/) {
    Schedule::MsgSelectScheduleInfoSend retData{};
    retData.resData.rows = schedule_service_.Query("", 1, 1000, retData.resData.total);

    return retData;
}

Schedule::MsgDeleteSend MessageScheduleHandler::Handle(Schedule::MsgDeleteRecv&& data,
                                                       std::error_condition& errc) {
    Schedule::MsgDeleteSend retData{};
    if (camera_task_config_.ScheduleInUse(data.scheduleId)) {
        errc = util::ErrorEnum::TimeTemplateInUse;
        return retData;
    }
    errc = schedule_service_.Delete(data.scheduleId);
    return retData;
}
}  // namespace cosmo