#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

// Time fetch request
struct MsgQueryTimeRecv : public MsgRecvHead {};

// Time fetch response
struct MsgQueryTimeSend : public MsgSendHead {
    struct ZoneInfo {
        std::string name;
        std::string value;
        int id;
        friend void to_json(nlohmann::json& j, const ZoneInfo& v);
        friend void from_json(const nlohmann::json& j, ZoneInfo& v);
    };

    struct Ntp {
        int enable{0};
        std::string server;
        int interval{60};
        short port{123};
        friend void to_json(nlohmann::json& j, const Ntp& v);
        friend void from_json(const nlohmann::json& j, Ntp& v);
    };

    struct TimeStatus {
        int64_t timestamp{0};
        std::string timeString{"0000-00-00 00:00:00"};
        std::string timeZoneValue{"+08:00"};
        int timeZoneId{75};
        Ntp ntp;
        friend void to_json(nlohmann::json& j, const TimeStatus& v);
        friend void from_json(const nlohmann::json& j, TimeStatus& v);
    };

    struct ResData {
        TimeStatus timeStatus;
        std::vector<ZoneInfo> zoneInfoList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryTimeSend& v);
void from_json(const nlohmann::json& j, MsgQueryTimeSend& v);

// NTP time sync request
struct MsgNTPDateRecv : public MsgRecvHead {
    int ntpEnable{-1};
    std::string ntpServer;
    short ntpPort{123};
    int interval{60};
    int timeZoneId{75};
};

void to_json(nlohmann::json& j, const MsgNTPDateRecv& v);
void from_json(const nlohmann::json& j, MsgNTPDateRecv& v);

// NTP time sync response
struct MsgNTPDateSend : public MsgSendHead {
    struct ResData {
        int status{1};
        std::string statusMsg;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgNTPDateSend& v);
void from_json(const nlohmann::json& j, MsgNTPDateSend& v);

// Time set request
struct MsgModifyTimeRecv : public MsgRecvHead {
    int64_t timestamp{0};
    int timeZoneId{75};
};

void to_json(nlohmann::json& j, const MsgModifyTimeRecv& v);
void from_json(const nlohmann::json& j, MsgModifyTimeRecv& v);

// Time set response
struct MsgModifyTimeSend : public MsgSendHead {};

}  // namespace cosmo::System
