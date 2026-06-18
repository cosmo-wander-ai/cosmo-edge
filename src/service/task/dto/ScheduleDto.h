// Schedule DTO definitions (extracted from MessageScheduleHandler.h)

#pragma once

#include <system_error>

#include "service/task/dto/ScheduleMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::Schedule {
struct MsgAddRecv : public MsgRecvHead, public MsgScheduleTemplate {};

void to_json(nlohmann::json& j, const MsgAddRecv& v);
void from_json(const nlohmann::json& j, MsgAddRecv& v);

// Add
struct MsgAddSend : public MsgSendHead {
    struct ResData {
        std::string id;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgAddSend& v);
void from_json(const nlohmann::json& j, MsgAddSend& v);

struct MsgUpdateRecv : public MsgRecvHead, public MsgScheduleTemplate {};

void to_json(nlohmann::json& j, const MsgUpdateRecv& v);
void from_json(const nlohmann::json& j, MsgUpdateRecv& v);

// Modify
struct MsgUpdateSend : public MsgSendHead {};

struct MsgPageRecv : public MsgRecvHead {
    std::string groupId;
    int pageNum{1};
    int pageSize{10};
};

void to_json(nlohmann::json& j, const MsgPageRecv& v);
void from_json(const nlohmann::json& j, MsgPageRecv& v);

// Query
struct MsgPageSend : public MsgSendHead {
    struct ResData {
        size_t total{0};
        std::vector<MsgScheduleTemplate> rows;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgPageSend& v);
void from_json(const nlohmann::json& j, MsgPageSend& v);

struct MsgSelectScheduleInfoRecv : public MsgRecvHead {};

// Query
struct MsgSelectScheduleInfoSend : public MsgSendHead {
    struct ResData {
        size_t total{0};
        std::vector<MsgScheduleTemplate> rows;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgSelectScheduleInfoSend& v);
void from_json(const nlohmann::json& j, MsgSelectScheduleInfoSend& v);

struct MsgDeleteRecv : public MsgRecvHead {
    std::string scheduleId;
};

void to_json(nlohmann::json& j, const MsgDeleteRecv& v);
void from_json(const nlohmann::json& j, MsgDeleteRecv& v);

// Delete
struct MsgDeleteSend : public MsgSendHead {};
}  // namespace cosmo::Schedule
