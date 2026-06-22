// AudioDto — Audio DTO definitions (extracted from MessageAudioHandler.h)

#include "AudioDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::Audio {
void to_json(nlohmann::json& j, const MsgQueryAudioFileRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["pageNum"]       = v.pageNum;
    j["pageSize"]      = v.pageSize;
    j["fileName"]      = v.fileName;
    j["keepFileTypes"] = v.keepFileTypes;
}

void from_json(const nlohmann::json& j, MsgQueryAudioFileRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, keepFileTypes);
}

void to_json(nlohmann::json& j, const MsgQueryAudioFileSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryAudioFileSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteAudioFileRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["fileIdList"] = v.fileIdList;
}

void from_json(const nlohmann::json& j, MsgDeleteAudioFileRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("fileIdList").get_to(v.fileIdList);
}

void to_json(nlohmann::json& j, const MsgDeleteAudioFileSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDeleteAudioFileSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyAudioDeviceRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devOperation"] = v.devOperation;
    j["audioDev"]     = v.audioDev;
}

void from_json(const nlohmann::json& j, MsgModifyAudioDeviceRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("devOperation").get_to(v.devOperation);
    j.at("audioDev").get_to(v.audioDev);
}

void to_json(nlohmann::json& j, const MsgModifyAudioDeviceSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgModifyAudioDeviceSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryAudioDeviceRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["pageNum"]    = v.pageNum;
    j["pageSize"]   = v.pageSize;
    j["name"]       = v.name;
    j["checkAlive"] = v.checkAlive;
}

void from_json(const nlohmann::json& j, MsgQueryAudioDeviceRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, checkAlive);
}

void to_json(nlohmann::json& j, const MsgQueryAudioDeviceSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryAudioDeviceSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteAudioDeviceRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devIdList"] = v.devIdList;
}

void from_json(const nlohmann::json& j, MsgDeleteAudioDeviceRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("devIdList").get_to(v.devIdList);
}

void to_json(nlohmann::json& j, const MsgTestAudioDeviceRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["operation"] = v.operation;
    j["data"]      = v.data;
    j["devSn"]     = v.devSn;
    j["volume"]    = v.volume;
    j["duration"]  = v.duration;
    j["times"]     = v.times;
    j["gap"]       = v.gap;
    j["speed"]     = v.speed;
    j["tone"]      = v.tone;
}

void from_json(const nlohmann::json& j, MsgTestAudioDeviceRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("operation").get_to(v.operation);
    j.at("data").get_to(v.data);
    j.at("devSn").get_to(v.devSn);
    JSON_OPT(j, v, volume);
    JSON_OPT(j, v, duration);
    JSON_OPT(j, v, times);
    JSON_OPT(j, v, gap);
    JSON_OPT(j, v, speed);
    JSON_OPT(j, v, tone);
}

void to_json(nlohmann::json& j, const MsgDeleteAudioDeviceSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDeleteAudioDeviceSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgQueryAudioFileSend::AudioFile& v) {
    JSON_OPT(j, v, fileId);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgQueryAudioFileSend::AudioFile& v) {
    j["fileId"]    = v.fileId;
    j["fileName"]  = v.fileName;
    j["timestamp"] = v.timestamp;
    j["filePath"]  = v.filePath;
}

void from_json(const nlohmann::json& j, MsgQueryAudioFileSend::ResData& v) {
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, audioFileList);
}

void to_json(nlohmann::json& j, const MsgQueryAudioFileSend::ResData& v) {
    j["totalCount"]    = v.totalCount;
    j["audioFileList"] = v.audioFileList;
}

void from_json(const nlohmann::json& j, MsgDeleteAudioFileSend::ResData& v) {
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgDeleteAudioFileSend::ResData& v) {
    j["failedList"] = v.failedList;
}

void from_json(const nlohmann::json& j, MsgBaseAudioDeviceInfo& v) {
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, ethName);
}

void to_json(nlohmann::json& j, const MsgBaseAudioDeviceInfo& v) {
    j["devId"]   = v.devId;
    j["name"]    = v.name;
    j["ip"]      = v.ip;
    j["ethName"] = v.ethName;
}

void from_json(const nlohmann::json& j, MsgModifyAudioDeviceSend::ResData& v) {
    JSON_OPT(j, v, devId);
}

void to_json(nlohmann::json& j, const MsgModifyAudioDeviceSend::ResData& v) {
    j["devId"] = v.devId;
}

void from_json(const nlohmann::json& j, MsgQueryAudioDeviceSend::AudioDevice& v) {
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, online);
    JSON_OPT(j, v, ethName);
}

void to_json(nlohmann::json& j, const MsgQueryAudioDeviceSend::AudioDevice& v) {
    j["devId"]     = v.devId;
    j["name"]      = v.name;
    j["ip"]        = v.ip;
    j["timestamp"] = v.timestamp;
    j["online"]    = v.online;
    j["ethName"]   = v.ethName;
}

void from_json(const nlohmann::json& j, MsgQueryAudioDeviceSend::ResData& v) {
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, audioDevList);
}

void to_json(nlohmann::json& j, const MsgQueryAudioDeviceSend::ResData& v) {
    j["totalCount"]   = v.totalCount;
    j["audioDevList"] = v.audioDevList;
}

void from_json(const nlohmann::json& j, MsgDeleteAudioDeviceSend::ResData& v) {
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgDeleteAudioDeviceSend::ResData& v) {
    j["failedList"] = v.failedList;
}

}  // namespace cosmo::Audio
