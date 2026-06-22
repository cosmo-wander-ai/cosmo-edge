// MessageHandler.h — Message router for dispatching HTTP/MQTT requests to appropriate services.
#pragma once

#include <system_error>

#include "service/media/dto/DetectMsgTypes.h"
#include "service/task/dto/StatusMsgTypes.h"
#include "util/dto/AlgorithmMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {

// Route requests to appropriate handler methods based on message type
class MessageHandler {
public:
    MessageHandler() = default;

    MsgInterfaceTestSend Handle(MsgInterfaceTestRecv&& data,
                                std::error_condition& errc);                         // Test interface
    MsgTaskCreateSend Handle(MsgTaskCreateRecv&& data, std::error_condition& errc);  // Create task
    MsgTaskCancleSend Handle(MsgTaskCancleRecv&& data, std::error_condition& errc);  // Cancel task
    MsgPTaskCreateSend Handle(MsgPTaskCreateRecv&& data,
                              std::error_condition& errc);  // Create task (image algorithm)
    MsgPTaskCancleSend Handle(MsgPTaskCancleRecv&& data,
                              std::error_condition& errc);  // Cancel task (image algorithm)
    MsgPTaskDetectPicSend Handle(MsgPTaskDetectPicRecv&& data,
                                 std::error_condition& errc);  // Image detection for image algorithms
    MsgDetectSend Handle(MsgDetectRecv&& data,
                         std::error_condition& errc);  // Image detection for image algorithms (China Mobile)
    MsgOperateNodeSend Handle(MsgOperateNodeRecv&& data,
                              std::error_condition& errc);                 // Update image algorithm
    MsgInfoSend Handle(MsgInfoRecv&& data, std::error_condition& errc);    // System info
    MsgProbeSend Handle(MsgProbeRecv&& data, std::error_condition& errc);  // Probe
    MsgViewRoutesSend Handle(MsgViewRoutesRecv&& data,
                             std::error_condition& errc);  // Set number of preview routes
    MsgGraphicsMemorySend Handle(MsgGraphicsMemoryRecv&& data,
                                 std::error_condition& errc);  // Graphics memory utilization
    MsgOverviewStructrueRecordSend Handle(
        MsgOverviewStructrueRecordRecv&& data,
        std::error_condition& errc);  // DEBUG: Toggle structured file recording
    MsgAlgorithmPreloadSend Handle(MsgAlgorithmPreloadRecv&& data,
                                   std::error_condition& errc);  // Preload face feature comparison service
    MsgGetFeaturesSend Handle(MsgGetFeaturesRecv&& data,
                              std::error_condition& errc);  // Face feature extraction
    MsgLoadLocalAlgorithmActionSend Handle(
        MsgLoadLocalAlgorithmActionRecv&& data,
        std::error_condition& errc);  // DEBUG: Load local algorithm orchestration
    MsgLogicTestSend Handle(MsgLogicTestRecv&& data, std::error_condition& errc);  // Logic interface test
    MsgQueryTaskOverviewFileSend Handle(MsgQueryTaskOverviewFileRecv&& data,
                                        std::error_condition& errc);  // Overlay structured file request
    MsgQueryTaskStatusSend Handle(MsgQueryTaskStatusRecv&& data,
                                  std::error_condition& errc);  // Query task status
    MsgQueryTaskInfoSend Handle(MsgQueryTaskInfoRecv&& data,
                                std::error_condition& errc);  // Query task information
    MsgQueryDeviceMemStatusSend Handle(MsgQueryDeviceMemStatusRecv&& data,
                                       std::error_condition& errc);  // Query device graphics memory status
    MsgQueryLogsSend Handle(MsgQueryLogsRecv&& data, std::error_condition& errc);  // Query log list
};

}  // namespace cosmo
