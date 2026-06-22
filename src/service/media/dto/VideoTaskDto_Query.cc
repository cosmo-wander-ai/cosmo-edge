// VideoTaskDto_Query — Video Task Dto_ Query implementation.

#include <nlohmann/json.hpp>

#include "VideoTaskDto.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Query, channel and batch operations serialization (split from VideoTaskDto.cc)
namespace cosmo::VideoTask {

void to_json(nlohmann::json& j, const MsgSelectAllAlgorithmInfoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["channelId"] = v.channelId;
}

void from_json(const nlohmann::json& j, MsgSelectAllAlgorithmInfoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("channelId").get_to(v.channelId);
}

void to_json(nlohmann::json& j, const MsgSelectAllAlgorithmInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgSelectAllAlgorithmInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgListChannelRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmId"] = v.algorithmId;
}

void from_json(const nlohmann::json& j, MsgListChannelRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("algorithmId").get_to(v.algorithmId);
}

void to_json(nlohmann::json& j, const MsgListChannelSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgListChannelSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgApplyParamsBatchRecv& v) {
    to_json(j, static_cast<const MsgSaveOrUpdateRecv&>(v));
    j["targetChannelIds"] = v.targetChannelIds;
}

void from_json(const nlohmann::json& j, MsgApplyParamsBatchRecv& v) {
    from_json(j, static_cast<MsgSaveOrUpdateRecv&>(v));
    j.at("targetChannelIds").get_to(v.targetChannelIds);
}

void to_json(nlohmann::json& j, const MsgApplyParamsBatchSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgApplyParamsBatchSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgRunningDetailRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["tasks"] = v.tasks;
}

void from_json(const nlohmann::json& j, MsgRunningDetailRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, tasks);
}

void to_json(nlohmann::json& j, const MsgRunningDetailSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgRunningDetailSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, SelectAllAlgorithmInfoCategoryInfo& v) {
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, blanceCount);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmCategoryName);
    JSON_OPT(j, v, algorithmUsage);
}

void to_json(nlohmann::json& j, const SelectAllAlgorithmInfoCategoryInfo& v) {
    j["algorithmId"]           = v.algorithmId;
    j["algorithmName"]         = v.algorithmName;
    j["blanceCount"]           = v.blanceCount;
    j["algorithmCode"]         = v.algorithmCode;
    j["algorithmCategory"]     = v.algorithmCategory;
    j["algorithmCategoryName"] = v.algorithmCategoryName;
    j["algorithmUsage"]        = v.algorithmUsage;
}

void from_json(const nlohmann::json& j, SelectAllAlgorithmInfoCategory& v) {
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmCategoryName);
    JSON_OPT(j, v, simpleAlgorithmInfos);
}

void to_json(nlohmann::json& j, const SelectAllAlgorithmInfoCategory& v) {
    j["algorithmCategory"]     = v.algorithmCategory;
    j["algorithmCategoryName"] = v.algorithmCategoryName;
    j["simpleAlgorithmInfos"]  = v.simpleAlgorithmInfos;
}

void from_json(const nlohmann::json& j, MsgSelectAllAlgorithmInfoSend::ResData& v) {
    JSON_OPT(j, v, algorithmIds);
    JSON_OPT(j, v, algorithmList);
}

void to_json(nlohmann::json& j, const MsgSelectAllAlgorithmInfoSend::ResData& v) {
    j["algorithmIds"]  = v.algorithmIds;
    j["algorithmList"] = v.algorithmList;
}

void from_json(const nlohmann::json& j, MsgListChannelItem& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, channelId);
    JSON_OPT(j, v, channelName);
}

void to_json(nlohmann::json& j, const MsgListChannelItem& v) {
    j["id"]          = v.id;
    j["channelId"]   = v.channelId;
    j["channelName"] = v.channelName;
}

void from_json(const nlohmann::json& j, MsgApplyParamsBatchSend::ResData& v) {
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgApplyParamsBatchSend::ResData& v) {
    j["failedList"] = v.failedList;
}

void from_json(const nlohmann::json& j, MsgRunningDetailSend::ResData& v) {
    JSON_OPT(j, v, status);
}

void to_json(nlohmann::json& j, const MsgRunningDetailSend::ResData& v) {
    j["status"] = v.status;
}

}  // namespace cosmo::VideoTask
