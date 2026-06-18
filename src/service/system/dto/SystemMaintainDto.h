#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

// Restart/Reset request
struct MsgResetSystemRecv : public MsgRecvHead {
    int resetOperation{0};
};

void to_json(nlohmann::json& j, const MsgResetSystemRecv& v);
void from_json(const nlohmann::json& j, MsgResetSystemRecv& v);

// Restart/Reset response
struct MsgResetSystemSend : public MsgSendHead {
    struct ResData {
        int waitSeconds{10};
        std::string locationUrl;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgResetSystemSend& v);
void from_json(const nlohmann::json& j, MsgResetSystemSend& v);

// Config/Log export request
struct MsgExportFileRecv : public MsgRecvHead {
    int exportType{-1};
};

void to_json(nlohmann::json& j, const MsgExportFileRecv& v);
void from_json(const nlohmann::json& j, MsgExportFileRecv& v);

// Config/Log export response
struct MsgExportFileSend : public MsgSendHead {
    struct ResData {
        std::string fileName;
        std::string fileUrl;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgExportFileSend& v);
void from_json(const nlohmann::json& j, MsgExportFileSend& v);

struct MsgUpgradeRecv : public MsgRecvHead {
    std::string contentLength;
    std::string fileName;
    std::string filePath;
    std::string fileUrl;
};

void to_json(nlohmann::json& j, const MsgUpgradeRecv& v);
void from_json(const nlohmann::json& j, MsgUpgradeRecv& v);

//
struct MsgUpgradeSend : public MsgSendHead {};

// Document download address request
struct MsgQueryDocumentUrlRecv : public MsgRecvHead {
    int type{0};
};

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlRecv& v);
void from_json(const nlohmann::json& j, MsgQueryDocumentUrlRecv& v);

// Document download address response
struct MsgQueryDocumentUrlSend : public MsgSendHead {
    struct ResData {
        std::string url;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlSend& v);
void from_json(const nlohmann::json& j, MsgQueryDocumentUrlSend& v);

}  // namespace cosmo::System
