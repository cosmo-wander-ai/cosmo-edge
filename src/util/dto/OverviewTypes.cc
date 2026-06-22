// OverviewTypes — Media/overview types — MsgOverviewMem, MsgOverviewFile, MsgOverviewDebugInfo,...

#include "OverviewTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgOverviewMem& m) {
    j["name"] = m.name;
    if (MsgOverviewMemDataType::MsgOverviewMemDataTypeAIData == m.type) {
        j["aiFrames"] = m.aiFrames;
    } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypeAlarm == m.type) {
        j["alarms"] = m.alarms;
    } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypeAiFilter == m.type) {
        j["aiFilters"] = m.aiFilters;
    } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypeSensitity == m.type) {
        j["sensititys"] = m.sensititys;
    } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypePosSaveSensitity == m.type) {
        j["posSaveSens"] = m.posSaveSens;
    } else {
        j["name"] = m.name;
    }
}

void from_json(const nlohmann::json& j, MsgOverviewMem& m) {
    JSON_OPT(j, m, name);
    JSON_OPT(j, m, aiFrames);
    JSON_OPT(j, m, alarms);
    JSON_OPT(j, m, aiFilters);
    JSON_OPT(j, m, sensititys);
    JSON_OPT(j, m, posSaveSens);
    JSON_OPT(j, m, params);
}

void to_json(nlohmann::json& j, const MsgQueryTaskOverviewFileRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["taskId"] = r.taskId;
}

void from_json(const nlohmann::json& j, MsgQueryTaskOverviewFileRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    JSON_OPT(j, r, taskId);
}

void to_json(nlohmann::json& j, const MsgQueryTaskOverviewFileSend& s) {
    to_json(j, static_cast<const MsgSendHead&>(s));
    j["taskId"]    = s.taskId;
    j["index"]     = s.index;
    j["pts"]       = s.pts;
    j["frameSize"] = s.frameSize;
    j["streamUrl"] = s.streamUrl;
    j["type"]      = static_cast<int>(s.type);
    if (MsgTaskOverviewFileType::MsgTaskOverviewFileTypeVod == s.type) {
        j["files"] = s.files;
    } else if (MsgTaskOverviewFileType::MsgTaskOverviewFileTypeLive == s.type) {
        j["liveDatas"] = s.liveDatas;
    } else {
        j["files"] = s.files;
    }
}

void from_json(const nlohmann::json& j, MsgQueryTaskOverviewFileSend& s) {
    from_json(j, static_cast<MsgSendHead&>(s));
    JSON_OPT(j, s, taskId);
    JSON_OPT(j, s, index);
    JSON_OPT(j, s, pts);
    JSON_OPT(j, s, frameSize);
    JSON_OPT(j, s, streamUrl);
    if (auto it = j.find("type"); it != j.end() && !it->is_null())
        s.type = static_cast<MsgTaskOverviewFileType>(it->get<int>());
    JSON_OPT(j, s, files);
    JSON_OPT(j, s, liveDatas);
}

void from_json(const nlohmann::json& j, MsgOverviewFile& v) {
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, base64Content);
}

void to_json(nlohmann::json& j, const MsgOverviewFile& v) {
    j["fileName"]      = v.fileName;
    j["base64Content"] = v.base64Content;
}

void from_json(const nlohmann::json& j, MsgAlarmVideoOverviewInfoExtra& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, area);
    JSON_OPT(j, v, infos);
}

void to_json(nlohmann::json& j, const MsgAlarmVideoOverviewInfoExtra& v) {
    j["algorithmCode"] = v.algorithmCode;
    j["area"]          = v.area;
    j["infos"]         = v.infos;
}

}  // namespace cosmo
