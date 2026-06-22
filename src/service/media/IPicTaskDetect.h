/// @file IPicTaskDetect.h
/// @brief Image detection execution and process orchestration interface.
///        ISP split from IPicTaskService.
///        Consumed by MessageHandler.
#pragma once

#include <string>
#include <system_error>

#include "service/media/dto/DetectMsgTypes.h"
#include "util/ErrorCode.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo::service {

/// Detection execution and platform-level task process orchestration
/// for image analysis tasks.
class IPicTaskDetect {
public:
    virtual ~IPicTaskDetect() = default;

    /// Execute detection on a single image.
    /// @param taskId  Task identifier.
    /// @param data    Detection request containing the image.
    /// @param retData [out] Detection results.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum DetectPic(const std::string& taskId, cosmo::MsgPTaskDetectPicRecv& data,
                                             cosmo::MsgPTaskDetectPicSend& retData) = 0;

    /// Process a task creation request from the platform.
    /// @param data Request payload.
    /// @param errc [out] Error condition if creation fails.
    /// @return Response payload.
    virtual cosmo::MsgPTaskCreateSend ProcessPTaskCreate(cosmo::MsgPTaskCreateRecv& data,
                                                         std::error_condition& errc) = 0;

    /// Process a task cancellation request from the platform.
    /// @param data Request payload.
    /// @param errc [out] Error condition if cancellation fails.
    /// @return Response payload.
    virtual cosmo::MsgPTaskCancleSend ProcessPTaskCancel(cosmo::MsgPTaskCancleRecv& data,
                                                         std::error_condition& errc) = 0;

    /// Process a batch detection request.
    /// @param data Request payload with image group.
    /// @param errc [out] Error condition if detection fails.
    /// @return Response payload with detection results.
    virtual cosmo::MsgDetectSend ProcessDetectGroup(cosmo::MsgDetectRecv& data,
                                                    std::error_condition& errc) = 0;
};

}  // namespace cosmo::service
