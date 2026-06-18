#pragma once
#include <system_error>

#include "service/media/dto/VideoTaskDto.h"

namespace cosmo::service {
class ICameraTaskConfig;
class IAlgorithmQuery;
class ICameraDeviceCrud;
class ITaskQuery;
}  // namespace cosmo::service

namespace cosmo {

class MessageVideoTaskHandler {
public:
    MessageVideoTaskHandler(service::ICameraTaskConfig& task_config,
                            service::IAlgorithmQuery& algorithm_query,
                            service::ICameraDeviceCrud& camera_crud, service::ITaskQuery& task_query);
    VideoTask::MsgModifyParamSend Handle(VideoTask::MsgModifyParamRecv&& data, std::error_condition& errc);
    VideoTask::MsgQueryParamSend Handle(VideoTask::MsgQueryParamRecv&& data, std::error_condition& errc);
    VideoTask::MsgModifyAreaSend Handle(VideoTask::MsgModifyAreaRecv&& data, std::error_condition& errc);
    VideoTask::MsgQueryAreaSend Handle(VideoTask::MsgQueryAreaRecv&& data, std::error_condition& errc);
    VideoTask::MsgModifyStrategySend Handle(VideoTask::MsgModifyStrategyRecv&& data,
                                            std::error_condition& errc);
    VideoTask::MsgQueryStrategySend Handle(VideoTask::MsgQueryStrategyRecv&& data,
                                           std::error_condition& errc);
    VideoTask::MsgSwitchTaskSend Handle(VideoTask::MsgSwitchTaskRecv&& data, std::error_condition& errc);
    VideoTask::MsgBatchSwitchTaskSend Handle(VideoTask::MsgBatchSwitchTaskRecv&& data,
                                             std::error_condition& errc);
    VideoTask::MsgQuerySwitchSend Handle(VideoTask::MsgQuerySwitchRecv&& data, std::error_condition& errc);
    VideoTask::MsgSaveOrUpdateSend Handle(VideoTask::MsgSaveOrUpdateRecv&& data, std::error_condition& errc);
    VideoTask::MsgSelectConfigByAlgorithmIdSend Handle(VideoTask::MsgSelectConfigByAlgorithmIdRecv&& data,
                                                       std::error_condition& errc);
    VideoTask::MsgSelectAllAlgorithmInfoSend Handle(VideoTask::MsgSelectAllAlgorithmInfoRecv&& data,
                                                    std::error_condition& errc);
    VideoTask::MsgListChannelSend Handle(VideoTask::MsgListChannelRecv&& data, std::error_condition& errc);
    VideoTask::MsgApplyParamsBatchSend Handle(VideoTask::MsgApplyParamsBatchRecv&& data,
                                              std::error_condition& errc);
    VideoTask::MsgDeleteSend Handle(VideoTask::MsgDeleteRecv&& data, std::error_condition& errc);
    VideoTask::MsgBatchDeleteSend Handle(VideoTask::MsgBatchDeleteRecv&& data, std::error_condition& errc);
    VideoTask::MsgRunningDetailSend Handle(VideoTask::MsgRunningDetailRecv&& data,
                                           std::error_condition& errc);

private:
    service::ICameraTaskConfig& task_config_;
    service::IAlgorithmQuery& algorithm_query_;
    service::ICameraDeviceCrud& camera_crud_;
    service::ITaskQuery& task_query_;
};

}  // namespace cosmo
