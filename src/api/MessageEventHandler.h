#pragma once
#include <system_error>

#include "service/event/dto/EventDto.h"

namespace cosmo::service {
class IAlarmRecordService;
class IAlgorithmQuery;
class INetworkConfig;
}  // namespace cosmo::service

namespace cosmo {

/// Handles event-related API requests: event query, alarm export, passenger flow.
class MessageEventHandler {
public:
    MessageEventHandler(service::IAlarmRecordService& alarm_service,
                        service::IAlgorithmQuery& algorithm_query, service::INetworkConfig& network_config);
    ~MessageEventHandler() = default;

    MessageEventHandler(const MessageEventHandler&)            = delete;
    MessageEventHandler& operator=(const MessageEventHandler&) = delete;

    /// Query alarm events with pagination.
    [[nodiscard]] Event::MsgPageSend Handle(Event::MsgPageRecv&& data, std::error_condition& errc) const;

    /// Export alarm events to CSV file.
    [[nodiscard]] Event::MsgExportAlarmSend Handle(Event::MsgExportAlarmRecv&& data,
                                                   std::error_condition& errc) const;

    /// Query passenger flow statistics.
    [[nodiscard]] Event::MsgQueryPassengerFlowNumberSend Handle(Event::MsgQueryPassengerFlowNumberRecv&& data,
                                                                std::error_condition& errc) const;

private:
    service::IAlarmRecordService& alarm_service_;
    service::IAlgorithmQuery& algorithm_query_;
    service::INetworkConfig& network_config_;
};

}  // namespace cosmo
