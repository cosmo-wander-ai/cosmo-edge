// Event DTO definitions (extracted from MessageEventHandler.h)

#pragma once

#include <system_error>

#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace Event {
    struct MsgPageRecv : public MsgRecvHead, public MsgConditionEvent {};

    void to_json(nlohmann::json& j, const MsgPageRecv& v);
    void from_json(const nlohmann::json& j, MsgPageRecv& v);

    struct MsgExportAlarmRecv : public MsgRecvHead, public MsgConditionEvent {};

    void to_json(nlohmann::json& j, const MsgExportAlarmRecv& v);
    void from_json(const nlohmann::json& j, MsgExportAlarmRecv& v);

    // Query
    struct MsgPageSend : public MsgSendHead {
        struct ResData {
            int64_t total{0};
            std::vector<MsgEventUnit> rows;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgPageSend& v);
    void from_json(const nlohmann::json& j, MsgPageSend& v);

    struct MsgExportAlarmSend : public MsgSendHead {
        struct ResData {
            std::string fileUrl;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgExportAlarmSend& v);
    void from_json(const nlohmann::json& j, MsgExportAlarmSend& v);

    // Passenger flow statistics request
    struct MsgQueryPassengerFlowNumberRecv : public MsgRecvHead {
        util::String<1, 36> channelId;
        util::String<1, 36> algorithmCode;
        util::RangeInt<0, 4> type{0};
        std::string startTime;
        std::string endTime;
        uint64_t startHour{0};
        uint64_t endHour{0};
        int reported{-1};
    };

    void to_json(nlohmann::json& j, const MsgQueryPassengerFlowNumberRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryPassengerFlowNumberRecv& v);
    // Passenger flow statistics response
    struct MsgQueryPassengerFlowNumberSend : public MsgSendHead {
        struct TimePoint {
            uint64_t hour{0};
            std::string timeString;
            std::string channelId;
            std::string algorithmCode;
            int enterNumber{0};
            int leaveNumber{0};
            friend void to_json(nlohmann::json& j, const TimePoint& v);
            friend void from_json(const nlohmann::json& j, TimePoint& v);
        };

        struct ResData {
            size_t totalCount{0};
            std::vector<TimePoint> numberList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryPassengerFlowNumberSend& v);
    void from_json(const nlohmann::json& j, MsgQueryPassengerFlowNumberSend& v);
}  // namespace Event
}  // namespace cosmo
