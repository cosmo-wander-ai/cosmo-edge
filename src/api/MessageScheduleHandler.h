#pragma once
#include <system_error>

#include "service/task/dto/ScheduleDto.h"

namespace cosmo::service {
class IScheduleService;
class ICameraTaskConfig;
}  // namespace cosmo::service

namespace cosmo {

class MessageScheduleHandler {
public:
    explicit MessageScheduleHandler(service::IScheduleService& schedule_service,
                                    service::ICameraTaskConfig& camera_task_config);
    Schedule::MsgAddSend Handle(Schedule::MsgAddRecv&& data, std::error_condition& errc);
    Schedule::MsgUpdateSend Handle(Schedule::MsgUpdateRecv&& data, std::error_condition& errc);
    Schedule::MsgPageSend Handle(Schedule::MsgPageRecv&& data, std::error_condition& errc);
    Schedule::MsgSelectScheduleInfoSend Handle(Schedule::MsgSelectScheduleInfoRecv&& data,
                                               std::error_condition& errc);
    Schedule::MsgDeleteSend Handle(Schedule::MsgDeleteRecv&& data, std::error_condition& errc);

private:
    service::IScheduleService& schedule_service_;
    service::ICameraTaskConfig& camera_task_config_;
};

}  // namespace cosmo
