// Client-side algorithm orchestration message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/AlgorithmMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

// Get algorithm orchestration config request
struct CMsgAlgorithmProcessConfigNGReq : public MsgRecvHead {
    std::string algorithmCode;
    std::string devId;  // Device ID
};

void to_json(nlohmann::json& j, const CMsgAlgorithmProcessConfigNGReq& v);
void from_json(const nlohmann::json& j, CMsgAlgorithmProcessConfigNGReq& v);

struct CMsgAlgorithmProcessConfigNGRsp : public MsgSendHead {
    ActionAlg resData;
};

void to_json(nlohmann::json& j, const CMsgAlgorithmProcessConfigNGRsp& v);
void from_json(const nlohmann::json& j, CMsgAlgorithmProcessConfigNGRsp& v);

// Get atomic model list for algorithm orchestration
struct CMsgGetAtomicCodeListReq : public MsgRecvHead {
    std::string algorithmCode;
    std::string devId;  // Device ID
    std::string engineType;
};

void to_json(nlohmann::json& j, const CMsgGetAtomicCodeListReq& v);
void from_json(const nlohmann::json& j, CMsgGetAtomicCodeListReq& v);

struct AtomicList {
    std::string atomicCode;        // Atomic algorithm code
    std::string atomicName;        // Atomic algorithm name
    std::string atomicPacketName;  // Atomic algorithm package name
    std::string packetURL;         // Atomic algorithm download URL
    std::string atomicPacketPath;  // Atomic algorithm local path
};

void to_json(nlohmann::json& j, const AtomicList& v);
void from_json(const nlohmann::json& j, AtomicList& v);

struct CMsgGetAtomicCodeListRsp : public MsgSendHead {
    struct ResData {
        std::vector<AtomicList> atomicList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const CMsgGetAtomicCodeListRsp& v);
void from_json(const nlohmann::json& j, CMsgGetAtomicCodeListRsp& v);

// Picture algorithm list request
struct CMsgQueryPictureAlgorithmListReq : public MsgRecvHead {
    std::string devId;
};

void to_json(nlohmann::json& j, const CMsgQueryPictureAlgorithmListReq& v);
void from_json(const nlohmann::json& j, CMsgQueryPictureAlgorithmListReq& v);

struct PictureAlgorithmInfo {
    std::string algorithmCode;
    std::string algorithmName;
    std::string algorithmId;
    std::string algorithmCheckSum;
    std::string algorithmUpdateTime;
    friend void to_json(nlohmann::json& j, const PictureAlgorithmInfo& v);
    friend void from_json(const nlohmann::json& j, PictureAlgorithmInfo& v);
};

// Picture algorithm list response
struct CMsgQueryPictureAlgorithmListRsp : public MsgSendHead {
    struct ResData {
        std::string nodeAlgorithmCheckSum;
        std::vector<PictureAlgorithmInfo> list;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const CMsgQueryPictureAlgorithmListRsp& v);
void from_json(const nlohmann::json& j, CMsgQueryPictureAlgorithmListRsp& v);

}  // namespace cosmo
