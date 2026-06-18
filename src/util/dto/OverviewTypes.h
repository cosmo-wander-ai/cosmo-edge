// Media/overview types — MsgOverviewMem, MsgOverviewFile, MsgOverviewDebugInfo, etc.
// Shared DTO types used across service, flow, infer, and api layers.

#pragma once

#include "util/dto/FilterTypes.h"

namespace cosmo {

struct MsgOverviewFile {
    std::string fileName;
    std::string base64Content;
    friend void to_json(nlohmann::json& j, const MsgOverviewFile& v);
    friend void from_json(const nlohmann::json& j, MsgOverviewFile& v);
};

enum class MsgOverviewMemDataType {
    MsgOverviewMemDataTypeNone = 0,
    MsgOverviewMemDataTypeAIData,
    MsgOverviewMemDataTypeSensitity,
    MsgOverviewMemDataTypePosSaveSensitity,
    MsgOverviewMemDataTypeAlarm,
    MsgOverviewMemDataTypeAiFilter

    ,
    MsgOverviewMemDataTypeParams,
    MsgOverviewMemDataTypeMax  //
};

struct MsgOverviewMem {
    std::string name;
    MsgOverviewMemDataType type{MsgOverviewMemDataType::MsgOverviewMemDataTypeNone};
    std::vector<MsgRecSensitity> sensititys;
    std::vector<MsgRecAlarm> alarms;
    std::vector<MsgAiDetFrame> aiFrames;
    std::vector<MsgAiFilterFrame> aiFilters;
    std::vector<MsgRecPosSaveSensitity> posSaveSens;
    MsgTaskConfig params;
};

// Conditional: CCM/CEC/CE by type
void to_json(nlohmann::json& j, const MsgOverviewMem& m);
void from_json(const nlohmann::json& j, MsgOverviewMem& m);

struct MsgAlarmVideoOverviewInfoExtra {
    std::string algorithmCode;
    MsgTaskArea area;
    std::vector<MsgOverviewMem> infos;
    friend void to_json(nlohmann::json& j, const MsgAlarmVideoOverviewInfoExtra& v);
    friend void from_json(const nlohmann::json& j, MsgAlarmVideoOverviewInfoExtra& v);
};

struct MsgQueryTaskOverviewFileRecv : public MsgRecvHead {
    std::string taskId;
};
void to_json(nlohmann::json& j, const MsgQueryTaskOverviewFileRecv& r);
void from_json(const nlohmann::json& j, MsgQueryTaskOverviewFileRecv& r);

enum class MsgTaskOverviewFileType {
    MsgTaskOverviewFileTypeVod = 0,
    MsgTaskOverviewFileTypeLive

    ,
    MsgTaskOverviewFileTypeMax  //
};
struct MsgQueryTaskOverviewFileSend : public MsgSendHead {
    std::string taskId;
    int64_t index{-1};
    int64_t pts{-1};
    int64_t frameSize{-1};
    std::string streamUrl;
    MsgTaskOverviewFileType type{MsgTaskOverviewFileType::MsgTaskOverviewFileTypeVod};
    std::vector<MsgOverviewFile> files;
    std::vector<MsgOverviewMem> liveDatas;
};

// Conditional: CCM/CEC/CE by type
void to_json(nlohmann::json& j, const MsgQueryTaskOverviewFileSend& s);
void from_json(const nlohmann::json& j, MsgQueryTaskOverviewFileSend& s);

struct MsgOverviewDebugInfo {
    int64_t recvFrames{0};
    int64_t sendFrames{0};
};

}  // namespace cosmo
